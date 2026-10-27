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

include(opencv.pri)
ROBOT_SOURCE_ROOT = $$clean_path($$PWD/src/robot)
MV3DLP_SOURCE_ROOT = $$clean_path($$PWD/vendor/mv3dlp_laser_profile)
INCLUDEPATH += $$PWD/vendor/phaselink/include $$PWD/src $$PWD/src/common $$ROBOT_SOURCE_ROOT
INCLUDEPATH += $$MV3DLP_SOURCE_ROOT/include
LIBS += -L$$PWD/vendor/phaselink/lib -lclient
win32: LIBS += Setupapi.lib Winusb.lib
win32-msvc*: QMAKE_CXXFLAGS += /utf-8
win32-msvc*: QMAKE_LFLAGS_RELEASE += /MAP:$$shell_path($$PWD/bin/PA1664Workbench.map)
SOURCES += \
    src/common/protocol/control_codec.cpp \
    src/common/protocol/realtime_codec.cpp \
    src/common/protocol/media_codec.cpp \
    src/common/protocol/bulk_codec.cpp \
    src/common/transport/control_client.cpp \
    src/common/transport/realtime_channel.cpp \
    src/common/transport/media_channel.cpp \
    src/common/transport/bulk_channel.cpp \
    src/common/services/remote_workbench_session.cpp \
    src/common/services/robot_json_codec.cpp \
    src/common/services/workbench_data_codec.cpp \
    src/services/ultrasound_parameter_service.cpp \
    src/services/remote_ultrasound_source.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/devicecontroller.cpp \
    src/pulseloadguard.cpp \
    src/parameterpresetstore.cpp \
    src/framedecoderworker.cpp \
    src/scanwaveformcache.cpp \
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
    src/network_stream_reader.cpp \
    src/camera_frame_processor.cpp \
    src/camera_server_client.cpp \
    src/robot_profile_view.cpp \
    src/laser_correction_controller.cpp \
    src/laser_trajectory_renderer.cpp \
    src/laser_gap_detector.cpp \
    src/laser_path_estimator.cpp \
    src/laser_seam_trajectory.cpp \
    src/seam_state_machine.cpp \
    src/opencv_laser_contour.cpp \
    src/probeadjustmentpanel.cpp \
    src/robot_hardware_discovery_qt6.cpp \
    $$MV3DLP_SOURCE_ROOT/src/driver.cpp \
    $$MV3DLP_SOURCE_ROOT/src/vendor_sdk.cpp \
    $$ROBOT_SOURCE_ROOT/drive_settings.cpp \
    $$ROBOT_SOURCE_ROOT/differential_mixer.cpp \
    $$ROBOT_SOURCE_ROOT/wheel_synchronizer.cpp \
    $$ROBOT_SOURCE_ROOT/servo_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/mwd_rs485_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/mwd_modbus_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/rim302_protocol.cpp \
    $$ROBOT_SOURCE_ROOT/wheel_motor_controller.cpp \
    $$ROBOT_SOURCE_ROOT/synchronized_drive_controller.cpp \
    $$ROBOT_SOURCE_ROOT/clamp_motor_controller.cpp
SOURCES += $$ROBOT_SOURCE_ROOT/zdt_can_protocol.cpp \
           $$ROBOT_SOURCE_ROOT/candle_can_adapter.cpp \
           $$ROBOT_SOURCE_ROOT/pcan_can_adapter.cpp \
           $$ROBOT_SOURCE_ROOT/clamp_motor_widgets.cpp

HEADERS += \
    src/common/protocol/protocol_types.h \
    src/common/protocol/control_codec.h \
    src/common/protocol/realtime_codec.h \
    src/common/protocol/media_codec.h \
    src/common/protocol/bulk_codec.h \
    src/common/transport/control_client.h \
    src/common/transport/realtime_channel.h \
    src/common/transport/media_channel.h \
    src/common/transport/bulk_channel.h \
    src/common/services/remote_workbench_session.h \
    src/common/services/robot_json_codec.h \
    src/common/services/workbench_data_codec.h \
    src/common/services/parameter_key.h \
    src/services/ultrasound_parameter_service.h \
    src/services/remote_ultrasound_source.h \
    src/mainwindow.h \
    src/devicecontroller.h \
    src/pulseloadguard.h \
    src/parameterpresetstore.h \
    src/framedecoderworker.h \
    src/scanwaveformcache.h \
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
    src/network_stream_reader.h \
    src/camera_frame_processor.h \
    src/camera_server_client.h \
    src/robot_profile_view.h \
    src/laser_correction_controller.h \
    src/laser_trajectory_renderer.h \
    src/laser_gap_detector.h \
    src/laser_path_estimator.h \
    src/laser_seam_trajectory.h \
    src/seam_state_machine.h \
    src/profile_weld_detector.h \
    src/opencv_laser_contour.h \
    src/probeadjustmentpanel.h \
    $$ROBOT_SOURCE_ROOT/drive_types.h \
    $$ROBOT_SOURCE_ROOT/drive_settings.h \
    $$ROBOT_SOURCE_ROOT/differential_mixer.h \
    $$ROBOT_SOURCE_ROOT/wheel_synchronizer.h \
    $$ROBOT_SOURCE_ROOT/servo_protocol.h \
    $$ROBOT_SOURCE_ROOT/mwd_rs485_protocol.h \
    $$ROBOT_SOURCE_ROOT/mwd_modbus_protocol.h \
    $$ROBOT_SOURCE_ROOT/rim302_protocol.h \
    $$ROBOT_SOURCE_ROOT/hardware_discovery.h \
    $$ROBOT_SOURCE_ROOT/wheel_motor_controller.h \
    $$ROBOT_SOURCE_ROOT/synchronized_drive_controller.h \
    $$ROBOT_SOURCE_ROOT/clamp_motor_controller.h \
    $$ROBOT_SOURCE_ROOT/zdt_can_protocol.h \
    $$ROBOT_SOURCE_ROOT/candle_can_adapter.h \
    $$ROBOT_SOURCE_ROOT/pcan_can_adapter.h \
    $$ROBOT_SOURCE_ROOT/clamp_motor_widgets.h \
    $$ROBOT_SOURCE_ROOT/utf8_compat.h
