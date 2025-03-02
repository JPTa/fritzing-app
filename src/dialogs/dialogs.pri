# /*******************************************************************
# Part of the Fritzing project - http://fritzing.org
# Copyright (c) 2007-08 Fritzing
# Fritzing is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Fritzing is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with Fritzing. If not, see <http://www.gnu.org/licenses/>.
# ********************************************************************/

INCLUDEPATH += $$PWD

HEADERS += src/dialogs/prefsdialog.h \
        $$PWD/exportparametersdialog.h \
        src/dialogs/pinlabeldialog.h \
        src/dialogs/groundfillseeddialog.h \
        src/dialogs/quotedialog.h \
        src/dialogs/recoverydialog.h \
        src/dialogs/setcolordialog.h \
        src/dialogs/translatorlistmodel.h \
        src/dialogs/fabuploaddialog.h \
        src/dialogs/fabuploadprogress.h \
        src/dialogs/networkhelper.h

SOURCES += src/dialogs/prefsdialog.cpp \
        $$PWD/exportparametersdialog.cpp \
        src/dialogs/pinlabeldialog.cpp \
        src/dialogs/groundfillseeddialog.cpp \
        src/dialogs/quotedialog.cpp \
        src/dialogs/recoverydialog.cpp \
        src/dialogs/setcolordialog.cpp \
        src/dialogs/translatorlistmodel.cpp \
        src/dialogs/fabuploaddialog.cpp \
        src/dialogs/fabuploadprogress.cpp \
        src/dialogs/networkhelper.cpp

FORMS += src/dialogs/fabuploaddialog.ui \
    $$PWD/exportparametersdialog.ui
