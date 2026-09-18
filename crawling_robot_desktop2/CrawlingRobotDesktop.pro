QT += core gui widgets serialport

CONFIG += c++17 warn_on
QMAKE_CXXFLAGS += /utf-8
TEMPLATE = app
TARGET = CrawlingRobotDesktop

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/main_window.cpp \
    src/drive_settings.cpp \
    src/differential_mixer.cpp \
    src/wheel_synchronizer.cpp \
    src/servo_protocol.cpp \
    src/mwd_rs485_protocol.cpp \
    src/slcan_transport.cpp \
    src/synchronized_drive_controller.cpp \
    src/wheel_motor_controller.cpp \
    src/rim302_protocol.cpp \
    src/device_controller.cpp \
    src/hardware_discovery.cpp \
    src/device_window.cpp \
    src/app_logger.cpp \
    src/point_cloud_view.cpp \
    src/laser_gap_detector.cpp \
    src/laser_path_estimator.cpp \
    src/laser_trajectory_renderer.cpp \
    src/laser_correction_controller.cpp

HEADERS += \
    src/drive_types.h \
    src/drive_settings.h \
    src/differential_mixer.h \
    src/wheel_synchronizer.h \
    src/servo_protocol.h \
    src/mwd_rs485_protocol.h \
    src/slcan_transport.h \
    src/synchronized_drive_controller.h \
    src/wheel_motor_controller.h \
    src/rim302_protocol.h \
    src/device_controller.h \
    src/hardware_discovery.h \
    src/device_window.h \
    src/app_logger.h \
    src/utf8_compat.h \
    src/point_cloud_view.h \
    src/laser_gap_detector.h \
    src/laser_path_estimator.h \
    src/laser_trajectory_renderer.h \
    src/laser_correction_controller.h \
    src/main_window.h

INCLUDEPATH += $$PWD/src \
    $$PWD/../modules/mv3dlp_laser_profile/windows_x64/include

SOURCES += \
    $$PWD/../modules/mv3dlp_laser_profile/windows_x64/src/driver.cpp \
    $$PWD/../modules/mv3dlp_laser_profile/windows_x64/src/vendor_sdk.cpp
