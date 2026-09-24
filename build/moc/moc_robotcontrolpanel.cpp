/****************************************************************************
** Meta object code from reading C++ file 'robotcontrolpanel.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/robotcontrolpanel.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'robotcontrolpanel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN17RobotControlPanelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN17RobotControlPanelE = QtMocHelpers::stringData(
    "RobotControlPanel",
    "setLaserProfileImage",
    "",
    "image",
    "setLaserProfilePoints",
    "QList<QVector3D>",
    "points",
    "setUsbCameraImage",
    "refreshPorts",
    "connectDrive",
    "disconnectDrive",
    "connectAllConfiguredDevices",
    "disconnectAllDevices",
    "connectConfiguredDevices",
    "connectConfiguredImu",
    "connectConfiguredLaser",
    "connectConfiguredUsbCamera",
    "saveSettings",
    "restoreSettings",
    "sendMotionCommand",
    "updateTelemetry",
    "crawling::DriveTelemetry",
    "telemetry",
    "updateState",
    "crawling::DriveState",
    "state",
    "reason",
    "updateConnection",
    "connected",
    "message",
    "updateImu",
    "crawling::ImuSample",
    "sample",
    "updateImuConnection",
    "updateSensorDetection",
    "running",
    "updateLaserDevices",
    "devices",
    "updateLaserConnection",
    "updateLaserFrame",
    "frame",
    "width",
    "height",
    "updateUsbDevices",
    "updateUsbConnection",
    "appendLog"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN17RobotControlPanelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      27,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  176,    2, 0x0a,    1 /* Public */,
       4,    1,  179,    2, 0x0a,    3 /* Public */,
       7,    1,  182,    2, 0x0a,    5 /* Public */,
       8,    0,  185,    2, 0x08,    7 /* Private */,
       9,    0,  186,    2, 0x08,    8 /* Private */,
      10,    0,  187,    2, 0x08,    9 /* Private */,
      11,    0,  188,    2, 0x08,   10 /* Private */,
      12,    0,  189,    2, 0x08,   11 /* Private */,
      13,    0,  190,    2, 0x08,   12 /* Private */,
      14,    0,  191,    2, 0x08,   13 /* Private */,
      15,    0,  192,    2, 0x08,   14 /* Private */,
      16,    0,  193,    2, 0x08,   15 /* Private */,
      17,    0,  194,    2, 0x08,   16 /* Private */,
      18,    0,  195,    2, 0x08,   17 /* Private */,
      19,    0,  196,    2, 0x08,   18 /* Private */,
      20,    1,  197,    2, 0x08,   19 /* Private */,
      23,    2,  200,    2, 0x08,   21 /* Private */,
      27,    2,  205,    2, 0x08,   24 /* Private */,
      30,    1,  210,    2, 0x08,   27 /* Private */,
      33,    2,  213,    2, 0x08,   29 /* Private */,
      34,    2,  218,    2, 0x08,   32 /* Private */,
      36,    1,  223,    2, 0x08,   35 /* Private */,
      38,    2,  226,    2, 0x08,   37 /* Private */,
      39,    4,  231,    2, 0x08,   40 /* Private */,
      43,    1,  240,    2, 0x08,   45 /* Private */,
      44,    2,  243,    2, 0x08,   47 /* Private */,
      45,    1,  248,    2, 0x08,   50 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::QImage,    3,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, QMetaType::QImage,    3,
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
    QMetaType::Void, 0x80000000 | 21,   22,
    QMetaType::Void, 0x80000000 | 24, QMetaType::QString,   25,   26,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   28,   29,
    QMetaType::Void, 0x80000000 | 31,   32,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   28,   29,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   35,   29,
    QMetaType::Void, QMetaType::QStringList,   37,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   28,   29,
    QMetaType::Void, QMetaType::UInt, QMetaType::UInt, QMetaType::UInt, QMetaType::ULongLong,   40,   41,   42,    6,
    QMetaType::Void, QMetaType::QStringList,   37,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   28,   29,
    QMetaType::Void, QMetaType::QString,   29,

       0        // eod
};

Q_CONSTINIT const QMetaObject RobotControlPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN17RobotControlPanelE.offsetsAndSizes,
    qt_meta_data_ZN17RobotControlPanelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN17RobotControlPanelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<RobotControlPanel, std::true_type>,
        // method 'setLaserProfileImage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        // method 'setLaserProfilePoints'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVector<QVector3D> &, std::false_type>,
        // method 'setUsbCameraImage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        // method 'refreshPorts'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectDrive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'disconnectDrive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectAllConfiguredDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'disconnectAllDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectConfiguredDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectConfiguredImu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectConfiguredLaser'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectConfiguredUsbCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'saveSettings'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'restoreSettings'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sendMotionCommand'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateTelemetry'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::DriveTelemetry &, std::false_type>,
        // method 'updateState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<crawling::DriveState, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updateConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updateImu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::ImuSample &, std::false_type>,
        // method 'updateImuConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updateSensorDetection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updateLaserDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QStringList &, std::false_type>,
        // method 'updateLaserConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'updateLaserFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint64, std::false_type>,
        // method 'updateUsbDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QStringList &, std::false_type>,
        // method 'updateUsbConnection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'appendLog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void RobotControlPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RobotControlPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setLaserProfileImage((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1]))); break;
        case 1: _t->setLaserProfilePoints((*reinterpret_cast< std::add_pointer_t<QList<QVector3D>>>(_a[1]))); break;
        case 2: _t->setUsbCameraImage((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1]))); break;
        case 3: _t->refreshPorts(); break;
        case 4: _t->connectDrive(); break;
        case 5: _t->disconnectDrive(); break;
        case 6: _t->connectAllConfiguredDevices(); break;
        case 7: _t->disconnectAllDevices(); break;
        case 8: _t->connectConfiguredDevices(); break;
        case 9: _t->connectConfiguredImu(); break;
        case 10: _t->connectConfiguredLaser(); break;
        case 11: _t->connectConfiguredUsbCamera(); break;
        case 12: _t->saveSettings(); break;
        case 13: _t->restoreSettings(); break;
        case 14: _t->sendMotionCommand(); break;
        case 15: _t->updateTelemetry((*reinterpret_cast< std::add_pointer_t<crawling::DriveTelemetry>>(_a[1]))); break;
        case 16: _t->updateState((*reinterpret_cast< std::add_pointer_t<crawling::DriveState>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 17: _t->updateConnection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 18: _t->updateImu((*reinterpret_cast< std::add_pointer_t<crawling::ImuSample>>(_a[1]))); break;
        case 19: _t->updateImuConnection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 20: _t->updateSensorDetection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 21: _t->updateLaserDevices((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 22: _t->updateLaserConnection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 23: _t->updateLaserFrame((*reinterpret_cast< std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<quint64>>(_a[4]))); break;
        case 24: _t->updateUsbDevices((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 25: _t->updateUsbConnection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 26: _t->appendLog((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QVector3D> >(); break;
            }
            break;
        case 15:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::DriveTelemetry >(); break;
            }
            break;
        case 16:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::DriveState >(); break;
            }
            break;
        }
    }
}

const QMetaObject *RobotControlPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RobotControlPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN17RobotControlPanelE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int RobotControlPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 27)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 27;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 27)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 27;
    }
    return _id;
}
QT_WARNING_POP
