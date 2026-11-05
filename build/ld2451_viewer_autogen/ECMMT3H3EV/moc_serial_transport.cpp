/****************************************************************************
** Meta object code from reading C++ file 'serial_transport.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/transport/serial_transport.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'serial_transport.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.3. It"
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
struct qt_meta_tag_ZN6ld245115SerialTransportE_t {};
} // unnamed namespace

template <> constexpr inline auto ld2451::SerialTransport::qt_create_metaobjectdata<qt_meta_tag_ZN6ld245115SerialTransportE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ld2451::SerialTransport",
        "scanReady",
        "",
        "std::shared_ptr<ld2451::Scan>",
        "scan",
        "ackReady",
        "command",
        "success",
        "payload",
        "frameError",
        "message",
        "baudRateChanged",
        "baudRate",
        "sequenceFinished",
        "portLost",
        "reason",
        "onReadyRead"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'scanReady'
        QtMocHelpers::SignalData<void(const std::shared_ptr<ld2451::Scan> &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'ackReady'
        QtMocHelpers::SignalData<void(quint16, bool, const QByteArray &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UShort, 6 }, { QMetaType::Bool, 7 }, { QMetaType::QByteArray, 8 },
        }}),
        // Signal 'frameError'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Signal 'baudRateChanged'
        QtMocHelpers::SignalData<void(int)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Signal 'sequenceFinished'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'portLost'
        QtMocHelpers::SignalData<void(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 15 },
        }}),
        // Slot 'onReadyRead'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SerialTransport, qt_meta_tag_ZN6ld245115SerialTransportE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ld2451::SerialTransport::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ld245115SerialTransportE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ld245115SerialTransportE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6ld245115SerialTransportE_t>.metaTypes,
    nullptr
} };

void ld2451::SerialTransport::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SerialTransport *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->scanReady((*reinterpret_cast<std::add_pointer_t<std::shared_ptr<ld2451::Scan>>>(_a[1]))); break;
        case 1: _t->ackReady((*reinterpret_cast<std::add_pointer_t<quint16>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 2: _t->frameError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->baudRateChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->sequenceFinished(); break;
        case 5: _t->portLost((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->onReadyRead(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)(const std::shared_ptr<ld2451::Scan> & )>(_a, &SerialTransport::scanReady, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)(quint16 , bool , const QByteArray & )>(_a, &SerialTransport::ackReady, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)(const QString & )>(_a, &SerialTransport::frameError, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)(int )>(_a, &SerialTransport::baudRateChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)()>(_a, &SerialTransport::sequenceFinished, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (SerialTransport::*)(const QString & )>(_a, &SerialTransport::portLost, 5))
            return;
    }
}

const QMetaObject *ld2451::SerialTransport::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ld2451::SerialTransport::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ld245115SerialTransportE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ld2451::SerialTransport::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void ld2451::SerialTransport::scanReady(const std::shared_ptr<ld2451::Scan> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ld2451::SerialTransport::ackReady(quint16 _t1, bool _t2, const QByteArray & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void ld2451::SerialTransport::frameError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void ld2451::SerialTransport::baudRateChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void ld2451::SerialTransport::sequenceFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void ld2451::SerialTransport::portLost(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
