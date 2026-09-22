#pragma once

#include "laser_path_estimator.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

namespace crawling {

struct LaserTrajectorySegment {
  QVector<LaserEdgeSample> scanSamples;
  LaserPathFit fit;
  double segmentLengthM = 0.0;
  int sequenceNumber = 0;
  QString label;
};

struct LaserTrajectorySaveResult {
  bool success = false;
  QString segmentPath;
  QString error;
};

class LaserTrajectoryRenderer final {
 public:
  bool startSession(QString* error = nullptr);
  void reset();
  QString sessionDirectory() const;

  LaserTrajectorySaveResult appendSegment(
      const QVector<LaserEdgeSample>& scanSamples, const LaserPathFit& fit,
      double segmentLengthM, double lateralSpanM, const QString& label);

  static QImage render(const QVector<LaserTrajectorySegment>& segments,
                       double lateralSpanM);

 private:
  QString sessionDirectory_;
  int segmentCount_ = 0;
};

class LaserTrajectoryWriter final : public QObject {
  Q_OBJECT

 public:
  explicit LaserTrajectoryWriter(QObject* parent = nullptr);

 public slots:
  void beginSession(quint64 sessionId);
  void saveSegment(quint64 sessionId,
                   const QVector<crawling::LaserEdgeSample>& scanSamples,
                   const crawling::LaserPathFit& fit, double segmentLengthM,
                   double lateralSpanM, const QString& label);
  void saveRawFrame(quint64 sessionId, quint64 frameSequence,
                    const QImage& image, const QString& metadataJson);
  void shutdown();

 signals:
  void sessionStarted(quint64 sessionId, const QString& directory);
  void imageSaved(quint64 sessionId, const QString& path);
  void saveFailed(quint64 sessionId, const QString& error);
  void rawFrameSaved(quint64 sessionId, quint64 frameSequence,
                     const QString& path, const QString& error);

 private:
  quint64 currentSessionId_ = 0;
  int rawFrameCount_ = 0;
  qint64 rawFrameBytes_ = 0;
  LaserTrajectoryRenderer renderer_;
};

}  // namespace crawling
