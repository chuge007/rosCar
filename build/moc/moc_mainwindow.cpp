/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/mainwindow.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN10MainWindowE = QtMocHelpers::stringData(
    "MainWindow",
    "decoderSettingsChanged",
    "",
    "groupOffsetWords",
    "beamCount",
    "pointCount",
    "scanModes",
    "selectedBeam",
    "amplitudeScale",
    "encoderPrecision",
    "decoderRecordingChanged",
    "enabled",
    "connectDevice",
    "acceptDecodedFrame",
    "DecodedFrame",
    "frame",
    "deviceId",
    "acceptCScanBatch",
    "DecodedFrameBatch",
    "frames",
    "updateStatus",
    "updateParametersForPlot",
    "saveConfig",
    "loadConfig",
    "openRecording",
    "toggleRecording",
    "openRemoteDesktop",
    "openRobotControl",
    "processPlaybackFrame",
    "packet"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10MainWindowE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    7,   98,    2, 0x06,    1 /* Public */,
      10,    1,  113,    2, 0x06,    9 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      12,    0,  116,    2, 0x08,   11 /* Private */,
      13,    2,  117,    2, 0x08,   12 /* Private */,
      17,    2,  122,    2, 0x08,   15 /* Private */,
      20,    0,  127,    2, 0x08,   18 /* Private */,
      21,    0,  128,    2, 0x08,   19 /* Private */,
      22,    0,  129,    2, 0x08,   20 /* Private */,
      23,    0,  130,    2, 0x08,   21 /* Private */,
      24,    0,  131,    2, 0x08,   22 /* Private */,
      25,    0,  132,    2, 0x08,   23 /* Private */,
      26,    0,  133,    2, 0x08,   24 /* Private */,
      27,    0,  134,    2, 0x08,   25 /* Private */,
      28,    3,  135,    2, 0x08,   26 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,    3,    4,    5,    6,    7,    8,    9,
    QMetaType::Void, QMetaType::Bool,   11,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 14, QMetaType::Int,   15,   16,
    QMetaType::Void, 0x80000000 | 18, QMetaType::Int,   19,   16,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::Int, QMetaType::Int,   29,    5,    4,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_ZN10MainWindowE.offsetsAndSizes,
    qt_meta_data_ZN10MainWindowE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN10MainWindowE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'decoderSettingsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'decoderRecordingChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'connectDevice'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'acceptDecodedFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<DecodedFrame, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'acceptCScanBatch'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<DecodedFrameBatch, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateParametersForPlot'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'saveConfig'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'loadConfig'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggleRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openRemoteDesktop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openRobotControl'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'processPlaybackFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QByteArray &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->decoderSettingsChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[7]))); break;
        case 1: _t->decoderRecordingChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->connectDevice(); break;
        case 3: _t->acceptDecodedFrame((*reinterpret_cast< std::add_pointer_t<DecodedFrame>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->acceptCScanBatch((*reinterpret_cast< std::add_pointer_t<DecodedFrameBatch>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 5: _t->updateStatus(); break;
        case 6: _t->updateParametersForPlot(); break;
        case 7: _t->saveConfig(); break;
        case 8: _t->loadConfig(); break;
        case 9: _t->openRecording(); break;
        case 10: _t->toggleRecording(); break;
        case 11: _t->openRemoteDesktop(); break;
        case 12: _t->openRobotControl(); break;
        case 13: _t->processPlaybackFrame((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< DecodedFrame >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< DecodedFrameBatch >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (MainWindow::*)(int , int , int , int , int , int , int );
            if (_q_method_type _q_method = &MainWindow::decoderSettingsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (MainWindow::*)(bool );
            if (_q_method_type _q_method = &MainWindow::decoderRecordingChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN10MainWindowE.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::decoderSettingsChanged(int _t1, int _t2, int _t3, int _t4, int _t5, int _t6, int _t7)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t6))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t7))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void MainWindow::decoderRecordingChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_WARNING_POP
