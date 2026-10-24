#pragma once

#include "framedecoderworker.h"
#include <QFile>
#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QMutex>
#include <QQueue>

class QTimer;

struct ScanCacheCKey
{
    int groupId = 0;
    int gate = 0;
    int x = 0;
    int y = 0;
    bool operator==(const ScanCacheCKey &other) const
    {
        return groupId == other.groupId && gate == other.gate
               && x == other.x && y == other.y;
    }
};

inline size_t qHash(const ScanCacheCKey &key, size_t seed = 0) noexcept
{
    seed = qHash(key.groupId, seed);
    seed = qHash(key.gate, seed);
    seed = qHash(key.x, seed);
    return qHash(key.y, seed);
}

class ScanWaveformCache : public QObject
{
    Q_OBJECT
public:
    explicit ScanWaveformCache(const QString &filePath, QObject *parent = nullptr);

public slots:
    void initialize();
    void close();
    void setCachingEnabled(bool enabled);
    void enqueueBatch(const GroupScanFrameBatch &batch, int deviceId);
    void storeBatch(GroupScanFrameBatch batch, int deviceId);
    void requestBWaveform(int groupId, int column, double depth);
    void requestCWaveform(int groupId, int gate, int x, int y);
    void resetAll();
    void resetGroupMode(int groupId, int mode);
    void requestStatistics();
    bool saveSnapshot(const QString &path);
    bool loadSnapshot(const QString &path);

signals:
    void waveformReady(int groupId, QVector<qint16> samples,
                       int amplitudeScale, double rangeStart, double rangeEnd,
                       double cursorRange, QString sourceText);
    void warning(QString text);
    void statisticsChanged(QString filePath, bool enabled, qint64 fileBytes,
                           quint64 recordCount, int bPointCount, int cPointCount,
                           QString groupSummary, QString updatedAt);

private:
    struct RecordMeta {
        qint64 offset = -1;
        int score = -1;
    };
    struct LoadedRecord {
        int groupId = 0;
        int amplitudeScale = 4096;
        double rangeStart = 0.0;
        double rangeEnd = 50.0;
        QVector<qint16> samples;
    };

    static quint64 bKey(int groupId, int column);
    void drainPendingBatches();
    bool readRecord(qint64 offset, LoadedRecord &record);
    bool compactReferencedRecords();
    bool ensureCapacity(qint64 bytesToAppend);
    void disableCaching(const QString &reason);
    void emitStatistics(bool force = false);

    QString m_filePath;
    QFile m_file;
    QHash<quint64, RecordMeta> m_bIndex;
    QHash<ScanCacheCKey, RecordMeta> m_cIndex;
    QHash<int, int> m_bGroupCounts;
    QHash<int, int> m_cGroupCounts;
    QMutex m_pendingMutex;
    QHash<GroupScanFrameKey, GroupScanFrame> m_pendingFrames;
    QHash<GroupScanFrameKey, int> m_pendingScores;
    QQueue<GroupScanFrameKey> m_pendingOrder;
    QHash<GroupScanFrameKey, GroupScanFrame> m_drainFrames;
    QHash<GroupScanFrameKey, int> m_drainScores;
    QQueue<GroupScanFrameKey> m_drainOrder;
    int m_pendingDeviceId = -1;
    int m_drainDeviceId = -1;
    qint64 m_pendingBytes = 0;
    qint64 m_pendingPeakBytes = 0;
    qint64 m_drainBytes = 0;
    quint64 m_pendingEvicted = 0;
    quint64 m_pendingRejected = 0;
    qint64 m_lastDrainDurationUs = 0;
    qint64 m_lastStoreDurationUs = 0;
    qint64 m_lastTimerGapMs = 0;
    QTimer *m_drainTimer = nullptr;
    bool m_disabled = false;
    bool m_cachingEnabled = false;
    quint64 m_recordCount = 0;
    QElapsedTimer m_flushClock;
    QElapsedTimer m_statisticsClock;
    QElapsedTimer m_pendingTelemetryClock;
    QElapsedTimer m_drainTimerClock;
    QElapsedTimer m_storageCheckClock;
    qint64 m_lastStorageAvailableBytes = -1;
    bool m_storageSnapshotValid = false;
    static constexpr qint64 ReservedFreeBytes = qint64(512) * 1024 * 1024;
    static constexpr qint64 PendingByteCapacity = qint64(64) * 1024 * 1024;
    static constexpr int PendingKeyCapacity = 4096;
};
