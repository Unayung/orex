QT += core gui qml quick network
CONFIG += c++17 console
TARGET = layout_test
LIBS += -lghostty-vt
SOURCES += layout_test.cpp ../src/workspace.cpp ../src/blockstream.cpp ../src/rexclient.cpp ../src/wsclient.cpp ../src/endpoint.cpp ../src/terminalitem.cpp ../src/theme.cpp
HEADERS += ../src/workspace.h ../src/blockstream.h ../src/rexclient.h ../src/wsclient.h ../src/endpoint.h ../src/terminalitem.h ../src/theme.h ../src/paneborder.h
