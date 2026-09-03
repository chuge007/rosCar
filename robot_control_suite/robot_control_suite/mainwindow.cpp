#include "mainwindow.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPolygonF>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
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
    environment.insert(QStringLiteral("ROS_LOCALHOST_ONLY"), QStringLiteral("0"));
    environment.insert(QStringLiteral("ROS_AUTOMATIC_DISCOVERY_RANGE"), QStringLiteral("SUBNET"));
    // The ROS 2 CLI daemon survives between app launches and keeps the old
    // network interface selection. Run each command with this launch's DDS
    // settings instead of querying that stale daemon.
    environment.insert(QStringLiteral("ROS2CLI_DISABLE_DAEMON"), QStringLiteral("1"));
    const QString fastDdsProfile = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("fastdds_profile.xml"));
    if (QFileInfo(fastDdsProfile).exists()) {
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
    setWindowTitle(QStringLiteral("爬壁机器人控制台"));
    resize(1120, 720);
    buildUi();
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
    connect(&discoveryTimeoutTimer_, &QTimer::timeout, this, &MainWindow::discoveryProcessTimeout);
    connect(&statusTimeoutTimer_, &QTimer::timeout, this, &MainWindow::statusProcessTimeout);
    connect(&profileTimeoutTimer_, &QTimer::timeout, this, &MainWindow::profileProcessTimeout);
    commandTimer_.start(100);
    discoveryTimer_.start(1200);
    statusTimer_.start(1000);
    profileTimer_.start(500);
    pollDiscovery();
}

MainWindow::~MainWindow()
{
    publishTwist(0.0, 0.0);
    if (discoveryProcess_) discoveryProcess_->kill();
    if (statusProcess_) statusProcess_->kill();
    if (profileProcess_) profileProcess_->kill();
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
    addDrive(QStringLiteral("前进"), QStringLiteral("up"), 0, 1);
    addDrive(QStringLiteral("左转"), QStringLiteral("left"), 1, 0);
    addDrive(QStringLiteral("停止"), QStringLiteral("stop"), 1, 1);
    addDrive(QStringLiteral("右转"), QStringLiteral("right"), 1, 2);
    addDrive(QStringLiteral("后退"), QStringLiteral("down"), 2, 1);
    leftLayout->addWidget(driveBox);

    QGroupBox *speedBox = new QGroupBox(QStringLiteral("速度控制"));
    QFormLayout *speedLayout = new QFormLayout(speedBox);
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(5, 120);
    speedSlider_->setValue(25);
    speedSlider_->setToolTip(QStringLiteral("基础线速度，单位 mm/s"));
    turnSlider_ = new QSlider(Qt::Horizontal);
    turnSlider_->setRange(5, 80);
    turnSlider_->setValue(30);
    turnSlider_->setToolTip(QStringLiteral("手动转向角速度，单位 0.01 rad/s"));
    speedLayout->addRow(QStringLiteral("基础速度 (mm/s)"), speedSlider_);
    speedLayout->addRow(QStringLiteral("转向角速度 (0.01 rad/s)"), turnSlider_);
    leftLayout->addWidget(speedBox);

    QHBoxLayout *actions = new QHBoxLayout;
    QPushButton *drive = new QPushButton(QStringLiteral("底盘使能"));
    QPushButton *automatic = new QPushButton(QStringLiteral("自动纠偏"));
    QPushButton *reset = new QPushButton(QStringLiteral("复位滤波"));
    QPushButton *contour = new QPushButton(QStringLiteral("打开轮廓成像"));
    actions->addWidget(drive);
    actions->addWidget(automatic);
    actions->addWidget(reset);
    actions->addWidget(contour);
    leftLayout->addLayout(actions);
    connect(drive, &QPushButton::clicked, this, &MainWindow::toggleDrive);
    connect(automatic, &QPushButton::clicked, this, &MainWindow::toggleAuto);
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
    QGroupBox *statusBox = new QGroupBox(QStringLiteral("纠偏状态"));
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
    statusForm->addRow(QStringLiteral("模式"), modeLabel_);
    statusForm->addRow(QStringLiteral("状态"), statusLabel_);
    statusForm->addRow(QStringLiteral("横向误差"), errorLabel_);
    statusForm->addRow(QStringLiteral("前视误差"), previewErrorLabel_);
    statusForm->addRow(QStringLiteral("角度误差"), headingLabel_);
    statusForm->addRow(QStringLiteral("轨迹曲率"), curvatureLabel_);
    statusForm->addRow(QStringLiteral("角加速度"), angularAccelLabel_);
    statusForm->addRow(QStringLiteral("实际线速度"), linearCommandLabel_);
    statusForm->addRow(QStringLiteral("实际角速度"), angularCommandLabel_);
    statusForm->addRow(QStringLiteral("轮廓置信度"), confidenceLabel_);
    statusForm->addRow(QStringLiteral("拟合残差"), fitLabel_);
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
        "QLineEdit{background:#1d2933;border:1px solid #405563;border-radius:3px;padding:6px;}"
        "QSlider::groove:horizontal{height:5px;background:#30414d;}"
        "QSlider::handle:horizontal{width:15px;margin:-5px 0;border-radius:7px;background:#4ed7e8;}"
        "QStatusBar{color:#8ea0ad;}"));
}

QProcess *MainWindow::startCli(const QStringList &arguments, bool includeOverlay)
{
    if (!ros2Available_ || ros2Root_.isEmpty() || ros2Python_.isEmpty() || ros2Script_.isEmpty()) {
        return nullptr;
    }
    QProcess *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    configureRosEnvironment(environment);
    process->setProcessEnvironment(environment);

    const QString setupBat = resolveSetupBat(ros2Root_);
    const QString overlaySetupBat = includeOverlay ? resolveSetupBat(ros2Overlay_) : QString();
    if (setupBat.isEmpty()) {
        process->deleteLater();
        return nullptr;
    }
    QString command = QStringLiteral("call %1").arg(cmdQuote(setupBat));
    if (!overlaySetupBat.isEmpty()) {
        command += QStringLiteral(" && call %1").arg(cmdQuote(overlaySetupBat));
    }
    command += QStringLiteral(" && set PATH=%ROBOT_CONTROL_ROS_PATH%;%PATH%");
    command += QStringLiteral(" && %1 %2").arg(cmdQuote(ros2Python_), cmdQuote(ros2Script_));
    if (!arguments.isEmpty()) {
        command += QStringLiteral(" ");
        command += joinCmdArguments(arguments);
    }
    process->start(QStringLiteral("cmd.exe"), QStringList()
                   << QStringLiteral("/d")
                   << QStringLiteral("/c")
                   << command);
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
    const QString payload = QStringLiteral(
        "{linear: {x: %1, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: %2}}")
        .arg(linear, 0, 'f', 5).arg(angular, 0, 'f', 5);
    startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("pub") << QStringLiteral("--once")
             << QStringLiteral("/cmd_vel") << QStringLiteral("geometry_msgs/msg/Twist") << payload
             << QStringLiteral("--no-daemon"));
}

void MainWindow::callService(const QString &service, const QString &type, const QString &request)
{
    QProcess *process = startCli(QStringList() << QStringLiteral("service") << QStringLiteral("call")
                                 << service << type << request << QStringLiteral("--no-daemon"));
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

void MainWindow::discoveryProcessFinished(int, QProcess::ExitStatus)
{
    discoveryTimeoutTimer_.stop();
    if (!discoveryProcess_) return;
    const QByteArray output = discoveryProcess_->readAllStandardOutput();
    const QByteArray errors = discoveryProcess_->readAllStandardError();
    discoveryProcess_ = nullptr;
    if (!errors.trimmed().isEmpty()) {
        statusBar()->showMessage(QString::fromLocal8Bit(errors).simplified(), 4000);
    }
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
}

void MainWindow::pollStatus()
{
    if (!ros2Available_ || !rosDiscovered_ || statusProcess_) return;
    statusProcess_ = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                              << QStringLiteral("--once") << QStringLiteral("/laser_correction/status")
                              << QStringLiteral("crawling_robot_interfaces/msg/LaserCorrectionStatus")
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
        parseStatus(output);
        connectionLabel_->setText(QStringLiteral("ROS 2 状态已连接"));
    } else if (rosDiscovered_) {
        connectionLabel_->setText(QStringLiteral("ROS 2 已发现，等待 /laser_correction/status"));
        statusLabel_->setText(QStringLiteral("等待 /laser_correction/status"));
    }
}

void MainWindow::statusProcessTimeout()
{
    statusTimeoutTimer_.stop();
    if (!statusProcess_) return;
    statusProcess_->kill();
    statusProcess_ = nullptr;
    if (rosDiscovered_) {
        connectionLabel_->setText(QStringLiteral("ROS 2 已发现，等待 /laser_correction/status"));
        statusLabel_->setText(QStringLiteral("等待 /laser_correction/status"));
    }
}

void MainWindow::pollProfile()
{
    if (!ros2Available_ || !rosDiscovered_ || profileProcess_) return;
    profileProcess_ = startCli(QStringList() << QStringLiteral("topic") << QStringLiteral("echo")
                               << QStringLiteral("--once") << QStringLiteral("/laser_profile/frame")
                               << QStringLiteral("crawling_robot_interfaces/msg/LaserProfile")
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

void MainWindow::parseStatus(const QByteArray &output)
{
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

void MainWindow::parseProfile(const QByteArray &output)
{
    const QString text = QString::fromLocal8Bit(output);
    const QVector<int> bytes = statusBytes(text);
    if (bytes.isEmpty()) return;
    const int pointStep = qMax(1, qRound(statusNumber(text, QStringLiteral("point_step"), 12.0)));
    const int width = qMax(1, qRound(statusNumber(text, QStringLiteral("width"), bytes.size() / pointStep)));
    const int height = qMax(1, qRound(statusNumber(text, QStringLiteral("height"), 1.0)));
    const bool bigEndian = statusBool(text, QStringLiteral("is_bigendian"));
    const int pointCount = qMin(width * height, bytes.size() / pointStep);
    QVector<QPointF> points;
    points.reserve(pointCount);
    for (int index = 0; index < pointCount; ++index) {
        float lateral = 0.0f;
        float heightValue = 0.0f;
        if (!readFloat32(bytes, index * pointStep + 4, bigEndian, lateral) ||
            !readFloat32(bytes, index * pointStep + 8, bigEndian, heightValue)) continue;
        if (std::isfinite(static_cast<double>(lateral)) &&
            std::isfinite(static_cast<double>(heightValue)) &&
            std::abs(static_cast<double>(lateral)) < 1000.0 &&
            std::abs(static_cast<double>(heightValue)) < 1000.0) {
            points.push_back(QPointF(lateral, heightValue));
        }
    }
    std::sort(points.begin(), points.end(), [](const QPointF &first, const QPointF &second) {
        return first.x() < second.x();
    });
    contourPoints_ = points;
    plot_->setPoints(points);
    if (dialogPlot_) dialogPlot_->setPoints(points);
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
    if (pressedKeys_.contains(QStringLiteral("up"))) linear += speedSlider_->value() / 1000.0;
    if (pressedKeys_.contains(QStringLiteral("down"))) linear -= speedSlider_->value() / 1000.0;
    if (pressedKeys_.contains(QStringLiteral("left"))) angular += turnSlider_->value() / 100.0;
    if (pressedKeys_.contains(QStringLiteral("right"))) angular -= turnSlider_->value() / 100.0;
    publishTwist(linear, angular);
}

void MainWindow::setPressed(const QString &key, bool pressed)
{
    if (key == QStringLiteral("stop")) pressedKeys_.clear();
    else if (pressed) pressedKeys_.insert(key);
    else pressedKeys_.remove(key);
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
    const bool enabled = !autoEnabled_;
    callService(QStringLiteral("/laser_correction/enable"), QStringLiteral("std_srvs/srv/SetBool"),
                QStringLiteral("{data: %1}").arg(enabled ? QStringLiteral("true") : QStringLiteral("false")));
    autoEnabled_ = enabled;
    modeLabel_->setText(enabled ? QStringLiteral("自动纠偏：开启") : QStringLiteral("自动纠偏：关闭"));
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
    publishTwist(0.0, 0.0);
    event->accept();
}
