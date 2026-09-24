/****************************************************************************
** Meta object code from reading C++ file 'robot_usb_camera_controller.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/robot_usb_camera_controller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'robot_usb_camera_controller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8crawling24RobotUsbCameraControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN8crawling24RobotUsbCameraControllerE = QtMocHelpers::stringData(
    "crawling::RobotUsbCameraController",
    "devicesChanged",
    "",
    "devices",
    "connectionChanged",
    "connected",
    "message",
    "frameChanged",
    "image",
    "logMessage",
    "scanDevices",
    "connectCamera",
    "deviceIndex",
    "fps",
    "flipHorizontal",
    "flipVertical",
    "disconnectCamera",
    "shutdown",
    "captureFrame"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN8crawling24RobotUsbCameraControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   68,    2, 0x06,    1 /* Public */,
       4,    2,   71,    2, 0x06,    3 /* Public */,
       7,    1,   76,    2, 0x06,    6 /* Public */,
       9,    1,   79,    2, 0x06,    8 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      10,    0,   82,    2, 0x0a,   10 /* Public */,
      11,    4,   83,    2, 0x0a,   11 /* Public */,
      16,    0,   92,    2, 0x0a,   16 /* Public */,
      17,    0,   93,    2, 0x0a,   17 /* Public */,
      18,    0,   94,    2, 0x08,   18 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QStringList,    3,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,    5,    6,
    QMetaType::Void, QMetaType::QImage,    8,
    QMetaType::Void, QMetaType::QString,    6,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Bool, QMetaType::Bool,   12,   13,   14,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject crawling::RobotUsbCameraController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN8crawling24RobotUsbCameraControllerE.offsetsAndSizes,
    qt_meta_data_ZN8crawling24RobotUsbCameraControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN8crawling24RobotUsbCameraControllerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<RobotUsbCameraController, std::true_type>,
        // method 'devicesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QStringList &, std::false_type>,
        // method 'connectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'frameChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        // method 'logMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'scanDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'disconnectCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'shutdown'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'captureFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void crawling::RobotUsbCameraController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RobotUsbCameraController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->devicesChanged((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 1: _t->connectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->frameChanged((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1]))); break;
        case 3: _t->logMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->scanDevices(); break;
        case 5: _t->connectCamera((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[4]))); break;
        case 6: _t->disconnectCamera(); break;
        case 7: _t->shutdown(); break;
        case 8: _t->captureFrame(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (RobotUsbCameraController::*)(const QStringList & );
            if (_q_method_type _q_method = &RobotUsbCameraController::devicesChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (RobotUsbCameraController::*)(bool , const QString & );
            if (_q_method_type _q_method = &RobotUsbCameraController::connectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (RobotUsbCameraController::*)(const QImage & );
            if (_q_method_type _q_method = &RobotUsbCameraController::frameChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (RobotUsbCameraController::*)(const QString & );
            if (_q_method_type _q_method = &RobotUsbCameraController::logMessage; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *crawling::RobotUsbCameraController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *crawling::RobotUsbCameraController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN8crawling24RobotUsbCameraControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int crawling::RobotUsbCameraController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void crawling::RobotUsbCameraController::devicesChanged(const QStringList & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void crawling::RobotUsbCameraController::connectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void crawling::RobotUsbCameraController::frameChanged(const QImage & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void crawling::RobotUsbCameraController::logMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
