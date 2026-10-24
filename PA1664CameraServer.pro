QT += core network multimedia
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = PA1664CameraServer
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/camera_server/obj
MOC_DIR = $$PWD/build/camera_server/moc

ROBOT_SOURCE_ROOT = $$clean_path($$PWD/src/robot)
INCLUDEPATH += $$PWD/src $$ROBOT_SOURCE_ROOT

win32-msvc*: QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    src/camera_server_main.cpp \
    src/camera_server.cpp

HEADERS += \
    src/camera_server.h \
    $$ROBOT_SOURCE_ROOT/drive_constant.h
