#pragma once

#include "framedecoderworker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "rawpacket.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QMutex>
#include <QQueue>

class AScanWidget;
class BScanWidget;
class CScanWidget;
class DeviceController;
class EScanWidget;
class FrameRecorder;
class FrameDecoderWorker;
class FramePlayer;
class ScanWaveformCache;
class QAction;
class QComboBox;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QStackedWidget;
class QThread;
class QTabWidget;
class QToolBar;
class ParameterPanel;
class RemoteControlServer;
class RobotControlPanel;
namespace crawling { class ClampMotorController; }
namespace pa1664::remote {
class RemoteUltrasoundSource;
class RemoteWorkbenchSession;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    explicit MainWindow(pa1664::remote::RemoteWorkbenchSession *remoteSession,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void connectDevice();
    void acceptDecodedFrame(DecodedFrame frame, int groupId, int deviceId);
    void acceptGroupScanBatch(GroupScanFrameBatch frames, int deviceId);
    void updateStatus();
    void updateParametersForPlot();
    void saveConfig();
    void loadConfig();
    void openRecording();
    void toggleRecording();
    void openRemoteDesktop();
    void openRobotControl();
    void processPlaybackFrame(const QByteArray &packet, int pointCount, int beamCount);
    void saveScanSession();
    void loadScanSession();
    void showScreenLayoutDialog();

private:
    enum ScreenLayoutMode {
        AutomaticScreenLayout = 0,
        SingleWindowLayout = 1,
        DualWindowLayout = 2
    };

    void applyScreenLayout();
    void buildUi();
    void buildMenus();
    void showMessage(const QString &text, bool error);
    void renderCurrentFrame(bool newlyAcquired);
    void updateDecoderSettings();
    void updateImagingGates();
    void showPanelSection(int index);
    int currentGroupId() const;
    int encoderCountsPerCell(int groupId) const;
    BScanWidget *ensureBScan(int groupId);
    CScanWidget *ensureCScan(int groupId);
    void selectCurrentGroupViews();
    void restoreCurrentGroupControls();
    void clearAllScanHistories();
    void updateEncoderCalibration();
    void setScanSessionActive(bool active);
    void setScanCachingPaused(bool paused);
    void startAcquisitionFromUi();
    void enqueueGroupScanBatch(const GroupScanFrameBatch &frames, int deviceId);
    void drainPendingGroupScanBatches();
    void applyRemoteStatus(const QJsonObject &status);
    void stopAcquisition();
    void refreshGeometry();

signals:
    void decoderSettingsChanged(QVector<GroupScanSettings> groups,
                                int displayGroupId);
    void decoderRecordingChanged(bool enabled);
    void bWaveformRequested(int groupId, int column, double depth);
    void cWaveformRequested(int groupId, int gate, int x, int y);
    void waveformCacheReset();
    void waveformCacheGroupReset(int groupId, int mode);
    void waveformCacheStatisticsRequested();
    void waveformCachingChanged(bool enabled);

private:
    pa1664::remote::RemoteWorkbenchSession *m_remoteSession = nullptr;
    pa1664::remote::RemoteUltrasoundSource *m_remoteUltrasound = nullptr;
    QJsonObject m_remoteStatus;
    QJsonDocument m_remoteHardwareConfig;
    bool m_remoteMode = false;
    bool m_parameterSnapshotReady = false;
    DeviceController *m_device = nullptr;
    FrameRecorder *m_recorder = nullptr;
    QThread *m_recorderThread = nullptr;
    FrameDecoderWorker *m_decoder = nullptr;
    QThread *m_decoderThread = nullptr;
    ScanWaveformCache *m_waveformCache = nullptr;
    QThread *m_waveformCacheThread = nullptr;
    FramePlayer *m_player = nullptr;
    RemoteControlServer *m_remoteControl = nullptr;
    QProcess *m_remoteDesktopProcess = nullptr;
    RobotControlPanel *m_robotPanel = nullptr;
    crawling::ClampMotorController *m_clampController = nullptr;
    QThread *m_clampThread = nullptr;
    QTabWidget *m_workspace = nullptr;
    QMainWindow *m_robotWindow = nullptr;
    ScreenLayoutMode m_screenLayoutMode = AutomaticScreenLayout;
    QString m_ultrasoundScreenName;
    ParameterPanel *m_parameters = nullptr;
    AScanWidget *m_aScan = nullptr;
    EScanWidget *m_eScan = nullptr;
    BScanWidget *m_bScan = nullptr;
    CScanWidget *m_cScan = nullptr;
    QStackedWidget *m_bScanStack = nullptr;
    QStackedWidget *m_cScanStack = nullptr;
    QHash<int, BScanWidget *> m_bScans;
    QHash<int, CScanWidget *> m_cScans;
    QTabWidget *m_mapTabs = nullptr;
    QLineEdit *m_address = nullptr;
    QSpinBox *m_deviceId = nullptr;
    QSpinBox *m_beam = nullptr;
    QDoubleSpinBox *m_cPrecision = nullptr;
    QComboBox *m_eImagingGate = nullptr;
    QComboBox *m_bImagingGate = nullptr;
    QComboBox *m_bViewMode = nullptr;
    QComboBox *m_bPaletteMode = nullptr;
    QComboBox *m_bScaleMode = nullptr;
    QDoubleSpinBox *m_bDisplayReject = nullptr;
    QDoubleSpinBox *m_bDisplayLower = nullptr;
    QDoubleSpinBox *m_bDisplayUpper = nullptr;
    QDoubleSpinBox *m_bGamma = nullptr;
    QCheckBox *m_bInterpolateMissing = nullptr;
    QComboBox *m_cImagingGate = nullptr;
    QLabel *m_serviceStatus = nullptr;
    QLabel *m_hardwareStatus = nullptr;
    QLabel *m_groupStatus = nullptr;
    QLabel *m_encoderStatus = nullptr;
    QLabel *m_statusCaptureDetail = nullptr;
    QLabel *m_tailInfo = nullptr;
    QLabel *m_measurements = nullptr;
    QLabel *m_cacheStatus = nullptr;
    QLabel *m_cachePathLabel = nullptr;
    QLabel *m_cacheSizeLabel = nullptr;
    QLabel *m_cacheContentLabel = nullptr;
    QLabel *m_cacheUpdatedLabel = nullptr;
    QLabel *m_recordingPathLabel = nullptr;
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
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QSlider *m_captureToggle = nullptr;
    bool m_panelPositioned = false;
    DecodedFrame m_lastFrame;
    QHash<int, DecodedFrame> m_groupLastFrames;
    QHash<int, int> m_groupScanModes;
    QHash<int, int> m_groupSelectedBeam;
    QHash<int, double> m_groupEncoderPrecision;
    QHash<int, int> m_groupEImagingGate;
    QHash<int, int> m_groupBImagingGate;
    QHash<int, int> m_groupCImagingGate;
    QElapsedTimer m_auxUiClock;
    double m_rangeStart = 0;
    double m_rangeEnd = 50;
    double m_encoderMmPerCount = 3.14159265358979323846 * 500.0 / 4096.0;
    bool m_restoringGroupControls = false;
    bool m_reviewingScanWaveform = false;
    bool m_decoderSdkReady = false;
    bool m_parameterPresetsLoaded = false;
    bool m_scanSessionActive = false;
    bool m_scanCachingPaused = false;
    bool m_waitingForTrackingStart = false;
    bool m_captureWasActiveBeforeApply = false;
    QMutex m_pendingScanMutex;
    QHash<GroupScanFrameKey, GroupScanFrame> m_pendingScanFrames;
    QHash<GroupScanFrameKey, int> m_pendingScanScores;
    QQueue<GroupScanFrameKey> m_pendingScanOrder;
    qint64 m_pendingScanBytes = 0;
    qint64 m_pendingScanPeakBytes = 0;
    quint64 m_pendingScanDropped = 0;
    int m_pendingScanDeviceId = -1;
    QHash<GroupScanFrameKey, GroupScanFrame> m_drainScanFrames;
    QHash<GroupScanFrameKey, int> m_drainScanScores;
    QQueue<GroupScanFrameKey> m_drainScanOrder;
    qint64 m_drainScanBytes = 0;
    int m_drainScanDeviceId = -1;
    QTimer *m_scanUiTimer = nullptr;
    QElapsedTimer m_scanUiTimerClock;
    qint64 m_lastScanUiGapMs = 0;
    qint64 m_lastDrainDurationUs = 0;
    qint64 m_lastAcceptDurationUs = 0;
    QElapsedTimer m_scanUiTelemetryClock;
    int m_activePresetId = 0;
    QString m_scanCachePath;
};
