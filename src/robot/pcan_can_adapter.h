#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <cstdint>
#include <memory>

namespace crawling {

// PCAN-Basic classic CAN transport. Also supports FD-capable adapters in CAN mode.
class PcanCanAdapter final : public QObject {
  Q_OBJECT
 public:
  struct Port {
    QString name; // PCAN_USBBUS1, ...
    QString id;   // pcan:51, ... (PCAN-Basic channel handle in hexadecimal)
    QString description;
  };

  explicit PcanCanAdapter(QObject* parent = nullptr);
  ~PcanCanAdapter() override;
  static QVector<Port> availablePorts(QString* error = nullptr);
  static bool isPcanPort(const QString& port);
  static QString canonicalPort(const QString& port);
  static QString portName(const QString& port);
  bool open(const QString& port, int bitrate, QString* error);
  bool isOpen() const;
  bool sendFrame(std::uint32_t id, const QByteArray& data, bool extended,
                 QString* error);
 signals:
  void faulted(const QString& message);
  void frameReceived(std::uint32_t id, const QByteArray& data, bool extended);
 private:
  struct Private;
  std::unique_ptr<Private> d_;
};

} // namespace crawling
