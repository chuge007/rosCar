#pragma once

#include <QMetaType>
#include <QSharedPointer>

// Owns the new[] buffer handed over by the vendor SDK callback.  Copies of
// this value are cheap and keep the raw storage alive across queued signals.
struct RawPacketBuffer
{
    QSharedPointer<char> bytes;
    int length = 0;
    int deviceId = 0;

    bool isValid() const { return !bytes.isNull() && length > 0; }
};

Q_DECLARE_METATYPE(RawPacketBuffer)

