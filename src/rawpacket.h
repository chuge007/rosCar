#pragma once

#include <QMetaType>
#include <QSharedPointer>
#include <QtGlobal>

// Owns the new[] buffer handed over by the vendor SDK callback.  Copies of
// this value are cheap and keep the raw storage alive across queued signals.
struct RawPacketBuffer
{
    QSharedPointer<char> bytes;
    int length = 0;
    int deviceId = 0;
    quint64 ingestSequence = 0;
    quint64 ingestMonotonicNs = 0;
    quint32 captureEpoch = 0;

    bool isValid() const { return !bytes.isNull() && length > 0; }
    int packetBytes() const { return qMax(0, length); }
};

Q_DECLARE_METATYPE(RawPacketBuffer)

