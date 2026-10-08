#pragma once
#include "laser_gap_detector.h"

namespace crawling {
// Works exclusively in original camera pixels. Image-up displacement is not
// calibrated 3D height. Both exterior plate shoulders must be measured.
class OpenCvLaserContour final {
 public:
  static LaserGapDetection detect(const QImage& grayscale,
                                  double baselineOffset, double baselineSlope,
                                  double baselineHalfWidth,
                                  const LaserGapDetectorConfig& config);
};
}
