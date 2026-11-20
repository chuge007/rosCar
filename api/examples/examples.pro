# 第三方调用示例：控制台程序，链接 api/lib/PA1664RobotApi.lib
# 静态库内部用到 QtNetwork 与 QtGui（图像解码），示例必须同样链接。
QT += core network gui
QT -= widgets

TEMPLATE = app
CONFIG += console c++17
CONFIG -= debug_and_release debug app_bundle
CONFIG += release

TARGET = forward_demo
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
INCLUDEPATH += $$PWD/../include

LIBS += -L$$PWD/../lib -lPA1664RobotApi
# 不要加 PRE_TARGETDEPS：它会把绝对路径（可能含中文）写进 Makefile，
# 中文目录下 nmake 解析会乱码并报 U1073。先编 lib 再编示例即可。
win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += forward_demo.cpp
