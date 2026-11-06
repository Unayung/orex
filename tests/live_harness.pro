QT += core gui qml quick network
CONFIG += c++17
TARGET = live_harness
LIBS += -lghostty-vt
SOURCES += live_harness.cpp ../src/endpoint.cpp ../src/wsclient.cpp ../src/rexclient.cpp ../src/blockstream.cpp ../src/terminalitem.cpp
HEADERS += ../src/endpoint.h ../src/wsclient.h ../src/rexclient.h ../src/blockstream.h ../src/terminalitem.h
