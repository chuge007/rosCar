#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "rim302_protocol.h"

#include <QImage>
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
class SynchronizedDriveController;
class RobotSensorController;
class RobotUsbCameraController;
}

class RobotControlPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit RobotControlPanel(QWidget *parent = nullptr);
    ~RobotControlPanel() override;

    void showInformationDialog(QWidget *parent = nullptr);
    void showConfigurationPage();

public slots:
    void setLaserProfileImage(const QImage &image);
    void setLaserProfilePoints(const QVector<QVector3D> &points);
    void setUsbCameraImage(const QImage &image);

protected:
    void resizeEvent(QResizeEvent *event) override;

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
    void updateUsbDevices(const QStringList &devices);
    void updateUsbConnection(bool connected, const QString &message);
    void appendLog(const QString &message);

private:
    void buildUi();
    void loadSettings();
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

    crawling::SynchronizedDriveController *m_controller = nullptr;
    QThread *m_driveThread = nullptr;
    crawling::RobotSensorController *m_sensorController = nullptr;
    QThread *m_sensorThread = nullptr;
    crawling::RobotUsbCameraController *m_usbCameraController = nullptr;
    QThread *m_usbCameraThread = nullptr;
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
    QTabWidget *m_tabs = nullptr;
    QLabel *m_laserConfigState = nullptr;
    QLabel *m_laserFrameState = nullptr;
    QLabel *m_imuConfigState = nullptr;
    QLabel *m_usbConfigState = nullptr;
    QLabel *m_imuStatus = nullptr;
    QLabel *m_laserStatus = nullptr;
    QLabel *m_usbStatus = nullptr;

    QComboBox *m_leftPort = nullptr;
    QComboBox *m_rightPort = nullptr;
    QComboBox *m_leftBaud = nullptr;
    QComboBox *m_rightBaud = nullptr;
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
    QSpinBox *m_cameraFps = nullptr;
    QCheckBox *m_cameraAutoConnect = nullptr;
    QCheckBox *m_cameraFlipHorizontal = nullptr;
    QCheckBox *m_cameraFlipVertical = nullptr;
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
};
