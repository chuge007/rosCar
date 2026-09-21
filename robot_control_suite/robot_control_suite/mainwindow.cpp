#include "mainwindow.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPolygonF>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QStatusBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtGlobal>

// Decode UI strings explicitly as UTF-8 for the MSVC/Qt 5.12 Windows build.
// This avoids the QStringLiteral source-character conversion mismatch that
// otherwise turns Chinese labels into mojibake at runtime.
#ifdef QStringLiteral
#undef QStringLiteral
#endif
#define QStringLiteral(text) QString::fromUtf8(text)

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

QString cmdQuote(const QString &value)
{
    const QString native = QDir::toNativeSeparators(value);
    // QProcess applies its own Windows argument quoting. Passing quotes for
    // a path without spaces makes cmd.exe receive the literal `\"` sequence.
    if (!native.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        return native;
    }
    QString quoted = native;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(quoted);
}

QString joinCmdArguments(const QStringList &arguments)
{
    QStringList quotedArguments;
    quotedArguments.reserve(arguments.size());
    for (const QString &argument : arguments) quotedArguments.push_back(cmdQuote(argument));
    return quotedArguments.join(QStringLiteral(" "));
}

bool isLocalDiscoveryNode(const QString &nodeName)
{
    const QString trimmed = nodeName.trimmed();
    return trimmed.isEmpty() ||
           trimmed == QStringLiteral("/rosout") ||
           trimmed == QStringLiteral("/parameter_events");
}

QString firstExistingFile(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (!path.isEmpty() && QFileInfo::exists(path)) return QDir::cleanPath(path);
    }
    return QString();
}

QString resolveRos2Python(const QString &root)
{
    if (root.isEmpty()) return QString();
    const QDir base(root);
    return firstExistingFile({
        base.filePath(QStringLiteral(".pixi/envs/default/python.exe")),
        base.filePath(QStringLiteral("python/python.exe")),
        base.filePath(QStringLiteral("python/Scripts/python.exe")),
        base.filePath(QStringLiteral("python.exe")),
        base.filePath(QStringLiteral("Scripts/python.exe"))
    });
}

QString resolveRos2Script(const QString &root)
{
    if (root.isEmpty()) return QString();
    return firstExistingFile({QDir(root).filePath(QStringLiteral("Scripts/ros2-script.py"))});
}

QString resolveSetupBat(const QString &root)
{
    if (root.isEmpty()) return QString();
    return firstExistingFile({
        QDir(root).filePath(QStringLiteral("setup.bat")),
        QDir(root).filePath(QStringLiteral("local_setup.bat"))
    });
}

QString resolveCmdVelBridge()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return firstExistingFile({
        QDir(appDir).filePath(QStringLiteral("cmd_vel_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../desktop/cmd_vel_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../../../robot_control_suite/desktop/cmd_vel_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../../../../robot_control_suite/desktop/cmd_vel_bridge.py"))
    });
}

QString controlSettingsPath()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (directory.isEmpty()) directory = QCoreApplication::applicationDirPath();
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("settings.ini"));
}

QString statusValue(const QString &text, const QString &key)
{
    const QRegularExpression expression(
        QStringLiteral("^\\s*%1:\\s*([^\\r\\n]+)").arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

double statusNumber(const QString &text, const QString &key, double fallback = 0.0)
{
    bool ok = false;
    const double value = statusValue(text, key).toDouble(&ok);
    return ok ? value : fallback;
}

bool statusBool(const QString &text, const QString &key, bool fallback = false)
{
    const QString value = statusValue(text, key).toLower();
    if (value == QStringLiteral("true")) return true;
    if (value == QStringLiteral("false")) return false;
    return fallback;
}

QVector<int> statusBytes(const QString &text)
{
    QVector<int> bytes;
    const QStringList lines = text.split('\n');
    bool inData = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!inData) {
            if (!trimmed.startsWith(QStringLiteral("data:"))) continue;
            inData = true;
            const int left = trimmed.indexOf('[');
            const int right = trimmed.lastIndexOf(']');
            if (left >= 0 && right > left) {
                const QStringList values = trimmed.mid(left + 1, right - left - 1)
                    .split(',', QString::SkipEmptyParts);
                for (const QString &value : values) {
                    bool ok = false;
                    const int number = value.trimmed().toInt(&ok);
                    if (ok) bytes.push_back(number);
                }
                inData = false;
            }
            continue;
        }
        if (!trimmed.startsWith('-')) break;
        bool ok = false;
        const int number = trimmed.mid(1).trimmed().toInt(&ok);
        if (ok) bytes.push_back(number);
    }
    return bytes;
}

bool readFloat32(const QVector<int> &bytes, int offset, bool bigEndian, float &result)
{
    if (offset < 0 || offset + 4 > bytes.size()) return false;
    unsigned char raw[4];
    for (int i = 0; i < 4; ++i) raw[i] = static_cast<unsigned char>(qBound(0, bytes[offset + i], 255));
    unsigned char ordered[4];
    if (bigEndian) {
        for (int i = 0; i < 4; ++i) ordered[i] = raw[3 - i];
    } else {
        for (int i = 0; i < 4; ++i) ordered[i] = raw[i];
    }
    std::memcpy(&result, ordered, sizeof(result));
    return std::isfinite(static_cast<double>(result));
}

QLabel *valueLabel(const QString &value)
{
    QLabel *label = new QLabel(value);
    label->setMinimumWidth(145);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

QString MainWindow::resolveRos2Root() const
{
    const QStringList candidates = {
        qEnvironmentVariable("ROS2_WINDOW_ROOT"),
        qEnvironmentVariable("ROS_DISTRO_ROOT"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ros2-window")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../ros2-window")),
        QStringLiteral("D:/ros2/ros2-window"),
        QStringLiteral("C:/pixi_ws/ros2-window"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../../ros2-window"))
    };
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        const QDir root(candidate);
        if (QFileInfo(root.filePath(QStringLiteral("setup.bat"))).exists() &&
            QFileInfo(root.filePath(QStringLiteral("local_setup.bat"))).exists() &&
            QFileInfo(root.filePath(QStringLiteral("Scripts/ros2-script.py"))).exists()) {
            return QDir::cleanPath(root.absolutePath());
        }
    }
    return QString();
}

QString MainWindow::resolveRos2Overlay() const
{
    const QStringList candidates = {
        qEnvironmentVariable("ROBOT_CONTROL_WS_INSTALL"),
        QStringLiteral("D:/dev/CrawlingRobot/target_board_release/install"),
        QStringLiteral("D:/dev/CrawlingRobot/ros2_ws/install"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../../ros2_ws/install"))
    };
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        const QDir install(candidate);
        if (QFileInfo(install.filePath(QStringLiteral("setup.bat"))).exists() ||
            QFileInfo(install.filePath(QStringLiteral("local_setup.bat"))).exists()) {
            return QDir::cleanPath(install.absolutePath());
        }
    }
    return QString();
}

void MainWindow::configureRosEnvironment(QProcessEnvironment &environment) const
{
    const QString domain = domainEdit_ ? domainEdit_->text().trimmed() : QString();
    environment.insert(QStringLiteral("ROS_DOMAIN_ID"), domain.isEmpty() ? QStringLiteral("0") : domain);
    environment.insert(QStringLiteral("RMW_IMPLEMENTATION"), QStringLiteral("rmw_fastrtps_cpp"));
    // This workstation's motor test must stay on the local DDS domain.
    environment.insert(QStringLiteral("ROS_LOCALHOST_ONLY"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ROS_AUTOMATIC_DISCOVERY_RANGE"), QStringLiteral("LOCALHOST"));
    // The ROS 2 CLI daemon survives between app launches and keeps the old
    // network interface selection. Run each command with this launch's DDS
    // settings instead of querying that stale daemon.
    environment.insert(QStringLiteral("ROS2CLI_DISABLE_DAEMON"), QStringLiteral("1"));
    const QString fastDdsProfile = firstExistingFile({
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/fastdds_profile.xml")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fastdds_profile.xml"))
    });
    if (!fastDdsProfile.isEmpty()) {
        environment.insert(QStringLiteral("FASTRTPS_DEFAULT_PROFILES_FILE"), fastDdsProfile);
    }
    if (!ros2Root_.isEmpty()) {
        environment.insert(QStringLiteral("ROS2_WINDOW_ROOT"), ros2Root_);
        environment.insert(QStringLiteral("ROS_DISTRO_ROOT"), ros2Root_);

        // The desktop process does not inherit the shell that normally sources
        // ROS 2. Add the relocatable runtime DLL directories explicitly.
        QStringList pathEntries;
        pathEntries << ros2Root_
                    << QDir(ros2Root_).filePath(QStringLiteral("bin"))
                    << QDir(ros2Root_).filePath(QStringLiteral("python"))
                    << QDir(ros2Root_).filePath(QStringLiteral("python/Scripts"))
                    << QDir(ros2Root_).filePath(QStringLiteral("python/Library/bin"))
                    << QDir(ros2Root_).filePath(QStringLiteral(".pixi/envs/default"))
                    << QDir(ros2Root_).filePath(QStringLiteral(".pixi/envs/default/Library/bin"))
                    << QDir(ros2Root_).filePath(QStringLiteral(".pixi/envs/default/Scripts"));
        if (!ros2Python_.isEmpty()) {
            const QDir pythonDir(QFileInfo(ros2Python_).absolutePath());
            pathEntries << pythonDir.absolutePath()
                        << pythonDir.filePath(QStringLiteral("Scripts"))
                        << pythonDir.filePath(QStringLiteral("Library/bin"));
        }
        if (!ros2Overlay_.isEmpty()) {
            pathEntries << ros2Overlay_
                        << QDir(ros2Overlay_).filePath(QStringLiteral("bin"));
        }
        environment.insert(QStringLiteral("ROBOT_CONTROL_ROS_PATH"),
                           pathEntries.join(QStringLiteral(";")));
        // QProcess starts Python directly, so its DLL loader reads PATH rather
        // than the application-specific ROBOT_CONTROL_ROS_PATH helper above.
        const QString inheritedPath = environment.value(QStringLiteral("PATH"));
        environment.insert(QStringLiteral("PATH"),
                           pathEntries.join(QStringLiteral(";")) + QStringLiteral(";") + inheritedPath);
        // The bundled interpreter does not discover the ROS distribution by
        // itself when it is launched from a Qt process. This is the part of
        // setup.bat needed by ros2-script.py; keeping it here avoids cmd.exe
        // quote parsing entirely.
        environment.insert(QStringLiteral("PYTHONPATH"),
                           QDir(ros2Root_).filePath(QStringLiteral("Lib/site-packages")));
        const QString inheritedAment = environment.value(QStringLiteral("AMENT_PREFIX_PATH"));
        const QString inheritedCmake = environment.value(QStringLiteral("CMAKE_PREFIX_PATH"));
        const QString overlayPrefix = ros2Overlay_.isEmpty() ? QString() : ros2Overlay_;
        environment.insert(QStringLiteral("AMENT_PREFIX_PATH"),
                           overlayPrefix.isEmpty() ? ros2Root_ : overlayPrefix + QStringLiteral(";") + ros2Root_ +
                               (inheritedAment.isEmpty() ? QString() : QStringLiteral(";") + inheritedAment));
        environment.insert(QStringLiteral("CMAKE_PREFIX_PATH"),
                           overlayPrefix.isEmpty() ? ros2Root_ : overlayPrefix + QStringLiteral(";") + ros2Root_ +
                               (inheritedCmake.isEmpty() ? QString() : QStringLiteral(";") + inheritedCmake));
        if (!overlayPrefix.isEmpty()) {
            const QString overlayPython = QDir(overlayPrefix).filePath(QStringLiteral("Lib/site-packages"));
            const QString pythonPath = environment.value(QStringLiteral("PYTHONPATH"));
            environment.insert(QStringLiteral("PYTHONPATH"), overlayPython + QStringLiteral(";") + pythonPath);
        }
    }
    if (!ros2Overlay_.isEmpty()) {
        environment.insert(QStringLiteral("ROBOT_CONTROL_WS_INSTALL"), ros2Overlay_);
    }
}

ContourPlot::ContourPlot(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(440, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ContourPlot::setPoints(const QVector<QPointF> &points)
{
    points_ = points;
    update();
}

void ContourPlot::setSeamLateral(double lateral, bool valid)
{
    seamLateral_ = lateral;
    seamValid_ = valid;
    update();
}

QSize ContourPlot::minimumSizeHint() const
{
    return QSize(440, 300);
}

void ContourPlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#0d141a")));
    const QRectF frame = rect().adjusted(54, 26, -22, -42);
    painter.setPen(QPen(QColor(QStringLiteral("#30414d")), 1));
    painter.drawRect(frame);
    painter.setPen(QColor(QStringLiteral("#8ea0ad")));
    painter.drawText(QRectF(54, 5, width() - 76, 18), Qt::AlignLeft,
                     QStringLiteral("线激光轮廓仪成像"));
    painter.drawText(QRectF(54, height() - 31, width() - 76, 20), Qt::AlignLeft,
                     QStringLiteral("横向 / lateral (m)"));
    painter.save();
    painter.translate(13, frame.bottom());
    painter.rotate(-90);
    painter.drawText(QRectF(0, 0, frame.height(), 20), Qt::AlignLeft,
                     QStringLiteral("高度 / height"));
    painter.restore();

    double xMin = -0.05;
    double xMax = 0.05;
    for (const QPointF &point : points_) {
        xMin = std::min(xMin, point.x());
        xMax = std::max(xMax, point.x());
    }
    if (seamValid_) {
        xMin = std::min(xMin, seamLateral_);
        xMax = std::max(xMax, seamLateral_);
    }
    const double xSpan = std::max(0.01, xMax - xMin);
    xMin -= xSpan * 0.08;
    xMax += xSpan * 0.08;
    double yMin = -0.005;
    double yMax = 0.005;
    if (!points_.isEmpty()) {
        yMin = points_.first().y();
        yMax = yMin;
        for (const QPointF &point : points_) {
            yMin = std::min(yMin, point.y());
            yMax = std::max(yMax, point.y());
        }
        const double span = std::max(0.001, yMax - yMin);
        yMin -= span * 0.12;
        yMax += span * 0.12;
    }
    const auto mapPoint = [&](const QPointF &point) {
        const double px = frame.left() + (point.x() - xMin) / (xMax - xMin) * frame.width();
        const double py = frame.bottom() - (point.y() - yMin) /
                          std::max(1e-9, yMax - yMin) * frame.height();
        return QPointF(px, py);
    };
    const double targetX = frame.left() + (-xMin) / (xMax - xMin) * frame.width();
    painter.setPen(QPen(QColor(QStringLiteral("#e5b84b")), 1, Qt::DashLine));
    painter.drawLine(QPointF(targetX, frame.top()), QPointF(targetX, frame.bottom()));
    if (seamValid_) {
        const double seamX = frame.left() + (seamLateral_ - xMin) / (xMax - xMin) * frame.width();
        painter.setPen(QPen(QColor(QStringLiteral("#e85d5d")), 2, Qt::DashLine));
        painter.drawLine(QPointF(seamX, frame.top()), QPointF(seamX, frame.bottom()));
    }
    if (points_.size() >= 2) {
        QPolygonF polygon;
        for (const QPointF &point : points_) polygon << mapPoint(point);
        painter.setPen(QPen(QColor(QStringLiteral("#4ed7e8")), 2));
        painter.drawPolyline(polygon);
    } else {
        painter.setPen(QColor(QStringLiteral("#8ea0ad")));
        painter.drawText(frame, Qt::AlignCenter, QStringLiteral("等待 /laser_profile/frame 数据"));
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ros2Root_ = resolveRos2Root();
    ros2Overlay_ = resolveRos2Overlay();
    ros2Python_ = resolveRos2Python(ros2Root_);
    ros2Script_ = resolveRos2Script(ros2Root_);
    ros2Available_ = !ros2Root_.isEmpty() && !ros2Python_.isEmpty() && !ros2Script_.isEmpty();
    discoveryTimeoutTimer_.setSingleShot(true);
    statusTimeoutTimer_.setSingleShot(true);
    profileTimeoutTimer_.setSingleShot(true);
    jointTimeoutTimer_.setSingleShot(true);
    setWindowTitle(QStringLiteral("爬壁机器人控制台"));
    resize(1120, 720);
    buildUi();
    loadSettings();
    connect(domainEdit_, &QLineEdit::textChanged, this, [this](const QString &) { saveSettings(); });
    connect(linearSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(angularSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(linearAccelSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(angularAccelSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(maxWheelSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(minimumInnerWheelRatioSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    connect(leftMotorIdSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { saveSettings(); });
    connect(rightMotorIdSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { saveSettings(); });
    connect(leftMotorSignCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSettings(); });
    connect(rightMotorSignCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSettings(); });
    if (ros2Available_) {
        connectionLabel_->setText(QStringLiteral("等待 ROS 2 发现"));
        statusBar()->showMessage(QStringLiteral("ROS 2 运行时已就绪，正在搜索目标机"));
    } else {
        connectionLabel_->setText(QStringLiteral("未找到 ROS 2 运行时"));
        statusBar()->showMessage(QStringLiteral("未找到 ROS 2 运行时，无法连接目标机"));
    }
    connect(&commandTimer_, &QTimer::timeout, this, &MainWindow::commandTick);
    connect(&discoveryTimer_, &QTimer::timeout, this, &MainWindow::pollDiscovery);
    connect(&statusTimer_, &QTimer::timeout, this, &MainWindow::pollStatus);
    connect(&profileTimer_, &QTimer::timeout, this, &MainWindow::pollProfile);
    connect(&jointTimer_, &QTimer::timeout, this, &MainWindow::pollJointState);
    connect(&jointTimeoutTimer_, &QTimer::timeout, this, &MainWindow::jointProcessTimeout);
    connect(&discoveryTimeoutTimer_, &QTimer::timeout, this, &MainWindow::discoveryProcessTimeout);
    connect(&statusTimeoutTimer_, &QTimer::timeout, this, &MainWindow::statusProcessTimeout);
    connect(&profileTimeoutTimer_, &QTimer::timeout, this, &MainWindow::profileProcessTimeout);
    // Keep the command heartbeat below the drive-node timeout. The bridge is
    // started eagerly so the first key press never waits for a ROS CLI spawn.
    commandTimer_.start(20);
    discoveryTimer_.start(1200);
    statusTimer_.start(1000);
    profileTimer_.start(500);
    jointTimer_.start(500);
    if (ros2Available_) startCmdVelBridge();
    pollDiscovery();
}

MainWindow::~MainWindow()
{
    saveSettings();
    publishTwist(0.0, 0.0);
    if (cmdVelProcess_) {
        cmdVelProcess_->closeWriteChannel();
        cmdVelProcess_->kill();
        cmdVelProcess_ = nullptr;
    }
    if (discoveryProcess_) discoveryProcess_->kill();
    if (statusProcess_) statusProcess_->kill();
    if (profileProcess_) profileProcess_->kill();
    if (jointProcess_) jointProcess_->kill();
}

void MainWindow::loadSettings()
{
    QSettings settings(controlSettingsPath(), QSettings::IniFormat);
    if (domainEdit_) {
        domainEdit_->setText(settings.value(QStringLiteral("ros/domain_id"), QStringLiteral("0"))
                                  .toString());
    }
    if (linearSpeedSpin_) {
        linearSpeedSpin_->setValue(settings.value(QStringLiteral("drive/linear_speed_m_s"),
                                                  linearSpeedSpin_->value()).toDouble());
    }
    if (angularSpeedSpin_) {
        angularSpeedSpin_->setValue(settings.value(QStringLiteral("drive/angular_speed_rad_s"),
                                                   angularSpeedSpin_->value()).toDouble());
    }
    if (linearAccelSpin_) {
        linearAccelSpin_->setValue(settings.value(QStringLiteral("drive/linear_accel_m_s2"),
                                                  linearAccelSpin_->value()).toDouble());
    }
    if (angularAccelSpin_) {
        angularAccelSpin_->setValue(settings.value(QStringLiteral("drive/angular_accel_rad_s2"),
                                                   angularAccelSpin_->value()).toDouble());
    }
    if (minimumInnerWheelRatioSpin_) {
        minimumInnerWheelRatioSpin_->setValue(settings.value(QStringLiteral("drive/minimum_inner_wheel_ratio"),
                                                             minimumInnerWheelRatioSpin_->value()).toDouble());
    }
    if (maxWheelSpeedSpin_) {
        maxWheelSpeedSpin_->setValue(settings.value(QStringLiteral("drive/max_wheel_speed_m_s"),
                                                    maxWheelSpeedSpin_->value()).toDouble());
    }
    if (leftMotorIdSpin_) {
        leftMotorIdSpin_->setValue(settings.value(QStringLiteral("drive/left_motor_id"),
                                                  leftMotorIdSpin_->value()).toInt());
    }
    if (rightMotorIdSpin_) {
        rightMotorIdSpin_->setValue(settings.value(QStringLiteral("drive/right_motor_id"),
                                                   rightMotorIdSpin_->value()).toInt());
    }
    const auto restoreSign = [&settings](QComboBox *combo, const QString &key) {
        if (!combo) return;
        const int sign = settings.value(key, combo->currentData()).toInt();
        const int index = combo->findData(sign);
        if (index >= 0) combo->setCurrentIndex(index);
    };
    restoreSign(leftMotorSignCombo_, QStringLiteral("drive/left_motor_sign"));
    restoreSign(rightMotorSignCombo_, QStringLiteral("drive/right_motor_sign"));

    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    const QByteArray state = settings.value(QStringLiteral("window/state")).toByteArray();
    if (!state.isEmpty()) restoreState(state);
}

void MainWindow::saveSettings() const
{
    QSettings settings(controlSettingsPath(), QSettings::IniFormat);
    if (domainEdit_) settings.setValue(QStringLiteral("ros/domain_id"), domainEdit_->text());
    if (linearSpeedSpin_) {
        settings.setValue(QStringLiteral("drive/linear_speed_m_s"), linearSpeedSpin_->value());
    }
    if (angularSpeedSpin_) {
        settings.setValue(QStringLiteral("drive/angular_speed_rad_s"), angularSpeedSpin_->value());
    }
    if (linearAccelSpin_) {
        settings.setValue(QStringLiteral("drive/linear_accel_m_s2"), linearAccelSpin_->value());
    }
    if (angularAccelSpin_) {
        settings.setValue(QStringLiteral("drive/angular_accel_rad_s2"), angularAccelSpin_->value());
    }
    if (minimumInnerWheelRatioSpin_) {
        settings.setValue(QStringLiteral("drive/minimum_inner_wheel_ratio"), minimumInnerWheelRatioSpin_->value());
    }
    if (maxWheelSpeedSpin_) {
        settings.setValue(QStringLiteral("drive/max_wheel_speed_m_s"), maxWheelSpeedSpin_->value());
    }
    if (leftMotorIdSpin_) {
        settings.setValue(QStringLiteral("drive/left_motor_id"), leftMotorIdSpin_->value());
    }
    if (rightMotorIdSpin_) {
        settings.setValue(QStringLiteral("drive/right_motor_id"), rightMotorIdSpin_->value());
    }
    if (leftMotorSignCombo_) {
        settings.setValue(QStringLiteral("drive/left_motor_sign"), leftMotorSignCombo_->currentData());
    }
    if (rightMotorSignCombo_) {
        settings.setValue(QStringLiteral("drive/right_motor_sign"), rightMotorSignCombo_->currentData());
    }
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.sync();
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(18, 14, 18, 14);
    QLabel *title = new QLabel(QStringLiteral("爬壁机器人控制台"));
    title->setObjectName(QStringLiteral("title"));
    root->addWidget(title);
    QLabel *subtitle = new QLabel(QStringLiteral("开发板 ROS 2 控制接口 / 线激光焊缝纠偏"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    root->addWidget(subtitle);

    QHBoxLayout *connection = new QHBoxLayout;
    connection->addWidget(new QLabel(QStringLiteral("ROS_DOMAIN_ID")));
    domainEdit_ = new QLineEdit;
    domainEdit_->setPlaceholderText(QStringLiteral("默认域"));
    domainEdit_->setMaximumWidth(120);
    connection->addWidget(domainEdit_);
    QPushButton *apply = new QPushButton(QStringLiteral("应用网络设置"));
    connection->addWidget(apply);
    connection->addStretch();
    connectionLabel_ = new QLabel(QStringLiteral("未连接"));
    connection->addWidget(connectionLabel_);
    root->addLayout(connection);
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyDomain);

    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    QWidget *left = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    QGroupBox *driveBox = new QGroupBox(QStringLiteral("手动驾驶"));
    QGridLayout *pad = new QGridLayout(driveBox);
    const auto addDrive = [&](const QString &text, const QString &key, int row, int column) {
        QPushButton *button = new QPushButton(text);
        button->setProperty("driveKey", key);
        button->setMinimumSize(96, 46);
        pad->addWidget(button, row, column);
        connect(button, &QPushButton::pressed, this, &MainWindow::drivePressed);
        connect(button, &QPushButton::released, this, &MainWindow::driveReleased);
    };
    addDrive(QStringLiteral("前进 ↑"), QStringLiteral("up"), 0, 1);
    addDrive(QStringLiteral("左转 ←"), QStringLiteral("left"), 1, 0);
    addDrive(QStringLiteral("停止 ■"), QStringLiteral("stop"), 1, 1);
    addDrive(QStringLiteral("右转 →"), QStringLiteral("right"), 1, 2);
    addDrive(QStringLiteral("后退 ↓"), QStringLiteral("down"), 2, 1);
    leftLayout->addWidget(driveBox);

    QGroupBox *speedBox = new QGroupBox(QStringLiteral("运动参数"));
    QFormLayout *speedLayout = new QFormLayout(speedBox);
    const auto makeSpin = [speedBox](double minimum, double maximum, double step,
                                     double value, int decimals, const QString &suffix) {
        QDoubleSpinBox *spin = new QDoubleSpinBox(speedBox);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(value);
        spin->setSuffix(suffix);
        spin->setKeyboardTracking(false);
        return spin;
    };
    linearSpeedSpin_ = makeSpin(0.001, 0.300, 0.001, 0.025, 3, QStringLiteral(" m/s"));
    angularSpeedSpin_ = makeSpin(0.01, 2.00, 0.01, 0.30, 2, QStringLiteral(" rad/s"));
    // Keep the low test speed, but make the initial response perceptible. The
    // drive node still enforces the configured limits and command timeout.
    linearAccelSpin_ = makeSpin(0.001, 1.000, 0.001, 0.500, 3, QStringLiteral(" m/s²"));
    angularAccelSpin_ = makeSpin(0.01, 5.00, 0.01, 2.00, 2, QStringLiteral(" rad/s²"));
    maxWheelSpeedSpin_ = makeSpin(0.001, 1.00, 0.001, 0.300, 3, QStringLiteral(" m/s"));
    minimumInnerWheelRatioSpin_ = makeSpin(0.0, 1.0, 0.05, 0.50, 2, QStringLiteral(""));
    speedLayout->addRow(QStringLiteral("线速度上限"), linearSpeedSpin_);
    speedLayout->addRow(QStringLiteral("角速度上限"), angularSpeedSpin_);
    speedLayout->addRow(QStringLiteral("线加速度"), linearAccelSpin_);
    speedLayout->addRow(QStringLiteral("角加速度"), angularAccelSpin_);
    speedLayout->addRow(QStringLiteral("车轮速度上限"), maxWheelSpeedSpin_);
    speedLayout->addRow(QStringLiteral("转弯内轮/外轮比例"), minimumInnerWheelRatioSpin_);
    QPushButton *applyDriveLimits = new QPushButton(QStringLiteral("应用运动参数"));
    speedLayout->addRow(applyDriveLimits);
    connect(applyDriveLimits, &QPushButton::clicked, this, &MainWindow::applyDriveLimits);
    leftLayout->addWidget(speedBox);

    QGroupBox *mappingBox = new QGroupBox(QStringLiteral("电机节点映射"));
    QFormLayout *mappingLayout = new QFormLayout(mappingBox);
    auto makeMotorId = [mappingBox](int value) {
        QSpinBox *spin = new QSpinBox(mappingBox);
        spin->setRange(1, 32);
        spin->setValue(value);
        return spin;
    };
    auto makeMotorSign = [mappingBox](int value) {
        QComboBox *combo = new QComboBox(mappingBox);
        combo->addItem(QStringLiteral("+1"), 1);
        combo->addItem(QStringLiteral("-1"), -1);
        combo->setCurrentIndex(value > 0 ? 0 : 1);
        return combo;
    };
    leftMotorIdSpin_ = makeMotorId(1);
    rightMotorIdSpin_ = makeMotorId(2);
    leftMotorSignCombo_ = makeMotorSign(1);
    rightMotorSignCombo_ = makeMotorSign(-1);
    QWidget *leftMotor = new QWidget(mappingBox);
    QHBoxLayout *leftMotorLayout = new QHBoxLayout(leftMotor);
    leftMotorLayout->setContentsMargins(0, 0, 0, 0);
    leftMotorLayout->addWidget(leftMotorIdSpin_);
    leftMotorLayout->addWidget(leftMotorSignCombo_);
    QWidget *rightMotor = new QWidget(mappingBox);
    QHBoxLayout *rightMotorLayout = new QHBoxLayout(rightMotor);
    rightMotorLayout->setContentsMargins(0, 0, 0, 0);
    rightMotorLayout->addWidget(rightMotorIdSpin_);
    rightMotorLayout->addWidget(rightMotorSignCombo_);
    mappingLayout->addRow(QStringLiteral("左轮 节点/输出符号"), leftMotor);
    mappingLayout->addRow(QStringLiteral("右轮 节点/输出符号"), rightMotor);
    QLabel *mappingHint = new QLabel(
        QStringLiteral("+1 保持协议正向，-1 反向；用于校正左右电机的镜像安装。"), mappingBox);
    mappingHint->setWordWrap(true);
    mappingLayout->addRow(mappingHint);
    QPushButton *applyMapping = new QPushButton(QStringLiteral("应用电机映射"));
    mappingLayout->addRow(applyMapping);
    connect(applyMapping, &QPushButton::clicked, this, &MainWindow::applyMotorMapping);
    leftLayout->addWidget(mappingBox);

    QGridLayout *actions = new QGridLayout;
    QPushButton *drive = new QPushButton(QStringLiteral("底盘使能"));
    QPushButton *automatic = new QPushButton(QStringLiteral("自动纠偏行走"));
    QPushButton *stopAutomatic = new QPushButton(QStringLiteral("停止自动行走"));
    QPushButton *localization = new QPushButton(QStringLiteral("定位感知"));
    QPushButton *reset = new QPushButton(QStringLiteral("复位滤波"));
    QPushButton *contour = new QPushButton(QStringLiteral("打开轮廓成像"));
    actions->addWidget(drive, 0, 0);
    actions->addWidget(automatic, 0, 1);
    actions->addWidget(stopAutomatic, 0, 2);
    actions->addWidget(localization, 1, 0);
    actions->addWidget(reset, 1, 1);
    actions->addWidget(contour, 1, 2);
    for (int column = 0; column < 3; ++column) actions->setColumnStretch(column, 1);
    leftLayout->addLayout(actions);
    connect(drive, &QPushButton::clicked, this, &MainWindow::toggleDrive);
    connect(automatic, &QPushButton::clicked, this, &MainWindow::toggleAuto);
    connect(stopAutomatic, &QPushButton::clicked, this, &MainWindow::stopAutoWalk);
    connect(localization, &QPushButton::clicked, this, &MainWindow::captureLocalization);
    connect(reset, &QPushButton::clicked, this, &MainWindow::resetCorrection);
    connect(contour, &QPushButton::clicked, this, &MainWindow::openContourDialog);

    QGroupBox *safety = new QGroupBox(QStringLiteral("安全操作"));
    QVBoxLayout *safetyLayout = new QVBoxLayout(safety);
    QPushButton *estop = new QPushButton(QStringLiteral("急停 / 清除速度"));
    QPushButton *release = new QPushButton(QStringLiteral("解除急停"));
    safetyLayout->addWidget(estop);
    safetyLayout->addWidget(release);
    safetyLayout->addWidget(new QLabel(QStringLiteral("急停会持续发布零速度。解除后需重新底盘使能。")));
    leftLayout->addWidget(safety);
    connect(estop, &QPushButton::clicked, this, &MainWindow::emergencyStop);
    connect(release, &QPushButton::clicked, this, &MainWindow::releaseEmergencyStop);
    leftLayout->addStretch();

    QWidget *right = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    QGroupBox *statusBox = new QGroupBox(QStringLiteral("ROS 2 姿态状态"));
    QFormLayout *statusForm = new QFormLayout(statusBox);
    modeLabel_ = valueLabel(QStringLiteral("自动纠偏：关闭"));
    statusLabel_ = valueLabel(QStringLiteral("等待状态"));
    errorLabel_ = valueLabel(QStringLiteral("--"));
    previewErrorLabel_ = valueLabel(QStringLiteral("--"));
    headingLabel_ = valueLabel(QStringLiteral("--"));
    curvatureLabel_ = valueLabel(QStringLiteral("--"));
    angularAccelLabel_ = valueLabel(QStringLiteral("--"));
    linearCommandLabel_ = valueLabel(QStringLiteral("--"));
    angularCommandLabel_ = valueLabel(QStringLiteral("--"));
    confidenceLabel_ = valueLabel(QStringLiteral("--"));
    fitLabel_ = valueLabel(QStringLiteral("--"));
    motorLabel_ = valueLabel(QStringLiteral("等待 /drive/joint_states"));
    statusForm->addRow(QStringLiteral("模式"), modeLabel_);
    statusForm->addRow(QStringLiteral("状态"), statusLabel_);
    statusForm->addRow(QStringLiteral("横滚 Roll"), errorLabel_);
    statusForm->addRow(QStringLiteral("俯仰 Pitch"), previewErrorLabel_);
    statusForm->addRow(QStringLiteral("航向角"), headingLabel_);
    statusForm->addRow(QStringLiteral("角速度 Z"), curvatureLabel_);
    statusForm->addRow(QStringLiteral("角加速度"), angularAccelLabel_);
    statusForm->addRow(QStringLiteral("实际线速度"), linearCommandLabel_);
    statusForm->addRow(QStringLiteral("实际角速度"), angularCommandLabel_);
    statusForm->addRow(QStringLiteral("轮廓置信度"), confidenceLabel_);
    statusForm->addRow(QStringLiteral("拟合残差"), fitLabel_);
    statusForm->addRow(QStringLiteral("电机反馈"), motorLabel_);
    rightLayout->addWidget(statusBox);
    plot_ = new ContourPlot;
    rightLayout->addWidget(plot_, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(
        "QMainWindow,QWidget{background:#111820;color:#e8eef2;font-family:'Segoe UI';font-size:10pt;}"
        "#title{font-size:20pt;font-weight:600;color:white;}#subtitle{color:#8ea0ad;}"
        "QGroupBox{border:1px solid #30414d;border-radius:5px;margin-top:10px;padding:12px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 5px;color:#d8e4ea;}"
        "QPushButton{background:#263744;border:1px solid #405563;border-radius:4px;padding:8px 12px;}"
        "QPushButton:hover{background:#315064;}QPushButton:pressed{background:#1976a8;}"
        "QLineEdit,QDoubleSpinBox{background:#1d2933;border:1px solid #405563;border-radius:3px;padding:6px;}"
        "QSlider::groove:horizontal{height:5px;background:#30414d;}"
        "QSlider::handle:horizontal{width:15px;margin:-5px 0;border-radius:7px;background:#4ed7e8;}"
        "QStatusBar{color:#8ea0ad;}"));
}

QProcess *MainWindow::startCli(const QStringList &arguments, bool includeOverlay)
{
    Q_UNUSED(includeOverlay);
    if (!ros2Available_ || ros2Root_.isEmpty() || ros2Python_.isEmpty() || ros2Script_.isEmpty()) {
        return nullptr;
    }
    QProcess *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    configureRosEnvironment(environment);
    process->setProcessEnvironment(environment);

    QStringList cliArguments;
    cliArguments << ros2Script_ << arguments;
    process->start(ros2Python_, cliArguments);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            statusBar()->showMessage(QStringLiteral("ROS CLI 启动失败：%1").arg(process->errorString()), 5000);
            process->deleteLater();
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process, &QObject::deleteLater);
    return process;
}

void MainWindow::publishTwist(double linear, double angular)
{
    pendingLinear_ = linear;
    pendingAngular_ = angular;
    if (!cmdVelProcess_) startCmdVelBridge();
    sendPendingTwist();
}

void MainWindow::startCmdVelBridge()
{
    if (cmdVelProcess_ || !ros2Available_) return;
    const QString script = resolveCmdVelBridge();
    if (script.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未找到低延迟 /cmd_vel 发布桥"), 5000);
        return;
    }
    cmdVelProcess_ = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    configureRosEnvironment(environment);
    cmdVelProcess_->setProcessEnvironment(environment);
    connect(cmdVelProcess_, &QProcess::started, this, &MainWindow::sendPendingTwist);
    connect(cmdVelProcess_, &QProcess::readyReadStandardError, this, [this]() {
        if (!cmdVelProcess_) return;
        const QString error = QString::fromLocal8Bit(cmdVelProcess_->readAllStandardError()).simplified();
        if (!error.isEmpty()) statusBar()->showMessage(error, 5000);
    });
    connect(cmdVelProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (cmdVelProcess_) {
                    cmdVelProcess_->deleteLater();
                    cmdVelProcess_ = nullptr;
                }
            });
    connect(cmdVelProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            statusBar()->showMessage(QStringLiteral("低延迟 /cmd_vel 发布桥启动失败"), 5000);
        }
    });
    cmdVelProcess_->start(ros2Python_, QStringList() << script);
}

void MainWindow::sendPendingTwist()
{
    if (!cmdVelProcess_ || cmdVelProcess_->state() != QProcess::Running) return;
    const QByteArray line = QStringLiteral("{\"linear\":%1,\"angular\":%2}\n")
        .arg(pendingLinear_, 0, 'f', 6).arg(pendingAngular_, 0, 'f', 6).toUtf8();
    cmdVelProcess_->write(line);
}

void MainWindow::applyDriveLimits()
{
    saveSettings();
    const QString request = QStringLiteral(
        "{max_linear_speed_m_s: %1, max_angular_speed_rad_s: %2, "
        "max_linear_accel_m_s2: %3, max_angular_accel_rad_s2: %4, "
        "max_wheel_speed_m_s: %5, minimum_inner_wheel_ratio: %6}")
        .arg(linearSpeedSpin_->value(), 0, 'f', 3)
        .arg(angularSpeedSpin_->value(), 0, 'f', 3)
        .arg(linearAccelSpin_->value(), 0, 'f', 3)
        .arg(angularAccelSpin_->value(), 0, 'f', 3)
        .arg(maxWheelSpeedSpin_->value(), 0, 'f', 3)
        .arg(minimumInnerWheelRatioSpin_->value(), 0, 'f', 2);
    pressedKeys_.clear();
    publishTwist(0.0, 0.0);
    callService(QStringLiteral("/drive/set_limits"),
                QStringLiteral("crawling_robot_interfaces/srv/SetDriveLimits"), request);
}

void MainWindow::applyMotorMapping()
{
    saveSettings();
    if (!leftMotorIdSpin_ || !rightMotorIdSpin_ || !leftMotorSignCombo_ || !rightMotorSignCombo_) return;
    if (leftMotorIdSpin_->value() == rightMotorIdSpin_->value()) {
        statusBar()->showMessage(QStringLiteral("左右轮电机节点不能相同"), 5000);
        return;
    }
    const QString request = QStringLiteral(
        "{left_motor_id: %1, right_motor_id: %2, left_motor_sign: %3, right_motor_sign: %4}")
        .arg(leftMotorIdSpin_->value())
        .arg(rightMotorIdSpin_->value())
        .arg(leftMotorSignCombo_->currentData().toInt())
        .arg(rightMotorSignCombo_->currentData().toInt());
    pressedKeys_.clear();
    publishTwist(0.0, 0.0);
    callService(QStringLiteral("/drive/set_motor_mapping"),
                QStringLiteral("crawling_robot_interfaces/srv/SetMotorMapping"), request);
    statusBar()->showMessage(QStringLiteral("电机映射请求已发送，底盘将保持禁用"), 4000);
}

void MainWindow::callService(const QString &service, const QString &type, const QString &request)
{
    QProcess *process = startCli(QStringList() << QStringLiteral("service") << QStringLiteral("call")
                                 << service << type << request);
    if (!process) return;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).simplified();
        if (!output.isEmpty()) statusBar()->showMessage(output, 4000);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        const QString output = QString::fromLocal8Bit(process->readAllStandardError()).simplified();
        if (!output.isEmpty()) statusBar()->showMessage(output, 4000);
    });
}

void MainWindow::pollDiscovery()
{
    if (!ros2Available_ || discoveryProcess_) return;
    // Discovery only needs the ROS distribution. Do not source a possibly
    // incomplete local overlay before node discovery, otherwise its package
    // hooks can abort the command before DDS discovery starts.
    // Do not reuse a daemon created with another network interface/profile.
    discoveryProcess_ = startCli(QStringList() << QStringLiteral("node") << QStringLiteral("list")
                                  << QStringLiteral("--no-daemon"), false);
    if (!discoveryProcess_) return;
    connect(discoveryProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::discoveryProcessFinished);
    discoveryTimeoutTimer_.start(2500);
}

void MainWindow::discoveryProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    discoveryTimeoutTimer_.stop();
    if (!discoveryProcess_) return;
    const QByteArray output = discoveryProcess_->readAllStandardOutput();
    const QByteArray errors = discoveryProcess_->readAllStandardError();
    discoveryProcess_ = nullptr;
    const QString text = QString::fromLocal8Bit(output);
    const QStringList nodes = text.split(QRegularExpression("[\\r\\n]+"), QString::SkipEmptyParts);
    bool discovered = false;
    for (const QString &node : nodes) {
        if (!isLocalDiscoveryNode(node)) {
            discovered = true;
            break;
        }
    }
    rosDiscovered_ = discovered;
    if (discovered) {
        connectionLabel_->setText(QStringLiteral("ROS 2 已发现"));
        statusBar()->showMessage(QStringLiteral("已发现目标机 ROS 2，正在读取状态"), 3000);
        pollStatus();
        pollProfile();
    } else {
        connectionLabel_->setText(QStringLiteral("未发现目标机 ROS 2"));
        statusLabel_->setText(QStringLiteral("等待目标机 ROS 2"));
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString errorText = QString::fromLocal8Bit(errors).simplified();
            statusBar()->showMessage(errorText.isEmpty()
                ? QStringLiteral("ROS 发现命令失败：退出码 %1").arg(exitCode)
                : QStringLiteral("ROS 发现失败：%1").arg(errorText), 7000);
        }
    }
}

void MainWindow::discoveryProcessTimeout()
{
    discoveryTimeoutTimer_.stop();
    if (!discoveryProcess_) return;
    discoveryProcess_->kill();
    discoveryProcess_ = nullptr;
    rosDiscovered_ = false;
    connectionLabel_->setText(QStringLiteral("未发现目标机 ROS 2"));
    statusLabel_->setText(QStringLiteral("等待目标机 ROS 2"));
    statusBar()->showMessage(QStringLiteral("ROS 发现超时（2.5 秒）"), 5000);
}

void MainWindow::pollStatus()
{
    if (!ros2Available_ || !rosDiscovered_ || statusProcess_) return;
    statusProcess_ = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                              << QStringLiteral("--once") << QStringLiteral("/imu/data")
                              << QStringLiteral("sensor_msgs/msg/Imu")
                              << QStringLiteral("--qos-reliability") << QStringLiteral("best_effort")
                              << QStringLiteral("--qos-durability") << QStringLiteral("volatile")
                              << QStringLiteral("--no-daemon"));
    if (!statusProcess_) return;
    connect(statusProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::statusProcessFinished);
    connect(statusProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            statusLabel_->setText(QStringLiteral("ROS CLI 启动失败"));
        }
    });
    statusTimeoutTimer_.start(2500);
}

void MainWindow::statusProcessFinished(int, QProcess::ExitStatus)
{
    statusTimeoutTimer_.stop();
    if (!statusProcess_) return;
    const QByteArray output = statusProcess_->readAllStandardOutput();
    const QByteArray errors = statusProcess_->readAllStandardError();
    statusProcess_ = nullptr;
    if (!errors.trimmed().isEmpty()) {
        statusBar()->showMessage(QString::fromLocal8Bit(errors).simplified(), 4000);
    }
    if (!output.trimmed().isEmpty()) {
        parseImu(output);
        connectionLabel_->setText(QStringLiteral("ROS 2 已连接，正在接收 IMU"));
        // Read the correction state independently so the localization action
        // has the latest contour and heading reference available.
        QProcess *correction = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                                         << QStringLiteral("--once") << QStringLiteral("/laser_correction/status")
                                         << QStringLiteral("crawling_robot_interfaces/msg/LaserCorrectionStatus")
                                         << QStringLiteral("--no-daemon"));
        if (correction) {
            connect(correction, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, correction](int, QProcess::ExitStatus) {
                const QByteArray state = correction->readAllStandardOutput();
                if (!state.trimmed().isEmpty()) parseStatus(state);
            });
        }
    } else if (rosDiscovered_) {
        connectionLabel_->setText(QStringLiteral("ROS 2 已发现，等待 /imu/data"));
        statusLabel_->setText(QStringLiteral("等待 /imu/data"));
    }
}

void MainWindow::statusProcessTimeout()
{
    statusTimeoutTimer_.stop();
    if (!statusProcess_) return;
    statusProcess_->kill();
    statusProcess_ = nullptr;
    if (rosDiscovered_) {
        connectionLabel_->setText(QStringLiteral("ROS 2 已发现，等待 /imu/data"));
        statusLabel_->setText(QStringLiteral("等待 /imu/data"));
    }
}

void MainWindow::pollProfile()
{
    if (!ros2Available_ || !rosDiscovered_ || profileProcess_) return;
    profileProcess_ = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                               << QStringLiteral("--once") << QStringLiteral("/laser_profile/display_points")
                               << QStringLiteral("sensor_msgs/msg/PointCloud2")
                               << QStringLiteral("--qos-reliability") << QStringLiteral("best_effort")
                               << QStringLiteral("--qos-durability") << QStringLiteral("volatile")
                               << QStringLiteral("--no-daemon"));
    if (!profileProcess_) return;
    connect(profileProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::profileProcessFinished);
    connect(profileProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            statusBar()->showMessage(QStringLiteral("轮廓数据读取失败：ROS CLI 启动失败"), 5000);
        }
    });
    profileTimeoutTimer_.start(2500);
}

void MainWindow::profileProcessFinished(int, QProcess::ExitStatus)
{
    profileTimeoutTimer_.stop();
    if (!profileProcess_) return;
    const QByteArray output = profileProcess_->readAllStandardOutput();
    const QByteArray errors = profileProcess_->readAllStandardError();
    if (!output.trimmed().isEmpty()) parseProfile(output);
    if (!errors.trimmed().isEmpty()) {
        statusBar()->showMessage(QString::fromLocal8Bit(errors).simplified(), 4000);
    }
    profileProcess_ = nullptr;
}

void MainWindow::profileProcessTimeout()
{
    profileTimeoutTimer_.stop();
    if (!profileProcess_) return;
    profileProcess_->kill();
    profileProcess_ = nullptr;
}

void MainWindow::pollJointState()
{
    if (!ros2Available_ || !rosDiscovered_ || jointProcess_) return;
    jointProcess_ = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                              << QStringLiteral("--once") << QStringLiteral("/drive/joint_states")
                              << QStringLiteral("sensor_msgs/msg/JointState")
                              << QStringLiteral("--no-daemon"));
    if (!jointProcess_) return;
    connect(jointProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::jointProcessFinished);
    jointTimeoutTimer_.start(2500);
}

void MainWindow::jointProcessFinished(int, QProcess::ExitStatus)
{
    jointTimeoutTimer_.stop();
    if (!jointProcess_) return;
    const QByteArray output = jointProcess_->readAllStandardOutput();
    const QByteArray errors = jointProcess_->readAllStandardError();
    jointProcess_ = nullptr;
    if (!output.trimmed().isEmpty()) parseJointState(output);
    if (!errors.trimmed().isEmpty()) {
        statusBar()->showMessage(QString::fromLocal8Bit(errors).simplified(), 3000);
    }
}

void MainWindow::jointProcessTimeout()
{
    jointTimeoutTimer_.stop();
    if (!jointProcess_) return;
    jointProcess_->kill();
    jointProcess_ = nullptr;
}

void MainWindow::parseStatus(const QByteArray &output)
{
    lastStatusOutput_ = output;
    const QString text = QString::fromLocal8Bit(output);
    contourValid_ = statusBool(text, QStringLiteral("contour_valid"));
    geometryValid_ = statusBool(text, QStringLiteral("geometry_valid"));
    autoEnabled_ = statusBool(text, QStringLiteral("active"), autoEnabled_);
    contourLateral_ = statusNumber(text, QStringLiteral("contour_lateral_m"));
    modeLabel_->setText(autoEnabled_ ? QStringLiteral("自动纠偏：开启") : QStringLiteral("自动纠偏：关闭"));
    statusLabel_->setText(!contourValid_ ? QStringLiteral("等待有效轮廓") :
                          !geometryValid_ ? QStringLiteral("轮廓正常，正在建立轨迹") :
                          QStringLiteral("轨迹纠偏正常"));
    errorLabel_->setText(QStringLiteral("%1 m").arg(statusNumber(text, QStringLiteral("lateral_error_m")), 0, 'f', 5));
    previewErrorLabel_->setText(QStringLiteral("%1 m").arg(statusNumber(text, QStringLiteral("preview_lateral_error_m")), 0, 'f', 5));
    headingLabel_->setText(QStringLiteral("%1 deg").arg(statusNumber(text, QStringLiteral("heading_error_rad")) * 57.2957795, 0, 'f', 2));
    headingReferenceRad_ = statusNumber(text, QStringLiteral("heading_error_rad"));
    curvatureLabel_->setText(QStringLiteral("%1 1/m").arg(statusNumber(text, QStringLiteral("curvature_1pm")), 0, 'f', 3));
    angularAccelLabel_->setText(QStringLiteral("%1 rad/s²").arg(statusNumber(text, QStringLiteral("angular_accel_rad_s2")), 0, 'f', 3));
    linearCommandLabel_->setText(QStringLiteral("%1 m/s").arg(statusNumber(text, QStringLiteral("linear_command_m_s")), 0, 'f', 4));
    angularCommandLabel_->setText(QStringLiteral("%1 rad/s").arg(statusNumber(text, QStringLiteral("angular_command_rad_s")), 0, 'f', 3));
    confidenceLabel_->setText(QStringLiteral("%1").arg(statusNumber(text, QStringLiteral("confidence")), 0, 'f', 2));
    fitLabel_->setText(QStringLiteral("%1 m / %2 点").arg(statusNumber(text, QStringLiteral("fit_residual_m")), 0, 'f', 4)
                       .arg(statusValue(text, QStringLiteral("trajectory_points"))));
    plot_->setSeamLateral(contourLateral_, contourValid_);
    if (dialogPlot_) dialogPlot_->setSeamLateral(contourLateral_, contourValid_);
}

void MainWindow::parseImu(const QByteArray &output)
{
    lastImuOutput_ = output;
    const QString text = QString::fromLocal8Bit(output);
    const QRegularExpression orientationExpression(
        QStringLiteral("orientation:\\s*\\r?\\n\\s*x:\\s*([^\\r\\n]+)\\r?\\n\\s*y:\\s*([^\\r\\n]+)\\r?\\n\\s*z:\\s*([^\\r\\n]+)\\r?\\n\\s*w:\\s*([^\\r\\n]+)"));
    const QRegularExpressionMatch orientation = orientationExpression.match(text);
    if (!orientation.hasMatch()) return;

    bool ok = false;
    const double qx = orientation.captured(1).trimmed().toDouble(&ok);
    if (!ok) return;
    const double qy = orientation.captured(2).trimmed().toDouble(&ok);
    if (!ok) return;
    const double qz = orientation.captured(3).trimmed().toDouble(&ok);
    if (!ok) return;
    const double qw = orientation.captured(4).trimmed().toDouble(&ok);
    if (!ok) return;

    const double roll = std::atan2(2.0 * (qw * qx + qy * qz),
                                  1.0 - 2.0 * (qx * qx + qy * qy));
    const double pitch = std::asin(qBound(-1.0, 2.0 * (qw * qy - qz * qx), 1.0));
    const double yaw = std::atan2(2.0 * (qw * qz + qx * qy),
                                  1.0 - 2.0 * (qy * qy + qz * qz));
    errorLabel_->setText(QStringLiteral("%1 deg").arg(roll * 57.2957795, 0, 'f', 2));
    previewErrorLabel_->setText(QStringLiteral("%1 deg").arg(pitch * 57.2957795, 0, 'f', 2));
    headingLabel_->setText(QStringLiteral("%1 deg").arg(yaw * 57.2957795, 0, 'f', 2));
    const QRegularExpression angularZ(QStringLiteral("angular_velocity:\\s*\\r?\\n(?:.*\\r?\\n){2}\\s*z:\\s*([^\\r\\n]+)"));
    curvatureLabel_->setText(QStringLiteral("%1 rad/s").arg(angularZ.match(text).captured(1).trimmed().toDouble(), 0, 'f', 3));
    statusLabel_->setText(QStringLiteral("IMU 姿态已更新"));
}

void MainWindow::parseProfile(const QByteArray &output)
{
    lastProfileOutput_ = output;
    const QString text = QString::fromLocal8Bit(output);
    const QVector<int> bytes = statusBytes(text);
    if (bytes.isEmpty()) return;
    const int pointStep = qMax(1, qRound(statusNumber(text, QStringLiteral("point_step"), 12.0)));
    const int width = qMax(1, qRound(statusNumber(text, QStringLiteral("width"), bytes.size() / pointStep)));
    const int height = qMax(1, qRound(statusNumber(text, QStringLiteral("height"), 1.0)));
    const bool bigEndian = statusBool(text, QStringLiteral("is_bigendian"));
    const int pointCount = qMin(width * height, bytes.size() / pointStep);
    const int rowStep = qMax(width * pointStep,
                             qRound(statusNumber(text, QStringLiteral("row_step"),
                                                  width * pointStep)));
    QVector<QVector<QPointF>> rows(height);
    for (int index = 0; index < pointCount; ++index) {
        const int row = qBound(0, (index * pointStep) / rowStep, height - 1);
        const int rowOffset = row * rowStep + (index % width) * pointStep;
        float lateral = 0.0f;
        float heightValue = 0.0f;
        if (!readFloat32(bytes, rowOffset + 4, bigEndian, lateral) ||
            !readFloat32(bytes, rowOffset + 8, bigEndian, heightValue)) continue;
        if (std::abs(static_cast<double>(lateral)) < 1000.0 &&
            std::abs(static_cast<double>(heightValue)) < 1000.0) {
            rows[row].push_back(QPointF(lateral, heightValue));
        }
    }
    // A point-cloud image contains several profile rows. Display the row with
    // the strongest usable lateral span instead of interleaving all rows.
    QVector<QPointF> points;
    double bestSpan = -1.0;
    for (QVector<QPointF> &candidate : rows) {
        if (candidate.size() < 2) continue;
        std::sort(candidate.begin(), candidate.end(), [](const QPointF &first, const QPointF &second) {
            return first.x() < second.x();
        });
        const double span = candidate.last().x() - candidate.first().x();
        if (span > bestSpan) {
            bestSpan = span;
            points = candidate;
        }
    }
    std::sort(points.begin(), points.end(), [](const QPointF &first, const QPointF &second) {
        return first.x() < second.x();
    });
    contourPoints_ = points;
    plot_->setPoints(points);
    if (dialogPlot_) dialogPlot_->setPoints(points);
}

void MainWindow::parseJointState(const QByteArray &output)
{
    lastJointOutput_ = output;
    const QString text = QString::fromLocal8Bit(output);
    const QRegularExpression namesExpression(
        QStringLiteral("name:\\s*\\r?\\n((?:\\s*-.*\\r?\\n)+)"));
    const auto parseList = [](const QString &block) {
        QVector<double> values;
        const QRegularExpression valueExpression(QStringLiteral("-\\s*([-+0-9.eE]+)"));
        auto iterator = valueExpression.globalMatch(block);
        while (iterator.hasNext()) values.push_back(iterator.next().captured(1).toDouble());
        return values;
    };
    QVector<QString> names;
    const auto nameMatch = namesExpression.match(text);
    if (nameMatch.hasMatch()) {
        const QRegularExpression nameExpression(QStringLiteral("-\\s*([^\\r\\n]+)"));
        auto iterator = nameExpression.globalMatch(nameMatch.captured(1));
        while (iterator.hasNext()) names.push_back(iterator.next().captured(1).trimmed());
    }
    const auto listFor = [&](const QString &key) {
        const QRegularExpression expression(
            QStringLiteral("%1:\\s*\\r?\\n((?:\\s*-.*\\r?\\n)+)").arg(key));
        const auto match = expression.match(text);
        return match.hasMatch() ? parseList(match.captured(1)) : QVector<double>();
    };
    const QVector<double> positions = listFor(QStringLiteral("position"));
    const QVector<double> velocities = listFor(QStringLiteral("velocity"));
    if (names.isEmpty()) return;
    for (int index = 0; index < names.size(); ++index) {
        const double position = index < positions.size() ? positions[index] : 0.0;
        const double velocity = index < velocities.size() ? velocities[index] : 0.0;
        if (names[index] == QStringLiteral("left_drive_wheel_joint")) {
            leftJointValid_ = true;
            leftJointPosition_ = position;
            leftJointVelocity_ = velocity;
            leftJointEffort_ = 0.0;
        } else if (names[index] == QStringLiteral("right_drive_wheel_joint")) {
            rightJointValid_ = true;
            rightJointPosition_ = position;
            rightJointVelocity_ = velocity;
            rightJointEffort_ = 0.0;
        }
    }
    const auto formatWheel = [](bool valid, double velocity, double position) {
        return valid ? QStringLiteral("v=%1 m/s p=%2 rad")
                            .arg(velocity, 0, 'f', 3).arg(position, 0, 'f', 3)
                     : QStringLiteral("--");
    };
    if (motorLabel_) {
        motorLabel_->setText(QStringLiteral("电机状态: 左轮 %1 | 右轮 %2")
                             .arg(formatWheel(leftJointValid_, leftJointVelocity_, leftJointPosition_))
                             .arg(formatWheel(rightJointValid_, rightJointVelocity_, rightJointPosition_)));
    }
}

void MainWindow::commandTick()
{
    if (emergencyLatched_ || autoEnabled_) return;
    updateCommandFromKeys();
}

void MainWindow::updateCommandFromKeys()
{
    double linear = 0.0;
    double angular = 0.0;
    const bool forward = pressedKeys_.contains(QStringLiteral("up"));
    const bool reverse = pressedKeys_.contains(QStringLiteral("down"));
    const bool turning = pressedKeys_.contains(QStringLiteral("left")) ||
                         pressedKeys_.contains(QStringLiteral("right"));
    if (forward && !reverse) linear += linearSpeedSpin_->value();
    else if (reverse && !forward) linear -= linearSpeedSpin_->value();
    else if (turning) {
        // A turn button is an arc turn: both wheels keep moving forward, and
        // the base node creates the speed difference from angular velocity.
        linear += linearSpeedSpin_->value();
    }
    if (pressedKeys_.contains(QStringLiteral("left"))) angular += angularSpeedSpin_->value();
    if (pressedKeys_.contains(QStringLiteral("right"))) angular -= angularSpeedSpin_->value();
    publishTwist(linear, angular);
}

void MainWindow::setPressed(const QString &key, bool pressed)
{
    if (key == QStringLiteral("stop")) {
        pressedKeys_.clear();
        publishTwist(0.0, 0.0);
        return;
    }
    if (pressed) pressedKeys_.insert(key);
    else pressedKeys_.remove(key);

    // Button and keyboard events should update /cmd_vel immediately. The
    // timer remains as a heartbeat for held keys and stale-input protection.
    if (!emergencyLatched_ && !autoEnabled_) updateCommandFromKeys();
}

void MainWindow::drivePressed()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button) setPressed(button->property("driveKey").toString(), true);
}

void MainWindow::driveReleased()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button) setPressed(button->property("driveKey").toString(), false);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    const QString key = event->key() == Qt::Key_W || event->key() == Qt::Key_Up ? QStringLiteral("up") :
                        event->key() == Qt::Key_S || event->key() == Qt::Key_Down ? QStringLiteral("down") :
                        event->key() == Qt::Key_A || event->key() == Qt::Key_Left ? QStringLiteral("left") :
                        event->key() == Qt::Key_D || event->key() == Qt::Key_Right ? QStringLiteral("right") :
                        event->key() == Qt::Key_Space ? QStringLiteral("stop") : QString();
    if (!key.isEmpty()) {
        setPressed(key, true);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    const QString key = event->key() == Qt::Key_W || event->key() == Qt::Key_Up ? QStringLiteral("up") :
                        event->key() == Qt::Key_S || event->key() == Qt::Key_Down ? QStringLiteral("down") :
                        event->key() == Qt::Key_A || event->key() == Qt::Key_Left ? QStringLiteral("left") :
                        event->key() == Qt::Key_D || event->key() == Qt::Key_Right ? QStringLiteral("right") : QString();
    if (!key.isEmpty()) {
        setPressed(key, false);
        event->accept();
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::applyDomain()
{
    saveSettings();
    statusBar()->showMessage(QStringLiteral("网络参数已应用：ROS_DOMAIN_ID=%1").arg(domainEdit_->text()), 3000);
    if (discoveryProcess_) discoveryProcess_->kill();
    if (statusProcess_) statusProcess_->kill();
    if (profileProcess_) profileProcess_->kill();
    discoveryProcess_ = nullptr;
    statusProcess_ = nullptr;
    profileProcess_ = nullptr;
    rosDiscovered_ = false;
    connectionLabel_->setText(QStringLiteral("等待 ROS 2 发现"));
    statusLabel_->setText(QStringLiteral("等待目标机 ROS 2"));
    pollDiscovery();
}

void MainWindow::toggleDrive()
{
    const bool enabled = !manualEnabled_;
    callService(QStringLiteral("/drive/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                QStringLiteral("{data: %1}").arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
    manualEnabled_ = enabled;
    statusBar()->showMessage(enabled ? QStringLiteral("底盘使能请求已发送") : QStringLiteral("底盘禁用请求已发送"), 3000);
}

void MainWindow::toggleAuto()
{
    if (autoEnabled_) stopAutoWalk();
    else startAutoWalk();
}

void MainWindow::startAutoWalk()
{
    if (emergencyLatched_) return;
    pressedKeys_.clear();
    publishTwist(0.0, 0.0);
    if (!manualEnabled_) {
        callService(QStringLiteral("/drive/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                    QStringLiteral("{data: true}"));
        manualEnabled_ = true;
    }
    callService(QStringLiteral("/laser_correction/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                QStringLiteral("{data: true}"));
    autoEnabled_ = true;
    modeLabel_->setText(QStringLiteral("自动纠偏行走：开启"));
    statusBar()->showMessage(QStringLiteral("自动纠偏行走请求已发送，等待有效轮廓"), 4000);
}

void MainWindow::stopAutoWalk()
{
    pressedKeys_.clear();
    callService(QStringLiteral("/laser_correction/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                QStringLiteral("{data: false}"));
    publishTwist(0.0, 0.0);
    callService(QStringLiteral("/drive/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                QStringLiteral("{data: false}"));
    autoEnabled_ = false;
    manualEnabled_ = false;
    modeLabel_->setText(QStringLiteral("自动纠偏行走：已停止"));
    statusBar()->showMessage(QStringLiteral("自动行走已停止，底盘已禁用"), 4000);
}

void MainWindow::captureLocalization()
{
    const QString reference = QStringLiteral(
        "{contour_lateral_m: %1, heading_reference_rad: %2}")
        .arg(contourLateral_, 0, 'f', 6).arg(headingReferenceRad_, 0, 'f', 6);
    callService(QStringLiteral("/laser_correction/set_reference"),
                QStringLiteral("crawling_robot_interfaces/srv/SetLocalizationReference"), reference);
    QJsonObject root;
    root.insert(QStringLiteral("captured_at"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("ros_domain_id"), domainEdit_ ? domainEdit_->text() : QStringLiteral("0"));
    QJsonObject drive;
    drive.insert(QStringLiteral("enabled"), manualEnabled_);
    drive.insert(QStringLiteral("auto_enabled"), autoEnabled_);
    drive.insert(QStringLiteral("max_linear_speed_m_s"), linearSpeedSpin_->value());
    drive.insert(QStringLiteral("max_angular_speed_rad_s"), angularSpeedSpin_->value());
    drive.insert(QStringLiteral("max_linear_accel_m_s2"), linearAccelSpin_->value());
    drive.insert(QStringLiteral("max_angular_accel_rad_s2"), angularAccelSpin_->value());
    drive.insert(QStringLiteral("max_wheel_speed_m_s"), maxWheelSpeedSpin_->value());
    drive.insert(QStringLiteral("minimum_inner_wheel_ratio"), minimumInnerWheelRatioSpin_->value());
    drive.insert(QStringLiteral("left_motor_id"), leftMotorIdSpin_->value());
    drive.insert(QStringLiteral("right_motor_id"), rightMotorIdSpin_->value());
    drive.insert(QStringLiteral("left_motor_sign"), leftMotorSignCombo_->currentData().toInt());
    drive.insert(QStringLiteral("right_motor_sign"), rightMotorSignCombo_->currentData().toInt());
    root.insert(QStringLiteral("drive"), drive);
    QJsonObject referenceObject;
    referenceObject.insert(QStringLiteral("contour_lateral_m"), contourLateral_);
    referenceObject.insert(QStringLiteral("heading_reference_rad"), headingReferenceRad_);
    root.insert(QStringLiteral("localization_reference"), referenceObject);
    root.insert(QStringLiteral("imu_echo"), QString::fromLocal8Bit(lastImuOutput_));
    root.insert(QStringLiteral("laser_profile_echo"), QString::fromLocal8Bit(lastProfileOutput_));
    root.insert(QStringLiteral("correction_status_echo"), QString::fromLocal8Bit(lastStatusOutput_));
    const QString logDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logDir);
    const QString path = QDir(logDir).filePath(QStringLiteral("localization_reference_%1.json")
                                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hHmmss"))));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        statusBar()->showMessage(QStringLiteral("定位感知已记录：%1").arg(path), 5000);
    } else {
        statusBar()->showMessage(QStringLiteral("定位感知记录写入失败：%1").arg(path), 5000);
    }
}

void MainWindow::resetCorrection()
{
    callService(QStringLiteral("/laser_correction/reset"), QStringLiteral("std_srvs/srv/Trigger"), QStringLiteral("{}"));
}

void MainWindow::emergencyStop()
{
    emergencyLatched_ = true;
    manualEnabled_ = false;
    autoEnabled_ = false;
    pressedKeys_.clear();
    callService(QStringLiteral("/drive/enable"), QStringLiteral("std_srvs/srv/SetBool"), QStringLiteral("{data: false}"));
    callService(QStringLiteral("/laser_correction/enable"), QStringLiteral("std_srvs/srv/SetBool"), QStringLiteral("{data: false}"));
    publishTwist(0.0, 0.0);
    modeLabel_->setText(QStringLiteral("急停锁定"));
    statusBar()->showMessage(QStringLiteral("急停已触发"));
}

void MainWindow::releaseEmergencyStop()
{
    emergencyLatched_ = false;
    statusBar()->showMessage(QStringLiteral("急停已解除，请重新底盘使能"), 4000);
}

void MainWindow::openContourDialog()
{
    if (!contourDialog_) {
        contourDialog_ = new QDialog(this);
        contourDialog_->setWindowTitle(QStringLiteral("线激光轮廓成像"));
        contourDialog_->resize(820, 600);
        QVBoxLayout *layout = new QVBoxLayout(contourDialog_);
        dialogPlot_ = new ContourPlot(contourDialog_);
        dialogPlot_->setPoints(contourPoints_);
        dialogPlot_->setSeamLateral(contourLateral_, contourValid_);
        layout->addWidget(dialogPlot_);
        connect(contourDialog_, &QObject::destroyed, this, [this]() {
            contourDialog_ = nullptr;
            dialogPlot_ = nullptr;
        });
    }
    contourDialog_->show();
    contourDialog_->raise();
    contourDialog_->activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    if (autoEnabled_) stopAutoWalk();
    else if (manualEnabled_) {
        callService(QStringLiteral("/drive/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                    QStringLiteral("{data: false}"));
    }
    publishTwist(0.0, 0.0);
    event->accept();
}
