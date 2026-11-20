# 动态库测试工程：链接交付目录里的 shared\PA1664RobotApi.lib（导入库），
# 必须定义 PA1664_ROBOT_API_DLL，运行时需要同目录的 PA1664RobotApi.dll。
# 产物：examples\bin_shared\api_test_shared.exe
QT += core network gui
QT -= widgets

TEMPLATE = app
CONFIG += console c++17
CONFIG -= debug_and_release debug app_bundle
CONFIG += release

TARGET = api_test_shared
DESTDIR = $$PWD/bin_shared
OBJECTS_DIR = $$PWD/build_shared/obj
MOC_DIR = $$PWD/build_shared/moc

DEFINES += PA1664_ROBOT_API_DLL
INCLUDEPATH += $$PWD/sdk/include
LIBS += $$PWD/sdk/shared/PA1664RobotApi.lib

win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += forward_demo.cpp
