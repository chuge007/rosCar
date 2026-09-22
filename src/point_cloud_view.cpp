#include "point_cloud_view.h"
#include "utf8_compat.h"

#include <QPainter>
#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace crawling {
PointCloudView::PointCloudView(QWidget* parent) : QWidget(parent) {
  setMinimumSize(420, 220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAttribute(Qt::WA_OpaquePaintEvent);
  auto* freshnessTimer = new QTimer(this);
  connect(freshnessTimer, &QTimer::timeout, this, [this]() {
    if (!profileMode_) return;

    // Keep profile ingestion and detection at camera rate, but repaint the
    // human-facing preview at a calmer cadence.  A stale frame still gets one
    // repaint so the age/hidden-location state becomes visible.
    const qint64 age = scanReceivedAtMs_ > 0
        ? QDateTime::currentMSecsSinceEpoch() - scanReceivedAtMs_
        : 0;
    if (profileUpdatePending_ || (scanReceivedAtMs_ > 0 && age >= 180)) {
      profileUpdatePending_ = false;
      update();
    }
  });
  freshnessTimer->start(150);
}

void PointCloudView::setProfileMode(bool active) {
  // The main window now remains a profile view even after disconnection.
  profileMode_ = true;
  if (!active) {
    scanPoints_.clear();
    scanDisplayInitialized_ = false;
    displayRangeInitialized_ = false;
    scanDetection_ = LaserGapDetection{};
    scanReceivedAtMs_ = 0;
    profileUpdatePending_ = false;
  }
  update();
}

void PointCloudView::setProfileObservation(
    const QVector<QVector3D>& points, const LaserGapDetection& detection,
    quint32 sourceFrameNumber, qint64 receivedAtEpochMs) {
  profileMode_ = true;
  // Smooth only the human-facing trace. Detection has already consumed the
  // SDK points and keeps its original indices; the preview uses a temporal
  // blend so single-frame height jitter does not make the trace flicker.
  if (!scanDisplayInitialized_ || scanPoints_.size() != points.size()) {
    scanPoints_ = points;
    scanDisplayInitialized_ = true;
  } else {
    constexpr float kDisplayAlpha = 0.35f;
    for (int i = 0; i < points.size(); ++i) {
      const QVector3D& current = points[i];
      QVector3D& displayed = scanPoints_[i];
      if (!std::isfinite(current.x()) || !std::isfinite(current.z()) || current.z() <= 0) {
        displayed = current;
        continue;
      }
      if (!std::isfinite(displayed.x()) || !std::isfinite(displayed.z()) || displayed.z() <= 0) {
        displayed = current;
        continue;
      }
      displayed.setX(current.x());
      displayed.setY(current.y());
      displayed.setZ(displayed.z() + kDisplayAlpha * (current.z() - displayed.z()));
    }
  }
  scanDetection_ = detection;
  scanFrameNumber_ = sourceFrameNumber;
  scanReceivedAtMs_ = receivedAtEpochMs;
  profileUpdatePending_ = true;
}

void PointCloudView::paintProfile(QPainter& painter, const QRect& area) {
  const qint64 age = QDateTime::currentMSecsSinceEpoch() - scanReceivedAtMs_;
  const bool fresh = scanReceivedAtMs_ > 0 && age >= 0 && age <= 180;
  double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
  QVector<int> valid;
  for (int i = 0; i < scanPoints_.size(); ++i) {
    const auto& p = scanPoints_[i];
    if (!std::isfinite(p.x()) || !std::isfinite(p.z()) || p.z() <= 0 ||
        (!valid.isEmpty() && p.x() <= scanPoints_[valid.back()].x())) continue;
    if (valid.isEmpty()) {
      minX = maxX = p.x();
      minZ = maxZ = p.z();
    }
    minX = std::min(minX, double(p.x())); maxX = std::max(maxX, double(p.x()));
    minZ = std::min(minZ, double(p.z())); maxZ = std::max(maxZ, double(p.z()));
    valid.append(i);
  }
  painter.setPen(QColor("#9fb3bf"));
  if (valid.size() < 2 || maxX <= minX) {
    painter.drawText(area, Qt::AlignCenter, CRAWLING_TEXT("\xE7\xAD\x89\xE5\xBE\x85\xE7\x9B\xB8\xE6\x9C\xBA\x20\x53\x44\x4B\x20\xE5\x8E\x9F\xE5\xA7\x8B\xE8\xBD\xAE\xE5\xBB\x93\xE7\x82\xB9"));
    return;
  }
  const double measuredMinZ = minZ;
  const double measuredMaxZ = maxZ;
  if (!displayRangeInitialized_) {
    displayMinZ_ = measuredMinZ;
    displayMaxZ_ = measuredMaxZ;
    displayRangeInitialized_ = true;
  } else {
    constexpr double kRangeAlpha = 0.20;
    displayMinZ_ += kRangeAlpha * (measuredMinZ - displayMinZ_);
    displayMaxZ_ += kRangeAlpha * (measuredMaxZ - displayMaxZ_);
  }
  minZ = displayMinZ_;
  maxZ = displayMaxZ_;
  if (maxZ <= minZ) maxZ = minZ + 1e-6;
  const double zPadding = std::max(1e-6, (maxZ-minZ)*.08);
  minZ -= zPadding; maxZ += zPadding;
  const QRect plot = area.adjusted(0, 28, 0, -23);
  const auto position = [&](double x, double z) {
    return QPointF(plot.left() + (x-minX)/(maxX-minX)*plot.width(),
                   plot.bottom() - (z-minZ)/(maxZ-minZ)*plot.height());
  };
  painter.save();
  painter.setClipRect(plot.adjusted(-1, -1, 1, 1));
  painter.setPen(QPen(QColor("#344651"), 1));
  for (int k = 0; k <= 10; ++k) {
    const double x = plot.left() + plot.width()*k/10.0;
    const double y = plot.top() + plot.height()*k/10.0;
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
  }
  const bool located = fresh && scanDetection_.valid && scanDetection_.profileContour &&
      scanDetection_.gapStartPx >= 0 && scanDetection_.gapEndPx < scanPoints_.size() &&
      scanDetection_.gapEndPx > scanDetection_.gapStartPx;
  if (located) {
    const double left = position(scanPoints_[scanDetection_.gapStartPx].x(), minZ).x();
    const double right = position(scanPoints_[scanDetection_.gapEndPx].x(), minZ).x();
    if (std::isfinite(left) && std::isfinite(right))
      painter.fillRect(QRectF(left, plot.top(), right-left, plot.height()), QColor(83,238,130,25));
  }
  if (scanDetection_.profileNoise > 0) {
    painter.setPen(QPen(QColor("#f6cb54"), 1, Qt::DashLine));
    painter.drawLine(position(minX, scanDetection_.profileBaselineOffset + scanDetection_.profileBaselineSlope*minX),
                     position(maxX, scanDetection_.profileBaselineOffset + scanDetection_.profileBaselineSlope*maxX));
  }
  // Fit both axes to the entire plot. Screen scaling never changes detector input.
  painter.setPen(QPen(QColor(fresh ? "#59d8ff" : "#78848b"), 2));
  int previous = -2;
  for (int i : valid) {
    const QPointF p = position(scanPoints_[i].x(), scanPoints_[i].z());
    if (i == previous+1)
      painter.drawLine(position(scanPoints_[previous].x(), scanPoints_[previous].z()), p);
    painter.drawPoint(p);
    previous = i;
  }
  if (located) {
    painter.setPen(QPen(QColor("#53ee82"), 2));
    for (int i : {scanDetection_.gapStartPx, scanDetection_.gapEndPx}) {
      const double x = position(scanPoints_[i].x(), minZ).x();
      if (std::isfinite(x)) painter.drawLine(QPointF(x,plot.top()), QPointF(x,plot.bottom()));
    }
    // The detector center is in original scan-index space. Interpolate X
    // around that index, not the midpoint of two possibly nonlinear X edges.
    const double centerIndex = (scanDetection_.gapStartPx + scanDetection_.gapEndPx)*.5;
    int left = -1, right = -1;
    for (int i : valid) {
      if (i <= centerIndex) left = i;
      if (i >= centerIndex) { right = i; break; }
    }
    if (left >= 0 && right >= 0) {
      const double fraction = right > left ? (centerIndex-left)/(right-left) : 0;
      const double centerX = scanPoints_[left].x() + fraction*(scanPoints_[right].x()-scanPoints_[left].x());
      const double x = position(centerX,minZ).x();
      painter.setPen(QPen(QColor("#ff668a"), 2, Qt::DashLine));
      painter.drawLine(QPointF(x,plot.top()), QPointF(x,plot.bottom()));
    }
  }
  painter.restore();
  painter.setPen(QColor("#9fb3bf"));
  painter.drawText(area.adjusted(0, area.height()-20, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
      QStringLiteral("X: %1 .. %2    Z: %3 .. %4")
          .arg(minX,0,'f',2).arg(maxX,0,'f',2).arg(minZ,0,'f',2).arg(maxZ,0,'f',2));
  painter.setPen(QColor(fresh ? "#d9e5eb" : "#ffaa44"));
  const QString state = !fresh ? CRAWLING_TEXT("\xE6\x95\xB0\xE6\x8D\xAE\xE8\xBF\x87\xE6\x9C\x9F\xEF\xBC\x8C\xE5\xAE\x9A\xE4\xBD\x8D\xE7\xBA\xBF\xE5\xB7\xB2\xE9\x9A\x90\xE8\x97\x8F")
      : scanDetection_.valid ? CRAWLING_TEXT("\xE6\x9C\xAC\xE5\xB8\xA7\xE7\x84\x8A\xE9\x81\x93\xE5\x80\x99\xE9\x80\x89\xEF\xBC\x9A\xE7\xBB\xBF\xE7\xBA\xBF\x3D\xE8\xBE\xB9\xE7\x95\x8C\xEF\xBC\x8C\xE7\xBA\xA2\xE7\xBA\xBF\x3D\xE4\xB8\xAD\xE5\xBF\x83")
      : scanDetection_.continuityRejected ? CRAWLING_TEXT("\xE6\x9C\xAC\xE5\xB8\xA7\xE5\x80\x99\xE9\x80\x89\xE4\xBD\x8D\xE7\xBD\xAE\xE8\xB7\xB3\xE5\x8F\x98\xEF\xBC\x8C\xE6\x9C\xAA\xE7\xA1\xAE\xE8\xAE\xA4")
      : scanDetection_.widthRejected ? CRAWLING_TEXT("\xE6\x9C\xAC\xE5\xB8\xA7\xE5\x80\x99\xE9\x80\x89\xE5\xAE\xBD\xE5\xBA\xA6\xE8\xB7\xB3\xE5\x8F\x98\xEF\xBC\x8C\xE6\x9C\xAA\xE7\xA1\xAE\xE8\xAE\xA4")
      : CRAWLING_TEXT("\xE6\x9C\xAC\xE5\xB8\xA7\xE6\x9C\xAA\xE7\xA1\xAE\xE8\xAE\xA4\xE7\x84\x8A\xE9\x81\x93");
  painter.drawText(area.adjusted(0,0,0,-area.height()+23), Qt::AlignLeft | Qt::AlignVCenter, state);
  painter.drawText(42, height()-8,
      CRAWLING_TEXT("\x53\x44\x4B\x20\xE5\xB8\xA7\x20\x25\x31\x20\x7C\x20\xE6\x9C\x89\xE6\x95\x88\xE7\x82\xB9\x20\x25\x32\x2F\x25\x33\x20\x7C\x20\xE6\x95\xB0\xE6\x8D\xAE\xE5\xB9\xB4\xE9\xBE\x84\x20\x25\x34\x20\x6D\x73\x20\x7C\x20\x58\x2F\x5A\x20\xE7\x9B\xB8\xE6\x9C\xBA\xE5\x9D\x90\xE6\xA0\x87\xEF\xBC\x88\xE6\x98\xBE\xE7\xA4\xBA\xE6\xAF\x94\xE4\xBE\x8B\xE8\x87\xAA\xE9\x80\x82\xE5\xBA\x94\xEF\xBC\x89")
          .arg(scanFrameNumber_).arg(valid.size()).arg(scanPoints_.size()).arg(age));
}

void PointCloudView::setPoints(const QVector<QVector3D>& points) {
  profileMode_ = false;
  profileUpdatePending_ = false;
  showDetection_ = false;
  image_ = QImage();
  points_ = points;
  rebuildProjectionCache();
  update();
}

void PointCloudView::setImage(const QImage& image) {
  profileMode_ = false;
  profileUpdatePending_ = false;
  showDetection_ = false;
  points_.clear();
  projectedPoints_.clear();
  image_ = image;
  update();
}

void PointCloudView::setProjectionPlane(int plane) {
  const int nextPlane = std::clamp(plane, 0, 2);
  if (projectionPlane_ == nextPlane) return;
  projectionPlane_ = nextPlane;
  rebuildProjectionCache();
  update();
}

void PointCloudView::setDetectionImage(
    const QImage& image, const LaserGapDetection& detection) {
  setImage(image);
  detection_ = detection;
  showDetection_ = true;
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
  if (profileMode_) {
    paintProfile(painter, area);
    return;
  }
  if (!image_.isNull()) {
    // The preview canvas is intentionally wider than the camera frame. Fill
    // the complete canvas so the live image remains easy to inspect instead
    // of shrinking into a small centered rectangle. This preserves every
    // source pixel and avoids cropping; only the display aspect is adapted
    // to the actual widget.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(area, image_);
    if (showDetection_) {
      painter.save();
      painter.setClipRect(area);
      const auto screenPoint = [&](double along, double cross) {
        const double x = detection_.horizontal ? along : cross;
        const double y = detection_.horizontal ? cross : along;
        return QPointF(area.left() + x * area.width() / std::max(1, image_.width() - 1),
                       area.top() + y * area.height() / std::max(1, image_.height() - 1));
      };
      const double axisLast = detection_.horizontal ? image_.width() - 1 : image_.height() - 1;
      const double crossLast = detection_.horizontal ? image_.height() - 1 : image_.width() - 1;
      painter.setPen(QPen(QColor("#3de2ed"), 1, Qt::DashLine));
      painter.drawLine(screenPoint(axisLast * 0.5, 0), screenPoint(axisLast * 0.5, crossLast));
      if (detection_.baselineSupported) {
        painter.setPen(QPen(QColor("#f6cb54"), 1, Qt::DashLine));
        painter.drawLine(screenPoint(0, detection_.baselineOffsetPx),
                         screenPoint(axisLast, detection_.baselineOffsetPx + detection_.baselineSlope * axisLast));
      }
      if (detection_.valid) {
        // Keep the visual mode consistent with the detector's authority:
        // contour recovery is an auxiliary low-confidence observation, edge
        // inference is the next fallback, and only the remaining case is a
        // measured two-edge gap.  Contour must not be presented as a normal
        // two-edge result because that makes a degraded frame look trusted.
        const QColor gapColor(detection_.contourFallback
                                  ? "#c58cff"
                                  : (detection_.edgeBreakFallback
                                         ? "#ffaa44"
                                         : "#53ee82"));
        painter.setPen(QPen(gapColor, 2));
        for (int along : {detection_.gapStartPx, detection_.gapEndPx}) {
          painter.drawLine(screenPoint(along, 0), screenPoint(along, crossLast));
        }
        const double center = 0.5 * (detection_.gapStartPx + detection_.gapEndPx);
        painter.setPen(QPen(gapColor, 1, Qt::DashLine));
        painter.drawLine(screenPoint(center, 0), screenPoint(center, crossLast));
      }
      painter.setPen(detection_.valid
                         ? (detection_.contourFallback
                                ? QColor("#c58cff")
                                : (detection_.edgeBreakFallback
                                       ? QColor("#ffaa44")
                                       : QColor("#53ee82")))
                         : QColor("#ffaa44"));
      painter.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft,
          detection_.valid
              ? CRAWLING_TEXT("\xE6\xA3\x80\xE6\xB5\x8B\xE7\x84\x8A\xE7\xBC\x9D\x20\x25\x31\x2E\x2E\x25\x32\x20\x70\x78\x20\x2F\x20\x25\x33\xEF\xBC\x9B\xE9\x9D\x92\xE7\xBA\xBF\x3D\xE4\xB8\xAD\xE5\xBF\x83\xEF\xBC\x8C\xE9\xBB\x84\xE7\xBA\xBF\x3D\xE4\xB8\xBB\xE6\xBF\x80\xE5\x85\x89\xE5\x9F\xBA\xE7\xBA\xBF")
                    .arg(detection_.gapStartPx).arg(detection_.gapEndPx)
                    .arg(detection_.contourFallback
                             ? CRAWLING_TEXT("\xE5\x87\xB8\xE8\xB5\xB7\xE8\xBD\xAE\xE5\xBB\x93")
                             : (detection_.edgeBreakFallback
                                    ? CRAWLING_TEXT("\xE5\x8D\x95\xE8\xBE\xB9\xE6\x8E\xA8\xE6\x96\xAD")
                                    : CRAWLING_TEXT("\xE5\x8F\x8C\xE8\xBE\xB9\xE5\xAE\x9E\xE6\xB5\x8B")))
              : CRAWLING_TEXT("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB8\xA7\xE6\x9C\xAA\xE7\xA1\xAE\xE8\xAE\xA4\xE7\x84\x8A\xE7\xBC\x9D\xEF\xBC\x9B\xE9\x9D\x92\xE7\xBA\xBF\x3D\xE7\x9B\xAE\xE6\xA0\x87\xE4\xB8\xAD\xE5\xBF\x83"));
      painter.restore();
    }
    painter.setPen(QColor("#9fb3bf"));
    painter.drawText(42, height() - 8,
                     CRAWLING_TEXT("\xE5\x8E\x9F\xE5\xA7\x8B\xE5\x9B\xBE\x20\x25\x31\x20\x78\x20\x25\x32\xEF\xBC\x8C\xE5\xB7\xB2\xE9\x93\xBA\xE6\xBB\xA1\xE7\x94\xBB\xE5\xB8\x83")
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
