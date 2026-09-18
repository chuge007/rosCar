QT += core testlib

CONFIG += c++17 console testcase warn_on
QMAKE_CXXFLAGS += /utf-8
TEMPLATE = app
TARGET = drive_core_tests

SOURCES += \
    drive_core_tests.cpp \
    ../src/differential_mixer.cpp \
    ../src/wheel_synchronizer.cpp \
    ../src/servo_protocol.cpp

HEADERS += \
    ../src/drive_types.h \
    ../src/differential_mixer.h \
    ../src/wheel_synchronizer.h \
    ../src/servo_protocol.h

INCLUDEPATH += $$PWD/../src
