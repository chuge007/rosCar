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
#include <QVector>
#include <QVector3D>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
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
class DeviceWindow;
class PointCloudView;
class QCheckBox;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(SynchronizedDriveController* controller, DeviceController* devices, LaserCorrectionController* correction,
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
  void showDevices();
  void autoDetectHardware();
  void applySensorDetection(const QString& imuPort, int imuBaudRate,
                            const QString& laserSerialNumber);
  void applyCanDetection(const crawling::HardwareDetectionResult& result);
  void updatePointCloudReady();
  void setPointCloudPlane(int plane);
  void startAutoCorrection();
  void stopAutoCorrection();
  void setCorrectionReference();
  void updateCorrectionStatus(const crawling::LaserCorrectionStatus& status);
  void saveScreenshot();

 private:
  enum class MotionKey { Forward, Reverse, Left, Right };

  void buildInterface();
  void bindController();
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
  DeviceWindow* deviceWindow_ = nullptr;
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
  bool isClosing_ = false;
  bool autoCorrectionActive_ = false;
  DriveState currentState_ = DriveState::Disconnected;

  QComboBox* serialPortBox_ = nullptr;
  QComboBox* serialBaudBox_ = nullptr;
  QComboBox* canBitrateBox_ = nullptr;
  QComboBox* imuPortBox_ = nullptr;
  QComboBox* imuBaudBox_ = nullptr;
  QLineEdit* laserSerialBox_ = nullptr;
  QComboBox* clampSerialPortBox_ = nullptr;
  QComboBox* clampSerialBaudBox_ = nullptr;
  QComboBox* clampCanBitrateBox_ = nullptr;
  QSpinBox* clampNodeIdBox_ = nullptr;
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
  QPushButton* autoDetectButton_ = nullptr;
  QLabel* clampConnectionLabel_ = nullptr;
  bool autoDetectRunning_ = false;
  QPlainTextEdit* logOutput_ = nullptr;
  QPushButton* screenshotButton_ = nullptr;
  PointCloudView* pointCloud_ = nullptr;
  QComboBox* pointCloudPlaneBox_ = nullptr;
  QDoubleSpinBox* correctionSpeedBox_ = nullptr;
  QDoubleSpinBox* correctionKpBox_ = nullptr;
  QDoubleSpinBox* correctionIntegralBox_ = nullptr;
  QDoubleSpinBox* correctionKdBox_ = nullptr;
  QDoubleSpinBox* correctionLimitBox_ = nullptr;
  QDoubleSpinBox* correctionLookaheadBox_ = nullptr;
  QDoubleSpinBox* correctionHeadingRefBox_ = nullptr;
  QDoubleSpinBox* correctionHeadingKpBox_ = nullptr;
  QDoubleSpinBox* correctionLateralFilterBox_ = nullptr;
  QDoubleSpinBox* correctionHeadingFilterBox_ = nullptr;
  QDoubleSpinBox* correctionGyroFilterBox_ = nullptr;
  QComboBox* correctionSignBox_ = nullptr;
  QComboBox* correctionHeadingSignBox_ = nullptr;
  QComboBox* correctionGyroSignBox_ = nullptr;
  QPushButton* autoStartButton_ = nullptr;
  QPushButton* autoStopButton_ = nullptr;
  QLabel* correctionStatusLabel_ = nullptr;
  QLabel* parameterStatusLabel_ = nullptr;
  QPushButton* applyAllButton_ = nullptr;
};

}  // namespace crawling
