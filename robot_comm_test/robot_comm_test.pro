QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

# MSVC UTF-8 support
msvc {
    QMAKE_CXXFLAGS += /utf-8
}

TARGET = robot_comm_test
TEMPLATE = app

SOURCES += main.cpp

# Install settings
target.path = $$PWD
INSTALLS += target
