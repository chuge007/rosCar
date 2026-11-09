QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

# MSVC UTF-8 support
msvc {
    QMAKE_CXXFLAGS += /utf-8
}

TARGET = test_server
TEMPLATE = app

SOURCES += test_server.cpp

# Install settings
target.path = $$PWD
INSTALLS += target
