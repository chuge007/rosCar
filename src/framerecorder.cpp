#include "framerecorder.h"

#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QTimer>

FrameRecorder::FrameRecorder(QObject *parent) : QObject(parent)
{
    m_writeTimer = new QTimer(this);
    m_writeTimer->setTimerType(Qt::CoarseTimer);
    m_writeTimer->setInterval(15);
    connect(m_writeTimer, &QTimer::timeout, this, &FrameRecorder::drainQueue);
}

QString FrameRecorder::fileName() const
{
    QMutexLocker locker(&m_nameMutex);
    return m_fileName;
}

bool FrameRecorder::start(const QString &fileName, int pointCount, int beamCount,
                          int groupId)
{
    if (m_recording.load(std::memory_order_relaxed) || m_file.isOpen())
        stop();
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.clear();
        m_queueBytes = 0;
    }
    m_droppedFrames.store(0, std::memory_order_relaxed);
    m_frames = 0;
    m_file.setFileName(fileName);
    if (!m_file.open(QIODevice::WriteOnly)) {
        emit error(QStringLiteral("无法创建数据文件：%1").arg(m_file.errorString()));
        return false;
    }

    const QJsonObject meta{{"format", "PA1664_RAW_V1"},
                           {"created", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
                           {"pointCount", pointCount},
                           {"beamCount", beamCount},
                           {"groupId", groupId}};
    const QByteArray json = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    QDataStream stream(&m_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("PA16RAW1", 8);
    stream << quint32(json.size());
    if (stream.writeRawData(json.constData(), json.size()) != json.size()) {
        const QString reason = m_file.errorString();
        m_file.close();
        emit error(QStringLiteral("写入录制文件头失败：%1").arg(reason));
        return false;
    }

    {
        QMutexLocker locker(&m_nameMutex);
        m_fileName = fileName;
    }
    m_flushClock.start();
    m_recording.store(true, std::memory_order_release);
    m_writeTimer->start();
    emit stateChanged(true, fileName);
    return true;
}

void FrameRecorder::stop()
{
    m_recording.store(false, std::memory_order_release);
    if (m_writeTimer)
        m_writeTimer->stop();
    if (!m_file.isOpen())
        return;

    // Finish the bounded queue before closing so a normal operator Stop does
    // not truncate frames already accepted by the recorder.
    writeQueuedPackets(250);
    m_file.flush();
    QString path;
    {
        QMutexLocker locker(&m_nameMutex);
        path = m_fileName;
    }
    m_file.close();
    emit stateChanged(false, path);
}

void FrameRecorder::append(RawPacketBuffer packet)
{
    if (!m_recording.load(std::memory_order_acquire))
        return;

    quint64 dropped = 0;
    {
        QMutexLocker locker(&m_queueMutex);
        if (!m_recording.load(std::memory_order_relaxed))
            return;
        const qint64 bytes = qMax(0, packet.packetBytes());
        while (!m_queue.isEmpty()
               && (m_queue.size() >= MaximumQueuedPackets
                   || m_queueBytes + bytes > MaximumQueuedBytes)) {
            const RawPacketBuffer oldest = m_queue.dequeue();
            m_queueBytes -= qMax(0, oldest.packetBytes());
            dropped = m_droppedFrames.fetch_add(
                          1, std::memory_order_relaxed) + 1;
        }
        if (bytes > MaximumQueuedBytes
            || m_queue.size() >= MaximumQueuedPackets) {
            dropped = m_droppedFrames.fetch_add(
                          1, std::memory_order_relaxed) + 1;
            return;
        }
        m_queue.enqueue(std::move(packet));
        m_queueBytes += bytes;
    }
    if (dropped > 0 && (dropped % 1000) == 1)
        qWarning() << "FRAME_RECORDER_DROP: total=" << dropped;
}

void FrameRecorder::drainQueue()
{
    if (!m_file.isOpen() || !m_recording.load(std::memory_order_relaxed))
        return;
    writeQueuedPackets(8);
}

bool FrameRecorder::writeQueuedPackets(int budgetMs)
{
    if (!m_file.isOpen())
        return false;

    QElapsedTimer budget;
    budget.start();
    bool wroteAnything = false;
    while (budget.elapsed() < budgetMs) {
        QQueue<RawPacketBuffer> packets;
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_queue.isEmpty())
                break;
            while (!m_queue.isEmpty() && packets.size() < 1024) {
                RawPacketBuffer packet = m_queue.dequeue();
                m_queueBytes -= qMax(0, packet.packetBytes());
                packets.enqueue(std::move(packet));
                if (packets.size() >= 1024)
                    break;
            }
        }
        if (packets.isEmpty())
            break;

        QByteArray buffer;
        QDataStream stream(&buffer, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        const quint64 timestamp = quint64(QDateTime::currentMSecsSinceEpoch());
        for (const RawPacketBuffer &packet : qAsConst(packets)) {
            if (!packet.isValid())
                continue;
            stream << timestamp << qint32(packet.deviceId)
                   << quint32(packet.length);
            if (stream.writeRawData(packet.bytes.data(), packet.length)
                != packet.length) {
                emit error(QStringLiteral("组装原始帧数据失败"));
                break;
            }
        }
        if (buffer.isEmpty())
            continue;
        if (m_file.write(buffer) != buffer.size()) {
            const QString reason = m_file.errorString();
            m_recording.store(false, std::memory_order_release);
            if (m_writeTimer)
                m_writeTimer->stop();
            m_file.close();
            {
                QMutexLocker locker(&m_queueMutex);
                m_queue.clear();
                m_queueBytes = 0;
            }
            emit error(QStringLiteral("写入原始帧失败：%1").arg(reason));
            emit stateChanged(false, fileName());
            return false;
        }
        m_frames += quint64(packets.size());
        wroteAnything = true;
    }

    if (wroteAnything
        && (!m_flushClock.isValid() || m_flushClock.elapsed() >= 1000)) {
        m_file.flush();
        m_flushClock.restart();
    }
    return wroteAnything;
}
