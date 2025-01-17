/*******************************************************************

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

#include "propertydef.h"
#include "../debugdialog.h"
#include "../model/modelpart.h"
#include "../utils/textutils.h"

#include <QDomNodeList>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>

QList <PropertyDef *> PropertyDefMaster::PropertyDefs;
QList <QString> PropertyDefMaster::ModuleIDSuffixes;

void PropertyDefMaster::loadPropertyDefs() {
	QFile file(":/resources/properties.xml");

	QString errorStr;
	int errorLine;
	int errorColumn;

	QDomDocument domDocument;
	if (!domDocument.setContent(&file, true, &errorStr, &errorLine, &errorColumn)) {
		DebugDialog::debug(QString("failed loading properties %1 line:%2 col:%3").arg(errorStr).arg(errorLine).arg(errorColumn));
		return;
	}

	QDomElement root = domDocument.documentElement();
	if (root.isNull()) return;
	if (root.tagName() != "properties") return;

	QDomElement propertyElement = root.firstChildElement("property");
	while (!propertyElement.isNull()) {
		PropertyDef * propertyDef = new PropertyDef;
		propertyDef->name = propertyElement.attribute("name");

		PropertyDefs.append(propertyDef);
		propertyDef->symbol = propertyElement.attribute("symbol");
		propertyDef->minValue = propertyElement.attribute("minValue").toDouble();
		propertyDef->maxValue = propertyElement.attribute("maxValue").toDouble();
		propertyDef->defaultValue = propertyElement.attribute("defaultValue");
		propertyDef->editable = propertyElement.attribute("editable", "").compare("yes") == 0;
		propertyDef->numeric = propertyElement.attribute("numeric", "").compare("yes") == 0;
		QDomElement menuItem = propertyElement.firstChildElement("menuItem");
		while (!menuItem.isNull()) {
			QString val = menuItem.attribute("value");
			if (propertyDef->numeric) {
				propertyDef->menuItems.append(val.toDouble());
			}
			else {
				propertyDef->sMenuItems.append(val);
			}
			QString adjunct = menuItem.attribute("adjunct");
			if (!adjunct.isEmpty()) {
				propertyDef->adjuncts.insert(val, adjunct);
			}
			menuItem = menuItem.nextSiblingElement("menuItem");
		}
		QDomElement suffixElement = propertyElement.firstChildElement("suffix");
		while (!suffixElement.isNull()) {
			QString suffix = suffixElement.attribute("suffix");
			propertyDef->suffixes.append(suffix);
			if (!ModuleIDSuffixes.contains(suffix))
				ModuleIDSuffixes.append(suffix);
			suffixElement = suffixElement.nextSiblingElement("suffix");
		}

		propertyElement = propertyElement.nextSiblingElement("property");
	}

}

void PropertyDefMaster::cleanup() {
	foreach (PropertyDef * propertyDef, PropertyDefs) {
		delete propertyDef;
	}

	PropertyDefs.clear();
	ModuleIDSuffixes.clear();
}

void PropertyDefMaster::initPropertyDefs(ModelPart * modelPart, QHash<PropertyDef *, QString> & propertyDefs)
{
	if (PropertyDefs.count() == 0) {
		loadPropertyDefs();
	}

	foreach (PropertyDef * propertyDef, PropertyDefs) {
		foreach (QString suffix, propertyDef->suffixes) {
			if (!modelPart->moduleID().endsWith(suffix, Qt::CaseInsensitive)) continue;

			//DebugDialog::debug(QString("%1 %2").arg(suffix).arg(modelPart->moduleID()));
			QString defaultValue;
			if (propertyDef->numeric) {
				if (!propertyDef->defaultValue.isEmpty()) {
					defaultValue = TextUtils::convertToPowerPrefix(propertyDef->defaultValue.toDouble()) + propertyDef->symbol;
				}
			}
			else {
				defaultValue = propertyDef->defaultValue;
			}
			QString savedValue = modelPart->localProp(propertyDef->name).toString();
			if (savedValue.isEmpty()) {
				savedValue = modelPart->properties().value(propertyDef->name.toLower(), defaultValue);
				if (!savedValue.isEmpty()) {
					modelPart->setLocalProp(propertyDef->name, savedValue);
				}
			}
			// caches the current value
			propertyDefs.insert(propertyDef, savedValue);
		}
	}
}

/**
 * Returns true if the moduleID of a part ends with any of the sufixxes listed in the properties.xml.
 * Thus, the properties of the part can be changed in the Inspector pane.
 * @param[in] moduleID The module ID of the part to check
 * @returns true if the moduleID is listed in properties.xml, false otherwise .
 */
bool PropertyDefMaster::partPropertiesCanBeModified(QString moduleID){
	foreach(QString suffix, ModuleIDSuffixes){
		if(moduleID.endsWith(suffix)) {
			return true;
		}
	}
	return false;
}
