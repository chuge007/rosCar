#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "laser_correction_controller.h"
#include "laser_trajectory_renderer.h"
#include "rim302_protocol.h"
#include "wheel_motor_controller.h"

#include <QImage>
#include <QList>
#include <QPointer>
#include <QVector>
#include <QVector3D>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QCamera;
class QCheckBox;
class QDialog;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QTabWidget;
class QThread;
class QTimer;
class RobotProfileView;

namespace crawling {
class ClampMotorController;
class SynchronizedDriveController;
class RobotSensorController;
class RobotUsbCameraController;
class ProfileDetectionLogWriter;
}

class RobotControlPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit RobotControlPanel(QWidget *parent = nullptr);
    RobotControlPanel(crawling::ClampMotorController *clampController,
                      QWidget *parent);
    ~RobotControlPanel() override;

    void showInformationDialog(QWidget *parent = nullptr);
    void showConfigurationPage();

public slots:
    void setLaserProfileImage(const QImage &image);
    void setLaserProfilePoints(const QVector<QVector3D> &points);
    void setUsbCameraImage(const QImage &image);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void refreshPorts();
    void connectDrive();
    void disconnectDrive();
    void connectAllConfiguredDevices();
    void disconnectAllDevices();
    void connectConfiguredDevices();
    void connectConfiguredImu();
    void connectConfiguredLaser();
    void connectConfiguredUsbCamera();
    void connectConfiguredClampMotor();
    void saveSettings();
    void restoreSettings();
    void sendMotionCommand();
    void updateTelemetry(const crawling::DriveTelemetry &telemetry);
    void updateState(crawling::DriveState state, const QString &reason);
    void updateConnection(bool connected, const QString &message);
    void updateImu(const crawling::ImuSample &sample);
    void updateImuConnection(bool connected, const QString &message);
    void updateSensorDetection(bool running, const QString &message);
    void updateLaserDevices(const QStringList &devices);
    void updateLaserConnection(bool connected, const QString &message);
    void updateLaserFrame(quint32 frame, quint32 width, quint32 height, quint64 points);
    void updateCorrectionStatus(const crawling::LaserCorrectionStatus &status);
    void startAutoCorrection();
    void stopAutoCorrection();
    void updateUsbDevices(const QStringList &devices);
    void updateUsbConnection(bool connected, const QString &message);
    void updateClampConnection(bool connected, const QString &message);
    void appendLog(const QString &message);

signals:
    // 车体状态快照（12 项，顺序固定）：运行状态/状态说明/适配器/目标运动/实际运动/
    // 左轮/右轮/反馈看门狗/双轮同步/IMU/激光/USB。供主窗口「状态」分区复用展示。
    void vehicleStatusChanged(const QStringList &metrics);

private:
    void buildUi();
    void loadSettings();
    void emitVehicleStatus();
    crawling::DriveSettings settingsFromUi() const;
    void settingsToUi(const crawling::DriveSettings &settings);
    void setMotion(bool &flag, bool active);
    void requestEnable(bool enabled);
    void emergencyStop();
    void stopMotion();
    void setStateStyle(crawling::DriveState state);
    void updateInformationDialog();
    QWidget *buildConfigurationPage();
    void persistAutoConnectSetting(bool enabled);
    void showProfileTemplateDialog();
    void loadProfileTemplates();
    void persistProfileTemplates();
    void applyProfileTemplates();
    void loadProfileWeldTuning();
    void persistProfileWeldTuning();
    void applyProfileWeldTuning();
    bool beginProfileDetectionLogSession();
    void finishProfileDetectionLogSession(const QString &reason);

    crawling::SynchronizedDriveController *m_controller = nullptr;
    crawling::ClampMotorController *m_clampController = nullptr;
    QThread *m_driveThread = nullptr;
    crawling::RobotSensorController *m_sensorController = nullptr;
    QThread *m_sensorThread = nullptr;
    crawling::RobotUsbCameraController *m_usbCameraController = nullptr;
    QThread *m_usbCameraThread = nullptr;
    crawling::LaserCorrectionController *m_correctionController = nullptr;
    QThread *m_correctionThread = nullptr;
    crawling::ProfileDetectionLogWriter *m_profileDetectionLogWriter = nullptr;
    QThread *m_profileDetectionLogThread = nullptr;
    crawling::LaserTrajectoryWriter *m_trajectoryWriter = nullptr;
    QThread *m_trajectoryThread = nullptr;
    QTimer *m_commandTimer = nullptr;
    crawling::DriveSettings m_settings;
    crawling::DriveState m_state = crawling::DriveState::Disconnected;
    bool m_forward = false;
    bool m_reverse = false;
    bool m_left = false;
    bool m_right = false;
    bool m_connected = false;
    bool m_imuConnected = false;
    bool m_laserConnected = false;
    bool m_usbConnected = false;
    bool m_autoCorrectionActive = false;
    bool m_autoCorrectionStartPending = false;
    bool m_profileDetectionLogSessionActive = false;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    RobotProfileView *m_laserView = nullptr;
    QLabel *m_usbView = nullptr;
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedLabel = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_enableButton = nullptr;
    QPushButton *m_connectAllButton = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QCheckBox *m_profileDetectionLogCheck = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_laserConfigState = nullptr;
    QLabel *m_laserFrameState = nullptr;
    QLabel *m_imuConfigState = nullptr;
    QLabel *m_usbConfigState = nullptr;
    QLabel *m_clampConfigState = nullptr;
    QLabel *m_imuStatus = nullptr;
    QLabel *m_laserStatus = nullptr;
    QLabel *m_usbStatus = nullptr;
    QLabel *m_correctionStatus = nullptr;
    QDoubleSpinBox *m_correctionSpeed = nullptr;
    QDoubleSpinBox *m_correctionSegment = nullptr;
    QDoubleSpinBox *m_correctionKp = nullptr;
    QDoubleSpinBox *m_correctionKd = nullptr;
    QPushButton *m_autoCorrectionStart = nullptr;
    QPushButton *m_autoCorrectionStop = nullptr;

    QComboBox *m_leftPort = nullptr;
    QComboBox *m_rightPort = nullptr;
    QComboBox *m_leftBaud = nullptr;
    QComboBox *m_rightBaud = nullptr;
    QComboBox *m_wheelCommunicationMode = nullptr;
    QComboBox *m_wheelCanPort = nullptr;
    QComboBox *m_wheelCanBaud = nullptr;
    QComboBox *m_wheelCanBitrate = nullptr;
    QList<QWidget *> m_wheelCanRows;
    QList<QWidget *> m_wheelRs485Rows;
    QSpinBox *m_leftId = nullptr;
    QSpinBox *m_rightId = nullptr;
    QComboBox *m_leftSign = nullptr;
    QComboBox *m_rightSign = nullptr;
    QDoubleSpinBox *m_wheelRadius = nullptr;
    QDoubleSpinBox *m_trackWidth = nullptr;
    QDoubleSpinBox *m_ratio = nullptr;
    QDoubleSpinBox *m_maxWheelSpeed = nullptr;
    QDoubleSpinBox *m_maxLinearSpeed = nullptr;
    QDoubleSpinBox *m_maxAngularSpeed = nullptr;
    QDoubleSpinBox *m_maxLinearAcceleration = nullptr;
    QDoubleSpinBox *m_maxAngularAcceleration = nullptr;
    QDoubleSpinBox *m_minimumInnerRatio = nullptr;
    QSpinBox *m_commandTimeout = nullptr;
    QSpinBox *m_feedbackTimeout = nullptr;
    QSpinBox *m_armingTimeout = nullptr;
    QComboBox *m_imuPort = nullptr;
    QComboBox *m_imuBaud = nullptr;
    QComboBox *m_imuDivider = nullptr;
    QLineEdit *m_laserSerial = nullptr;
    QComboBox *m_laserDevice = nullptr;
    QComboBox *m_cameraDevice = nullptr;
    QLineEdit *m_networkCameraUrl = nullptr;
    QSpinBox *m_cameraFps = nullptr;
    QCheckBox *m_cameraAutoConnect = nullptr;
    QCheckBox *m_cameraFlipHorizontal = nullptr;
    QCheckBox *m_cameraFlipVertical = nullptr;
    QComboBox *m_clampCommunicationMode = nullptr;
    QComboBox *m_clampPort = nullptr;
    QComboBox *m_clampBaud = nullptr;
    QComboBox *m_clampCanBitrate = nullptr;
    QSpinBox *m_clampNodeId = nullptr;
    QSpinBox *m_clampXId = nullptr;
    QSpinBox *m_clampYId = nullptr;
    QSpinBox *m_clampZId = nullptr;
    QComboBox *m_clampXSign = nullptr;
    QComboBox *m_clampYSign = nullptr;
    QComboBox *m_clampZSign = nullptr;
    QDoubleSpinBox *m_syncP = nullptr;
    QDoubleSpinBox *m_syncI = nullptr;
    QDoubleSpinBox *m_syncMaxCorrection = nullptr;
    QDoubleSpinBox *m_syncMinSpeed = nullptr;
    QPointer<QDialog> m_infoDialog;
    QPointer<QLabel> m_infoState;
    QPointer<QLabel> m_infoReason;
    QPointer<QLabel> m_infoConnection;
    QPointer<QLabel> m_infoTarget;
    QPointer<QLabel> m_infoApplied;
    QPointer<QLabel> m_infoLeftWheel;
    QPointer<QLabel> m_infoRightWheel;
    QPointer<QLabel> m_infoFeedback;
    QPointer<QLabel> m_infoSync;
    QPointer<QPlainTextEdit> m_infoLog;
    QPointer<QLabel> m_dialogState;
    QPointer<QLabel> m_dialogReason;
    QPointer<QLabel> m_dialogConnection;
    QPointer<QLabel> m_dialogTarget;
    QPointer<QLabel> m_dialogApplied;
    QPointer<QLabel> m_dialogLeftWheel;
    QPointer<QLabel> m_dialogRightWheel;
    QPointer<QLabel> m_dialogFeedback;
    QPointer<QLabel> m_dialogSync;
    QPointer<QDialog> m_profileTemplateDialog;
    QStringList m_logLines;
    QString m_reasonText = QStringLiteral("等待连接");
    QString m_targetText = QStringLiteral("--");
    QString m_appliedText = QStringLiteral("--");
    QString m_leftWheelText = QStringLiteral("--");
    QString m_rightWheelText = QStringLiteral("--");
    QString m_feedbackText = QStringLiteral("--");
    QString m_syncText = QStringLiteral("--");
    QString m_imuText = QStringLiteral("未连接");
    QString m_laserText = QStringLiteral("未连接");
    QString m_usbText = QStringLiteral("未连接");
    QImage m_laserImage;
    QImage m_usbImage;
    QVector<QVector3D> m_laserPoints;
    crawling::ProfileWeldTuning m_profileWeldTuning;
    QVector<crawling::ProfileWeldTemplate> m_profileTemplates;
};
