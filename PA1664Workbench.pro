QT += core gui widgets network serialport multimedia
CONFIG += c++17 release console
CONFIG += force_debug_info
CONFIG -= app_bundle

TARGET = PA1664Workbench
contains(CONFIG, staging): TARGET = PA1664Workbench_USBHotplug
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

ROBOT_SOURCE_ROOT = $$clean_path($$PWD/src/robot)
MV3DLP_SOURCE_ROOT = $$clean_path($$PWD/vendor/mv3dlp_laser_profile)
INCLUDEPATH += $$PWD/vendor/phaselink/include $$PWD/src $$ROBOT_SOURCE_ROOT
INCLUDEPATH += $$MV3DLP_SOURCE_ROOT/include
LIBS += -L$$PWD/vendor/phaselink/lib -lclient
win32: LIBS += Setupapi.lib
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
win32-msvc*: QMAKE_LFLAGS_RELEASE += /MAP:$$shell_path($$PWD/bin/PA1664Workbench.map)
include($$PWD/opencv.pri)

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/devicecontroller.cpp \
    src/framedecoderworker.cpp \
    src/packetdecoder.cpp \
    src/scanwidgets.cpp \
    src/parameterpanel.cpp \
    src/framerecorder.cpp \
    src/frameplayer.cpp \
    src/usbbootstrap.cpp \
    src/remotecontrolserver.cpp \
    src/robotcontrolpanel.cpp \
    src/robot_app_logger.cpp \
    src/robot_sensor_controller.cpp \
    src/robot_usb_camera_controller.cpp \
    src/robot_profile_view.cpp \
    src/probeadjustmentpanel.cpp \
    src/robot_hardware_discovery_qt6.cpp \
    $$MV3DLP_SOURCE_ROOT/src/driver.cpp \
    $$MV3DLP_SOURCE_ROOT/src/vendor_sdk.cpp \
    $$ROBOT_SOURCE_ROOT/drive_settings.cpp \
    $$ROBOT_SOURCE_ROOT/differential_mixer.cpp \
    $$ROBOT_SOURCE_ROOT/wheel_synchronizer.cpp \
    $$ROBOT_SOURCE_ROOT/servo_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/mwd_rs485_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/rim302_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/wheel_motor_controller.cpp \
    $$ROBOT_SOURCE_ROOT/synchronized_drive_controller.cpp \
    $$ROBOT_SOURCE_ROOT/clamp_motor_controller.cpp

HEADERS += \
    src/mainwindow.h \
    src/devicecontroller.h \
    src/framedecoderworker.h \
    src/packetdecoder.h \
    src/scanwidgets.h \
    src/parameterpanel.h \
    src/framerecorder.h \
    src/frameplayer.h \
    src/rawpacket.h \
    src/usbbootstrap.h \
    src/remotecontrolserver.h \
    src/robotcontrolpanel.h \
    src/robot_app_logger.h \
    src/robot_sensor_controller.h \
    src/robot_usb_camera_controller.h \
    src/robot_profile_view.h \
    src/probeadjustmentpanel.h \
    $$ROBOT_SOURCE_ROOT/drive_types.h \
    $$ROBOT_SOURCE_ROOT/drive_settings.h \
    $$ROBOT_SOURCE_ROOT/differential_mixer.h \
    $$ROBOT_SOURCE_ROOT/wheel_synchronizer.h \
    $$ROBOT_SOURCE_ROOT/servo_protocol.h \
    $$ROBOT_SOURCE_ROOT/mwd_rs485_protocol.h \
    $$ROBOT_SOURCE_ROOT/rim302_protocol.h \
    $$ROBOT_SOURCE_ROOT/hardware_discovery.h \
    $$ROBOT_SOURCE_ROOT/wheel_motor_controller.h \
    $$ROBOT_SOURCE_ROOT/synchronized_drive_controller.h \
    $$ROBOT_SOURCE_ROOT/clamp_motor_controller.h \
    $$ROBOT_SOURCE_ROOT/utf8_compat.h
