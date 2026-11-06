QT += core gui qml quick quickcontrols2 network testlib
CONFIG += c++17
TARGET = ui_harness
LIBS += -lghostty-vt
SOURCES += ui_harness.cpp ../src/wsclient.cpp ../src/rexclient.cpp ../src/blockstream.cpp ../src/terminalitem.cpp ../src/workspace.cpp ../src/theme.cpp ../src/endpoint.cpp
HEADERS += ../src/wsclient.h ../src/rexclient.h ../src/blockstream.h ../src/terminalitem.h ../src/workspace.h ../src/theme.h ../src/paneborder.h ../src/endpoint.h
RESOURCES += ../src/resources.qrc
