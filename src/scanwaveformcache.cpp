#include "scanwaveformcache.h"
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr quint32 RecordMagic = 0x4153434E; // ASCN
constexpr quint16 RecordVersion = 1;
constexpr quint32 SnapshotMagicV1 = 0x53434153; // SCAS
constexpr quint32 SnapshotMagicV2 = 0x53434232; // SCB2, streamed payload
constexpr quint32 MaximumIndexEntries = 2000000;
constexpr quint32 MaximumGroupEntries = 4096;
constexpr qint64 CopyChunkBytes = 1024 * 1024;

bool copyBytes(QIODevice &source, QIODevice &target, qint64 byteCount)
{
    QByteArray buffer;
    buffer.resize(int(qMin(CopyChunkBytes, qMax<qint64>(1, byteCount))));
    qint64 remaining = byteCount;
    while (remaining > 0) {
        const qint64 chunk = qMin<qint64>(remaining, buffer.size());
        const qint64 read = source.read(buffer.data(), chunk);
        if (read != chunk || target.write(buffer.constData(), read) != read)
            return false;
        remaining -= read;
    }
    return true;
}

int cachedWaveformScore(const GroupScanFrame &item)
{
    if (item.frame.beams.isEmpty())
        return -1;
    int score = 0;
    for (qint16 sample : item.frame.beams.first())
        score = qMax(score, qAbs(int(sample)));
    return score;
}
}

ScanWaveformCache::ScanWaveformCache(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath), m_file(filePath)
{
}

void ScanWaveformCache::initialize()
{
    close();
    if (!m_drainTimer) {
        m_drainTimer = new QTimer(this);
        m_drainTimer->setTimerType(Qt::CoarseTimer);
        m_drainTimer->setInterval(20);
        connect(m_drainTimer, &QTimer::timeout,
                this, &ScanWaveformCache::drainPendingBatches);
        m_drainTimer->start();
    }
    m_bIndex.clear();
    m_cIndex.clear();
    m_bGroupCounts.clear();
    m_cGroupCounts.clear();
    m_recordCount = 0;
    const QFileInfo info(m_filePath);
    QDir().mkpath(info.absolutePath());
    QFile::remove(m_filePath);
    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::ReadWrite)) {
        disableCaching(QStringLiteral("无法建立成像 A 扫缓存：%1")
                           .arg(m_file.errorString()));
        return;
    }
    m_disabled = false;
    m_flushClock.start();
    m_statisticsClock.start();
    emitStatistics(true);
}

void ScanWaveformCache::close()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

quint64 ScanWaveformCache::bKey(int groupId, int column)
{
    return (quint64(quint32(groupId)) << 32) | quint32(column);
}

bool ScanWaveformCache::ensureCapacity(qint64 bytesToAppend)
{
    if (m_disabled || !m_cachingEnabled)
        return false;
    if (!m_file.isOpen())
        initialize();
    if (!m_file.isOpen())
        return false;
    QStorageInfo storage(QFileInfo(m_filePath).absolutePath());
    if (storage.isValid() && storage.isReady()
        && storage.bytesAvailable() < ReservedFreeBytes + bytesToAppend) {
        disableCaching(QStringLiteral("磁盘剩余空间不足，图像继续采集，但停止保存新的点选波形。"));
        return false;
    }
    return true;
}

void ScanWaveformCache::disableCaching(const QString &reason)
{
    if (!m_disabled)
        emit warning(reason);
    m_disabled = true;
    m_cachingEnabled = false;
    emitStatistics(true);
}

void ScanWaveformCache::setCachingEnabled(bool enabled)
{
    if (!enabled) {
        if (m_file.isOpen())
            m_file.flush();
        m_cachingEnabled = false;
        emitStatistics(true);
        return;
    }

    if (!m_file.isOpen())
        initialize();
    QStorageInfo storage(QFileInfo(m_filePath).absolutePath());
    if (!m_file.isOpen() || (storage.isValid() && storage.isReady()
        && storage.bytesAvailable() <= ReservedFreeBytes)) {
        disableCaching(QStringLiteral("磁盘剩余空间不足，不能启动扫描缓存。"));
        return;
    }
    m_disabled = false;
    m_cachingEnabled = true;
    m_flushClock.restart();
    emitStatistics(true);
}

void ScanWaveformCache::enqueueBatch(const GroupScanFrameBatch &batch,
                                     int deviceId)
{
    if (batch.isEmpty())
        return;
    QMutexLocker locker(&m_pendingMutex);
    if (m_pendingDeviceId != -1 && m_pendingDeviceId != deviceId) {
        m_pendingFrames.clear();
        m_pendingScores.clear();
        m_pendingOrder.clear();
    }
    m_pendingDeviceId = deviceId;

    const auto merge = [this](const QString &key, GroupScanFrame item, int score) {
        if (m_pendingFrames.contains(key)) {
            if (score > m_pendingScores.value(key, -1)) {
                m_pendingFrames.insert(key, std::move(item));
                m_pendingScores.insert(key, score);
            }
            return;
        }
        while (m_pendingFrames.size() >= 4096 && !m_pendingOrder.isEmpty()) {
            const QString oldest = m_pendingOrder.dequeue();
            m_pendingFrames.remove(oldest);
            m_pendingScores.remove(oldest);
        }
        m_pendingOrder.enqueue(key);
        m_pendingFrames.insert(key, std::move(item));
        m_pendingScores.insert(key, score);
    };

    for (const GroupScanFrame &source : batch) {
        if ((source.triggeredModes & 0x2) != 0 && source.bColumn >= 0) {
            GroupScanFrame item = source;
            item.triggeredModes = 0x2;
            item.cGateMask = 0;
            merge(QStringLiteral("B:%1:%2").arg(item.groupId).arg(item.bColumn),
                  std::move(item), cachedWaveformScore(source));
        }
        if ((source.triggeredModes & 0x4) == 0
            || source.frame.measurements.isEmpty())
            continue;
        const BeamMeasurement &measurement = source.frame.measurements.first();
        const int amplitudes[4] = {int(measurement.amplitudeA),
                                   int(measurement.amplitudeB),
                                   int(measurement.amplitudeC),
                                   int(measurement.amplitudeI)};
        for (int gate = 0; gate < 4; ++gate) {
            if ((source.cGateMask & (1 << gate)) == 0)
                continue;
            GroupScanFrame item = source;
            item.triggeredModes = 0x4;
            item.bColumn = -1;
            item.cGateMask = 1 << gate;
            merge(QStringLiteral("C:%1:%2:%3:%4")
                      .arg(item.groupId).arg(gate)
                      .arg(item.cColumn).arg(item.cRow),
                  std::move(item), amplitudes[gate]);
        }
    }
}

void ScanWaveformCache::drainPendingBatches()
{
    GroupScanFrameBatch batch;
    int deviceId = -1;
    {
        QMutexLocker locker(&m_pendingMutex);
        batch.reserve(m_pendingFrames.size());
        while (!m_pendingOrder.isEmpty()) {
            const QString key = m_pendingOrder.dequeue();
            const auto it = m_pendingFrames.find(key);
            if (it == m_pendingFrames.end())
                continue;
            batch.append(std::move(it.value()));
            m_pendingFrames.erase(it);
            m_pendingScores.remove(key);
        }
        deviceId = m_pendingDeviceId;
        m_pendingDeviceId = -1;
    }
    if (!batch.isEmpty())
        storeBatch(std::move(batch), deviceId);
}

void ScanWaveformCache::storeBatch(GroupScanFrameBatch batch, int deviceId)
{
    Q_UNUSED(deviceId)
    if (!m_cachingEnabled || m_disabled)
        return;

    struct PendingRecord {
        qint64 relativeOffset = -1;
        int groupId = 0;
        bool updateB = false;
        quint64 bIndexKey = 0;
        int bScore = -1;
        QVector<QPair<ScanCacheCKey, int>> cUpdates;
    };
    QByteArray writeBuffer;
    QVector<PendingRecord> pendingRecords;
    pendingRecords.reserve(batch.size());

    for (const GroupScanFrame &item : qAsConst(batch)) {
        if (item.frame.beams.isEmpty() || item.frame.beams.first().isEmpty())
            continue;

        const QVector<qint16> &samples = item.frame.beams.first();
        const int waveformScore = cachedWaveformScore(item);

        const quint64 currentBKey = item.bColumn >= 0
            ? bKey(item.groupId, item.bColumn) : 0;
        const bool updateB = (item.triggeredModes & 0x2) != 0
                             && item.bColumn >= 0
                             && (!m_bIndex.contains(currentBKey)
                                 || waveformScore > m_bIndex.value(currentBKey).score);

        QVector<QPair<ScanCacheCKey, int>> cUpdates;
        if ((item.triggeredModes & 0x4) != 0
            && !item.frame.measurements.isEmpty()) {
            const BeamMeasurement &measurement = item.frame.measurements.first();
            const int amplitudes[4] = {int(measurement.amplitudeA),
                                       int(measurement.amplitudeB),
                                       int(measurement.amplitudeC),
                                       int(measurement.amplitudeI)};
            for (int gate = 0; gate < 4; ++gate) {
                if ((item.cGateMask & (1 << gate)) == 0)
                    continue;
                const ScanCacheCKey key{item.groupId, gate,
                                        item.cColumn, item.cRow};
                if (!m_cIndex.contains(key)
                    || amplitudes[gate] > m_cIndex.value(key).score)
                    cUpdates.append(qMakePair(key, amplitudes[gate]));
            }
        }
        if (!updateB && cUpdates.isEmpty())
            continue;

        QByteArray record;
        QDataStream out(&record, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << RecordMagic << RecordVersion
            << qint32(item.groupId) << qint32(item.amplitudeScale)
            << item.rangeStart << item.rangeEnd
            << quint64(item.frame.tail.timestamp) << quint32(samples.size());
        record.append(reinterpret_cast<const char *>(samples.constData()),
                      qsizetype(samples.size()) * qsizetype(sizeof(qint16)));
        if (out.status() != QDataStream::Ok)
            continue;
        PendingRecord pending;
        pending.relativeOffset = writeBuffer.size();
        pending.groupId = item.groupId;
        pending.updateB = updateB;
        pending.bIndexKey = currentBKey;
        pending.bScore = waveformScore;
        pending.cUpdates = std::move(cUpdates);
        pendingRecords.append(std::move(pending));
        writeBuffer.append(record);
    }

    if (!writeBuffer.isEmpty()) {
        if (!ensureCapacity(writeBuffer.size()))
            return;
        const qint64 baseOffset = m_file.size();
        if (!m_file.seek(baseOffset)
            || m_file.write(writeBuffer) != writeBuffer.size()) {
            disableCaching(QStringLiteral("批量写入成像 A 扫缓存失败：%1")
                               .arg(m_file.errorString()));
            return;
        }
        for (const PendingRecord &pending : qAsConst(pendingRecords)) {
            const qint64 offset = baseOffset + pending.relativeOffset;
            if (pending.updateB) {
                if (!m_bIndex.contains(pending.bIndexKey))
                    ++m_bGroupCounts[pending.groupId];
                m_bIndex.insert(pending.bIndexKey,
                                RecordMeta{offset, pending.bScore});
            }
            for (const auto &update : pending.cUpdates) {
                if (!m_cIndex.contains(update.first))
                    ++m_cGroupCounts[pending.groupId];
                m_cIndex.insert(update.first,
                                RecordMeta{offset, update.second});
            }
        }
        m_recordCount += quint64(pendingRecords.size());
    }
    // Flushing every decoder batch can stall the cache thread and grow its
    // queued connection backlog at high PRF. One flush per second preserves
    // crash resilience without turning every batch into a synchronous write.
    if (m_file.isOpen() && (!m_flushClock.isValid() || m_flushClock.elapsed() >= 1000)) {
        m_file.flush();
        m_flushClock.restart();
    }
    emitStatistics();
}

bool ScanWaveformCache::readRecord(qint64 offset, LoadedRecord &record)
{
    if (!m_file.isOpen() || offset < 0 || !m_file.seek(offset))
        return false;
    QDataStream in(&m_file);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    quint16 version = 0;
    qint32 groupId = 0;
    qint32 amplitudeScale = 0;
    quint64 timestamp = 0;
    quint32 sampleCount = 0;
    in >> magic >> version >> groupId >> amplitudeScale
       >> record.rangeStart >> record.rangeEnd >> timestamp >> sampleCount;
    Q_UNUSED(timestamp)
    if (in.status() != QDataStream::Ok || magic != RecordMagic
        || version != RecordVersion || sampleCount == 0
        || sampleCount > 1000000)
        return false;
    record.groupId = groupId;
    record.amplitudeScale = amplitudeScale;
    record.samples.resize(int(sampleCount));
    return m_file.read(reinterpret_cast<char *>(record.samples.data()),
                       qint64(sampleCount) * qint64(sizeof(qint16)))
           == qint64(sampleCount) * qint64(sizeof(qint16));
}

void ScanWaveformCache::requestBWaveform(int groupId, int column, double depth)
{
    const auto it = m_bIndex.constFind(bKey(groupId, column));
    LoadedRecord record;
    if (it == m_bIndex.constEnd() || !readRecord(it->offset, record)) {
        emit warning(QStringLiteral("该 B 扫位置尚无可回看的 A 扫数据。"));
        return;
    }
    emit waveformReady(groupId, std::move(record.samples),
                       record.amplitudeScale, record.rangeStart, record.rangeEnd,
                       depth, QStringLiteral("B 扫第 %1 列").arg(column));
}

void ScanWaveformCache::requestCWaveform(int groupId, int gate, int x, int y)
{
    const ScanCacheCKey key{groupId, gate, x, y};
    const auto it = m_cIndex.constFind(key);
    LoadedRecord record;
    if (it == m_cIndex.constEnd() || !readRecord(it->offset, record)) {
        emit warning(QStringLiteral("该 C 扫网格尚无可回看的 A 扫数据。"));
        return;
    }
    emit waveformReady(groupId, std::move(record.samples),
                       record.amplitudeScale, record.rangeStart, record.rangeEnd,
                       std::numeric_limits<double>::quiet_NaN(),
                       QStringLiteral("C 扫 Gate %1（%2, %3）")
                           .arg(QStringLiteral("ABCI").mid(gate, 1))
                           .arg(x).arg(y));
}

void ScanWaveformCache::resetAll()
{
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingFrames.clear();
        m_pendingScores.clear();
        m_pendingOrder.clear();
        m_pendingDeviceId = -1;
    }
    initialize();
}

void ScanWaveformCache::resetGroupMode(int groupId, int mode)
{
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingFrames.clear();
        m_pendingScores.clear();
        m_pendingOrder.clear();
        m_pendingDeviceId = -1;
    }
    if (mode & 0x2) {
        for (auto it = m_bIndex.begin(); it != m_bIndex.end();) {
            if (int(quint32(it.key() >> 32)) == groupId)
                it = m_bIndex.erase(it);
            else
                ++it;
        }
        m_bGroupCounts.remove(groupId);
    }
    if (mode & 0x4) {
        for (auto it = m_cIndex.begin(); it != m_cIndex.end();) {
            if (it.key().groupId == groupId)
                it = m_cIndex.erase(it);
            else
                ++it;
        }
        m_cGroupCounts.remove(groupId);
    }
    if (!compactReferencedRecords())
        emit warning(QStringLiteral("扫描索引已清空，但缓存文件暂时无法压缩；下次启动会自动重建。"));
    emitStatistics(true);
}

bool ScanWaveformCache::compactReferencedRecords()
{
    if (!m_file.isOpen())
        return false;

    QSet<qint64> referenced;
    for (auto it = m_bIndex.constBegin(); it != m_bIndex.constEnd(); ++it)
        referenced.insert(it->offset);
    for (auto it = m_cIndex.constBegin(); it != m_cIndex.constEnd(); ++it)
        referenced.insert(it->offset);

    if (referenced.isEmpty()) {
        close();
        QFile empty(m_filePath);
        const bool ok = empty.open(QIODevice::WriteOnly | QIODevice::Truncate);
        empty.close();
        m_file.setFileName(m_filePath);
        if (!m_file.open(QIODevice::ReadWrite))
            return false;
        m_recordCount = 0;
        m_disabled = false;
        m_flushClock.restart();
        return ok;
    }

    auto offsets = referenced.values();
    std::sort(offsets.begin(), offsets.end());
    QHash<qint64, qint64> remapped;
    remapped.reserve(offsets.size());

    QSaveFile replacement(m_filePath);
    if (!replacement.open(QIODevice::WriteOnly))
        return false;

    for (qint64 oldOffset : qAsConst(offsets)) {
        LoadedRecord record;
        if (!readRecord(oldOffset, record)) {
            replacement.cancelWriting();
            return false;
        }
        const qint64 newOffset = replacement.pos();
        QDataStream out(&replacement);
        out.setByteOrder(QDataStream::LittleEndian);
        out << RecordMagic << RecordVersion
            << qint32(record.groupId) << qint32(record.amplitudeScale)
            << record.rangeStart << record.rangeEnd << quint64(0)
            << quint32(record.samples.size());
        const qint64 sampleBytes = qint64(record.samples.size()) * qint64(sizeof(qint16));
        if (out.status() != QDataStream::Ok
            || replacement.write(reinterpret_cast<const char *>(record.samples.constData()),
                                 sampleBytes) != sampleBytes) {
            replacement.cancelWriting();
            return false;
        }
        remapped.insert(oldOffset, newOffset);
    }

    close();
    if (!replacement.commit()) {
        m_file.setFileName(m_filePath);
        m_file.open(QIODevice::ReadWrite);
        return false;
    }
    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::ReadWrite))
        return false;

    for (auto it = m_bIndex.begin(); it != m_bIndex.end(); ++it)
        it->offset = remapped.value(it->offset, -1);
    for (auto it = m_cIndex.begin(); it != m_cIndex.end(); ++it)
        it->offset = remapped.value(it->offset, -1);
    m_recordCount = quint64(offsets.size());
    m_disabled = false;
    m_flushClock.restart();
    return true;
}

void ScanWaveformCache::requestStatistics()
{
    emitStatistics(true);
}

void ScanWaveformCache::emitStatistics(bool force)
{
    if (!force && m_statisticsClock.isValid() && m_statisticsClock.elapsed() < 500)
        return;
    if (!m_statisticsClock.isValid())
        m_statisticsClock.start();
    else
        m_statisticsClock.restart();

    QMap<int, QPair<int, int>> groupCounts;
    for (auto it = m_bGroupCounts.constBegin(); it != m_bGroupCounts.constEnd(); ++it)
        groupCounts[it.key()].first = it.value();
    for (auto it = m_cGroupCounts.constBegin(); it != m_cGroupCounts.constEnd(); ++it)
        groupCounts[it.key()].second = it.value();

    QStringList summaries;
    for (auto it = groupCounts.constBegin(); it != groupCounts.constEnd(); ++it) {
        summaries << QStringLiteral("组 %1：B %2 点 / C %3 格")
                         .arg(it.key()).arg(it.value().first).arg(it.value().second);
    }
    if (summaries.isEmpty())
        summaries << QStringLiteral("尚未缓存扫描点");

    emit statisticsChanged(m_filePath,
                           m_cachingEnabled && !m_disabled && m_file.isOpen(),
                           m_file.isOpen() ? m_file.size() : QFileInfo(m_filePath).size(),
                           m_recordCount, m_bIndex.size(), m_cIndex.size(),
                           summaries.join(QStringLiteral("\n")),
                           QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

bool ScanWaveformCache::saveSnapshot(const QString &path)
{
    // Saving is an explicit, infrequent operation. Compact first so replaced
    // maxima and cleared scan regions are not carried into every session file.
    if (!compactReferencedRecords()) {
        emit warning(QStringLiteral("保存前无法压缩扫查缓存"));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit warning(QStringLiteral("无法保存扫查缓存快照：%1").arg(file.errorString()));
        return false;
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out << SnapshotMagicV2;

    out << quint32(m_bIndex.size());
    for (auto it = m_bIndex.constBegin(); it != m_bIndex.constEnd(); ++it)
        out << quint64(it.key()) << qint64(it->offset) << qint32(it->score);

    out << quint32(m_cIndex.size());
    for (auto it = m_cIndex.constBegin(); it != m_cIndex.constEnd(); ++it)
        out << qint32(it.key().groupId) << qint32(it.key().gate)
            << qint32(it.key().x) << qint32(it.key().y)
            << qint64(it->offset) << qint32(it->score);

    out << quint64(m_recordCount);

    out << quint32(m_bGroupCounts.size());
    for (auto it = m_bGroupCounts.constBegin(); it != m_bGroupCounts.constEnd(); ++it)
        out << qint32(it.key()) << qint32(it.value());
    out << quint32(m_cGroupCounts.size());
    for (auto it = m_cGroupCounts.constBegin(); it != m_cGroupCounts.constEnd(); ++it)
        out << qint32(it.key()) << qint32(it.value());

    const qint64 recordBytes = m_file.isOpen() ? m_file.size() : 0;
    out << recordBytes;
    if (out.status() != QDataStream::Ok) {
        file.cancelWriting();
        emit warning(QStringLiteral("写入扫查缓存索引失败"));
        return false;
    }

    qint64 oldPosition = 0;
    bool copied = true;
    if (recordBytes > 0) {
        m_file.flush();
        oldPosition = m_file.pos();
        copied = m_file.seek(0) && copyBytes(m_file, file, recordBytes);
        m_file.seek(oldPosition);
    }
    if (!copied) {
        file.cancelWriting();
        emit warning(QStringLiteral("复制扫查 A 扫缓存数据失败"));
        return false;
    }

    if (!file.commit()) {
        emit warning(QStringLiteral("保存扫查缓存快照失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

bool ScanWaveformCache::loadSnapshot(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit warning(QStringLiteral("无法读取扫查缓存快照：%1").arg(file.errorString()));
        return false;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    in >> magic;
    if (magic != SnapshotMagicV1 && magic != SnapshotMagicV2) {
        emit warning(QStringLiteral("扫查缓存快照格式不识别"));
        return false;
    }

    QHash<quint64, RecordMeta> bIndex;
    QHash<ScanCacheCKey, RecordMeta> cIndex;
    QHash<int, int> bGroupCounts, cGroupCounts;
    quint64 recordCount = 0;

    quint32 n = 0;
    in >> n;
    if (in.status() != QDataStream::Ok || n > MaximumIndexEntries
        || qint64(n) * 20 > file.bytesAvailable()) {
        emit warning(QStringLiteral("扫查缓存 B 扫索引损坏或数量异常"));
        return false;
    }
    bIndex.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) {
        quint64 key = 0;
        qint64 offset = -1;
        qint32 score = -1;
        in >> key >> offset >> score;
        bIndex.insert(key, RecordMeta{offset, score});
    }
    in >> n;
    if (in.status() != QDataStream::Ok || n > MaximumIndexEntries
        || qint64(n) * 32 > file.bytesAvailable()) {
        emit warning(QStringLiteral("扫查缓存 C 扫索引损坏或数量异常"));
        return false;
    }
    cIndex.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) {
        qint32 gid = 0, gate = 0, x = 0, y = 0;
        qint64 offset = -1;
        qint32 score = -1;
        in >> gid >> gate >> x >> y >> offset >> score;
        if (gate < 0 || gate > 3) {
            emit warning(QStringLiteral("扫查缓存 C 扫闸门索引异常"));
            return false;
        }
        cIndex.insert(ScanCacheCKey{gid, gate, x, y}, RecordMeta{offset, score});
    }
    in >> recordCount;

    in >> n;
    if (in.status() != QDataStream::Ok || n > MaximumGroupEntries
        || qint64(n) * 8 > file.bytesAvailable()) {
        emit warning(QStringLiteral("扫查缓存 B 扫分组统计损坏"));
        return false;
    }
    for (quint32 i = 0; i < n; ++i) {
        qint32 k = 0, v = 0;
        in >> k >> v;
        if (v < 0 || v > int(MaximumIndexEntries)) {
            emit warning(QStringLiteral("扫查缓存 B 扫分组统计异常"));
            return false;
        }
        bGroupCounts.insert(int(k), int(v));
    }
    in >> n;
    if (in.status() != QDataStream::Ok || n > MaximumGroupEntries
        || qint64(n) * 8 > file.bytesAvailable()) {
        emit warning(QStringLiteral("扫查缓存 C 扫分组统计损坏"));
        return false;
    }
    for (quint32 i = 0; i < n; ++i) {
        qint32 k = 0, v = 0;
        in >> k >> v;
        if (v < 0 || v > int(MaximumIndexEntries)) {
            emit warning(QStringLiteral("扫查缓存 C 扫分组统计异常"));
            return false;
        }
        cGroupCounts.insert(int(k), int(v));
    }

    qint64 byteCount = 0;
    in >> byteCount;
    if (in.status() != QDataStream::Ok || byteCount < 0
        || byteCount > file.bytesAvailable()) {
        emit warning(QStringLiteral("扫查缓存数据长度异常"));
        return false;
    }

    const auto validOffset = [byteCount](qint64 offset) {
        return offset >= 0 && offset < byteCount;
    };
    for (auto it = bIndex.constBegin(); it != bIndex.constEnd(); ++it) {
        if (!validOffset(it->offset)) {
            emit warning(QStringLiteral("扫查缓存 B 扫索引偏移越界"));
            return false;
        }
    }
    for (auto it = cIndex.constBegin(); it != cIndex.constEnd(); ++it) {
        if (!validOffset(it->offset)) {
            emit warning(QStringLiteral("扫查缓存 C 扫索引偏移越界"));
            return false;
        }
    }

    const qint64 recordStart = file.pos();
    in.setDevice(nullptr);
    QSet<qint64> offsetsToValidate;
    for (auto it = bIndex.constBegin(); it != bIndex.constEnd(); ++it)
        offsetsToValidate.insert(it->offset);
    for (auto it = cIndex.constBegin(); it != cIndex.constEnd(); ++it)
        offsetsToValidate.insert(it->offset);
    auto sortedOffsets = offsetsToValidate.values();
    std::sort(sortedOffsets.begin(), sortedOffsets.end());
    for (qint64 offset : qAsConst(sortedOffsets)) {
        if (!file.seek(recordStart + offset)) {
            emit warning(QStringLiteral("扫查缓存记录位置不可读取"));
            return false;
        }
        QDataStream recordIn(&file);
        recordIn.setByteOrder(QDataStream::LittleEndian);
        quint32 recordMagic = 0, sampleCount = 0;
        quint16 recordVersion = 0;
        qint32 groupId = 0, amplitudeScale = 0;
        double rangeStart = 0.0, rangeEnd = 0.0;
        quint64 timestamp = 0;
        recordIn >> recordMagic >> recordVersion >> groupId >> amplitudeScale
                 >> rangeStart >> rangeEnd >> timestamp >> sampleCount;
        Q_UNUSED(groupId)
        Q_UNUSED(amplitudeScale)
        Q_UNUSED(timestamp)
        const qint64 sampleBytes = qint64(sampleCount) * qint64(sizeof(qint16));
        if (recordIn.status() != QDataStream::Ok || recordMagic != RecordMagic
            || recordVersion != RecordVersion || sampleCount == 0
            || sampleCount > 1000000 || !std::isfinite(rangeStart)
            || !std::isfinite(rangeEnd) || rangeEnd <= rangeStart
            || file.pos() + sampleBytes > recordStart + byteCount) {
            emit warning(QStringLiteral("扫查缓存记录内容损坏"));
            return false;
        }
    }
    if (!file.seek(recordStart)) {
        emit warning(QStringLiteral("无法定位扫查缓存数据"));
        return false;
    }

    // All metadata is validated before touching the active cache. QSaveFile
    // keeps the old cache intact if copying or committing the replacement fails.
    close();
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile replacement(m_filePath);
    bool copied = replacement.open(QIODevice::WriteOnly);
    if (copied && byteCount > 0)
        copied = copyBytes(file, replacement, byteCount);
    if (!copied || !replacement.commit()) {
        replacement.cancelWriting();
        m_file.setFileName(m_filePath);
        m_file.open(QIODevice::ReadWrite);
        emit warning(QStringLiteral("无法重建扫查缓存：%1").arg(replacement.errorString()));
        return false;
    }
    m_file.setFileName(m_filePath);
    if (!m_file.open(QIODevice::ReadWrite)) {
        disableCaching(QStringLiteral("无法打开已恢复的扫查缓存：%1").arg(m_file.errorString()));
        return false;
    }

    m_bIndex = std::move(bIndex);
    m_cIndex = std::move(cIndex);
    m_bGroupCounts = std::move(bGroupCounts);
    m_cGroupCounts = std::move(cGroupCounts);
    Q_UNUSED(recordCount)
    QSet<qint64> restoredRecords;
    for (auto it = m_bIndex.constBegin(); it != m_bIndex.constEnd(); ++it)
        restoredRecords.insert(it->offset);
    for (auto it = m_cIndex.constBegin(); it != m_cIndex.constEnd(); ++it)
        restoredRecords.insert(it->offset);
    m_recordCount = quint64(restoredRecords.size());
    m_disabled = false;
    m_flushClock.restart();
    emitStatistics(true);
    return true;
}
