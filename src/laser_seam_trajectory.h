#pragma once

#include "laser_path_estimator.h"

#include <QtGlobal>
#include <QVector>

namespace crawling {

// The same body/odometry convention used by the correction controller:
// longitudinal is forward; positive lateral and positive yaw are rightward.
struct LaserSeamPose {
  double longitudinalM = 0.0;
  double lateralM = 0.0;
  double yawRad = 0.0;
};

struct LaserSeamPrediction {
  bool valid = false;
  double absoluteCenterRatio = 0.5;
  double absoluteWidthRatio = 0.0;
  double uncertaintyRatio = 0.0;
  double confidence = 0.0;
  double observedSpanM = 0.0;
  double extrapolationM = 0.0;
  qint64 observationAgeMs = -1;
  int sampleCount = 0;
};

// A bounded association prior from real, registered scan edges. It does not
// produce a measurement and must never renew a detector timestamp or feed
// predicted points back into the fitted cloud. This keeps an occluded weld's
// identity continuous without turning an old pixel into permanent evidence.
class LaserSeamTrajectory final {
 public:
  void clear();
  void observe(const LaserSeamPose& pose, qint64 receivedMs,
               double leftLateralM, double rightLateralM,
               double lookaheadM, double confidence);
  LaserSeamPrediction predict(const LaserSeamPose& pose, qint64 receivedMs,
                              double lookaheadM, double imageSpanM,
                              double cameraLateralSign) const;

 private:
  struct Observation {
    LaserEdgeSample worldEdges;
    LaserSeamPose pose;
    qint64 receivedMs = -1;
    double confidence = 0.0;
  };
  QVector<Observation> observations_;
};

}  // namespace crawling
