#include "framerecorder.h"

#include <QDataStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

FrameRecorder::FrameRecorder(QObject *parent) : QObject(parent) {}

bool FrameRecorder::start(const QString &fileName, int pointCount, int beamCount, int groupId)
{
    stop();
    m_file.setFileName(fileName);
    if (!m_file.open(QIODevice::WriteOnly)) {
        emit error(QStringLiteral("无法创建数据文件：%1").arg(m_file.errorString()));
        return false;
    }
    const QJsonObject meta{{"format", "PA1664_RAW_V1"},
                           {"created", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
                           {"pointCount", pointCount}, {"beamCount", beamCount}, {"groupId", groupId}};
    const QByteArray json = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    QDataStream stream(&m_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("PA16RAW1", 8);
    stream << quint32(json.size());
    stream.writeRawData(json.constData(), json.size());
    m_frames = 0;
    emit stateChanged(true, fileName);
    return true;
}

void FrameRecorder::stop()
{
    if (!m_file.isOpen()) return;
    m_file.flush();
    const QString path = m_file.fileName();
    m_file.close();
    emit stateChanged(false, path);
}

void FrameRecorder::append(const QByteArray &packet, int deviceId)
{
    if (!m_file.isOpen()) return;
    QDataStream stream(&m_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint64(QDateTime::currentMSecsSinceEpoch()) << qint32(deviceId) << quint32(packet.size());
    if (stream.writeRawData(packet.constData(), packet.size()) != packet.size()) {
        emit error(QStringLiteral("写入原始帧失败：%1").arg(m_file.errorString()));
        stop();
        return;
    }
    ++m_frames;
    if ((m_frames % 100) == 0) m_file.flush();
}

