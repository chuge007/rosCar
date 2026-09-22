#pragma once

#include <QImage>
#include "laser_gap_detector.h"
#include <QPoint>
#include <QVector>
#include <QVector3D>
#include <QWidget>

class QPainter;

namespace crawling {
class PointCloudView final : public QWidget {
  Q_OBJECT
 public:
  explicit PointCloudView(QWidget* parent = nullptr);
 public slots:
  void setPoints(const QVector<QVector3D>& points);
  void setImage(const QImage& image);
  void setDetectionImage(const QImage& image, const LaserGapDetection& detection);
  void setProfileMode(bool active);
  void setProfileObservation(const QVector<QVector3D>& points,
                             const LaserGapDetection& detection,
                             quint32 sourceFrameNumber, qint64 receivedAtEpochMs);
  void setProjectionPlane(int plane);
 protected:
  void paintEvent(QPaintEvent* event) override;
 private:
  void rebuildProjectionCache();
  void paintProfile(QPainter& painter, const QRect& area);

  bool profileMode_ = true;
  QVector<QVector3D> scanPoints_;
  bool scanDisplayInitialized_ = false;
  bool displayRangeInitialized_ = false;
  double displayMinZ_ = 0.0;
  double displayMaxZ_ = 1.0;
  LaserGapDetection scanDetection_;
  quint32 scanFrameNumber_ = 0;
  qint64 scanReceivedAtMs_ = 0;
  bool profileUpdatePending_ = false;

  QVector<QVector3D> points_;
  QVector<QPoint> projectedPoints_;
  QImage image_;
  LaserGapDetection detection_;
  bool showDetection_ = false;
  double minHorizontal_ = 0.0;
  double maxHorizontal_ = 1.0;
  double minVertical_ = 0.0;
  double maxVertical_ = 1.0;
  int projectionPlane_ = 1;
};
}  // namespace crawling
