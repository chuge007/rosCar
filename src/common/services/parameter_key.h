#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>

namespace pa1664::remote {

inline QString parameterCacheKey(const QString& method, const QJsonArray& args) {
  const QByteArray encoded =
      QJsonDocument(args).toJson(QJsonDocument::Compact).toBase64();
  return method + QLatin1Char(':') + QString::fromLatin1(encoded);
}

}  // namespace pa1664::remote
