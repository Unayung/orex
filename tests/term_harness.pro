# Builds TerminalItem on its own, without the Rex protocol files, so it can be
# checked offscreen: QT_QPA_PLATFORM=offscreen ./term_harness stream.bin outdir
QT += core gui quick
CONFIG += c++17 console warn_on
CONFIG -= app_bundle
TARGET = term_harness
TEMPLATE = app

INCLUDEPATH += ../src
# Arch's libghostty-vt.pc points at the build-time prefix, so link directly.
LIBS += -lghostty-vt
QMAKE_CXXFLAGS += -Wextra

HEADERS += ../src/terminalitem.h
SOURCES += term_harness.cpp ../src/terminalitem.cpp
