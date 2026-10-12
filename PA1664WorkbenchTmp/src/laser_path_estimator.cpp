#include "laser_path_estimator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

struct Line {
  bool valid = false;
  double slope = 0.0;
  double intercept = 0.0;
};

double edgeLongitudinal(const LaserEdgeSample& sample, bool leftEdge) {
  const double coordinate = leftEdge ? sample.leftLongitudinalM
                                     : sample.rightLongitudinalM;
  return std::isfinite(coordinate) ? coordinate : sample.longitudinalM;
}

Line leastSquares(const QVector<LaserEdgeSample>& samples,
                  const QVector<int>& indices, bool leftEdge) {
  if (indices.size() < 2) return {};

  double sumX = 0.0;
  double sumY = 0.0;
  for (const int index : indices) {
    const LaserEdgeSample& sample = samples[index];
    sumX += edgeLongitudinal(sample, leftEdge);
    sumY += leftEdge ? sample.leftLateralM : sample.rightLateralM;
  }
  const double meanX = sumX / indices.size();
  const double meanY = sumY / indices.size();
  double numerator = 0.0;
  double denominator = 0.0;
  for (const int index : indices) {
    const LaserEdgeSample& sample = samples[index];
    const double x = edgeLongitudinal(sample, leftEdge) - meanX;
    const double y = (leftEdge ? sample.leftLateralM
                               : sample.rightLateralM) - meanY;
    numerator += x * y;
    denominator += x * x;
  }
  if (!(denominator > 1e-12) || !std::isfinite(denominator)) return {};
  const double slope = numerator / denominator;
  const double intercept = meanY - slope * meanX;
  if (!std::isfinite(slope) || !std::isfinite(intercept)) return {};
  return {true, slope, intercept};
}

double median(QVector<double> values) {
  if (values.isEmpty()) return 0.0;
  const int middle = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + middle, values.end());
  const double upper = values[middle];
  if (values.size() % 2 != 0) return upper;
  std::nth_element(values.begin(), values.begin() + middle - 1,
                   values.begin() + middle);
  return (values[middle - 1] + upper) * 0.5;
}

double residual(const LaserEdgeSample& sample, const Line& left,
                const Line& right) {
  const double leftError = sample.leftLateralM -
                           (left.slope * edgeLongitudinal(sample, true) +
                            left.intercept);
  const double rightError = sample.rightLateralM -
                            (right.slope * edgeLongitudinal(sample, false) +
                             right.intercept);
  return std::sqrt((leftError * leftError + rightError * rightError) * 0.5);
}

}  // namespace

LaserPathFit LaserPathEstimator::fit(const QVector<LaserEdgeSample>& samples,
                                     int minimumSamples,
                                     double minimumSpanM,
                                     double maximumRmsErrorM) {
  LaserPathFit result;
  minimumSamples = std::max(4, minimumSamples);
  if (samples.size() < minimumSamples) {
    result.failureCode = 1;
    return result;
  }

  QVector<int> indices;
  indices.reserve(samples.size());
  double minimumX = std::numeric_limits<double>::infinity();
  double maximumX = -std::numeric_limits<double>::infinity();
  for (int index = 0; index < samples.size(); ++index) {
    const LaserEdgeSample& sample = samples[index];
    if (!std::isfinite(sample.longitudinalM) ||
        !std::isfinite(sample.leftLateralM) ||
        !std::isfinite(sample.rightLateralM) ||
        sample.leftLateralM >= sample.rightLateralM) {
      continue;
    }
    indices.append(index);
    minimumX = std::min(minimumX, sample.longitudinalM);
    maximumX = std::max(maximumX, sample.longitudinalM);
  }
  result.validSampleCount = indices.size();
  result.observedSpanM = std::max(0.0, maximumX - minimumX);
  if (indices.size() < minimumSamples) {
    result.failureCode = 2;
    return result;
  }
  if (maximumX - minimumX < minimumSpanM) {
    result.failureCode = 3;
    return result;
  }

  Line left = leastSquares(samples, indices, true);
  Line right = leastSquares(samples, indices, false);
  if (!left.valid || !right.valid) {
    result.failureCode = 4;
    return result;
  }

  // Reject isolated reflections without discarding a consistently curved or
  // noisy edge. Both edge residuals contribute to one robust threshold.
  QVector<double> residuals;
  residuals.reserve(indices.size());
  for (const int index : indices) {
    residuals.append(residual(samples[index], left, right));
  }
  const double residualMedian = median(residuals);
  QVector<double> deviations;
  deviations.reserve(residuals.size());
  for (const double value : residuals) {
    deviations.append(std::abs(value - residualMedian));
  }
  const double mad = median(deviations);
  const double rejectionThreshold =
      std::max(0.0015, residualMedian + std::max(0.001, 3.5 * mad));
  QVector<int> inliers;
  inliers.reserve(indices.size());
  for (int i = 0; i < indices.size(); ++i) {
    if (residuals[i] <= rejectionThreshold) inliers.append(indices[i]);
  }
  result.inlierCount = inliers.size();
  if (inliers.size() < minimumSamples) {
    result.failureCode = 5;
    return result;
  }

  left = leastSquares(samples, inliers, true);
  right = leastSquares(samples, inliers, false);
  if (!left.valid || !right.valid) {
    result.failureCode = 4;
    return result;
  }

  double squaredError = 0.0;
  minimumX = std::numeric_limits<double>::infinity();
  maximumX = -std::numeric_limits<double>::infinity();
  for (const int index : inliers) {
    const double error = residual(samples[index], left, right);
    squaredError += error * error;
    minimumX = std::min(minimumX, samples[index].longitudinalM);
    maximumX = std::max(maximumX, samples[index].longitudinalM);
  }
  const double rms = std::sqrt(squaredError / inliers.size());
  const double centerSlope = (left.slope + right.slope) * 0.5;
  const double span = maximumX - minimumX;
  result.observedSpanM = std::max(0.0, span);
  result.observedRmsErrorM = std::isfinite(rms) ? rms : 0.0;
  if (!std::isfinite(rms) || rms > maximumRmsErrorM) {
    result.failureCode = 6;
    return result;
  }
  if (span < minimumSpanM) {
    result.failureCode = 3;
    return result;
  }

  result.valid = true;
  result.leftSlope = left.slope;
  result.leftInterceptM = left.intercept;
  result.rightSlope = right.slope;
  result.rightInterceptM = right.intercept;
  result.centerSlope = centerSlope;
  result.centerInterceptM = (left.intercept + right.intercept) * 0.5;
  result.angleRad = std::atan(centerSlope);
  result.rmsErrorM = rms;
  result.spanM = span;
  result.sampleCount = inliers.size();
  return result;
}

}  // namespace crawling
