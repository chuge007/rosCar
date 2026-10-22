#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <cstdint>
#include <memory>

namespace crawling {

// Native Windows candleLight/gs_usb transport, separate from serial SLCAN.
class CandleCanAdapter final : public QObject {
  Q_OBJECT
 public:
  struct Port {
    QString name; // candle0, candle1, ... (display only)
    QString id;   // candle:<Windows device path>, retained across refresh/reorder
  };

  explicit CandleCanAdapter(QObject* parent = nullptr);
  ~CandleCanAdapter() override;
  static QVector<Port> availablePorts();
  static bool isCandlePort(const QString& port);
  bool open(const QString& port, int bitrate, QString* error);
  bool isOpen() const;
  bool sendFrame(std::uint32_t id, const QByteArray& data, bool extended,
                 QString* error);

 signals:
  void frameReceived(std::uint32_t id, const QByteArray& data, bool extended);
  void faulted(const QString& message);

 private:
  struct Private;
  std::unique_ptr<Private> d_;
};

} // namespace crawling
