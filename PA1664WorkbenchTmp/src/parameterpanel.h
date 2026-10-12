#pragma once

#include <QWidget>
#include <QVariant>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
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

signals:
    void configurationChanged();
    void groupChanged();
    void message(const QString &text, bool error);
    void applyStarted();
    void applyFinished();
    void presetSwitchFinished();
    void gatePlacementRequested(int gateIndex);
    void surfaceWindowPlacementRequested();
    void gateVisualizationChanged();
    void surfaceTrackingChanged();
    void encoderCalibrationChanged();

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
    bool switchPreset(int presetId);

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
    bool m_syncingGateUi = false;
    int m_currentPresetId = 0;
    bool m_switchingPreset = false;
    QTimer *m_autoApplyTimer = nullptr;
    bool m_applying = false;
};
