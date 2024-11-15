QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

CONFIG += c++17

QMAKE_CXXFLAGS += -O1
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    acqcontrol.cpp \
    go4server.cpp \
    go4socket.cpp \
    main.cpp \
    cronoacqdlg.cpp \
    qcustomplot.cpp \
    qtpropertybrowser/qtbuttonpropertybrowser.cpp \
    qtpropertybrowser/qteditorfactory.cpp \
    qtpropertybrowser/qtgroupboxpropertybrowser.cpp \
    qtpropertybrowser/qtpropertybrowser.cpp \
    qtpropertybrowser/qtpropertybrowserutils.cpp \
    qtpropertybrowser/qtpropertymanager.cpp \
    qtpropertybrowser/qttreepropertybrowser.cpp \
    qtpropertybrowser/qtvariantproperty.cpp \
    treeitem.cpp \
    treemodel.cpp

HEADERS += \
    acqcontrol.h \
    cronoacqdlg.h \
    cronotypes.h \
    go4server.h \
    go4socket.h \
    qcustomplot.h \
    qtpropertybrowser/qtbuttonpropertybrowser.h \
    qtpropertybrowser/qteditorfactory.h \
    qtpropertybrowser/qtgroupboxpropertybrowser.h \
    qtpropertybrowser/qtpropertybrowser.h \
    qtpropertybrowser/qtpropertybrowserutils_p.h \
    qtpropertybrowser/qtpropertymanager.h \
    qtpropertybrowser/qttreepropertybrowser.h \
    qtpropertybrowser/qtvariantproperty.h \
    treeitem.h \
    treemodel.h

FORMS += \
    cronoacqdlg.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH += qtpropertybrowser/


#QMAKE_LFLAGS += -x -E

RESOURCES += \
    cronoacq_ressources.qrc \

DISTFILES += \
    tree.txt

#win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../../../../../../../root_v6.28.06/lib/ -llibCore -llibThread -llibRIO -llibNet
#else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../../../../../../../root_v6.28.06/lib/ -llibCore -llibThread -llibRIO -llibNet
#else:unix: LIBS += -L$$PWD/../../../../../../../root_v6.28.06/lib/ -llibCore -llibThread -llibRIO -llibNet

#INCLUDEPATH += $$PWD/../../../../../../../root_v6.28.06/include
#DEPENDPATH += $$PWD/../../../../../../../root_v6.28.06/include

win32: LIBS += -L$$PWD/driver/x64/ -lcrono_tools_64 -lndigo_driver_64
unix: LIBS += -L$$PWD/../../ndigo5g_babel/lib/x64/ -lndigo_driver

INCLUDEPATH += $$PWD/../../ndigo5g_babel/include
DEPENDPATH += $$PWD/../../ndigo5g_babel/include
