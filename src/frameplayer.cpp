#include "frameplayer.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>

FramePlayer::FramePlayer(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &FramePlayer::next);
}

bool FramePlayer::open(const QString &path, QString *error)
{
    pause();
    m_file.close();
    m_offsets.clear();
    m_index = 0;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error) *error = m_file.errorString();
        return false;
    }
    QDataStream stream(&m_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    char magic[8]{};
    if (stream.readRawData(magic, 8) != 8 || QByteArray(magic, 8) != "PA16RAW1") {
        if (error) *error = QStringLiteral("不是 PA1664Workbench 原始帧文件");
        m_file.close();
        return false;
    }
    quint32 jsonLength = 0;
    stream >> jsonLength;
    if (jsonLength > 1024 * 1024 || m_file.bytesAvailable() < jsonLength) {
        if (error) *error = QStringLiteral("文件头损坏");
        m_file.close();
        return false;
    }
    QByteArray json(jsonLength, Qt::Uninitialized);
    stream.readRawData(json.data(), json.size());
    const auto meta = QJsonDocument::fromJson(json).object();
    m_pointCount = meta.value("pointCount").toInt();
    m_beamCount = meta.value("beamCount").toInt();
    while (!stream.atEnd()) {
        const qint64 offset = m_file.pos();
        quint64 time = 0; qint32 device = 0; quint32 length = 0;
        stream >> time >> device >> length;
        if (stream.status() != QDataStream::Ok || length > 256u * 1024u * 1024u || m_file.bytesAvailable() < length)
            break;
        m_offsets.push_back(offset);
        if (!m_file.seek(m_file.pos() + length)) break;
    }
    if (m_offsets.isEmpty() || m_pointCount <= 0 || m_beamCount <= 0) {
        if (error) *error = QStringLiteral("文件中没有有效帧");
        m_file.close();
        return false;
    }
    rewind();
    return true;
}

void FramePlayer::play()
{
    if (!m_offsets.isEmpty()) m_timer.start();
}

void FramePlayer::pause()
{
    m_timer.stop();
}

void FramePlayer::rewind()
{
    pause();
    m_index = 0;
    if (!m_offsets.isEmpty()) next();
}

void FramePlayer::next()
{
    if (m_index >= m_offsets.size()) {
        pause(); emit finished(); return;
    }
    if (!m_file.seek(m_offsets[m_index])) { pause(); return; }
    QDataStream stream(&m_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint64 time = 0; qint32 device = 0; quint32 length = 0;
    stream >> time >> device >> length;
    QByteArray packet(length, Qt::Uninitialized);
    if (stream.readRawData(packet.data(), packet.size()) != packet.size()) { pause(); return; }
    ++m_index;
    emit frameReady(packet, m_pointCount, m_beamCount);
    emit positionChanged(m_index, m_offsets.size());
}

