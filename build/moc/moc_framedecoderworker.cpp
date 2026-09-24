/****************************************************************************
** Meta object code from reading C++ file 'framedecoderworker.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/framedecoderworker.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'framedecoderworker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN18FrameDecoderWorkerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN18FrameDecoderWorkerE = QtMocHelpers::stringData(
    "FrameDecoderWorker",
    "frameDecoded",
    "",
    "DecodedFrame",
    "frame",
    "deviceId",
    "cScanBatchDecoded",
    "DecodedFrameBatch",
    "frames",
    "decodeFailed",
    "error",
    "recordingPacketReady",
    "packet",
    "startPolling",
    "updateSettings",
    "groupOffsetWords",
    "beamCount",
    "pointCount",
    "scanModes",
    "selectedBeam",
    "amplitudeScale",
    "encoderPrecision",
    "setRecordingEnabled",
    "enabled"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN18FrameDecoderWorkerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,   56,    2, 0x06,    1 /* Public */,
       6,    2,   61,    2, 0x06,    4 /* Public */,
       9,    1,   66,    2, 0x06,    7 /* Public */,
      11,    2,   69,    2, 0x06,    9 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      13,    0,   74,    2, 0x0a,   12 /* Public */,
      14,    7,   75,    2, 0x0a,   13 /* Public */,
      22,    1,   90,    2, 0x0a,   21 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::Int,    4,    5,
    QMetaType::Void, 0x80000000 | 7, QMetaType::Int,    8,    5,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::Int,   12,    5,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Int,   15,   16,   17,   18,   19,   20,   21,
    QMetaType::Void, QMetaType::Bool,   23,

       0        // eod
};

Q_CONSTINIT const QMetaObject FrameDecoderWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN18FrameDecoderWorkerE.offsetsAndSizes,
    qt_meta_data_ZN18FrameDecoderWorkerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN18FrameDecoderWorkerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<FrameDecoderWorker, std::true_type>,
        // method 'frameDecoded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<DecodedFrame, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'cScanBatchDecoded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<DecodedFrameBatch, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'decodeFailed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'recordingPacketReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QByteArray, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'startPolling'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateSettings'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setRecordingEnabled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void FrameDecoderWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<FrameDecoderWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->frameDecoded((*reinterpret_cast< std::add_pointer_t<DecodedFrame>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->cScanBatchDecoded((*reinterpret_cast< std::add_pointer_t<DecodedFrameBatch>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 2: _t->decodeFailed((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->recordingPacketReady((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->startPolling(); break;
        case 5: _t->updateSettings((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[5])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[6])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[7]))); break;
        case 6: _t->setRecordingEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
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
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< DecodedFrame >(); break;
            }
            break;
        case 1:
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
            using _q_method_type = void (FrameDecoderWorker::*)(DecodedFrame , int );
            if (_q_method_type _q_method = &FrameDecoderWorker::frameDecoded; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (FrameDecoderWorker::*)(DecodedFrameBatch , int );
            if (_q_method_type _q_method = &FrameDecoderWorker::cScanBatchDecoded; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (FrameDecoderWorker::*)(QString );
            if (_q_method_type _q_method = &FrameDecoderWorker::decodeFailed; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (FrameDecoderWorker::*)(QByteArray , int );
            if (_q_method_type _q_method = &FrameDecoderWorker::recordingPacketReady; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *FrameDecoderWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FrameDecoderWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN18FrameDecoderWorkerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int FrameDecoderWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void FrameDecoderWorker::frameDecoded(DecodedFrame _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void FrameDecoderWorker::cScanBatchDecoded(DecodedFrameBatch _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void FrameDecoderWorker::decodeFailed(QString _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void FrameDecoderWorker::recordingPacketReady(QByteArray _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
