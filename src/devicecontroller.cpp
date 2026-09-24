#include "devicecontroller.h"

#include "client.h"
#include "usbbootstrap.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <exception>

std::atomic<DeviceController *> DeviceController::s_instance{nullptr};

DeviceController::DeviceController(QObject *parent) : QObject(parent)
{
    s_instance.store(this, std::memory_order_release);
    Client::getInstance().setDataPacketCallback(&DeviceController::packetCallback);
    m_watchdog.setInterval(500);
    connect(&m_watchdog, &QTimer::timeout, this, &DeviceController::watchdog);
    m_watchdog.start();
    m_connectionSettling.setSingleShot(true);
    m_connectionSettling.setInterval(3000);
    connect(&m_connectionSettling, &QTimer::timeout,
            this, &DeviceController::finishConnectionSettling);
    // 参数写入会触发 SDK/USB 端重新映射和下发。不要由 MainWindow 用一个裸 singleShot
    // 直接重启采集，而是在控制器内部显式进入“配置稳定期”，避免启动命令撞上设备更新。
    m_configurationSettling.setSingleShot(true);
    // 应用参数后模拟一次明确的“停止 -> 开始”操作。停止命令发出后留 800 ms
    // 给硬件/SDK 清理旧采集状态，再自动启动。
    m_configurationSettling.setInterval(800);
    connect(&m_configurationSettling, &QTimer::timeout,
            this, &DeviceController::finishConfigurationSettling);
    m_startRetry.setSingleShot(true);
    m_startRetry.setInterval(1500);
    connect(&m_startRetry, &QTimer::timeout, this, &DeviceController::tryStart);
    m_frameConfirmation.setSingleShot(true);
    m_frameConfirmation.setInterval(3000);
    connect(&m_frameConfirmation, &QTimer::timeout,
            this, &DeviceController::confirmFrameArrival);
    m_usbRecovery.setSingleShot(true);
    connect(&m_usbRecovery, &QTimer::timeout,
            this, &DeviceController::continueUsbRecovery);
    m_lastUsbMode = int(UsbBootstrap::detectDeviceMode());
    m_rateClock.start();
}

DeviceController::~DeviceController()
{
    m_watchdog.stop();
    m_connectionSettling.stop();
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    m_usbRecovery.stop();
    // Block callbacks before releasing the vendor singleton.  Always issue a
    // hardware Stop when a process-wide SDK session existed, even if the user
    // had previously pressed the UI's "断开 SDK" button.
    s_instance.store(nullptr, std::memory_order_release);
    auto &sdk = Client::getInstance();
    sdk.removeDataPacketCallback();
    if (m_sdkSessionEstablished) {
        try {
            sdk.startCapture(false);
        } catch (...) {
        }
        // Give cyusb3 a short, bounded interval to retire pending transfers
        // before destroying the SDK connection.  This avoids carrying stale
        // endpoint requests into the next application process.
        QThread::msleep(150);
        sdk.Disconnect();
    }
}

void DeviceController::packetCallback(const char *data, int length, int deviceId)
{
    if (auto *self = s_instance.load(std::memory_order_acquire)) {
        if (data && length > 0 && deviceId == self->m_deviceId) {
            // The USB firmware does not guarantee a monotonically increasing
            // frameNo field in free-running mode, so it must not be used as a
            // packet-loss counter.  Count only actual local queue evictions.
            // The vendor transfers ownership of a new[] buffer.  Adopt it
            // directly instead of copying a potentially large packet in the
            // high-PRF callback.  QSharedPointer also makes queued delivery
            // and shutdown safe without custom lifetime handshakes.
            RawPacketBuffer packet;
            packet.bytes = QSharedPointer<char>(
                const_cast<char *>(data), [](char *buffer) { delete[] buffer; });
            packet.length = length;
            packet.deviceId = deviceId;
            self->m_frames.fetch_add(1, std::memory_order_relaxed);
            {
                // The critical section contains only O(1), implicitly-shared
                // QByteArray moves.  SDK producer threads and the GUI consumer
                // therefore never wait on a full packet memcpy.
                QMutexLocker locker(&self->m_frameMailboxMutex);
                if (self->m_cScanPacketCollection.load(std::memory_order_relaxed)) {
                    if (self->m_cScanPackets.size() >= CScanPacketCapacity)
                        self->m_cScanPackets.dequeue();
                    // RawPacketBuffer owns a QSharedPointer, so the second FIFO
                    // shares the SDK allocation without copying the packet.
                    self->m_cScanPackets.enqueue(packet);
                }
                if (self->m_displayPackets.size() >= DisplayPacketCapacity) {
                    // Drop the oldest queued display frame, not the newly arrived
                    // frame.  The queue remains low-latency while still retaining
                    // enough frames to bridge normal USB delivery gaps.
                    self->m_displayPackets.dequeue();
                    self->m_dropped.fetch_add(1, std::memory_order_relaxed);
                }
                self->m_displayPackets.enqueue(std::move(packet));
            }
            return;
        }
    }

    // Invalid/unroutable callbacks still own the vendor new[] allocation.
    delete[] data;
}

bool DeviceController::takeLatestDisplayPacket(RawPacketBuffer &packet)
{
    QMutexLocker locker(&m_frameMailboxMutex);
    if (m_displayPackets.isEmpty())
        return false;
    // Keep enough frames to bridge the SDK's burst-to-burst silence.  When
    // continuously overloaded, trim only the excess above the latency target;
    // FIFO consumption then remains smooth without an ever-growing delay.
    while (m_displayPackets.size() > DisplayLatencyTarget) {
        m_displayPackets.dequeue();
        m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    packet = m_displayPackets.dequeue();
    return packet.isValid();
}

void DeviceController::setCScanPacketCollection(bool enabled)
{
    const bool previous = m_cScanPacketCollection.exchange(
        enabled, std::memory_order_acq_rel);
    if (previous == enabled)
        return;
    QMutexLocker locker(&m_frameMailboxMutex);
    m_cScanPackets.clear();
}

int DeviceController::takeCScanPackets(QVector<RawPacketBuffer> &packets,
                                       int maximumPackets)
{
    packets.clear();
    maximumPackets = qMax(1, maximumPackets);
    QMutexLocker locker(&m_frameMailboxMutex);
    const int count = qMin(maximumPackets, m_cScanPackets.size());
    packets.reserve(count);
    for (int i = 0; i < count; ++i)
        packets.append(m_cScanPackets.dequeue());
    return count;
}

bool DeviceController::connectDevice(const QString &address, int deviceId)
{
    auto &sdk = Client::getInstance();
    m_deviceId = deviceId;
    m_groupsSynchronized = false;
    if (m_sdkSessionEstablished) {
        // The vendor Client owns a process-lifetime USB framework. Calling
        // Disconnect() and Connect() again does not rebuild its physical-device
        // map and can leave physical_id at -1. Reuse the known-good session.
        m_serviceConnected = true;
        m_captureReady = false;
        m_startAttempts = 0;
        refreshGeometry();
        m_connectionSettling.setInterval(500);
        m_connectionSettling.start();
        emit message(QStringLiteral("SDK 底层会话仍然就绪，正在恢复连接…"), false);
        emit statusChanged();
        if (m_usbNeedsRecovery)
            scheduleUsbRecovery(QStringLiteral("检测到 USB 曾经重新枚举"));
        return true;
    }

    bool found = false;
    for (const auto &server : sdk.getAllServers()) {
        if (server.serverId == deviceId) {
            found = true;
            sdk.setServerAddr(address.toStdString(), deviceId);
            break;
        }
    }
    if (!found)
        sdk.addServer(address.toStdString(), deviceId);
    sdk.setCurrentServerId(deviceId);
    m_serviceConnected = sdk.Connect();
    if (m_serviceConnected) {
        m_sdkSessionEstablished = true;
        sdk.startCapture(false);
        refreshGeometry();
        m_captureReady = false;
        m_startAttempts = 0;
        m_connectionSettling.setInterval(3000);
        m_connectionSettling.start();
        emit message(QStringLiteral("SDK 已连接，正在建立真实 USB 板卡连接；现在点击开始会自动排队"), false);
        if (m_usbNeedsRecovery)
            scheduleUsbRecovery(QStringLiteral("SDK 启动后 USB 已自动重新枚举"));
    } else {
        emit message(QStringLiteral("SDK 服务连接失败"), true);
    }
    emit statusChanged();
    return m_serviceConnected;
}

void DeviceController::cycleUsbForColdStart()
{
    if (!m_serviceConnected || !m_sdkSessionEstablished)
        return;
    if (UsbBootstrap::detectDeviceMode() != UsbBootstrap::DeviceMode::Streamer) {
        emit message(QStringLiteral("系统尚未检测到 USB Streamer，保持软件打开并重新插入 USB 后会自动恢复"), true);
        return;
    }

    QString error;
    m_expectedUsbCycle = true;
    emit message(QStringLiteral("SDK 已就绪，正在自动重新枚举 USB 板卡（不是实际拔线）…"), false);
    if (!UsbBootstrap::cycleStreamerPort(&error)) {
        m_expectedUsbCycle = false;
        emit message(error, true);
        return;
    }
    // The watchdog observes Missing -> Streamer and owns the remaining
    // BoardInfo/configuration/start sequence.
    m_usbNeedsRecovery = true;
    // The 500 ms watchdog can miss a very short remove/arrival transition.
    // Start a fallback recovery timer now; a visible transition will cancel
    // and reschedule it from the real Streamer-arrival edge.
    scheduleUsbRecovery(QStringLiteral("USB 自动重新枚举已触发"));
}

void DeviceController::disconnectDevice()
{
    auto &sdk = Client::getInstance();
    m_connectionSettling.stop();
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    m_usbRecovery.stop();
    m_usbRecoveryInProgress = false;
    m_usbRecoveryStep = 0;
    m_startRequested = false;
    m_captureReady = false;
    if (m_sdkSessionEstablished) {
        try {
            sdk.startCapture(false);
        } catch (...) {
            // The local/UI state must still be cleared. A final Disconnect()
            // is issued from the destructor when the application exits.
        }
    }
    m_capturing = false;
    setHardwareOnline(false);
    // Deliberately do not call Client::Disconnect() here. The SDK documents no
    // way to recreate its USB framework and its own log reports that hot-plug
    // is not implemented. Keeping this session makes a UI reconnect immediate.
    m_serviceConnected = false;
    emit message(QStringLiteral("已停止采集并断开界面连接；USB 底层保持就绪，可立即重新连接。退出软件后才会完全释放设备。"), false);
    emit statusChanged();
}

bool DeviceController::start()
{
    if (!m_serviceConnected && !connectDevice())
        return false;
    if (!m_startRequested) m_startAttempts = 0;
    m_startRequested = true;
    if (!m_captureReady) {
        const auto usbMode = UsbBootstrap::detectDeviceMode();
        if (usbMode != UsbBootstrap::DeviceMode::Streamer) {
            emit message(usbMode == UsbBootstrap::DeviceMode::BootLoader
                             ? QStringLiteral("开始采集已排队，正在等待自动加载设备固件")
                             : QStringLiteral("开始采集已排队；请现在插入 PA-1664 USB"), false);
            emit statusChanged();
            return true;
        }
        const bool initializationTimerActive = m_connectionSettling.isActive()
                                               || m_configurationSettling.isActive()
                                               || m_usbRecovery.isActive()
                                               || m_usbRecoveryInProgress;
        if (!initializationTimerActive) {
            // A previous automatic recovery may have been cancelled/failed.
            // A deliberate new Start click begins one fresh bounded cycle.
            m_usbRecoveryAttempts = 0;
            scheduleUsbRecovery(QStringLiteral("用户重新请求采集，硬件尚未就绪"));
            return true;
        }
        emit message(QStringLiteral("开始采集已排队，SDK 初始化完成后将自动启动"), false);
        emit statusChanged();
        return true;
    }
    tryStart();
    return m_capturing || m_startRequested;
}

void DeviceController::finishConnectionSettling()
{
    if (!m_serviceConnected || m_usbRecoveryInProgress)
        return;
    if (!m_groupsSynchronized) {
        if (!synchronizeConfiguredGroups()) {
            m_captureReady = false;
            m_startRequested = false;
            emit statusChanged();
            return;
        }
        m_groupsSynchronized = true;
        refreshGeometry();
        // loadConfig/removeGroup/pushConfigToDevice are queued by the vendor
        // SDK. Do not start acquisition while those commands are still
        // replacing the board's former multi-group table.
        m_captureReady = false;
        m_configurationSettling.setInterval(2200);
        m_configurationSettling.start();
        emit message(QStringLiteral("工作组已按配置重新同步到硬件，等待板卡完成更新…"), false);
        emit statusChanged();
        return;
    }
    m_captureReady = true;
    emit message(m_startRequested ? QStringLiteral("SDK 初始化完成，正在自动启动采集…")
                                  : QStringLiteral("SDK 初始化完成，可以开始采集"), false);
    emit statusChanged();
    if (m_startRequested)
        tryStart();
}

void DeviceController::resumeAfterConfigurationChange()
{
    if (!m_serviceConnected)
        return;

    // 参数写完以后，再明确执行一次 SDK 停止采集。
    // 这不是只修改本地 m_capturing 状态，而是真正调用 startCapture(false)，
    // 等价于用户再点一次“停止采集”。随后由 m_configurationSettling 定时器
    // 自动调用 tryStart()，等价于再点一次“开始采集”。
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    m_startRequested = false;
    try {
        Client::getInstance().startCapture(false);
    } catch (const std::exception &e) {
        emit message(QStringLiteral("参数应用后自动停止采集时 SDK 异常：%1")
                         .arg(QString::fromLocal8Bit(e.what())), true);
    } catch (...) {
        emit message(QStringLiteral("参数应用后自动停止采集时 SDK 抛出未知异常"), true);
    }

    m_capturing = false;
    setHardwareOnline(false);
    m_startAttempts = 0;
    m_startRequested = true;
    m_captureReady = false;
    m_configurationSettling.setInterval(800);
    m_configurationSettling.start();
    emit message(QStringLiteral("参数已应用：已自动停止采集，0.8 秒后自动开始采集…"), false);
    emit statusChanged();
}

void DeviceController::finishConfigurationSettling()
{
    if (!m_serviceConnected)
        return;
    m_captureReady = true;
    emit message(m_startRequested ? QStringLiteral("自动停止完成，正在自动开始采集…")
                                  : QStringLiteral("自动停止完成，可以开始采集"), false);
    emit statusChanged();
    if (m_startRequested)
        tryStart();
}

void DeviceController::tryStart()
{
    if (!m_serviceConnected || !m_captureReady || !m_startRequested || m_capturing)
        return;
    refreshGeometry();
    // The board limits aggregate pulse rate, not merely the value shown in the
    // PRF editor.  Starting above that limit can work for a short time and then
    // make the USB stream stop completely.  Correct an unsafe persisted/config
    // value before every start as a final line of defence.
    if (!ensureSafeFrameRate())
        return;
    {
        QMutexLocker locker(&m_frameMailboxMutex);
        m_displayPackets.clear();
    }
    m_dropped.store(0, std::memory_order_relaxed);
    m_lastFrameClock.invalidate();
    ++m_startAttempts;
    try {
        m_capturing = Client::getInstance().startCapture(true);
    } catch (const std::exception &e) {
        m_capturing = false;
        emit message(QStringLiteral("启动采集时 SDK 异常：%1").arg(QString::fromLocal8Bit(e.what())), true);
    } catch (...) {
        m_capturing = false;
        emit message(QStringLiteral("启动采集时 SDK 抛出未知异常"), true);
    }
    if (m_capturing) {
        m_startRequested = false;
        m_framesAtStart = m_frames.load(std::memory_order_relaxed);
        m_frameConfirmation.start();
        emit message(QStringLiteral("采集命令已发送，正在确认真实设备帧…"), false);
    } else if (m_startAttempts < 3) {
        emit message(QStringLiteral("设备仍在初始化，1.5 秒后自动重试（第 %1 次未成功）").arg(m_startAttempts), false);
        m_startRetry.start();
    } else {
        // A local Connect() can succeed while the SDK still has physical id
        // -1. Re-synchronizing the full configuration is the operation that
        // transitions the board from its post-enumeration Started state to an
        // Updated/startable state.
        scheduleUsbRecovery(QStringLiteral("连续启动未收到硬件响应"));
    }
    emit statusChanged();
}

void DeviceController::confirmFrameArrival()
{
    if (!m_serviceConnected || !m_capturing
        || m_frames.load(std::memory_order_relaxed) > m_framesAtStart
        || m_hardwareOnline)
        return;
    m_capturing = false;
    try {
        Client::getInstance().startCapture(false);
    } catch (...) {
        // 后续重试仍可重新建立采集状态，停止异常不应终止程序。
    }
    if (m_startAttempts < 3) {
        m_startRequested = true;
        emit message(QStringLiteral("启动命令未产生数据帧，1.5 秒后自动重新启动（第 %1 次）")
                         .arg(m_startAttempts), false);
        m_startRetry.start();
    } else {
        scheduleUsbRecovery(QStringLiteral("启动命令未产生真实硬件帧"));
    }
    emit statusChanged();
}

void DeviceController::stop()
{
    m_startRequested = false;
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    m_usbRecovery.stop();
    m_usbRecoveryInProgress = false;
    m_usbRecoveryStep = 0;
    {
        QMutexLocker locker(&m_frameMailboxMutex);
        m_displayPackets.clear();
    }
    if (m_serviceConnected) {
        try {
            Client::getInstance().startCapture(false);
        } catch (...) {
            emit message(QStringLiteral("停止采集时 SDK 异常，已继续清理本地状态"), true);
        }
    }
    m_capturing = false;
    setHardwareOnline(false);
    emit statusChanged();
}

void DeviceController::resetEncoder()
{
    if (!Client::getInstance().resetCapture())
        emit message(QStringLiteral("编码器复位失败"), true);
    else
        emit message(QStringLiteral("编码器已复位"), false);
}

void DeviceController::refreshGeometry()
{
    auto &sdk = Client::getInstance();
    m_currentGroup = sdk.getCurrentGroup();
    m_pointCount = qMax(1, sdk.getPointQuantity(m_currentGroup));
    m_beamCount = qMax(1, sdk.getBeamCounts(m_currentGroup));
    m_maxAmplitudeValue = int(sdk.getMaxAmplitude(m_currentGroup));
    m_groupOffsetWords = 0;
    const auto groups = sdk.getGroupsNo(true);
    for (int group : groups) {
        if (group == m_currentGroup)
            break;
        // 与官方 SDK 一致：点数/波束数无效的组跳过，不计入偏移。
        // 原用 qMax(1, …) 会把已删除/异常组也计入 17 字，导致偏移错位、A 扫无信号。
        const int points = sdk.getPointQuantity(group);
        const int beams = sdk.getBeamCounts(group);
        if (points > 0 && beams > 0)
            m_groupOffsetWords += beams * (points + 16);
    }
    emit statusChanged();
}

QVector<int> DeviceController::allKnownGroups() const
{
    auto &sdk = Client::getInstance();
    QSet<int> unique;
    // true is the enabled list used by the official UI. false may also
    // contain disabled/stale groups. The USB mapper has been observed to map
    // those stale groups, so both lists matter for hardware load and cleanup.
    for (int group : sdk.getGroupsNo(true))
        unique.insert(group);
    for (int group : sdk.getGroupsNo(false))
        unique.insert(group);
    QVector<int> result = unique.values().toVector();
    std::sort(result.begin(), result.end());
    return result;
}

bool DeviceController::synchronizeConfiguredGroups()
{
    const QString path = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("default_config.json"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message(QStringLiteral("无法读取工作组配置：%1").arg(path), true);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit message(QStringLiteral("工作组配置 JSON 无效：%1").arg(parseError.errorString()), true);
        return false;
    }

    QSet<int> desired;
    const QJsonObject groupsObject = document.object().value(QStringLiteral("groups")).toObject();
    for (auto it = groupsObject.begin(); it != groupsObject.end(); ++it) {
        bool ok = false;
        const int group = it.key().toInt(&ok);
        if (ok)
            desired.insert(group);
    }
    if (desired.isEmpty()) {
        emit message(QStringLiteral("配置文件没有有效工作组，已阻止向硬件下发空配置"), true);
        return false;
    }

    auto &sdk = Client::getInstance();
    try {
        sdk.startCapture(false);

        // Connect() can merge the board's old groups into the client even
        // when default_config.json has only one group.
        const QVector<int> staleCandidates = allKnownGroups();
        const int configuredCurrent = document.object()
                                          .value(QStringLiteral("currentGroup")).toInt(-1);
        const int desiredCurrent = desired.contains(configuredCurrent)
                                       ? configuredCurrent : *desired.constBegin();

        int removed = 0;
        // Delete stale groups while they still exist in the post-Connect SDK
        // state.  Once loadConfig() replaces the local table, querying those
        // half-deleted entries can throw "Key not found in group".
        if (staleCandidates.contains(desiredCurrent))
            sdk.setCurrentGroup(desiredCurrent);
        for (int group : staleCandidates) {
            if (desired.contains(group))
                continue;
            try {
                if (sdk.removeGroup(group))
                    ++removed;
            } catch (...) {
                // A disabled group may be only a stale identifier with no
                // parameter map. Reloading and pushing the exact JSON below
                // still removes it from the next board configuration.
            }
        }

        // Reload after Connect, not only during framework construction. This
        // replaces any configuration merged back from the board and restores
        // all desired parameters after removeGroup().
        if (!sdk.loadConfig(path.toStdString())) {
            emit message(QStringLiteral("连接后重新加载工作组配置失败"), true);
            return false;
        }
        sdk.setCurrentGroup(desiredCurrent);
        sdk.pushConfigToDevice();

        const QVector<int> after = sdk.getGroupsNo(true);
        QStringList groupNames;
        for (int group : after)
            groupNames << QString::number(group);
        emit message(QStringLiteral("硬件配置同步：目标 %1 组，清理 %2 个残留组；当前 SDK 组 [%3]")
                         .arg(desired.size()).arg(removed).arg(groupNames.join(',')), false);
        return true;
    } catch (const std::exception &e) {
        emit message(QStringLiteral("同步硬件工作组时 SDK 异常：%1")
                         .arg(QString::fromLocal8Bit(e.what())), true);
    } catch (...) {
        emit message(QStringLiteral("同步硬件工作组时 SDK 抛出未知异常"), true);
    }
    return false;
}

bool DeviceController::ensureSafeFrameRate()
{
    constexpr qint64 MaxEffectivePrf = 12848;
    auto &sdk = Client::getInstance();
    try {
        qint64 totalBeams = 0;
        // Only enabled groups have a complete parameter map. getGroupsNo(false)
        // can return tombstones left by the vendor SDK; calling getBeamCounts
        // on one of those entries throws "Key not found in group".
        const auto groups = sdk.getGroupsNo(true);
        for (int group : groups) {
            try {
                const int beams = sdk.getBeamCounts(group);
                if (beams > 0)
                    totalBeams += beams;
            } catch (...) {
                // Ignore incomplete SDK tombstones. The configured active
                // group is validated separately by refreshGeometry().
            }
        }
        totalBeams = qMax<qint64>(1, totalBeams);

        const int frameRate = qMax(1, sdk.getFrameRate());
        const qint64 effectivePrf = qint64(frameRate) * totalBeams;
        if (effectivePrf <= MaxEffectivePrf)
            return true;

        const int safeFrameRate = qMax(1, int(MaxEffectivePrf / totalBeams));
        if (!sdk.setFrameRate(safeFrameRate)) {
            m_startRequested = false;
            emit message(QStringLiteral("不能启动：有效 PRF %1（%2 Hz × %3 beam）超过硬件上限 %4，"
                                        "且 SDK 未接受安全值 %5 Hz")
                             .arg(effectivePrf).arg(frameRate).arg(totalBeams)
                             .arg(MaxEffectivePrf).arg(safeFrameRate), true);
            emit statusChanged();
            return false;
        }

        // setFrameRate causes a hardware configuration update.  Give it the
        // same settling interval used by an ordinary parameter application,
        // then tryStart() will be called again automatically.
        m_captureReady = false;
        m_configurationSettling.start();
        emit message(QStringLiteral("已保护设备：%1 Hz × %2 beam = %3 超过有效 PRF 上限 %4；"
                                    "已自动限制为 %5 Hz，配置稳定后自动开始采集")
                         .arg(frameRate).arg(totalBeams).arg(effectivePrf)
                         .arg(MaxEffectivePrf).arg(safeFrameRate), false);
        emit statusChanged();
        return false;
    } catch (const std::exception &e) {
        m_startRequested = false;
        emit message(QStringLiteral("启动前校验 PRF 时 SDK 异常：%1")
                         .arg(QString::fromLocal8Bit(e.what())), true);
    } catch (...) {
        m_startRequested = false;
        emit message(QStringLiteral("启动前校验 PRF 时 SDK 抛出未知异常"), true);
    }
    emit statusChanged();
    return false;
}

void DeviceController::watchdog()
{
    if (m_firmwareBootstrapInProgress)
        return;
    const int usbMode = int(UsbBootstrap::detectDeviceMode());
    if (usbMode != m_lastUsbMode) {
        const int previousMode = m_lastUsbMode;
        m_lastUsbMode = usbMode;
        if (usbMode != int(UsbBootstrap::DeviceMode::Streamer)) {
            // Do not issue SDK commands while Windows is removing or
            // re-enumerating the endpoints.
            m_usbNeedsRecovery = true;
            m_usbRecovery.stop();
            m_usbRecoveryInProgress = false;
            m_usbRecoveryStep = 0;
            m_usbRecoveryAttempts = 0;
            m_startRetry.stop();
            m_frameConfirmation.stop();
            m_capturing = false;
            setHardwareOnline(false);
            if (m_expectedUsbCycle) {
                emit message(QStringLiteral("USB 正在受控复位和重新枚举，请保持线缆连接…"), false);
            } else if (usbMode == int(UsbBootstrap::DeviceMode::BootLoader)) {
                emit message(QStringLiteral("已检测到 USB BootLoader，正在自动加载固件…"), false);
                QTimer::singleShot(0, this, &DeviceController::initializeHotpluggedBootLoader);
            } else {
                emit message(QStringLiteral("系统未检测到 USB 设备，等待重新插入"), true);
            }
            emit statusChanged();
        } else if (previousMode != int(UsbBootstrap::DeviceMode::Streamer)) {
            const bool wasExpectedCycle = m_expectedUsbCycle;
            m_expectedUsbCycle = false;
            m_usbNeedsRecovery = true;
            if (m_serviceConnected)
                scheduleUsbRecovery(wasExpectedCycle
                                        ? QStringLiteral("USB 自动重新枚举完成")
                                        : QStringLiteral("USB 已重新插入并枚举为 Streamer"));
        }
    }

    const quint64 observedFrames = m_frames.load(std::memory_order_relaxed);
    if (observedFrames != m_watchdogObservedFrames) {
        m_watchdogObservedFrames = observedFrames;
        // A final USB transfer may complete just after Stop.  Only treat frame
        // activity as capture confirmation while a capture is active/pending.
        if (m_serviceConnected && (m_capturing || m_startRequested)) {
            m_lastFrameClock.restart();
            m_capturing = true;
            m_startRequested = false;
            m_startRetry.stop();
            m_frameConfirmation.stop();
            m_usbRecovery.stop();
            m_usbRecoveryInProgress = false;
            m_usbRecoveryStep = 0;
            m_usbNeedsRecovery = false;
            m_usbRecoveryAttempts = 0;
            setHardwareOnline(true);
        }
    }

    if (m_capturing && m_lastFrameClock.isValid() && m_lastFrameClock.elapsed() > 2500) {
        setHardwareOnline(false);
        // A mid-stream firmware/USB stall used to leave m_capturing=true, so
        // the Start button stayed disabled forever.  Retire that capture and
        // feed it back through the normal guarded start path (which also checks
        // the aggregate PRF limit).
        m_frameConfirmation.stop();
        try {
            Client::getInstance().startCapture(false);
        } catch (...) {
            // A fresh start below is still worth attempting.
        }
        m_capturing = false;
        m_startRequested = true;
        m_startAttempts = 0;
        {
            QMutexLocker locker(&m_frameMailboxMutex);
            m_displayPackets.clear();
        }
        emit message(QStringLiteral("数据流中断，已停止旧采集，1.5 秒后自动重新启动"), true);
        m_startRetry.start();
        emit statusChanged();
    }

    const qint64 elapsed = m_rateClock.elapsed();
    if (elapsed >= 1000) {
        const quint64 received = m_frames.load(std::memory_order_relaxed);
        const quint64 delta = received - m_lastReportedFrames;
        emit frameRateChanged(delta * 1000.0 / elapsed, received,
                              m_dropped.load(std::memory_order_relaxed));
        m_lastReportedFrames = received;
        m_rateClock.restart();
    }
}

void DeviceController::initializeHotpluggedBootLoader()
{
    if (m_firmwareBootstrapInProgress
        || UsbBootstrap::detectDeviceMode() != UsbBootstrap::DeviceMode::BootLoader)
        return;

    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app) {
        emit message(QStringLiteral("无法取得应用程序实例，不能自动加载设备固件"), true);
        return;
    }

    m_firmwareBootstrapInProgress = true;
    m_connectionSettling.stop();
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    m_usbRecovery.stop();
    m_usbRecoveryInProgress = false;
    m_captureReady = false;
    emit statusChanged();

    QString error;
    const bool loaded = UsbBootstrap::loadFirmware(*app, &error);
    m_firmwareBootstrapInProgress = false;
    m_lastUsbMode = int(UsbBootstrap::detectDeviceMode());
    if (!loaded) {
        emit message(QStringLiteral("设备固件自动加载失败：%1").arg(error), true);
        emit statusChanged();
        return;
    }

    m_usbNeedsRecovery = true;
    m_usbRecoveryAttempts = 0;
    emit message(QStringLiteral("设备固件和 FPGA 已初始化，正在交接给采集 SDK…"), false);
    if (m_serviceConnected)
        scheduleUsbRecovery(QStringLiteral("USB Streamer 已由 BootLoader 初始化完成"));
    emit statusChanged();
}

void DeviceController::scheduleUsbRecovery(const QString &reason)
{
    if (!m_serviceConnected || m_usbRecoveryInProgress)
        return;
    if (UsbBootstrap::detectDeviceMode() != UsbBootstrap::DeviceMode::Streamer) {
        m_usbNeedsRecovery = true;
        emit message(QStringLiteral("%1，但 USB Streamer 尚未出现").arg(reason), true);
        return;
    }
    if (m_usbRecoveryAttempts >= 2) {
        m_startRequested = false;
        m_captureReady = false;
        emit message(QStringLiteral("自动恢复两次后仍无硬件帧。请退出软件，确认厂家程序已关闭，再重新打开本软件"), true);
        emit statusChanged();
        return;
    }

    m_usbNeedsRecovery = true;
    m_groupsSynchronized = false;
    m_usbRecoveryInProgress = true;
    ++m_usbRecoveryAttempts;
    m_usbRecoveryStep = 0;
    m_connectionSettling.stop();
    m_configurationSettling.stop();
    m_startRetry.stop();
    m_frameConfirmation.stop();
    const bool shouldResumeCapture = m_startRequested || m_capturing;
    m_capturing = false;
    m_startRequested = shouldResumeCapture;
    m_captureReady = false;
    // Board Info is emitted roughly four seconds after the Windows Streamer
    // endpoint appears. Sending DeviceSet before that only targets id -1.
    m_usbRecovery.start(6000);
    emit message(QStringLiteral("%1：等待板卡稳定后将自动重新下发配置（恢复 %2/2），无需反复点击开始")
                     .arg(reason).arg(m_usbRecoveryAttempts), false);
    emit statusChanged();
}

void DeviceController::continueUsbRecovery()
{
    if (!m_serviceConnected || !m_usbRecoveryInProgress)
        return;
    if (UsbBootstrap::detectDeviceMode() != UsbBootstrap::DeviceMode::Streamer) {
        m_usbRecoveryInProgress = false;
        m_usbNeedsRecovery = true;
        emit message(QStringLiteral("USB 恢复中断：Streamer 已离线"), true);
        emit statusChanged();
        return;
    }

    auto &sdk = Client::getInstance();
    if (m_usbRecoveryStep < 2) {
        try {
            sdk.startCapture(false);
            if (m_usbRecoveryStep == 0) {
                if (!synchronizeConfiguredGroups()) {
                    m_usbRecoveryInProgress = false;
                    m_startRequested = false;
                    emit statusChanged();
                    return;
                }
                m_groupsSynchronized = true;
            } else {
                sdk.pushConfigToDevice();
            }
        } catch (const std::exception &e) {
            emit message(QStringLiteral("USB 恢复下发配置异常：%1")
                             .arg(QString::fromLocal8Bit(e.what())), true);
        } catch (...) {
            emit message(QStringLiteral("USB 恢复下发配置出现未知 SDK 异常"), true);
        }
        ++m_usbRecoveryStep;
        emit message(QStringLiteral("正在恢复硬件配置（第 %1/2 次）…").arg(m_usbRecoveryStep), false);
        m_usbRecovery.start(2500);
        return;
    }

    m_usbRecoveryInProgress = false;
    m_usbNeedsRecovery = false;
    m_expectedUsbCycle = false;
    m_startAttempts = 0;
    m_captureReady = true;
    if (m_startRequested) {
        emit message(QStringLiteral("硬件配置恢复完成，正在自动开始采集…"), false);
        tryStart();
    } else {
        emit message(QStringLiteral("USB 板卡连接已建立，可以点击开始采集"), false);
        emit statusChanged();
    }
}

void DeviceController::setHardwareOnline(bool online)
{
    if (m_hardwareOnline == online)
        return;
    m_hardwareOnline = online;
    emit statusChanged();
    emit message(online ? QStringLiteral("已收到真实设备数据：USB 硬件在线")
                        : QStringLiteral("超过 2.5 秒未收到数据：USB 数据流已中断"), !online);
}
