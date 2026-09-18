#include "point_cloud_view.h"
#include "app_logger.h"
#include "utf8_compat.h"

#include <QDateTime>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace crawling {
PointCloudView::PointCloudView(QWidget* parent) : QWidget(parent) {
  setMinimumSize(420, 220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAttribute(Qt::WA_OpaquePaintEvent);
}

void PointCloudView::setPoints(const QVector<QVector3D>& points) {
  image_ = QImage();
  points_ = points;
  rebuildProjectionCache();
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastViewLogMs_ >= 500) {
    lastViewLogMs_ = now;
    AppLogger::write(QStringLiteral("PointCloudUI"),
                     QStringLiteral("setPoints input=%1 finiteProjected=%2 widget=%3x%4 plane=%5")
                         .arg(points_.size()).arg(projectedPoints_.size())
                         .arg(width()).arg(height()).arg(projectionPlane_));
  }
  update();
}

void PointCloudView::setImage(const QImage& image) {
  points_.clear();
  projectedPoints_.clear();
  image_ = image;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - lastViewLogMs_ >= 500) {
    lastViewLogMs_ = now;
    AppLogger::write(QStringLiteral("CameraUI"),
                     QStringLiteral("setImage source=%1x%2 widget=%3x%4 format=%5")
                         .arg(image_.width()).arg(image_.height())
                         .arg(width()).arg(height()).arg(static_cast<int>(image_.format())));
  }
  update();
}

void PointCloudView::setProjectionPlane(int plane) {
  const int nextPlane = std::clamp(plane, 0, 2);
  if (projectionPlane_ == nextPlane) return;
  projectionPlane_ = nextPlane;
  rebuildProjectionCache();
  update();
}

void PointCloudView::rebuildProjectionCache() {
  projectedPoints_.clear();
  if (points_.isEmpty()) return;

  const auto coordinate = [this](const QVector3D& point, bool horizontal) {
    if (projectionPlane_ == 0) return horizontal ? double(point.x()) : double(point.y());
    if (projectionPlane_ == 2) return horizontal ? double(point.y()) : double(point.z());
    return horizontal ? double(point.x()) : double(point.z());
  };

  bool haveFinitePoint = false;
  for (const QVector3D& point : points_) {
    const double horizontal = coordinate(point, true);
    const double vertical = coordinate(point, false);
    if (!std::isfinite(horizontal) || !std::isfinite(vertical)) continue;
    if (!haveFinitePoint) {
      minHorizontal_ = maxHorizontal_ = horizontal;
      minVertical_ = maxVertical_ = vertical;
      haveFinitePoint = true;
    } else {
      minHorizontal_ = std::min(minHorizontal_, horizontal);
      maxHorizontal_ = std::max(maxHorizontal_, horizontal);
      minVertical_ = std::min(minVertical_, vertical);
      maxVertical_ = std::max(maxVertical_, vertical);
    }
  }
  if (!haveFinitePoint) return;

  const double horizontalRange = std::max(0.001, maxHorizontal_ - minHorizontal_);
  const double verticalRange = std::max(0.001, maxVertical_ - minVertical_);
  constexpr double kPadding = 0.05;
  minHorizontal_ -= horizontalRange * kPadding;
  maxHorizontal_ += horizontalRange * kPadding;
  minVertical_ -= verticalRange * kPadding;
  maxVertical_ += verticalRange * kPadding;

  projectedPoints_.reserve(points_.size());
  const double horizontalScale = 1.0 / (maxHorizontal_ - minHorizontal_);
  const double verticalScale = 1.0 / (maxVertical_ - minVertical_);
  for (const QVector3D& point : points_) {
    const double horizontal = coordinate(point, true);
    const double vertical = coordinate(point, false);
    if (!std::isfinite(horizontal) || !std::isfinite(vertical)) continue;
    const int x = std::clamp(static_cast<int>((horizontal - minHorizontal_) * horizontalScale * 65535.0), 0, 65535);
    const int y = std::clamp(static_cast<int>((vertical - minVertical_) * verticalScale * 65535.0), 0, 65535);
    projectedPoints_.append(QPoint(x, y));
  }
}

void PointCloudView::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#101820"));
  const QRect area = rect().adjusted(38, 12, -12, -28);
  if (!image_.isNull()) {
    // The preview canvas is intentionally wider than the camera frame. Fill
    // the complete canvas so the live image remains easy to inspect instead
    // of shrinking into a small centered rectangle. This preserves every
    // source pixel and avoids cropping; only the display aspect is adapted
    // to the actual widget.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(area, image_);
    painter.setPen(QColor("#9fb3bf"));
    painter.drawText(42, height() - 8,
                     CRAWLING_TEXT("原始图 %1 x %2，已铺满画布")
                         .arg(image_.width()).arg(image_.height()));
    return;
  }
  painter.setPen(QColor("#344651"));
  for (int i = 0; i <= 10; ++i) {
    const int x = area.left() + area.width() * i / 10;
    const int y = area.top() + area.height() * i / 10;
    painter.drawLine(x, area.top(), x, area.bottom());
    painter.drawLine(area.left(), y, area.right(), y);
  }
  const QString horizontalAxis = projectionPlane_ == 2 ? QStringLiteral("Y") : QStringLiteral("X");
  const QString verticalAxis = projectionPlane_ == 0 ? QStringLiteral("Y") : QStringLiteral("Z");
  painter.setPen(QColor("#9fb3bf"));
  painter.drawText(8, 24, horizontalAxis);
  painter.drawText(area.right() - 12, height() - 8, verticalAxis);
  if (points_.isEmpty() || projectedPoints_.isEmpty()) {
    painter.drawText(area, Qt::AlignCenter,
                     CRAWLING_TEXT("\xE7\xAD\x89""\xE5\xBE\x85""\xE6\xBF\x80""\xE5\x85\x89""\xE7\x82\xB9""\xE4\xBA\x91""\xE6\x95\xB0""\xE6\x8D\xAE"""));
    return;
  }

  QVector<QPoint> screenPoints;
  screenPoints.reserve(projectedPoints_.size());
  // Use one uniform scale for both axes and center the complete data bounds.
  // Independent X/Y scales make a point cloud look distorted; anchoring at
  // the top-left can also clip one side when the widget aspect ratio differs
  // from the source image.
  const double scale = std::min(area.width(), area.height()) / 65535.0;
  const int renderedWidth = static_cast<int>(65535.0 * scale);
  const int renderedHeight = static_cast<int>(65535.0 * scale);
  const int offsetX = area.left() + (area.width() - renderedWidth) / 2;
  const int offsetY = area.top() + (area.height() - renderedHeight) / 2;
  for (const QPoint& normalized : projectedPoints_) {
    screenPoints.append(QPoint(offsetX + static_cast<int>(normalized.x() * scale),
                               offsetY + renderedHeight -
                                   static_cast<int>(normalized.y() * scale)));
  }
  painter.setPen(QPen(QColor("#59d8ff"), 2));
  painter.drawPoints(screenPoints.constData(), screenPoints.size());
  painter.setPen(QColor("#9fb3bf"));
  painter.drawText(42, height() - 8,
                   CRAWLING_TEXT("%1 \xE7\x82\xB9""\xEF\xBC\x8C""%2: %3..%4 m\xEF\xBC\x8C""%5: %6..%7 m")
                       .arg(projectedPoints_.size())
                       .arg(horizontalAxis)
                       .arg(minHorizontal_, 0, 'f', 2)
                       .arg(maxHorizontal_, 0, 'f', 2)
                       .arg(verticalAxis)
                       .arg(minVertical_, 0, 'f', 2)
                       .arg(maxVertical_, 0, 'f', 2));
}
}  // namespace crawling
