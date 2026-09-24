#include "remotecontrolserver.h"

#include "client.h"
#include "devicecontroller.h"
#include "parameterpanel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QWidget>
#include <exception>

namespace {
constexpr int MaxControlMessageBytes = 2 * 1024 * 1024;

bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && qIsFinite(value.toDouble());
}
}

RemoteControlServer::RemoteControlServer(DeviceController *device,
                                         ParameterPanel *parameters,
                                         QWidget *hostWindow, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_parameters(parameters)
    , m_hostWindow(hostWindow)
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection,
            this, &RemoteControlServer::acceptPendingConnections);
    connect(m_device, &DeviceController::statusChanged,
            this, &RemoteControlServer::publishStateIfChanged);
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    timer->setTimerType(Qt::CoarseTimer);
    connect(timer, &QTimer::timeout, this, &RemoteControlServer::publishStateIfChanged);
    timer->start();
}

RemoteControlServer::~RemoteControlServer()
{
    stop();
}

QString RemoteControlServer::serverName()
{
    return QStringLiteral("PA1664Workbench-Control-v1");
}

bool RemoteControlServer::start(QString *errorText)
{
    if (m_server->isListening())
        return true;
    QLocalServer::removeServer(serverName());
    if (!m_server->listen(serverName())) {
        if (errorText)
            *errorText = m_server->errorString();
        return false;
    }
    return true;
}

void RemoteControlServer::stop()
{
    const auto clients = m_clientIds.keys();
    for (QLocalSocket *socket : clients) {
        socket->disconnect(this);
        socket->disconnectFromServer();
        socket->deleteLater();
    }
    m_clientIds.clear();
    m_receiveBuffers.clear();
    if (m_server->isListening())
        m_server->close();
    QLocalServer::removeServer(serverName());
}

bool RemoteControlServer::isListening() const
{
    return m_server->isListening();
}

void RemoteControlServer::acceptPendingConnections()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            continue;
        m_clientIds.insert(socket, m_nextClientId++);
        m_receiveBuffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket] { readClient(socket); });
        connect(socket, &QLocalSocket::disconnected, this,
                [this, socket] { removeClient(socket); });
    }
    publishStateIfChanged();
}

void RemoteControlServer::readClient(QLocalSocket *socket)
{
    if (!socket || !m_clientIds.contains(socket))
        return;
    QByteArray &buffer = m_receiveBuffers[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > MaxControlMessageBytes) {
        socket->disconnectFromServer();
        return;
    }
    for (;;) {
        const int newline = buffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            QJsonObject invalid;
            invalid.insert(QStringLiteral("kind"), QStringLiteral("response"));
            invalid.insert(QStringLiteral("success"), false);
            invalid.insert(QStringLiteral("errorCode"), QStringLiteral("invalid_json"));
            invalid.insert(QStringLiteral("message"), QStringLiteral("本机控制消息不是有效的 JSON 对象。"));
            writeMessage(socket, invalid);
            continue;
        }
        handleRequest(m_clientIds.value(socket), document.object());
    }
}

void RemoteControlServer::removeClient(QLocalSocket *socket)
{
    if (!socket)
        return;
    m_clientIds.remove(socket);
    m_receiveBuffers.remove(socket);
    socket->deleteLater();
}

void RemoteControlServer::writeMessage(QLocalSocket *socket,
                                       const QJsonObject &message)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState)
        return;
    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket->write(payload);
}

void RemoteControlServer::sendResponse(quint64 clientId,
                                       const QJsonObject &message)
{
    for (auto it = m_clientIds.cbegin(); it != m_clientIds.cend(); ++it) {
        if (it.value() == clientId) {
            writeMessage(it.key(), message);
            return;
        }
    }
}

void RemoteControlServer::broadcastEvent(const QJsonObject &message)
{
    for (QLocalSocket *socket : m_clientIds.keys())
        writeMessage(socket, message);
}

QJsonObject RemoteControlServer::reply(const QJsonObject &request, bool success,
                                       const QString &message,
                                       const QString &errorCode) const
{
    QJsonObject response;
    response.insert(QStringLiteral("kind"), QStringLiteral("response"));
    response.insert(QStringLiteral("operation"), request.value(QStringLiteral("operation")));
    response.insert(QStringLiteral("requestId"), request.value(QStringLiteral("requestId")));
    response.insert(QStringLiteral("success"), success);
    if (!message.isEmpty())
        response.insert(QStringLiteral("message"), message);
    if (!errorCode.isEmpty())
        response.insert(QStringLiteral("errorCode"), errorCode);
    return response;
}

QJsonObject RemoteControlServer::stateObject() const
{
    QJsonObject device;
    device.insert(QStringLiteral("connected"), m_device->serviceConnected());
    device.insert(QStringLiteral("hardwareOnline"), m_device->hardwareOnline());
    device.insert(QStringLiteral("captureReady"), m_device->captureReady());
    device.insert(QStringLiteral("startPending"), m_device->startPending());
    device.insert(QStringLiteral("address"), m_address);
    device.insert(QStringLiteral("deviceId"), m_deviceId);

    QJsonObject acquisition;
    acquisition.insert(QStringLiteral("running"), m_device->capturing());
    acquisition.insert(QStringLiteral("frames"), static_cast<double>(m_device->totalFrames()));
    acquisition.insert(QStringLiteral("pointCount"), m_device->pointCount());
    acquisition.insert(QStringLiteral("beamCount"), m_device->beamCount());
    acquisition.insert(QStringLiteral("currentGroup"), m_device->currentGroup());

    QJsonObject state;
    state.insert(QStringLiteral("product"), QStringLiteral("PA1664Workbench"));
    state.insert(QStringLiteral("device"), device);
    state.insert(QStringLiteral("acquisition"), acquisition);
    return state;
}

QJsonObject RemoteControlServer::configurationObject() const
{
    QJsonObject pa;
    if (!m_device->serviceConnected())
        return QJsonObject{{QStringLiteral("pa1664"), pa}};
    try {
        auto &sdk = Client::getInstance();
        pa.insert(QStringLiteral("groupId"), sdk.getCurrentGroup());
        pa.insert(QStringLiteral("gainDb"), sdk.getGain());
        pa.insert(QStringLiteral("rangeStartMm"), sdk.getRangeStart());
        pa.insert(QStringLiteral("rangeEndMm"), sdk.getRangeEnd());
        pa.insert(QStringLiteral("velocityMps"), sdk.getWorkpieceVelocity());
        pa.insert(QStringLiteral("pointQuantity"), sdk.getPointQuantity());
        pa.insert(QStringLiteral("frameRateHz"), sdk.getFrameRate());
        pa.insert(QStringLiteral("transmissionChannel"), sdk.getTransmissionChannel());
        pa.insert(QStringLiteral("receptionChannel"), sdk.getReceptionChannel());
        QJsonArray groups;
        for (int group : sdk.getGroupsNo(false))
            groups.append(group);
        pa.insert(QStringLiteral("groups"), groups);
    } catch (...) {
        // A state/configuration query must never unwind into Qt's event loop.
    }
    return QJsonObject{{QStringLiteral("pa1664"), pa}};
}

QJsonObject RemoteControlServer::applyConfigurationPatch(const QJsonObject &request)
{
    if (!m_device->serviceConnected())
        return reply(request, false, QStringLiteral("SDK 尚未连接。"), QStringLiteral("device_disconnected"));
    const QJsonObject pa = request.value(QStringLiteral("patch")).toObject()
                               .value(QStringLiteral("pa1664")).toObject();
    if (pa.isEmpty())
        return reply(request, false, QStringLiteral("没有可应用的 PA-1664 参数。"), QStringLiteral("empty_patch"));

    auto inRange = [&pa](const char *name, double low, double high) {
        const QJsonValue value = pa.value(QLatin1String(name));
        return value.isUndefined() || (finiteNumber(value) && value.toDouble() >= low && value.toDouble() <= high);
    };
    if (!inRange("gainDb", 0.0, 120.0) ||
        !inRange("rangeStartMm", -1000.0, 100000.0) ||
        !inRange("rangeEndMm", -1000.0, 100000.0) ||
        !inRange("velocityMps", 100.0, 20000.0) ||
        !inRange("pointQuantity", 1, 65536) ||
        !inRange("frameRateHz", 1, 12848) ||
        !inRange("transmissionChannel", 1, 8) ||
        !inRange("receptionChannel", 1, 8)) {
        return reply(request, false, QStringLiteral("参数超出 PA-1664 安全范围。"), QStringLiteral("invalid_parameter"));
    }
    if (pa.contains(QStringLiteral("rangeStartMm")) && pa.contains(QStringLiteral("rangeEndMm")) &&
        pa.value(QStringLiteral("rangeEndMm")).toDouble() <= pa.value(QStringLiteral("rangeStartMm")).toDouble()) {
        return reply(request, false, QStringLiteral("范围终点必须大于范围起点。"), QStringLiteral("invalid_range"));
    }

    const bool wasCapturing = m_device->capturing() || m_device->startPending();
    m_device->stop();
    const auto resumeIfNeeded = [this, wasCapturing] {
        if (wasCapturing)
            m_device->resumeAfterConfigurationChange();
    };
    try {
        auto &sdk = Client::getInstance();
        if (pa.contains(QStringLiteral("groupId"))) {
            const int group = pa.value(QStringLiteral("groupId")).toInt(-1);
            if (!sdk.getGroupsNo(false).contains(group) || !sdk.setCurrentGroup(group)) {
                resumeIfNeeded();
                return reply(request, false, QStringLiteral("工作组不存在或无法切换。"), QStringLiteral("invalid_group"));
            }
        }
        bool ok = true;
        bool prfClamped = false;
        int appliedFrameRate = -1;
        if (pa.contains(QStringLiteral("gainDb")))
            ok = sdk.setGain(pa.value(QStringLiteral("gainDb")).toDouble()) && ok;
        const bool hasRangeStart = pa.contains(QStringLiteral("rangeStartMm"));
        const bool hasRangeEnd = pa.contains(QStringLiteral("rangeEndMm"));
        const double targetStart = pa.value(QStringLiteral("rangeStartMm")).toDouble(sdk.getRangeStart());
        const double targetEnd = pa.value(QStringLiteral("rangeEndMm")).toDouble(sdk.getRangeEnd());
        // When shifting a complete window past the old end, update the end
        // first so the SDK does not reject an otherwise valid intermediate
        // start/end pair.
        if (hasRangeStart && hasRangeEnd && targetStart > sdk.getRangeEnd()) {
            ok = sdk.setRangeEnd(targetEnd) && ok;
            ok = sdk.setRangeStart(targetStart) && ok;
        } else {
            if (hasRangeStart) ok = sdk.setRangeStart(targetStart) && ok;
            if (hasRangeEnd) ok = sdk.setRangeEnd(targetEnd) && ok;
        }
        if (pa.contains(QStringLiteral("velocityMps")))
            ok = sdk.setWorkpieceVelocity(pa.value(QStringLiteral("velocityMps")).toDouble()) && ok;
        if (pa.contains(QStringLiteral("pointQuantity")))
            ok = sdk.setPointQuantity(pa.value(QStringLiteral("pointQuantity")).toInt()) && ok;
        if (pa.contains(QStringLiteral("frameRateHz"))) {
            const int requestedRate = pa.value(QStringLiteral("frameRateHz")).toInt();
            qint64 totalBeams = 0;
            for (int group : sdk.getGroupsNo(true)) {
                try {
                    const int beams = sdk.getBeamCounts(group);
                    if (beams > 0) totalBeams += beams;
                } catch (...) {
                    // Disabled SDK tombstones may not have a complete map.
                }
            }
            totalBeams = qMax<qint64>(1, totalBeams);
            appliedFrameRate = qMin(requestedRate, qMax(1, int(12848 / totalBeams)));
            prfClamped = appliedFrameRate != requestedRate;
            ok = sdk.setFrameRate(appliedFrameRate) && ok;
        }
        if (pa.contains(QStringLiteral("transmissionChannel"))) {
            const int channel = pa.value(QStringLiteral("transmissionChannel")).toInt();
            ok = sdk.setTransmissionChannel(channel) && sdk.getTransmissionChannel() == channel && ok;
        }
        if (pa.contains(QStringLiteral("receptionChannel"))) {
            const int channel = pa.value(QStringLiteral("receptionChannel")).toInt();
            ok = sdk.setReceptionChannel(channel) && sdk.getReceptionChannel() == channel && ok;
        }
        if (!ok) {
            resumeIfNeeded();
            return reply(request, false, QStringLiteral("一个或多个参数被 SDK 拒绝。"), QStringLiteral("sdk_rejected"));
        }
        sdk.pushConfigToDevice();
        m_parameters->refreshGroups();
        m_parameters->reload();
        m_device->refreshGeometry();
        resumeIfNeeded();
        const QString message = prfClamped
            ? QStringLiteral("PA-1664 参数已写入；为防止 USB 停流，PRF 已自动限制为 %1 Hz。")
                  .arg(appliedFrameRate)
            : QStringLiteral("PA-1664 参数已写入并推送到设备。");
        QJsonObject response = reply(request, true, message);
        response.insert(QStringLiteral("configuration"), configurationObject());
        return response;
    } catch (const std::exception &error) {
        resumeIfNeeded();
        return reply(request, false,
                     QStringLiteral("SDK 参数写入异常：%1").arg(QString::fromLocal8Bit(error.what())),
                     QStringLiteral("sdk_exception"));
    } catch (...) {
        resumeIfNeeded();
        return reply(request, false, QStringLiteral("SDK 参数写入发生未知异常。"), QStringLiteral("sdk_exception"));
    }
}

QJsonObject RemoteControlServer::executeCommand(const QJsonObject &request)
{
    const QString command = request.value(QStringLiteral("command")).toString();
    bool ok = false;
    QString message;
    QString errorCode;
    if (command == QStringLiteral("workbench.activate")) {
        m_hostWindow->showNormal();
        m_hostWindow->raise();
        m_hostWindow->activateWindow();
        ok = true;
        message = QStringLiteral("PA-1664 工作台已置前。");
    } else if (command == QStringLiteral("device.connect")) {
        m_address = request.value(QStringLiteral("address")).toString(QStringLiteral("127.0.0.1")).trimmed();
        m_deviceId = request.value(QStringLiteral("deviceId")).toInt(0);
        if (m_address.isEmpty() || m_deviceId < 0 || m_deviceId > 255) {
            errorCode = QStringLiteral("invalid_device_target");
            message = QStringLiteral("SDK 地址或设备编号无效。");
        } else {
            ok = m_device->connectDevice(m_address, m_deviceId);
            message = ok ? QStringLiteral("已提交 SDK 连接。") : QStringLiteral("SDK 连接失败。");
            if (!ok) errorCode = QStringLiteral("connect_failed");
        }
    } else if (command == QStringLiteral("device.disconnect")) {
        m_device->disconnectDevice();
        ok = true;
        message = QStringLiteral("SDK 已断开。");
    } else if (command == QStringLiteral("acquisition.start")) {
        if (!m_device->serviceConnected()) {
            errorCode = QStringLiteral("device_disconnected");
            message = QStringLiteral("请先连接 SDK。");
        } else {
            ok = m_device->start();
            message = ok ? QStringLiteral("已提交开始采集；硬件准备期间会自动排队。")
                         : QStringLiteral("开始采集被设备拒绝。");
            if (!ok) errorCode = QStringLiteral("start_rejected");
        }
    } else if (command == QStringLiteral("acquisition.stop")) {
        m_device->stop();
        ok = true;
        message = QStringLiteral("已停止采集。");
    } else if (command == QStringLiteral("encoder.reset")) {
        if (!m_device->serviceConnected()) {
            errorCode = QStringLiteral("device_disconnected");
            message = QStringLiteral("请先连接 SDK。");
        } else {
            m_device->resetEncoder();
            ok = true;
            message = QStringLiteral("编码器已复位。");
        }
    } else {
        errorCode = QStringLiteral("unsupported_command");
        message = QStringLiteral("当前 PA-1664 工作台不支持命令：%1").arg(command);
    }
    QJsonObject response = reply(request, ok, message, errorCode);
    response.insert(QStringLiteral("state"), stateObject());
    return response;
}

void RemoteControlServer::handleRequest(quint64 clientId,
                                        const QJsonObject &request)
{
    const QString operation = request.value(QStringLiteral("operation")).toString();
    QJsonObject response;
    if (operation == QStringLiteral("ultrasound.capabilities.get")) {
        response = reply(request, true, QStringLiteral("已获取 PA-1664 远程控制能力。"));
        response.insert(QStringLiteral("capabilities"), QJsonObject{
            {QStringLiteral("product"), QStringLiteral("PA1664Workbench")},
            {QStringLiteral("deviceControl"), true},
            {QStringLiteral("acquisitionControl"), true},
            {QStringLiteral("parameterControl"), true},
            {QStringLiteral("motionControl"), false},
            {QStringLiteral("dpr500Control"), false}});
        response.insert(QStringLiteral("state"), stateObject());
    } else if (operation == QStringLiteral("ultrasound.state.get")) {
        response = reply(request, true);
        response.insert(QStringLiteral("state"), stateObject());
    } else if (operation == QStringLiteral("ultrasound.configuration.get")) {
        response = reply(request, true, QStringLiteral("已读取 PA-1664 常用参数。"));
        response.insert(QStringLiteral("configuration"), configurationObject());
    } else if (operation == QStringLiteral("ultrasound.configuration.patch")) {
        response = applyConfigurationPatch(request);
    } else if (operation == QStringLiteral("ultrasound.command.execute")) {
        response = executeCommand(request);
    } else {
        response = reply(request, false, QStringLiteral("未知操作：%1").arg(operation),
                         QStringLiteral("unsupported_operation"));
    }
    sendResponse(clientId, response);
    publishStateIfChanged();
}

void RemoteControlServer::publishStateIfChanged()
{
    if (m_clientIds.isEmpty())
        return;
    const QJsonObject state = stateObject();
    const QString signature = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
    if (signature == m_lastStateSignature)
        return;
    m_lastStateSignature = signature;
    QJsonObject event;
    event.insert(QStringLiteral("kind"), QStringLiteral("event"));
    event.insert(QStringLiteral("operation"), QStringLiteral("ultrasound.state.changed"));
    event.insert(QStringLiteral("success"), true);
    event.insert(QStringLiteral("state"), state);
    broadcastEvent(event);
}
