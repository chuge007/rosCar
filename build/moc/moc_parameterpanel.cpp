/****************************************************************************
** Meta object code from reading C++ file 'parameterpanel.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../src/parameterpanel.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'parameterpanel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14ParameterPanelE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN14ParameterPanelE = QtMocHelpers::stringData(
    "ParameterPanel",
    "configurationChanged",
    "",
    "groupChanged",
    "message",
    "text",
    "error",
    "applyStarted",
    "applyFinished",
    "reload",
    "apply",
    "refreshGroups"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN14ParameterPanelE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   62,    2, 0x06,    1 /* Public */,
       3,    0,   63,    2, 0x06,    2 /* Public */,
       4,    2,   64,    2, 0x06,    3 /* Public */,
       7,    0,   69,    2, 0x06,    6 /* Public */,
       8,    0,   70,    2, 0x06,    7 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       9,    0,   71,    2, 0x0a,    8 /* Public */,
      10,    0,   72,    2, 0x0a,    9 /* Public */,
      11,    0,   73,    2, 0x0a,   10 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    5,    6,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ParameterPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ZN14ParameterPanelE.offsetsAndSizes,
    qt_meta_data_ZN14ParameterPanelE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN14ParameterPanelE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ParameterPanel, std::true_type>,
        // method 'configurationChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'groupChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'message'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'applyStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'applyFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'reload'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'apply'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refreshGroups'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ParameterPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ParameterPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->configurationChanged(); break;
        case 1: _t->groupChanged(); break;
        case 2: _t->message((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 3: _t->applyStarted(); break;
        case 4: _t->applyFinished(); break;
        case 5: _t->reload(); break;
        case 6: _t->apply(); break;
        case 7: _t->refreshGroups(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (ParameterPanel::*)();
            if (_q_method_type _q_method = &ParameterPanel::configurationChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (ParameterPanel::*)();
            if (_q_method_type _q_method = &ParameterPanel::groupChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (ParameterPanel::*)(const QString & , bool );
            if (_q_method_type _q_method = &ParameterPanel::message; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (ParameterPanel::*)();
            if (_q_method_type _q_method = &ParameterPanel::applyStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (ParameterPanel::*)();
            if (_q_method_type _q_method = &ParameterPanel::applyFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
    }
}

const QMetaObject *ParameterPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ParameterPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN14ParameterPanelE.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ParameterPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
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
void ParameterPanel::configurationChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ParameterPanel::groupChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ParameterPanel::message(const QString & _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ParameterPanel::applyStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ParameterPanel::applyFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
