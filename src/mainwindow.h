#pragma once

#include "packetdecoder.h"
#include "rawpacket.h"
#include <QElapsedTimer>
#include <QMainWindow>

class AScanWidget;
class BScanWidget;
class CScanWidget;
class DeviceController;
class EScanWidget;
class FrameRecorder;
class FrameDecoderWorker;
class FramePlayer;
class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QThread;
class QTabWidget;
class QToolBar;
class ParameterPanel;
class RemoteControlServer;
class RobotControlPanel;
namespace crawling { class ClampMotorController; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void connectDevice();
    void acceptDecodedFrame(DecodedFrame frame, int deviceId);
    void acceptCScanBatch(DecodedFrameBatch frames, int deviceId);
    void updateStatus();
    void updateParametersForPlot();
    void saveConfig();
    void loadConfig();
    void openRecording();
    void toggleRecording();
    void openRemoteDesktop();
    void openRobotControl();
    void processPlaybackFrame(const QByteArray &packet, int pointCount, int beamCount);

private:
    void buildUi();
    void buildMenus();
    void showMessage(const QString &text, bool error);
    void renderCurrentFrame(bool newlyAcquired);
    void updateDecoderSettings();
    void updateImagingGates();
    void showPanelSection(int index);

signals:
    void decoderSettingsChanged(int groupOffsetWords, int beamCount,
                                int pointCount, int scanModes,
                                int selectedBeam, int amplitudeScale,
                                int encoderPrecision);
    void decoderRecordingChanged(bool enabled);

private:
    DeviceController *m_device = nullptr;
    FrameRecorder *m_recorder = nullptr;
    FrameDecoderWorker *m_decoder = nullptr;
    QThread *m_decoderThread = nullptr;
    FramePlayer *m_player = nullptr;
    RemoteControlServer *m_remoteControl = nullptr;
    QProcess *m_remoteDesktopProcess = nullptr;
    RobotControlPanel *m_robotPanel = nullptr;
    crawling::ClampMotorController *m_clampController = nullptr;
    QTabWidget *m_workspace = nullptr;
    ParameterPanel *m_parameters = nullptr;
    AScanWidget *m_aScan = nullptr;
    EScanWidget *m_eScan = nullptr;
    BScanWidget *m_bScan = nullptr;
    CScanWidget *m_cScan = nullptr;
    QTabWidget *m_mapTabs = nullptr;
    QLineEdit *m_address = nullptr;
    QSpinBox *m_deviceId = nullptr;
    QSpinBox *m_beam = nullptr;
    QSpinBox *m_cPrecision = nullptr;
    QComboBox *m_eImagingGate = nullptr;
    QComboBox *m_bImagingGate = nullptr;
    QComboBox *m_cImagingGate = nullptr;
    QLabel *m_serviceStatus = nullptr;
    QLabel *m_hardwareStatus = nullptr;
    QLabel *m_groupStatus = nullptr;
    QLabel *m_encoderStatus = nullptr;
    QLabel *m_statusCaptureDetail = nullptr;
    QLabel *m_tailInfo = nullptr;
    QLabel *m_measurements = nullptr;
    QVector<QLabel *> m_vehicleMetrics;
    QPushButton *m_connect = nullptr;
    QPushButton *m_record = nullptr;
    QPushButton *m_eScanToggle = nullptr;
    QPushButton *m_bScanToggle = nullptr;
    QPushButton *m_cScanToggle = nullptr;
    QToolBar *m_actionRail = nullptr;
    QDockWidget *m_panelDock = nullptr;
    QStackedWidget *m_panelPages = nullptr;
    QLabel *m_panelTitle = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    bool m_panelPositioned = false;
    DecodedFrame m_lastFrame;
    QElapsedTimer m_auxUiClock;
    double m_rangeStart = 0;
    double m_rangeEnd = 50;
};
