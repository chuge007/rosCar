#pragma once

namespace crawling {

struct SynchronizerConfig {
  double proportionalGain = 0.30;
  double integralGain = 0.08;
  double maximumCorrectionMps = 0.030;
  double minimumControlledSpeedMps = 0.015;
};

struct SynchronizerResult {
  double leftMps = 0.0;
  double rightMps = 0.0;
  double normalizedError = 0.0;
  double correctionMps = 0.0;
  bool active = false;
};

class WheelSynchronizer final {
 public:
  SynchronizerResult update(double leftTargetMps, double rightTargetMps,
                            double leftActualMps, double rightActualMps,
                            bool feedbackFresh, double deltaSeconds,
                            const SynchronizerConfig& configuration);
  void reset();

 private:
  double integral_ = 0.0;
};

}  // namespace crawling

