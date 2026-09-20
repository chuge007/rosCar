QT += core gui testlib
CONFIG += c++17 console testcase warn_on
QMAKE_CXXFLAGS += /utf-8
TEMPLATE = app
TARGET = opencv_laser_contour_tests
include($$PWD/../opencv.pri)
INCLUDEPATH += $$PWD/../src
SOURCES += opencv_laser_contour_tests.cpp \
    ../src/opencv_laser_contour.cpp ../src/laser_gap_detector.cpp \
    ../src/laser_correction_controller.cpp ../src/laser_path_estimator.cpp \
    ../src/laser_seam_trajectory.cpp
HEADERS += ../src/laser_correction_controller.h
