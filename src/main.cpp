#include "mainwindow.h"
#include "devicecontroller.h"
#include "parameterpanel.h"
#include "robot_app_logger.h"
#include "client.h"
#include "usbbootstrap.h"
#include "services/remote_workbench_session.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QUrl>
#include <QSet>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>

namespace {
void prepareSessionLogs()
{
    const QDir logDirectory(QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("logs")));
    if (!logDirectory.exists())
        return;

    // The ultrasound SDK creates one log_YYYYMMDD_HHMMSS.txt per run and never
    // cleans up after itself. Remove the previous runs' files so the directory
    // never accumulates more than the current session's single file.
    const int removed = crawling::AppLogger::removeOldSessionLogs(
        logDirectory.absolutePath(), QStringLiteral("^log_\\d{8}_\\d{6}\\.txt$"),
        0);
    if (removed > 0)
        qInfo() << "SESSION_LOG_CLEANUP: removed" << removed
                << "old ultrasound log files";

    // Keep the tail of the previous run instead of wiping it: an operator often
    // restarts right after a failure and still needs that context. AppLogger
    // enforces the same per-file line budget while the session is running.
    struct RetainedLog {
        QString name;
        int lines;
    };
    const RetainedLog retainedLogs[] = {
        {QStringLiteral("robot_console.log"),
         crawling::AppLogger::Retention::Console},
        {QStringLiteral("camera_connection.log"),
         crawling::AppLogger::Retention::Camera},
        {QStringLiteral("profile_weld_detection.log"),
         crawling::AppLogger::Retention::Profile},
        {QStringLiteral("errors.log"),
         crawling::AppLogger::Retention::Errors}};
    for (const RetainedLog &entry : retainedLogs) {
        const QString path = logDirectory.filePath(entry.name);
        if (!QFileInfo::exists(path))
            continue;
        if (!crawling::AppLogger::trimToLastLines(path, entry.lines))
            qWarning() << "SESSION_LOG_CLEANUP: failed" << path;
    }
    qInfo().noquote() << "CAMERA_CONNECTION_LOG:"
                      << logDirectory.filePath(QStringLiteral("camera_connection.log"));
}

void pruneInactiveServiceGroups()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QFile config(QDir(appDir).filePath(QStringLiteral("default_config.json")));
    if (!config.open(QIODevice::ReadOnly)) {
        qWarning() << "SERVICE_GROUP_PRUNE: cannot read default_config.json";
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(config.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "SERVICE_GROUP_PRUNE: invalid default_config.json" << error.errorString();
        return;
    }

    QSet<int> desiredGroups;
    const QJsonObject groups = document.object().value(QStringLiteral("groups")).toObject();
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (ok)
            desiredGroups.insert(id);
    }
    if (desiredGroups.isEmpty()) {
        qWarning() << "SERVICE_GROUP_PRUNE: refusing to prune for an empty group configuration";
        return;
    }

    QDir serviceRoot(QDir(appDir).filePath(
        QStringLiteral("configs/ultrasound_device_0")));
    if (!serviceRoot.exists())
        return;
    const QRegularExpression groupPattern(QStringLiteral("^group(\\d+)$"));
    const QString canonicalRoot = QFileInfo(serviceRoot.absolutePath()).canonicalFilePath();
    for (const QString &entry : serviceRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto match = groupPattern.match(entry);
        if (!match.hasMatch())
            continue;
        const int id = match.captured(1).toInt();
        if (desiredGroups.contains(id))
            continue;
        QDir stale(serviceRoot.filePath(entry));
        const QString stalePath = QFileInfo(stale.absolutePath()).canonicalFilePath();
        if (canonicalRoot.isEmpty() || stalePath.isEmpty()
            || !stalePath.startsWith(canonicalRoot + QDir::separator())) {
            qWarning() << "SERVICE_GROUP_PRUNE: rejected unsafe path" << stale.absolutePath();
            continue;
        }
        if (!stale.removeRecursively())
            qWarning() << "SERVICE_GROUP_PRUNE: failed" << stalePath;
        else
            qInfo() << "SERVICE_GROUP_PRUNE: removed stale hardware group" << id;
    }
}

bool recoverBrokenConfigRepository()
{
    const QDir configsDir(QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("configs")));
    if (!configsDir.exists())
        return false;

    const QRegularExpression devicePattern(QStringLiteral("^device_(\\d+)$"));
    for (const QString &entry : configsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!devicePattern.match(entry).hasMatch())
            continue;
        QFile file(configsDir.filePath(entry + QStringLiteral("/prefs.json")));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject())
            continue;

        QString ultrasoundConfig;
        for (const QJsonValue &pref : doc.object().value(QStringLiteral("prefs")).toArray()) {
            const QJsonObject object = pref.toObject();
            if (object.value(QStringLiteral("name")).toString()
                == QStringLiteral("ultrasound_config")) {
                ultrasoundConfig = object.value(QStringLiteral("value")).toString();
                break;
            }
        }
        if (ultrasoundConfig.isEmpty() || configsDir.exists(ultrasoundConfig))
            continue;

        const QString backupPath = configsDir.absolutePath()
            + QStringLiteral(".recovered_")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        qWarning() << "CONFIG_REPO_BROKEN: device" << entry
                   << "references missing" << ultrasoundConfig
                   << "- moving repository to" << backupPath;
        QDir().rename(configsDir.absolutePath(), backupPath);
        return true;
    }
    return false;
}

struct ChannelHardwareTestState {
    int originalGroup = -1;
    int targetGroup = -1;
    int originalTx = -1;
    int originalRx = -1;
    bool writePassed = false;
};

bool isLocalTestMode(const QStringList &arguments)
{
    const QStringList testOptions{
        QStringLiteral("--hardware-test"),
        QStringLiteral("--reconnect-test"),
        QStringLiteral("--channel-hardware-test"),
        QStringLiteral("--channel-ui-test"),
        QStringLiteral("--apply-test")};
    for (const QString &option : testOptions) {
        if (arguments.contains(option)) return true;
    }
    return false;
}

QString configuredRemoteEndpoint(const QStringList &arguments)
{
    QString endpoint;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) == QStringLiteral("--remote")) {
            if (i + 1 < arguments.size()) endpoint = arguments.at(i + 1);
            break;
        }
        if (arguments.at(i).startsWith(QStringLiteral("--remote="))) {
            endpoint = arguments.at(i).mid(9);
            break;
        }
    }
    if (endpoint.isEmpty()) endpoint = qEnvironmentVariable("PA1664_REMOTE_HOST");
    if (endpoint.isEmpty()) {
        QSettings settings;
        endpoint = settings.value(QStringLiteral("remote/endpoint")).toString();
    }
    if (endpoint.isEmpty()) endpoint = QStringLiteral("127.0.0.1:43100");
    return endpoint;
}

bool parseRemoteEndpoint(const QString &text, QString *host, quint16 *port)
{
    if (!host || !port) return false;
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) return false;
    if (!normalized.contains(QStringLiteral("://")))
        normalized.prepend(QStringLiteral("tcp://"));
    const QUrl url(normalized);
    if (!url.isValid() || url.host().isEmpty()) return false;
    const int parsedPort = url.port(int(pa1664::remote::kDefaultControlPort));
    if (parsedPort <= 0 || parsedPort > 65535) return false;
    *host = url.host();
    *port = static_cast<quint16>(parsedPort);
    return true;
}

struct AppLoggerShutdownGuard {
    ~AppLoggerShutdownGuard() { crawling::AppLogger::shutdown(); }
};

// Operator-side crash/exit evidence: without this the app can vanish (terminal
// closed, unhandled exception, Qt fatal) and the log simply stops mid-sentence.
void installDiagnosticHooks(QApplication *app)
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context,
                              const QString &message) {
        const char *level = "INFO";
        switch (type) {
        case QtDebugMsg: level = "DEBUG"; break;
        case QtInfoMsg: level = "INFO"; break;
        case QtWarningMsg: level = "WARN"; break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg: level = "FATAL"; break;
        }
        const QString line = QStringLiteral("[%1] %2 (%3:%4)")
                                 .arg(QString::fromLatin1(level), message,
                                      QString::fromLatin1(context.file ? context.file : ""))
                                 .arg(context.line);
        std::fprintf(stderr, "%s\n", line.toUtf8().constData());
        std::fflush(stderr);
        crawling::AppLogger::write(QStringLiteral("QT.MESSAGE"), line);
        if (type == QtFatalMsg) std::abort();
    });
    std::set_terminate([] {
        crawling::AppLogger::write(
            QStringLiteral("SESSION"),
            QStringLiteral("event=terminate reason=unhandled_exception"));
        crawling::AppLogger::shutdown();
        std::abort();
    });
    QObject::connect(app, &QCoreApplication::aboutToQuit, [] {
        crawling::AppLogger::write(
            QStringLiteral("SESSION"),
            QStringLiteral("event=app_quit reason=about_to_quit"));
    });
}

// Workbench 的界面按浅色设计（见 MainWindow::buildUi 的样式表）。Windows 处于
// 深色模式时 Qt 6.8 会跟随系统给出深色调色板：只有被样式表显式指定背景色的
// 控件才变浅，其余容器、页签、输入框仍按深色绘制，输入框会出现深底 + 深字或
// 白底 + 白字，参数值完全看不见。这里把应用配色固定为浅色，界面不再随系统
// 主题漂移。
void applyLightColorScheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    QPalette palette = QGuiApplication::palette();
    palette.setColor(QPalette::Window, QColor(0xf4, 0xf8, 0xfc));
    palette.setColor(QPalette::WindowText, QColor(0x1f, 0x29, 0x37));
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::AlternateBase, QColor(0xf2, 0xf6, 0xfa));
    palette.setColor(QPalette::Text, QColor(0x1f, 0x29, 0x37));
    palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x94, 0xa0));
    palette.setColor(QPalette::Button, QColor(0xf8, 0xfa, 0xfc));
    palette.setColor(QPalette::ButtonText, QColor(0x1f, 0x29, 0x37));
    palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::ToolTipText, QColor(0x1f, 0x29, 0x37));
    palette.setColor(QPalette::Highlight, QColor(0x16, 0x77, 0xff));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    QGuiApplication::setPalette(palette);
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyLightColorScheme();
    AppLoggerShutdownGuard loggerShutdownGuard;
    // Windows machines in the field may have a system HTTP proxy configured
    // (Clash/VPN tools, corporate policy). Qt would then try to tunnel our raw
    // TCP control link through that proxy and fail with "The proxy type is
    // invalid for this operation". This application only talks to the robot on
    // the local network, so never use a proxy.
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    QCoreApplication::setOrganizationName("PA1664");
    QCoreApplication::setApplicationName("PA1664Workbench");
    QDir::setCurrent(QCoreApplication::applicationDirPath());
    installDiagnosticHooks(&app);
    crawling::AppLogger::write(
        QStringLiteral("SESSION"),
        QStringLiteral("event=app_start pid=%1 exe=\"%2\" args=\"%3\" qt=%4")
            .arg(QCoreApplication::applicationPid())
            .arg(QCoreApplication::applicationFilePath())
            .arg(QCoreApplication::arguments().join(QLatin1Char(' ')))
            .arg(QString::fromLatin1(qVersion())));
    prepareSessionLogs();
    const bool localTestMode = isLocalTestMode(app.arguments());
    const bool localMode = localTestMode || app.arguments().contains("--local");
    QString remoteHost;
    quint16 remotePort = static_cast<quint16>(pa1664::remote::kDefaultControlPort);
    if (!localMode) {
        const QString endpoint = configuredRemoteEndpoint(app.arguments());
        if (!parseRemoteEndpoint(endpoint, &remoteHost, &remotePort)) {
            qCritical().noquote() << "REMOTE_ENDPOINT_INVALID" << endpoint;
            return 2;
        }
        QSettings settings;
        settings.setValue(QStringLiteral("remote/endpoint"),
                          QStringLiteral("%1:%2").arg(remoteHost).arg(remotePort));
        qInfo().noquote() << "REMOTE_MODE" << remoteHost << remotePort;
    } else {
        QString usbPreparationWarning;
        UsbBootstrap::prepare(app, &usbPreparationWarning);
        if (!usbPreparationWarning.isEmpty())
            qWarning().noquote() << "USB_PREPARE:" << usbPreparationWarning;
        pruneInactiveServiceGroups();
        recoverBrokenConfigRepository();
    }
    if (app.arguments().contains("--hardware-test")) {
        DeviceController device;
        QObject::connect(&device, &DeviceController::message, [](const QString &text, bool error) {
            qInfo().noquote() << (error ? "[ERROR]" : "[INFO]") << text;
        });
        QTimer::singleShot(3000, &device, [&device] {
            if (!device.connectDevice("127.0.0.1", 0)) {
                qCritical() << "HARDWARE_TEST: SDK_CONNECT_FAILED";
                QCoreApplication::exit(2);
                return;
            }
            // 模拟用户连接成功后立刻点击开始；控制器应自动排队，不要求人工等待。
            device.start();
        });
        QTimer::singleShot(24000, &device, [&device] {
            const bool passed = device.hardwareOnline() && device.totalFrames() > 0;
            qInfo() << "HARDWARE_TEST:" << (passed ? "PASS" : "FAIL")
                    << "frames=" << device.totalFrames()
                    << "points=" << device.pointCount() << "beams=" << device.beamCount();
            QCoreApplication::exit(passed ? 0 : 3);
        });
        return app.exec();
    }
    if (app.arguments().contains("--reconnect-test")) {
        DeviceController device;
        auto framesBeforeDisconnect = std::make_shared<quint64>(0);
        QObject::connect(&device, &DeviceController::message, [](const QString &text, bool error) {
            qInfo().noquote() << (error ? "[ERROR]" : "[INFO]") << text;
        });
        QTimer::singleShot(1500, &device, [&device] {
            qInfo() << "RECONNECT_TEST: FIRST_CONNECT_AND_START";
            if (!device.connectDevice("127.0.0.1", 0)) {
                qCritical() << "RECONNECT_TEST: FIRST_CONNECT_FAILED";
                QCoreApplication::exit(12);
                return;
            }
            device.start();
        });
        QTimer::singleShot(12000, &device, [&device, framesBeforeDisconnect] {
            if (!device.hardwareOnline() || device.totalFrames() == 0) {
                qCritical() << "RECONNECT_TEST: NO_FRAMES_BEFORE_DISCONNECT";
                QCoreApplication::exit(13);
                return;
            }
            *framesBeforeDisconnect = device.totalFrames();
            qInfo() << "RECONNECT_TEST: SOFT_DISCONNECT frames=" << *framesBeforeDisconnect;
            device.disconnectDevice();
        });
        QTimer::singleShot(13500, &device, [&device] {
            qInfo() << "RECONNECT_TEST: SECOND_CONNECT_AND_START";
            if (!device.connectDevice("127.0.0.1", 0)) {
                qCritical() << "RECONNECT_TEST: SECOND_CONNECT_FAILED";
                QCoreApplication::exit(14);
                return;
            }
            device.start();
        });
        QTimer::singleShot(23000, &device, [&device, framesBeforeDisconnect] {
            const bool passed = device.hardwareOnline()
                                && device.totalFrames() > *framesBeforeDisconnect;
            qInfo() << "RECONNECT_TEST:" << (passed ? "PASS" : "FAIL")
                    << "before=" << *framesBeforeDisconnect
                    << "after=" << device.totalFrames();
            QCoreApplication::exit(passed ? 0 : 15);
        });
        return app.exec();
    }
    if (app.arguments().contains("--channel-hardware-test")) {
        DeviceController device;
        auto state = std::make_shared<ChannelHardwareTestState>();
        QTimer::singleShot(3000, &device, [&device] {
            if (!device.connectDevice("127.0.0.1", 0)) {
                std::fprintf(stdout, "CHANNEL_HW_TEST: SDK_CONNECT_FAILED\n");
                std::fflush(stdout);
                QCoreApplication::exit(7);
            }
        });
        // Connect 后 SDK 会逐组异步下发初始化配置；等待其命令队列稳定再测试。
        QTimer::singleShot(26000, &device, [state] {
            auto &sdk = Client::getInstance();
            const auto groups = sdk.getGroupsNo(false);
            if (groups.isEmpty()) {
                std::fprintf(stdout, "CHANNEL_HW_TEST: NO_GROUP\n");
                std::fflush(stdout);
                QCoreApplication::exit(8);
                return;
            }
            state->originalGroup = sdk.getCurrentGroup();
            state->targetGroup = groups.contains(4) ? 4 : groups.last();
            try {
                if (!sdk.setCurrentGroup(state->targetGroup))
                    throw std::runtime_error("setCurrentGroup returned false");
                state->originalTx = sdk.getTransmissionChannel();
                state->originalRx = sdk.getReceptionChannel();
                const bool txOk = sdk.setTransmissionChannel(7); // 界面通道 7
                const bool rxOk = sdk.setReceptionChannel(2);    // 界面通道 2
                const int txReadBack = sdk.getTransmissionChannel();
                const int rxReadBack = sdk.getReceptionChannel();
                state->writePassed = txOk && rxOk && txReadBack == 7 && rxReadBack == 2;
                std::fprintf(stdout,
                             "CHANNEL_HW_TEST: group=%d txSdk=%d rxSdk=%d write=%s\n",
                             state->targetGroup, txReadBack, rxReadBack,
                             state->writePassed ? "PASS" : "FAIL");
                std::fflush(stdout);
            } catch (const std::exception &e) {
                std::fprintf(stdout, "CHANNEL_HW_TEST: EXCEPTION %s\n", e.what());
                std::fflush(stdout);
                QCoreApplication::exit(9);
            } catch (...) {
                std::fprintf(stdout, "CHANNEL_HW_TEST: UNKNOWN_EXCEPTION\n");
                std::fflush(stdout);
                QCoreApplication::exit(9);
            }
        });
        QTimer::singleShot(33000, &device, [state] {
            if (state->originalTx < 0 || state->originalRx < 0) return;
            bool restored = false;
            try {
                auto &sdk = Client::getInstance();
                sdk.setTransmissionChannel(state->originalTx);
                sdk.setReceptionChannel(state->originalRx);
                restored = sdk.getTransmissionChannel() == state->originalTx
                           && sdk.getReceptionChannel() == state->originalRx;
                if (state->originalGroup >= 1)
                    sdk.setCurrentGroup(state->originalGroup);
            } catch (...) {
                restored = false;
            }
            std::fprintf(stdout, "CHANNEL_HW_TEST: restore=%s\n", restored ? "PASS" : "FAIL");
            std::fflush(stdout);
            QCoreApplication::exit(state->writePassed && restored ? 0 : 10);
        });
        return app.exec();
    }
    app.setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei UI"; font-size: 13px; }
        QPushButton { min-height: 30px; padding: 2px 10px; border: 1px solid #b9c5d0; border-radius: 5px; background: #f8fafc; color: #1f2937; }
        QPushButton:hover { background: #eaf4ff; border-color: #2684c7; }
        QPushButton:disabled { color: #9aa5af; background: #eef1f4; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { min-height: 28px; border: 1px solid #b9c5d0; border-radius: 4px; background: white; color: #1f2937; }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background: #eef1f4; color: #6b7683; border-color: #d5dde5; }
        QComboBox QAbstractItemView { background: white; color: #1f2937; selection-background-color: #1677ff; selection-color: white; }
        QTabWidget::pane { border: 1px solid #cbd5df; background: white; }
        QTabBar::tab { padding: 8px 14px; background: #eef2f6; color: #33475b; }
        QTabBar::tab:selected { background: white; color: #087dcc; font-weight: 600; }
        QScrollArea { background: white; }
        QScrollArea > QWidget > QWidget { background: white; }
    )");
    if (app.arguments().contains("--channel-ui-test")) {
        ParameterPanel panel;
        bool txPassed = false;
        bool rxPassed = false;
        for (auto *combo : panel.findChildren<QComboBox *>()) {
            const QString name = combo->property("parameterName").toString();
            if (name != QStringLiteral("激励通道（TX）")
                && name != QStringLiteral("接收通道（RX）"))
                continue;
            bool mappingPassed = combo->count() == 8;
            for (int i = 0; mappingPassed && i < 8; ++i)
                mappingPassed = combo->itemText(i) == QStringLiteral("通道 %1").arg(i + 1)
                                && combo->itemData(i).toInt() == i + 1;
            if (name == QStringLiteral("激励通道（TX）")) txPassed = mappingPassed;
            if (name == QStringLiteral("接收通道（RX）")) rxPassed = mappingPassed;
        }
        const bool passed = txPassed && rxPassed;
        qInfo() << "CHANNEL_UI_TEST:" << (passed ? "PASS" : "FAIL")
                << "tx=" << txPassed << "rx=" << rxPassed;
        return passed ? 0 : 6;
    }
    if (!localMode) {
        auto *remoteSession = new pa1664::remote::RemoteWorkbenchSession(&app);
        MainWindow window(remoteSession);
        QObject::connect(remoteSession,
                         &pa1664::remote::RemoteWorkbenchSession::stateChanged,
                         [](pa1664::remote::WorkbenchConnectionState state,
                            const QString &detail) {
                             qInfo().noquote() << "REMOTE_STATE"
                                               << int(state) << detail;
                         });
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                         remoteSession,
                         &pa1664::remote::RemoteWorkbenchSession::stop);
        window.showMaximized();
        crawling::AppLogger::write(
            QStringLiteral("SESSION"),
            QStringLiteral("event=remote_start host=%1 port=%2")
                .arg(remoteHost)
                .arg(remotePort));
        remoteSession->start(remoteHost, remotePort);
        return app.exec();
    }

    MainWindow window;
    if (app.arguments().contains("--apply-test")) {
        window.show();
        QTimer::singleShot(3000, &window, [&window] {
            qInfo() << "APPLY_TEST: CONNECT";
            QMetaObject::invokeMethod(&window, "connectDevice", Qt::DirectConnection);
        });
        QTimer::singleShot(12000, &window, [&window] {
            qInfo() << "APPLY_TEST: START";
            if (auto *device = window.findChild<DeviceController *>()) device->start();
        });
        QTimer::singleShot(19000, &window, [&window] {
            qInfo() << "APPLY_TEST: APPLY_ALL";
            if (auto *panel = window.findChild<ParameterPanel *>()) {
                auto *device = window.findChild<DeviceController *>();
                for (auto *spin : panel->findChildren<QDoubleSpinBox *>()) {
                    if (spin->property("parameterName").toString() == QStringLiteral("增益")) {
                        const double original = spin->value();
                        const quint64 framesBeforeApply = device ? device->totalFrames() : 0;
                        spin->setValue(original + 0.01);
                        panel->apply();
                        QTimer::singleShot(9000, panel, [panel, spin, original, device, framesBeforeApply] {
                            if (!device || !device->hardwareOnline() || device->totalFrames() <= framesBeforeApply) {
                                qCritical() << "APPLY_TEST: NO_FRAMES_AFTER_FIRST_APPLY";
                                QCoreApplication::exit(4);
                                return;
                            }
                            qInfo() << "APPLY_TEST: RESTORE";
                            spin->setValue(original);
                            panel->apply();
                        });
                        break;
                    }
                }
            }
        });
        QTimer::singleShot(55000, &window, [&window] {
            auto *device = window.findChild<DeviceController *>();
            const bool passed = device && device->hardwareOnline() && device->totalFrames() > 0;
            qInfo() << "APPLY_TEST:" << (passed ? "PASS" : "NO_FRAMES_AFTER_RESTORE");
            QCoreApplication::exit(passed ? 0 : 5);
        });
        return app.exec();
    }
    window.showMaximized();
    return app.exec();
}
