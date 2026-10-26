#pragma once

#include "rawpacket.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <atomic>

class QTimer;

class FrameRecorder : public QObject
{
    Q_OBJECT
public:
    explicit FrameRecorder(QObject *parent = nullptr);
    bool isRecording() const { return m_recording.load(std::memory_order_relaxed); }
    QString fileName() const;

public slots:
    bool start(const QString &fileName, int pointCount, int beamCount, int groupId);
    void stop();
    // Called from the decoder thread. This only performs a bounded, shared
    // buffer enqueue; all file I/O stays on the recorder thread.
    void append(RawPacketBuffer packet);

signals:
    void stateChanged(bool recording, const QString &path);
    void error(const QString &text);

private:
    void drainQueue();
    bool writeQueuedPackets(int budgetMs);

    QFile m_file;
    QTimer *m_writeTimer = nullptr;
    mutable QMutex m_queueMutex;
    QQueue<RawPacketBuffer> m_queue;
    qint64 m_queueBytes = 0;
    mutable QMutex m_nameMutex;
    QString m_fileName;
    std::atomic_bool m_recording{false};
    std::atomic<quint64> m_droppedFrames{0};
    quint64 m_frames = 0;
    QElapsedTimer m_flushClock;
    static constexpr qint64 MaximumQueuedBytes = qint64(64) * 1024 * 1024;
    static constexpr int MaximumQueuedPackets = 16384;
};

