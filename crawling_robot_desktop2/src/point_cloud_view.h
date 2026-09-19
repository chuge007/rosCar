#pragma once

#include <QImage>
#include "laser_gap_detector.h"
#include <QPoint>
#include <QVector>
#include <QVector3D>
#include <QWidget>

namespace crawling {
class PointCloudView final : public QWidget {
  Q_OBJECT
 public:
  explicit PointCloudView(QWidget* parent = nullptr);
 public slots:
  void setPoints(const QVector<QVector3D>& points);
  void setImage(const QImage& image);
  void setDetectionImage(const QImage& image, const LaserGapDetection& detection);
  void setProjectionPlane(int plane);
 protected:
  void paintEvent(QPaintEvent* event) override;
 private:
  void rebuildProjectionCache();

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
