#include "main_window.h"

#include "app_logger.h"
#include "synchronized_drive_controller.h"
#include "device_controller.h"
#include "point_cloud_view.h"
#include "usb_camera_controller.h"
#include "clamp_motor_controller.h"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace crawling {
namespace {

constexpr double kMillimetersPerMeter = 1000.0;
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr double kRadiansPerDegree = 0.017453292519943295;

QString motionKeyName(int key) {
  switch (key) {
    case 0: return QStringLiteral("forward");
    case 1: return QStringLiteral("reverse");
    case 2: return QStringLiteral("left");
    case 3: return QStringLiteral("right");
    default: return QStringLiteral("unknown");
  }
}
// A mouse click can deliver pressed/released before the 40 ms input timer
// runs. Keep a short command pulse for clicks, while preserving immediate
// stop behavior for a deliberate long press.

QLabel* makeMetricValue(QWidget* parent) {
  auto* label = new QLabel(CRAWLING_TEXT("--"), parent);
  label->setObjectName(CRAWLING_TEXT("metricValue"));
  label->setMinimumWidth(185);
  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return label;
}

QPushButton* makeDirectionButton(const QString& text, QWidget* parent) {
  auto* button = new QPushButton(text, parent);
  button->setObjectName(CRAWLING_TEXT("directionButton"));
  button->setMinimumSize(220, 72);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  button->setProperty("touch", true);
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

QPushButton* makeTouchButton(const QString& text, QWidget* parent,
                             const QString& objectName = {}) {
  auto* button = new QPushButton(text, parent);
  if (!objectName.isEmpty()) {
    button->setObjectName(objectName);
  }
  button->setMinimumSize(220, 56);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  button->setProperty("touch", true);
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

void setComboToValue(QComboBox* combo, int value) {
  const int index = combo->findData(value);
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }
}

void setComboToText(QComboBox* combo, const QString& value) {
  if (!combo || value.isEmpty()) {
    return;
  }
  int index = combo->findData(value);
  if (index < 0) {
    combo->addItem(value, value);
    index = combo->count() - 1;
  }
  combo->setCurrentIndex(index);
}

}  // namespace

MainWindow::MainWindow(SynchronizedDriveController* controller, DeviceController* devices,
                       UsbCameraController* usbCamera,
                       LaserCorrectionController* correction, QWidget* parent)
    : QMainWindow(parent), controller_(controller), devices_(devices),
      usbCamera_(usbCamera), correction_(correction) {
  clampMotors_ = new ClampMotorController(this);
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_ = DriveSettings::load(persistent);
  settings_.save(persistent);
  persistent.sync();
  buildInterface();
  settingsToUi(settings_);
  clampMotors_->setSettings(settings_);
  const QString savedPlane = persistent.value(CRAWLING_TEXT("pointCloud/plane"), CRAWLING_TEXT("xz")).toString().toLower();
  const int savedPlaneIndex = pointCloudPlaneBox_->findData(savedPlane);
  const int initialPlaneIndex = savedPlaneIndex >= 0 ? savedPlaneIndex
                                                      : pointCloudPlaneBox_->currentIndex();
  pointCloudPlaneBox_->setCurrentIndex(initialPlaneIndex);
  setPointCloudPlane(initialPlaneIndex);
  double correctionSpeedMps =
      persistent.value(CRAWLING_TEXT("laserCorrection/speed"), 0.005).toDouble();
  const int correctionSpeedDefaultVersion =
      persistent.value(CRAWLING_TEXT("laserCorrection/speedDefaultVersion"), 1).toInt();
  if (correctionSpeedDefaultVersion < 2 &&
      qFuzzyCompare(correctionSpeedMps, 0.025)) {
    correctionSpeedMps = 0.005;
    persistent.setValue(CRAWLING_TEXT("laserCorrection/speed"),
                        correctionSpeedMps);
  }
  persistent.setValue(CRAWLING_TEXT("laserCorrection/speedDefaultVersion"), 2);
  double correctionSegmentM =
      persistent.value(CRAWLING_TEXT("laserCorrection/segmentLength"), 0.10)
          .toDouble();
  const int correctionSegmentDefaultVersion =
      persistent
          .value(CRAWLING_TEXT("laserCorrection/segmentDefaultVersion"), 1)
          .toInt();
  if (correctionSegmentDefaultVersion < 2 &&
      qFuzzyCompare(correctionSegmentM, 0.20)) {
    correctionSegmentM = 0.10;
    persistent.setValue(CRAWLING_TEXT("laserCorrection/segmentLength"),
                        correctionSegmentM);
  }
  persistent.setValue(CRAWLING_TEXT("laserCorrection/segmentDefaultVersion"),
                      2);
  // One segment is used for both the initial out-and-back survey and every
  // subsequent correction update. The controller adapts its sample spacing
  // and fit requirements for short segments.
  correctionSegmentM = std::clamp(correctionSegmentM, 0.02, 2.0);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/segmentLength"),
                      correctionSegmentM);
  persistent.sync();
  correctionSpeedBox_->setValue(correctionSpeedMps * kMillimetersPerMeter);
  correctionSegmentBox_->setValue(correctionSegmentM * kMillimetersPerMeter);
  correctionKpBox_->setValue(persistent.value(CRAWLING_TEXT("laserCorrection/kp"), correctionKpBox_->value()).toDouble());
  double correctionKd =
      persistent.value(CRAWLING_TEXT("laserCorrection/kd"), 0.12).toDouble();
  const int correctionKdDefaultVersion =
      persistent.value(CRAWLING_TEXT("laserCorrection/kdDefaultVersion"), 1)
          .toInt();
  if (correctionKdDefaultVersion < 2 && qFuzzyCompare(correctionKd, 0.35)) {
    correctionKd = 0.12;
    persistent.setValue(CRAWLING_TEXT("laserCorrection/kd"), correctionKd);
  }
  persistent.setValue(CRAWLING_TEXT("laserCorrection/kdDefaultVersion"), 2);
  persistent.sync();
  correctionKdBox_->setValue(correctionKd);
  parameterStatusLabel_->setText(CRAWLING_TEXT("参数已加载"));
  bindController();
  refreshPorts();
  motionClock_.start();

  inputTimer_ = new QTimer(this);
  inputTimer_->setTimerType(Qt::PreciseTimer);
  inputTimer_->setInterval(40);
  connect(inputTimer_, &QTimer::timeout, this, &MainWindow::updateManualCommand);
  inputTimer_->start();

  restoreGeometry(persistent.value(CRAWLING_TEXT("window/geometry")).toByteArray());
  updateState(DriveState::Disconnected, QStringLiteral("Connect the MWD RS485 motor bus to begin."));
  QTimer::singleShot(0, this, [this] {
    if (settings_.autoConnectOnStartup) connectConfiguredDevices();
  });
}

void MainWindow::shutdownControl() {
  if (!controller_ || isClosing_) {
    return;
  }
  isClosing_ = true;
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  if (inputTimer_) {
    inputTimer_->stop();
  }
  if (correction_) {
    QMetaObject::invokeMethod(correction_, "shutdown", Qt::BlockingQueuedConnection);
  }
  QMetaObject::invokeMethod(controller_, "shutdown", Qt::BlockingQueuedConnection);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }
  switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
      setMotionKey(MotionKey::Forward, true);
      break;
    case Qt::Key_S:
    case Qt::Key_Down:
      setMotionKey(MotionKey::Reverse, true);
      break;
    case Qt::Key_A:
    case Qt::Key_Left:
      setMotionKey(MotionKey::Left, true);
      break;
    case Qt::Key_D:
    case Qt::Key_Right:
      setMotionKey(MotionKey::Right, true);
      break;
    case Qt::Key_Space:
      emergencyStop();
      break;
    default:
      QMainWindow::keyPressEvent(event);
      return;
  }
  event->accept();
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    event->accept();
    return;
  }
  switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
      setMotionKey(MotionKey::Forward, false);
      break;
    case Qt::Key_S:
    case Qt::Key_Down:
      setMotionKey(MotionKey::Reverse, false);
      break;
    case Qt::Key_A:
    case Qt::Key_Left:
      setMotionKey(MotionKey::Left, false);
      break;
    case Qt::Key_D:
    case Qt::Key_Right:
      setMotionKey(MotionKey::Right, false);
      break;
    default:
      QMainWindow::keyReleaseEvent(event);
      return;
  }
  event->accept();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  persistent.setValue(CRAWLING_TEXT("window/geometry"), saveGeometry());
  saveSettings();
  shutdownControl();
  event->accept();
}

void MainWindow::refreshPorts() {
  const QString selectedLeft = leftMotorPortBox_->currentData().toString();
  const QString selectedRight = rightMotorPortBox_->currentData().toString();
  const QString selectedImu = imuPortBox_->currentData().toString();
  const QString selectedClamp = clampSerialPortBox_->currentData().toString();
  const auto ports = QSerialPortInfo::availablePorts();
  const auto repopulate = [&ports](QComboBox* combo, const QString& selected,
                                   const QString& configured) {
    combo->clear();
    for (const QSerialPortInfo& port : ports) {
      const QString description = port.description().isEmpty() ? QString() :
          CRAWLING_TEXT(" (%1)").arg(port.description());
      combo->addItem(port.portName() + description, port.portName());
    }
    const QString desired = selected.isEmpty() ? configured : selected;
    if (desired.isEmpty()) {
      combo->setCurrentIndex(-1);
    } else {
      setComboToText(combo, desired);
    }
  };
  repopulate(leftMotorPortBox_, selectedLeft, settings_.leftMotorSerialPort);
  repopulate(rightMotorPortBox_, selectedRight, settings_.rightMotorSerialPort);
  repopulate(imuPortBox_, selectedImu, settings_.imuSerialPort);
  repopulate(clampSerialPortBox_, selectedClamp, settings_.clampSerialPort);
}

void MainWindow::connectAdapter() {
  const DriveSettings settings = settingsFromUi();
  const QString error = settings.validationError();
  if (!error.isEmpty()) {
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=connect_drive_request result=REJECTED reason=%1").arg(error));
    appendLog(error);
    setStatePresentation(DriveState::Fault, error);
    return;
  }
  settings_ = settings;
  saveSettings();
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=connect_drive_request target=DRIVE.CONTROL left_port=%1 right_port=%2")
                       .arg(settings_.leftMotorSerialPort, settings_.rightMotorSerialPort));
  emit enableRequested(false);
  emit connectionRequested(settings_);
}

void MainWindow::disconnectAdapter() {
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=disconnect_drive_request target=DRIVE.CONTROL"));
  emit enableRequested(false);
  emit disconnectRequested();
}

void MainWindow::connectAllConfiguredDevices() {
  connectConfiguredDevices();
}

void MainWindow::disconnectAllDevices() {
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=disconnect_all_request targets=DRIVE.MOTOR,IMU,CAMERA,USB_CAMERA"));
  disconnectAdapter();
  QMetaObject::invokeMethod(devices_, "disconnectImu", Qt::QueuedConnection);
  QMetaObject::invokeMethod(devices_, "disconnectCamera", Qt::QueuedConnection);
  QMetaObject::invokeMethod(usbCamera_, "disconnectCamera", Qt::QueuedConnection);
  appendLog(CRAWLING_TEXT("已请求断开电机、IMU、激光相机和 USB 摄像头"));
}

void MainWindow::connectConfiguredDevices() {
  settings_ = settingsFromUi();
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  persistent.sync();

  AppLogger::write(
      QStringLiteral("UI.OPERATION"),
      QStringLiteral("event=connect_all_request mode=%1 targets=DRIVE.MOTOR,IMU,CAMERA,USB_CAMERA "
                     "left_port=%2 right_port=%3 imu_port=%4 camera_serial=%5 usb_camera=%6")
          .arg(QStringLiteral("saved_configuration"))
          .arg(settings_.leftMotorSerialPort)
          .arg(settings_.rightMotorSerialPort)
          .arg(settings_.imuSerialPort)
          .arg(settings_.laserSerialNumber)
          .arg(settings_.usbCameraDeviceIndex));

  const QString driveError = settings_.validationError();
  bool driveReady = true;
  if (settings_.leftMotorSerialPort.trimmed().isEmpty() ||
      settings_.rightMotorSerialPort.trimmed().isEmpty()) {
    driveReady = false;
    const QString message = CRAWLING_TEXT("电机连接已跳过：左右电机串口配置不完整");
    appendLog(message);
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=connect_module_skipped module=DRIVE.MOTOR reason=missing_port"));
  } else if (!driveError.isEmpty()) {
    driveReady = false;
    appendLog(CRAWLING_TEXT("电机连接已跳过：%1").arg(driveError));
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=connect_module_skipped module=DRIVE.MOTOR reason=%1")
                           .arg(driveError));
  }

  const auto connectDrive = [this] {
    cancelMotionButtonPulses();
    activeMotionKeys_.clear();
    keyboardMotionKeys_.clear();
    emit enableRequested(false);
    emit connectionRequested(settings_);
  };

  if (driveReady) {
    connectDrive();
  }
  connectConfiguredImu();
  connectConfiguredCamera();
  connectConfiguredUsbCamera();
}

void MainWindow::connectConfiguredImu() {
  settings_ = settingsFromUi();
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  persistent.sync();
  const QString port = settings_.imuSerialPort.trimmed();
  if (port.isEmpty()) {
    appendLog(CRAWLING_TEXT("IMU 连接已跳过：未配置串口"));
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=connect_module_skipped module=IMU reason=missing_port"));
    return;
  }
  QMetaObject::invokeMethod(
      devices_, "connectImu", Qt::QueuedConnection, Q_ARG(QString, port),
      Q_ARG(int, settings_.imuBaudRate),
      Q_ARG(int, settings_.imuOutputDivider));
}

void MainWindow::scanConfiguredCamera() {
  QMetaObject::invokeMethod(devices_, "scanCamera", Qt::QueuedConnection);
}

void MainWindow::connectConfiguredCamera() {
  settings_ = settingsFromUi();
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  persistent.sync();
  const QString serial = settings_.laserSerialNumber.trimmed();
  if (serial.isEmpty()) {
    appendLog(CRAWLING_TEXT("激光相机连接已跳过：未配置序列号"));
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=connect_module_skipped module=CAMERA reason=missing_serial"));
    return;
  }
  if (cameraConnected_) {
    QMetaObject::invokeMethod(devices_, "disconnectCamera", Qt::QueuedConnection);
  }
  QMetaObject::invokeMethod(devices_, "connectCamera", Qt::QueuedConnection,
                            Q_ARG(QString, serial));
}

void MainWindow::connectConfiguredUsbCamera() {
  settings_ = settingsFromUi();
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  persistent.sync();
  if (!settings_.usbCameraAutoConnect) {
    appendLog(CRAWLING_TEXT("USB 摄像头连接已跳过：未勾选参与连接全部接口"));
    return;
  }
  if (settings_.usbCameraDeviceIndex < 0) {
    appendLog(CRAWLING_TEXT("USB 摄像头连接已跳过：未配置设备"));
    return;
  }
  QMetaObject::invokeMethod(
      usbCamera_, "connectCamera", Qt::QueuedConnection,
      Q_ARG(int, settings_.usbCameraDeviceIndex),
      Q_ARG(int, settings_.usbCameraFps),
      Q_ARG(bool, settings_.usbCameraFlipHorizontal),
      Q_ARG(bool, settings_.usbCameraFlipVertical));
}

void MainWindow::applyAllParameters() {
  const DriveSettings current = settingsFromUi();
  const QString error = current.validationError();
  if (!error.isEmpty()) {
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=apply_parameters result=REJECTED reason=%1").arg(error));
    if (parameterStatusLabel_) {
      parameterStatusLabel_->setText(CRAWLING_TEXT("应用失败：%1").arg(error));
    }
    appendLog(error);
    return;
  }

  settings_ = current;
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  LaserCorrectionSettings correctionSettings;
  correctionSettings.targetSpeedMps = correctionSpeedBox_->value() / kMillimetersPerMeter;
  correctionSettings.segmentLengthM = correctionSegmentBox_->value() / kMillimetersPerMeter;
  correctionSettings.proportionalGain = correctionKpBox_->value();
  correctionSettings.derivativeGain = correctionKdBox_->value();
  correctionSettings.wheelRadiusM = settings_.wheelRadiusM;
  correctionSettings.trackWidthM = settings_.trackWidthM;
  correctionSettings.minimumInnerWheelRatio = settings_.minimumInnerWheelRatio;
  persistent.setValue(CRAWLING_TEXT("laserCorrection/speed"), correctionSettings.targetSpeedMps);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/segmentLength"), correctionSettings.segmentLengthM);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/kp"), correctionSettings.proportionalGain);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/kd"), correctionSettings.derivativeGain);
  persistent.sync();
  if (correction_) {
    QMetaObject::invokeMethod(correction_, "setSettings", Qt::QueuedConnection,
                             Q_ARG(crawling::LaserCorrectionSettings, correctionSettings));
  }

  if (connected_) {
    connectConfiguredDevices();
  }
  const QString message = connected_
                              ? CRAWLING_TEXT("全部参数已保存并应用，电机、IMU 和相机已请求重新连接")
                              : CRAWLING_TEXT("全部参数已保存，将在连接设备时应用");
  if (parameterStatusLabel_) parameterStatusLabel_->setText(message);
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=apply_parameters result=OK reconnect_drive=%1 imu_port=%2 imu_baud=%3")
                       .arg(connected_).arg(settings_.imuSerialPort).arg(settings_.imuBaudRate));
  appendLog(message);
}

void MainWindow::saveSettings() {
  const DriveSettings current = settingsFromUi();
  if (!current.validationError().isEmpty()) {
    return;
  }
  settings_ = current;
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  settings_.save(persistent);
  persistent.sync();
}

void MainWindow::enableDrive() {
  saveSettings();
  if (!connected_) {
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=enable_drive_request result=REJECTED reason=not_connected"));
    appendLog(CRAWLING_TEXT("\xE8\xAF\xB7""\xE5\x85\x88""\xE8\xBF\x9E""\xE6\x8E\xA5""\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xEF\xBC\x8C""\xE5\x86\x8D""\xE4\xBD\xBF""\xE8\x83\xBD""\xE5\xBA\x95""\xE7\x9B\x98""\xE8\xBE\x93""\xE5\x87\xBA""\xE3\x80\x82"""));
    return;
  }
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=enable_drive_request target=DRIVE.CONTROL"));
  emit enableRequested(true);
}

void MainWindow::stopDrive() {
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=stop_drive_request target=DRIVE.CONTROL"));
  // Send zero motion before the stop command so no queued manual command can
  // retain a non-zero target while the controller transitions to Idle.
  emit commandRequested(0.0, 0.0);
  emit enableRequested(false);
}

void MainWindow::emergencyStop() {
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::warning(QStringLiteral("UI.OPERATION"),
                     QStringLiteral("event=emergency_stop_request target=DRIVE.CONTROL"));
  emit emergencyStopRequested();
}

void MainWindow::systemReset() {
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=system_reset_request target=DRIVE.CONTROL"));
  emit systemResetRequested();
}

void MainWindow::clearAlarm() {
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=clear_alarm_request target=DRIVE.CONTROL"));
  emit clearAlarmRequested();
}

void MainWindow::updateManualCommand() {
  if (autoCorrectionActive_) return;
  const DriveSettings settings = settingsFromUi();
  const double scale = speedSlider_->value() / 100.0;
  const int forward = motionKey(MotionKey::Forward) ? 1 : 0;
  const int reverse = motionKey(MotionKey::Reverse) ? 1 : 0;
  const int left = motionKey(MotionKey::Left) ? 1 : 0;
  const int right = motionKey(MotionKey::Right) ? 1 : 0;
  const double synchronizedWheelLimitMps =
      WheelMotorConfig::kMaximumSynchronizedMotorSpeedDps *
      settings.wheelRadiusM * kRadiansPerDegree /
      settings.motorOutputToWheelRatio;
  const double manualLinearLimitMps = std::min(
      settings.maximumLinearSpeedMps, synchronizedWheelLimitMps);
  const double manualAngularLimitRadps = std::min(
      settings.maximumAngularSpeedRadps,
      2.0 * synchronizedWheelLimitMps / settings.trackWidthM);
  // A standalone left/right button is a pivot. Holding forward or reverse
  // together with a turn button produces a translating arc instead.
  const double linear =
      (forward - reverse) * manualLinearLimitMps * scale;
  // Positive angular command denotes a right turn in the operator view:
  // the physical left wheel is the outer (faster) wheel. Therefore the
  // right button maps to positive angular speed and the left button negative.
  const double angular = (right - left) * manualAngularLimitRadps * scale;
  emit commandRequested(linear, angular);
}

void MainWindow::updateTelemetry(const DriveTelemetry& telemetry) {
  setValueLabel(targetValue_, CRAWLING_TEXT("\xE7\xBA\xBF""\xE9\x80\x9F""\xE5\xBA\xA6"" %1 mm/s    \xE8\xA7\x92""\xE9\x80\x9F""\xE5\xBA\xA6"" %2 \xE5\xBA\xA6/s")
                                   .arg(telemetry.targetLinearMps * kMillimetersPerMeter, 0, 'f', 1)
                                   .arg(telemetry.targetAngularRadps * kDegreesPerRadian, 0, 'f', 1));
  setValueLabel(appliedValue_, CRAWLING_TEXT("\xE7\xBA\xBF""\xE9\x80\x9F""\xE5\xBA\xA6"" %1 mm/s    \xE8\xA7\x92""\xE9\x80\x9F""\xE5\xBA\xA6"" %2 \xE5\xBA\xA6/s")
                                    .arg(telemetry.appliedLinearMps * kMillimetersPerMeter, 0, 'f', 1)
                                    .arg(telemetry.appliedAngularRadps * kDegreesPerRadian, 0, 'f', 1));
  setValueLabel(leftValue_, CRAWLING_TEXT("\xE7\x9B\xAE""\xE6\xA0\x87"" %1    \xE5\xAE\x9E""\xE9\x99\x85"" %2 mm/s")
                                  .arg(telemetry.leftTargetMps * kMillimetersPerMeter, 0, 'f', 1)
                                   .arg(telemetry.left.wheelSpeedMps * kMillimetersPerMeter, 0, 'f', 1));
  setValueLabel(rightValue_, CRAWLING_TEXT("\xE7\x9B\xAE""\xE6\xA0\x87"" %1    \xE5\xAE\x9E""\xE9\x99\x85"" %2 mm/s")
                                   .arg(telemetry.rightTargetMps * kMillimetersPerMeter, 0, 'f', 1)
                                   .arg(telemetry.right.wheelSpeedMps * kMillimetersPerMeter, 0, 'f', 1));
  setValueLabel(leftEncoderValue_, CRAWLING_TEXT("%1 rad    %2 度")
                                      .arg(telemetry.left.wheelPositionRad, 0, 'f', 3)
                                      .arg(telemetry.left.wheelPositionRad * kDegreesPerRadian, 0, 'f', 1));
  setValueLabel(rightEncoderValue_, CRAWLING_TEXT("%1 rad    %2 度")
                                       .arg(telemetry.right.wheelPositionRad, 0, 'f', 3)
                                       .arg(telemetry.right.wheelPositionRad * kDegreesPerRadian, 0, 'f', 1));
  setValueLabel(leftMotorValue_, CRAWLING_TEXT("转速 %1 度/s    电机控制量 %2")
                                    .arg(telemetry.left.motorSpeedDps, 0, 'f', 1)
                                    .arg(telemetry.left.motorControlValue, 0, 'f', 2));
  setValueLabel(rightMotorValue_, CRAWLING_TEXT("转速 %1 度/s    电机控制量 %2")
                                     .arg(telemetry.right.motorSpeedDps, 0, 'f', 1)
                                     .arg(telemetry.right.motorControlValue, 0, 'f', 2));
  setValueLabel(leftHealthValue_, CRAWLING_TEXT("反馈 %1    温度 %2 度C")
                                     .arg(telemetry.left.valid ? CRAWLING_TEXT("有效") : CRAWLING_TEXT("无效"))
                                     .arg(telemetry.left.temperatureC));
  setValueLabel(rightHealthValue_, CRAWLING_TEXT("反馈 %1    温度 %2 度C")
                                      .arg(telemetry.right.valid ? CRAWLING_TEXT("有效") : CRAWLING_TEXT("无效"))
                                      .arg(telemetry.right.temperatureC));
  setValueLabel(feedbackValue_, CRAWLING_TEXT("\xE5\x91\xBD""\xE4\xBB\xA4"" %1    \xE5\x8F\x8D""\xE9\xA6\x88"" %2")
                                     .arg(telemetry.commandFresh ? CRAWLING_TEXT("\xE6\xAD\xA3""\xE5\xB8\xB8""") : CRAWLING_TEXT("\xE8\xB6\x85""\xE6\x97\xB6"""))
                                     .arg(telemetry.feedbackFresh ? CRAWLING_TEXT("\xE6\xAD\xA3""\xE5\xB8\xB8""") : CRAWLING_TEXT("\xE8\xB6\x85""\xE6\x97\xB6""")));
  setValueLabel(syncValue_, CRAWLING_TEXT("\xE8\xAF\xAF""\xE5\xB7\xAE"" %1    \xE4\xBF\xAE""\xE6\xAD\xA3"" %2 m/s")
                                 .arg(telemetry.synchronizationError, 0, 'f', 3)
                                  .arg(telemetry.synchronizationCorrectionMps * kMillimetersPerMeter, 0, 'f', 1));
}

void MainWindow::updateImu(const ImuSample& sample) {
  setValueLabel(imuOrientationValue_, CRAWLING_TEXT("横滚 %1    俯仰 %2    航向 %3 度")
                                           .arg(sample.rollRad * kDegreesPerRadian, 0, 'f', 2)
                                           .arg(sample.pitchRad * kDegreesPerRadian, 0, 'f', 2)
                                           .arg(sample.yawRad * kDegreesPerRadian, 0, 'f', 2));
  setValueLabel(imuGyroValue_, CRAWLING_TEXT("X %1    Y %2    Z %3 度/s")
                                    .arg(sample.gyroXRadps * kDegreesPerRadian, 0, 'f', 2)
                                    .arg(sample.gyroYRadps * kDegreesPerRadian, 0, 'f', 2)
                                    .arg(sample.gyroZRadps * kDegreesPerRadian, 0, 'f', 2));
  setValueLabel(imuAccelerationValue_, CRAWLING_TEXT("X %1    Y %2    Z %3 m/s2")
                                            .arg(sample.accelerationXMps2, 0, 'f', 3)
                                            .arg(sample.accelerationYMps2, 0, 'f', 3)
                                            .arg(sample.accelerationZMps2, 0, 'f', 3));
}

void MainWindow::updateImuConnection(bool connected, const QString& message) {
  const QString stateText =
      message.isEmpty()
          ? (connected ? CRAWLING_TEXT("IMU 已连接") : CRAWLING_TEXT("IMU 未连接"))
          : message;
  if (imuStateValue_) {
    imuStateValue_->setText(stateText);
    imuStateValue_->setProperty("connected", connected);
    imuStateValue_->style()->unpolish(imuStateValue_);
    imuStateValue_->style()->polish(imuStateValue_);
  }
  if (imuConfigStateLabel_) imuConfigStateLabel_->setText(stateText);
  if (imuConnectButton_) imuConnectButton_->setEnabled(!connected);
}

void MainWindow::updateState(DriveState state, const QString& reason) {
  if (autoCorrectionActive_ && state != DriveState::Enabled) stopAutoCorrection();
  currentState_ = state;
  setStatePresentation(state, reason);
  enableButton_->setEnabled(connected_ && state != DriveState::Enabled &&
                            state != DriveState::Arming && state != DriveState::EmergencyStop);
  stopButton_->setEnabled(state == DriveState::Enabled || state == DriveState::Arming ||
                          state == DriveState::Fault || state == DriveState::EmergencyStop);
}

void MainWindow::updateConnection(bool connected, const QString& message) {
  connected_ = connected;
  connectionLabel_->setText(connected ? CRAWLING_TEXT("\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE5\xB7\xB2""\xE8\xBF\x9E""\xE6\x8E\xA5""")
                                      : CRAWLING_TEXT("\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE6\x9C\xAA""\xE8\xBF\x9E""\xE6\x8E\xA5"""));
  connectionLabel_->setProperty("connected", connected);
  connectionLabel_->style()->unpolish(connectionLabel_);
  connectionLabel_->style()->polish(connectionLabel_);
  enableButton_->setEnabled(connected_ && currentState_ != DriveState::Enabled &&
                            currentState_ != DriveState::Arming &&
                            currentState_ != DriveState::EmergencyStop);
  appendLog(message);
}

void MainWindow::appendLog(const QString& message) {
  if (!logOutput_ || message.isEmpty()) {
    return;
  }
  logOutput_->appendPlainText(QDateTime::currentDateTime().toString(CRAWLING_TEXT("HH:mm:ss.zzz  ")) + message);
}

void MainWindow::buildInterface() {
  setWindowTitle(CRAWLING_TEXT("\xE5\xB1\xA5""\xE5\xB8\xA6""\xE6\x9C\xBA""\xE5\x99\xA8""\xE4\xBA\xBA""\xE5\xBA\x95""\xE7\x9B\x98""\xE6\x8E\xA7""\xE5\x88\xB6""\xE5\x8F\xB0"""));
  setMinimumSize(1100, 720);
  setStyleSheet(CRAWLING_TEXT(
      "QMainWindow { background: #f3f5f7; color: #17212b; }"
      "QGroupBox { background: #ffffff; border: 1px solid #cbd3da; border-radius: 6px; "
      "margin-top: 12px; padding: 14px 10px 10px 10px; font-weight: 600; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
      "QPushButton { background: #ffffff; border: 1px solid #7b8794; border-radius: 4px; "
      "padding: 7px 12px; min-height: 22px; }"
      "QPushButton:hover { background: #e7f0f5; } QPushButton:pressed { background: #cfdde5; }"
      "QPushButton#enableButton { background: #0f766e; color: white; border-color: #0f766e; }"
      "QPushButton#stopButton { background: #b45309; color: white; border-color: #b45309; }"
      "QPushButton#emergencyButton { background: #b42318; color: white; border-color: #b42318; font-weight: 700; }"
      "QPushButton#directionButton { font-weight: 600; font-size: 13px; }"
      "QPushButton[touch=\"true\"] { min-width: 220px; min-height: 48px; font-size: 14px; font-weight: 600; padding: 4px 14px; }"
      "QPushButton#applyAllButton { background: #176b87; color: white; border-color: #176b87; font-size: 15px; font-weight: 700; }"
      "QLabel#metricValue { color: #0b4f6c; font-family: Consolas, monospace; }"
      "QLabel#controlSectionTitle { color: #24526b; font-weight: 700; padding-top: 4px; }"
      "QLabel#correctionStatus { background: #eef3f5; border: 1px solid #cbd3da; border-radius: 4px; padding: 8px; }"
      "QLabel#statusSectionTitle { color: #24526b; border-bottom: 1px solid #d4dde3; "
      "font-weight: 700; margin-top: 8px; padding: 5px 0 3px 0; }"
      "QLabel#stateLabel { border-radius: 4px; padding: 7px 10px; font-weight: 700; }"
      "QLabel#connectionLabel { border-radius: 4px; padding: 6px 9px; font-weight: 700; "
      "background: #e3e8ed; color: #46515d; }"
      "QLabel#connectionLabel[connected=\"true\"] { background: #d8f0e5; color: #12633d; }"
      "QPlainTextEdit { background: #17212b; color: #d9e2ec; border: 0; font-family: Consolas, monospace; }"
      "QTabWidget::pane { border: 1px solid #cbd3da; background: #f8fafb; }"
      "QTabBar::tab { padding: 8px 14px; background: #e4e9ed; border: 1px solid #cbd3da; }"
      "QTabBar::tab:selected { background: #ffffff; }"));

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(18, 16, 18, 18);
  root->setSpacing(12);

  auto* header = new QHBoxLayout();
  auto* title = new QLabel(CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98""\xE6\x8E\xA7""\xE5\x88\xB6"""), central);
  QFont titleFont = title->font();
  titleFont.setPointSize(18);
  titleFont.setBold(true);
  title->setFont(titleFont);
  header->addWidget(title);
  header->addStretch();
  connectionLabel_ = new QLabel(CRAWLING_TEXT("\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE6\x9C\xAA""\xE8\xBF\x9E""\xE6\x8E\xA5"""), central);
  connectionLabel_->setObjectName(CRAWLING_TEXT("connectionLabel"));
  header->addWidget(connectionLabel_);
  autoConnectCheckBox_ = new QCheckBox(CRAWLING_TEXT("启动时自动连接"), central);
  autoConnectCheckBox_->setToolTip(
      CRAWLING_TEXT("仅按上次保存的电机、IMU、激光相机和 USB 摄像头参数连接；"
                    "空配置或连接失败的设备会跳过，不会自动选择接口。"));
  header->addWidget(autoConnectCheckBox_);
  auto* connectAllButton =
      new QPushButton(CRAWLING_TEXT("手动连接全部接口"), central);
  connect(connectAllButton, &QPushButton::clicked, this,
          &MainWindow::connectAllConfiguredDevices);
  header->addWidget(connectAllButton);
  auto* disconnectAllButton =
      new QPushButton(CRAWLING_TEXT("断开全部接口"), central);
  connect(disconnectAllButton, &QPushButton::clicked, this,
          &MainWindow::disconnectAllDevices);
  header->addWidget(disconnectAllButton);
  stateLabel_ = new QLabel(central);
  stateLabel_->setObjectName(CRAWLING_TEXT("stateLabel"));
  header->addWidget(stateLabel_);
  root->addLayout(header);

  reasonLabel_ = new QLabel(central);
  reasonLabel_->setWordWrap(true);
  reasonLabel_->setStyleSheet(CRAWLING_TEXT("color: #4a5561;"));
  root->addWidget(reasonLabel_);

  auto* tabs = new QTabWidget(central);
  auto* controlScroll = new QScrollArea(tabs);
  controlScroll->setWidgetResizable(true);
  auto* controlPage = new QWidget(controlScroll);
  controlPage->setMinimumHeight(560);
  // Keep the original two-column console layout: manual controls on the left,
  // diagnostics and point-cloud views on the right.
  auto* controlLayout = new QHBoxLayout(controlPage);
  controlLayout->setContentsMargins(12, 12, 12, 12);
  controlLayout->setSpacing(12);

  auto* manualGroup = new QGroupBox(CRAWLING_TEXT("\xE6\x89\x8B""\xE5\x8A\xA8""\xE8\xBF\x90""\xE5\x8A\xA8"""), controlPage);
  auto* manualLayout = new QVBoxLayout(manualGroup);
  auto* instruction = new QLabel(CRAWLING_TEXT("\xE6\x8C\x89""\xE4\xBD\x8F"" W/A/S/D \xE6\x88\x96""\xE6\x96\xB9""\xE5\x90\x91""\xE9\x94\xAE""\xE6\x8E\xA7""\xE5\x88\xB6""\xEF\xBC\x9B""\xE6\x9D\xBE""\xE5\xBC\x80""\xE5\x90\x8E""\xE6\x8C\x89""\xE6\x96\x9C""\xE5\x9D\xA1""\xE5\x87\x8F""\xE9\x80\x9F""\xE3\x80\x82""\xE7\xA9\xBA""\xE6\xA0\xBC""\xE9\x94\xAE""\xE8\xA7\xA6""\xE5\x8F\x91""\xE7\xB4\xA7""\xE6\x80\xA5""\xE5\x81\x9C""\xE6\xAD\xA2""\xE3\x80\x82"""), manualGroup);
  instruction->setWordWrap(true);
  manualLayout->addWidget(instruction);
  auto* directionGrid = new QGridLayout();
  directionGrid->setHorizontalSpacing(10);
  directionGrid->setVerticalSpacing(10);
  for (int column = 0; column < 3; ++column) directionGrid->setColumnStretch(column, 1);
  auto* forwardButton = makeDirectionButton(CRAWLING_TEXT("\xE5\x89\x8D""\xE8\xBF\x9B""\nW / \xE4\xB8\x8A"""), manualGroup);
  auto* reverseButton = makeDirectionButton(CRAWLING_TEXT("\xE5\x90\x8E""\xE9\x80\x80""\nS / \xE4\xB8\x8B"""), manualGroup);
  auto* leftButton = makeDirectionButton(CRAWLING_TEXT("\xE5\xB7\xA6""\xE8\xBD\xAC""\nA / \xE5\xB7\xA6"""), manualGroup);
  auto* rightButton = makeDirectionButton(CRAWLING_TEXT("\xE5\x8F\xB3""\xE8\xBD\xAC""\nD / \xE5\x8F\xB3"""), manualGroup);
  directionGrid->addWidget(forwardButton, 0, 1);
  directionGrid->addWidget(leftButton, 1, 0);
  directionGrid->addWidget(rightButton, 1, 2);
  directionGrid->addWidget(reverseButton, 2, 1);
  manualLayout->addLayout(directionGrid);
  connect(forwardButton, &QPushButton::pressed, this, [this] { pressMotionButton(MotionKey::Forward); });
  connect(forwardButton, &QPushButton::released, this, [this] { releaseMotionButton(MotionKey::Forward); });
  connect(reverseButton, &QPushButton::pressed, this, [this] { pressMotionButton(MotionKey::Reverse); });
  connect(reverseButton, &QPushButton::released, this, [this] { releaseMotionButton(MotionKey::Reverse); });
  connect(leftButton, &QPushButton::pressed, this, [this] { pressMotionButton(MotionKey::Left); });
  connect(leftButton, &QPushButton::released, this, [this] { releaseMotionButton(MotionKey::Left); });
  connect(rightButton, &QPushButton::pressed, this, [this] { pressMotionButton(MotionKey::Right); });
  connect(rightButton, &QPushButton::released, this, [this] { releaseMotionButton(MotionKey::Right); });

  auto* speedLayout = new QHBoxLayout();
  speedLayout->addWidget(new QLabel(CRAWLING_TEXT("\xE6\x89\x8B""\xE5\x8A\xA8""\xE8\xBE\x93""\xE5\x87\xBA"""), manualGroup));
  speedSlider_ = new QSlider(Qt::Horizontal, manualGroup);
  speedSlider_->setRange(5, 100);
  speedSlider_->setValue(settings_.manualJogPercent);
  speedSlider_->setMinimumHeight(36);
  speedLayout->addWidget(speedSlider_, 1);
  speedPercentLabel_ = new QLabel(CRAWLING_TEXT("30%"), manualGroup);
  speedPercentLabel_->setMinimumWidth(44);
  speedLayout->addWidget(speedPercentLabel_);
  connect(speedSlider_, &QSlider::valueChanged, this, [this](int value) {
    speedPercentLabel_->setText(CRAWLING_TEXT("%1%").arg(value));
    settings_.manualJogPercent = value;
    QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
    persistent.beginGroup(CRAWLING_TEXT("drive"));
    persistent.setValue(CRAWLING_TEXT("manualJogPercent"), value);
    persistent.endGroup();
    persistent.sync();
  });
  manualLayout->addLayout(speedLayout);

  auto* chassisHeading = new QLabel(CRAWLING_TEXT("底盘控制"), manualGroup);
  chassisHeading->setObjectName(CRAWLING_TEXT("controlSectionTitle"));
  manualLayout->addWidget(chassisHeading);
  auto* chassisGrid = new QGridLayout();
  chassisGrid->setSpacing(10);
  chassisGrid->setColumnStretch(0, 1);
  chassisGrid->setColumnStretch(1, 1);
  enableButton_ = makeTouchButton(CRAWLING_TEXT("底盘使能（静止保持）"), manualGroup,
                                  CRAWLING_TEXT("enableButton"));
  stopButton_ = makeTouchButton(CRAWLING_TEXT("停止并保持"), manualGroup,
                                CRAWLING_TEXT("stopButton"));
  emergencyButton_ = makeTouchButton(CRAWLING_TEXT("紧急停止"), manualGroup,
                                     CRAWLING_TEXT("emergencyButton"));
  chassisGrid->addWidget(enableButton_, 0, 0);
  chassisGrid->addWidget(stopButton_, 0, 1);
  chassisGrid->addWidget(emergencyButton_, 1, 0, 1, 2);
  manualLayout->addLayout(chassisGrid);
  connect(enableButton_, &QPushButton::clicked, this, &MainWindow::enableDrive);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopDrive);
  connect(emergencyButton_, &QPushButton::clicked, this, &MainWindow::emergencyStop);

  auto* deviceHeading = new QLabel(CRAWLING_TEXT("设备操作"), manualGroup);
  deviceHeading->setObjectName(CRAWLING_TEXT("controlSectionTitle"));
  manualLayout->addWidget(deviceHeading);
  auto* protocolGrid = new QGridLayout();
  protocolGrid->setSpacing(10);
  protocolGrid->setColumnStretch(0, 1);
  protocolGrid->setColumnStretch(1, 1);
  auto* resetButton = makeTouchButton(CRAWLING_TEXT("系统复位"), manualGroup);
  auto* clearAlarmButton = makeTouchButton(CRAWLING_TEXT("清除报警"), manualGroup);
  protocolGrid->addWidget(resetButton, 0, 0);
  protocolGrid->addWidget(clearAlarmButton, 0, 1);
  manualLayout->addLayout(protocolGrid);
  connect(resetButton, &QPushButton::clicked, this, &MainWindow::systemReset);
  connect(clearAlarmButton, &QPushButton::clicked, this, &MainWindow::clearAlarm);

  auto* correctionHeading = new QLabel(CRAWLING_TEXT("激光自动纠偏"), manualGroup);
  correctionHeading->setObjectName(CRAWLING_TEXT("controlSectionTitle"));
  manualLayout->addWidget(correctionHeading);
  auto* correctionActionGrid = new QGridLayout();
  correctionActionGrid->setSpacing(10);
  correctionActionGrid->setColumnStretch(0, 1);
  correctionActionGrid->setColumnStretch(1, 1);
  autoStartButton_ = makeTouchButton(CRAWLING_TEXT("启动自动纠偏"), manualGroup);
  autoStopButton_ = makeTouchButton(CRAWLING_TEXT("停止自动纠偏"), manualGroup);
  correctionActionGrid->addWidget(autoStartButton_, 0, 0);
  correctionActionGrid->addWidget(autoStopButton_, 0, 1);
  manualLayout->addLayout(correctionActionGrid);
  correctionStatusLabel_ = new QLabel(CRAWLING_TEXT("未启动"), manualGroup);
  correctionStatusLabel_->setObjectName(CRAWLING_TEXT("correctionStatus"));
  correctionStatusLabel_->setMinimumHeight(44);
  correctionStatusLabel_->setWordWrap(true);
  manualLayout->addWidget(correctionStatusLabel_);
  auto* clampHeading = new QLabel(CRAWLING_TEXT("夹子电机手动微调"), manualGroup);
  clampHeading->setObjectName(CRAWLING_TEXT("controlSectionTitle"));
  manualLayout->addWidget(clampHeading);
  auto* clampGrid = new QGridLayout();
  const auto addAxisButton = [this, clampGrid, manualGroup](int row, const QString& axis,
                                                               auto plusSlot, auto minusSlot) {
    clampGrid->addWidget(new QLabel(axis, manualGroup), row, 0);
    auto* plus = makeTouchButton(axis + CRAWLING_TEXT(" +"), manualGroup);
    auto* minus = makeTouchButton(axis + CRAWLING_TEXT(" -"), manualGroup);
    clampGrid->addWidget(plus, row, 1); clampGrid->addWidget(minus, row, 2);
    connect(plus, &QPushButton::clicked, clampMotors_, plusSlot);
    connect(minus, &QPushButton::clicked, clampMotors_, minusSlot);
  };
  addAxisButton(0, CRAWLING_TEXT("X"), &ClampMotorController::moveXPositive, &ClampMotorController::moveXNegative);
  addAxisButton(1, CRAWLING_TEXT("Y"), &ClampMotorController::moveYPositive, &ClampMotorController::moveYNegative);
  addAxisButton(2, CRAWLING_TEXT("Z"), &ClampMotorController::moveZPositive, &ClampMotorController::moveZNegative);
  manualLayout->addLayout(clampGrid);
  connect(autoStartButton_, &QPushButton::clicked, this, &MainWindow::startAutoCorrection);
  connect(autoStopButton_, &QPushButton::clicked, this, &MainWindow::stopAutoCorrection);
  manualLayout->addStretch(1);
  controlLayout->addWidget(manualGroup, 1);

  auto* rightColumn = new QVBoxLayout();
  auto* cloudGroup = new QGroupBox(CRAWLING_TEXT("\xE7\xBA\xBF\xE6\xBF\x80\xE5\x85\x89\xE7\x82\xB9\xE4\xBA\x91\xE9\xA2\x84\xE8\xA7\x88"), controlPage);
  pointCloudGroup_ = cloudGroup;
  auto* cloudLayout = new QVBoxLayout(cloudGroup);
  auto* cloudToolbar = new QHBoxLayout();
  cloudToolbar->addWidget(new QLabel(CRAWLING_TEXT("\xE6\x8A\x95""\xE5\xBD\xB1""\xE5\xB9\xB3""\xE9\x9D\xA2"""), cloudGroup));
  pointCloudPlaneBox_ = new QComboBox(cloudGroup);
  pointCloudPlaneBox_->addItem(CRAWLING_TEXT("X-Y"), CRAWLING_TEXT("xy"));
  pointCloudPlaneBox_->addItem(CRAWLING_TEXT("X-Z"), CRAWLING_TEXT("xz"));
  pointCloudPlaneBox_->addItem(CRAWLING_TEXT("Y-Z"), CRAWLING_TEXT("yz"));
  pointCloudPlaneBox_->setCurrentIndex(1);
  cloudToolbar->addWidget(pointCloudPlaneBox_);
  cloudToolbar->addStretch(1);
  cloudLayout->addLayout(cloudToolbar);
  pointCloud_ = new PointCloudView(cloudGroup);
  pointCloud_->setMinimumHeight(220);
  cloudLayout->addWidget(pointCloud_);
  rightColumn->addWidget(cloudGroup);

  auto* statusGroup = new QGroupBox(CRAWLING_TEXT("车体状态"), controlPage);
  auto* statusLayout = new QGridLayout(statusGroup);
  auto* leftStatus = new QFormLayout();
  auto* rightStatus = new QFormLayout();
  statusGroup->setTitle(CRAWLING_TEXT("车体状态"));
  leftStatus->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  rightStatus->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  leftStatus->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  rightStatus->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  QFormLayout* statusForm = leftStatus;
  targetValue_ = makeMetricValue(statusGroup);
  appliedValue_ = makeMetricValue(statusGroup);
  leftValue_ = makeMetricValue(statusGroup);
  rightValue_ = makeMetricValue(statusGroup);
  feedbackValue_ = makeMetricValue(statusGroup);
  syncValue_ = makeMetricValue(statusGroup);
  auto* vehicleHeading = new QLabel(CRAWLING_TEXT("车体运动与驱动轮速度"), statusGroup);
  vehicleHeading->setObjectName(CRAWLING_TEXT("statusSectionTitle"));
  statusForm->addRow(vehicleHeading);
  statusForm->addRow(CRAWLING_TEXT("\xE8\xAF\xB7""\xE6\xB1\x82""\xE8\xBD\xA6""\xE4\xBD\x93""\xE5\x91\xBD""\xE4\xBB\xA4"""), targetValue_);
  statusForm->addRow(CRAWLING_TEXT("\xE5\xB7\xB2""\xE5\xBA\x94""\xE7\x94\xA8""\xE8\xBD\xA6""\xE4\xBD\x93""\xE5\x91\xBD""\xE4\xBB\xA4"""), appliedValue_);
  statusForm->addRow(CRAWLING_TEXT("\xE5\xB7\xA6""\xE8\xBD\xAE"""), leftValue_);
  statusForm->addRow(CRAWLING_TEXT("\xE5\x8F\xB3""\xE8\xBD\xAE"""), rightValue_);
  statusForm->addRow(CRAWLING_TEXT("\xE7\x9C\x8B""\xE9\x97\xA8""\xE7\x8B\x97"""), feedbackValue_);
  statusForm->addRow(CRAWLING_TEXT("\xE5\x8F\x8C""\xE8\xBD\xAE""\xE5\x90\x8C""\xE6\xAD\xA5"""), syncValue_);
  statusForm = rightStatus;
  auto* motorHeading = new QLabel(CRAWLING_TEXT("驱动轮编码器与电机"), statusGroup);
  motorHeading->setObjectName(CRAWLING_TEXT("statusSectionTitle"));
  statusForm->addRow(motorHeading);
  leftEncoderValue_ = makeMetricValue(statusGroup);
  rightEncoderValue_ = makeMetricValue(statusGroup);
  leftMotorValue_ = makeMetricValue(statusGroup);
  rightMotorValue_ = makeMetricValue(statusGroup);
  leftHealthValue_ = makeMetricValue(statusGroup);
  rightHealthValue_ = makeMetricValue(statusGroup);
  statusForm->addRow(CRAWLING_TEXT("左轮端编码器位置"), leftEncoderValue_);
  statusForm->addRow(CRAWLING_TEXT("右轮端编码器位置"), rightEncoderValue_);
  statusForm->addRow(CRAWLING_TEXT("左电机转速 / 控制量"), leftMotorValue_);
  statusForm->addRow(CRAWLING_TEXT("右电机转速 / 控制量"), rightMotorValue_);
  statusForm->addRow(CRAWLING_TEXT("左轮反馈 / 温度"), leftHealthValue_);
  statusForm->addRow(CRAWLING_TEXT("右轮反馈 / 温度"), rightHealthValue_);

  auto* imuHeading = new QLabel(CRAWLING_TEXT("IMU"), statusGroup);
  imuHeading->setObjectName(CRAWLING_TEXT("statusSectionTitle"));
  statusForm->addRow(imuHeading);
  imuStateValue_ = makeMetricValue(statusGroup);
  imuOrientationValue_ = makeMetricValue(statusGroup);
  imuGyroValue_ = makeMetricValue(statusGroup);
  imuAccelerationValue_ = makeMetricValue(statusGroup);
  imuStateValue_->setText(CRAWLING_TEXT("未连接"));
  imuOrientationValue_->setText(CRAWLING_TEXT("等待数据"));
  imuGyroValue_->setText(CRAWLING_TEXT("等待数据"));
  imuAccelerationValue_->setText(CRAWLING_TEXT("等待数据"));
  statusForm->addRow(CRAWLING_TEXT("连接状态"), imuStateValue_);
  statusForm->addRow(CRAWLING_TEXT("姿态"), imuOrientationValue_);
  statusForm->addRow(CRAWLING_TEXT("角速度"), imuGyroValue_);
  statusForm->addRow(CRAWLING_TEXT("加速度"), imuAccelerationValue_);

  statusForm = leftStatus;
  auto* auxiliaryHeading = new QLabel(CRAWLING_TEXT("外部编码器与夹子电机"), statusGroup);
  auxiliaryHeading->setObjectName(CRAWLING_TEXT("statusSectionTitle"));
  statusForm->addRow(auxiliaryHeading);
  externalEncoderStateValue_ = makeMetricValue(statusGroup);
  clampMotorStateValue_ = makeMetricValue(statusGroup);
  externalEncoderStateValue_->setText(CRAWLING_TEXT("未接入（Modbus 待读取）"));
  clampMotorStateValue_->setText(CRAWLING_TEXT("未检测节点；实时反馈未接入"));
  externalEncoderStateValue_->setWordWrap(true);
  clampMotorStateValue_->setWordWrap(true);
  statusForm->addRow(CRAWLING_TEXT("外部编码器"), externalEncoderStateValue_);
  statusForm->addRow(CRAWLING_TEXT("夹子电机"), clampMotorStateValue_);
  statusLayout->addLayout(leftStatus, 0, 0);
  statusLayout->addLayout(rightStatus, 0, 1);
  statusLayout->setColumnStretch(0, 1);
  statusLayout->setColumnStretch(1, 1);
  rightColumn->addWidget(statusGroup);

  auto* usbPreviewGroup = new QGroupBox(CRAWLING_TEXT("USB 摄像头画面"), controlPage);
  auto* usbPreviewLayout = new QVBoxLayout(usbPreviewGroup);
  usbCameraPreview_ = new QLabel(CRAWLING_TEXT("USB 摄像头未连接"), usbPreviewGroup);
  usbCameraPreview_->setAlignment(Qt::AlignCenter);
  usbCameraPreview_->setMinimumHeight(200);
  usbCameraPreview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  usbCameraPreview_->setStyleSheet(
      CRAWLING_TEXT("background: #111820; color: #aab6c2; border: 1px solid #cbd3da;"));
  usbPreviewLayout->addWidget(usbCameraPreview_);
  rightColumn->addWidget(usbPreviewGroup, 1);
  rightColumn->addStretch(1);
  controlLayout->addLayout(rightColumn, 2);
  controlScroll->setWidget(controlPage);
  tabs->addTab(controlScroll, CRAWLING_TEXT("\xE5\xBA\x95""\xE7\x9B\x98"""));

  auto* settingsScroll = new QScrollArea(tabs);
  settingsScroll->setWidgetResizable(true);
  auto* settingsPage = new QWidget(settingsScroll);
  auto* settingsLayout = new QGridLayout(settingsPage);
  settingsLayout->setContentsMargins(16, 16, 16, 16);
  settingsLayout->setHorizontalSpacing(12);
  settingsLayout->setVerticalSpacing(12);

  auto* adapterGroup = new QGroupBox(QStringLiteral("MWD RS485 wheel ports"), settingsPage);
  auto* adapterForm = new QFormLayout(adapterGroup);
  leftMotorPortBox_ = new QComboBox(adapterGroup);
  leftMotorBaudBox_ = new QComboBox(adapterGroup);
  rightMotorPortBox_ = new QComboBox(adapterGroup);
  rightMotorBaudBox_ = new QComboBox(adapterGroup);
  for (int baud : {115200, 500000, 1000000, 1500000, 2500000}) {
    leftMotorBaudBox_->addItem(QString::number(baud), baud);
    rightMotorBaudBox_->addItem(QString::number(baud), baud);
  }
  auto* refreshButton = new QPushButton(CRAWLING_TEXT("\xE5\x88\xB7""\xE6\x96\xB0""\xE4\xB8\xB2""\xE5\x8F\xA3"""), adapterGroup);
  auto* connectButton = new QPushButton(CRAWLING_TEXT("连接底盘"), adapterGroup);
  auto* disconnectButton = new QPushButton(CRAWLING_TEXT("\xE6\x96\xAD""\xE5\xBC\x80""\xE8\xBF\x9E""\xE6\x8E\xA5"""), adapterGroup);
  adapterForm->addRow(QStringLiteral("Left motor port"), leftMotorPortBox_);
  adapterForm->addRow(QStringLiteral("Left motor baud rate"), leftMotorBaudBox_);
  adapterForm->addRow(QStringLiteral("Right motor port"), rightMotorPortBox_);
  adapterForm->addRow(QStringLiteral("Right motor baud rate"), rightMotorBaudBox_);
  adapterForm->addRow(QString(), refreshButton);
  adapterForm->addRow(QString(), connectButton);
  adapterForm->addRow(QString(), disconnectButton);
  connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
  connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectAdapter);
  connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectAdapter);
  settingsLayout->addWidget(adapterGroup, 0, 0);

  auto* imuGroup = new QGroupBox(CRAWLING_TEXT("RIM302 IMU"), settingsPage);
  auto* imuForm = new QFormLayout(imuGroup);
  imuPortBox_ = new QComboBox(imuGroup);
  imuBaudBox_ = new QComboBox(imuGroup);
  for (int baud : {9600, 19200, 38400, 57600, 115200, 256000}) {
    imuBaudBox_->addItem(QString::number(baud), baud);
  }
  imuBaudBox_->setCurrentIndex(imuBaudBox_->findData(115200));
  imuDividerBox_ = new QComboBox(imuGroup);
  for (const int divider : {1, 2, 4, 8, 10, 20, 40, 200}) {
    imuDividerBox_->addItem(CRAWLING_TEXT("分频 %1").arg(divider), divider);
  }
  auto* imuRefreshButton = new QPushButton(CRAWLING_TEXT("刷新串口"), imuGroup);
  imuConnectButton_ = new QPushButton(CRAWLING_TEXT("连接 IMU"), imuGroup);
  auto* imuDisconnectButton = new QPushButton(CRAWLING_TEXT("断开 IMU"), imuGroup);
  imuConfigStateLabel_ = new QLabel(CRAWLING_TEXT("未连接"), imuGroup);
  imuConfigStateLabel_->setWordWrap(true);
  imuForm->addRow(CRAWLING_TEXT("串口"), imuPortBox_);
  imuForm->addRow(CRAWLING_TEXT("波特率"), imuBaudBox_);
  imuForm->addRow(CRAWLING_TEXT("输出分频"), imuDividerBox_);
  imuForm->addRow(QString(), imuRefreshButton);
  imuForm->addRow(QString(), imuConnectButton_);
  imuForm->addRow(QString(), imuDisconnectButton);
  imuForm->addRow(CRAWLING_TEXT("连接状态"), imuConfigStateLabel_);
  connect(imuRefreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
  connect(imuConnectButton_, &QPushButton::clicked, this,
          &MainWindow::connectConfiguredImu);
  connect(imuDisconnectButton, &QPushButton::clicked, this, [this] {
    QMetaObject::invokeMethod(devices_, "disconnectImu", Qt::QueuedConnection);
  });
  settingsLayout->addWidget(imuGroup, 2, 0);

  auto* cameraGroup =
      new QGroupBox(CRAWLING_TEXT("MV3DLP 激光相机"), settingsPage);
  auto* cameraForm = new QFormLayout(cameraGroup);
  laserSerialBox_ = new QLineEdit(cameraGroup);
  laserSerialBox_->setPlaceholderText(CRAWLING_TEXT("\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B""\xE6\x88\x96""\xE6\x89\x8B""\xE5\x8A\xA8""\xE5\xA1\xAB""\xE5\x86\x99""\xE5\xBA\x8F""\xE5\x88\x97""\xE5\x8F\xB7"""));
  cameraDeviceBox_ = new QComboBox(cameraGroup);
  auto* cameraScanButton = new QPushButton(CRAWLING_TEXT("扫描相机"), cameraGroup);
  cameraConnectButton_ = new QPushButton(CRAWLING_TEXT("连接相机"), cameraGroup);
  auto* cameraDisconnectButton =
      new QPushButton(CRAWLING_TEXT("断开相机"), cameraGroup);
  cameraConfigStateLabel_ = new QLabel(CRAWLING_TEXT("未连接"), cameraGroup);
  cameraConfigStateLabel_->setWordWrap(true);
  cameraFrameConfigLabel_ = new QLabel(CRAWLING_TEXT("暂无帧数据"), cameraGroup);
  cameraFrameConfigLabel_->setWordWrap(true);
  cameraForm->addRow(CRAWLING_TEXT("配置序列号"), laserSerialBox_);
  cameraForm->addRow(CRAWLING_TEXT("已发现相机"), cameraDeviceBox_);
  cameraForm->addRow(QString(), cameraScanButton);
  cameraForm->addRow(QString(), cameraConnectButton_);
  cameraForm->addRow(QString(), cameraDisconnectButton);
  cameraForm->addRow(CRAWLING_TEXT("连接状态"), cameraConfigStateLabel_);
  cameraForm->addRow(CRAWLING_TEXT("最新帧"), cameraFrameConfigLabel_);
  connect(cameraScanButton, &QPushButton::clicked, this,
          &MainWindow::scanConfiguredCamera);
  connect(cameraConnectButton_, &QPushButton::clicked, this,
          &MainWindow::connectConfiguredCamera);
  connect(cameraDisconnectButton, &QPushButton::clicked, this, [this] {
    QMetaObject::invokeMethod(devices_, "disconnectCamera", Qt::QueuedConnection);
  });
  connect(cameraDeviceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0) return;
            laserSerialBox_->setText(
                cameraDeviceBox_->itemText(index).section(" | ", 0, 0));
          });
  settingsLayout->addWidget(cameraGroup, 2, 1);

  auto* usbCameraGroup = new QGroupBox(CRAWLING_TEXT("USB 摄像头"), settingsPage);
  auto* usbCameraForm = new QFormLayout(usbCameraGroup);
  usbCameraDeviceBox_ = new QComboBox(usbCameraGroup);
  for (int index = 0; index < 10; ++index) {
    usbCameraDeviceBox_->addItem(CRAWLING_TEXT("设备 %1").arg(index), index);
  }
  usbCameraDeviceBox_->setCurrentIndex(-1);
  usbCameraFpsBox_ = new QSpinBox(usbCameraGroup);
  usbCameraFpsBox_->setRange(1, 120);
  usbCameraFpsBox_->setValue(30);
  usbCameraAutoConnectBox_ = new QCheckBox(CRAWLING_TEXT("参与连接全部接口和启动时自动连接"), usbCameraGroup);
  usbCameraFlipHorizontalBox_ = new QCheckBox(CRAWLING_TEXT("画面水平翻转"), usbCameraGroup);
  usbCameraFlipVerticalBox_ = new QCheckBox(CRAWLING_TEXT("画面竖直翻转"), usbCameraGroup);
  auto* usbCameraScanButton = new QPushButton(CRAWLING_TEXT("扫描 USB 摄像头"), usbCameraGroup);
  usbCameraConnectButton_ = new QPushButton(CRAWLING_TEXT("打开 USB 摄像头"), usbCameraGroup);
  auto* usbCameraDisconnectButton = new QPushButton(CRAWLING_TEXT("关闭 USB 摄像头"), usbCameraGroup);
  usbCameraConfigStateLabel_ = new QLabel(CRAWLING_TEXT("未连接"), usbCameraGroup);
  usbCameraConfigStateLabel_->setWordWrap(true);
  usbCameraForm->addRow(CRAWLING_TEXT("设备"), usbCameraDeviceBox_);
  usbCameraForm->addRow(CRAWLING_TEXT("帧率 (fps)"), usbCameraFpsBox_);
  usbCameraForm->addRow(QString(), usbCameraAutoConnectBox_);
  usbCameraForm->addRow(QString(), usbCameraFlipHorizontalBox_);
  usbCameraForm->addRow(QString(), usbCameraFlipVerticalBox_);
  usbCameraForm->addRow(QString(), usbCameraScanButton);
  usbCameraForm->addRow(QString(), usbCameraConnectButton_);
  usbCameraForm->addRow(QString(), usbCameraDisconnectButton);
  usbCameraForm->addRow(CRAWLING_TEXT("连接状态"), usbCameraConfigStateLabel_);
  connect(usbCameraScanButton, &QPushButton::clicked, this, [this] {
    QMetaObject::invokeMethod(usbCamera_, "scanDevices", Qt::QueuedConnection);
  });
  connect(usbCameraConnectButton_, &QPushButton::clicked, this, [this] {
    if (usbCameraDeviceBox_->currentIndex() < 0) {
      appendLog(CRAWLING_TEXT("请先选择 USB 摄像头设备"));
      return;
    }
    settings_ = settingsFromUi();
    QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
    settings_.save(persistent);
    persistent.sync();
    const int deviceIndex = usbCameraDeviceBox_->currentData().toInt();
    QMetaObject::invokeMethod(usbCamera_, "connectCamera", Qt::QueuedConnection,
                              Q_ARG(int, deviceIndex),
                              Q_ARG(int, usbCameraFpsBox_->value()),
                              Q_ARG(bool, usbCameraFlipHorizontalBox_->isChecked()),
                              Q_ARG(bool, usbCameraFlipVerticalBox_->isChecked()));
  });
  connect(usbCameraDisconnectButton, &QPushButton::clicked, this, [this] {
    QMetaObject::invokeMethod(usbCamera_, "disconnectCamera", Qt::QueuedConnection);
  });
  settingsLayout->addWidget(usbCameraGroup, 3, 0, 1, 3);

  auto* clampGroup = new QGroupBox(CRAWLING_TEXT("\xE5\xA4\xB9""\xE5\xAD\x90""\xE7\x94\xB5""\xE6\x9C\xBA"" CANopen"), settingsPage);
  auto* clampForm = new QFormLayout(clampGroup);
  clampSerialPortBox_ = new QComboBox(clampGroup);
  clampSerialBaudBox_ = new QComboBox(clampGroup);
  for (int baud : {115200, 230400, 460800, 921600}) {
    clampSerialBaudBox_->addItem(QString::number(baud), baud);
  }
  clampCanBitrateBox_ = new QComboBox(clampGroup);
  for (int bitrate : {125000, 250000, 500000, 800000, 1000000}) {
    clampCanBitrateBox_->addItem(QString::number(bitrate), bitrate);
  }
  clampNodeIdBox_ = new QSpinBox(clampGroup);
  clampNodeIdBox_->setRange(0, 127);
  clampXMotorIdBox_ = new QSpinBox(clampGroup); clampXMotorIdBox_->setRange(1, 127);
  clampYMotorIdBox_ = new QSpinBox(clampGroup); clampYMotorIdBox_->setRange(1, 127);
  clampZMotorIdBox_ = new QSpinBox(clampGroup); clampZMotorIdBox_->setRange(1, 127);
  clampXMotorSignBox_ = new QComboBox(clampGroup);
  clampYMotorSignBox_ = new QComboBox(clampGroup);
  clampZMotorSignBox_ = new QComboBox(clampGroup);
  for (auto* box : {clampXMotorSignBox_, clampYMotorSignBox_, clampZMotorSignBox_}) {
    box->addItem(CRAWLING_TEXT("+1 正向"), 1);
    box->addItem(CRAWLING_TEXT("-1 反向"), -1);
  }
  clampConnectionLabel_ = new QLabel(CRAWLING_TEXT("\xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B"""), clampGroup);
  clampForm->addRow(CRAWLING_TEXT("SLCAN \xE4\xB8\xB2""\xE5\x8F\xA3"""), clampSerialPortBox_);
  clampForm->addRow(CRAWLING_TEXT("\xE9\x80\x82""\xE9\x85\x8D""\xE5\x99\xA8""\xE6\xB3\xA2""\xE7\x89\xB9""\xE7\x8E\x87"""), clampSerialBaudBox_);
  clampForm->addRow(CRAWLING_TEXT("CAN \xE6\xB3\xA2""\xE7\x89\xB9""\xE7\x8E\x87"""), clampCanBitrateBox_);
  clampForm->addRow(CRAWLING_TEXT("CANopen \xE8\x8A\x82""\xE7\x82\xB9"" ID"), clampNodeIdBox_);
  auto addClampAxis = [clampGroup, clampForm](const QString& label, QSpinBox* id, QComboBox* sign) {
    auto* w = new QWidget(clampGroup); auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0); l->addWidget(id); l->addWidget(sign);
    clampForm->addRow(label, w);
  };
  addClampAxis(CRAWLING_TEXT("X 电机 ID / 方向"), clampXMotorIdBox_, clampXMotorSignBox_);
  addClampAxis(CRAWLING_TEXT("Y 电机 ID / 方向"), clampYMotorIdBox_, clampYMotorSignBox_);
  addClampAxis(CRAWLING_TEXT("Z 电机 ID / 方向"), clampZMotorIdBox_, clampZMotorSignBox_);
  clampForm->addRow(CRAWLING_TEXT("\xE6\xA3\x80""\xE6\xB5\x8B""\xE7\x8A\xB6""\xE6\x80\x81"""), clampConnectionLabel_);
  settingsLayout->addWidget(clampGroup, 2, 2);

  auto* mappingGroup = new QGroupBox(CRAWLING_TEXT("\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\x98\xA0""\xE5\xB0\x84"""), settingsPage);
  auto* mappingForm = new QFormLayout(mappingGroup);
  leftMotorIdBox_ = new QSpinBox(mappingGroup);
  rightMotorIdBox_ = new QSpinBox(mappingGroup);
  leftMotorIdBox_->setRange(1, 32);
  rightMotorIdBox_->setRange(1, 32);
  leftSignBox_ = new QComboBox(mappingGroup);
  rightSignBox_ = new QComboBox(mappingGroup);
  for (QComboBox* combo : {leftSignBox_, rightSignBox_}) {
    combo->addItem(CRAWLING_TEXT("+1"), 1);
    combo->addItem(CRAWLING_TEXT("-1"), -1);
  }
  mappingForm->addRow(QStringLiteral("Left motor RS485 ID"), leftMotorIdBox_);
  mappingForm->addRow(CRAWLING_TEXT("\xE5\xB7\xA6""\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\x96\xB9""\xE5\x90\x91"""), leftSignBox_);
  mappingForm->addRow(QStringLiteral("Right motor RS485 ID"), rightMotorIdBox_);
  mappingForm->addRow(CRAWLING_TEXT("\xE5\x8F\xB3""\xE7\x94\xB5""\xE6\x9C\xBA""\xE6\x96\xB9""\xE5\x90\x91"""), rightSignBox_);
  settingsLayout->addWidget(mappingGroup, 0, 1);

  auto* geometryGroup = new QGroupBox(CRAWLING_TEXT("\xE8\xBD\xAE""\xE5\xAD\x90""\xE5\x87\xA0""\xE4\xBD\x95""\xE5\x8F\x82""\xE6\x95\xB0"""), settingsPage);
  auto* geometryForm = new QFormLayout(geometryGroup);
  wheelRadiusBox_ = makeDoubleSpin(1.0, 500.0, 1.0, 2, geometryGroup);
  trackWidthBox_ = makeDoubleSpin(10.0, 2000.0, 1.0, 1, geometryGroup);
  ratioBox_ = makeDoubleSpin(0.001, 1000.0, 0.010, 4, geometryGroup);
  geometryForm->addRow(CRAWLING_TEXT("\xE8\xBD\xAE""\xE5\x8D\x8A""\xE5\xBE\x84"" (mm)"), wheelRadiusBox_);
  geometryForm->addRow(CRAWLING_TEXT("\xE8\xBD\xAE""\xE8\xB7\x9D"" (mm)"), trackWidthBox_);
  geometryForm->addRow(CRAWLING_TEXT("\xE7\x94\xB5""\xE6\x9C\xBA""\xE8\xBE\x93""\xE5\x87\xBA""/\xE8\xBD\xAE""\xE5\xAD\x90""\xE4\xBC\xA0""\xE5\x8A\xA8""\xE6\xAF\x94"""), ratioBox_);
  settingsLayout->addWidget(geometryGroup, 0, 2);

  auto* limitsGroup = new QGroupBox(CRAWLING_TEXT("\xE5\x91\xBD""\xE4\xBB\xA4""\xE9\x99\x90""\xE5\x88\xB6"""), settingsPage);
  auto* limitsForm = new QFormLayout(limitsGroup);
  maxWheelSpeedBox_ = makeDoubleSpin(5.0, 2000.0, 5.0, 1, limitsGroup);
  maxLinearSpeedBox_ = makeDoubleSpin(5.0, 2000.0, 5.0, 1, limitsGroup);
  maxAngularSpeedBox_ = makeDoubleSpin(0.5, 573.0, 0.5, 1, limitsGroup);
  maxLinearAccelBox_ = makeDoubleSpin(10.0, 10000.0, 10.0, 1, limitsGroup);
  maxAngularAccelBox_ = makeDoubleSpin(0.5, 1146.0, 0.5, 1, limitsGroup);
  minimumInnerRatioBox_ = makeDoubleSpin(0.000, 1.000, 0.010, 3, limitsGroup);
  limitsForm->addRow(CRAWLING_TEXT("\xE6\x9C\x80""\xE5\xA4\xA7""\xE8\xBD\xAE""\xE9\x80\x9F"" (mm/s)"), maxWheelSpeedBox_);
  limitsForm->addRow(CRAWLING_TEXT("\xE6\x9C\x80""\xE5\xA4\xA7""\xE7\xBA\xBF""\xE9\x80\x9F""\xE5\xBA\xA6"" (mm/s)"), maxLinearSpeedBox_);
  limitsForm->addRow(CRAWLING_TEXT("\xE6\x9C\x80""\xE5\xA4\xA7""\xE8\xA7\x92""\xE9\x80\x9F""\xE5\xBA\xA6"" (deg/s)"), maxAngularSpeedBox_);
  limitsForm->addRow(CRAWLING_TEXT("\xE7\xBA\xBF""\xE5\x8A\xA0""\xE9\x80\x9F""\xE5\xBA\xA6"" (mm/s2)"), maxLinearAccelBox_);
  limitsForm->addRow(CRAWLING_TEXT("\xE8\xA7\x92""\xE5\x8A\xA0""\xE9\x80\x9F""\xE5\xBA\xA6"" (deg/s2)"), maxAngularAccelBox_);
  limitsForm->addRow(CRAWLING_TEXT("\xE5\x86\x85""\xE4\xBE\xA7""\xE8\xBD\xAE""\xE6\x9C\x80""\xE5\xB0\x8F""\xE6\xAF\x94""\xE4\xBE\x8B"""), minimumInnerRatioBox_);
  settingsLayout->addWidget(limitsGroup, 1, 0);

  auto* syncGroup = new QGroupBox(CRAWLING_TEXT("\xE5\x8F\x8C""\xE8\xBD\xAE""\xE5\x90\x8C""\xE6\xAD\xA5"""), settingsPage);
  auto* syncForm = new QFormLayout(syncGroup);
  synchronizationPBox_ = makeDoubleSpin(0.000, 5.000, 0.010, 3, syncGroup);
  synchronizationIBox_ = makeDoubleSpin(0.000, 5.000, 0.010, 3, syncGroup);
  maxCorrectionBox_ = makeDoubleSpin(1.0, 300.0, 1.0, 1, syncGroup);
  minSyncSpeedBox_ = makeDoubleSpin(1.0, 300.0, 1.0, 1, syncGroup);
  syncForm->addRow(CRAWLING_TEXT("\xE6\xAF\x94""\xE4\xBE\x8B""\xE5\xA2\x9E""\xE7\x9B\x8A"" P"), synchronizationPBox_);
  syncForm->addRow(CRAWLING_TEXT("\xE7\xA7\xAF""\xE5\x88\x86""\xE5\xA2\x9E""\xE7\x9B\x8A"" I"), synchronizationIBox_);
  syncForm->addRow(CRAWLING_TEXT("\xE6\x9C\x80""\xE5\xA4\xA7""\xE4\xBF\xAE""\xE6\xAD\xA3""\xE9\x87\x8F"" (mm/s)"), maxCorrectionBox_);
  syncForm->addRow(CRAWLING_TEXT("\xE6\x9C\x80""\xE5\xB0\x8F""\xE5\x8F\x97""\xE6\x8E\xA7""\xE9\x80\x9F""\xE5\xBA\xA6"" (mm/s)"), minSyncSpeedBox_);
  settingsLayout->addWidget(syncGroup, 1, 1);

  auto* safetyGroup = new QGroupBox(CRAWLING_TEXT("\xE5\xAE\x89""\xE5\x85\xA8""\xE7\x9C\x8B""\xE9\x97\xA8""\xE7\x8B\x97"""), settingsPage);
  auto* safetyForm = new QFormLayout(safetyGroup);
  commandTimeoutBox_ = new QSpinBox(safetyGroup);
  feedbackTimeoutBox_ = new QSpinBox(safetyGroup);
  armingTimeoutBox_ = new QSpinBox(safetyGroup);
  for (QSpinBox* box : {commandTimeoutBox_, feedbackTimeoutBox_, armingTimeoutBox_}) {
    box->setRange(50, 10000);
    box->setSuffix(CRAWLING_TEXT(" ms"));
  }
  commandTimeoutBox_->setMinimum(100);
  feedbackTimeoutBox_->setMinimum(DriveSettings::kMinimumFeedbackTimeoutMs);
  armingTimeoutBox_->setMinimum(DriveSettings::kMinimumFeedbackTimeoutMs);
  safetyForm->addRow(CRAWLING_TEXT("\xE5\x91\xBD""\xE4\xBB\xA4""\xE8\xB6\x85""\xE6\x97\xB6"""), commandTimeoutBox_);
  safetyForm->addRow(CRAWLING_TEXT("\xE5\x8F\x8D""\xE9\xA6\x88""\xE8\xB6\x85""\xE6\x97\xB6"""), feedbackTimeoutBox_);
  safetyForm->addRow(CRAWLING_TEXT("\xE4\xBD\xBF""\xE8\x83\xBD""\xE7\xAD\x89""\xE5\xBE\x85""\xE8\xB6\x85""\xE6\x97\xB6"""), armingTimeoutBox_);
  settingsLayout->addWidget(safetyGroup, 1, 2);

  auto* correctionGroup = new QGroupBox(CRAWLING_TEXT("激光自动纠偏参数"), settingsPage);
  auto* correctionLayout = new QGridLayout(correctionGroup);
  const QStringList correctionLabels = {
      CRAWLING_TEXT("跟踪速度 (mm/s)"), CRAWLING_TEXT("分段距离 (mm)"),
      CRAWLING_TEXT("航向辅助 Kp"), CRAWLING_TEXT("转向阻尼 Kd")};
  correctionSpeedBox_ = makeDoubleSpin(5.0, 150.0, 1.0, 3, correctionGroup);
  correctionSpeedBox_->setValue(5.0);
  correctionSegmentBox_ = makeDoubleSpin(20.0, 2000.0, 10.0, 1, correctionGroup);
  correctionSegmentBox_->setValue(100.0);
  correctionKpBox_ = makeDoubleSpin(0.000, 20.000, 0.010, 3, correctionGroup);
  correctionKpBox_->setValue(3.0);
  correctionKdBox_ = makeDoubleSpin(0.000, 4.000, 0.010, 3, correctionGroup);
  correctionKdBox_->setValue(0.12);
  const auto makeGainControl = [this, correctionGroup](
      QDoubleSpinBox* box, QSlider*& slider, const QString& name,
      const QString& description) {
    auto* control = new QWidget(correctionGroup);
    auto* layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* sliderRow = new QHBoxLayout;
    slider = new QSlider(Qt::Horizontal, control);
    // Integer ticks retain the existing 0.001 precision; keyboard arrows
    // change by 0.01 and Page Up/Down change by 0.10.
    slider->setRange(qRound(box->minimum() * 1000.0),
                     qRound(box->maximum() * 1000.0));
    slider->setSingleStep(10);
    slider->setPageStep(100);
    slider->setValue(qRound(box->value() * 1000.0));
    slider->setTracking(true);
    slider->setFocusPolicy(Qt::StrongFocus);
    slider->setAccessibleName(name);
    const QString tooltip = description + CRAWLING_TEXT(
        "\n拖动调节；方向键每次 0.01，Page Up/Down 每次 0.10。"
        "也可输入精确数值。修改后点击“保存并应用全部参数”或启动自动纠偏时生效。");
    slider->setToolTip(tooltip);
    box->setToolTip(tooltip);
    box->setAccessibleName(name + CRAWLING_TEXT("数值"));
    sliderRow->addWidget(new QLabel(QString::number(box->minimum(), 'f', 0), control));
    sliderRow->addWidget(slider, 1);
    sliderRow->addWidget(new QLabel(QString::number(box->maximum(), 'f', 0), control));
    layout->addLayout(sliderRow);
    layout->addWidget(box);
    connect(slider, &QSlider::valueChanged, this, [box](int value) {
      box->setValue(value / 1000.0);
    });
    connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [slider](double value) {
              const QSignalBlocker blocker(slider);
              slider->setValue(qRound(value * 1000.0));
            });
    return control;
  };
  QWidget* correctionKpControl = makeGainControl(
      correctionKpBox_, correctionKpSlider_, CRAWLING_TEXT("航向辅助 Kp"),
      CRAWLING_TEXT("范围 0 到 20。调节滚动点云航向误差的辅助响应；"
                    "增大时小航向误差响应更强，0 表示关闭该比例辅助。"));
  QWidget* correctionKdControl = makeGainControl(
      correctionKdBox_, correctionKdSlider_, CRAWLING_TEXT("转向阻尼 Kd"),
      CRAWLING_TEXT("范围 0 到 4。依据左右轮反馈的转动速度施加反向阻尼，"
                    "帮助减小过冲；过大可能降低转向响应，0 表示关闭阻尼。"));
  const QList<QWidget*> correctionControls = {
      correctionSpeedBox_, correctionSegmentBox_, correctionKpControl, correctionKdControl};
  for (int column = 0; column < correctionLabels.size(); ++column) {
    correctionLayout->addWidget(new QLabel(correctionLabels.at(column), correctionGroup),
                                0, column);
    correctionLayout->addWidget(correctionControls.at(column), 1, column);
    correctionLayout->setColumnStretch(column, 1);
  }
  settingsLayout->addWidget(correctionGroup, 4, 0, 1, 3);

  auto* applyArea = new QWidget(settingsPage);
  auto* applyLayout = new QVBoxLayout(applyArea);
  applyLayout->setContentsMargins(0, 4, 0, 0);
  applyLayout->setSpacing(6);
  applyAllButton_ = makeTouchButton(CRAWLING_TEXT("保存并应用全部参数"), applyArea,
                                    CRAWLING_TEXT("applyAllButton"));
  applyAllButton_->setMinimumHeight(56);
  parameterStatusLabel_ = new QLabel(CRAWLING_TEXT("参数已加载"), applyArea);
  parameterStatusLabel_->setAlignment(Qt::AlignCenter);
  parameterStatusLabel_->setWordWrap(true);
  applyLayout->addWidget(applyAllButton_);
  applyLayout->addWidget(parameterStatusLabel_);
  settingsLayout->addWidget(applyArea, 5, 0, 1, 3);
  connect(applyAllButton_, &QPushButton::clicked, this, &MainWindow::applyAllParameters);
  const auto markParametersDirty = [this] {
    if (parameterStatusLabel_) {
      parameterStatusLabel_->setText(CRAWLING_TEXT("参数已修改，请点击“保存并应用全部参数”"));
    }
  };
  for (QDoubleSpinBox* box : {wheelRadiusBox_, trackWidthBox_, ratioBox_, maxWheelSpeedBox_,
                              maxLinearSpeedBox_, maxAngularSpeedBox_, maxLinearAccelBox_,
                              maxAngularAccelBox_, minimumInnerRatioBox_, synchronizationPBox_,
                              synchronizationIBox_, maxCorrectionBox_, minSyncSpeedBox_,
                              correctionSpeedBox_, correctionSegmentBox_, correctionKpBox_,
                              correctionKdBox_}) {
    connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, markParametersDirty);
  }
  for (QSpinBox* box : {leftMotorIdBox_, rightMotorIdBox_, commandTimeoutBox_, feedbackTimeoutBox_,
                        armingTimeoutBox_, clampNodeIdBox_, usbCameraFpsBox_}) {
    connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, markParametersDirty);
  }
  for (QComboBox* box : {leftMotorPortBox_, leftMotorBaudBox_,
                         rightMotorPortBox_, rightMotorBaudBox_, imuPortBox_, imuBaudBox_,
                         imuDividerBox_, usbCameraDeviceBox_,
                         clampSerialPortBox_, clampSerialBaudBox_, clampCanBitrateBox_,
                         leftSignBox_, rightSignBox_}) {
    connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, markParametersDirty);
  }
  connect(laserSerialBox_, &QLineEdit::textChanged, this, markParametersDirty);
  connect(usbCameraAutoConnectBox_, &QCheckBox::toggled, this, markParametersDirty);
  connect(autoConnectCheckBox_, &QCheckBox::toggled, this, markParametersDirty);
  connect(autoConnectCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
    settings_.autoConnectOnStartup = checked;
    QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
    persistent.beginGroup(CRAWLING_TEXT("drive"));
    persistent.setValue(CRAWLING_TEXT("autoConnectOnStartup"), checked);
    persistent.endGroup();
    persistent.sync();
  });
  settingsLayout->setRowStretch(6, 1);
  settingsScroll->setWidget(settingsPage);
  tabs->addTab(settingsScroll, CRAWLING_TEXT("\xE9\x85\x8D""\xE7\xBD\xAE"""));
  root->addWidget(tabs, 1);

  auto* logGroup = new QGroupBox(CRAWLING_TEXT("\xE6\x8E\xA7""\xE5\x88\xB6""\xE5\x8F\xB0""\xE6\x97\xA5""\xE5\xBF\x97"""), central);
  auto* logLayout = new QVBoxLayout(logGroup);
  logOutput_ = new QPlainTextEdit(logGroup);
  logOutput_->setReadOnly(true);
  logOutput_->setMaximumBlockCount(400);
  logOutput_->setFixedHeight(120);
  logLayout->addWidget(logOutput_);
  screenshotButton_ = new QPushButton(QStringLiteral("Save Screenshot"), logGroup);
  logLayout->addWidget(screenshotButton_);
  connect(screenshotButton_, &QPushButton::clicked, this, &MainWindow::saveScreenshot);
  root->addWidget(logGroup);

  setCentralWidget(central);

}

void MainWindow::saveScreenshot() {
  const QString defaultName = QStringLiteral("crawling_robot_%1.png")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Save Screenshot"), defaultName,
      QStringLiteral("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)"));
  if (path.isEmpty()) return;
  if (grab().save(path)) {
    AppLogger::write(QStringLiteral("UI.OPERATION"),
                     QStringLiteral("event=save_screenshot result=OK path=%1").arg(path));
    appendLog(QStringLiteral("Screenshot saved: %1").arg(path));
  } else {
    AppLogger::error(QStringLiteral("UI.OPERATION"),
                     QStringLiteral("event=save_screenshot result=FAILED path=%1").arg(path));
    appendLog(QStringLiteral("Screenshot save failed: %1").arg(path));
  }
}
void MainWindow::bindController() {
  connect(this, &MainWindow::connectionRequested, controller_,
          &SynchronizedDriveController::connectAdapter, Qt::QueuedConnection);
  connect(this, &MainWindow::disconnectRequested, controller_,
          &SynchronizedDriveController::disconnectAdapter, Qt::QueuedConnection);
  connect(this, &MainWindow::commandRequested, controller_,
          &SynchronizedDriveController::setInputCommand, Qt::QueuedConnection);
  connect(this, &MainWindow::enableRequested, controller_,
          &SynchronizedDriveController::requestEnable, Qt::QueuedConnection);
  connect(this, &MainWindow::emergencyStopRequested, controller_,
          &SynchronizedDriveController::emergencyStop, Qt::QueuedConnection);
  connect(this, &MainWindow::systemResetRequested, controller_,
          &SynchronizedDriveController::systemReset, Qt::QueuedConnection);
  connect(this, &MainWindow::clearAlarmRequested, controller_,
          &SynchronizedDriveController::clearAlarm, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::telemetryChanged, this,
          &MainWindow::updateTelemetry, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::telemetryChanged, correction_,
          &LaserCorrectionController::processDriveTelemetry, Qt::QueuedConnection);
  connect(devices_, &DeviceController::imuSampleChanged, this,
          &MainWindow::updateImu, Qt::QueuedConnection);
  connect(devices_, &DeviceController::imuConnectionChanged, this,
          &MainWindow::updateImuConnection, Qt::QueuedConnection);
  connect(devices_, &DeviceController::cameraDevicesChanged, this,
          &MainWindow::updateCameraDevices, Qt::QueuedConnection);
  connect(devices_, &DeviceController::cameraConnectionChanged, this,
          &MainWindow::updateCameraConnection, Qt::QueuedConnection);
  connect(devices_, &DeviceController::cameraFrameChanged, this,
          &MainWindow::updateCameraFrame, Qt::QueuedConnection);
  connect(usbCamera_, &UsbCameraController::devicesChanged, this,
          &MainWindow::updateUsbCameraDevices, Qt::QueuedConnection);
  connect(usbCamera_, &UsbCameraController::connectionChanged, this,
          &MainWindow::updateUsbCameraConnection, Qt::QueuedConnection);
  connect(usbCamera_, &UsbCameraController::frameChanged, this,
          &MainWindow::updateUsbCameraFrame, Qt::QueuedConnection);
  connect(usbCamera_, &UsbCameraController::logMessage, this,
          &MainWindow::appendLog, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::stateChanged, this,
          &MainWindow::updateState, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::connectionChanged, this,
          &MainWindow::updateConnection, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::logMessage, this,
          &MainWindow::appendLog, Qt::QueuedConnection);
  connect(devices_, &DeviceController::logMessage, this,
          &MainWindow::appendLog, Qt::QueuedConnection);
  connect(devices_, &DeviceController::pointCloudProfileReady, this,
          &MainWindow::updatePointCloudReady, Qt::QueuedConnection);
  connect(devices_, &DeviceController::cameraImageReady, this, [this]() {
    // Drain any image-channel notifications without replacing the SDK scan.
    if (devices_) devices_->takeLatestCameraImage();
  }, Qt::QueuedConnection);
  connect(devices_, &DeviceController::cameraProfileModeChanged, this, [this](bool active) {
    if (pointCloud_) pointCloud_->setProfileMode(active);
    if (pointCloudGroup_) pointCloudGroup_->setTitle(CRAWLING_TEXT("SDK 实时轮廓与焊道定位（X/Z）"));
    if (pointCloudPlaneBox_) {
      pointCloudPlaneBox_->setEnabled(false);
      pointCloudPlaneBox_->setToolTip(CRAWLING_TEXT("焊道定位固定显示相机 X/Z 轮廓"));
    }
  }, Qt::QueuedConnection);
  connect(pointCloudPlaneBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::setPointCloudPlane);
  // Automatic correction now consumes native scan coordinates exclusively.
  // Image preview updates must not overwrite a profile observation.
  connect(devices_, &DeviceController::correctionProfileFrameReady, correction_,
          &LaserCorrectionController::processProfileFrame, Qt::QueuedConnection);
  connect(devices_, &DeviceController::correctionProfilePrepared, this, [this](bool ready) {
    if (!autoCorrectionStartPending_) return;
    if (ready) {
      appendLog(CRAWLING_TEXT("相机轮廓流已就绪，正在启动自动纠偏控制器"));
      if (pointCloudGroup_) pointCloudGroup_->setTitle(CRAWLING_TEXT("SDK 实时轮廓与焊道定位（X/Z）"));
      QMetaObject::invokeMethod(correction_, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, true));
    } else {
      appendLog(CRAWLING_TEXT("相机轮廓准备失败，自动纠偏未启动；请查看上方相机错误"));
      if (correctionStatusLabel_) correctionStatusLabel_->setText(CRAWLING_TEXT("未启动：相机轮廓流准备失败"));
      autoCorrectionStartPending_ = autoCorrectionActive_ = false;
      autoStartButton_->setEnabled(true);
      autoStopButton_->setEnabled(false);
    }
  }, Qt::QueuedConnection);
  connect(correction_, &LaserCorrectionController::cameraObservationReady, this,
          [this](const QImage& image, const LaserGapDetection& detection) {
            if (pointCloud_) pointCloud_->setDetectionImage(image, detection);
          }, Qt::QueuedConnection);
  connect(correction_, &LaserCorrectionController::profileObservationReady, this,
          [this](const QVector<QVector3D>& points, const LaserGapDetection& detection,
                 quint32 frameNumber, qint64 receivedAtMs) {
            if (pointCloud_) pointCloud_->setProfileObservation(points, detection, frameNumber, receivedAtMs);
          }, Qt::QueuedConnection);
  connect(correction_, &LaserCorrectionController::commandChanged, controller_,
          &SynchronizedDriveController::setCorrectionCommand, Qt::QueuedConnection);
  connect(correction_, &LaserCorrectionController::statusChanged, this,
          &MainWindow::updateCorrectionStatus, Qt::QueuedConnection);
  connect(correction_, &LaserCorrectionController::logMessage, this,
          &MainWindow::appendLog, Qt::QueuedConnection);
  connect(controller_, &SynchronizedDriveController::canSettingsDetected, this,
          &MainWindow::applyCanDetection, Qt::QueuedConnection);
}

void MainWindow::applyCanDetection(const HardwareDetectionResult& result) {
  // Wheel settings are configured as MWD RS485 parameters. Legacy CAN
  // discovery must not overwrite the selected motor bus or motor IDs.
  if (result.clampDetected) {
    setComboToText(clampSerialPortBox_, result.clampCanPort);
    setComboToValue(clampSerialBaudBox_, result.clampSerialBaudRate);
    setComboToValue(clampCanBitrateBox_, result.clampCanBitrate);
    clampNodeIdBox_->setValue(result.clampNodeId);
    clampMotorStateValue_->setText(
        CRAWLING_TEXT("\xE6\xA3\x80\xE6\xB5\x8B\xE5\x88\xB0\xE8\x8A\x82\xE7\x82\xB9 %1\xEF\xBC\x9B\xE5\xAE\x9E\xE6\x97\xB6\xE5\x8F\x8D\xE9\xA6\x88\xE6\x9C\xAA\xE6\x8E\xA5\xE5\x85\xA5")
            .arg(result.clampNodeId));
    clampConnectionLabel_->setText(CRAWLING_TEXT("\xE5\xB7\xB2""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0""\xE8\x8A\x82""\xE7\x82\xB9"" %1").arg(result.clampNodeId));
  } else {
    clampConnectionLabel_->setText(CRAWLING_TEXT("\xE6\x9C\xAA""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\x88\xB0"""));
  }
  if (!result.clampDetected) {
    clampMotorStateValue_->setText(CRAWLING_TEXT("\xE6\x9C\xAA\xE6\xA3\x80\xE6\xB5\x8B\xE5\x88\xB0\xE8\x8A\x82\xE7\x82\xB9\xEF\xBC\x9B\xE5\xAE\x9E\xE6\x97\xB6\xE5\x8F\x8D\xE9\xA6\x88\xE6\x9C\xAA\xE6\x8E\xA5\xE5\x85\xA5"));
  }
  settings_ = settingsFromUi();
  settingsToUi(settings_);
  saveSettings();
  if (result.details.isEmpty()) {
    appendLog(CRAWLING_TEXT("\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\xAE\x8C""\xE6\x88\x90""\xEF\xBC\x9A""\xE6\x9C\xAA""\xE5\x8F\x91""\xE7\x8E\xB0""\xE8\xBD\xAE""\xE5\xAD\x90""\xE4\xBC\xBA""\xE6\x9C\x8D""\xE6\x88\x96""\xE5\xA4\xB9""\xE5\xAD\x90"" CANopen \xE8\x8A\x82""\xE7\x82\xB9""\xE3\x80\x82"""));
  } else {
    appendLog(CRAWLING_TEXT("\xE8\x87\xAA""\xE5\x8A\xA8""\xE6\xA3\x80""\xE6\xB5\x8B""\xE5\xAE\x8C""\xE6\x88\x90""\xEF\xBC\x9A""%1").arg(result.details.join(CRAWLING_TEXT("\xEF\xBC\x9B"""))));
  }
}

void MainWindow::updateCameraDevices(const QStringList& devices) {
  if (!cameraDeviceBox_) return;
  const QString configured = laserSerialBox_->text().trimmed();
  const QSignalBlocker blocker(cameraDeviceBox_);
  cameraDeviceBox_->clear();
  cameraDeviceBox_->addItems(devices);
  int selected = -1;
  for (int i = 0; i < cameraDeviceBox_->count(); ++i) {
    if (cameraDeviceBox_->itemText(i).section(" | ", 0, 0) == configured) {
      selected = i;
      break;
    }
  }
  if (selected >= 0) {
    cameraDeviceBox_->setCurrentIndex(selected);
  } else {
    cameraDeviceBox_->setCurrentIndex(-1);
  }
}

void MainWindow::updateCameraConnection(bool connected, const QString& message) {
  cameraConnected_ = connected;
  if (cameraConfigStateLabel_) {
    cameraConfigStateLabel_->setText(
        message.isEmpty()
            ? (connected ? CRAWLING_TEXT("相机已连接") : CRAWLING_TEXT("相机未连接"))
            : message);
  }
  if (cameraConnectButton_) cameraConnectButton_->setEnabled(!connected);
}

void MainWindow::updateCameraFrame(quint32 frameNumber, quint32 width,
                                   quint32 height, quint64 pointCount) {
  if (!cameraFrameConfigLabel_) return;
  cameraFrameConfigLabel_->setText(
      CRAWLING_TEXT("帧 #%1，%2 x %3，数据点 %4")
          .arg(frameNumber).arg(width).arg(height).arg(pointCount));
}

void MainWindow::updateUsbCameraDevices(const QStringList& devices) {
  if (!usbCameraDeviceBox_) return;
  const int configured = usbCameraDeviceBox_->currentIndex() >= 0
                             ? usbCameraDeviceBox_->currentData().toInt()
                             : settings_.usbCameraDeviceIndex;
  const QSignalBlocker blocker(usbCameraDeviceBox_);
  usbCameraDeviceBox_->clear();
  for (const QString& device : devices) {
    bool okay = false;
    const int index = device.section(CRAWLING_TEXT(" | "), 0, 0).toInt(&okay);
    if (okay) usbCameraDeviceBox_->addItem(device, index);
  }
  const int selected = usbCameraDeviceBox_->findData(configured);
  usbCameraDeviceBox_->setCurrentIndex(selected >= 0 ? selected
                                                     : (usbCameraDeviceBox_->count() > 0 ? 0 : -1));
}

void MainWindow::updateUsbCameraConnection(bool connected, const QString& message) {
  if (usbCameraConfigStateLabel_) {
    usbCameraConfigStateLabel_->setText(message.isEmpty()
                                            ? (connected ? CRAWLING_TEXT("已连接")
                                                         : CRAWLING_TEXT("未连接"))
                                            : message);
  }
  if (usbCameraConnectButton_) usbCameraConnectButton_->setEnabled(!connected);
  if (!connected && usbCameraPreview_) {
    usbCameraPreview_->setPixmap(QPixmap());
    usbCameraPreview_->setText(CRAWLING_TEXT("USB 摄像头未连接"));
  }
}

void MainWindow::updateUsbCameraFrame(const QImage& image) {
  if (!usbCameraPreview_ || image.isNull()) return;
  usbCameraPreview_->setMinimumSize(image.size());
  usbCameraPreview_->setMaximumSize(image.size());
  usbCameraPreview_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  usbCameraPreview_->setText(QString());
  usbCameraPreview_->setPixmap(QPixmap::fromImage(image));
}

void MainWindow::updatePointCloudReady() {
  // Drain the legacy decimated-cloud notification. The window receives the
  // complete native scan and its detection together via profileObservationReady.
  if (devices_) devices_->takeLatestPointCloudProfile();
}

void MainWindow::setPointCloudPlane(int) {
  if (!pointCloudPlaneBox_ || !pointCloud_) return;
  // Native weld geometry is always X/Z; old saved XY/YZ preferences must
  // not change the live weld display or its boundary-line coordinate system.
  const QSignalBlocker blocker(pointCloudPlaneBox_);
  pointCloudPlaneBox_->setCurrentIndex(1);
  pointCloudPlaneBox_->setEnabled(false);
  pointCloud_->setProjectionPlane(1);
  if (pointCloudGroup_)
    pointCloudGroup_->setTitle(CRAWLING_TEXT("SDK 实时轮廓与焊道定位（X/Z）"));
}

void MainWindow::startAutoCorrection() {
  if (autoCorrectionActive_ || autoCorrectionStartPending_) {
    appendLog(CRAWLING_TEXT("自动纠偏正在运行或等待相机准备，请查看纠偏状态；可点击停止取消"));
    return;
  }
  if (!connected_ || currentState_ != DriveState::Enabled) {
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
                       QStringLiteral("event=start_auto_correction result=REJECTED connected=%1 drive_state=%2")
                           .arg(connected_).arg(static_cast<int>(currentState_)));
    appendLog(CRAWLING_TEXT("\xE8\xAF\xB7""\xE5\x85\x88""\xE8\xBF\x9E""\xE6\x8E\xA5""\xE5\xB9\xB6""\xE4\xBD\xBF""\xE8\x83\xBD""\xE5\xBA\x95""\xE7\x9B\x98""\xEF\xBC\x8C""\xE5\x86\x8D""\xE5\x90\xAF""\xE5\x8A\xA8""\xE8\x87\xAA""\xE5\x8A\xA8""\xE7\xBA\xA0""\xE5\x81\x8F""\xE3\x80\x82"""));
    return;
  }
  LaserCorrectionSettings settings;
  settings.targetSpeedMps = correctionSpeedBox_->value() / kMillimetersPerMeter;
  settings.segmentLengthM = correctionSegmentBox_->value() / kMillimetersPerMeter;
  settings.proportionalGain = correctionKpBox_->value();
  settings.derivativeGain = correctionKdBox_->value();
  settings.wheelRadiusM = settings_.wheelRadiusM;
  settings.trackWidthM = settings_.trackWidthM;
  settings.minimumInnerWheelRatio = settings_.minimumInnerWheelRatio;
  const double motorWheelLimitMps =
      WheelMotorConfig::kMaximumSynchronizedMotorSpeedDps *
      settings_.wheelRadiusM * kRadiansPerDegree /
      settings_.motorOutputToWheelRatio;
  const double correctionSpeedLimitMps = std::min(
      {settings_.maximumLinearSpeedMps, settings_.maximumWheelSpeedMps,
       motorWheelLimitMps});
  if (!std::isfinite(settings.targetSpeedMps) ||
      !std::isfinite(correctionSpeedLimitMps) ||
      settings.targetSpeedMps <= 0.0 ||
      settings.targetSpeedMps > correctionSpeedLimitMps) {
    AppLogger::warning(QStringLiteral("UI.OPERATION"),
        QStringLiteral("event=start_auto_correction result=REJECTED reason=speed_limit requested_mps=%1 limit_mps=%2")
            .arg(settings.targetSpeedMps).arg(correctionSpeedLimitMps));
    appendLog(CRAWLING_TEXT("纠偏速度 %1 mm/s 超过当前可用范围（上限 %2 mm/s），未启动；请调整纠偏速度。")
                  .arg(settings.targetSpeedMps * kMillimetersPerMeter, 0, 'f', 1)
                  .arg(correctionSpeedLimitMps * kMillimetersPerMeter, 0, 'f', 1));
    return;
  }
  QSettings persistent(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/speed"), settings.targetSpeedMps);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/segmentLength"), settings.segmentLengthM);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/kp"), settings.proportionalGain);
  persistent.setValue(CRAWLING_TEXT("laserCorrection/kd"), settings.derivativeGain);
  persistent.sync();
  QMetaObject::invokeMethod(correction_, "setSettings", Qt::QueuedConnection,
                           Q_ARG(crawling::LaserCorrectionSettings, settings));
  cancelMotionButtonPulses();
  activeMotionKeys_.clear();
  keyboardMotionKeys_.clear();
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=start_auto_correction target=CORRECTION.CONTROL speed_mps=%1 segment_m=%2 kp=%3 kd=%4")
                       .arg(settings.targetSpeedMps).arg(settings.segmentLengthM)
                       .arg(settings.proportionalGain).arg(settings.derivativeGain));
  // Ownership changes at the click, before the worker's status arrives.
  // Otherwise the manual timer can insert a zero command during startup.
  autoCorrectionActive_ = true;
  autoCorrectionStartPending_ = true;
  autoStartButton_->setEnabled(false);
  autoStopButton_->setEnabled(true);
  appendLog(CRAWLING_TEXT("已收到启动请求，正在准备相机轮廓流，尚未启动纠偏运动"));
  if (correctionStatusLabel_) correctionStatusLabel_->setText(CRAWLING_TEXT("启动准备：等待相机轮廓流"));
  const quint64 noticeGeneration = ++correctionStartNoticeGeneration_;
  if (!QMetaObject::invokeMethod(devices_, "prepareCorrectionProfile", Qt::QueuedConnection)) {
    autoCorrectionStartPending_ = autoCorrectionActive_ = false;
    autoStartButton_->setEnabled(true);
    autoStopButton_->setEnabled(false);
    appendLog(CRAWLING_TEXT("相机准备请求投递失败，自动纠偏未启动"));
    if (correctionStatusLabel_) correctionStatusLabel_->setText(CRAWLING_TEXT("启动失败：相机准备请求未投递"));
    return;
  }
  // This timer runs on the UI thread, so a blocking camera SDK call cannot
  // hide startup progress. It does not enable motion or extend observations.
  QTimer::singleShot(5000, this, [this, noticeGeneration]() {
    if (!autoCorrectionStartPending_ || noticeGeneration != correctionStartNoticeGeneration_) return;
    appendLog(CRAWLING_TEXT("相机启动准备超过 5 秒仍未完成，可能阻塞在 SDK 调用；可点击停止取消"));
    if (correctionStatusLabel_) correctionStatusLabel_->setText(CRAWLING_TEXT("等待相机响应超过 5 秒；可停止取消"));
  });
}
void MainWindow::stopAutoCorrection() {
  if (!correction_) return;
  const bool preparing = autoCorrectionStartPending_;
  autoCorrectionStartPending_ = false;
  if (preparing)
    QMetaObject::invokeMethod(devices_, "restoreOriginalPreview", Qt::QueuedConnection);
  AppLogger::write(QStringLiteral("UI.OPERATION"),
                   QStringLiteral("event=stop_auto_correction target=CORRECTION.CONTROL"));
  QMetaObject::invokeMethod(correction_, "setEnabled", Qt::QueuedConnection,
                           Q_ARG(bool, false));
}
void MainWindow::updateCorrectionStatus(const LaserCorrectionStatus& status) {
  if (!correctionStatusLabel_) return;
  // The worker publishes its active acknowledgement before any subsequent
  // stop/error. Earlier inactive messages cannot return ownership to manual
  // input during this queued startup handshake.
  if (autoCorrectionStartPending_ && !status.active) return;
  if (status.active) autoCorrectionStartPending_ = false;
  const QString gapPosition = status.gapValid
                                   ? CRAWLING_TEXT("线内 %1%，全图 %2%（基准 %3%，中心 %4 px，%5，%6）")
                                         .arg(status.gapCenterRatio * 100.0, 0, 'f', 1)
                                         .arg(status.gapAbsoluteCenterRatio * 100.0, 0, 'f', 1)
                                         .arg(status.referenceGapAbsoluteCenterRatio * 100.0, 0, 'f', 1)
                                         .arg((status.gapStartPx + status.gapEndPx) * 0.5,
                                              0, 'f', 1)
                                         .arg(status.detectionHeld
                                                  ? CRAWLING_TEXT("沿用/预测")
                                                  : status.contourFallback
                                                      ? CRAWLING_TEXT("凸起轮廓")
                                                      : status.edgeBreakFallback
                                                          ? CRAWLING_TEXT("边缘推断")
                                                          : CRAWLING_TEXT("双边缘原始图"))
                                         .arg(status.horizontalLaser ? CRAWLING_TEXT("横向激光线")
                                                                     : CRAWLING_TEXT("纵向激光线"))
                                  : CRAWLING_TEXT("--");
  const QString trajectoryImage = status.lastTrajectoryImage.isEmpty()
                                      ? CRAWLING_TEXT("等待本段完成")
                                      : QFileInfo(status.lastTrajectoryImage).fileName();
  correctionStatusLabel_->setText(
       CRAWLING_TEXT("%1 [%2]\n断口：%3，位置：%4，车体左右边缘：%5 / %6 mm，置信度：%7\n分段：%8 mm，样本：%9，车体中心偏差：%10 mm，实际偏差角：%11°，航向误差：%12°，RMS：%13 mm，周期：%14\n命令：%15 mm/s，%16 deg/s，点云图：%17")
          .arg(status.reason)
          .arg(status.phase)
          .arg(status.gapValid ? CRAWLING_TEXT("有效") : CRAWLING_TEXT("无效"))
          .arg(gapPosition)
          .arg(status.leftEdgeLateralM * kMillimetersPerMeter, 0, 'f', 1)
          .arg(status.rightEdgeLateralM * kMillimetersPerMeter, 0, 'f', 1)
          .arg(status.confidence, 0, 'f', 2)
          .arg(status.segmentProgressM * kMillimetersPerMeter, 0, 'f', 1)
          .arg(status.collectedSamples)
           .arg(status.laserCenterErrorM * kMillimetersPerMeter, 0, 'f', 1)
           .arg(status.fittedAngleRad * kDegreesPerRadian, 0, 'f', 2)
           .arg(status.headingErrorRad * kDegreesPerRadian, 0, 'f', 2)
           .arg(status.fitRmsErrorM * kMillimetersPerMeter, 0, 'f', 2)
           .arg(status.cycleCount)
           .arg(status.linearCommandMps * kMillimetersPerMeter, 0, 'f', 1)
           .arg(status.angularCommandRadps * kDegreesPerRadian, 0, 'f', 1)
          .arg(trajectoryImage));
  autoStartButton_->setEnabled(!status.active); autoStopButton_->setEnabled(status.active);
  if (autoCorrectionActive_ && !status.active)
    QMetaObject::invokeMethod(devices_, "restoreOriginalPreview", Qt::QueuedConnection);
  autoCorrectionActive_ = status.active;
}

DriveSettings MainWindow::settingsFromUi() const {
  DriveSettings value = settings_;
  value.leftMotorSerialPort = leftMotorPortBox_->currentData().toString();
  value.leftMotorBaudRate = leftMotorBaudBox_->currentData().toInt();
  value.rightMotorSerialPort = rightMotorPortBox_->currentData().toString();
  value.rightMotorBaudRate = rightMotorBaudBox_->currentData().toInt();
  value.imuSerialPort = imuPortBox_->currentData().toString();
  value.imuBaudRate = imuBaudBox_->currentData().toInt();
  value.imuOutputDivider = imuDividerBox_->currentData().toInt();
  value.laserSerialNumber = laserSerialBox_->text().trimmed();
  value.usbCameraDeviceIndex = usbCameraDeviceBox_->currentIndex() >= 0
                                   ? usbCameraDeviceBox_->currentData().toInt()
                                   : -1;
  value.usbCameraFps = usbCameraFpsBox_->value();
  value.usbCameraAutoConnect = usbCameraAutoConnectBox_->isChecked();
  value.usbCameraFlipHorizontal = usbCameraFlipHorizontalBox_->isChecked();
  value.usbCameraFlipVertical = usbCameraFlipVerticalBox_->isChecked();
  value.autoConnectOnStartup = autoConnectCheckBox_->isChecked();
  value.manualJogPercent = speedSlider_->value();
  value.clampSerialPort = clampSerialPortBox_->currentData().toString();
  value.clampSerialBaudRate = clampSerialBaudBox_->currentData().toInt();
  value.clampCanBitrate = clampCanBitrateBox_->currentData().toInt();
  value.clampNodeId = clampNodeIdBox_->value();
  value.clampXMotorId = clampXMotorIdBox_->value();
  value.clampYMotorId = clampYMotorIdBox_->value();
  value.clampZMotorId = clampZMotorIdBox_->value();
  value.clampXMotorSign = clampXMotorSignBox_->currentData().toInt();
  value.clampYMotorSign = clampYMotorSignBox_->currentData().toInt();
  value.clampZMotorSign = clampZMotorSignBox_->currentData().toInt();
  value.leftMotorId = leftMotorIdBox_->value();
  value.rightMotorId = rightMotorIdBox_->value();
  value.leftMotorSign = leftSignBox_->currentData().toInt();
  value.rightMotorSign = rightSignBox_->currentData().toInt();
  value.wheelRadiusM = wheelRadiusBox_->value() / kMillimetersPerMeter;
  value.trackWidthM = trackWidthBox_->value() / kMillimetersPerMeter;
  value.motorOutputToWheelRatio = ratioBox_->value();
  value.maximumWheelSpeedMps = maxWheelSpeedBox_->value() / kMillimetersPerMeter;
  value.maximumLinearSpeedMps = maxLinearSpeedBox_->value() / kMillimetersPerMeter;
  value.maximumAngularSpeedRadps = maxAngularSpeedBox_->value() * kRadiansPerDegree;
  value.maximumLinearAccelerationMps2 = maxLinearAccelBox_->value() / kMillimetersPerMeter;
  value.maximumAngularAccelerationRadps2 = maxAngularAccelBox_->value() * kRadiansPerDegree;
  value.minimumInnerWheelRatio = minimumInnerRatioBox_->value();
  value.commandTimeoutMs = commandTimeoutBox_->value();
  value.feedbackTimeoutMs = feedbackTimeoutBox_->value();
  value.armingTimeoutMs = armingTimeoutBox_->value();
  value.synchronizer.proportionalGain = synchronizationPBox_->value();
  value.synchronizer.integralGain = synchronizationIBox_->value();
  value.synchronizer.maximumCorrectionMps = maxCorrectionBox_->value() / kMillimetersPerMeter;
  value.synchronizer.minimumControlledSpeedMps = minSyncSpeedBox_->value() / kMillimetersPerMeter;
  if (clampMotors_) clampMotors_->setSettings(value);
  return value;
}

void MainWindow::settingsToUi(const DriveSettings& settings) {
  setComboToText(leftMotorPortBox_, settings.leftMotorSerialPort);
  setComboToValue(leftMotorBaudBox_, settings.leftMotorBaudRate);
  setComboToText(rightMotorPortBox_, settings.rightMotorSerialPort);
  setComboToValue(rightMotorBaudBox_, settings.rightMotorBaudRate);
  setComboToText(imuPortBox_, settings.imuSerialPort);
  setComboToValue(imuBaudBox_, settings.imuBaudRate);
  setComboToValue(imuDividerBox_, settings.imuOutputDivider);
  laserSerialBox_->setText(settings.laserSerialNumber);
  setComboToValue(usbCameraDeviceBox_, settings.usbCameraDeviceIndex);
  if (settings.usbCameraDeviceIndex < 0) usbCameraDeviceBox_->setCurrentIndex(-1);
  usbCameraFpsBox_->setValue(settings.usbCameraFps);
  usbCameraAutoConnectBox_->setChecked(settings.usbCameraAutoConnect);
  usbCameraFlipHorizontalBox_->setChecked(settings.usbCameraFlipHorizontal);
  usbCameraFlipVerticalBox_->setChecked(settings.usbCameraFlipVertical);
  autoConnectCheckBox_->setChecked(settings.autoConnectOnStartup);
  speedSlider_->setValue(settings.manualJogPercent);
  setComboToText(clampSerialPortBox_, settings.clampSerialPort);
  setComboToValue(clampSerialBaudBox_, settings.clampSerialBaudRate);
  setComboToValue(clampCanBitrateBox_, settings.clampCanBitrate);
  clampNodeIdBox_->setValue(settings.clampNodeId);
  clampXMotorIdBox_->setValue(settings.clampXMotorId);
  clampYMotorIdBox_->setValue(settings.clampYMotorId);
  clampZMotorIdBox_->setValue(settings.clampZMotorId);
  setComboToValue(clampXMotorSignBox_, settings.clampXMotorSign);
  setComboToValue(clampYMotorSignBox_, settings.clampYMotorSign);
  setComboToValue(clampZMotorSignBox_, settings.clampZMotorSign);
  leftMotorIdBox_->setValue(settings.leftMotorId);
  rightMotorIdBox_->setValue(settings.rightMotorId);
  setComboToValue(leftSignBox_, settings.leftMotorSign);
  setComboToValue(rightSignBox_, settings.rightMotorSign);
  wheelRadiusBox_->setValue(settings.wheelRadiusM * kMillimetersPerMeter);
  trackWidthBox_->setValue(settings.trackWidthM * kMillimetersPerMeter);
  ratioBox_->setValue(settings.motorOutputToWheelRatio);
  maxWheelSpeedBox_->setValue(settings.maximumWheelSpeedMps * kMillimetersPerMeter);
  maxLinearSpeedBox_->setValue(settings.maximumLinearSpeedMps * kMillimetersPerMeter);
  maxAngularSpeedBox_->setValue(settings.maximumAngularSpeedRadps * kDegreesPerRadian);
  maxLinearAccelBox_->setValue(settings.maximumLinearAccelerationMps2 * kMillimetersPerMeter);
  maxAngularAccelBox_->setValue(settings.maximumAngularAccelerationRadps2 * kDegreesPerRadian);
  minimumInnerRatioBox_->setValue(settings.minimumInnerWheelRatio);
  commandTimeoutBox_->setValue(settings.commandTimeoutMs);
  feedbackTimeoutBox_->setValue(settings.feedbackTimeoutMs);
  armingTimeoutBox_->setValue(settings.armingTimeoutMs);
  synchronizationPBox_->setValue(settings.synchronizer.proportionalGain);
  synchronizationIBox_->setValue(settings.synchronizer.integralGain);
  maxCorrectionBox_->setValue(settings.synchronizer.maximumCorrectionMps * kMillimetersPerMeter);
  minSyncSpeedBox_->setValue(settings.synchronizer.minimumControlledSpeedMps * kMillimetersPerMeter);
}

void MainWindow::setMotionKey(MotionKey key, bool active) {
  const int keyValue = static_cast<int>(key);
  const bool wasActive = keyboardMotionKeys_.contains(keyValue);
  if (active) {
    keyboardMotionKeys_.insert(keyValue);
  } else {
    keyboardMotionKeys_.remove(keyValue);
  }
  if (keyboardMotionKeys_.contains(keyValue) || pressedMotionButtons_.contains(keyValue)) {
    activeMotionKeys_.insert(keyValue);
  } else {
    activeMotionKeys_.remove(keyValue);
  }
  if (wasActive != active) {
    AppLogger::write(QStringLiteral("UI.MOTION"),
                     QStringLiteral("event=keyboard_motion direction=%1 action=%2 target=DRIVE.CONTROL")
                         .arg(motionKeyName(keyValue),
                              active ? QStringLiteral("press") : QStringLiteral("release")));
  }
  // Do not wait for the 40 ms UI timer for a safety-critical direction
  // transition.
  if (!autoCorrectionActive_) {
    updateManualCommand();
  }
}

void MainWindow::pressMotionButton(MotionKey key) {
  const int keyValue = static_cast<int>(key);
  const qint64 now = motionClock_.elapsed();
  // Touch taps use a short pulse after release. Replace an opposite pending
  // tap immediately so two successive taps do not create an artificial zero
  // command between forward and reverse motion.
  if (key == MotionKey::Forward || key == MotionKey::Reverse) {
    const int opposite = static_cast<int>(key == MotionKey::Forward
                                              ? MotionKey::Reverse
                                              : MotionKey::Forward);
    if (!pressedMotionButtons_.contains(opposite) &&
        !keyboardMotionKeys_.contains(opposite)) {
      activeMotionKeys_.remove(opposite);
      ++motionPulseGeneration_[opposite];
    }
  }
  pressedMotionButtons_.insert(keyValue);
  motionButtonPressMs_.insert(keyValue, now);
  ++motionPulseGeneration_[keyValue];
  activeMotionKeys_.insert(keyValue);
  AppLogger::write(QStringLiteral("UI.MOTION"),
                   QStringLiteral("event=button_motion direction=%1 action=press target=DRIVE.CONTROL")
                       .arg(motionKeyName(keyValue)));
  if (!autoCorrectionActive_) {
    updateManualCommand();
  }
}

void MainWindow::releaseMotionButton(MotionKey key) {
  const int keyValue = static_cast<int>(key);
  if (!pressedMotionButtons_.remove(keyValue)) {
    return;
  }

  motionButtonPressMs_.remove(keyValue);
  const bool keyboardStillActive = keyboardMotionKeys_.contains(keyValue);
  // Touch direction buttons are momentary controls: release immediately
  // removes the direction, sends 0x81, then holds the current position with
  // 0x92 + 0xA4. The previous
  // delayed click pulse kept a stale direction alive for 360 ms, then issued
  // an abrupt zero command while the operator was tapping repeatedly; at
  // higher speed this looked like a brief direction reversal.
  ++motionPulseGeneration_[keyValue];
  if (!keyboardStillActive) {
    activeMotionKeys_.remove(keyValue);
  }
  AppLogger::write(QStringLiteral("UI.MOTION"),
                   QStringLiteral("event=button_motion direction=%1 action=release target=DRIVE.CONTROL")
                       .arg(motionKeyName(keyValue)));
  if (!autoCorrectionActive_) {
    updateManualCommand();
  }
}

void MainWindow::cancelMotionButtonPulses() {
  // Invalidate every queued singleShot callback, including callbacks for a
  // click that has already been released.
  for (auto it = motionPulseGeneration_.begin(); it != motionPulseGeneration_.end(); ++it) {
    ++it.value();
  }
  pressedMotionButtons_.clear();
  motionButtonPressMs_.clear();
  const QSet<int> activeKeys = activeMotionKeys_;
  for (const int keyValue : activeKeys) {
    if (!keyboardMotionKeys_.contains(keyValue)) {
      activeMotionKeys_.remove(keyValue);
    }
  }
}

bool MainWindow::motionKey(MotionKey key) const {
  return activeMotionKeys_.contains(static_cast<int>(key));
}

void MainWindow::setValueLabel(QLabel* label, const QString& value) {
  QString display = value;
  label->setText(display.replace(CRAWLING_TEXT(" m/s"), CRAWLING_TEXT(" mm/s"))
                     .replace(CRAWLING_TEXT(" rad/s"), CRAWLING_TEXT(" deg/s")));
}

void MainWindow::setStatePresentation(DriveState state, const QString& reason) {
  stateLabel_->setText(driveStateText(state).toUpper());
  reasonLabel_->setText(reason);
  QString color = CRAWLING_TEXT("#4a5561");
  QString background = CRAWLING_TEXT("#e3e8ed");
  if (state == DriveState::Enabled) {
    color = CRAWLING_TEXT("#12633d");
    background = CRAWLING_TEXT("#d8f0e5");
  } else if (state == DriveState::Arming) {
    color = CRAWLING_TEXT("#8a4b08");
    background = CRAWLING_TEXT("#fce9cb");
  } else if (state == DriveState::Fault || state == DriveState::EmergencyStop) {
    color = CRAWLING_TEXT("#9f1f16");
    background = CRAWLING_TEXT("#fde0dd");
  }
  stateLabel_->setStyleSheet(CRAWLING_TEXT("background: %1; color: %2;").arg(background, color));
}

QDoubleSpinBox* MainWindow::makeDoubleSpin(double minimum, double maximum,
                                            double step, int decimals, QWidget* parent) {
  auto* box = new QDoubleSpinBox(parent);
  box->setRange(minimum, maximum);
  box->setSingleStep(step);
  box->setDecimals(decimals);
  box->setKeyboardTracking(false);
  return box;
}

}  // namespace crawling

