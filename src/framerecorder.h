#pragma once

#include <QFile>
#include <QObject>

class FrameRecorder : public QObject
{
    Q_OBJECT
public:
    explicit FrameRecorder(QObject *parent = nullptr);
    bool isRecording() const { return m_file.isOpen(); }
    QString fileName() const { return m_file.fileName(); }

public slots:
    bool start(const QString &fileName, int pointCount, int beamCount, int groupId);
    void stop();
    void append(const QByteArray &packet, int deviceId);

signals:
    void stateChanged(bool recording, const QString &path);
    void error(const QString &text);

private:
    QFile m_file;
    quint64 m_frames = 0;
};

