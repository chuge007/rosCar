/****************************************************************************
** Meta object code from reading C++ file 'robot_sensor_controller.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/robot_sensor_controller.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'robot_sensor_controller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8crawling21RobotSensorControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN8crawling21RobotSensorControllerE = QtMocHelpers::stringData(
    "crawling::RobotSensorController",
    "imuConnectionChanged",
    "",
    "connected",
    "message",
    "sensorSettingsDetected",
    "imuPort",
    "imuBaudRate",
    "laserSerialNumber",
    "deviceDetectionChanged",
    "running",
    "imuSampleChanged",
    "crawling::ImuSample",
    "sample",
    "cameraDevicesChanged",
    "devices",
    "cameraConnectionChanged",
    "cameraFrameChanged",
    "frameNumber",
    "width",
    "height",
    "pointCount",
    "pointCloudProfileChanged",
    "QList<QVector3D>",
    "points",
    "pointCloudProfileReady",
    "cameraImageFrameChanged",
    "image",
    "correctionCameraFrameReady",
    "sourceFrameNumber",
    "receivedAtEpochMs",
    "correctionProfileFrameReady",
    "cameraImageReady",
    "correctionProfilePrepared",
    "ready",
    "cameraProfileModeChanged",
    "active",
    "logMessage",
    "connectImu",
    "portName",
    "baudRate",
    "divider",
    "disconnectImu",
    "autoDetectDevices",
    "preferredImuPort",
    "preferredLaserSerial",
    "scanCamera",
    "connectCamera",
    "serialNumber",
    "prepareCorrectionProfile",
    "restoreOriginalPreview",
    "disconnectCamera",
    "shutdown",
    "readImu",
    "checkImuHealth",
    "captureCameraFrame"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN8crawling21RobotSensorControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      28,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      16,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,  182,    2, 0x06,    1 /* Public */,
       5,    3,  187,    2, 0x06,    4 /* Public */,
       9,    2,  194,    2, 0x06,    8 /* Public */,
      11,    1,  199,    2, 0x06,   11 /* Public */,
      14,    1,  202,    2, 0x06,   13 /* Public */,
      16,    2,  205,    2, 0x06,   15 /* Public */,
      17,    4,  210,    2, 0x06,   18 /* Public */,
      22,    1,  219,    2, 0x06,   23 /* Public */,
      25,    0,  222,    2, 0x06,   25 /* Public */,
      26,    1,  223,    2, 0x06,   26 /* Public */,
      28,    3,  226,    2, 0x06,   28 /* Public */,
      31,    3,  233,    2, 0x06,   32 /* Public */,
      32,    0,  240,    2, 0x06,   36 /* Public */,
      33,    1,  241,    2, 0x06,   37 /* Public */,
      35,    1,  244,    2, 0x06,   39 /* Public */,
      37,    1,  247,    2, 0x06,   41 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      38,    3,  250,    2, 0x0a,   43 /* Public */,
      42,    0,  257,    2, 0x0a,   47 /* Public */,
      43,    2,  258,    2, 0x0a,   48 /* Public */,
      46,    0,  263,    2, 0x0a,   51 /* Public */,
      47,    1,  264,    2, 0x0a,   52 /* Public */,
      49,    0,  267,    2, 0x0a,   54 /* Public */,
      50,    0,  268,    2, 0x0a,   55 /* Public */,
      51,    0,  269,    2, 0x0a,   56 /* Public */,
      52,    0,  270,    2, 0x0a,   57 /* Public */,
      53,    0,  271,    2, 0x08,   58 /* Private */,
      54,    0,  272,    2, 0x08,   59 /* Private */,
      55,    0,  273,    2, 0x08,   60 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,    3,    4,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, QMetaType::QString,    6,    7,    8,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,   10,    4,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void, QMetaType::QStringList,   15,
    QMetaType::Void, QMetaType::Bool, QMetaType::QString,    3,    4,
    QMetaType::Void, QMetaType::UInt, QMetaType::UInt, QMetaType::UInt, QMetaType::ULongLong,   18,   19,   20,   21,
    QMetaType::Void, 0x80000000 | 23,   24,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QImage,   27,
    QMetaType::Void, QMetaType::QImage, QMetaType::UInt, QMetaType::LongLong,   27,   29,   30,
    QMetaType::Void, 0x80000000 | 23, QMetaType::UInt, QMetaType::LongLong,   24,   29,   30,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   34,
    QMetaType::Void, QMetaType::Bool,   36,
    QMetaType::Void, QMetaType::QString,    4,

 // slots: parameters
    QMetaType::Void, QMetaType::QString, QMetaType::Int, QMetaType::Int,   39,   40,   41,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::QString,   44,   45,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   48,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject crawling::RobotSensorController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN8crawling21RobotSensorControllerE.offsetsAndSizes,
    qt_meta_data_ZN8crawling21RobotSensorControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN8crawling21RobotSensorControllerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<RobotSensorController, std::true_type>,
        // method 'imuConnectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'sensorSettingsDetected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'deviceDetectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'imuSampleChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const crawling::ImuSample &, std::false_type>,
        // method 'cameraDevicesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QStringList &, std::false_type>,
        // method 'cameraConnectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'cameraFrameChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint64, std::false_type>,
        // method 'pointCloudProfileChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVector<QVector3D> &, std::false_type>,
        // method 'pointCloudProfileReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'cameraImageFrameChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        // method 'correctionCameraFrameReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'correctionProfileFrameReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVector<QVector3D> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<quint32, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'cameraImageReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'correctionProfilePrepared'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'cameraProfileModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'logMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'connectImu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'disconnectImu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'autoDetectDevices'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'scanCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'connectCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'prepareCorrectionProfile'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'restoreOriginalPreview'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'disconnectCamera'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'shutdown'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'readImu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'checkImuHealth'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'captureCameraFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void crawling::RobotSensorController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RobotSensorController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->imuConnectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->sensorSettingsDetected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 2: _t->deviceDetectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->imuSampleChanged((*reinterpret_cast< std::add_pointer_t<crawling::ImuSample>>(_a[1]))); break;
        case 4: _t->cameraDevicesChanged((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 5: _t->cameraConnectionChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 6: _t->cameraFrameChanged((*reinterpret_cast< std::add_pointer_t<quint32>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<quint64>>(_a[4]))); break;
        case 7: _t->pointCloudProfileChanged((*reinterpret_cast< std::add_pointer_t<QList<QVector3D>>>(_a[1]))); break;
        case 8: _t->pointCloudProfileReady(); break;
        case 9: _t->cameraImageFrameChanged((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1]))); break;
        case 10: _t->correctionCameraFrameReady((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[3]))); break;
        case 11: _t->correctionProfileFrameReady((*reinterpret_cast< std::add_pointer_t<QList<QVector3D>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<quint32>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[3]))); break;
        case 12: _t->cameraImageReady(); break;
        case 13: _t->correctionProfilePrepared((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->cameraProfileModeChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 15: _t->logMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->connectImu((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 17: _t->disconnectImu(); break;
        case 18: _t->autoDetectDevices((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 19: _t->scanCamera(); break;
        case 20: _t->connectCamera((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 21: _t->prepareCorrectionProfile(); break;
        case 22: _t->restoreOriginalPreview(); break;
        case 23: _t->disconnectCamera(); break;
        case 24: _t->shutdown(); break;
        case 25: _t->readImu(); break;
        case 26: _t->checkImuHealth(); break;
        case 27: _t->captureCameraFrame(); break;
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
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< crawling::ImuSample >(); break;
            }
            break;
        case 7:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QVector3D> >(); break;
            }
            break;
        case 11:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QVector3D> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (RobotSensorController::*)(bool , const QString & );
            if (_q_method_type _q_method = &RobotSensorController::imuConnectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QString & , int , const QString & );
            if (_q_method_type _q_method = &RobotSensorController::sensorSettingsDetected; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(bool , const QString & );
            if (_q_method_type _q_method = &RobotSensorController::deviceDetectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const crawling::ImuSample & );
            if (_q_method_type _q_method = &RobotSensorController::imuSampleChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QStringList & );
            if (_q_method_type _q_method = &RobotSensorController::cameraDevicesChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(bool , const QString & );
            if (_q_method_type _q_method = &RobotSensorController::cameraConnectionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(quint32 , quint32 , quint32 , quint64 );
            if (_q_method_type _q_method = &RobotSensorController::cameraFrameChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QVector<QVector3D> & );
            if (_q_method_type _q_method = &RobotSensorController::pointCloudProfileChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)();
            if (_q_method_type _q_method = &RobotSensorController::pointCloudProfileReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QImage & );
            if (_q_method_type _q_method = &RobotSensorController::cameraImageFrameChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QImage & , quint32 , qint64 );
            if (_q_method_type _q_method = &RobotSensorController::correctionCameraFrameReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QVector<QVector3D> & , quint32 , qint64 );
            if (_q_method_type _q_method = &RobotSensorController::correctionProfileFrameReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)();
            if (_q_method_type _q_method = &RobotSensorController::cameraImageReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 12;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(bool );
            if (_q_method_type _q_method = &RobotSensorController::correctionProfilePrepared; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 13;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(bool );
            if (_q_method_type _q_method = &RobotSensorController::cameraProfileModeChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 14;
                return;
            }
        }
        {
            using _q_method_type = void (RobotSensorController::*)(const QString & );
            if (_q_method_type _q_method = &RobotSensorController::logMessage; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 15;
                return;
            }
        }
    }
}

const QMetaObject *crawling::RobotSensorController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *crawling::RobotSensorController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN8crawling21RobotSensorControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int crawling::RobotSensorController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 28)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 28;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 28)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 28;
    }
    return _id;
}

// SIGNAL 0
void crawling::RobotSensorController::imuConnectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void crawling::RobotSensorController::sensorSettingsDetected(const QString & _t1, int _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void crawling::RobotSensorController::deviceDetectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void crawling::RobotSensorController::imuSampleChanged(const crawling::ImuSample & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void crawling::RobotSensorController::cameraDevicesChanged(const QStringList & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void crawling::RobotSensorController::cameraConnectionChanged(bool _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void crawling::RobotSensorController::cameraFrameChanged(quint32 _t1, quint32 _t2, quint32 _t3, quint64 _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void crawling::RobotSensorController::pointCloudProfileChanged(const QVector<QVector3D> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void crawling::RobotSensorController::pointCloudProfileReady()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void crawling::RobotSensorController::cameraImageFrameChanged(const QImage & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void crawling::RobotSensorController::correctionCameraFrameReady(const QImage & _t1, quint32 _t2, qint64 _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void crawling::RobotSensorController::correctionProfileFrameReady(const QVector<QVector3D> & _t1, quint32 _t2, qint64 _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void crawling::RobotSensorController::cameraImageReady()
{
    QMetaObject::activate(this, &staticMetaObject, 12, nullptr);
}

// SIGNAL 13
void crawling::RobotSensorController::correctionProfilePrepared(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void crawling::RobotSensorController::cameraProfileModeChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 14, _a);
}

// SIGNAL 15
void crawling::RobotSensorController::logMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}
QT_WARNING_POP
