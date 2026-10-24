#pragma once

#include <QFile>
#include <QObject>
#include <QTimer>

class FramePlayer : public QObject
{
    Q_OBJECT
public:
    explicit FramePlayer(QObject *parent = nullptr);
    bool open(const QString &path, QString *error = nullptr);
    int frameCount() const { return m_offsets.size(); }
    int pointCount() const { return m_pointCount; }
    int beamCount() const { return m_beamCount; }

public slots:
    void play();
    void pause();
    void rewind();

signals:
    void frameReady(const QByteArray &packet, int pointCount, int beamCount);
    void positionChanged(int current, int total);
    void finished();

private slots:
    void next();

private:
    QFile m_file;
    QVector<qint64> m_offsets;
    QTimer m_timer;
    int m_index = 0;
    int m_pointCount = 0;
    int m_beamCount = 0;
};

