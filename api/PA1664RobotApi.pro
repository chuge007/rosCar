# PA1664 第三方调用接口库
#
# 默认产出静态库（api/lib/PA1664RobotApi.lib）。
# 追加 "CONFIG+=pa1664_shared" 产出动态库（api/build/shared/：
# PA1664RobotApi.dll + 导入库 PA1664RobotApi.lib）。
#
# 依赖 Qt Core + Network + Gui：Gui 只为复用 workbench_data_codec 里的
# QImage 解码（摄像头帧、超声 A 扫批次），不涉及任何界面代码。
# 通信实现直接复用 ../../src/common 下的上位机同一份协议与通道代码，
# 因此 API 行为与 PA1664Workbench 完全一致，不会出现两套协议实现。

QT += core network gui
QT -= widgets

TEMPLATE = lib
CONFIG += c++17
CONFIG -= debug_and_release debug app_bundle
CONFIG += release

TARGET = PA1664RobotApi
# 静态库与动态库目标文件必须分开：DLL 版本定义了导出宏，复用旧 obj
# 会导致 DLL 里没有任何导出符号。
contains(CONFIG, pa1664_shared) {
    CONFIG += shared
    DEFINES += PA1664_ROBOT_API_EXPORTS
    DESTDIR = $$PWD/build/shared
    OBJECTS_DIR = $$PWD/build/obj_shared
    MOC_DIR = $$PWD/build/moc_shared
} else {
    CONFIG += staticlib
    DEFINES += PA1664_ROBOT_API_STATIC
    DESTDIR = $$PWD/lib
    OBJECTS_DIR = $$PWD/build/obj
    MOC_DIR = $$PWD/build/moc
}

COMMON = $$clean_path($$PWD/../src)
INCLUDEPATH += $$PWD/include $$COMMON $$COMMON/common $$COMMON/robot
DEPENDPATH += $$PWD/include

win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    src/pa1664_robot_api.cpp \
    $$COMMON/common/protocol/control_codec.cpp \
    $$COMMON/common/protocol/realtime_codec.cpp \
    $$COMMON/common/protocol/media_codec.cpp \
    $$COMMON/common/protocol/bulk_codec.cpp \
    $$COMMON/common/transport/control_client.cpp \
    $$COMMON/common/transport/realtime_channel.cpp \
    $$COMMON/common/transport/media_channel.cpp \
    $$COMMON/common/transport/bulk_channel.cpp \
    $$COMMON/common/services/remote_workbench_session.cpp \
    $$COMMON/common/services/workbench_data_codec.cpp \
    $$COMMON/robot_app_logger.cpp

HEADERS += \
    include/pa1664_robot_api.h \
    $$COMMON/common/protocol/protocol_types.h \
    $$COMMON/common/protocol/control_codec.h \
    $$COMMON/common/protocol/realtime_codec.h \
    $$COMMON/common/protocol/media_codec.h \
    $$COMMON/common/protocol/bulk_codec.h \
    $$COMMON/common/transport/control_client.h \
    $$COMMON/common/transport/realtime_channel.h \
    $$COMMON/common/transport/media_channel.h \
    $$COMMON/common/transport/bulk_channel.h \
    $$COMMON/common/services/remote_workbench_session.h \
    $$COMMON/common/services/workbench_data_codec.h \
    $$COMMON/robot_app_logger.h \
    $$COMMON/robot/drive_types.h
