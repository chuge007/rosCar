#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "hardware_discovery.h"
#include "laser_correction_controller.h"
#include "rim302_protocol.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QVector3D>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QImage;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QCloseEvent;

namespace crawling {

class SynchronizedDriveController;
class DeviceController;
class PointCloudView;
class UsbCameraController;
class ClampMotorController;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(SynchronizedDriveController* controller, DeviceController* devices,
                      UsbCameraController* usbCamera, LaserCorrectionController* correction,
                      QWidget* parent = nullptr);
  void shutdownControl();

 signals:
  void connectionRequested(const crawling::DriveSettings& settings);
  void disconnectRequested();
  void commandRequested(double linearMps, double angularRadps);
  void enableRequested(bool enabled);
  void emergencyStopRequested();
  void systemResetRequested();
  void clearAlarmRequested();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void refreshPorts();
  void connectAdapter();
  void disconnectAdapter();
  void connectAllConfiguredDevices();
  void disconnectAllDevices();
  void connectConfiguredImu();
  void connectConfiguredCamera();
  void connectConfiguredUsbCamera();
  void scanConfiguredCamera();
  void saveSettings();
  void applyAllParameters();
  void enableDrive();
  void stopDrive();
  void emergencyStop();
  void systemReset();
  void clearAlarm();
  void updateManualCommand();
  void updateTelemetry(const crawling::DriveTelemetry& telemetry);
  void updateImu(const crawling::ImuSample& sample);
  void updateImuConnection(bool connected, const QString& message);
  void updateState(crawling::DriveState state, const QString& reason);
  void updateConnection(bool connected, const QString& message);
  void appendLog(const QString& message);
  void applyCanDetection(const crawling::HardwareDetectionResult& result);
  void updateCameraDevices(const QStringList& devices);
  void updateCameraConnection(bool connected, const QString& message);
  void updateCameraFrame(quint32 frameNumber, quint32 width, quint32 height,
                         quint64 pointCount);
  void updateUsbCameraDevices(const QStringList& devices);
  void updateUsbCameraConnection(bool connected, const QString& message);
  void updateUsbCameraFrame(const QImage& image);
  void updatePointCloudReady();
  void setPointCloudPlane(int plane);
  void startAutoCorrection();
  void stopAutoCorrection();
  void updateCorrectionStatus(const crawling::LaserCorrectionStatus& status);
  void saveScreenshot();

 private:
  enum class MotionKey { Forward, Reverse, Left, Right };

  void buildInterface();
  void bindController();
  void connectConfiguredDevices();
  DriveSettings settingsFromUi() const;
  void settingsToUi(const DriveSettings& settings);
  void setMotionKey(MotionKey key, bool active);
  void pressMotionButton(MotionKey key);
  void releaseMotionButton(MotionKey key);
  void cancelMotionButtonPulses();
  bool motionKey(MotionKey key) const;
  void setValueLabel(QLabel* label, const QString& value);
  void setStatePresentation(DriveState state, const QString& reason);
  static QDoubleSpinBox* makeDoubleSpin(double minimum, double maximum,
                                         double step, int decimals, QWidget* parent);

  SynchronizedDriveController* controller_ = nullptr;
  DeviceController* devices_ = nullptr;
  UsbCameraController* usbCamera_ = nullptr;
  ClampMotorController* clampMotors_ = nullptr;
  LaserCorrectionController* correction_ = nullptr;
  DriveSettings settings_;
  QTimer* inputTimer_ = nullptr;
  QSet<int> activeMotionKeys_;
  QSet<int> keyboardMotionKeys_;
  QSet<int> pressedMotionButtons_;
  QElapsedTimer motionClock_;
  QHash<int, qint64> motionButtonPressMs_;
  QHash<int, quint64> motionPulseGeneration_;
  bool connected_ = false;
  bool cameraConnected_ = false;
  bool isClosing_ = false;
  bool autoCorrectionActive_ = false;
  bool autoCorrectionStartPending_ = false;
  quint64 correctionStartNoticeGeneration_ = 0;
  DriveState currentState_ = DriveState::Disconnected;

  QComboBox* leftMotorPortBox_ = nullptr;
  QComboBox* leftMotorBaudBox_ = nullptr;
  QComboBox* rightMotorPortBox_ = nullptr;
  QComboBox* rightMotorBaudBox_ = nullptr;
  QComboBox* imuPortBox_ = nullptr;
  QComboBox* imuBaudBox_ = nullptr;
  QComboBox* imuDividerBox_ = nullptr;
  QLineEdit* laserSerialBox_ = nullptr;
  QComboBox* cameraDeviceBox_ = nullptr;
  QComboBox* usbCameraDeviceBox_ = nullptr;
  QSpinBox* usbCameraFpsBox_ = nullptr;
  QCheckBox* usbCameraAutoConnectBox_ = nullptr;
  QCheckBox* usbCameraFlipHorizontalBox_ = nullptr;
  QCheckBox* usbCameraFlipVerticalBox_ = nullptr;
  QComboBox* clampSerialPortBox_ = nullptr;
  QComboBox* clampSerialBaudBox_ = nullptr;
  QComboBox* clampCanBitrateBox_ = nullptr;
  QSpinBox* clampNodeIdBox_ = nullptr;
  QSpinBox* clampXMotorIdBox_ = nullptr;
  QSpinBox* clampYMotorIdBox_ = nullptr;
  QSpinBox* clampZMotorIdBox_ = nullptr;
  QComboBox* clampXMotorSignBox_ = nullptr;
  QComboBox* clampYMotorSignBox_ = nullptr;
  QComboBox* clampZMotorSignBox_ = nullptr;
  QSpinBox* leftMotorIdBox_ = nullptr;
  QSpinBox* rightMotorIdBox_ = nullptr;
  QComboBox* leftSignBox_ = nullptr;
  QComboBox* rightSignBox_ = nullptr;
  QDoubleSpinBox* wheelRadiusBox_ = nullptr;
  QDoubleSpinBox* trackWidthBox_ = nullptr;
  QDoubleSpinBox* ratioBox_ = nullptr;
  QDoubleSpinBox* maxWheelSpeedBox_ = nullptr;
  QDoubleSpinBox* maxLinearSpeedBox_ = nullptr;
  QDoubleSpinBox* maxAngularSpeedBox_ = nullptr;
  QDoubleSpinBox* maxLinearAccelBox_ = nullptr;
  QDoubleSpinBox* maxAngularAccelBox_ = nullptr;
  QDoubleSpinBox* minimumInnerRatioBox_ = nullptr;
  QSpinBox* commandTimeoutBox_ = nullptr;
  QSpinBox* feedbackTimeoutBox_ = nullptr;
  QSpinBox* armingTimeoutBox_ = nullptr;
  QDoubleSpinBox* synchronizationPBox_ = nullptr;
  QDoubleSpinBox* synchronizationIBox_ = nullptr;
  QDoubleSpinBox* maxCorrectionBox_ = nullptr;
  QDoubleSpinBox* minSyncSpeedBox_ = nullptr;
  QSlider* speedSlider_ = nullptr;
  QLabel* speedPercentLabel_ = nullptr;
  QLabel* stateLabel_ = nullptr;
  QLabel* reasonLabel_ = nullptr;
  QLabel* connectionLabel_ = nullptr;
  QLabel* targetValue_ = nullptr;
  QLabel* appliedValue_ = nullptr;
  QLabel* leftValue_ = nullptr;
  QLabel* rightValue_ = nullptr;
  QLabel* leftEncoderValue_ = nullptr;
  QLabel* rightEncoderValue_ = nullptr;
  QLabel* leftMotorValue_ = nullptr;
  QLabel* rightMotorValue_ = nullptr;
  QLabel* leftHealthValue_ = nullptr;
  QLabel* rightHealthValue_ = nullptr;
  QLabel* feedbackValue_ = nullptr;
  QLabel* syncValue_ = nullptr;
  QLabel* imuStateValue_ = nullptr;
  QLabel* imuOrientationValue_ = nullptr;
  QLabel* imuGyroValue_ = nullptr;
  QLabel* imuAccelerationValue_ = nullptr;
  QLabel* externalEncoderStateValue_ = nullptr;
  QLabel* clampMotorStateValue_ = nullptr;
  QGroupBox* pointCloudGroup_ = nullptr;
  QPushButton* enableButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QPushButton* emergencyButton_ = nullptr;
  QCheckBox* autoConnectCheckBox_ = nullptr;
  QPushButton* imuConnectButton_ = nullptr;
  QPushButton* cameraConnectButton_ = nullptr;
  QPushButton* usbCameraConnectButton_ = nullptr;
  QLabel* imuConfigStateLabel_ = nullptr;
  QLabel* cameraConfigStateLabel_ = nullptr;
  QLabel* cameraFrameConfigLabel_ = nullptr;
  QLabel* usbCameraConfigStateLabel_ = nullptr;
  QLabel* usbCameraPreview_ = nullptr;
  QLabel* clampConnectionLabel_ = nullptr;
  QPlainTextEdit* logOutput_ = nullptr;
  QPushButton* screenshotButton_ = nullptr;
  PointCloudView* pointCloud_ = nullptr;
  QComboBox* pointCloudPlaneBox_ = nullptr;
  QDoubleSpinBox* correctionSpeedBox_ = nullptr;
  QDoubleSpinBox* correctionSegmentBox_ = nullptr;
  QDoubleSpinBox* correctionKpBox_ = nullptr;
  QDoubleSpinBox* correctionKdBox_ = nullptr;
  QSlider* correctionKpSlider_ = nullptr;
  QSlider* correctionKdSlider_ = nullptr;
  QPushButton* autoStartButton_ = nullptr;
  QPushButton* autoStopButton_ = nullptr;
  QLabel* correctionStatusLabel_ = nullptr;
  QLabel* parameterStatusLabel_ = nullptr;
  QPushButton* applyAllButton_ = nullptr;
};

}  // namespace crawling
