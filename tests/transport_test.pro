# Checks the transport layer against live Rex servers, headless:
# ./transport_test [--parse-only] ADDRESS[=SESSION] ...
QT += core network
QT -= gui
CONFIG += c++17 console warn_on
CONFIG -= app_bundle
TARGET = transport_test
TEMPLATE = app
QMAKE_CXXFLAGS += -Wextra

HEADERS += ../src/endpoint.h ../src/wsclient.h ../src/rexclient.h
SOURCES += transport_test.cpp ../src/endpoint.cpp ../src/wsclient.cpp ../src/rexclient.cpp
