/**
 * @file main.cpp
 * @brief PA1664 Robot Communication Test Client
 *
 * Verifies the PA1664 binary control protocol (TCP 43100), then optionally
 * applies a drive_settings.ini to the host and opens the laser / camera
 * peripherals exactly like the operator workbench does.
 */

#include <QNetworkProxy>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QCommandLineParser>
#include <QVector>
#include <QHash>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// PA1664 Protocol Implementation
namespace PA1664 {
    constexpr quint32 kWireMagic = 0x50413136u;  // "PA16" big-endian
    constexpr quint16 kProtocolVersion = 1;
    constexpr int kControlHeaderBytes = 50;
    constexpr int kSettingsSchemaVersion = 13;

    enum class MessageType : quint16 {
        Hello = 1,
        HelloAck = 2,
        Request = 3,
        Response = 4,
        Event = 5,
        Ping = 6,
        Pong = 7,
        Error = 8,
    };

    struct Frame {
        MessageType type = MessageType::Hello;
        quint16 flags = 0;
        quint64 sessionId = 0;
        quint64 sequence = 0;
        quint64 requestId = 0;
        quint64 timestampUs = 0;
        QByteArray payload;
    };

    quint32 crc32(const char* data, qsizetype size) {
        constexpr quint32 poly = 0xEDB88320u;
        static QVector<quint32> table;

        if (table.isEmpty()) {
            table.resize(256);
            for (int i = 0; i < 256; i++) {
                quint32 c = i;
                for (int j = 0; j < 8; j++)
                    c = (c & 1) ? (c >> 1) ^ poly : c >> 1;
                table[i] = c;
            }
        }

        quint32 crc = 0xFFFFFFFF;
        for (qsizetype i = 0; i < size; i++)
            crc = table[(crc ^ static_cast<quint8>(data[i])) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }

    QByteArray encode(const Frame& f) {
        QByteArray buf;
        buf.reserve(kControlHeaderBytes + f.payload.size());
        QDataStream s(&buf, QIODevice::WriteOnly);
        s.setByteOrder(QDataStream::LittleEndian);

        s << kWireMagic << kProtocolVersion;
        s << static_cast<quint16>(f.type) << f.flags;
        s << f.sessionId << f.sequence << f.requestId << f.timestampUs;
        s << static_cast<quint32>(f.payload.size());
        s << crc32(f.payload.constData(), f.payload.size());
        s.writeRawData(f.payload.constData(), f.payload.size());

        return buf;
    }

    bool decode(const QByteArray& data, Frame* f, int* consumed) {
        if (data.size() < kControlHeaderBytes) return false;

        QDataStream s(data);
        s.setByteOrder(QDataStream::LittleEndian);

        quint32 magic; quint16 ver, type;
        quint32 payloadSize, crc;

        s >> magic >> ver >> type >> f->flags;
        s >> f->sessionId >> f->sequence >> f->requestId >> f->timestampUs;
        s >> payloadSize >> crc;

        if (magic != kWireMagic || ver != kProtocolVersion) return false;
        if (data.size() < kControlHeaderBytes + static_cast<int>(payloadSize)) return false;

        f->type = static_cast<MessageType>(type);
        f->payload = data.mid(kControlHeaderBytes, payloadSize);

        if (payloadSize > 0 && crc32(f->payload.constData(), f->payload.size()) != crc)
            return false;

        if (consumed) *consumed = kControlHeaderBytes + payloadSize;
        return true;
    }
}

// One queued request sent to the host after the handshake completes.
struct Step {
    QString operation;
    QJsonObject data;
    int delayAfterMs = 0;
    QString label;
};

class RobotCommTester : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString host = QStringLiteral("192.168.1.20");
        quint16 port = 43100;
        QString iniPath;
        QString laserSerial;
        QString usbCameraIndices;   // "0,1" -> use USB camera source for slots
        int holdSeconds = 0;
        bool connectDrive = true;
        bool connectLaser = true;
        bool connectCameras = true;
        bool connectImu = false;
        bool enableDriveTest = false;
        bool clampProbe = false;
        bool clampMove = false;
        bool verbose = false;
    };

    explicit RobotCommTester(const Options& options, QObject* parent = nullptr)
        : QObject(parent), opt_(options), out_(stdout)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out_.setEncoding(QStringConverter::Utf8);
#else
        out_.setCodec("UTF-8");
#endif
        sock_ = new QTcpSocket(this);
        sock_->setProxy(QNetworkProxy::NoProxy);

        logFile_.setFileName("robot_comm_test.log");
        if (logFile_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            logStream_.setDevice(&logFile_);

        // Full, untruncated protocol trace for offline diagnosis.
        fullLogFile_.setFileName("robot_comm_test_full.log");
        if (fullLogFile_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            fullLogStream_.setDevice(&fullLogFile_);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            fullLogStream_.setEncoding(QStringConverter::Utf8);
#else
            fullLogStream_.setCodec("UTF-8");
#endif
        }

        connect(sock_, &QTcpSocket::connected, this, &RobotCommTester::onConnected);
        connect(sock_, &QTcpSocket::disconnected, this, &RobotCommTester::onDisconnected);
        connect(sock_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this, &RobotCommTester::onError);
        connect(sock_, &QTcpSocket::readyRead, this, &RobotCommTester::onData);

        heartbeatTimer_.setParent(this);
        connect(&heartbeatTimer_, &QTimer::timeout, this, &RobotCommTester::sendPing);

        pollTimer_.setParent(this);
        pollTimer_.setInterval(10000);
        connect(&pollTimer_, &QTimer::timeout, this, &RobotCommTester::pollSnapshot);
    }

    void start() {
        out_ << "\n========================================\n";
        out_ << "   PA1664 Robot Communication Test\n";
        out_ << "   Binary Protocol Version\n";
        out_ << "========================================\n\n";
        out_ << QString("Target: %1:%2\n").arg(opt_.host).arg(opt_.port);
        if (!opt_.iniPath.isEmpty())
            out_ << QString("Settings: %1\n").arg(opt_.iniPath);
        if (opt_.holdSeconds > 0)
            out_ << QString("Hold: %1s\n").arg(opt_.holdSeconds);
        if (opt_.enableDriveTest)
            out_ << QStringLiteral("Drive enable probe: on\n");
        if (opt_.clampProbe)
            out_ << QStringLiteral("Clamp motor probe: on%1\n")
                        .arg(opt_.clampMove ? QStringLiteral(" (with 6% pulse)")
                                            : QStringLiteral(" (no motion)"));
        out_ << "\n";

        log("========== Test Start ==========");
        log(QString("Time: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
        log(QString("Target: %1:%2").arg(opt_.host).arg(opt_.port));

        out_ << "[1] TCP Connection Test\n";
        out_ << "-----------------------------------\n";

        connTimer_.start();
        sock_->connectToHost(opt_.host, opt_.port);

        QTimer::singleShot(5000, this, [this]() {
            if (sock_->state() != QTcpSocket::ConnectedState) {
                log("[ERROR] Connection timeout (5s)");
                out_ << "[FAIL] Connection timeout\n\n";
                printSummary();
                emit finishedNow();
            }
        });
    }

signals:
    void finishedNow();

private slots:
    void onConnected() {
        const int ms = connTimer_.elapsed();
        out_ << QString("[ OK ] TCP Connected (%1ms)\n").arg(ms);
        out_ << QString("  Local: %1:%2\n")
                    .arg(sock_->localAddress().toString())
                    .arg(sock_->localPort());
        out_ << QString("  Remote: %1:%2\n\n")
                    .arg(sock_->peerAddress().toString())
                    .arg(sock_->peerPort());
        log(QString("TCP connected in %1ms").arg(ms));

        results_.connected = true;
        results_.connMs = ms;
        results_.passed++;

        QTimer::singleShot(100, this, &RobotCommTester::sendHello);
    }

    void onDisconnected() {
        log("Disconnected");
        out_ << "\nConnection closed\n";
        if (!summaryPrinted_) printSummary();
        emit finishedNow();
    }

    void onError(QAbstractSocket::SocketError err) {
        Q_UNUSED(err)
        const QString errStr = sock_->errorString();
        log(QString("[ERROR] Socket error: %1").arg(errStr));
        out_ << QString("[FAIL] Error: %1\n\n").arg(errStr);
        printSummary();
        emit finishedNow();
    }

    void onData() {
        const QByteArray data = sock_->readAll();
        rxBuf_.append(data);

        while (rxBuf_.size() >= PA1664::kControlHeaderBytes) {
            PA1664::Frame frame;
            int consumed = 0;
            if (!PA1664::decode(rxBuf_, &frame, &consumed)) break;

            switch (frame.type) {
            case PA1664::MessageType::HelloAck:
                handleHelloAck(frame);
                break;
            case PA1664::MessageType::Response:
                handleResponse(frame);
                break;
            case PA1664::MessageType::Event:
                handleEvent(frame);
                break;
            case PA1664::MessageType::Error:
                out_ << "[!] Host error frame: "
                     << QString::fromUtf8(QJsonDocument::fromJson(frame.payload)
                                              .toJson(QJsonDocument::Compact)) << "\n";
                break;
            default:
                break;
            }
            rxBuf_.remove(0, consumed);
        }
    }

    void sendHello() {
        out_ << "[2] PA1664 Hello Handshake\n";
        out_ << "-----------------------------------\n";
        log("Sending PA1664 Hello");

        QJsonObject hello;
        hello["type"] = "hello";
        hello["protocolVersion"] = PA1664::kProtocolVersion;
        hello["clientId"] = "robot_comm_test";
        hello["clientVersion"] = "2.0";
        hello["timestamp"] = QDateTime::currentMSecsSinceEpoch();

        PA1664::Frame frame;
        frame.type = PA1664::MessageType::Hello;
        frame.sequence = seq_++;
        frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        frame.payload = QJsonDocument(hello).toJson(QJsonDocument::Compact);

        reqTimer_.start();
        sock_->write(PA1664::encode(frame));
        sock_->flush();

        out_ << "  Waiting for HelloAck...\n\n";
        log("Hello frame sent");

        QTimer::singleShot(5000, this, [this]() {
            if (!results_.helloAck) {
                log("[ERROR] HelloAck timeout");
                out_ << "[FAIL] HelloAck timeout (5s)\n\n";
                sock_->disconnectFromHost();
            }
        });
    }

    void sendPing() {
        if (sessionId_ == 0 || sock_->state() != QTcpSocket::ConnectedState) return;

        QJsonObject ping;
        ping["clientTimeMs"] = QString::number(QDateTime::currentMSecsSinceEpoch());

        PA1664::Frame frame;
        frame.type = PA1664::MessageType::Ping;
        frame.sessionId = sessionId_;
        frame.sequence = seq_++;
        frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        frame.payload = QJsonDocument(ping).toJson(QJsonDocument::Compact);
        sock_->write(PA1664::encode(frame));
    }

    void pollSnapshot() {
        if (sock_->state() != QTcpSocket::ConnectedState) return;
        sendRequest(QStringLiteral("robot.peripherals.snapshot"), QJsonObject(),
                    QStringLiteral("peripherals.poll"));
    }

private:
    void handleHelloAck(const PA1664::Frame& frame) {
        const int ms = reqTimer_.elapsed();
        results_.helloAck = true;
        results_.passed++;
        sessionId_ = frame.sessionId;

        out_ << QString("[ OK ] Received HelloAck (%1ms)\n").arg(ms);
        out_ << QString("  SessionId: %1\n").arg(frame.sessionId);
        const QJsonObject ack = QJsonDocument::fromJson(frame.payload).object();
        out_ << QString("  Host: %1  Version: %2\n")
                    .arg(ack.value(QStringLiteral("hostName")).toString())
                    .arg(ack.value(QStringLiteral("hostVersion")).toString());
        out_ << "\n";

        log(QString("HelloAck received in %1ms").arg(ms));

        heartbeatTimer_.start(250);
        buildSteps();
        QTimer::singleShot(200, this, &RobotCommTester::runNextStep);
    }

    void handleResponse(const PA1664::Frame& frame) {
        const QJsonObject body = QJsonDocument::fromJson(frame.payload).object();
        const bool ok = body.value(QStringLiteral("ok")).toBool(false);
        const QString operation = labels_.value(frame.requestId);
        const QString op = ops_.value(frame.requestId);
        const qint64 latencyMs = sentMs_.contains(frame.requestId)
                                     ? QDateTime::currentMSecsSinceEpoch()
                                           - sentMs_.value(frame.requestId)
                                     : -1;

        fullLog(QStringLiteral("RESPONSE requestId=%1 operation=%2 ok=%3 latency_ms=%4 payload=%5")
                    .arg(frame.requestId)
                    .arg(op)
                    .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(latencyMs)
                    .arg(QString::fromUtf8(
                        QJsonDocument(body).toJson(QJsonDocument::Compact))));

        out_ << QString("[%1] %2  (requestId=%3, %4 ms)\n")
                    .arg(ok ? " OK " : "FAIL")
                    .arg(operation.isEmpty() ? QStringLiteral("response") : operation)
                    .arg(frame.requestId)
                    .arg(latencyMs);
        if (!ok) {
            out_ << QString("       code=%1 text=%2\n")
                        .arg(body.value(QStringLiteral("errorCode")).toString())
                        .arg(body.value(QStringLiteral("errorText")).toString());
        }
        if (!ok) {
            const QString errorCode = body.value(QStringLiteral("errorCode")).toString();
            if (errorCode == QLatin1String("not_session_owner"))
                sessionBlocked_ = true;
            if (op.startsWith(QLatin1String("robot.clamp.")))
                clampFailures_ << QStringLiteral("%1: %2").arg(op, errorCode);
        }
        if (op == QLatin1String("robot.enable")) {
            if (ok) ++driveEnableAccepted_;
            else driveEnableRejected_ = body.value(QStringLiteral("errorText")).toString();
        }

        const QJsonObject data = body.value(QStringLiteral("data")).toObject();
        if (op == QLatin1String("robot.clamp.status") && opt_.clampProbe && ok) {
            trackSnapshot(data);
            if (!clampStepsScheduled_) scheduleClampSteps(data);
        }
        if (op == QLatin1String("robot.snapshot")
            || op == QLatin1String("robot.peripherals.snapshot")) {
            out_ << QString("       %1\n").arg(summaryLine(data));
            trackSnapshot(data);
        } else if (opt_.verbose) {
            out_ << QString("       %1\n").arg(QString::fromUtf8(
                       QJsonDocument(body).toJson(QJsonDocument::Compact)));
        } else {
            out_ << QString("       %1\n").arg(compactBody(body));
        }

        results_.responses++;
        if (frame.requestId == currentRequestId_) {
            awaitingResponse_ = false;
            if (!inHold_)
                QTimer::singleShot(stepDelayMs_, this, &RobotCommTester::runNextStep);
        }
    }

    void handleEvent(const PA1664::Frame& frame) {
        const QJsonObject ev = QJsonDocument::fromJson(frame.payload).object();
        const QString name = ev.value(QStringLiteral("event")).toString();

        if (name == QLatin1String("robot.laser.frame")) {
            ++laserFrames_;
            laserPoints_ = ev.value(QStringLiteral("pointCount")).toInt();
            return;
        }
        if (name == QLatin1String("ultrasound.status")) return;

        fullLog(QStringLiteral("EVENT %1 payload=%2")
                    .arg(name,
                         QString::fromUtf8(QJsonDocument(ev).toJson(QJsonDocument::Compact))));

        if (name == QLatin1String("robot.log")) {
            out_ << "       [log] "
                 << ev.value(QStringLiteral("message")).toString() << "\n";
            return;
        }
        if (name == QLatin1String("robot.peripheral.log")) {
            ++peripheralLogLines_;
            const QString text = ev.value(QStringLiteral("message")).toString();
            if (text.contains(QLatin1String("CLAMP.")) ||
                text.contains(QLatin1String("应答")) ||
                text.contains(QLatin1String("response")))
                ++clampReplyLines_;
            out_ << "       [clamp] " << text.left(220) << "\n";
            return;
        }
        if (name == QLatin1String("robot.laser.devices")
            || name == QLatin1String("robot.camera.devices")) {
            out_ << "       [" << name << "] "
                 << QString::fromUtf8(QJsonDocument(ev.value(QStringLiteral("devices")).toArray())
                                          .toJson(QJsonDocument::Compact)) << "\n";
            return;
        }
        if (name == QLatin1String("robot.laser.connection")) {
            laserConnected_ = ev.value(QStringLiteral("connected")).toBool();
            laserMessage_ = ev.value(QStringLiteral("message")).toString();
            out_ << "       [" << name << "] " << compactEvent(ev) << "\n";
            return;
        }
        if (name == QLatin1String("robot.camera.connection")) {
            const int slot = ev.value(QStringLiteral("slot")).toInt(-1);
            if (slot >= 0 && slot < 2) {
                camConnected_[slot] = ev.value(QStringLiteral("connected")).toBool();
                camMessage_[slot] = ev.value(QStringLiteral("message")).toString();
            }
            out_ << "       [" << name << "] " << compactEvent(ev) << "\n";
            return;
        }

        if (name.startsWith(QLatin1String("robot.laser."))
            || name.startsWith(QLatin1String("robot.camera."))
            || name.startsWith(QLatin1String("robot.imu."))
            || name.startsWith(QLatin1String("robot.clamp."))
            || name == QLatin1String("robot.connectionChanged")
            || name == QLatin1String("robot.stateChanged")) {
            out_ << "       [" << name << "] "
                 << compactEvent(ev) << "\n";
        }
    }

    void trackSnapshot(const QJsonObject& data) {
        if (data.contains(QStringLiteral("driveConnected"))) {
            driveConnected_ = data.value(QStringLiteral("driveConnected")).toBool();
            const QJsonObject tel = data.value(QStringLiteral("telemetry")).toObject();
            if (tel.contains(QStringLiteral("stateKey")))
                driveState_ = tel.value(QStringLiteral("stateKey")).toString();
            if (tel.contains(QStringLiteral("feedbackFresh")))
                driveFeedbackFresh_ = tel.value(QStringLiteral("feedbackFresh")).toBool();
            if (tel.contains(QStringLiteral("reason")))
                driveReason_ = tel.value(QStringLiteral("reason")).toString();
            const QJsonObject left = tel.value(QStringLiteral("left")).toObject();
            const QJsonObject right = tel.value(QStringLiteral("right")).toObject();
            if (!left.isEmpty()) leftValid_ = left.value(QStringLiteral("valid")).toBool();
            if (!right.isEmpty()) rightValid_ = right.value(QStringLiteral("valid")).toBool();
        }
        if (data.contains(QStringLiteral("laserConnected")))
            laserConnected_ = data.value(QStringLiteral("laserConnected")).toBool();
        if (data.contains(QStringLiteral("imuConnected")))
            imuConnected_ = data.value(QStringLiteral("imuConnected")).toBool();
        if (data.contains(QStringLiteral("clampConnected")))
            clampConnected_ = data.value(QStringLiteral("clampConnected")).toBool();
        if (data.contains(QStringLiteral("usbConnected"))) {
            const QJsonArray usb = data.value(QStringLiteral("usbConnected")).toArray();
            for (int i = 0; i < usb.size() && i < 2; ++i)
                camConnected_[i] = usb.at(i).toBool();
        }
    }

    void buildSteps() {
        steps_.clear();

        if (opt_.connectDrive) {
            QFile ini(opt_.iniPath);
            if (!ini.open(QIODevice::ReadOnly)) {
                out_ << QStringLiteral("[WARN] cannot read ini: %1\n").arg(opt_.iniPath);
            } else {
                const QByteArray raw = ini.readAll();
                ini.close();
                QJsonObject settings;
                settings.insert(QStringLiteral("schema"), PA1664::kSettingsSchemaVersion);
                settings.insert(QStringLiteral("iniBase64"),
                                QString::fromLatin1(raw.toBase64()));
                steps_.append({QStringLiteral("robot.connect"),
                               QJsonObject{{QStringLiteral("settings"), settings}},
                               2500, QStringLiteral("robot.connect (apply drive_settings.ini)")});
            }
        }

        if (opt_.connectLaser) {
            steps_.append({QStringLiteral("robot.laser.scan"), QJsonObject(), 1500,
                           QStringLiteral("robot.laser.scan")});
            steps_.append({QStringLiteral("robot.laser.connect"),
                           QJsonObject{{QStringLiteral("serialNumber"), opt_.laserSerial}},
                           3000, QStringLiteral("robot.laser.connect %1").arg(opt_.laserSerial)});
        }

        if (opt_.connectCameras) {
            steps_.append({QStringLiteral("robot.camera.scan"), QJsonObject(), 1500,
                           QStringLiteral("robot.camera.scan")});
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            const QStringList usbIndices = opt_.usbCameraIndices.split(
                QLatin1Char(','), Qt::SkipEmptyParts);
#else
            const QStringList usbIndices = opt_.usbCameraIndices.split(
                QLatin1Char(','), QString::SkipEmptyParts);
#endif
            for (int slot = 0; slot < 2; ++slot) {
                QJsonObject data;
                if (!usbIndices.isEmpty()) {
                    const bool hasIndex = slot < usbIndices.size();
                    bool parsed = false;
                    const int deviceIndex = hasIndex
                                                ? usbIndices.at(slot).trimmed().toInt(&parsed)
                                                : -1;
                    if (!parsed || deviceIndex < 0) {
                        out_ << QStringLiteral(
                                    "[WARN] --camera-usb missing device index for slot %1, skipped\n")
                                    .arg(slot);
                        continue;
                    }
                    data.insert(QStringLiteral("slot"), slot);
                    data.insert(QStringLiteral("source"), QStringLiteral("usb"));
                    data.insert(QStringLiteral("deviceIndex"), deviceIndex);
                    data.insert(QStringLiteral("fps"), 15);
                    data.insert(QStringLiteral("flipHorizontal"), false);
                    data.insert(QStringLiteral("flipVertical"), slot == 0);
                } else if (!cameraSettings(slot, &data)) {
                    out_ << QStringLiteral(
                                "[WARN] camera %1 has no usable source in the ini, skipped\n")
                                .arg(slot);
                    continue;
                }
                steps_.append({QStringLiteral("robot.camera.connect"), data, 1500,
                               QStringLiteral("robot.camera.connect slot=%1").arg(slot)});
            }
        }

        if (opt_.connectImu) {
            QSettings ini(opt_.iniPath, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            ini.setIniCodec("UTF-8");
#endif
            const QString port = ini.value(QStringLiteral("drive/imuSerialPort"))
                                     .toString().trimmed();
            if (port.isEmpty()) {
                out_ << QStringLiteral("[WARN] ini has no imuSerialPort, IMU skipped\n");
            } else {
                imuAttempted_ = true;
                QJsonObject data{{QStringLiteral("port"), port},
                                 {QStringLiteral("baudRate"),
                                  ini.value(QStringLiteral("drive/imuBaudRate"), 9600).toInt()},
                                 {QStringLiteral("outputDivider"),
                                  ini.value(QStringLiteral("drive/imuOutputDivider"), 10).toInt()}};
                steps_.append({QStringLiteral("robot.imu.connect"), data, 2500,
                               QStringLiteral("robot.imu.connect %1").arg(port)});
            }
        }

        steps_.append({QStringLiteral("robot.snapshot"), QJsonObject(), 300,
                       QStringLiteral("robot.snapshot")});
        steps_.append({QStringLiteral("robot.peripherals.snapshot"), QJsonObject(), 0,
                       QStringLiteral("robot.peripherals.snapshot")});

        if (opt_.enableDriveTest) {
            // Nothing here commands a wheel speed: this probe only asks the host
            // to arm the drive and then watches the feedback watchdog verdict.
            steps_.append({QStringLiteral("robot.motion.stop"), QJsonObject(), 200,
                           QStringLiteral("robot.motion.stop (pre-enable)")});
            steps_.append({QStringLiteral("robot.enable"),
                           QJsonObject{{QStringLiteral("enabled"), true}}, 3000,
                           QStringLiteral("robot.enable enabled=1 (feedback watchdog probe)")});
            steps_.append({QStringLiteral("robot.snapshot"), QJsonObject(), 200,
                           QStringLiteral("robot.snapshot (while enabled)")});
            steps_.append({QStringLiteral("robot.enable"),
                           QJsonObject{{QStringLiteral("enabled"), false}}, 600,
                           QStringLiteral("robot.enable enabled=0")});
            steps_.append({QStringLiteral("robot.motion.stop"), QJsonObject(), 200,
                           QStringLiteral("robot.motion.stop (post-enable)")});
        }

        if (opt_.clampProbe) {
            steps_.append({QStringLiteral("robot.clamp.status"), QJsonObject(), 400,
                           QStringLiteral("robot.clamp.status (probe start)")});
        }

        stepIndex_ = 0;
    }

    // Expanded once the host reports its clamp layout, so only configured
    // motors are touched.
    void scheduleClampSteps(const QJsonObject& statusData) {
        if (clampStepsScheduled_) return;
        clampStepsScheduled_ = true;
        clampConnected_ = statusData.value(QStringLiteral("clampConnected")).toBool();
        QVector<Step> probe;
        if (!clampConnected_ && !clampConnectRequested_) {
            // The host reports every motor as "connected" only after the clamp
            // service opens its buses; probe the real layout afterwards.
            clampConnectRequested_ = true;
            probe.append({QStringLiteral("robot.clamp.connect"), QJsonObject(), 2500,
                          QStringLiteral("robot.clamp.connect (was disconnected)")});
            probe.append({QStringLiteral("robot.clamp.status"), QJsonObject(), 400,
                          QStringLiteral("robot.clamp.status (after connect)")});
        }
        // Probe all three axes; axes without configured motors answer
        // invalid_argument, which is itself the layout report.
        const int counts[3] = {2, 2, 2};
        for (int axis = 0; axis < 3; ++axis) {
            for (int index = 0; index < counts[axis]; ++index) {
                const QString label = QStringLiteral("axis %1 motor %2")
                                          .arg(axis).arg(index);
                probe.append({QStringLiteral("robot.clamp.requestPosition"),
                              QJsonObject{{QStringLiteral("axis"), axis},
                                          {QStringLiteral("index"), index}},
                              250,
                              QStringLiteral("robot.clamp.requestPosition %1").arg(label)});
                probe.append({QStringLiteral("robot.clamp.setMotorSpeed"),
                              QJsonObject{{QStringLiteral("axis"), axis},
                                          {QStringLiteral("index"), index},
                                          {QStringLiteral("percent"),
                                           opt_.clampMove ? 6 : 0}},
                              opt_.clampMove ? 300 : 200,
                              QStringLiteral("robot.clamp.setMotorSpeed %1 percent=%2")
                                  .arg(label).arg(opt_.clampMove ? 6 : 0)});
                probe.append({QStringLiteral("robot.clamp.stopMotor"),
                              QJsonObject{{QStringLiteral("axis"), axis},
                                          {QStringLiteral("index"), index}},
                              200,
                              QStringLiteral("robot.clamp.stopMotor %1").arg(label)});
            }
        }
        probe.append({QStringLiteral("robot.clamp.positions"), QJsonObject(), 200,
                      QStringLiteral("robot.clamp.positions (probe end)")});
        for (int i = 0; i < probe.size(); ++i)
            steps_.insert(stepIndex_ + i, probe.at(i));
    }

    bool cameraSettings(int slot, QJsonObject* data) {
        QSettings ini(opt_.iniPath, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        ini.setIniCodec("UTF-8");
#endif
        const QString suffix = QString::number(slot);
        const QString source = ini.value(QStringLiteral("drive/cameraSource") + suffix,
                                         QStringLiteral("usb")).toString();
        const bool flipH = ini.value(QStringLiteral("drive/usbCameraFlipHorizontal") + suffix,
                                     false).toBool();
        const bool flipV = ini.value(QStringLiteral("drive/usbCameraFlipVertical") + suffix,
                                     false).toBool();
        QJsonObject out;
        out.insert(QStringLiteral("slot"), slot);
        out.insert(QStringLiteral("flipHorizontal"), flipH);
        out.insert(QStringLiteral("flipVertical"), flipV);
        if (source.compare(QStringLiteral("network"), Qt::CaseInsensitive) == 0) {
            const QString url = ini.value(QStringLiteral("drive/networkCameraUrl") + suffix)
                                    .toString().trimmed();
            if (url.isEmpty()) return false;
            out.insert(QStringLiteral("source"), QStringLiteral("network"));
            out.insert(QStringLiteral("url"), url);
        } else {
            const int index = ini.value(QStringLiteral("drive/usbCameraDeviceIndex") + suffix,
                                        -1).toInt();
            if (index < 0) return false;
            out.insert(QStringLiteral("source"), QStringLiteral("usb"));
            out.insert(QStringLiteral("deviceIndex"), index);
            out.insert(QStringLiteral("fps"),
                       ini.value(QStringLiteral("drive/usbCameraFps") + suffix, 15).toInt());
        }
        *data = out;
        return true;
    }

    void runNextStep() {
        if (stepIndex_ >= steps_.size()) {
            startHold();
            return;
        }
        const Step step = steps_.at(stepIndex_++);
        out_ << "\n[" << stepIndex_ << "/" << steps_.size() << "] " << step.label << "\n";
        stepDelayMs_ = step.delayAfterMs;
        sendRequest(step.operation, step.data, step.label);
    }

    void sendRequest(const QString& operation, const QJsonObject& data,
                     const QString& label) {
        if (sessionId_ == 0 || sock_->state() != QTcpSocket::ConnectedState) return;

        QJsonObject request;
        request.insert(QStringLiteral("operation"), operation);
        if (!data.isEmpty()) request.insert(QStringLiteral("data"), data);

        PA1664::Frame frame;
        frame.type = PA1664::MessageType::Request;
        frame.sessionId = sessionId_;
        frame.sequence = seq_++;
        frame.requestId = requestId_++;
        frame.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        frame.payload = QJsonDocument(request).toJson(QJsonDocument::Compact);

        labels_.insert(frame.requestId, label);
        ops_.insert(frame.requestId, operation);
        sentMs_.insert(frame.requestId, QDateTime::currentMSecsSinceEpoch());
        currentRequestId_ = frame.requestId;
        awaitingResponse_ = true;

        fullLog(QStringLiteral("REQUEST requestId=%1 operation=%2 payload=%3")
                    .arg(frame.requestId)
                    .arg(operation,
                         QString::fromUtf8(
                             QJsonDocument(request).toJson(QJsonDocument::Compact))));

        sock_->write(PA1664::encode(frame));
        sock_->flush();
        log(QString("Sent %1 requestId=%2").arg(operation).arg(frame.requestId));
    }

    void startHold() {
        if (inHold_) return;
        inHold_ = true;
        if (opt_.holdSeconds <= 0) {
            finish();
            return;
        }
        out_ << QStringLiteral("\n===== holding the session for %1s "
                               "(peripheral snapshot every 10s) =====\n")
                    .arg(opt_.holdSeconds);
        holdStartFrames_ = laserFrames_;
        holdTimer_.start();
        pollTimer_.start();

        QTimer::singleShot(opt_.holdSeconds * 1000, this, [this]() {
            holdElapsedMs_ = holdTimer_.elapsed();
            pollTimer_.stop();
            finish();
        });
    }

    void finish() {
        heartbeatTimer_.stop();
        pollTimer_.stop();

        if (holdElapsedMs_ > 0) {
            const double secs = holdElapsedMs_ / 1000.0;
            const int frames = laserFrames_ - holdStartFrames_;
            out_ << QStringLiteral("\nlaser frame rate ~ %1 fps (%2s, %3 frames, %4 points/frame)\n")
                        .arg(secs > 0.5 ? frames / secs : 0.0, 0, 'f', 1)
                        .arg(secs, 0, 'f', 0)
                        .arg(frames)
                        .arg(laserPoints_);
        }

        printSummary();
        sock_->disconnectFromHost();
        emit finishedNow();
    }

    QString summaryLine(const QJsonObject& data) {
        QStringList parts;
        const QJsonObject telemetry = data.value(QStringLiteral("telemetry")).toObject();
        if (data.contains(QStringLiteral("driveConnected"))) {
            parts << QStringLiteral("drive=%1")
                         .arg(data.value(QStringLiteral("driveConnected")).toBool());
        }
        if (telemetry.contains(QStringLiteral("stateKey"))) {
            parts << QStringLiteral("state=%1")
                         .arg(telemetry.value(QStringLiteral("stateKey")).toString());
        }
        if (telemetry.contains(QStringLiteral("left"))) {
            const QJsonObject left = telemetry.value(QStringLiteral("left")).toObject();
            const QJsonObject right = telemetry.value(QStringLiteral("right")).toObject();
            parts << QStringLiteral("left(valid=%1,temp=%2)")
                         .arg(left.value(QStringLiteral("valid")).toBool())
                         .arg(left.value(QStringLiteral("temperatureC")).toInt());
            parts << QStringLiteral("right(valid=%1,temp=%2)")
                         .arg(right.value(QStringLiteral("valid")).toBool())
                         .arg(right.value(QStringLiteral("temperatureC")).toInt());
        }
        if (data.contains(QStringLiteral("laserConnected"))) {
            parts << QStringLiteral("laser=%1")
                         .arg(data.value(QStringLiteral("laserConnected")).toBool());
        }
        if (data.contains(QStringLiteral("imuConnected"))) {
            parts << QStringLiteral("imu=%1")
                         .arg(data.value(QStringLiteral("imuConnected")).toBool());
        }
        if (data.contains(QStringLiteral("clampConnected"))) {
            parts << QStringLiteral("clamp=%1")
                         .arg(data.value(QStringLiteral("clampConnected")).toBool());
        }
        if (data.contains(QStringLiteral("usbConnected"))) {
            parts << QStringLiteral("usb=%1")
                         .arg(QString::fromUtf8(
                             QJsonDocument(data.value(QStringLiteral("usbConnected")).toArray())
                                 .toJson(QJsonDocument::Compact)));
        }
        if (parts.isEmpty())
            return compactBody(QJsonObject{{QStringLiteral("data"), data}});
        return parts.join(QStringLiteral("  "));
    }

    QString compactBody(const QJsonObject& body) {
        QJsonObject data = body.value(QStringLiteral("data")).toObject();
        QString text = QString::fromUtf8(
            QJsonDocument(data).toJson(QJsonDocument::Compact));
        if (data.isEmpty())
            text = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
        if (text.size() > 500) text = text.left(500) + QStringLiteral("...");
        return text;
    }

    QString compactEvent(const QJsonObject& ev) {
        QJsonObject copy = ev;
        copy.remove(QStringLiteral("event"));
        QString text = QString::fromUtf8(
            QJsonDocument(copy).toJson(QJsonDocument::Compact));
        if (text.size() > 300) text = text.left(300) + QStringLiteral("...");
        return text;
    }

    void printSummary() {
        if (summaryPrinted_) return;
        summaryPrinted_ = true;
        log("========== Summary ==========");

        out_ << "\n========================================\n";
        out_ << "Test Summary\n";
        out_ << "========================================\n\n";

        auto r = [this](const QString& name, bool ok) {
            out_ << QString("%1 %2\n").arg(name.leftJustified(30, ' '))
                        .arg(ok ? QStringLiteral("[ OK ] Pass") : QStringLiteral("[FAIL] Fail"));
            log(QString("%1: %2").arg(name).arg(ok ? "pass" : "fail"));
        };

        r("TCP Connection", results_.connected);
        r("PA1664 Hello Handshake", results_.helloAck);
        r("Responses received", results_.responses > 0);

        out_ << QStringLiteral("\nPeripheral verdict\n");
        out_ << QStringLiteral("-----------------------------------\n");
        if (sessionBlocked_) {
            out_ << QStringLiteral("[NOTE] 主机会话被另一个操作台占用（not_session_owner），"
                                   "业务请求全部被拒；先退出操作台再测。\n");
        }
        auto peripheral = [this](const QString& name, bool ok, const QString& detail) {
            out_ << QStringLiteral("%1 %2  %3\n")
                        .arg(name.leftJustified(22, ' '))
                        .arg(ok ? QStringLiteral("[ OK ] connected")
                                : QStringLiteral("[FAIL] not connected"))
                        .arg(detail.left(140));
            log(QStringLiteral("peripheral %1 = %2 (%3)")
                    .arg(name.leftJustified(22, ' '),
                         ok ? QStringLiteral("connected") : QStringLiteral("failed"),
                         detail.left(200)));
        };
        QString driveDetail =
            QStringLiteral("state=%1 feedback_fresh=%2 left_valid=%3 right_valid=%4")
                .arg(driveState_)
                .arg(driveFeedbackFresh_ ? 1 : 0)
                .arg(leftValid_ ? 1 : 0)
                .arg(rightValid_ ? 1 : 0);
        if (!driveReason_.isEmpty())
            driveDetail += QStringLiteral(" reason=%1").arg(driveReason_.left(70));
        if (opt_.enableDriveTest) {
            driveDetail += QStringLiteral("  enable_accepted=%1%2")
                               .arg(driveEnableAccepted_)
                               .arg(driveEnableRejected_.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" rejected=%1")
                                              .arg(driveEnableRejected_.left(60)));
        }
        if (sessionBlocked_)
            driveDetail = QStringLiteral("not tested (session owned by operator console)");
        peripheral(QStringLiteral("drive motors"), driveConnected_ && !sessionBlocked_,
                   driveDetail);
        peripheral(QStringLiteral("line laser"), laserConnected_, laserMessage_);
        peripheral(QStringLiteral("camera 1"), camConnected_[0], camMessage_[0]);
        peripheral(QStringLiteral("camera 2"), camConnected_[1], camMessage_[1]);
        peripheral(QStringLiteral("IMU"),
                   imuConnected_,
                   imuAttempted_ ? QStringLiteral("-")
                                 : QStringLiteral("not attempted (use --imu)"));
        QString clampDetail;
        if (!opt_.clampProbe) {
            clampDetail = QStringLiteral("not attempted (use --clamp)");
        } else if (sessionBlocked_) {
            clampDetail = QStringLiteral("not tested (session owned by operator console)");
        } else {
            clampDetail = QStringLiteral("connected=%1 rejected=%2 peripheral_log=%3")
                              .arg(clampConnected_)
                              .arg(clampFailures_.isEmpty()
                                       ? QStringLiteral("0")
                                       : clampFailures_.join(QStringLiteral(" | ")).left(90))
                              .arg(peripheralLogLines_);
        }
        peripheral(QStringLiteral("clamp motors"),
                   clampConnected_ && !sessionBlocked_, clampDetail);

        out_ << QStringLiteral("\nResponses: %1  Laser frames: %2\n")
                    .arg(results_.responses).arg(laserFrames_);
        if (results_.connected)
            out_ << QStringLiteral("Connection time: %1ms\n").arg(results_.connMs);
        out_ << QStringLiteral("\nLogs: robot_comm_test.log, robot_comm_test_full.log\n\n");
        log("========== Test End ==========");
    }

    void log(const QString& msg) {
        const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        if (logStream_.device())
            logStream_ << QString("[%1] %2\n").arg(ts).arg(msg);
        logStream_.flush();
    }

    void fullLog(const QString& msg) {
        if (!fullLogStream_.device()) return;
        const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        fullLogStream_ << QString("[%1] %2\n").arg(ts, msg);
        fullLogStream_.flush();
    }

private:
    Options opt_;
    QTcpSocket* sock_ = nullptr;
    QTextStream out_;
    QFile logFile_;
    QTextStream logStream_;
    QFile fullLogFile_;
    QTextStream fullLogStream_;
    QHash<quint64, qint64> sentMs_;
    QElapsedTimer connTimer_, reqTimer_, holdTimer_;
    QByteArray rxBuf_;
    quint64 seq_ = 1;

    QTimer heartbeatTimer_;
    QTimer pollTimer_;
    quint64 sessionId_ = 0;
    quint64 requestId_ = 1;
    bool summaryPrinted_ = false;

    QVector<Step> steps_;
    QHash<quint64, QString> labels_;
    QHash<quint64, QString> ops_;
    int stepIndex_ = 0;
    int stepDelayMs_ = 0;
    quint64 currentRequestId_ = 0;
    bool awaitingResponse_ = false;
    bool inHold_ = false;

    int laserFrames_ = 0;
    int laserPoints_ = 0;
    int holdStartFrames_ = 0;
    qint64 holdElapsedMs_ = 0;

    // Latest known peripheral state, used for the final verdict table.
    bool driveConnected_ = false;
    QString driveState_;
    QString driveReason_;
    bool leftValid_ = false;
    bool rightValid_ = false;
    bool laserConnected_ = false;
    QString laserMessage_;
    bool camConnected_[2] = {false, false};
    QString camMessage_[2];
    bool imuConnected_ = false;
    bool imuAttempted_ = false;
    bool clampConnected_ = false;
    bool clampStepsScheduled_ = false;
    bool clampConnectRequested_ = false;
    bool driveFeedbackFresh_ = false;
    QStringList clampFailures_;
    int peripheralLogLines_ = 0;
    int clampReplyLines_ = 0;
    bool sessionBlocked_ = false;
    int driveEnableAccepted_ = 0;
    QString driveEnableRejected_;

    struct {
        bool connected = false;
        bool helloAck = false;
        int responses = 0;
        int connMs = 0;
        int passed = 0;
    } results_;
};

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // Host snapshot text is UTF-8; match the console so Chinese renders.
    SetConsoleOutputCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();

    QCommandLineOption hostOpt(QStringList() << "H" << "host", "Robot IP", "host",
                               "192.168.1.20");
    QCommandLineOption portOpt(QStringList() << "p" << "port", "Control port", "port",
                               "43100");
    QCommandLineOption iniOpt(QStringList() << "i" << "ini",
                              "drive_settings.ini to apply via robot.connect", "path");
    QCommandLineOption laserOpt(QStringList() << "l" << "laser",
                                "Laser serial number to connect", "serial");
    QCommandLineOption holdOpt(QStringList() << "t" << "hold",
                               "Keep the control session open for N seconds", "seconds",
                               "0");
    QCommandLineOption noDriveOpt("no-drive", "Skip robot.connect (settings upload)");
    QCommandLineOption noLaserOpt("no-laser", "Skip laser scan/connect");
    QCommandLineOption noCameraOpt("no-camera", "Skip camera connect");
    QCommandLineOption usbCamOpt("camera-usb",
                                 "Use USB cameras instead of the ini source, "
                                 "device indices per slot, e.g. 0,1", "indices");
    QCommandLineOption imuOpt("imu", "Also connect the IMU described by the ini");
    QCommandLineOption enableOpt(
        "enable",
        "Enable the drive, watch feedback/watchdog events for 3s, then disable");
    QCommandLineOption clampOpt(
        "clamp",
        "Probe clamp motors: status, per-motor position query, zero-speed write");
    QCommandLineOption clampMoveOpt(
        "clamp-move",
        "Additionally pulse every clamp motor at 6% for 300ms (moves hardware)");
    QCommandLineOption verboseOpt(QStringList() << "v" << "verbose",
                                  "Print full JSON payloads on the console");
    parser.addOption(hostOpt);
    parser.addOption(portOpt);
    parser.addOption(iniOpt);
    parser.addOption(laserOpt);
    parser.addOption(holdOpt);
    parser.addOption(noDriveOpt);
    parser.addOption(noLaserOpt);
    parser.addOption(noCameraOpt);
    parser.addOption(usbCamOpt);
    parser.addOption(imuOpt);
    parser.addOption(enableOpt);
    parser.addOption(clampOpt);
    parser.addOption(clampMoveOpt);
    parser.addOption(verboseOpt);
    parser.process(app);

    RobotCommTester::Options options;
    options.host = parser.value(hostOpt);
    options.port = parser.value(portOpt).toUShort();
    options.iniPath = parser.value(iniOpt);
    options.laserSerial = parser.value(laserOpt);
    options.holdSeconds = parser.value(holdOpt).toInt();
    options.usbCameraIndices = parser.value(usbCamOpt);
    options.connectImu = parser.isSet(imuOpt) && !options.iniPath.isEmpty();
    options.verbose = parser.isSet(verboseOpt);
    options.connectDrive = !parser.isSet(noDriveOpt) && !options.iniPath.isEmpty();
    options.connectLaser = !parser.isSet(noLaserOpt) && !options.laserSerial.isEmpty();
    options.connectCameras = !parser.isSet(noCameraOpt)
                             && (!options.iniPath.isEmpty()
                                 || !options.usbCameraIndices.isEmpty());
    options.enableDriveTest = parser.isSet(enableOpt);
    options.clampProbe = parser.isSet(clampOpt) || parser.isSet(clampMoveOpt);
    options.clampMove = parser.isSet(clampMoveOpt);

    RobotCommTester tester(options);
    QObject::connect(&tester, &RobotCommTester::finishedNow, &app,
                     &QCoreApplication::quit);
    QTimer::singleShot(100, &tester, &RobotCommTester::start);

    return app.exec();
}

#include "main.moc"
