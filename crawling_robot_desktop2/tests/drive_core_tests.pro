QT += core gui testlib

CONFIG += c++17 console testcase warn_on
QMAKE_CXXFLAGS += /utf-8
TEMPLATE = app
TARGET = drive_core_tests

SOURCES += \
    drive_core_tests.cpp \
    ../src/drive_settings.cpp \
    ../src/laser_gap_detector.cpp \
    ../src/laser_path_estimator.cpp \
    ../src/laser_trajectory_renderer.cpp \
    ../src/laser_correction_controller.cpp \
    ../src/differential_mixer.cpp \
    ../src/wheel_synchronizer.cpp \
    ../src/servo_protocol.cpp \
    ../src/mwd_rs485_protocol.cpp

HEADERS += \
    ../src/drive_types.h \
    ../src/drive_settings.h \
    ../src/laser_gap_detector.h \
    ../src/laser_path_estimator.h \
    ../src/laser_trajectory_renderer.h \
    ../src/laser_correction_controller.h \
    ../src/differential_mixer.h \
    ../src/wheel_synchronizer.h \
    ../src/servo_protocol.h \
    ../src/mwd_rs485_protocol.h

INCLUDEPATH += $$PWD/../src
