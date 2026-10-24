#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>

class Client;

namespace pa1664::remote {

// Reflection boundary for the subset of the vendor SDK used by the operator
// UI.  It deliberately lives in the host process: the operator never links or
// invokes the ultrasound SDK directly in remote mode.
class UltrasoundParameterRpc final {
 public:
  static bool read(Client& client, const QString& method,
                   const QJsonArray& args, QJsonValue* value,
                   QString* error = nullptr);
  static bool write(Client& client, const QString& method,
                    const QJsonArray& args, QString* error = nullptr);
  static QStringList getterNames();
  static QJsonArray defaultArguments(const QString& method);
};

}  // namespace pa1664::remote
