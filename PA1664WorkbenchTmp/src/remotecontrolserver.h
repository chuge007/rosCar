#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>

class DeviceController;
class ParameterPanel;
class QLocalServer;
class QLocalSocket;
class QWidget;

// Local, same-user IPC endpoint used by the LanRemoteQt sidecar.  The
// network-facing process never receives a pointer to the vendor SDK and can
// only invoke the small whitelist implemented by this class.
class RemoteControlServer final : public QObject
{
    Q_OBJECT
public:
    RemoteControlServer(DeviceController *device, ParameterPanel *parameters,
                        QWidget *hostWindow, QObject *parent = nullptr);
    ~RemoteControlServer() override;

    bool start(QString *errorText = nullptr);
    void stop();
    bool isListening() const;
    static QString serverName();

private slots:
    void acceptPendingConnections();
    void publishStateIfChanged();

private:
    void readClient(QLocalSocket *socket);
    void removeClient(QLocalSocket *socket);
    void writeMessage(QLocalSocket *socket, const QJsonObject &message);
    void sendResponse(quint64 clientId, const QJsonObject &message);
    void broadcastEvent(const QJsonObject &message);
    void handleRequest(quint64 clientId, const QJsonObject &request);
    QJsonObject reply(const QJsonObject &request, bool success,
                      const QString &message = {}, const QString &errorCode = {}) const;
    QJsonObject stateObject() const;
    QJsonObject configurationObject() const;
    QJsonObject applyConfigurationPatch(const QJsonObject &request);
    QJsonObject executeCommand(const QJsonObject &request);

    DeviceController *m_device = nullptr;
    ParameterPanel *m_parameters = nullptr;
    QWidget *m_hostWindow = nullptr;
    QLocalServer *m_server = nullptr;
    QHash<QLocalSocket *, quint64> m_clientIds;
    QHash<QLocalSocket *, QByteArray> m_receiveBuffers;
    quint64 m_nextClientId = 1;
    QString m_lastStateSignature;
    QString m_address = QStringLiteral("127.0.0.1");
    int m_deviceId = 0;
};
