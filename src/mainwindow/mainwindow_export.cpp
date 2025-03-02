/*******************************************************************
skw
Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#include <QtCore>

#include <QSvgGenerator>
#include <QColor>
#include <QImageWriter>
#include <QPrinter>
#include <QSettings>
#include <QDesktopServices>
#include <QPrintDialog>
#include <QClipboard>
#include <QApplication>

#include "mainwindow.h"
#include "../debugdialog.h"
#include "../waitpushundostack.h"
#include "../help/aboutbox.h"
#include "../autoroute/autorouteprogressdialog.h"
#include "../items/virtualwire.h"
#include "../items/jumperitem.h"
#include "../items/via.h"
#include "../fsvgrenderer.h"
#include "../items/note.h"
#include "../items/partfactory.h"
#include "../eagle/fritzing2eagle.h"
#include "../sketch/breadboardsketchwidget.h"
#include "../sketch/schematicsketchwidget.h"
#include "../sketch/pcbsketchwidget.h"
#include "../partsbinpalette/binmanager/binmanager.h"
#include "../utils/expandinglabel.h"
#include "../infoview/htmlinfoview.h"
#include "../utils/bendpointaction.h"
#include "../sketch/fgraphicsscene.h"
#include "../utils/fileprogressdialog.h"
#include "../svg/svgfilesplitter.h"
#include "../version/version.h"
#include "../help/tipsandtricks.h"
#include "../dialogs/setcolordialog.h"
#include "../dialogs/exportparametersdialog.h"
#include "../utils/folderutils.h"
#include "../utils/graphicsutils.h"
#include "../utils/textutils.h"
#include "../connectors/ercdata.h"
#include "../items/moduleidnames.h"
#include "../utils/zoomslider.h"
#include "../dock/layerpalette.h"
#include "../program/programwindow.h"
#include "../utils/autoclosemessagebox.h"
#include "../svg/gerbergenerator.h"
#include "../processeventblocker.h"
#include "../items/propertydef.h"

static QString eagleActionType = ".eagle";
static QString gerberActionType = ".gerber";
static QString jpgActionType = ".jpg";
static QString pdfActionType = ".pdf";
static QString pngActionType = ".png";
static QString svgActionType = ".svg";
static QString bomActionType = ".html";
static QString netlistActionType = ".xml";
static QString spiceNetlistActionType = ".cir";

static QHash<QString, QPrinter::OutputFormat> filePrintFormats;
static QHash<QString, QImage::Format> fileExportFormats;
static QHash<QString, QString> fileExtFormats;

static QRegExp AaCc("[aAcC]");
static QRegExp LabelNumber("([^\\d]+)(.*)");

static const double InchesPerMeter = 39.3700787;

////////////////////////////////////////////////////////

bool sortPartList(ItemBase * b1, ItemBase * b2) {
	bool result = b1->instanceTitle().toLower() < b2->instanceTitle().toLower();

	int ix1 = LabelNumber.indexIn(b1->instanceTitle());
	if (ix1 < 0) return result;

	QString label1 = LabelNumber.cap(1);
	QString number1 = LabelNumber.cap(2);

	int ix2 = LabelNumber.indexIn(b2->instanceTitle());
	if (ix2 < 0) return result;

	QString label2 = LabelNumber.cap(1);
	QString number2 = LabelNumber.cap(2);
	if (label2.compare(label1, Qt::CaseInsensitive) != 0) return result;

	bool ok;
	double d1 = number1.toDouble(&ok);
	if (!ok) return result;

	double d2 = number2.toDouble(&ok);
	if (!ok) return result;

	return d1 < d2;
}

/////////////////////////////////////////////////////////

void MainWindow::initNames()
{
	OtherKnownExtensions << jpgActionType << pdfActionType << pngActionType << svgActionType << bomActionType << netlistActionType << spiceNetlistActionType;

	filePrintFormats[pdfActionType] = QPrinter::PdfFormat;

	fileExportFormats[pngActionType] = QImage::Format_ARGB32;
	fileExportFormats[jpgActionType] = QImage::Format_RGB32;

	fileExtFormats[pdfActionType] = tr("PDF (*.pdf)");
	fileExtFormats[pngActionType] = tr("PNG Image (*.png)");
	fileExtFormats[jpgActionType] = tr("JPEG Image (*.jpg)");
	fileExtFormats[svgActionType] = tr("SVG Image (*.svg)");
	fileExtFormats[bomActionType] = tr("BoM Text File (*.html)");

	QSettings settings;
	AutosaveEnabled = settings.value("autosaveEnabled", QString("%1").arg(AutosaveEnabled)).toBool();
	AutosaveTimeoutMinutes = settings.value("autosavePeriod", QString("%1").arg(AutosaveTimeoutMinutes)).toInt();
}

void MainWindow::print() {
	if (m_currentWidget->contentView() == m_programView) {
		m_programView->print();
	}

	if (m_currentGraphicsView == NULL) return;

#ifndef QT_NO_PRINTER
	QPrinter printer(QPrinter::HighResolution);

	QPrintDialog *printDialog = new QPrintDialog(&printer, this);
	if (printDialog->exec() == QDialog::Accepted) {
		m_statusBar->showMessage(tr("Printing..."));
		printAux(printer, true, true);
		m_statusBar->showMessage(tr("Ready"), 2000);
	} else {
		return;
	}
#endif
}

void MainWindow::exportEtchable() {
	if (sender() == NULL) return;

	bool wantSvg = sender()->property("svg").toBool();
	exportEtchable(!wantSvg, wantSvg);
}


void MainWindow::exportEtchable(bool wantPDF, bool wantSVG)
{
	int boardCount;
	ItemBase * board = m_pcbGraphicsView->findSelectedBoard(boardCount);
	if (boardCount == 0) {
		QMessageBox::critical(this, tr("Fritzing"),
		                      tr("Your sketch does not have a board yet! Please add a PCB in order to export etchable."));
		return;
	}
	if (board == NULL) {
		QMessageBox::critical(this, tr("Fritzing"),
		                      tr("Etchable export can only handle one board at a time--please select the board you want to export."));
		return;
	}

	RoutingStatus routingStatus;
	m_pcbGraphicsView->updateRoutingStatus(NULL, routingStatus, true);
	if (routingStatus.m_connectorsLeftToRoute > 0) {
		QMessageBox msgBox(this);
		msgBox.setWindowModality(Qt::WindowModal);
		msgBox.setText(tr("All traces have not yet been routed."));
		msgBox.setInformativeText(tr("Do you want to proceed anyway?"));
		msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
		msgBox.button(QMessageBox::Yes)->setText(tr("Proceed"));
		msgBox.button(QMessageBox::No)->setText(tr("Cancel"));
		msgBox.setDefaultButton(QMessageBox::Yes);
		int ret = msgBox.exec();
		if (ret != QMessageBox::Yes) return;
	}

	QString path = defaultSaveFolder();
	QString extFmt = (wantPDF) ? fileExtFormats.value(pdfActionType) : fileExtFormats.value(svgActionType);
	QString fileExt = extFmt;

	QString suffix = (wantPDF) ? pdfActionType : svgActionType;
	QString prefix = "";
	if (boardCount > 1) {
		prefix = QString("%1_%2_").arg(board->instanceTitle()).arg(board->id());
	}

	QString exportDir = QFileDialog::getExistingDirectory(this, tr("Choose a folder for exporting"),
	                    defaultSaveFolder(),
	                    QFileDialog::ShowDirsOnly
	                    | QFileDialog::DontResolveSymlinks);
	if (exportDir.isEmpty()) return;

	FolderUtils::setOpenSaveFolder(exportDir);
	FileProgressDialog * fileProgressDialog = exportProgress();

	QRectF r = board->sceneBoundingRect();
	QSizeF boardImageSize(r.width(), r.height());

	QStringList fileNames;

	fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_copper_bottom%1", suffix));
	fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_mask_bottom%1", suffix));
	fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_paste_mask_bottom%1", suffix));
	if (m_pcbGraphicsView->boardLayers() > 1) {
		fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_copper_top%1", suffix));
		fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_mask_top%1", suffix));
		fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_paste_mask_top%1", suffix));
	}
	fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_silk_top%1", suffix));
	fileNames.append(exportDir + "/" + constructFileName(prefix + "etch_silk_bottom%1", suffix));

	QString maskTop, maskBottom;
	QList<ItemBase *> copperLogoItems, holes;
	for (int ix = 0; ix < fileNames.count(); ix++) {
		bool doMask = false;
		bool doSilk = false;
		bool doPaste = false;
		QString fileName = fileNames[ix];
		LayerList viewLayerIDs;
		if (fileName.contains("copper_bottom")) {
			viewLayerIDs << ViewLayer::GroundPlane0 << ViewLayer::Copper0 << ViewLayer::Copper0Trace;
		}
		else if (fileName.contains("mask_bottom")) {
			doMask = true;
			viewLayerIDs << ViewLayer::Copper0;
			doPaste = fileName.contains("paste");
		}
		else if (fileName.contains("copper_top")) {
			viewLayerIDs << ViewLayer::GroundPlane1 << ViewLayer::Copper1 << ViewLayer::Copper1Trace;
		}
		else if (fileName.contains("mask_top")) {
			viewLayerIDs << ViewLayer::Copper1;
			doMask = true;
			doPaste = fileName.contains("paste");
		}
		else if (fileName.contains("silk_top")) {
			viewLayerIDs << ViewLayer::Silkscreen1 << ViewLayer::Silkscreen1Label;
			doSilk = true;
		}
		else if (fileName.contains("silk_bottom")) {
			viewLayerIDs << ViewLayer::Silkscreen0 << ViewLayer::Silkscreen0Label;
			doSilk = true;
		}

		if (doMask) {
			m_pcbGraphicsView->hideCopperLogoItems(copperLogoItems);
		}
		if (doPaste) {
			m_pcbGraphicsView->hideHoles(holes);
		}

		if (wantSVG) {
			RenderThing renderThing;
			renderThing.printerScale = GraphicsUtils::SVGDPI;
			renderThing.blackOnly = true;
			renderThing.dpi = GraphicsUtils::IllustratorDPI;
			renderThing.hideTerminalPoints = true;
			renderThing.selectedItems = renderThing.renderBlocker = false;
			QString svg = m_pcbGraphicsView->renderToSVG(renderThing, board, viewLayerIDs);
			massageOutput(svg, doMask, doSilk, doPaste, maskTop, maskBottom, fileName, board, GraphicsUtils::IllustratorDPI, viewLayerIDs);
			QString merged = mergeBoardSvg(svg, board, GraphicsUtils::IllustratorDPI, false, viewLayerIDs);
			TextUtils::writeUtf8(fileName.arg(""), merged);
			merged = mergeBoardSvg(svg, board, GraphicsUtils::IllustratorDPI, true, viewLayerIDs);
			TextUtils::writeUtf8(fileName.arg("_mirror"), merged);
		}
		else {
			QString svg;
			QList<bool> flips;
			flips << false << true;
			foreach (bool flip, flips) {
				QString mirror = flip ? "_mirror" : "";
				QPrinter printer(QPrinter::HighResolution);
				printer.setOutputFormat(filePrintFormats[fileExt]);
				printer.setOutputFileName(fileName.arg(mirror));
				int res = printer.resolution();

				if (svg.isEmpty()) {
					RenderThing renderThing;
					renderThing.printerScale = GraphicsUtils::SVGDPI;
					renderThing.blackOnly = true;
					renderThing.dpi = res;
					renderThing.hideTerminalPoints = true;
					renderThing.selectedItems = renderThing.renderBlocker = false;
					svg = m_pcbGraphicsView->renderToSVG(renderThing, board, viewLayerIDs);
					massageOutput(svg, doMask, doSilk, doPaste, maskTop, maskBottom, fileName, board, res, viewLayerIDs);
				}

				QString merged = mergeBoardSvg(svg, board, res, flip, viewLayerIDs);

				// now convert to pdf
				QSvgRenderer svgRenderer;
				svgRenderer.load(merged.toLatin1());
				double trueWidth = boardImageSize.width() / GraphicsUtils::SVGDPI;
				double trueHeight = boardImageSize.height() / GraphicsUtils::SVGDPI;
				QRectF target(0, 0, trueWidth * res, trueHeight * res);

				QSizeF psize((target.width() + printer.pageLayout().fullRectPixels(printer.resolution()).width() - printer.width()) / res,
				             (target.height() + printer.pageLayout().fullRectPixels(printer.resolution()).height() - printer.height()) / res);
				QPageSize pageSize(psize, QPageSize::Inch);
				printer.setPageSize(pageSize);

				QPainter painter;
				if (painter.begin(&printer))
				{
					svgRenderer.render(&painter, target);
				}

				painter.end();
			}
		}
		if (doMask) {
			m_pcbGraphicsView->restoreCopperLogoItems(copperLogoItems);
		}
		if (doPaste) {
			m_pcbGraphicsView->restoreCopperLogoItems(holes);
		}

	}

	m_statusBar->showMessage(tr("Sketch exported"), 2000);
	delete fileProgressDialog;

	/*

		int width = m_pcbGraphicsView->width();
		if (m_pcbGraphicsView->verticalScrollBar()->isVisible()) {
			width -= m_pcbGraphicsView->verticalScrollBar()->width();
		}
		int height = m_pcbGraphicsView->height();
		if (m_pcbGraphicsView->horizontalScrollBar()->isVisible()) {
			height -= m_pcbGraphicsView->horizontalScrollBar()->height();
		}

		double trueWidth = width / m_printerScale;
		double trueHeight = height / m_printerScale;

		// set everything to a 1200 dpi resolution
		QSize imgSize(trueWidth * 1200, trueHeight * 1200);
		QImage image(imgSize, QImage::Format_RGB32);
		image.setDotsPerMeterX(1200 * GraphicsUtils::InchesPerMeter);
		image.setDotsPerMeterY(1200 * GraphicsUtils::InchesPerMeter);
		QPainter painter;

		QColor color;
		color = m_pcbGraphicsView->background();
		m_pcbGraphicsView->setBackground(QColor::fromRgb(255,255,255,255));

		m_pcbGraphicsView->scene()->clearSelection();
		m_pcbGraphicsView->saveLayerVisibility();
		m_pcbGraphicsView->setAllLayersVisible(false);
		m_pcbGraphicsView->setLayerVisible(ViewLayer::Copper0, true);
		m_pcbGraphicsView->hideConnectors(true);

		painter.begin(&image);
		m_pcbGraphicsView->render(&painter);
		painter.end();


		QSvgGenerator svgGenerator;
		svgGenerator.setFileName("c:/fritzing2/testsvggenerator.svg");
	    svgGenerator.setSize(QSize(width * 8, height * 8));
		QPainter svgPainter(&svgGenerator);
		m_pcbGraphicsView->render(&svgPainter);
		svgPainter.end();


		m_pcbGraphicsView->hideConnectors(false);
		m_pcbGraphicsView->setBackground(color);
		m_pcbGraphicsView->restoreLayerVisibility();
		// TODO: restore the selection

		QRgb black = 0;
		for (int x = 0; x < imgSize.width(); x++) {
			for (int y = 0; y < imgSize.height(); y++) {
				QRgb p = image.pixel(x, y);
				if (p != 0xffffffff) {
					image.setPixel(x, y, black);
				}
			}
		}

		bool result = image.save(fileName);
		if (!result) {
			QMessageBox::warning(this, tr("Fritzing"), tr("Unable to save %1").arg(fileName) );
		}

	*/

}

QString MainWindow::mergeBoardSvg(QString & svg, ItemBase * board, int res, bool flip, LayerList & viewLayerIDs) {
	QString boardSvg = getBoardSvg(board, res, viewLayerIDs);

	LayerList outlineLayerIDs = ViewLayer::outlineLayers();
	RenderThing renderThing;
	renderThing.printerScale = GraphicsUtils::SVGDPI;
	renderThing.blackOnly = true;
	renderThing.dpi = res;
	renderThing.hideTerminalPoints = true;
	renderThing.selectedItems = renderThing.renderBlocker = false;
	QString outlineSvg = m_pcbGraphicsView->renderToSVG(renderThing, board, outlineLayerIDs);
	outlineSvg = GerberGenerator::cleanOutline(outlineSvg);
	outlineSvg = TextUtils::slamStrokeAndFill(outlineSvg, "black", "0.5", "none");

	if (!boardSvg.isEmpty() && !outlineSvg.isEmpty()) {
		boardSvg = TextUtils::mergeSvg(boardSvg, outlineSvg, "", false);
	}
	else if (boardSvg.isEmpty()) {
		boardSvg = outlineSvg;
	}

	return TextUtils::convertExtendedChars(TextUtils::mergeSvg(boardSvg, svg, "", flip));
}

QString MainWindow::getBoardSvg(ItemBase * board, int res,  LayerList & viewLayerIDs) {
	if (board == NULL) return ___emptyString___;

	board = board->layerKinChief();
	QList<ItemBase *> boardLayers;
	boardLayers << board;
	foreach (ItemBase * lk, board->layerKin()) {
		boardLayers << lk;
	}

	bool gotOne = false;
	foreach (ItemBase * boardLayer, boardLayers) {
		if (viewLayerIDs.contains(boardLayer->viewLayerID())) {
			gotOne = true;
			break;
		}
	}

	if (!gotOne) return "";

	m_pcbGraphicsView->setIgnoreSelectionChangeEvents(true);

	QList<QGraphicsItem *> items = m_pcbGraphicsView->scene()->selectedItems();
	foreach (QGraphicsItem * item, items) {
		item->setSelected(false);
	}
	board->setSelected(true);

	RenderThing renderThing;
	renderThing.printerScale = GraphicsUtils::SVGDPI;
	renderThing.blackOnly = true;
	renderThing.dpi = res;
	renderThing.selectedItems = renderThing.hideTerminalPoints = true;
	renderThing.renderBlocker = false;
	QString svg = m_pcbGraphicsView->renderToSVG(renderThing, board, viewLayerIDs);
	board->setSelected(false);
	foreach (QGraphicsItem * item, items) {
		item->setSelected(true);
	}

	m_pcbGraphicsView->setIgnoreSelectionChangeEvents(false);

	return svg;
}


void MainWindow::doExport() {
	QAction * action = qobject_cast<QAction *>(sender());
	if (action == NULL) return;

	QString actionType = action->data().toString();
	QString path = defaultSaveFolder();

	if (actionType.compare(eagleActionType) == 0) {
		exportToEagle();
		return;
	}

	if (actionType.compare(gerberActionType) == 0) {
		exportToGerber();
		return;
	}

	if (actionType.compare(bomActionType) == 0) {
		exportBOM();
		return;
	}

	if (actionType.compare(netlistActionType) == 0) {
		exportNetlist();
		return;
	}

	if (actionType.compare(spiceNetlistActionType) == 0) {
		exportSpiceNetlist();
		return;
	}

	if (actionType.compare(svgActionType) == 0) {
		exportSvg(GraphicsUtils::IllustratorDPI, false, false);
		return;
	}

#ifndef QT_NO_PRINTER
	QString fileExt;
	QString extFmt = fileExtFormats.value(actionType);
	DebugDialog::debug(QString("file export string %1").arg(extFmt));
	QString fileName = FolderUtils::getSaveFileName(this,
	                   tr("Export..."),
	                   path+"/"+constructFileName("", actionType),
	                   extFmt,
	                   &fileExt
	                                               );

	if (fileName.isEmpty()) {
		return; //Cancel pressed
	} else {
		FileProgressDialog * fileProgressDialog = exportProgress();
		DebugDialog::debug(fileExt+" selected to export");
		if(!alreadyHasExtension(fileName, actionType)) {
			fileName += actionType;
		}

		if(filePrintFormats.contains(actionType)) { // PDF or PS
			QPrinter printer(QPrinter::HighResolution);
			printer.setOutputFormat(filePrintFormats[actionType]);
			printer.setOutputFileName(fileName);
			m_statusBar->showMessage(tr("Exporting..."));
			printAux(printer, true, false);
			m_statusBar->showMessage(tr("Sketch exported"), 2000);
		} else { // PNG...
			DebugDialog::debug(QString("format: %1 %2").arg(fileExt).arg(fileExportFormats[actionType]));
			int quality = (actionType == pngActionType ? 1 : 100);
			exportAux(fileName,fileExportFormats[actionType], quality, true);
		}
		delete fileProgressDialog;

	}
#endif
}

void MainWindow::exportAux(QString fileName, QImage::Format format, int quality, bool removeBackground)
{
	if (m_currentGraphicsView == NULL) return;

        int dpi = 3 * GraphicsUtils::SVGDPI;

        ExportParametersDialog parameters(dpi, this);
        parameters.setValue(dpi);
        int parametersResult = parameters.exec();
        if ( parametersResult == QDialog::Rejected )
        {
                return;
        }
        dpi = parameters.getDpi();

	QRectF source = prepareExport(removeBackground);

	double resMultiplier = dpi / GraphicsUtils::SVGDPI;

	QSize imgSize(source.width() * resMultiplier, source.height() * resMultiplier);
	QImage image(imgSize, format);
	image.setDotsPerMeterX(InchesPerMeter * dpi);
	image.setDotsPerMeterY(InchesPerMeter * dpi);
	if (removeBackground) {
		image.fill(QColor::fromRgb(255,255,255,255));
	} else {
		image.fill(m_currentGraphicsView->background());
	}

	QPainter painter;
	painter.begin(&image);
	QRectF target(0, 0, imgSize.width(), imgSize.height());
	transformPainter(painter, target.width());
	m_currentGraphicsView->scene()->render(&painter, target, source, Qt::KeepAspectRatio);
	painter.end();

	afterExport(removeBackground);

	QImageWriter imageWriter(fileName);
	if (imageWriter.supportsOption(QImageIOHandler::Description)) {
		imageWriter.setText("", TextUtils::CreatedWithFritzingString);
	}
	imageWriter.setQuality(quality);
	bool result = imageWriter.write(image);
	if (!result) {
		QMessageBox::warning(this, tr("Fritzing"), tr("Unable to save %1").arg(fileName) );
	}
}

void MainWindow::printAux(QPrinter &printer, bool removeBackground, bool paginate) {
	if (m_currentGraphicsView == NULL) return;

	int res = printer.resolution();
	double scale2 = res / GraphicsUtils::SVGDPI;
	DebugDialog::debug(QString("p.w:%1 p.h:%2 pager.w:%3 pager.h:%4 paperr.w:%5 paperr.h:%6 source.w:%7 source.h:%8")
	                   .arg(printer.width())
	                   .arg(printer.height())
	                   .arg(printer.pageLayout().paintRectPixels(printer.resolution()).width())
	                   .arg(printer.pageLayout().paintRectPixels(printer.resolution()).height())
	                   .arg(printer.pageLayout().fullRectPixels(printer.resolution()).width())
	                   .arg(printer.pageLayout().fullRectPixels(printer.resolution()).height())
	                   .arg(printer.width() / scale2)
	                   .arg(printer.height() / scale2) );

	QRectF source = prepareExport(removeBackground);
	DebugDialog::debug("items bounding rect", source);
	DebugDialog::debug("scene items bounding rect", m_currentGraphicsView->scene()->itemsBoundingRect());

	QRectF target(0, 0, source.width() * scale2, source.height() * scale2);

	if (!paginate) {
		QSizeF psize((target.width() + printer.pageLayout().fullRectPixels(printer.resolution()).width() - printer.width()) / res,
		             (target.height() + printer.pageLayout().fullRectPixels(printer.resolution()).height() - printer.height()) / res);
		QPageSize pageSize(psize, QPageSize::Inch);
		printer.setPageSize(pageSize);
	}

	QPainter painter;
	if (!painter.begin(&printer)) {
		afterExport(removeBackground);
		QMessageBox::warning(this, tr("Fritzing"), tr("Cannot print to %1").arg(printer.docName()));
		return;
	}

	if (paginate) {
		int xPages = qCeil(target.width() / printer.width());
		int yPages = qCeil(target.height() / printer.height());
		int lastPage = xPages * yPages;

		int xSourcePage = qFloor(printer.width() / scale2);
		int ySourcePage = qFloor(printer.height() / scale2);

		int page = 0;
		for (int iy = 0; iy < yPages; iy++) {
			for (int ix = 0; ix < xPages; ix++) {
				// render to printer:
				QRectF pSource((ix * xSourcePage) + source.left(),
				               (iy * ySourcePage) + source.top(),
				               qMin(xSourcePage, (int) source.width() - (ix * xSourcePage)),
				               qMin(ySourcePage, (int) source.height() - (iy * ySourcePage)));
				QRectF pTarget(0, 0, pSource.width() * scale2, pSource.height() * scale2);
				transformPainter(painter, pTarget.width());
				m_currentGraphicsView->scene()->render(&painter, pTarget, pSource, Qt::KeepAspectRatio);
				if (++page < lastPage) {
					printer.newPage();
				}
			}
		}
	}
	else {
		transformPainter(painter, target.width());
		m_currentGraphicsView->scene()->render(&painter, target, source, Qt::KeepAspectRatio);
	}

	afterExport(removeBackground);

	DebugDialog::debug(QString("source w:%1 h:%2 target w:%5 h:%6 pres:%3 screenres:%4")
	                   .arg(source.width())
	                   .arg(source.height()).arg(res).arg(this->physicalDpiX())
	                   .arg(target.width()).arg(target.height()) );

	//#ifndef QT_NO_CONCURRENT
	//QProgressDialog dialog;
	//dialog.setLabelText(message);
	//
	// Create a QFutureWatcher and conncect signals and slots.
	//QFutureWatcher<void> futureWatcher;
	//QObject::connect(&futureWatcher, SIGNAL(finished()), &dialog, SLOT(reset()));
	//QObject::connect(&dialog, SIGNAL(canceled()), &futureWatcher, SLOT(cancel()));
	//QObject::connect(&futureWatcher, SIGNAL(progressRangeChanged(int, int)), &dialog, SLOT(setRange(int, int)));
	//QObject::connect(&futureWatcher, SIGNAL(progressValueChanged(int)), &dialog, SLOT(setValue(int)));
	//
	// Start the computation.
	//futureWatcher.setFuture(QtConcurrent::run(painter,&QPainter::end));
	//dialog.exec();
	//
	//futureWatcher.waitForFinished();
	//#endif

	//#ifdef QT_NO_CONCURRENT
	painter.end();
	//#endif

}

void MainWindow::transformPainter(QPainter &painter, qreal width)
{
	if(m_currentGraphicsView->viewFromBelow()) {
	// m_currentGraphicsView->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
		QTransform transform;
		transform.translate(width, 0.0);
		transform.scale(-1, 1);
		painter.setTransform(transform, true);
	}
}

QRectF MainWindow::prepareExport(bool removeBackground)
{
	//Deselect all the items that are selected before creating the image
	m_selectedItems = m_currentGraphicsView->scene()->selectedItems();
	foreach(QGraphicsItem *item, m_selectedItems) {
		item->setSelected(false);
	}

	QRectF itemsBoundingRect;
	foreach(QGraphicsItem *item,  m_currentGraphicsView->scene()->items()) {
		if (!item->isVisible()) continue;

		item->update();
		itemsBoundingRect |= item->sceneBoundingRect();
	}

	QRectF source = itemsBoundingRect;
	m_watermark = m_currentGraphicsView->addWatermark(":resources/images/watermark_fritzing_outline.svg");
	if (m_watermark) {
		m_watermark->setPos(source.right() - m_watermark->boundingRect().width(), source.bottom());
		if(m_currentGraphicsView->viewFromBelow()) {
			QTransform transformScale;
			transformScale.scale(-1, 1);
			m_watermark->setTransformOriginPoint((m_watermark->boundingRect().left() + m_watermark->boundingRect().right()) / 2, m_watermark->y());
			m_watermark->setTransform(transformScale, true);
			m_watermark->setPos(source.left() + m_watermark->boundingRect().width(), source.bottom());
		}
		source.adjust(0, 0, 0, m_watermark->boundingRect().height());
	}

	if(removeBackground) {
		m_bgColor = m_currentGraphicsView->background();
		m_currentGraphicsView->setBackground(QColor::fromRgb(255,255,255,255));
	}

	return source;
}

void MainWindow::afterExport(bool removeBackground)
{
	foreach(QGraphicsItem *item, m_selectedItems) {
		item->setSelected(true);
	}

	if (removeBackground) {
		m_currentGraphicsView->setBackground(m_bgColor);
	}

	if (m_watermark) {
		delete m_watermark;
	}
}


bool MainWindow::saveAsAux(const QString & fileName) {
	QFile file(fileName);
	if (!file.open(QFile::WriteOnly | QFile::Text)) {
		QMessageBox::warning(this, tr("Fritzing"),
		                     tr("Cannot write file %1:\n%2.")
		                     .arg(fileName)
		                     .arg(file.errorString()));
		return false;
	}

	file.close();

	setReadOnly(false);
	//FritzingWindow::saveAsAux(fileName);

	saveAsAuxAux(fileName);
	m_autosaveNeeded = false;
	undoStackCleanChanged(true);

	m_statusBar->showMessage(tr("Saved '%1'").arg(fileName), 2000);
	setCurrentFile(fileName, true, true);

	if(m_restarting && !m_fwFilename.isEmpty()) {
		QSettings settings;
		settings.setValue("lastOpenSketch",m_fwFilename);
	}

	// mark the stack clean so we update the window dirty flag
	m_undoStack->setClean();

	// slam it here in case we were modified due to m_linkedProgramFiles changes
	setWindowModified(false);

	m_saveAct->setEnabled(true);

	return true;
}

void MainWindow::saveAsAuxAux(const QString & fileName) {
	QApplication::setOverrideCursor(Qt::WaitCursor);

	connectStartSave(true);

	m_programView->saveAll();

	QDir dir(this->m_fzzFolder);
	QStringList nameFilters("*" + FritzingSketchExtension);
	QFileInfoList fileList = dir.entryInfoList(nameFilters, QDir::Files | QDir::NoSymLinks);
	foreach (QFileInfo fileInfo, fileList) {
		QFile file(fileInfo.absoluteFilePath());
		file.remove();
	}

	QString fzName = dir.absoluteFilePath(QFileInfo(fileName).completeBaseName() + FritzingSketchExtension);
	m_sketchModel->save(fzName, false);

	saveLastTabList();

	saveAsShareable(fileName, false);

	connectStartSave(false);

	QApplication::restoreOverrideCursor();
}


void MainWindow::saveAsShareable(const QString & path, bool saveModel)
{
	QString filename = path;
	QHash<QString, ModelPart *> saveParts;
	foreach (QGraphicsItem * item, m_pcbGraphicsView->scene()->items()) {
		ItemBase * itemBase = dynamic_cast<ItemBase *>(item);
		if (itemBase == NULL) continue;
		if (itemBase->modelPart() == NULL) {
			continue;
		}
		if (itemBase->modelPart()->isCore()) continue;
		if (itemBase->moduleID().contains(PartFactory::OldSchematicPrefix)) continue;

		saveParts.insert(itemBase->moduleID(), itemBase->modelPart());
	}
	if(alreadyHasExtension(filename, FritzingSketchExtension)) {
		saveBundledNonAtomicEntity(filename, FritzingSketchExtension, this, saveParts.values(), false, m_fzzFolder, saveModel, true);
	} else {
		saveBundledNonAtomicEntity(filename, FritzingBundleExtension, this, saveParts.values(), false, m_fzzFolder, saveModel, true);
	}

}

void MainWindow::saveBundledNonAtomicEntity(QString &filename, const QString &extension, Bundler *bundler, const QList<ModelPart*> &partsToSave, bool askForFilename, const QString & destFolderPath, bool saveModel, bool deleteLeftovers) {
	bool result;
	QStringList names;

	QString fileExt;
	QString path = defaultSaveFolder() + "/" + QFileInfo(filename).fileName()+"z";
	QString bundledFileName = askForFilename
	                          ? FolderUtils::getSaveFileName(this, tr("Specify a file name"), path, tr("Fritzing (*%1)").arg(extension), &fileExt)
	                          : filename;

	if (bundledFileName.isEmpty()) return; // Cancel pressed

	FileProgressDialog progress("Saving...", 0, this);

	if(!alreadyHasExtension(bundledFileName, extension)) {
		bundledFileName += extension;
	}

	ProcessEventBlocker::processEvents();

	QDir destFolder;
	QString dirToRemove;
	if (destFolderPath.isEmpty()) {
		destFolder = QDir::temp();
		FolderUtils::createFolderAndCdIntoIt(destFolder, TextUtils::getRandText());
		dirToRemove = destFolder.path();
	}
	else {
		destFolder = QDir(destFolderPath);
	}

	QString aux = QFileInfo(bundledFileName).fileName();
	QString destSketchPath;
	if (fritzingBundleExtensions().contains(extension)) {
		destSketchPath = // remove the last "z" from the extension
		    destFolder.path()+"/"+aux.left(aux.size()-1);
	} else {
		destSketchPath = destFolder.path()+"/"+aux;
	}
	DebugDialog::debug("saving entity temporarily to "+destSketchPath);

	QStringList skipSuffixes;

	if (extension.compare(FritzingBundleExtension) == 0 || \
	        extension.compare(FritzingSketchExtension) == 0 ) {
		for (int i = 0; i < m_linkedProgramFiles.count(); i++) {
			LinkedFile * linkedFile = m_linkedProgramFiles.at(i);
			QFileInfo fileInfo(linkedFile->linkedFilename);
			QFile file(linkedFile->linkedFilename);
			FolderUtils::slamCopy(file, destFolder.absoluteFilePath(fileInfo.fileName()));
		}
		skipSuffixes << FritzingBinExtension << FritzingBundleExtension;
	}

	if (saveModel) {
		QString prevFileName = filename;
		ProcessEventBlocker::processEvents();
		bundler->saveAsAux(destSketchPath);
		filename = prevFileName;
	}

	foreach(ModelPart* mp, partsToSave) {
		names.append(saveBundledAux(mp, destFolder));
	}

	if (deleteLeftovers) {
		QStringList nameFilters;
		nameFilters << ("*" + FritzingPartExtension) << "*.svg";
		QDir dir(destFolder);
		QStringList fileList = dir.entryList(nameFilters, QDir::Files | QDir::NoSymLinks);
		foreach (QString fileName, fileList) {
			if (!names.contains(fileName)) {
				QFile::remove(dir.absoluteFilePath(fileName));
			}
		}
	}

	ProcessEventBlocker::processEvents();

	if (fritzingBundleExtensions().contains(extension)) {
		result = FolderUtils::createZipAndSaveTo(destFolder, bundledFileName, skipSuffixes);
	} else {
		result = FolderUtils::createFZAndSaveTo(destFolder, bundledFileName, skipSuffixes);
	}

	if(!result) {
		QMessageBox::warning(
		    this,
		    tr("Fritzing"),
		    tr("Unable to export %1 as shareable").arg(bundledFileName)
		);
	}

	if (!dirToRemove.isEmpty()) {
		FolderUtils::rmdir(dirToRemove);
	}
}


void MainWindow::createExportActions() {

	m_saveAct = new QAction(tr("&Save"), this);
	m_saveAct->setShortcut(tr("Ctrl+S"));
	m_saveAct->setStatusTip(tr("Save the current sketch"));
	connect(m_saveAct, SIGNAL(triggered()), this, SLOT(save()));

	m_saveAsAct = new QAction(tr("&Save As..."), this);
	m_saveAsAct->setShortcut(tr("Shift+Ctrl+S"));
	m_saveAsAct->setStatusTip(tr("Save the current sketch"));
	connect(m_saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

	m_shareOnlineAct = new QAction(tr("Share online..."), this);
	m_shareOnlineAct->setStatusTip(tr("Post a project to the Fritzing website"));
	connect(m_shareOnlineAct, SIGNAL(triggered()), this, SLOT(shareOnline()));

	m_exportJpgAct = new QAction(tr("JPG..."), this);
	m_exportJpgAct->setData(jpgActionType);
	m_exportJpgAct->setStatusTip(tr("Export the visible area of the current sketch as a JPG image"));
	connect(m_exportJpgAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportPngAct = new QAction(tr("PNG..."), this);
	m_exportPngAct->setData(pngActionType);
	m_exportPngAct->setStatusTip(tr("Export the visible area of the current sketch as a PNG image"));
	connect(m_exportPngAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportPdfAct = new QAction(tr("PDF..."), this);
	m_exportPdfAct->setData(pdfActionType);
	m_exportPdfAct->setStatusTip(tr("Export the visible area of the current sketch as a PDF image"));
	connect(m_exportPdfAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportSvgAct = new QAction(tr("SVG..."), this);
	m_exportSvgAct->setData(svgActionType);
	m_exportSvgAct->setStatusTip(tr("Export the current sketch as an SVG image"));
	connect(m_exportSvgAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportBomAct = new QAction(tr("List of parts (&Bill of Materials)..."), this);
	m_exportBomAct->setData(bomActionType);
	m_exportBomAct->setStatusTip(tr("Save a Bill of Materials (BoM)/Shopping List as text"));
	connect(m_exportBomAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportNetlistAct = new QAction(tr("XML Netlist..."), this);
	m_exportNetlistAct->setData(netlistActionType);
	m_exportNetlistAct->setStatusTip(tr("Save a netlist in XML format"));
	connect(m_exportNetlistAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportSpiceNetlistAct = new QAction(tr("SPICE Netlist..."), this);
	m_exportSpiceNetlistAct->setData(spiceNetlistActionType);
	m_exportSpiceNetlistAct->setStatusTip(tr("Save a netlist in SPICE format"));
	connect(m_exportSpiceNetlistAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportEagleAct = new QAction(tr("Eagle..."), this);
	m_exportEagleAct->setData(eagleActionType);
	m_exportEagleAct->setStatusTip(tr("Export the current sketch to Eagle CAD"));
	connect(m_exportEagleAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportGerberAct = new QAction(tr("Extended Gerber (RS-274X)..."), this);
	m_exportGerberAct->setData(gerberActionType);
	m_exportGerberAct->setStatusTip(tr("Export the current sketch to Extended Gerber format (RS-274X) for professional PCB production"));
	connect(m_exportGerberAct, SIGNAL(triggered()), this, SLOT(doExport()));

	m_exportEtchablePdfAct = new QAction(tr("Etchable (PDF)..."), this);
	m_exportEtchablePdfAct->setStatusTip(tr("Export the current sketch to PDF for DIY PCB production (photoresist)"));
	m_exportEtchablePdfAct->setProperty("svg", false);
	connect(m_exportEtchablePdfAct, SIGNAL(triggered()), this, SLOT(exportEtchable()));

	m_exportEtchableSvgAct = new QAction(tr("Etchable (SVG)..."), this);
	m_exportEtchableSvgAct->setStatusTip(tr("Export the current sketch to SVG for DIY PCB production (photoresist)"));
	m_exportEtchableSvgAct->setProperty("svg", true);
	connect(m_exportEtchableSvgAct, SIGNAL(triggered()), this, SLOT(exportEtchable()));

	/*m_pageSetupAct = new QAction(tr("&Page Setup..."), this);
	m_pageSetupAct->setShortcut(tr("Shift+Ctrl+P"));
	m_pageSetupAct->setStatusTip(tr("Setup the current sketch page"));
	connect(m_pageSetupAct, SIGNAL(triggered()), this, SLOT(pageSetup()));*/

	m_printAct = new QAction(tr("&Print..."), this);
	m_printAct->setShortcut(tr("Ctrl+P"));
	m_printAct->setStatusTip(tr("Print the current view"));
	connect(m_printAct, SIGNAL(triggered()), this, SLOT(print()));

}

void MainWindow::exportToEagle() {

	QString text =
	    tr("This will soon provide an export of your Fritzing sketch to the EAGLE layout "
	       "software. If you'd like to have more exports to your favourite EDA tool, please let "
	       "us know, or contribute.");
	/*
		QString text =
			tr("The Eagle export module is very experimental.  If anything breaks or behaves "
			"strangely, please let us know.");
	*/

	QMessageBox::information(this, tr("Fritzing"), text);

	Fritzing2Eagle eagle = Fritzing2Eagle(m_pcbGraphicsView);

	/*
	QList <ItemBase*> partList;

	// bail out if something is wrong
	// TODO: show an error in QMessageBox
	if(m_currentWidget == NULL) {
		return;
	}

	m_pcbGraphicsView->collectParts(partList);

	QString exportInfoString = tr("parts include:\n");
	QString exportString = tr("GRID INCH 0.005\n");


	for(int i=0; i < partList.size(); i++){
		QString label = partList.at(i)->instanceTitle();
		QString desc = partList.at(i)->title();

		QHash<QString,QString> properties = partList.at(i)->modelPartShared()->properties();
		QString package = properties["package"];
		if (package == NULL) {
			package = tr("*** package not specified ***");
		}

		exportInfoString += label + tr(" which is a ") + desc + tr(" in a ") + package + tr(" package.\n");
	}
	QMessageBox::information(this, tr("Fritzing"), exportInfoString);
	*/

	/*
	QFile fp( fileName );
	fp.open(QIODevice::WriteOnly);
	fp.write(bom.toUtf8(),bom.length());
	fp.close();
	*/


	/*
	GRID INCH 0.005
	USE '/Applications/eclipse/eagle/lbr/fritzing.lbr';
	ADD RESISTOR@fritzing 'R_1' R0.000 (2.3055117 2.1307087);
	ADD LED@fritzing 'L_2' R0.000 (5.423622 2.425197);
	GRID LAST;
	*/
}

void MainWindow::exportSvg(double res, bool selectedItems, bool flatten) {
	QString path = defaultSaveFolder();
	QString fileExt;
	QString fileName = FolderUtils::getSaveFileName(this,
	                   tr("Export SVG..."),
	                   path+"/"+constructFileName("", svgActionType),
	                   fileExtFormats[svgActionType],
	                   &fileExt
	                                               );

	if (fileName.isEmpty()) return;

	exportSvg(res, selectedItems, flatten, fileName);
}

void MainWindow::exportSvg(double res, bool selectedItems, bool flatten, const QString & fileName)
{
	FileProgressDialog * fileProgressDialog = exportProgress();
	LayerList viewLayerIDs;
	foreach (ViewLayer * viewLayer, m_currentGraphicsView->viewLayers()) {
		if (viewLayer == NULL) continue;
		if (!viewLayer->visible()) continue;

		viewLayerIDs << viewLayer->viewLayerID();
	}

	RenderThing renderThing;
	renderThing.printerScale = GraphicsUtils::SVGDPI;
	renderThing.blackOnly = false;
	renderThing.dpi = res;
	renderThing.selectedItems = selectedItems;
	renderThing.hideTerminalPoints = true;
	renderThing.renderBlocker = false;
	QString svg = m_currentGraphicsView->renderToSVG(renderThing, NULL, viewLayerIDs);
	if (svg.isEmpty()) {
		// tell the user something reasonable
		return;
	}

	if (selectedItems == false && flatten == false) {
		exportSvgWatermark(svg, res);
	}

	TextUtils::writeUtf8(fileName, TextUtils::convertExtendedChars(svg));
	delete fileProgressDialog;
}

void MainWindow::exportSvgWatermark(QString & svg, double res)
{
	QFile file(":/resources/images/watermark_fritzing_outline.svg");
	if (!file.open(QFile::ReadOnly)) return;

	QString watermarkSvg = file.readAll();
	file.close();

	if (!watermarkSvg.contains("<svg")) return;

	QSizeF watermarkSize = TextUtils::parseForWidthAndHeight(watermarkSvg);
	QSizeF svgSize = TextUtils::parseForWidthAndHeight(svg);

	SvgFileSplitter splitter;
	bool result = splitter.splitString(watermarkSvg, "watermark");
	if (!result) return;

	double factor;
	result = splitter.normalize(res, "watermark", false, factor);
	if (!result) return;

	QString transWatermark = splitter.shift((svgSize.width() - watermarkSize.width()) * res, svgSize.height() * res, "watermark", true);
	QString newSvg = TextUtils::makeSVGHeader(1, res, svgSize.width(), svgSize.height() + watermarkSize.height()) + transWatermark + "</svg>";
	svg = TextUtils::mergeSvg(newSvg, svg, "", false);
}

void MainWindow::exportBOM() {

	// bail out if something is wrong
	// TODO: show an error in QMessageBox
	if (m_currentWidget == NULL) {
		return;
	}

	QString bomTemplate;
	QFile file(":/resources/templates/bom.html");
	if (file.open(QFile::ReadOnly)) {
		bomTemplate = file.readAll();
		file.close();
	}
	else {
		return;
	}

	QString bomRowTemplate;
	QFile file2(":/resources/templates/bom_row.html");
	if (file2.open(QFile::ReadOnly)) {
		bomRowTemplate = file2.readAll();
		file2.close();
	}
	else {
		return;
	}

	QList <ItemBase*> partList;
	QList<QString> descrList;
	QMultiHash<QString, ItemBase *> descrs;

	m_currentGraphicsView->collectParts(partList);

	qSort(partList.begin(), partList.end(), sortPartList);

	foreach (ItemBase * itemBase, partList) {
		if (itemBase->itemType() != ModelPart::Part) continue;
		QStringList keys;
		QHash<QString, QString> properties = HtmlInfoView::getPartProperties(itemBase->modelPart(), itemBase, false, keys);
		QString desc = itemBase->prop("mn") + "%%%%%" + itemBase->prop("mpn") + "%%%%%" + itemBase->title() + "%%%%%" + getBomProps(itemBase);  // keeps different parts separate if there are no properties
		descrs.insert(desc, itemBase);
		if (!descrList.contains(desc)) {
			descrList.append(desc);
		}
	}

	QString assemblyString;
	foreach (ItemBase * itemBase, partList) {
		if (itemBase->itemType() != ModelPart::Part) continue;
		QStringList keys;
		QHash<QString, QString> properties = HtmlInfoView::getPartProperties(itemBase->modelPart(), itemBase, false, keys);
		assemblyString += bomRowTemplate.arg(itemBase->instanceTitle()).arg(itemBase->prop("mn")).arg(itemBase->prop("mpn")).arg(itemBase->title()).arg(getBomProps(itemBase));
	}

	QString shoppingListString;
	foreach (QString descr, descrList) {
		QList<ItemBase *> itemBases = descrs.values(descr);
		QStringList split = descr.split("%%%%%");
		shoppingListString += bomRowTemplate.arg(itemBases.count()).arg(split.at(0)).arg(split.at(1)).arg(split.at(2)).arg(split.at(3));
	}

	QString bom = bomTemplate
	              .arg("Fritzing Bill of Materials")
	              .arg(QFileInfo(m_fwFilename).fileName())
	              .arg(m_fwFilename)
	              .arg(QDateTime::currentDateTime().toString("dddd, MMMM d yyyy, hh:mm:ss"))
	              .arg(assemblyString)
	              .arg(shoppingListString)
	              .arg(QString("%1.%2.%3").arg(Version::majorVersion()).arg(Version::minorVersion()).arg(Version::minorSubVersion()));


	QString path = defaultSaveFolder();

	QString fileExt;
	QString extFmt = fileExtFormats.value(bomActionType);
	QString fname = path+"/"+constructFileName("bom", bomActionType);
	DebugDialog::debug(QString("fname %1\n%2").arg(fname).arg(extFmt));

	QString fileName = FolderUtils::getSaveFileName(this,
	                   tr("Export Bill of Materials (BoM)..."),
	                   fname,
	                   extFmt,
	                   &fileExt
	                                               );

	if (fileName.isEmpty()) {
		return; //Cancel pressed
	}

	FileProgressDialog * fileProgressDialog = exportProgress();
	DebugDialog::debug(fileExt+" selected to export");
	if(!alreadyHasExtension(fileName, bomActionType)) {
		fileName += bomActionType;
	}

	if (!TextUtils::writeUtf8(fileName, bom)) {
		QMessageBox::warning(this, tr("Fritzing"), tr("Unable to save BOM file, but the text is on the clipboard."));
	}

	QFileInfo info(fileName);
	if (info.exists()) {
		QDesktopServices::openUrl(QString("file:///%1").arg(fileName));
	}

	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard) {
		clipboard->setText(bom);
	}
	delete fileProgressDialog;
}

void MainWindow::exportSpiceNetlist() {
	if (m_schematicGraphicsView == NULL) return;

	// examples:
	// http://www.allaboutcircuits.com/vol_5/chpt_7/8.html
	// http://cutler.eecs.berkeley.edu/classes/icbook/spice/UserGuide/elements_fr.html
	// http://www.csd.uoc.gr/~hy422/2011s/datasheets/ngspice-user-manual.pdf

	QString path = defaultSaveFolder();
	QString fileExt;
	QString extFmt = fileExtFormats.value(spiceNetlistActionType);
	QString fname = path + "/" + constructFileName("spice", spiceNetlistActionType);
	//DebugDialog::debug(QString("fname %1\n%2").arg(fname).arg(extFmt));
	QString fileName = FolderUtils::getSaveFileName(this,
	                   tr("Export SPICE Netlist..."),
	                   fname,
	                   extFmt,
	                   &fileExt
	                                               );

	if (fileName.isEmpty()) {
		return; //Cancel pressed
	}

	QFileInfo fileInfo(m_fwFilename);
	QString spiceNetlist = getSpiceNetlist(fileInfo.completeBaseName());
	//DebugDialog::debug(fileExt + " selected to export");
	if(!alreadyHasExtension(fileName, spiceNetlistActionType)) {
		fileName += spiceNetlistActionType;
	}

	TextUtils::writeUtf8(fileName, spiceNetlist);
}

/**
 * Build and return a circuit description in spice based on the current circuit.
 *
 * Excludes parts that are not connected to other parts.
 * @brief Create a circuit description in spice
 * @param[in] simulationName Name of the simulation to be included in the first line of output
 * @return A string that is a circuit description in spice
 */
QString MainWindow::getSpiceNetlist(QString simulationName) {
	QList< QList<ConnectorItem *>* > netList;
	QSet<ItemBase *> itemBases;
	QString spiceNetlist = getSpiceNetlist(simulationName, netList, itemBases);
	foreach (QList<ConnectorItem *> * net, netList) {
		delete net;
	}
	netList.clear();
	return spiceNetlist;
}

/**
 * Build and return a circuit description in spice based on the current circuit.
 * Additionally, the netlist and the parts to simulate are returned by pointers.
 *
 * Excludes parts that are not connected to other parts.
 * @brief Create a circuit description in spice
 * @param[in] simulationName Name of the simulation to be included in the first line of output
 * @param[out] netList A list with all the nets of the circuit that are going to be simulated and each net is a list of the connectors that belong to that net
 * @param[out] itemBases A set with the parts that are going to be simulated
 * @return A string that is a circuit description in spice
 */
QString MainWindow::getSpiceNetlist(QString simulationName, QList< QList<class ConnectorItem *>* >& netList, QSet<class ItemBase *>& itemBases) {
	QString output = simulationName + "\n";
	static QRegExp curlies("\\{([^\\{\\}]*)\\}");
	QHash<ConnectorItem *, int> indexer;
	this->m_schematicGraphicsView->collectAllNets(indexer, netList, true, false);


	//DebugDialog::debug("_______________");

	QList<ConnectorItem *> * ground = NULL;
	foreach (QList<ConnectorItem *> * net, netList) {
		if (net->count() < 2) continue;

		foreach (ConnectorItem * ci, *net) {
			//ci->debugInfo("net");
			if (ci->isGrounded()) {
				ground = net;
			}
			if (!ci->attachedTo()->spice().isEmpty())
				itemBases.insert(ci->attachedTo());
		}
		//DebugDialog::debug("_______________");
	}

	//If the circuit is built in the BB view, there is no ground. Then, try to find a negative terminal from a power supply as ground
	if (!ground){
		DebugDialog::debug("Netlist exporter: Trying to identify the negative connection of a power supply as ground");
		foreach (QList<ConnectorItem *> * net, netList) {
			if (ground) break;
			if (net->count() < 2) continue;
			foreach (ConnectorItem * ci, *net) {
				if (ci->connectorSharedName().compare("-", Qt::CaseInsensitive) == 0) {
					ground = net;
					break;
				}
			}
		}
	}

	if (ground) {
		DebugDialog::debug("Netlist exporter: ground found");
		// make sure ground is index zero
		netList.removeOne(ground);
		netList.prepend(ground);
	} else {
		if (netList.count() > 0) {
			DebugDialog::debug("Netlist exporter: ground NOT found. The ground has been connected to the following connectors");
			ground = netList.at(0);
			foreach (ConnectorItem * connector, * ground) {
				connector->debugInfo("ground set in: ");
			}
		}
	}

	foreach (QList<ConnectorItem *> * net, netList) {
		if (net->count() < 2) continue;

		foreach (ConnectorItem * ci, *net) {
			ci->debugInfo("net");
		}
		DebugDialog::debug("_______________");
	}

	//DebugDialog::debug("_______________");
	//DebugDialog::debug("_______________");

	foreach (ItemBase * itemBase, itemBases) {
		QString spice = itemBase->spice();
		if (spice.isEmpty()) continue;
		int pos = 0;
		while (true) {
			int ix = curlies.indexIn(spice, pos);
			if (ix < 0) break;

			QString token = curlies.cap(1).toLower();
			QString replacement;
			if (token == "instancetitle") {
				replacement = itemBase->instanceTitle();
				if (ix > 0 && replacement.at(0).toLower() == spice.at(ix - 1).toLower()) {
					// if the type letter is repeated
					replacement = replacement.mid(1);
				}
				replacement.replace(" ", "_");
			}
			else if (token.startsWith("net ")) {
				QString cname = token.mid(4).trimmed();
				foreach (ConnectorItem * ci, itemBase->cachedConnectorItems()) {
					if (ci->connectorSharedID().toLower() == cname) {
						int ix = -1;
						foreach (QList<ConnectorItem *> * net, netList) {
							ix++;
							if (net->contains(ci)) break;
						}

						replacement = QString::number(ix);
						break;
					}
				}
			}
			else {
				//Find the symbol of this property
				QString symbol;
				QHash<PropertyDef *, QString> propertyDefs;
				PropertyDefMaster::initPropertyDefs(itemBase->modelPart(), propertyDefs);
				foreach (PropertyDef * propertyDef, propertyDefs.keys()) {
					if (token.compare(propertyDef->name, Qt::CaseInsensitive) == 0) {
						symbol = propertyDef->symbol;
						break;
					}
				}
				//Find the value of the property
				QVariant variant = itemBase->modelPart()->localProp(token);
				if (variant.isNull()) {
					replacement = itemBase->modelPart()->properties().value(token, "");
					if(replacement.isEmpty()) {
						//Leave it, probably is a brace expresion for the spice simulator
						pos = ix + 1;
						replacement = curlies.cap(0);
						continue;
					}
				}
				else {
					replacement = variant.toString();
				}
				//Remove the symbol, if any. It is not mandatory:
				//(Ngspice ignores letters immediately following a number that are not scale factors)
				if (!symbol.isEmpty()) {
					replacement.replace(symbol, "");
				}
				//Ngspice does not differenciate from m and M prefixes, u shuld be used for micro
				replacement.replace("M", "Meg");
				replacement.replace(TextUtils::MicroSymbol, "u");
			}

			spice.replace(ix, curlies.cap(0).count(), replacement);
			DebugDialog::debug("spice " + spice);
		}

		output += spice;
	}

	output += "\n";

	// remove redundant models
	QStringList models;
	foreach (ItemBase * itemBase, itemBases) {
		QString spiceModel = itemBase->spiceModel();
		if (spiceModel.isEmpty()) continue;
		if (models.contains(spiceModel, Qt::CaseInsensitive)) continue;

		models.append(spiceModel);
	}

	foreach (QString model, models) {
		output += model;
		output += "\n";
	}

	QString incl = ".include";
	if (output.contains(incl, Qt::CaseInsensitive)) {
		QStringList lines = output.split("\n");
		QList<QDir > paths;
		paths << FolderUtils::getAppPartsSubFolder("");
		paths << QDir(FolderUtils::getUserPartsPath());

		QString output2;
		foreach (QString line, lines) {
			int ix = line.toLower().indexOf(incl);
			if (ix < 0) {
				output2 += line + "\n";
				continue;
			}

			QString temp = line;
			temp.replace(ix, incl.length(), "");
			QString filename = temp.trimmed();

			bool gotOne = false;
			foreach (QDir dir, paths) {
				foreach (QString folder, ModelPart::possibleFolders()) {
					QDir sub(dir);
					sub.cd(folder);
					sub.cd("spicemodels");
					if (QFile::exists(sub.absoluteFilePath(filename))) {
						output2 += incl.toUpper() + " " + QDir::toNativeSeparators(sub.absoluteFilePath(filename)) + "\n";
						gotOne = true;
						break;
					}
				}
				if (gotOne) break;
			}

			// can't find the include file, so just restore the original line
			if (!gotOne) {
				output2 += line + "\n";
			}
		}

		output = output2;
	}

	output += ".options savecurrents\n";
	output += ".OP\n";
	output += "*.TRAN 1ms 100ms\n";
	output += "* .AC DEC 100 100 1MEG\n";
	output += ".END";

	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard) {
		clipboard->setText(output);
	}

	return output;
}

void MainWindow::exportNetlist() {
	QHash<ConnectorItem *, int> indexer;
	QList< QList<ConnectorItem *>* > netList;
	this->m_currentGraphicsView->collectAllNets(indexer, netList, true, m_currentGraphicsView->boardLayers() > 1);

	QDomDocument doc;
	doc.setContent(QString("<?xml version='1.0' encoding='UTF-8'?>\n") + TextUtils::CreatedWithFritzingXmlComment);
	QDomElement netlist = doc.createElement("netlist");
	doc.appendChild(netlist);
	netlist.setAttribute("sketch", QFileInfo(m_fwFilename).fileName());
	netlist.setAttribute("date", QDateTime::currentDateTime().toString());

	// TODO: filter out 'ignore' connectors

	QList< QList<ConnectorItem *>* > deleteNets;
	foreach (QList<ConnectorItem *> * net, netList) {
		QList<ConnectorItem *> deleteItems;
		foreach (ConnectorItem * connectorItem, *net) {
			ErcData * ercData = connectorItem->connectorSharedErcData();
			if (ercData == NULL) continue;

			if (ercData->ignore() == ErcData::Always) {
				deleteItems.append(connectorItem);
			}
			else if ((ercData->ignore() == ErcData::IfUnconnected) && (net->count() == 1)) {
				deleteItems.append(connectorItem);
			}
		}

		foreach (ConnectorItem * connectorItem, deleteItems) {
			net->removeOne(connectorItem);
		}
		if (net->count() == 0) {
			deleteNets.append(net);
		}
	}

	foreach (QList<ConnectorItem *> * net, deleteNets) {
		netList.removeOne(net);
	}

	foreach (QList<ConnectorItem *> * net, netList) {
		QDomElement netElement = doc.createElement("net");
		netlist.appendChild(netElement);
		foreach (ConnectorItem * connectorItem, *net) {
			QDomElement connector = doc.createElement("connector");
			netElement.appendChild(connector);
			connector.setAttribute("id", connectorItem->connectorSharedID());
			connector.setAttribute("name", connectorItem->connectorSharedName());
			QDomElement part = doc.createElement("part");
			connector.appendChild(part);
			ItemBase * itemBase = connectorItem->attachedTo();
			part.setAttribute("id", itemBase->id());
			part.setAttribute("label", itemBase->instanceTitle());
			part.setAttribute("title", itemBase->title());
			ErcData * ercData = connectorItem->connectorSharedErcData();
			if (ercData) {
				QDomElement erc = doc.createElement("erc");
				if (ercData->writeToElement(erc, doc)) {
					connector.appendChild(erc);
				}
			}
		}
	}

	foreach (QList<ConnectorItem *> * net, netList) {
		delete net;
	}
	netList.clear();

	QString path = defaultSaveFolder();

	QString fileExt;
	QString extFmt = fileExtFormats.value(netlistActionType);
	QString fname = path + "/" +constructFileName("netlist", netlistActionType);
	//DebugDialog::debug(QString("fname %1\n%2").arg(fname).arg(extFmt));

	QString fileName = FolderUtils::getSaveFileName(this,
	                   tr("Export Netlist..."),
	                   fname,
	                   extFmt,
	                   &fileExt
	                                               );

	if (fileName.isEmpty()) {
		return; //Cancel pressed
	}

	FileProgressDialog * fileProgressDialog = exportProgress();
	//DebugDialog::debug(fileExt + " selected to export");
	if(!alreadyHasExtension(fileName, netlistActionType)) {
		fileName += netlistActionType;
	}

	QFile fp( fileName );
	fp.open(QIODevice::WriteOnly);
	fp.write(doc.toByteArray());
	fp.close();

	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard) {
		clipboard->setText(doc.toByteArray());
	}
	delete fileProgressDialog;
}

FileProgressDialog * MainWindow::exportProgress() {
	return (new FileProgressDialog("Exporting...", 0, this));
}

void MainWindow::exportNormalizedSVG() {
	exportSvg(GraphicsUtils::StandardFritzingDPI, true, false);
}

void MainWindow::exportNormalizedFlattenedSVG() {
	exportSvg(GraphicsUtils::StandardFritzingDPI, true, true);
}

QString MainWindow::getBomProps(ItemBase * itemBase)
{
	if (itemBase == NULL) return "";

	QStringList keys;
	QHash<QString, QString> properties = HtmlInfoView::getPartProperties(itemBase->modelPart(), itemBase, false, keys);
	QString pString;
	foreach (QString key, keys) {
		if (key.compare("family") == 0) continue;

		QString value = properties.value(key);

		QWidget widget;
		QWidget * resultWidget = NULL;
		QString resultKey, resultValue;
		bool hide;
		itemBase->collectExtraInfo(&widget, properties.value("family"), key, value, false, resultKey, resultValue, resultWidget, hide);
		if (resultValue.isEmpty()) continue;

		pString += ItemBase::translatePropertyName(resultKey) + " " + resultValue + "; ";
	}

	if (pString.length() > 2) pString.chop(2);

	return pString;
}

void MainWindow::exportToGerber() {

	//NOTE: this assumes just one board per sketch

	int boardCount;
	ItemBase * board = m_pcbGraphicsView->findSelectedBoard(boardCount);

	// barf an error if there's no board
	if (boardCount == 0) {
		QMessageBox::critical(this, tr("Fritzing"),
		                      tr("Your sketch does not have a board yet!  Please add a PCB in order to export to Gerber."));
		return;
	}
	if (board == NULL) {
		QMessageBox::critical(this, tr("Fritzing"),
		                      tr("Gerber export can only handle one board at a time--please select the board you want to export."));
		return;
	}

	QString exportDir = QFileDialog::getExistingDirectory(this, tr("Choose a folder for exporting"),
	                    defaultSaveFolder(),
	                    QFileDialog::ShowDirsOnly
	                    | QFileDialog::DontResolveSymlinks);

	if (exportDir.isEmpty()) return;

	FileProgressDialog * fileProgressDialog = exportProgress();

	FolderUtils::setOpenSaveFolder(exportDir);
	m_pcbGraphicsView->saveLayerVisibility();
	m_pcbGraphicsView->setAllLayersVisible(true);

	QFileInfo info(m_fwFilename);
	QString prefix = info.completeBaseName();
	if (boardCount > 1) {
		prefix += QString("_%1_%2").arg(board->instanceTitle()).arg(board->id());
	}
	GerberGenerator::exportToGerber(prefix, exportDir, board, m_pcbGraphicsView, true);

	m_pcbGraphicsView->restoreLayerVisibility();
	m_statusBar->showMessage(tr("Sketch exported to Gerber"), 2000);

	delete fileProgressDialog;
}

void MainWindow::connectStartSave(bool doConnect) {

	if (doConnect) {
		connect(m_sketchModel->root(), SIGNAL(startSaveInstances(const QString &, ModelPart *, QXmlStreamWriter &)),
		        this, SLOT(startSaveInstancesSlot(const QString &, ModelPart *, QXmlStreamWriter &)), Qt::DirectConnection);
	}
	else {
		disconnect(m_sketchModel->root(), SIGNAL(startSaveInstances(const QString &, ModelPart *, QXmlStreamWriter &)),
		           this, SLOT(startSaveInstancesSlot(const QString &, ModelPart *, QXmlStreamWriter &)));
	}
}

QString MainWindow::constructFileName(const QString & differentiator, const QString & suffix) {
	QString fn = QFileInfo(m_fwFilename).completeBaseName();
	fn += "_" + (differentiator.isEmpty() ? m_currentGraphicsView->getShortName() : differentiator);
	return fn + suffix;
}

void MainWindow::massageOutput(QString & svg, bool doMask, bool doSilk, bool doPaste, QString & maskTop, QString & maskBottom, const QString & fileName, ItemBase * board, int dpi, const LayerList & viewLayerIDs)
{
	if (doPaste) {
		// must test doPaste first, since doMask will also be true
		svg = pcbView()->makePasteMask(svg, board, dpi, viewLayerIDs);
	}
	else if (doSilk) {
		QString use = (fileName.contains("bottom")) ? maskBottom : maskTop;
		use = TextUtils::expandAndFill(use, "white", GerberGenerator::MaskClearanceMils * 2 * dpi / 1000);
		svg = TextUtils::mergeSvg(svg, use, "", false);
	}
	else if (doMask) {
		if (fileName.contains("bottom")) maskBottom = svg;
		else maskTop = svg;
		svg = TextUtils::expandAndFill(svg, "black", GerberGenerator::MaskClearanceMils * 2 * dpi / 1000);
	}
}

void MainWindow::dumpAllParts() {
	if (m_currentGraphicsView == NULL) return;

	QList<ItemBase *> already;
	foreach (QGraphicsItem * item, m_currentGraphicsView->items()) {
		ItemBase * ib = dynamic_cast<ItemBase *>(item);
		if (ib == NULL) continue;

		ItemBase * chief = ib->layerKinChief();
		if (already.contains(chief)) continue;

		already << chief;

		QList<ItemBase *> itemBases;
		itemBases << chief;
		itemBases.append(chief->layerKin());
		foreach (ItemBase * itemBase, itemBases) {
			itemBase->debugInfo("");
			foreach (ConnectorItem * connectorItem, itemBase->cachedConnectorItems()) {
				if (connectorItem->connectionsCount() > 0) {
					connectorItem->debugInfo("\t");
					foreach (ConnectorItem * to, connectorItem->connectedToItems()) {
						to->debugInfo("\t\t");
					}
				}
			}
		}
	}
}
