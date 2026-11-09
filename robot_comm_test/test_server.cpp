/**
 * PA1664 Local Test Server
 * Simulates ControlServer's HelloAck response for protocol testing
 */

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTextStream>
#include <QTimer>

namespace PA1664 {
    constexpr quint32 kWireMagic = 0x50413136u;
    constexpr quint16 kProtocolVersion = 1;
    constexpr int kControlHeaderBytes = 50;
    
    enum class MessageType : quint16 { Hello = 1, HelloAck = 2 };
    
    struct Frame {
        MessageType type;
        quint16 flags = 0;
        quint64 sessionId = 0;
        quint64 sequence = 0;
        quint64 requestId = 0;
        quint64 timestampUs = 0;
        QByteArray payload;
    };
    
    quint32 crc32(const char* data, qsizetype size) {
        constexpr quint32 poly = 0xEDB88320u;
        static QVector<quint32> table;
        if (table.isEmpty()) {
            table.resize(256);
            for (int i = 0; i < 256; i++) {
                quint32 c = i;
                for (int j = 0; j < 8; j++)
                    c = (c & 1) ? (c >> 1) ^ poly : c >> 1;
                table[i] = c;
            }
        }
        quint32 crc = 0xFFFFFFFF;
        for (qsizetype i = 0; i < size; i++)
            crc = table[(crc ^ static_cast<quint8>(data[i])) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }
    
    QByteArray encode(const Frame& f) {
        QByteArray buf;
        buf.reserve(kControlHeaderBytes + f.payload.size());
        QDataStream s(&buf, QIODevice::WriteOnly);
        s.setByteOrder(QDataStream::LittleEndian);
        s << kWireMagic << kProtocolVersion;
        s << static_cast<quint16>(f.type) << f.flags;
        s << f.sessionId << f.sequence << f.requestId << f.timestampUs;
        s << static_cast<quint32>(f.payload.size());
        s << crc32(f.payload.constData(), f.payload.size());
        s.writeRawData(f.payload.constData(), f.payload.size());
        return buf;
    }
    
    bool decode(const QByteArray& data, Frame* f, int* consumed) {
        if (data.size() < kControlHeaderBytes) return false;
        QDataStream s(data);
        s.setByteOrder(QDataStream::LittleEndian);
        quint32 magic; quint16 ver, type;
        quint32 payloadSize, crc;
        s >> magic >> ver >> type >> f->flags;
        s >> f->sessionId >> f->sequence >> f->requestId >> f->timestampUs;
        s >> payloadSize >> crc;
        if (magic != kWireMagic || ver != kProtocolVersion) return false;
        if (data.size() < kControlHeaderBytes + static_cast<int>(payloadSize)) return false;
        f->type = static_cast<MessageType>(type);
        f->payload = data.mid(kControlHeaderBytes, payloadSize);
        if (payloadSize > 0 && crc32(f->payload.constData(), f->payload.size()) != crc)
            return false;
        if (consumed) *consumed = kControlHeaderBytes + payloadSize;
        return true;
    }
}

class TestServer : public QObject {
    Q_OBJECT
public:
    TestServer(quint16 port, QObject* parent = nullptr) : QObject(parent) {
        server_ = new QTcpServer(this);
        connect(server_, &QTcpServer::newConnection, this, &TestServer::onNewConnection);
        if (server_->listen(QHostAddress::LocalHost, port)) {
            log(QString("✓ Test server listening on 127.0.0.1:%1").arg(port));
        } else {
            log(QString("✗ Failed to start server: %1").arg(server_->errorString()));
            qApp->quit();
        }
    }
    
private slots:
    void onNewConnection() {
        QTcpSocket* client = server_->nextPendingConnection();
        QString peer = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
        log(QString("→ New connection from %1").arg(peer));
        
        connect(client, &QTcpSocket::readyRead, this, [this, client, peer]() {
            QByteArray data = client->readAll();
            log(QString("← Received %1 bytes from %2").arg(data.size()).arg(peer));
            
            PA1664::Frame hello;
            int consumed = 0;
            
            if (PA1664::decode(data, &hello, &consumed)) {
                if (hello.type == PA1664::MessageType::Hello) {
                    log(QString("  Decoded Hello: seq=%1").arg(hello.sequence));
                    if (!hello.payload.isEmpty()) {
                        QJsonDocument doc = QJsonDocument::fromJson(hello.payload);
                        log(QString("  Payload: %1").arg(QString::fromUtf8(doc.toJson(QJsonDocument::Compact))));
                    }
                    
                    // Send HelloAck
                    QJsonObject ack;
                    ack["type"] = "hello_ack";
                    ack["sessionId"] = 12345;
                    ack["serverVersion"] = "test-1.0";
                    ack["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                    
                    PA1664::Frame response;
                    response.type = PA1664::MessageType::HelloAck;
                    response.sessionId = 12345;
                    response.sequence = hello.sequence;
                    response.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
                    response.payload = QJsonDocument(ack).toJson(QJsonDocument::Compact);
                    
                    QByteArray wire = PA1664::encode(response);
                    client->write(wire);
                    client->flush();
                    
                    log(QString("→ Sent HelloAck: %1 bytes, session=%2")
                        .arg(wire.size()).arg(response.sessionId));
                    
                    QTimer::singleShot(100, client, &QTcpSocket::disconnectFromHost);
                } else {
                    log(QString("  Unexpected message type: %1").arg(static_cast<int>(hello.type)));
                }
            } else {
                log("  Failed to decode frame");
            }
        });
        
        connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
    }
    
private:
    void log(const QString& msg) {
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        QTextStream(stdout) << QString("[%1] %2\n").arg(ts).arg(msg);
    }
    
    QTcpServer* server_;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    QTextStream(stdout) << "\n========================================\n";
    QTextStream(stdout) << "   PA1664 Protocol Test Server\n";
    QTextStream(stdout) << "========================================\n\n";
    
    TestServer server(43100);
    return app.exec();
}

#include "test_server.moc"
