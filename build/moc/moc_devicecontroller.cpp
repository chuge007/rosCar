/****************************************************************************
** Meta object code from reading C++ file 'devicecontroller.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/devicecontroller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'devicecontroller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN16DeviceControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN16DeviceControllerE = QtMocHelpers::stringData(
    "DeviceController",
    "statusChanged",
    "",
    "message",
    "text",
    "error",
    "frameRateChanged",
    "fps",
    "received",
    "dropped",
    "connectDevice",
    "address",
    "deviceId",
    "disconnectDevice",
    "start",
    "stop",
    "resetEncoder",
    "refreshGeometry",
    "resumeAfterConfigurationChange",
    "cycleUsbForColdStart",
    "watchdog",
    "finishConnectionSettling",
    "finishConfigurationSettling",
    "tryStart",
    "confirmFrameArrival",
    "continueUsbRecovery",
    "initializeHotpluggedBootLoader"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN16DeviceControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  134,    2, 0x06,    1 /* Public */,
       3,    2,  135,    2, 0x06,    2 /* Public */,
       6,    3,  140,    2, 0x06,    5 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      10,    2,  147,    2, 0x0a,    9 /* Public */,
      10,    1,  152,    2, 0x2a,   12 /* Public | MethodCloned */,
      10,    0,  155,    2, 0x2a,   14 /* Public | MethodCloned */,
      13,    0,  156,    2, 0x0a,   15 /* Public */,
      14,    0,  157,    2, 0x0a,   16 /* Public */,
      15,    0,  158,    2, 0x0a,   17 /* Public */,
      16,    0,  159,    2, 0x0a,   18 /* Public */,
      17,    0,  160,    2, 0x0a,   19 /* Public */,
      18,    0,  161,    2, 0x0a,   20 /* Public */,
      19,    0,  162,    2, 0x0a,   21 /* Public */,
      20,    0,  163,    2, 0x08,   22 /* Private */,
      21,    0,  164,    2, 0x08,   23 /* Private */,
      22,    0,  165,    2, 0x08,   24 /* Private */,
      23,    0,  166,    2, 0x08,   25 /* Private */,
      24,    0,  167,    2, 0x08,   26 /* Private */,
      25,    0,  168,    2, 0x08,   27 /* Private */,
      26,    0,  169,    2, 0x08,   28 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    4,    5,
    QMetaType::Void, QMetaType::Double, QMetaType::ULongLong, QMetaType::ULongLong,    7,    8,    9,

 // slots: parameters
    QMetaType::Bool, QMetaType::QString, QMetaType::Int,   11,   12,
    QMetaType::Bool, QMetaType::QString,   11,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject DeviceController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN16DeviceControllerE.offsetsAndSizes,
    qt_meta_data_ZN16DeviceControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN16DeviceControllerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DeviceController, std::true_type>,
        // method 'statusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'message'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'frameRateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint64, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint64, std::false_type>,
        // method 'connectDevice'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'connectDevice'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'connectDevice'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'disconnectDevice'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'start'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'resetEncoder'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refreshGeometry'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'resumeAfterConfigurationChange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'cycleUsbForColdStart'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'watchdog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'finishConnectionSettling'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'finishConfigurationSettling'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'tryStart'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'confirmFrameArrival'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'continueUsbRecovery'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'initializeHotpluggedBootLoader'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void DeviceController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DeviceController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->statusChanged(); break;
        case 1: _t->message((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->frameRateChanged((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<quint64>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<quint64>>(_a[3]))); break;
        case 3: { bool _r = _t->connectDevice((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->connectDevice((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->connectDevice();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 6: _t->disconnectDevice(); break;
        case 7: { bool _r = _t->start();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 8: _t->stop(); break;
        case 9: _t->resetEncoder(); break;
        case 10: _t->refreshGeometry(); break;
        case 11: _t->resumeAfterConfigurationChange(); break;
        case 12: _t->cycleUsbForColdStart(); break;
        case 13: _t->watchdog(); break;
        case 14: _t->finishConnectionSettling(); break;
        case 15: _t->finishConfigurationSettling(); break;
        case 16: _t->tryStart(); break;
        case 17: _t->confirmFrameArrival(); break;
        case 18: _t->continueUsbRecovery(); break;
        case 19: _t->initializeHotpluggedBootLoader(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DeviceController::*)();
            if (_q_method_type _q_method = &DeviceController::statusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (DeviceController::*)(const QString & , bool );
            if (_q_method_type _q_method = &DeviceController::message; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (DeviceController::*)(double , quint64 , quint64 );
            if (_q_method_type _q_method = &DeviceController::frameRateChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *DeviceController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DeviceController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN16DeviceControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DeviceController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
    return _id;
}

// SIGNAL 0
void DeviceController::statusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DeviceController::message(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void DeviceController::frameRateChanged(double _t1, quint64 _t2, quint64 _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
