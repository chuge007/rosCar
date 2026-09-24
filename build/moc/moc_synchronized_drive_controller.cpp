/****************************************************************************
** Meta object code from reading C++ file 'synchronized_drive_controller.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/robot/synchronized_drive_controller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'synchronized_drive_controller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8crawling27SynchronizedDriveControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN8crawling27SynchronizedDriveControllerE = QtMocHelpers::stringData(
    "crawling::SynchronizedDriveController",
    "telemetryChanged",
    "",
    "crawling::DriveTelemetry",
    "telemetry",
    "stateChanged",
    "crawling::DriveState",
    "state",
    "reason",
    "connectionChanged",
    "connected",
    "message",
    "canSettingsDetected",
    "crawling::HardwareDetectionResult",
    "result",
    "logMessage",
    "startControlLoop",
    "connectAdapter",
    "crawling::DriveSettings",
    "settings",
    "autoDetectCanDevices",
    "excludedPort",
    "disconnectAdapter",
    "setInputCommand",
    "linearMps",
    "angularRadps",
    "setCorrectionCommand",
    "requestEnable",
    "enabled",
    "emergencyStop",
    "systemReset",
    "clearAlarm",
    "shutdown",
    "controlTick"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN8crawling27SynchronizedDriveControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      17,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  116,    2, 0x06,    1 /* Public */,
       5,    2,  119,    2, 0x06,    3 /* Public */,
       9,    2,  124,    2, 0x06,    6 /* Public */,
      12,    1,  129,    2, 0x06,    9 /* Public */,
      15,    1,  132,    2, 0x06,   11 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      16,    0,  135,    2, 0x0a,   13 /* Public */,
      17,    1,  136,    2, 0x0a,   14 /* Public */,
      20,    1,  139,    2, 0x0a,   16 /* Public */,
      22,    0,  142,    2, 0x0a,   18 /* Public */,
      23,    2,  143,    2, 0x0a,   19 /* Public */,
      26,    2,  148,    2, 0x0a,   22 /* Public */,
      27,    1,  153,    2, 0x0a,   25 /* Public */,
      29,    0,  156,    2, 0x0a,   27 /* Public */,
      30,    0,  157,    2, 0x0a,   28 /* Public */,
      31,    0,  158,    2, 0x0a,   29 /* Public */,
      32,    0,  159,    2, 0x0a,   30 /* Public */,
      33,    0,  160,    2, 0x08,   31 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 6, QMetaType::QString,    7,    8,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   10,   11,
    QMetaType::Void, 0x80000000 | 13,   14,
    QMetaType::Void, QMetaType::QString,   11,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 18,   19,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,   24,   25,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,   24,   25,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject crawling::SynchronizedDriveController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN8crawling27SynchronizedDriveControllerE.offsetsAndSizes,
    qt_meta_data_ZN8crawling27SynchronizedDriveControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN8crawling27SynchronizedDriveControllerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<SynchronizedDriveController, std::true_type>,
        // method 'telemetryChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::DriveTelemetry &, std::false_type>,
        // method 'stateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<crawling::DriveState, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'connectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'canSettingsDetected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::HardwareDetectionResult &, std::false_type>,
        // method 'logMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'startControlLoop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectAdapter'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::DriveSettings &, std::false_type>,
        // method 'autoDetectCanDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'disconnectAdapter'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setInputCommand'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'setCorrectionCommand'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'requestEnable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'emergencyStop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'systemReset'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'clearAlarm'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'shutdown'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'controlTick'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void crawling::SynchronizedDriveController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SynchronizedDriveController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->telemetryChanged((*reinterpret_cast< std::add_pointer_t<crawling::DriveTelemetry>>(_a[1]))); break;
        case 1: _t->stateChanged((*reinterpret_cast< std::add_pointer_t<crawling::DriveState>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->connectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->canSettingsDetected((*reinterpret_cast< std::add_pointer_t<crawling::HardwareDetectionResult>>(_a[1]))); break;
        case 4: _t->logMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->startControlLoop(); break;
        case 6: _t->connectAdapter((*reinterpret_cast< std::add_pointer_t<crawling::DriveSettings>>(_a[1]))); break;
        case 7: _t->autoDetectCanDevices((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->disconnectAdapter(); break;
        case 9: _t->setInputCommand((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 10: _t->setCorrectionCommand((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 11: _t->requestEnable((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 12: _t->emergencyStop(); break;
        case 13: _t->systemReset(); break;
        case 14: _t->clearAlarm(); break;
        case 15: _t->shutdown(); break;
        case 16: _t->controlTick(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::DriveTelemetry >(); break;
            }
            break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::DriveState >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::HardwareDetectionResult >(); break;
            }
            break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::DriveSettings >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (SynchronizedDriveController::*)(const crawling::DriveTelemetry & );
            if (_q_method_type _q_method = &SynchronizedDriveController::telemetryChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (SynchronizedDriveController::*)(crawling::DriveState , const QString & );
            if (_q_method_type _q_method = &SynchronizedDriveController::stateChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (SynchronizedDriveController::*)(bool , const QString & );
            if (_q_method_type _q_method = &SynchronizedDriveController::connectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (SynchronizedDriveController::*)(const crawling::HardwareDetectionResult & );
            if (_q_method_type _q_method = &SynchronizedDriveController::canSettingsDetected; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (SynchronizedDriveController::*)(const QString & );
            if (_q_method_type _q_method = &SynchronizedDriveController::logMessage; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
    }
}

const QMetaObject *crawling::SynchronizedDriveController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *crawling::SynchronizedDriveController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN8crawling27SynchronizedDriveControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int crawling::SynchronizedDriveController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    return _id;
}

// SIGNAL 0
void crawling::SynchronizedDriveController::telemetryChanged(const crawling::DriveTelemetry & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void crawling::SynchronizedDriveController::stateChanged(crawling::DriveState _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void crawling::SynchronizedDriveController::connectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void crawling::SynchronizedDriveController::canSettingsDetected(const crawling::HardwareDetectionResult & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void crawling::SynchronizedDriveController::logMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
