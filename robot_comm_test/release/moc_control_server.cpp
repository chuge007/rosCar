/****************************************************************************
** Meta object code from reading C++ file 'control_server.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.4)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/common/transport/control_server.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'control_server.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.4. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_pa1664__remote__ControlServer_t {
    QByteArrayData data[13];
    char stringdata0[152];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_pa1664__remote__ControlServer_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_pa1664__remote__ControlServer_t qt_meta_stringdata_pa1664__remote__ControlServer = {
    {
QT_MOC_LITERAL(0, 0, 29), // "pa1664::remote::ControlServer"
QT_MOC_LITERAL(1, 30, 13), // "sessionOpened"
QT_MOC_LITERAL(2, 44, 0), // ""
QT_MOC_LITERAL(3, 45, 9), // "sessionId"
QT_MOC_LITERAL(4, 55, 8), // "clientId"
QT_MOC_LITERAL(5, 64, 11), // "peerAddress"
QT_MOC_LITERAL(6, 76, 13), // "sessionClosed"
QT_MOC_LITERAL(7, 90, 6), // "reason"
QT_MOC_LITERAL(8, 97, 15), // "requestReceived"
QT_MOC_LITERAL(9, 113, 9), // "requestId"
QT_MOC_LITERAL(10, 123, 7), // "request"
QT_MOC_LITERAL(11, 131, 13), // "protocolError"
QT_MOC_LITERAL(12, 145, 6) // "detail"

    },
    "pa1664::remote::ControlServer\0"
    "sessionOpened\0\0sessionId\0clientId\0"
    "peerAddress\0sessionClosed\0reason\0"
    "requestReceived\0requestId\0request\0"
    "protocolError\0detail"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_pa1664__remote__ControlServer[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       4,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    3,   34,    2, 0x06 /* Public */,
       6,    2,   41,    2, 0x06 /* Public */,
       8,    3,   46,    2, 0x06 /* Public */,
      11,    2,   53,    2, 0x06 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString, QMetaType::QString,    3,    4,    5,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString,    3,    7,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::ULongLong, QMetaType::QJsonObject,    3,    9,   10,
    QMetaType::Void, QMetaType::ULongLong, QMetaType::QString,    3,   12,

       0        // eod
};

void pa1664::remote::ControlServer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ControlServer *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->sessionOpened((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 1: _t->sessionClosed((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 2: _t->requestReceived((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< quint64(*)>(_a[2])),(*reinterpret_cast< const QJsonObject(*)>(_a[3]))); break;
        case 3: _t->protocolError((*reinterpret_cast< quint64(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ControlServer::*)(quint64 , const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ControlServer::sessionOpened)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ControlServer::*)(quint64 , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ControlServer::sessionClosed)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ControlServer::*)(quint64 , quint64 , const QJsonObject & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ControlServer::requestReceived)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ControlServer::*)(quint64 , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&ControlServer::protocolError)) {
                *result = 3;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject pa1664::remote::ControlServer::staticMetaObject = { {
    &QObject::staticMetaObject,
    qt_meta_stringdata_pa1664__remote__ControlServer.data,
    qt_meta_data_pa1664__remote__ControlServer,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *pa1664::remote::ControlServer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *pa1664::remote::ControlServer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_pa1664__remote__ControlServer.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int pa1664::remote::ControlServer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void pa1664::remote::ControlServer::sessionOpened(quint64 _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void pa1664::remote::ControlServer::sessionClosed(quint64 _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void pa1664::remote::ControlServer::requestReceived(quint64 _t1, quint64 _t2, const QJsonObject & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void pa1664::remote::ControlServer::protocolError(quint64 _t1, const QString & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
