QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    acqcontrol.cpp \
    go4connectionthread.cpp \
    main.cpp \
    cronoacqdlg.cpp \
    qcustomplot.cpp \
    rootserver.cpp \
    src/qtbuttonpropertybrowser.cpp \
    src/qteditorfactory.cpp \
    src/qtgroupboxpropertybrowser.cpp \
    src/qtpropertybrowser.cpp \
    src/qtpropertybrowserutils.cpp \
    src/qtpropertymanager.cpp \
    src/qttreepropertybrowser.cpp \
    src/qtvariantproperty.cpp

HEADERS += \
    acqcontrol.h \
    cronoacqdlg.h \
    cronotypes.h \
    go4connectionthread.h \
    qcustomplot.h \
    rootserver.h \
    src/QtAbstractEditorFactoryBase \
    src/QtAbstractPropertyBrowser \
    src/QtAbstractPropertyManager \
    src/QtBoolPropertyManager \
    src/QtBrowserItem \
    src/QtButtonPropertyBrowser \
    src/QtCharEditorFactory \
    src/QtCharPropertyManager \
    src/QtCheckBoxFactory \
    src/QtColorEditorFactory \
    src/QtColorPropertyManager \
    src/QtCursorEditorFactory \
    src/QtCursorPropertyManager \
    src/QtDateEditFactory \
    src/QtDatePropertyManager \
    src/QtDateTimeEditFactory \
    src/QtDateTimePropertyManager \
    src/QtDoublePropertyManager \
    src/QtDoubleSpinBoxFactory \
    src/QtEnumEditorFactory \
    src/QtEnumPropertyManager \
    src/QtFlagPropertyManager \
    src/QtFontEditorFactory \
    src/QtFontPropertyManager \
    src/QtGroupBoxPropertyBrowser \
    src/QtGroupPropertyManager \
    src/QtIntPropertyManager \
    src/QtKeySequenceEditorFactory \
    src/QtKeySequencePropertyManager \
    src/QtLineEditFactory \
    src/QtLocalePropertyManager \
    src/QtPointFPropertyManager \
    src/QtPointPropertyManager \
    src/QtProperty \
    src/QtRectFPropertyManager \
    src/QtRectPropertyManager \
    src/QtScrollBarFactory \
    src/QtSizeFPropertyManager \
    src/QtSizePolicyPropertyManager \
    src/QtSizePropertyManager \
    src/QtSliderFactory \
    src/QtSpinBoxFactory \
    src/QtStringPropertyManager \
    src/QtTimeEditFactory \
    src/QtTimePropertyManager \
    src/QtTreePropertyBrowser \
    src/QtVariantEditorFactory \
    src/QtVariantProperty \
    src/QtVariantPropertyManager \
    src/qtbuttonpropertybrowser.h \
    src/qteditorfactory.h \
    src/qtgroupboxpropertybrowser.h \
    src/qtpropertybrowser.h \
    src/qtpropertybrowserutils_p.h \
    src/qtpropertymanager.h \
    src/qttreepropertybrowser.h \
    src/qtvariantproperty.h

FORMS += \
    cronoacqdlg.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32:CONFIG(release, debug|release): LIBS += -L/home/studentfl/go4/root_install/lib/release/ -lCore -lThread -lRIO -lNet
else:win32:CONFIG(debug, debug|release): LIBS += -L/home/studentfl/go4/root_install/lib/debug/ -lCore -lThread -lRIO -lNet
else:unix: LIBS += -L/home/studentfl/go4/root_install/lib/ -lCore -lThread -lRIO -lNet

INCLUDEPATH += /home/studentfl/go4/root_install/include
INCLUDEPATH += src/
DEPENDPATH += /home/studentfl/go4/root_install/include

DISTFILES += \
    src/images/cursor-arrow.png \
    src/images/cursor-busy.png \
    src/images/cursor-closedhand.png \
    src/images/cursor-cross.png \
    src/images/cursor-forbidden.png \
    src/images/cursor-hand.png \
    src/images/cursor-hsplit.png \
    src/images/cursor-ibeam.png \
    src/images/cursor-openhand.png \
    src/images/cursor-sizeall.png \
    src/images/cursor-sizeb.png \
    src/images/cursor-sizef.png \
    src/images/cursor-sizeh.png \
    src/images/cursor-sizev.png \
    src/images/cursor-uparrow.png \
    src/images/cursor-vsplit.png \
    src/images/cursor-wait.png \
    src/images/cursor-whatsthis.png \
    src/qtpropertybrowser.pri

RESOURCES += \
    cronoacq_ressources.qrc \
    src/qtpropertybrowser.qrc
