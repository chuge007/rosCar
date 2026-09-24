#pragma once

namespace crawling {

struct SynchronizerConfig {
  // Weight of a new response sample and its time-dependent contribution.
  double proportionalGain = 0.30;
  double integralGain = 0.08;
  // Maximum amount by which the faster side may be throttled.
  double maximumCorrectionMps = 0.300;
  double minimumControlledSpeedMps = 0.015;
};

struct SynchronizerResult {
  double leftMps = 0.0;
  double rightMps = 0.0;
  double normalizedError = 0.0;
  double correctionMps = 0.0;
  double leftResponseFactor = 1.0;
  double rightResponseFactor = 1.0;
  double leftCommandScale = 1.0;
  double rightCommandScale = 1.0;
  bool active = false;
};

class WheelSynchronizer final {
 public:
  SynchronizerResult update(double leftTargetMps, double rightTargetMps,
                            double leftActualMps, double rightActualMps,
                            double leftAppliedCommandMps,
                            double rightAppliedCommandMps,
                            bool feedbackUpdated, double deltaSeconds,
                            const SynchronizerConfig& configuration);
  void reset();

 private:
  double leftResponseFactor_ = 1.0;
  double rightResponseFactor_ = 1.0;
  bool haveLeftResponse_ = false;
  bool haveRightResponse_ = false;
  int targetDirection_ = 0;
};

}  // namespace crawling
