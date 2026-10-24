#pragma once

#include "rawpacket.h"
#include <QObject>
#include <QElapsedTimer>
#include <QMutex>
#include <QQueue>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <limits>

class DeviceController : public QObject
{
    Q_OBJECT
public:
    explicit DeviceController(QObject *parent = nullptr);
    ~DeviceController() override;

    bool serviceConnected() const { return m_serviceConnected; }
    bool capturing() const { return m_capturing; }
    bool hardwareOnline() const { return m_hardwareOnline; }
    bool captureReady() const { return m_captureReady; }
    bool startPending() const { return m_startRequested && !m_capturing; }
    int pointCount() const { return m_pointCount; }
    int beamCount() const { return m_beamCount; }
    int groupOffsetWords() const { return m_groupOffsetWords; }
    int currentGroup() const { return m_currentGroup; }
    int maxAmplitudeValue() const { return m_maxAmplitudeValue; }
    quint64 totalFrames() const { return m_frames.load(std::memory_order_relaxed); }
    quint64 displayDroppedPackets() const
    {
        return m_displayDropped.load(std::memory_order_relaxed);
    }
    quint64 cScanDroppedPackets() const
    {
        return m_cScanDropped.load(std::memory_order_relaxed);
    }
    quint64 cScanEnqueuedPackets() const
    {
        return m_cScanEnqueued.load(std::memory_order_relaxed);
    }
    quint64 cScanDequeuedPackets() const
    {
        return m_cScanDequeued.load(std::memory_order_relaxed);
    }
    qint64 cScanQueueBytes() const
    {
        return m_cScanQueueBytesSnapshot.load(std::memory_order_relaxed);
    }
    qint64 cScanQueueDepth() const
    {
        return m_cScanQueueDepthSnapshot.load(std::memory_order_relaxed);
    }
    qint64 cScanQueueMaxBytes() const
    {
        return m_cScanQueueMaxBytesObserved.load(std::memory_order_relaxed);
    }
    qint64 cScanMaxAgeMs() const
    {
        return m_cScanMaxAgeMsObserved.load(std::memory_order_relaxed);
    }
    quint64 callbackLockWaitMaxUs() const
    {
        return m_callbackLockWaitMaxUs.load(std::memory_order_relaxed);
    }
    quint32 captureEpoch() const
    {
        return m_captureEpoch.load(std::memory_order_relaxed);
    }
    qint32 cScanDropFirstEncoder() const
    {
        return m_cScanDropFirstEncoder.load(std::memory_order_relaxed);
    }
    qint32 cScanDropLastEncoder() const
    {
        return m_cScanDropLastEncoder.load(std::memory_order_relaxed);
    }
    bool cScanSessionIncomplete() const
    {
        return m_cScanSessionIncomplete.load(std::memory_order_relaxed);
    }
    // Called only by the decoder thread.  Returns the newest packet and
    // discards superseded display packets so latency never grows with PRF.
    bool takeLatestDisplayPacket(RawPacketBuffer &packet);
    // Encoder C-scan has a separate FIFO. Unlike the presentation mailbox it
    // preserves packet order so encoder positions are not skipped by 30 FPS
    // display throttling. These methods are thread-safe.
    void setCScanPacketCollection(bool enabled);
    int takeCScanPackets(QQueue<RawPacketBuffer> &packets);

public slots:
    bool connectDevice(const QString &address = QStringLiteral("127.0.0.1"), int deviceId = 0);
    void disconnectDevice();
    bool start();
    void stop();
    void resetEncoder();
    void refreshGeometry();
    void resumeAfterConfigurationChange(bool resumeCapture = true);
    void resumeAfterPresetChange(bool resumeCapture = true);
    void cycleUsbForColdStart();

signals:
    void statusChanged();
    void message(const QString &text, bool error);
    void frameRateChanged(double fps, quint64 received, quint64 dropped);

private slots:
    void watchdog();
    void finishConnectionSettling();
    void finishConfigurationSettling();
    void tryStart();
    void confirmFrameArrival();
    void continueUsbRecovery();
    void initializeHotpluggedBootLoader();

private:
    static void packetCallback(const char *data, int length, int deviceId);
    void setHardwareOnline(bool online);
    void scheduleUsbRecovery(const QString &reason);
    bool ensureSafeFrameRate();
    bool synchronizeConfiguredGroups();
    QVector<int> allKnownGroups() const;
    void recordCScanDrop(const RawPacketBuffer &packet);
    void resetScanStatistics();

    static std::atomic<DeviceController *> s_instance;
    bool m_serviceConnected = false;
    // Client is a process-wide singleton. Disconnect() only tears down the
    // logical server connection; the vendor USB framework itself cannot be
    // constructed again in the same process. Keep a healthy SDK session alive
    // across the UI's disconnect/reconnect cycle and release it on exit.
    bool m_sdkSessionEstablished = false;
    bool m_capturing = false;
    bool m_hardwareOnline = false;
    bool m_captureReady = false;
    bool m_startRequested = false;
    bool m_usbNeedsRecovery = false;
    bool m_usbRecoveryInProgress = false;
    bool m_expectedUsbCycle = false;
    bool m_coldStartCycleAttempted = false;
    bool m_firmwareBootstrapInProgress = false;
    bool m_groupsSynchronized = false;
    int m_usbRecoveryStep = 0;
    int m_usbRecoveryAttempts = 0;
    int m_lastUsbMode = -1;
    int m_startAttempts = 0;
    int m_deviceId = 0;
    int m_pointCount = 1024;
    int m_beamCount = 1;
    int m_groupOffsetWords = 0;
    int m_currentGroup = 1;
    int m_maxAmplitudeValue = 4096;
    // The USB SDK delivers high-PRF frames in large bursts rather than at
    // uniform intervals.  Keep roughly one second of presentation frames so
    // the 30 FPS decoder can play a burst smoothly instead of collapsing an
    // entire SDK burst into a single visible frame.
    QMutex m_displayMutex;
    QQueue<RawPacketBuffer> m_displayPackets;
    QMutex m_cScanMutex;
    QQueue<RawPacketBuffer> m_cScanPackets;
    static constexpr int DisplayPacketCapacity = 36;
    static constexpr int DisplayLatencyTarget = 30;
    // The C-scan ingress is limited by bytes instead of packet count. 32 MiB
    // is about two seconds at the documented 8 kHz / 2 KiB packet rate; the
    // exact runtime margin is reported by the telemetry counters below.
    static constexpr qint64 CScanQueueByteCapacity = qint64(32) * 1024 * 1024;
    static constexpr qint64 CScanQueueMaxAgeMs = 2000;
    std::atomic_bool m_cScanPacketCollection{false};
    qint64 m_cScanQueueBytes = 0;
    std::atomic<qint64> m_cScanQueueBytesSnapshot{0};
    std::atomic<qint64> m_cScanQueueDepthSnapshot{0};
    std::atomic<qint64> m_cScanQueueMaxBytesObserved{0};
    std::atomic<qint64> m_cScanMaxAgeMsObserved{0};
    std::atomic<quint64> m_frames{0};
    std::atomic<quint64> m_rxBytes{0};
    std::atomic<quint64> m_rxSequence{0};
    std::atomic<quint32> m_captureEpoch{0};
    quint64 m_framesAtStart = 0;
    quint64 m_watchdogObservedFrames = 0;
    quint64 m_lastReportedFrames = 0;
    std::atomic<quint64> m_displayDropped{0};
    std::atomic<quint64> m_cScanDropped{0};
    std::atomic<quint64> m_cScanEnqueued{0};
    std::atomic<quint64> m_cScanDequeued{0};
    std::atomic_bool m_cScanSessionIncomplete{false};
    std::atomic_bool m_cScanDropRangeAvailable{false};
    std::atomic<qint32> m_cScanDropFirstEncoder{
        std::numeric_limits<qint32>::min()};
    std::atomic<qint32> m_cScanDropLastEncoder{
        std::numeric_limits<qint32>::min()};
    std::atomic<quint64> m_callbackLockWaitTotalUs{0};
    std::atomic<quint64> m_callbackLockWaitCount{0};
    std::atomic<quint64> m_callbackLockWaitMaxUs{0};
    QElapsedTimer m_lastFrameClock;
    QElapsedTimer m_rateClock;
    QTimer m_watchdog;
    QTimer m_connectionSettling;
    QTimer m_configurationSettling;
    QTimer m_startRetry;
    QTimer m_frameConfirmation;
    QTimer m_usbRecovery;
};
