#pragma once

#include <QToolBar>
#include <QWidget>

class QAction;
class QLabel;
class QProgressBar;

class ProductGlobalBar final : public QToolBar
{
    Q_OBJECT
public:
    explicit ProductGlobalBar(QWidget *parent = nullptr);

    void setDeviceState(const QString &address, bool serviceConnected,
                        bool hardwareOnline);
    void setEncoderKnown(bool known);
    void setAlarmCount(int count);

signals:
    void projectRequested();
    void taskRequested();
    void dataRequested();
    void reportRequested();
    void settingsRequested();
    void alarmRequested();

private:
    QLabel *m_server = nullptr;
    QLabel *m_ip = nullptr;
    QLabel *m_probe = nullptr;
    QLabel *m_encoder = nullptr;
    QLabel *m_board = nullptr;
    QAction *m_alarm = nullptr;
};

class ProductActionRail final : public QToolBar
{
    Q_OBJECT
public:
    explicit ProductActionRail(QWidget *parent = nullptr);
    void setCurrentSection(int index);

signals:
    void sectionRequested(int index);
    void startRequested();
    void stopRequested();
    void closeRequested();
};

class ProductScanStatus final : public QWidget
{
    Q_OBJECT
public:
    explicit ProductScanStatus(QWidget *parent = nullptr);

    void setStateText(const QString &text, bool error = false);
    void setProgress(int value);
    void setPosition(double millimeters);
    void setSystemHealthy(bool healthy);

private:
    QLabel *m_state = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressText = nullptr;
    QLabel *m_position = nullptr;
    QLabel *m_health = nullptr;
};
