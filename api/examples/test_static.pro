# 静态库测试工程：链接交付目录里的 static\PA1664RobotApi.lib，不定义任何宏。
# 产物：examples\bin_static\api_test_static.exe
QT += core network gui
QT -= widgets

TEMPLATE = app
CONFIG += console c++17
CONFIG -= debug_and_release debug app_bundle
CONFIG += release

TARGET = api_test_static
DESTDIR = $$PWD/bin_static
OBJECTS_DIR = $$PWD/build_static/obj
MOC_DIR = $$PWD/build_static/moc

INCLUDEPATH += $$PWD/sdk/include
LIBS += $$PWD/sdk/static/PA1664RobotApi.lib

win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += forward_demo.cpp
