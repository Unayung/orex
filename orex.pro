QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = orex
TEMPLATE = app

# ponytail: Arch's libghostty-vt.pc points at the build-time prefix, so link directly.
LIBS += -lghostty-vt

HEADERS += \
    src/endpoint.h \
    src/wsclient.h \
    src/rexclient.h \
    src/blockstream.h \
    src/terminalitem.h \
    src/workspace.h \
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/endpoint.cpp \
    src/wsclient.cpp \
    src/rexclient.cpp \
    src/blockstream.cpp \
    src/terminalitem.cpp \
    src/workspace.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc
