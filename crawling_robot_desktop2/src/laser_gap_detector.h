#pragma once

#include <QImage>

namespace crawling {

struct LaserGapDetectorConfig {
  double minimumGapRatio = 0.006;
  double minimumSideSupportRatio = 0.03;
  int minimumContrast = 24;
  int maximumHoleLength = 2;
  int minimumRunLength = 5;
  // When set, prefer the same gap as the previous frame. Ratios are measured
  // inside the detected laser-line span, not against the full image width.
  double expectedCenterRatio = -1.0;
  double expectedGapWidthRatio = -1.0;
  double maximumTrackingCenterJumpRatio = 0.10;
  // Full-image ratios provide a stable coordinate system when the detected
  // laser-line endpoints change because of reflections or partial occlusion.
  double expectedAbsoluteCenterRatio = -1.0;
  double referenceAbsoluteCenterRatio = -1.0;
  double maximumAbsoluteCenterJumpRatio = 0.06;
  double maximumReferenceCenterDriftRatio = 0.18;
  double trackingWidthWeight = 1.5;
  // If one weld edge is occluded or broken, the visible side can be used with
  // the previous line span/gap geometry to keep the trajectory continuous.
  bool allowEdgeBreakFallback = true;
  double edgeBreakGapRatio = 0.04;
  double minimumEdgeBreakRunRatio = 0.08;
  // Ratios against the full image axis, supplied by the previous frame.
  double expectedLineStartRatio = -1.0;
  double expectedLineEndRatio = -1.0;
  // 0 = no axis lock, 1 = horizontal laser line, 2 = vertical laser line.
  // Once tracking starts, locking the axis prevents a noisy fallback profile
  // from changing the coordinate system between frames.
  int expectedAxis = 0;
};

struct LaserGapDetection {
  bool valid = false;
  bool horizontal = true;
  double normalizedCenter = 0.5;
  double absoluteCenterRatio = 0.5;
  double confidence = 0.0;
  int gapStartPx = -1;
  int gapEndPx = -1;
  int lineStartPx = -1;
  int lineEndPx = -1;
  int supportingSamples = 0;
  bool continuityRejected = false;
  bool edgeBreakFallback = false;
};

class LaserGapDetector final {
 public:
  static LaserGapDetection detect(const QImage& image,
                                  const LaserGapDetectorConfig& config = {});
};

}  // namespace crawling
