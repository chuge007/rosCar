#pragma once

#include <QWidget>
#include <QVariant>
#include <functional>

class QComboBox;
class QFormLayout;
class QTabWidget;
class QTimer;

class ParameterPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ParameterPanel(QWidget *parent = nullptr);
    QVector<QVector<double>> currentGates() const;

public slots:
    void reload();
    void apply();
    void refreshGroups();

signals:
    void configurationChanged();
    void groupChanged();
    void message(const QString &text, bool error);
    void applyStarted();
    void applyFinished();

private:
    struct Field {
        QString name;
        QWidget *editor = nullptr;
        std::function<QVariant()> getter;
        std::function<bool(const QVariant &)> setter;
        QVariant loadedValue;
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
    int selectedGroupId() const;
    void scheduleAutoApply();

    QVector<Field> m_fields;
    QComboBox *m_group = nullptr;
    QVector<QWidget *> m_gateEditors;
    QTimer *m_autoApplyTimer = nullptr;
    bool m_applying = false;
};
