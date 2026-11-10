#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <QVariant>
#include <QStringList>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QPushButton;
class QSpinBox;
class QFormLayout;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QTreeWidget;

class ParameterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ParameterPanel(QWidget *parent = nullptr);
    QVector<QVector<double>> currentGates() const;
    int activeGate() const;
    bool surfaceTrackingEnabled() const;
    double surfaceWindowStart() const;
    double surfaceWindowEnd() const;
    double surfaceThreshold() const;
    bool surfaceHoldMissing() const;
    QVector<bool> surfaceTrackedGates() const;
    bool persistConfiguration(QString *errorMessage = nullptr) const;
    int currentPresetId() const { return m_currentPresetId; }
    double wheelDiameter() const;
    int encoderResolution() const;
    int encoderCalibrationMode() const;
    double measuredEncoderCountsPerMmA() const;
    double measuredEncoderCountsPerMmB() const;

public slots:
    bool activatePreset(int presetId) { return switchPreset(presetId); }
    void reload();
    void apply();
    void refreshGroups();
    void setGateRangeFromPlot(int gateIndex, double start, double end);
    void setGateThresholdFromPlot(int gateIndex, double threshold);
    void setSurfaceWindowFromPlot(double start, double end);
    void selectGate(int gateIndex);
    void updateSurfaceStatus(bool valid, double reference, double current);
    void setLiveEncoderCounts(qint64 encoderA, qint64 encoderB);

signals:
    void configurationChanged();
    void groupChanged();
    void message(const QString &text, bool error);
    void applyStarted();
    void applyFinished(bool ok, const QString &message);
    void presetSwitchFinished(bool ok, const QString &message);
    void scanPrecisionChanged(int presetId, double mmPerCell);
    void gatePlacementRequested(int gateIndex);
    void surfaceWindowPlacementRequested();
    void gateVisualizationChanged();
    void surfaceTrackingChanged();
    void encoderCalibrationChanged();
    void encoderDebugActiveChanged(bool active);

private:
    struct Field {
        QString name;
        QWidget *editor = nullptr;
        std::function<QVariant()> getter;
        std::function<bool(const QVariant &)> setter;
        QVariant loadedValue;
    };

    struct GateWidgets {
        QString name;
        QCheckBox *enabled = nullptr;
        QComboBox *sync = nullptr;
        QDoubleSpinBox *start = nullptr;
        QDoubleSpinBox *end = nullptr;
        QDoubleSpinBox *threshold = nullptr;
        QComboBox *measure = nullptr;
        QWidget *page = nullptr;
    };

    QWidget *pageFor(QFormLayout *form);
    void addDouble(QFormLayout *form, const QString &name, double min, double max, int decimals,
                   const std::function<double()> &getter,
                   const std::function<bool(double)> &setter, const QString &suffix = {});
    void addInt(QFormLayout *form, const QString &name, int min, int max,
                const std::function<int()> &getter,
                const std::function<bool(int)> &setter, const QString &suffix = {});
    void addBool(QFormLayout *form, const QString &name,
                 const std::function<bool()> &getter,
                 const std::function<bool(bool)> &setter);
    void addEnum(QFormLayout *form, const QString &name,
                 const QList<QPair<QString, int>> &items,
                 const std::function<int()> &getter,
                 const std::function<bool(int)> &setter);
    void addGateTab(QTabWidget *tabs);
    void addTcgTab(QTabWidget *tabs);
    void refreshGateTree();
    int selectedGroupId() const;
    void scheduleAutoApply();
    void updateLongitudinalAngleFromWedgeInput();
    void updateWedgeAngleFromLongitudinalInput(double longitudinalAngle);
    void updateEncoderCalibrationSummary();
    void resetEncoderDebugSession();
    bool hasFreshEncoderCounts() const;
    void recomputeTofdPcs();
    bool switchPreset(int presetId);
    bool remoteMode() const;
    void finishRemotePresetOperation(const QString &operation, bool ok,
                                     int presetId, const QString &message);

    QVector<Field> m_fields;
    QComboBox *m_group = nullptr;
    QVector<GateWidgets> m_gateWidgets;
    QTreeWidget *m_gateTree = nullptr;
    QStackedWidget *m_gateEditorStack = nullptr;
    QCheckBox *m_surfaceEnabled = nullptr;
    QDoubleSpinBox *m_surfaceStart = nullptr;
    QDoubleSpinBox *m_surfaceLength = nullptr;
    QDoubleSpinBox *m_surfaceThreshold = nullptr;
    QCheckBox *m_surfaceHold = nullptr;
    QVector<QCheckBox *> m_surfaceGateChecks;
    QLabel *m_surfaceStatus = nullptr;
    QDoubleSpinBox *m_wheelDiameter = nullptr;
    QSpinBox *m_encoderResolution = nullptr;
    QComboBox *m_encoderCalibrationMode = nullptr;
    QLabel *m_encoderTheorySummary = nullptr;
    QLabel *m_encoderActiveSummary = nullptr;
    QPushButton *m_encoderDebugToggle = nullptr;
    QGroupBox *m_encoderDebugGroup = nullptr;
    QLabel *m_encoderLiveCounts = nullptr;
    QComboBox *m_encoderDebugAxis = nullptr;
    QDoubleSpinBox *m_encoderDebugDistance = nullptr;
    QLabel *m_encoderDebugStartLabel = nullptr;
    QLabel *m_encoderDebugResult = nullptr;
    QPushButton *m_encoderRecordStart = nullptr;
    QPushButton *m_encoderRecordEnd = nullptr;
    QPushButton *m_encoderApplyMeasured = nullptr;
    qint64 m_encoderDebugStartCount = 0;
    bool m_hasEncoderDebugStart = false;
    double m_pendingCountsPerMmA = 0.0;
    double m_pendingCountsPerMmB = 0.0;
    double m_measuredCountsPerMmA = 0.0;
    double m_measuredCountsPerMmB = 0.0;
    qint64 m_liveEncoderA = 0;
    qint64 m_liveEncoderB = 0;
    bool m_hasLiveEncoderCounts = false;
    QElapsedTimer m_encoderLiveClock;
    QDoubleSpinBox *m_workpieceVelocityInput = nullptr;
    QDoubleSpinBox *m_wedgeAngleInput = nullptr;
    QDoubleSpinBox *m_wedgeVelocityInput = nullptr;
    QDoubleSpinBox *m_longitudinalAngleInput = nullptr;
    QLabel *m_wedgeAngleStatus = nullptr;
    QDoubleSpinBox *m_tofdDepthFraction = nullptr;
    QLabel *m_tofdAngleLabel = nullptr;
    QLabel *m_tofdDepthLabel = nullptr;
    QLabel *m_tofdPcsLabel = nullptr;
    bool m_syncingGateUi = false;
    int m_currentPresetId = 0;
    bool m_switchingPreset = false;
    QTimer *m_autoApplyTimer = nullptr;
    bool m_applying = false;
    // 下发进行中收到的参数修改不能丢：标记后在本轮结束立即补发一次。
    bool m_applyDeferred = false;
    int m_remoteRemoveAfterSwitch = -1;
    int m_remoteApplyLocalFailures = 0;
    QStringList m_remoteApplyLocalErrors;
    bool m_updatingEncoderSnapshot = false;
};
