#pragma once

#include <QMetaType>
#include <QVector>

namespace crawling {

struct LaserEdgeSample {
  double longitudinalM = 0.0;
  double leftLateralM = 0.0;
  double rightLateralM = 0.0;
};

struct LaserPathFit {
  bool valid = false;
  // 0 means valid; non-zero values identify the rejected-fit condition.
  int failureCode = 0;
  int validSampleCount = 0;
  int inlierCount = 0;
  double observedSpanM = 0.0;
  double observedRmsErrorM = 0.0;
  double leftSlope = 0.0;
  double leftInterceptM = 0.0;
  double rightSlope = 0.0;
  double rightInterceptM = 0.0;
  double centerSlope = 0.0;
  double centerInterceptM = 0.0;
  double angleRad = 0.0;
  double rmsErrorM = 0.0;
  double spanM = 0.0;
  int sampleCount = 0;
};

class LaserPathEstimator final {
 public:
  static LaserPathFit fit(const QVector<LaserEdgeSample>& samples,
                          int minimumSamples, double minimumSpanM,
                          double maximumRmsErrorM);
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserEdgeSample)
Q_DECLARE_METATYPE(crawling::LaserPathFit)
Q_DECLARE_METATYPE(QVector<crawling::LaserEdgeSample>)
