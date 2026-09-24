/****************************************************************************
** Meta object code from reading C++ file 'clamp_motor_controller.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/robot/clamp_motor_controller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'clamp_motor_controller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8crawling20ClampMotorControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN8crawling20ClampMotorControllerE = QtMocHelpers::stringData(
    "crawling::ClampMotorController",
    "statusChanged",
    "",
    "message",
    "moveXPositive",
    "moveXNegative",
    "moveYPositive",
    "moveYNegative",
    "moveZPositive",
    "moveZNegative",
    "stop"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN8crawling20ClampMotorControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   62,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    0,   65,    2, 0x0a,    3 /* Public */,
       5,    0,   66,    2, 0x0a,    4 /* Public */,
       6,    0,   67,    2, 0x0a,    5 /* Public */,
       7,    0,   68,    2, 0x0a,    6 /* Public */,
       8,    0,   69,    2, 0x0a,    7 /* Public */,
       9,    0,   70,    2, 0x0a,    8 /* Public */,
      10,    0,   71,    2, 0x0a,    9 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject crawling::ClampMotorController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN8crawling20ClampMotorControllerE.offsetsAndSizes,
    qt_meta_data_ZN8crawling20ClampMotorControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN8crawling20ClampMotorControllerE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ClampMotorController, std::true_type>,
        // method 'statusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'moveXPositive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveXNegative'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveYPositive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveYNegative'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveZPositive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveZNegative'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void crawling::ClampMotorController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ClampMotorController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->statusChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->moveXPositive(); break;
        case 2: _t->moveXNegative(); break;
        case 3: _t->moveYPositive(); break;
        case 4: _t->moveYNegative(); break;
        case 5: _t->moveZPositive(); break;
        case 6: _t->moveZNegative(); break;
        case 7: _t->stop(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (ClampMotorController::*)(const QString & );
            if (_q_method_type _q_method = &ClampMotorController::statusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *crawling::ClampMotorController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *crawling::ClampMotorController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN8crawling20ClampMotorControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int crawling::ClampMotorController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void crawling::ClampMotorController::statusChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
