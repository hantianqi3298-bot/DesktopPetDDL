QT       += core gui widgets

CONFIG += c++17

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    ddldialog.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    ddldialog.h \
    mainwindow.h

FORMS += \
    ddldialog.ui \
    mainwindow.ui

RESOURCES += \
    res.qrc

win32:LIBS += -ldwmapi

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
