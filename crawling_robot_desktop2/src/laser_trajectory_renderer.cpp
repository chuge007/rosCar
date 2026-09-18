#include "laser_trajectory_renderer.h"
#include "utf8_compat.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace crawling {
namespace {

constexpr int kTopMarginPx = 82;
constexpr int kBottomMarginPx = 74;
constexpr int kLeftMarginPx = 92;
constexpr int kRightMarginPx = 250;
constexpr int kMinimumWidthPx = 760;
constexpr int kMinimumHeightPx = 700;
constexpr int kMaximumWidthPx = 1600;
constexpr int kMaximumHeightPx = 5000;
constexpr double kNominalPixelsPerMeter = 4000.0;
constexpr double kGridSpacingM = 0.05;
constexpr int kMaximumStoredSessions = 3;

const QColor kBackground(18, 22, 27);
const QColor kPlotBackground(7, 10, 13);
const QColor kGrid(54, 63, 70);
const QColor kLaser(255, 62, 48, 170);
const QColor kLeftEdge(45, 212, 255);
const QColor kRightEdge(255, 190, 50);
const QColor kLeftFit(95, 151, 255);
const QColor kRightFit(255, 123, 84);
const QColor kCenterFit(80, 225, 130);
const QColor kRoadFill(70, 190, 120, 34);
const QColor kText(226, 232, 237);
const QColor kMutedText(160, 172, 181);

double maximumSampleDistance(const LaserTrajectorySegment& segment) {
  double maximum = 0.0;
  for (const LaserEdgeSample& sample : segment.scanSamples) {
    if (std::isfinite(sample.longitudinalM)) {
      maximum = std::max(maximum, sample.longitudinalM);
    }
  }
  return maximum;
}

double effectiveLength(const LaserTrajectorySegment& segment) {
  return std::max({0.001, segment.segmentLengthM,
                   maximumSampleDistance(segment)});
}

double averageSampleSpacing(const QVector<LaserEdgeSample>& samples) {
  if (samples.size() < 2) return 0.0;
  double total = 0.0;
  int count = 0;
  for (int index = 1; index < samples.size(); ++index) {
    const double delta = samples[index].longitudinalM -
                         samples[index - 1].longitudinalM;
    if (delta > 1e-7 && std::isfinite(delta)) {
      total += delta;
      ++count;
    }
  }
  return count > 0 ? total / count : 0.0;
}

bool savePng(const QImage& image, const QString& path, QString* error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = CRAWLING_TEXT("无法创建轨迹图片：%1").arg(path);
    }
    return false;
  }
  if (!image.save(&file, "PNG")) {
    file.cancelWriting();
    if (error) {
      *error = CRAWLING_TEXT("轨迹图片编码失败：%1").arg(path);
    }
    return false;
  }
  if (!file.commit()) {
    if (error) {
      *error = CRAWLING_TEXT("轨迹图片写入失败：%1").arg(path);
    }
    return false;
  }
  return true;
}

void drawLegendItem(QPainter* painter, int x, int y, const QColor& color,
                    const QString& text, Qt::PenStyle style = Qt::SolidLine) {
  QPen pen(color, 3.0, style);
  pen.setCapStyle(Qt::RoundCap);
  painter->setPen(pen);
  painter->drawLine(x, y, x + 28, y);
  painter->setPen(kText);
  painter->drawText(x + 38, y + 5, text);
}

}  // namespace

bool LaserTrajectoryRenderer::startSession(QString* error) {
  reset();
  const QDir applicationDirectory(QCoreApplication::applicationDirPath());
  const QString rootPath = applicationDirectory.filePath(
      QStringLiteral("laser_trajectory_maps"));
  if (!QDir().mkpath(rootPath)) {
    if (error) {
      *error = CRAWLING_TEXT("无法创建激光轨迹图目录：%1").arg(rootPath);
    }
    return false;
  }

  // Keep only the newest session directories. Match the names generated below
  // so unrelated files or manually-created folders are never removed.
  const QRegularExpression sessionNamePattern(
      QStringLiteral("^\\d{8}_\\d{6}_\\d{3}(?:_\\d+)?$"));
  QDir rootDirectory(rootPath);
  QFileInfoList sessionDirectories = rootDirectory.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  QVector<QFileInfo> matchingSessions;
  matchingSessions.reserve(sessionDirectories.size());
  for (const QFileInfo& directory : sessionDirectories) {
    if (sessionNamePattern.match(directory.fileName()).hasMatch()) {
      matchingSessions.append(directory);
    }
  }
  while (matchingSessions.size() >= kMaximumStoredSessions) {
    const QFileInfo oldest = matchingSessions.takeFirst();
    if (!QDir(oldest.absoluteFilePath()).removeRecursively()) {
      if (error) {
        *error = CRAWLING_TEXT("无法清理旧的激光轨迹图目录：%1")
                     .arg(oldest.absoluteFilePath());
      }
      return false;
    }
  }

  const QString timestamp = QDateTime::currentDateTime().toString(
      QStringLiteral("yyyyMMdd_HHmmss_zzz"));
  QString candidate = QDir(rootPath).filePath(timestamp);
  int suffix = 1;
  while (QFileInfo::exists(candidate)) {
    candidate = QDir(rootPath).filePath(
        QStringLiteral("%1_%2").arg(timestamp).arg(suffix++, 2, 10,
                                                   QLatin1Char('0')));
  }
  if (!QDir().mkpath(candidate)) {
    if (error) {
      *error = CRAWLING_TEXT("无法创建本次轨迹图目录：%1").arg(candidate);
    }
    return false;
  }
  sessionDirectory_ = QDir(candidate).absolutePath();
  return true;
}

void LaserTrajectoryRenderer::reset() {
  sessionDirectory_.clear();
  segmentCount_ = 0;
}

QString LaserTrajectoryRenderer::sessionDirectory() const {
  return sessionDirectory_;
}

LaserTrajectorySaveResult LaserTrajectoryRenderer::appendSegment(
    const QVector<LaserEdgeSample>& scanSamples, const LaserPathFit& fit,
    double segmentLengthM, double lateralSpanM, const QString& label) {
  LaserTrajectorySaveResult result;
  if (scanSamples.isEmpty()) {
    result.error = CRAWLING_TEXT("没有可绘制的激光断口轨迹样本");
    return result;
  }
  if (sessionDirectory_.isEmpty() && !startSession(&result.error)) {
    return result;
  }

  LaserTrajectorySegment segment;
  segment.scanSamples = scanSamples;
  segment.fit = fit;
  segment.segmentLengthM = std::max(0.001, segmentLengthM);
  segment.label = label;

  const int number = ++segmentCount_;
  segment.sequenceNumber = number;
  const QString numberText = QStringLiteral("%1").arg(number, 4, 10,
                                                       QLatin1Char('0'));
  result.segmentPath = QDir(sessionDirectory_).filePath(
      QStringLiteral("segment_%1.png").arg(numberText));
  const QImage segmentImage = render({segment}, lateralSpanM);
  if (!savePng(segmentImage, result.segmentPath, &result.error)) {
    return result;
  }
  result.success = true;
  return result;
}

QImage LaserTrajectoryRenderer::render(
    const QVector<LaserTrajectorySegment>& segments, double lateralSpanM) {
  lateralSpanM = std::max(0.001, lateralSpanM);
  double totalDistanceM = 0.0;
  for (const LaserTrajectorySegment& segment : segments) {
    totalDistanceM += effectiveLength(segment);
  }
  totalDistanceM = std::max(0.01, totalDistanceM);

  const double horizontalLimit =
      (kMaximumWidthPx - kLeftMarginPx - kRightMarginPx) / lateralSpanM;
  const double verticalLimit =
      (kMaximumHeightPx - kTopMarginPx - kBottomMarginPx) / totalDistanceM;
  const double scale = std::max(
      1.0, std::min({kNominalPixelsPerMeter, horizontalLimit, verticalLimit}));
  const int plotWidth = std::max(1, static_cast<int>(
                                        std::ceil(lateralSpanM * scale)));
  const int plotHeight = std::max(1, static_cast<int>(
                                         std::ceil(totalDistanceM * scale)));
  const int width = std::max(kMinimumWidthPx,
                             kLeftMarginPx + plotWidth + kRightMarginPx);
  const int height = std::max(kMinimumHeightPx,
                              kTopMarginPx + plotHeight + kBottomMarginPx);

  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(kBackground);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const bool canDrawText =
      qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr;

  const QRectF plot(kLeftMarginPx, kTopMarginPx, plotWidth, plotHeight);
  painter.fillRect(plot, kPlotBackground);
  const auto lateralToPixel = [&](double lateralM) {
    const double bounded = std::clamp(lateralM, -lateralSpanM * 0.5,
                                      lateralSpanM * 0.5);
    return plot.left() + (bounded + lateralSpanM * 0.5) * scale;
  };
  const auto distanceToPixel = [&](double distanceM) {
    return plot.bottom() - std::clamp(distanceM, 0.0, totalDistanceM) * scale;
  };

  painter.setPen(QPen(kGrid, 1.0, Qt::DotLine));
  const int lateralGridCount = static_cast<int>(
      std::ceil((lateralSpanM * 0.5) / kGridSpacingM));
  for (int index = -lateralGridCount; index <= lateralGridCount; ++index) {
    const double lateralM = index * kGridSpacingM;
    if (std::abs(lateralM) <= lateralSpanM * 0.5 + 1e-9) {
      const double x = lateralToPixel(lateralM);
      painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
  }
  for (double distanceM = 0.0; distanceM <= totalDistanceM + 1e-9;
       distanceM += kGridSpacingM) {
    const double y = distanceToPixel(distanceM);
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
  }

  double offsetM = 0.0;
  for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
    const LaserTrajectorySegment& segment = segments[segmentIndex];
    const double lengthM = effectiveLength(segment);
    double localMaximumM = maximumSampleDistance(segment);
    if (localMaximumM <= 0.0) localMaximumM = lengthM;

    if (segment.fit.valid) {
      const auto fitPoint = [&](double longitudinalM, double slope,
                                double interceptM) {
        return QPointF(lateralToPixel(slope * longitudinalM + interceptM),
                       distanceToPixel(offsetM + longitudinalM));
      };
      QPolygonF road;
      road << fitPoint(0.0, segment.fit.leftSlope,
                       segment.fit.leftInterceptM)
           << fitPoint(localMaximumM, segment.fit.leftSlope,
                       segment.fit.leftInterceptM)
           << fitPoint(localMaximumM, segment.fit.rightSlope,
                       segment.fit.rightInterceptM)
           << fitPoint(0.0, segment.fit.rightSlope,
                       segment.fit.rightInterceptM);
      painter.setPen(Qt::NoPen);
      painter.setBrush(kRoadFill);
      painter.drawPolygon(road);
    }

    const double averageSpacingM = averageSampleSpacing(segment.scanSamples);
    const double laserWidthPx = std::clamp(averageSpacingM * scale * 0.75,
                                           1.0, 3.0);
    painter.setPen(QPen(kLaser, laserWidthPx, Qt::SolidLine, Qt::FlatCap));
    for (const LaserEdgeSample& sample : segment.scanSamples) {
      const double y = distanceToPixel(offsetM + sample.longitudinalM);
      painter.drawLine(QPointF(lateralToPixel(-lateralSpanM * 0.5), y),
                       QPointF(lateralToPixel(sample.leftLateralM), y));
      painter.drawLine(QPointF(lateralToPixel(sample.rightLateralM), y),
                       QPointF(lateralToPixel(lateralSpanM * 0.5), y));
    }

    QPainterPath leftEdgePath;
    QPainterPath rightEdgePath;
    bool firstPoint = true;
    for (const LaserEdgeSample& sample : segment.scanSamples) {
      const QPointF left(lateralToPixel(sample.leftLateralM),
                         distanceToPixel(offsetM + sample.longitudinalM));
      const QPointF right(lateralToPixel(sample.rightLateralM),
                          distanceToPixel(offsetM + sample.longitudinalM));
      if (firstPoint) {
        leftEdgePath.moveTo(left);
        rightEdgePath.moveTo(right);
        firstPoint = false;
      } else {
        leftEdgePath.lineTo(left);
        rightEdgePath.lineTo(right);
      }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kLeftEdge, 1.4));
    painter.drawPath(leftEdgePath);
    painter.setPen(QPen(kRightEdge, 1.4));
    painter.drawPath(rightEdgePath);

    const int pointStep = std::max(1, segment.scanSamples.size() / 350);
    for (int index = 0; index < segment.scanSamples.size(); index += pointStep) {
      const LaserEdgeSample& sample = segment.scanSamples[index];
      const double y = distanceToPixel(offsetM + sample.longitudinalM);
      painter.setPen(Qt::NoPen);
      painter.setBrush(kLeftEdge);
      painter.drawEllipse(QPointF(lateralToPixel(sample.leftLateralM), y),
                          1.8, 1.8);
      painter.setBrush(kRightEdge);
      painter.drawEllipse(QPointF(lateralToPixel(sample.rightLateralM), y),
                          1.8, 1.8);
    }

    if (segment.fit.valid) {
      const auto drawFit = [&](double slope, double interceptM,
                               const QColor& color, double widthPx,
                               Qt::PenStyle style) {
        painter.setPen(QPen(color, widthPx, style, Qt::RoundCap));
        painter.drawLine(
            QPointF(lateralToPixel(interceptM), distanceToPixel(offsetM)),
            QPointF(lateralToPixel(slope * localMaximumM + interceptM),
                    distanceToPixel(offsetM + localMaximumM)));
      };
      drawFit(segment.fit.leftSlope, segment.fit.leftInterceptM, kLeftFit,
              2.5, Qt::DashLine);
      drawFit(segment.fit.rightSlope, segment.fit.rightInterceptM, kRightFit,
              2.5, Qt::DashLine);
      drawFit(segment.fit.centerSlope, segment.fit.centerInterceptM, kCenterFit,
              3.5, Qt::SolidLine);
    }

    painter.setPen(QPen(QColor(210, 219, 226, 150), 1.0, Qt::DashLine));
    const double boundaryY = distanceToPixel(offsetM + lengthM);
    painter.drawLine(QPointF(plot.left(), boundaryY),
                     QPointF(plot.right(), boundaryY));
    if (canDrawText) {
      painter.setPen(kMutedText);
      painter.drawText(QPointF(plot.right() + 12, boundaryY + 5),
                       CRAWLING_TEXT("段 %1  %2")
                           .arg(segment.sequenceNumber > 0
                                    ? segment.sequenceNumber
                                    : segmentIndex + 1)
                           .arg(segment.label));
      if (segment.fit.valid) {
        painter.drawText(
            QPointF(plot.right() + 12, boundaryY + 23),
            CRAWLING_TEXT("角度 %1°  RMS %2 mm")
                .arg(segment.fit.angleRad * 180.0 / 3.14159265358979323846,
                     0, 'f', 2)
                .arg(segment.fit.rmsErrorM * 1000.0, 0, 'f', 2));
      }
    }
    offsetM += lengthM;
  }

  painter.setPen(QPen(QColor(135, 148, 158), 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(plot);
  if (canDrawText) {
    painter.setPen(kText);
    QFont titleFont = painter.font();
    titleFont.setPointSize(15);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QPointF(kLeftMarginPx, 32),
                     CRAWLING_TEXT("线激光缺口连续轨迹图"));
    QFont normalFont = painter.font();
    normalFont.setPointSize(9);
    normalFont.setBold(false);
    painter.setFont(normalFont);
    painter.setPen(kMutedText);
    painter.drawText(QPointF(kLeftMarginPx, 55),
                     CRAWLING_TEXT("横向范围 %1 mm · 行驶距离 %2 mm")
                         .arg(lateralSpanM * 1000.0, 0, 'f', 1)
                         .arg(totalDistanceM * 1000.0, 0, 'f', 1));
    painter.drawText(QPointF(plot.left(), height - 27),
                     CRAWLING_TEXT("横向位置（左负 / 右正，单位 mm）"));
    painter.save();
    painter.translate(23, plot.bottom());
    painter.rotate(-90.0);
    painter.drawText(QPointF(0, 0), CRAWLING_TEXT("行驶距离（单位 mm）"));
    painter.restore();

    const int legendX = static_cast<int>(plot.right()) + 18;
    int legendY = kTopMarginPx + 24;
    drawLegendItem(&painter, legendX, legendY, kLaser,
                   CRAWLING_TEXT("检测到的激光段"));
    legendY += 28;
    drawLegendItem(&painter, legendX, legendY, kLeftEdge,
                   CRAWLING_TEXT("缺口左边缘点"));
    legendY += 28;
    drawLegendItem(&painter, legendX, legendY, kRightEdge,
                   CRAWLING_TEXT("缺口右边缘点"));
    legendY += 28;
    drawLegendItem(&painter, legendX, legendY, kLeftFit,
                   CRAWLING_TEXT("左边缘拟合线"), Qt::DashLine);
    legendY += 28;
    drawLegendItem(&painter, legendX, legendY, kRightFit,
                   CRAWLING_TEXT("右边缘拟合线"), Qt::DashLine);
    legendY += 28;
    drawLegendItem(&painter, legendX, legendY, kCenterFit,
                   CRAWLING_TEXT("轨迹道路中心线"));
  }

  painter.end();
  return image;
}

LaserTrajectoryWriter::LaserTrajectoryWriter(QObject* parent)
    : QObject(parent) {}

void LaserTrajectoryWriter::beginSession(quint64 sessionId) {
  currentSessionId_ = sessionId;
  QString error;
  if (!renderer_.startSession(&error)) {
    emit saveFailed(sessionId, error);
    return;
  }
  emit sessionStarted(sessionId, renderer_.sessionDirectory());
}

void LaserTrajectoryWriter::saveSegment(
    quint64 sessionId, const QVector<LaserEdgeSample>& scanSamples,
    const LaserPathFit& fit, double segmentLengthM, double lateralSpanM,
    const QString& label) {
  if (sessionId != currentSessionId_) return;
  const LaserTrajectorySaveResult result = renderer_.appendSegment(
      scanSamples, fit, segmentLengthM, lateralSpanM, label);
  if (!result.success) {
    emit saveFailed(sessionId, result.error);
    return;
  }
  emit imageSaved(sessionId, result.segmentPath);
}

void LaserTrajectoryWriter::shutdown() {
  currentSessionId_ = 0;
  renderer_.reset();
}

}  // namespace crawling
