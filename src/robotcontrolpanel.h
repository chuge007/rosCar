#pragma once

#include "drive_settings.h"
#include "drive_types.h"
#include "laser_correction_controller.h"
#include "laser_trajectory_renderer.h"
#include "rim302_protocol.h"
#include "wheel_motor_controller.h"
#include "services/remote_workbench_session.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QSize>
#include <QVector>
#include <QVector3D>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QCamera;
class QCheckBox;
class QDialog;
class QDoubleSpinBox;
class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QSlider;
class QSpinBox;
class QTabWidget;
class QThread;
class QTimer;
class RobotProfileView;

namespace crawling {
class ClampMotorController;
class ClampMotorSettingsWidget;
class ClampMotorControlWidget;
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
    RobotControlPanel(crawling::ClampMotorController *clampController,
                      pa1664::remote::RemoteWorkbenchSession *remoteSession,
                      QWidget *parent);
    ~RobotControlPanel() override;

    void showInformationDialog(QWidget *parent = nullptr);
    void showConfigurationPage();
    // 「车体控制」滑块组（纠偏速度/分段长度/Kp/Kd/内侧轮最小比例 + 保存并应用）。
    // 该分组在底盘控制页不再展示，由主窗口「状态」页取走嵌入（父窗口转移），
    // 信号连接仍归属本面板，reparent 后照常生效。
    QWidget *vehicleControlWidget() const;

public slots:
    void setLaserProfileImage(const QImage &image);
    void setLaserProfilePoints(const QVector<QVector3D> &points);
    void setUsbCameraImage(int slot, const QImage &image);
    // force=true 用在连接/重连之后：采集端会把目标尺寸重置为默认预览值，
    // 此时即使标签尺寸没变也必须重新下发一次。
    void pushUsbDisplaySize(int slot, bool force = false);
    void startAutoCorrection();
    void stopAutoCorrection();
    void setAutoCorrectionPaused(bool paused);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    // 摄像头预览标签的尺寸不会触发面板自身的 resizeEvent，单独拦截它的 Resize，
    // 把新尺寸同步给采集端并按新尺寸重绘当前帧。
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void connectConfiguredUsbCameraSlot(int slot);
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
    void updateUsbDevices(const QStringList &devices);
    void updateUsbConnection(int slot, bool connected, const QString &message);
    void updateClampConnection(bool connected, const QString &message);
    void appendLog(const QString &message);
    void onRemoteStateChanged(
        pa1664::remote::WorkbenchConnectionState state,
        const QString &detail);
    void onRemoteResponseReceived(const QString &operation, quint64 requestId,
                                  bool ok, const QJsonObject &data,
                                  const QString &errorCode,
                                  const QString &errorText);
    void onRemoteEventReceived(const QString &eventName,
                               const QJsonObject &data);
    void onRemoteTelemetry(const QJsonObject &telemetry);
    void applyRemotePeripheralSnapshot(const QJsonObject &snapshot);
    void onRemoteMediaReceived(
        quint16 streamId, pa1664::remote::MediaStreamKind kind,
        quint64 messageId, quint64 timestampUs, const QByteArray &payload);
    void queueHeavyMediaPayload(
        quint16 streamId, pa1664::remote::MediaStreamKind kind,
        const QByteArray &payload);
    void drainPendingMedia();

signals:
    // 车体状态快照（12 项，顺序固定）：运行状态/状态说明/适配器/目标运动/实际运动/
    // 左轮/右轮/反馈看门狗/双轮同步/IMU/激光/USB。供主窗口「状态」分区复用展示。
    void vehicleStatusChanged(const QStringList &metrics);
    void autoCorrectionPausedChanged(bool paused);
    void autoCorrectionTrackingStarted();

private:
    // Operator-side media accounting: shows whether frames actually arrive at
    // the console, independent of what the host believes it sent.
    quint64 m_mediaRxCount = 0;
    quint64 m_mediaRxBytes = 0;
    quint64 m_mediaRxLaserImage = 0;
    quint64 m_mediaRxLaserPoints = 0;
    quint64 m_mediaRxUsb[2] = {0, 0};
    quint64 m_mediaRxImu = 0;
    qint64 m_mediaRxLastLogMs = 0;
    // Receiving a media message is not the same as showing a picture: these
    // counters follow the payload through decodeImagePayload(), the visibility
    // gate and setPixmap(), so a frozen preview can be attributed to a stage.
    quint64 m_mediaDecodeOk[2] = {0, 0};
    quint64 m_mediaDecodeFailed[2] = {0, 0};
    quint64 m_mediaViewHidden[2] = {0, 0};
    quint64 m_mediaPixmapSet[2] = {0, 0};
    quint64 m_mediaProfileObservations = 0;
    quint64 m_mediaProfileDecodeFailed = 0;
    quint64 m_mediaRepaints[2] = {0, 0};
    // Per-stream application payload bytes received in the current 5 s window.
    QHash<quint16, quint64> m_mediaRxStreamBytes;
    // Latest-value mailbox for heavy media payloads (point clouds, camera
    // JPEGs): decoding runs on a fixed 30 ms drain tick so bursts cannot
    // backlog the UI thread; a newer frame replaces the queued older one.
    QHash<quint16, QByteArray> m_pendingMediaPayloads;
    QHash<quint16, pa1664::remote::MediaStreamKind> m_pendingMediaKinds;
    QTimer *m_mediaDrainTimer = nullptr;
    QTimer *m_cameraRepaintTimer = nullptr;
    void buildUi();
    void loadSettings();
    bool saveClampSettings();
    void emitVehicleStatus();
    crawling::DriveSettings settingsFromUi() const;
    void settingsToUi(const crawling::DriveSettings &settings);
    void setMotion(bool &flag, bool active);
    void clearJogLatch();
    void releaseAllJog(const QString &reason);
    void requestEnable(bool enabled);
    void emergencyStop();
    void stopMotion();
    void setStateStyle(crawling::DriveState state);
    void updateInformationDialog();
    void displayUsbCameraImage(int slot, const QImage &image);
    // 把一帧按标签当前物理尺寸等比铺满（居中裁掉溢出部分）后显示，避免
    // 固定小尺寸帧在标签里居中造成四周大片空白。
    void applyUsbPreviewPixmap(int slot, const QImage &image);
    // 让预览标签的高度跟随当前画面宽高比：框内不再多出上下空白，画面也不被裁切。
    void updateUsbPreviewHeight(int slot);
    void flushLogUi();
    void refreshVisibleUi();
    void updateUsbCameraSourceUi(int slot);
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

    // 车体控制页的可拖动参数：滑块整数位置按 scale 换算成工程值，数值标签随拖动刷新。
    struct ParameterSlider
    {
        QSlider *slider = nullptr;
        QLabel *valueLabel = nullptr;
        double scale = 1.0;
        int decimals = 1;

        double number() const;
        void setNumber(double value);
        void refreshValueText();
    };
    static QWidget *createParameterSlider(QWidget *parent, ParameterSlider *param,
                                          const QString &title, const QString &hint,
                                          int minimum, int maximum, int decimals,
                                          double scale);
    void applyConfigurationSettings();

    crawling::SynchronizedDriveController *m_controller = nullptr;
    pa1664::remote::RemoteWorkbenchSession *m_remoteSession = nullptr;
    QPointer<crawling::ClampMotorController> m_clampController;
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
    QTimer *m_logFlushTimer = nullptr;
    QElapsedTimer m_telemetryUiClock;
    QElapsedTimer m_imuUiClock;
    crawling::DriveSettings m_settings;
    crawling::DriveState m_state = crawling::DriveState::Disconnected;
    bool m_jogLatched = false;
    QPushButton *m_pressedJogButton = nullptr;
    bool *m_pressedJogFlag = nullptr;
    QPushButton *m_latchedJogButton = nullptr;
    bool *m_latchedJogFlag = nullptr;
    QTimer *m_jogHoldTimer = nullptr;
    bool m_forward = false;
    bool m_reverse = false;
    bool m_left = false;
    bool m_right = false;
    bool m_connected = false;
    bool m_remoteLinkReady = false;
    bool m_remoteAutoConnectAttempted = false;
    bool m_imuConnected = false;
    bool m_laserConnected = false;
    bool m_usbConnectedSlot[crawling::kUsbCameraChannelCount] = {false, false};
    bool m_autoCorrectionActive = false;
    bool m_autoCorrectionStartPending = false;
    bool m_autoCorrectionPaused = false;
    bool m_profileDetectionLogSessionActive = false;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QGroupBox *m_vehicleControlGroup = nullptr;
    RobotProfileView *m_laserView = nullptr;
    QLabel *m_usbViews[crawling::kUsbCameraChannelCount] = {nullptr, nullptr};
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedLabel = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_enableButton = nullptr;
    QPushButton *m_stopMotionButton = nullptr;
    QPushButton *m_connectAllButton = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QCheckBox *m_profileDetectionLogCheck = nullptr;
    QComboBox *m_profileDetectionMode = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_laserConfigState = nullptr;
    QLabel *m_laserFrameState = nullptr;
    QLabel *m_imuConfigState = nullptr;
    QLabel *m_clampConfigState = nullptr;
    QLabel *m_imuStatus = nullptr;
    QLabel *m_laserStatus = nullptr;
    QLabel *m_usbStatus = nullptr;
    QLabel *m_correctionStatus = nullptr;
    ParameterSlider m_correctionSpeed;
    ParameterSlider m_correctionSegment;
    ParameterSlider m_correctionKp;
    ParameterSlider m_correctionKd;
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
    ParameterSlider m_minimumInnerRatio;
    QSpinBox *m_commandTimeout = nullptr;
    QSpinBox *m_feedbackTimeout = nullptr;
    QSpinBox *m_armingTimeout = nullptr;
    QSpinBox *m_laserInputTimeout = nullptr;
    QComboBox *m_imuPort = nullptr;
    QComboBox *m_imuBaud = nullptr;
    QComboBox *m_imuDivider = nullptr;
    QLineEdit *m_laserSerial = nullptr;
    QComboBox *m_laserDevice = nullptr;
    struct UsbCameraUi {
        QComboBox *source = nullptr;
        QComboBox *device = nullptr;
        QLineEdit *networkUrl = nullptr;
        QSpinBox *fps = nullptr;
        QCheckBox *autoConnect = nullptr;
        QCheckBox *flipHorizontal = nullptr;
        QCheckBox *flipVertical = nullptr;
        QPushButton *scan = nullptr;
        QPushButton *connect = nullptr;
        QPushButton *disconnect = nullptr;
        QLabel *configState = nullptr;
    };
    UsbCameraUi m_usbCameraUi[crawling::kUsbCameraChannelCount];
    crawling::ClampMotorSettingsWidget *m_clampSettings = nullptr;
    crawling::ClampMotorControlWidget *m_clampControls = nullptr;
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
    QStringList m_pendingLogLines;
    bool m_logViewDirty = false;
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
    QImage m_usbImages[crawling::kUsbCameraChannelCount];
    // 已下发给采集端的显示尺寸（物理像素）。标签尺寸变化会触发 Resize 事件，
    // 用它去重，避免布局期间的重复请求。
    QSize m_usbPushedSizes[crawling::kUsbCameraChannelCount];
    QVector<QVector3D> m_laserPoints;
    crawling::ProfileWeldTuning m_profileWeldTuning;
    QVector<crawling::ProfileWeldTemplate> m_profileTemplates;
};
