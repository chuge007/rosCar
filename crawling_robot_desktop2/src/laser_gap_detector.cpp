#include "laser_gap_detector.h"

#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace crawling {
namespace {

struct Run {
  int start = 0;
  int end = 0;

  int length() const { return end - start + 1; }
};

void keepTopSamples(std::array<int, 3>* samples, int value) {
  if (value >= (*samples)[0]) {
    (*samples)[2] = (*samples)[1];
    (*samples)[1] = (*samples)[0];
    (*samples)[0] = value;
  } else if (value >= (*samples)[1]) {
    (*samples)[2] = (*samples)[1];
    (*samples)[1] = value;
  } else if (value > (*samples)[2]) {
    (*samples)[2] = value;
  }
}

int histogramPercentile(const std::array<int, 256>& histogram, int total,
                        double percentile) {
  const int target = std::clamp(
      static_cast<int>(std::ceil(percentile * std::max(1, total))), 1,
      std::max(1, total));
  int cumulative = 0;
  for (int value = 0; value < static_cast<int>(histogram.size()); ++value) {
    cumulative += histogram[static_cast<std::size_t>(value)];
    if (cumulative >= target) return value;
  }
  return 255;
}

int vectorPercentile(QVector<int> values, double percentile) {
  if (values.isEmpty()) return 0;
  const int index = std::clamp(
      static_cast<int>(std::lround(percentile * (values.size() - 1))), 0,
      values.size() - 1);
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}

struct Ridge {
  double crossPx = 0.0;
  double halfWidthPx = 0.0;
  int strength = 0;
};

struct BaselineProjection {
  bool valid = false;
  double offsetPx = 0.0;
  double slope = 0.0;
  double halfWidthPx = 0.0;
  double supportRatio = 0.0;
  double residualPx = 0.0;
  QVector<int> profile;
  QVector<int> ridgeCrossPx;
  QVector<bool> contourProfile;
  int contourStartSample = -1;
  int contourEndSample = -1;
  double contourConfidence = 0.0;
  int contourDisplacementThresholdPx = 0;
  int contourMinimumRunSamples = 0;
  int contourLineStartSample = -1;
  int contourLineEndSample = -1;
};

// Derive a geometric contour from the same raw image used by the baseline
// detector. A weld raised above the plate may have no dark interruption in
// the fitted stripe, but its laser return remains a bright, displaced ridge.
QVector<Run> collectRuns(const QVector<bool>& present);
void closeSmallHoles(QVector<bool>* present, int maximumHole);
void removeShortRuns(QVector<bool>* present, int minimumRun);

void detectDisplacedContour(const QImage& grayscale, int sampleStep,
                            int backgroundLevel, bool horizontal,
                            const LaserGapDetectorConfig& config,
                            BaselineProjection* output) {
  if (!output || !output->valid || output->profile.isEmpty()) return;
  const int axisLength = horizontal ? grayscale.width() : grayscale.height();
  const int crossLength = horizontal ? grayscale.height() : grayscale.width();
  const int count = output->profile.size();
  if (axisLength < 48 || crossLength < 24 || count < 24) return;

  // Search a wider geometric band than the intensity projection. The
  // threshold is relative to the supported stripe so background texture is
  // not promoted merely because it is a few gray levels above black.
  const int lineLevel = vectorPercentile(output->profile, 0.94);
  const int ridgeThreshold = backgroundLevel + std::max(
      config.minimumContrast,
      static_cast<int>(std::lround((lineLevel - backgroundLevel) * 0.28)));
  const int displacementThreshold = std::clamp(
      static_cast<int>(std::lround(std::max(5.0,
          output->halfWidthPx * 0.90))), 5, std::max(5, crossLength / 5));
  const int minimumRun = std::max(
      4, static_cast<int>(std::ceil(
          count * std::max(0.018, config.minimumGapRatio))));
  const int holeLimit = std::max(
      1, static_cast<int>(std::ceil(count * 0.006)));
  output->contourDisplacementThresholdPx = displacementThreshold;
  output->contourMinimumRunSamples = minimumRun;

  const auto pixel = [&](int axis, int cross) {
    return horizontal ? grayscale.constScanLine(cross)[axis]
                      : grayscale.constScanLine(axis)[cross];
  };
  QVector<bool> contour(count, false);
  QVector<bool> negativeContour(count, false);
  QVector<bool> positiveContour(count, false);
  const int searchRadius = std::max(
      displacementThreshold + 4, static_cast<int>(std::lround(crossLength * 0.22)));
  for (int i = 0; i < count; ++i) {
    const int axis = std::min(axisLength - 1, i * sampleStep);
    const double prediction = output->offsetPx + output->slope * axis;
    const int lower = std::max(0, static_cast<int>(std::floor(prediction)) -
                                      searchRadius);
    const int upper = std::min(crossLength - 1,
                               static_cast<int>(std::ceil(prediction)) +
                                   searchRadius);
    int cross = lower;
    while (cross <= upper) {
      if (pixel(axis, cross) < ridgeThreshold) {
        cross += sampleStep;
        continue;
      }
      const int start = cross;
      double moment = 0.0;
      double weight = 0.0;
      int peak = 0;
      while (cross <= upper && pixel(axis, cross) >= ridgeThreshold) {
        const int value = pixel(axis, cross);
        const double currentWeight = value - backgroundLevel;
        moment += cross * currentWeight;
        weight += currentWeight;
        peak = std::max(peak, value);
        cross += sampleStep;
      }
      const int width = cross - start;
      if (width <= std::max(12, crossLength / 10) && weight > 0.0 &&
          peak - backgroundLevel >= config.minimumContrast &&
          std::abs(moment / weight - prediction) >= displacementThreshold) {
        if (moment / weight < prediction) negativeContour[i] = true;
        else positiveContour[i] = true;
      }
    }
  }
  // A raised or recessed surface has a consistent displacement sign; never
  // join alternating upper/lower reflections into one artificial contour.
  for (QVector<bool>* side : {&negativeContour, &positiveContour}) {
    closeSmallHoles(side, holeLimit);
    removeShortRuns(side, minimumRun);
    closeSmallHoles(side, holeLimit);
  }
  for (int i = 0; i < count; ++i) {
    contour[i] = negativeContour[i] || positiveContour[i];
  }
  output->contourProfile = contour;
  QVector<Run> runs = collectRuns(negativeContour);
  runs += collectRuns(positiveContour);
  if (runs.isEmpty()) return;

  int firstBaseline = -1;
  int lastBaseline = -1;
  for (int i = 0; i < output->ridgeCrossPx.size(); ++i) {
    if (output->ridgeCrossPx[i] < 0) continue;
    if (firstBaseline < 0) firstBaseline = i;
    lastBaseline = i;
  }
  if (firstBaseline < 0 || lastBaseline <= firstBaseline) return;
  const int lineSpan = lastBaseline - firstBaseline + 1;
  const int minimumExteriorSupport = std::max(3, lineSpan / 30);
  const bool hasExpectedCenter = config.expectedAbsoluteCenterRatio >= 0.0 &&
                                 config.expectedAbsoluteCenterRatio <= 1.0;
  const bool hasExpectedWidth =
      config.expectedAbsoluteGapWidthRatio > 0.0 ||
      (config.expectedGapWidthRatio > 0.0 &&
       config.expectedLineEndRatio > config.expectedLineStartRatio &&
       config.expectedLineStartRatio >= 0.0);
  double bestScore = -1.0;
  int bestStart = -1;
  int bestEnd = -1;
  int bestSupport = 0;
  for (const Run& run : runs) {
    const int start = std::max(firstBaseline, run.start);
    const int end = std::min(lastBaseline, run.end);
    if (end < start || end - start + 1 < minimumRun) continue;
    const int width = end - start + 1;
    // A contour spanning nearly the complete line is generally a bad fit or
    // glare, not a weld. Require useful parent-stripe support where possible.
    if (width > std::max(minimumRun, static_cast<int>(lineSpan * 0.82))) continue;
    int leftSupport = 0;
    int rightSupport = 0;
    for (int i = firstBaseline; i < start; ++i) {
      if (output->ridgeCrossPx[i] >= 0) ++leftSupport;
    }
    for (int i = end + 1; i <= lastBaseline; ++i) {
      if (output->ridgeCrossPx[i] >= 0) ++rightSupport;
    }
    const int exteriorSupport = leftSupport + rightSupport;
    // Both shoulders are measured for this observation. A single visible
    // shoulder remains admissible only at the established optical boundary;
    // the ordinary interior case must have shoulders on both sides.
    const double runCenterRatio =
        (static_cast<double>(start + end) * 0.5 - firstBaseline) /
        std::max(1, lineSpan - 1);
    const bool nearBoundary = runCenterRatio < 0.18 || runCenterRatio > 0.82;
    if ((!nearBoundary && (leftSupport < minimumExteriorSupport ||
                           rightSupport < minimumExteriorSupport)) ||
        (nearBoundary && std::max(leftSupport, rightSupport) <
                              minimumExteriorSupport)) continue;
    int unsupportedBaseline = 0;
    for (int i = start; i <= end; ++i) {
      if (output->ridgeCrossPx[i] < 0) ++unsupportedBaseline;
    }
    // A displaced stripe alongside a perfectly continuous parent stripe is
    // a reflection, not evidence that the plate contour rose at that point.
    if (unsupportedBaseline < width * 0.55) continue;
    const double absoluteCenter =
        static_cast<double>(start + end) * 0.5 * sampleStep /
        std::max(1, axisLength - 1);
    if (config.referenceAbsoluteCenterRatio >= 0.0 &&
        config.referenceAbsoluteCenterRatio <= 1.0 &&
        std::abs(absoluteCenter - config.referenceAbsoluteCenterRatio) >
            std::max(0.08, config.maximumReferenceCenterDriftRatio)) continue;
    double score = width * (1.0 + std::log1p(exteriorSupport));
    if (hasExpectedCenter) {
      const double centerError =
          std::abs(absoluteCenter - config.expectedAbsoluteCenterRatio);
      if (centerError > std::max(0.02, config.maximumAbsoluteCenterJumpRatio)) {
        continue;
      }
      score /= 1.0 + centerError * 8.0;
    }
    if (hasExpectedWidth) {
      const double expectedWidthPx = config.expectedAbsoluteGapWidthRatio > 0.0
          ? config.expectedAbsoluteGapWidthRatio *
                std::max(1, axisLength - 1)
          : config.expectedGapWidthRatio *
                (config.expectedLineEndRatio - config.expectedLineStartRatio) *
                std::max(1, axisLength - 1);
      const double widthScale = width * sampleStep / std::max(1.0, expectedWidthPx);
      if (widthScale < 0.45 || widthScale > 1.75) continue;
      score /= 1.0 + std::abs(widthScale - 1.0) * 2.0;
    }
    if (score > bestScore) {
      bestScore = score;
      bestStart = start;
      bestEnd = end;
      bestSupport = exteriorSupport;
    }
  }
  if (bestStart < 0) return;
  output->contourStartSample = bestStart;
  output->contourEndSample = bestEnd;
  output->contourLineStartSample = firstBaseline;
  output->contourLineEndSample = lastBaseline;
  const double supportScore = std::clamp(
      static_cast<double>(bestSupport) / std::max(1, lineSpan / 3), 0.0, 1.0);
  const double widthScore = std::clamp(
      static_cast<double>(bestEnd - bestStart + 1) /
          std::max(1, lineSpan / 3),
      0.0, 1.0);
  // Keep the auxiliary path below a normal measured gap's authority. The
  // controller can use it to avoid losing the seam, then wait for a fresh
  // two-edge observation before restoring full steering authority.
  // Keep this below the controller's geometry-sample threshold. A contour
  // recovery can hold the live center loop, but must not enter the stitched
  // path fit until a normal two-edge frame corroborates it.
  output->contourConfidence = std::min(0.075, 0.035 +
      0.025 * widthScore + 0.025 * supportScore);
}

// Keep the long parent stripe, including a slope, separate from elevated or
// depressed weld returns. A whole-image maximum/top-three projection loses
// this distinction and makes a physically broken stripe appear continuous.
BaselineProjection projectLaserBaseline(const QImage& grayscale,
                                        const QVector<int>& fullProfile,
                                        int sampleStep, int backgroundLevel,
                                        bool horizontal,
                                        const LaserGapDetectorConfig& config) {
  BaselineProjection output;
  const int axisLength = horizontal ? grayscale.width() : grayscale.height();
  const int crossLength = horizontal ? grayscale.height() : grayscale.width();
  const int count = fullProfile.size();
  output.profile.fill(backgroundLevel, count);
  output.ridgeCrossPx.fill(-1, count);
  if (count < 24) return output;
  const int lineLevel = vectorPercentile(fullProfile, 0.94);
  if (lineLevel - backgroundLevel < config.minimumContrast) return output;
  const int ridgeThreshold = backgroundLevel + std::max(
      config.minimumContrast,
      static_cast<int>(std::lround((lineLevel - backgroundLevel) * 0.28)));
  const auto pixel = [&](int axis, int cross) {
    return horizontal ? grayscale.constScanLine(cross)[axis]
                      : grayscale.constScanLine(axis)[cross];
  };

  // Keep several distinct ridges per column instead of the brightest pixel:
  // a short bright weld return must not hide the dimmer main laser stripe.
  QVector<QVector<Ridge>> ridges(count);
  QVector<int> occupied;
  for (int i = 0; i < count; ++i) {
    const int axis = std::min(axisLength - 1, i * sampleStep);
    int cross = 0;
    while (cross < crossLength) {
      if (pixel(axis, cross) < ridgeThreshold) {
        cross += sampleStep;
        continue;
      }
      const int start = cross;
      double moment = 0.0;
      double weight = 0.0;
      int peak = 0;
      while (cross < crossLength && pixel(axis, cross) >= ridgeThreshold) {
        const int value = pixel(axis, cross);
        const double currentWeight = value - backgroundLevel;
        moment += cross * currentWeight;
        weight += currentWeight;
        peak = std::max(peak, value);
        cross += sampleStep;
      }
      const int width = cross - start;
      // Saturated broad patches are not evidence of a laser-line center.
      if (width <= std::max(12, crossLength / 10) && weight > 0.0) {
        ridges[i].append({moment / weight, width * 0.5, peak});
      }
    }
    std::sort(ridges[i].begin(), ridges[i].end(),
              [](const Ridge& left, const Ridge& right) {
                return left.strength > right.strength;
              });
    if (ridges[i].size() > 4) ridges[i].resize(4);
    if (!ridges[i].isEmpty()) occupied.append(i);
  }
  if (occupied.size() < std::max(12, count / 6)) return output;

  const double fitTolerancePx = std::max(4.0, crossLength * 0.008);
  const auto nearestRidge = [&](int index, double prediction,
                                double tolerance) -> const Ridge* {
    const Ridge* nearest = nullptr;
    double bestDistance = tolerance;
    for (const Ridge& ridge : ridges[index]) {
      const double distance = std::abs(ridge.crossPx - prediction);
      if (distance <= bestDistance) {
        bestDistance = distance;
        nearest = &ridge;
      }
    }
    return nearest;
  };

  // Bounded deterministic consensus search: at most 12 anchors and 192
  // scoring columns. Runtime cannot grow quadratically with camera width.
  QVector<int> anchors;
  const int anchorCount = std::min(12, occupied.size());
  for (int i = 0; i < anchorCount; ++i) {
    anchors.append(occupied[i * (occupied.size() - 1) /
                            std::max(1, anchorCount - 1)]);
  }
  constexpr int kMaximumScoringColumns = 192;
  const int scoreStep = std::max(
      1, (count + kMaximumScoringColumns - 1) / kMaximumScoringColumns);
  double bestScore = -1.0;
  double bestOffset = 0.0;
  double bestSlope = 0.0;
  for (int a = 0; a < anchors.size(); ++a) {
    for (int b = a + 1; b < anchors.size(); ++b) {
      const int first = anchors[a];
      const int last = anchors[b];
      if (last - first < std::max(8, count / 5)) continue;
      for (const Ridge& left : ridges[first]) {
        for (const Ridge& right : ridges[last]) {
          const double slope = (right.crossPx - left.crossPx) /
                               ((last - first) * sampleStep);
          // The horizontal and vertical searches must meet at 45 degrees;
          // a smaller limit leaves a range of orientations unsupported by
          // both axes even when the stripe is otherwise perfectly visible.
          if (std::abs(slope) > 1.0) continue;
          const double offset = left.crossPx - slope * first * sampleStep;
          int support = 0;
          std::array<int, kMaximumScoringColumns> supportColumns{};
          double residual = 0.0;
          for (int i = 0; i < count; i += scoreStep) {
            const double prediction = offset + slope * i * sampleStep;
            const Ridge* ridge = nearestRidge(i, prediction, fitTolerancePx);
            if (!ridge) continue;
            supportColumns[static_cast<std::size_t>(support)] = i;
            ++support;
            residual += std::abs(ridge->crossPx - prediction);
          }
          if (support < 6) continue;
          // Use a supported span, not the extreme bright pixels. Trimming
          // both outer 10% prevents a distant hot pixel from making a short
          // central reflection look like it spans the entire laser range.
          const int trim = std::max(1, support / 10);
          const double spanRatio = static_cast<double>(
              supportColumns[static_cast<std::size_t>(support - trim - 1)] -
              supportColumns[static_cast<std::size_t>(trim)]) /
              std::max(1, count - 1);
          // The two exterior parent-stripe sections may together be shorter
          // than the elevated return inside a wide weld. Their broad spatial
          // coverage must still beat that bright, concentrated central band.
          // Keep a small local-support term for genuinely clipped one-sided
          // views; no brightness weight is used to select the stripe.
          const double score =
              support * (0.15 + 0.85 * spanRatio * spanRatio) -
                               0.08 * residual / fitTolerancePx;
          if (score > bestScore) {
            bestScore = score;
            bestOffset = offset;
            bestSlope = slope;
          }
        }
      }
    }
  }
  if (bestScore < 0.0) return output;

  for (int iteration = 0; iteration < 2; ++iteration) {
    double n = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (int i = 0; i < count; ++i) {
      const double x = i * sampleStep;
      const Ridge* ridge = nearestRidge(
          i, bestOffset + bestSlope * x, fitTolerancePx);
      if (!ridge) continue;
      n += 1.0;
      sumX += x;
      sumY += ridge->crossPx;
      sumXX += x * x;
      sumXY += x * ridge->crossPx;
    }
    const double denominator = n * sumXX - sumX * sumX;
    if (n < 2.0 || denominator <= 1e-6) return output;
    bestSlope = (n * sumXY - sumX * sumY) / denominator;
    bestOffset = (sumY - bestSlope * sumX) / n;
  }

  QVector<int> widths;
  QVector<int> residuals;
  int firstSupport = -1;
  int lastSupport = -1;
  for (int i = 0; i < count; ++i) {
    const double prediction = bestOffset + bestSlope * i * sampleStep;
    const Ridge* ridge = nearestRidge(i, prediction, fitTolerancePx);
    if (!ridge) continue;
    if (firstSupport < 0) firstSupport = i;
    lastSupport = i;
    widths.append(static_cast<int>(std::lround(ridge->halfWidthPx * 2.0)));
    residuals.append(static_cast<int>(std::lround(
        std::abs(ridge->crossPx - prediction))));
    output.ridgeCrossPx[i] = static_cast<int>(std::lround(ridge->crossPx));
  }
  // Preserve the attempted fit for diagnosis even if it lacks sufficient
  // support. The validity flag remains the sole authorization to use it as
  // a measurement or to generate a gap from its projected profile.
  output.offsetPx = bestOffset;
  output.slope = bestSlope;
  output.supportRatio = static_cast<double>(widths.size()) / count;
  output.residualPx = vectorPercentile(residuals, 0.90);
  output.halfWidthPx = std::clamp(
      vectorPercentile(widths, 0.50) * 0.5 + output.residualPx + sampleStep,
      3.0, std::max(3.0, crossLength * 0.035));
  if (widths.size() < std::max(12, count / 6) ||
      lastSupport - firstSupport < count / 4) {
    return output;
  }
  output.valid = true;
  for (int i = 0; i < count; ++i) {
    const int axis = std::min(axisLength - 1, i * sampleStep);
    const double prediction = bestOffset + bestSlope * axis;
    const int start = std::max(0, static_cast<int>(std::ceil(
        (prediction - output.halfWidthPx) / sampleStep)) * sampleStep);
    const int end = std::min(crossLength - 1,
                            static_cast<int>(std::floor(
                                prediction + output.halfWidthPx)));
    std::array<int, 3> top{{backgroundLevel, backgroundLevel, backgroundLevel}};
    for (int cross = start; cross <= end; cross += sampleStep) {
      keepTopSamples(&top, pixel(axis, cross));
    }
    output.profile[i] = (top[0] + top[1] + top[2]) / 3;
  }
  detectDisplacedContour(grayscale, sampleStep, backgroundLevel, horizontal,
                         config, &output);
  return output;
}

QVector<int> medianSmooth(const QVector<int>& values) {
  QVector<int> smoothed(values.size());
  for (int i = 0; i < values.size(); ++i) {
    std::array<int, 5> window{};
    int count = 0;
    for (int offset = -2; offset <= 2; ++offset) {
      const int index = i + offset;
      if (index >= 0 && index < values.size()) window[count++] = values[index];
    }
    std::sort(window.begin(), window.begin() + count);
    smoothed[i] = window[count / 2];
  }
  return smoothed;
}

void closeSmallHoles(QVector<bool>* present, int maximumHole) {
  int index = 0;
  while (index < present->size()) {
    if (present->at(index)) {
      ++index;
      continue;
    }
    const int start = index;
    while (index < present->size() && !present->at(index)) ++index;
    const int end = index - 1;
    if (start > 0 && index < present->size() &&
        end - start + 1 <= maximumHole) {
      for (int i = start; i <= end; ++i) (*present)[i] = true;
    }
  }
}

void removeShortRuns(QVector<bool>* present, int minimumRun) {
  int index = 0;
  while (index < present->size()) {
    if (!present->at(index)) {
      ++index;
      continue;
    }
    const int start = index;
    while (index < present->size() && present->at(index)) ++index;
    if (index - start < minimumRun) {
      for (int i = start; i < index; ++i) (*present)[i] = false;
    }
  }
}

QVector<Run> collectRuns(const QVector<bool>& present) {
  QVector<Run> runs;
  int index = 0;
  while (index < present.size()) {
    if (!present[index]) {
      ++index;
      continue;
    }
    const int start = index;
    while (index < present.size() && present[index]) ++index;
    runs.append({start, index - 1});
  }
  return runs;
}

LaserGapDetection inferFromVisibleEdge(
    const QVector<Run>& runs, int minimumGap, int sampleStep,
    int axisPixelLength, int contrast, bool horizontal,
    const LaserGapDetectorConfig& config) {
  LaserGapDetection result;
  result.horizontal = horizontal;
  const bool hasExpectedAbsoluteCenter =
      config.expectedAbsoluteCenterRatio >= 0.0 &&
      config.expectedAbsoluteCenterRatio <= 1.0;
  const bool trackingActive =
      (config.expectedCenterRatio >= 0.0 && config.expectedCenterRatio <= 1.0) ||
      hasExpectedAbsoluteCenter;
  const bool lineRangeKnown = config.expectedLineStartRatio >= 0.0 &&
                              config.expectedLineStartRatio < 1.0 &&
                              config.expectedLineEndRatio > 0.0 &&
                              config.expectedLineEndRatio <= 1.0 &&
                              config.expectedLineStartRatio <
                                  config.expectedLineEndRatio;
  if (!config.allowEdgeBreakFallback || !trackingActive ||
      !lineRangeKnown || runs.isEmpty()) {
    return result;
  }

  const double axisLastPixel = std::max(1, axisPixelLength - 1);
  const int maximumSampleIndex =
      std::max(1, (axisPixelLength - 1) / sampleStep);
  int lineStartSample = static_cast<int>(std::lround(
      config.expectedLineStartRatio * axisLastPixel / sampleStep));
  int lineEndSample = static_cast<int>(std::floor(
      config.expectedLineEndRatio * axisLastPixel / sampleStep));
  lineStartSample = std::clamp(lineStartSample, 0, maximumSampleIndex - 1);
  lineEndSample = std::clamp(lineEndSample, lineStartSample + 1,
                             maximumSampleIndex);
  const int expectedLineSpan = lineEndSample - lineStartSample;
  if (expectedLineSpan < 12) return result;

  const bool hasExpectedAbsoluteWidth =
      config.expectedAbsoluteGapWidthRatio > 0.0 &&
      config.expectedAbsoluteGapWidthRatio <= 1.0;
  // expectedLineSpan and gapWidth are measured in sampled columns, whereas
  // the absolute-width field is a ratio of the full raw-image pixel axis.
  // Keep both branches in sample units before clamping; dividing the legacy
  // local-width branch by sampleStep would halve its inferred physical gap.
  const double expectedWidthSamples = hasExpectedAbsoluteWidth
      ? config.expectedAbsoluteGapWidthRatio * axisLastPixel / sampleStep
      : (config.expectedGapWidthRatio > 0.0
             ? config.expectedGapWidthRatio * expectedLineSpan
             : std::max(config.minimumGapRatio, config.edgeBreakGapRatio) *
                   expectedLineSpan);
  const int gapWidth = std::clamp(
      static_cast<int>(std::lround(expectedWidthSamples)),
      minimumGap, std::max(minimumGap, expectedLineSpan / 2));
  // Use the full-image center whenever available. The local center is tied to
  // the visible line endpoints and moves spuriously when one optical end is
  // clipped. Full-image continuity lets the last visible run remain a valid
  // observation of a gap at that boundary.
  const double expectedCenter = hasExpectedAbsoluteCenter
      ? config.expectedAbsoluteCenterRatio * axisLastPixel / sampleStep
      : lineStartSample + config.expectedCenterRatio * expectedLineSpan;
  const double expectedGapStart = expectedCenter - gapWidth * 0.5;
  const double expectedGapEnd = expectedGapStart + gapWidth - 1;
  const int minimumVisibleRun = std::max(
      config.allowEdgeBreakFallback ? std::max(2, config.minimumRunLength / 2)
                                    : std::max(1, config.minimumRunLength),
      static_cast<int>(std::ceil(
          std::max(0.012, config.minimumEdgeBreakRunRatio) *
          expectedLineSpan)));
  const int edgeTolerance = std::max(
      minimumGap * 2,
      static_cast<int>(std::ceil(
          std::max(0.06, config.maximumTrackingCenterJumpRatio) *
          expectedLineSpan)));

  const Run* bestRun = nullptr;
  bool visibleLeftEdge = false;
  double bestScore = -std::numeric_limits<double>::infinity();
  for (const Run& run : runs) {
    if (run.length() < minimumVisibleRun) continue;
    const int observedLeftGapStart = run.end + 1;
    const int observedRightGapEnd = run.start - 1;
    if (run.end < expectedCenter &&
        std::abs(observedLeftGapStart - expectedGapStart) <= edgeTolerance) {
      const double score = run.length() -
                           2.0 * std::abs(observedLeftGapStart -
                                          expectedGapStart);
      if (score > bestScore) {
        bestScore = score;
        bestRun = &run;
        visibleLeftEdge = true;
      }
    }
    if (run.start > expectedCenter &&
        std::abs(observedRightGapEnd - expectedGapEnd) <= edgeTolerance) {
      const double score = run.length() -
                           2.0 * std::abs(observedRightGapEnd -
                                          expectedGapEnd);
      if (score > bestScore) {
        bestScore = score;
        bestRun = &run;
        visibleLeftEdge = false;
      }
    }
  }
  if (!bestRun) return result;

  // Trust a visible broken edge enough to follow its direction, but blend it
  // with the previous geometry so one damaged frame cannot create a large
  // lateral jump. The opposite edge is inferred from the tracked gap width.
  constexpr double kObservedEdgeWeight = 0.35;
  int gapStart = 0;
  if (visibleLeftEdge) {
    gapStart = static_cast<int>(std::lround(
        (1.0 - kObservedEdgeWeight) * expectedGapStart +
        kObservedEdgeWeight * (bestRun->end + 1)));
  } else {
    const int gapEnd = static_cast<int>(std::lround(
        (1.0 - kObservedEdgeWeight) * expectedGapEnd +
        kObservedEdgeWeight * (bestRun->start - 1)));
    gapStart = gapEnd - gapWidth + 1;
  }
  gapStart = std::clamp(gapStart, lineStartSample,
                        std::max(lineStartSample,
                                 lineEndSample - gapWidth + 1));
  const int gapEnd = std::min(lineEndSample, gapStart + gapWidth - 1);
  const double gapCenter = (gapStart + gapEnd) * 0.5;
  const double absoluteCenterRatio = std::clamp(
      gapCenter * sampleStep / axisLastPixel, 0.0, 1.0);
  const double normalizedFallbackCenter =
      (gapCenter - lineStartSample) / std::max(1, expectedLineSpan);
  // Do not relax the raw-image continuity limit at a boundary. The visible
  // edge fallback is specifically intended to keep a clipped gap trackable;
  // allowing an 18% jump here lets a random bright run become the new gap.
  const double absoluteJumpLimit =
      std::max(0.02, config.maximumAbsoluteCenterJumpRatio);
  if (config.expectedAbsoluteCenterRatio >= 0.0 &&
      config.expectedAbsoluteCenterRatio <= 1.0 &&
      std::abs(absoluteCenterRatio -
               config.expectedAbsoluteCenterRatio) >
          absoluteJumpLimit) {
    result.continuityRejected = true;
    return result;
  }
  if (config.referenceAbsoluteCenterRatio >= 0.0 &&
      config.referenceAbsoluteCenterRatio <= 1.0 &&
      std::abs(absoluteCenterRatio -
               config.referenceAbsoluteCenterRatio) >
          std::max(0.08, config.maximumReferenceCenterDriftRatio)) {
    result.continuityRejected = true;
    return result;
  }

  result.valid = true;
  result.edgeBreakFallback = true;
  result.normalizedCenter = std::clamp(
      normalizedFallbackCenter, 0.0, 1.0);
  result.absoluteCenterRatio = absoluteCenterRatio;
  result.gapStartPx = gapStart * sampleStep;
  result.gapEndPx = std::min((gapEnd + 1) * sampleStep - 1,
                             axisPixelLength - 1);
  result.lineStartPx = lineStartSample * sampleStep;
  result.lineEndPx = std::min((lineEndSample + 1) * sampleStep - 1,
                              axisPixelLength - 1);
  result.supportingSamples = bestRun->length();
  const double contrastScore = std::clamp(contrast / 80.0, 0.0, 1.0);
  const double supportScore = std::clamp(
      static_cast<double>(bestRun->length()) /
          std::max(1.0, expectedLineSpan * 0.25),
      0.0, 1.0);
  // A visible edge is useful for continuity, but it is not equivalent to a
  // measured two-sided gap. Keep its confidence below the normal-gap control
  // threshold so the controller cannot give an inferred edge full authority.
  constexpr double kMaximumEdgeBreakConfidence = 0.045;
  result.confidence = std::min(kMaximumEdgeBreakConfidence,
                               0.35 * contrastScore * supportScore);
  return result;
}

LaserGapDetection detectAlongAxis(const QVector<int>& rawProfile, int sampleStep,
                                  int axisPixelLength, int backgroundLevel, bool horizontal,
                                  const LaserGapDetectorConfig& config,
                                  LaserRawFrameDiagnostic* diagnostic) {
  LaserGapDetection result;
  result.horizontal = horizontal;
  if (diagnostic) {
    diagnostic->available = !rawProfile.isEmpty();
    diagnostic->horizontal = horizontal;
    diagnostic->sampleStepPx = sampleStep;
    diagnostic->backgroundLevel = backgroundLevel;
    diagnostic->rawProfile = rawProfile;
  }
  if ((config.expectedAxis == 1 && !horizontal) ||
      (config.expectedAxis == 2 && horizontal)) {
    return result;
  }
  if (rawProfile.size() < 24) return result;

  const QVector<int> profile = medianSmooth(rawProfile);
  const bool trackedEdgeMayBePartial =
      config.allowEdgeBreakFallback && config.expectedCenterRatio >= 0.0 &&
      config.expectedLineStartRatio >= 0.0 &&
      config.expectedLineEndRatio > config.expectedLineStartRatio;
  const int lineLevel =
      vectorPercentile(profile, trackedEdgeMayBePartial ? 0.94 : 0.85);
  const int contrast = lineLevel - backgroundLevel;
  if (diagnostic) {
    diagnostic->filteredProfile = profile;
    diagnostic->lineLevel = lineLevel;
  }
  if (contrast < config.minimumContrast) return result;

  // This profile has already excluded off-stripe returns geometrically.
  // Using 55% of the brighter stripe sections here wrongly turns dim but
  // visible portions of the same stripe into weld gaps. Keep the weaker
  // stripe signal; median/run filtering still removes isolated speckle.
  const int threshold = backgroundLevel +
                        static_cast<int>(std::lround(contrast * 0.30));
  if (diagnostic) diagnostic->threshold = threshold;
  QVector<bool> present(profile.size(), false);
  for (int i = 0; i < profile.size(); ++i) present[i] = profile[i] >= threshold;
  const int adaptiveHoleLength = static_cast<int>(std::ceil(
      std::max(0.0, config.smallGapFillRatio) * profile.size()));
  const int maximumHoleLength =
      std::max(std::max(0, config.maximumHoleLength), adaptiveHoleLength);
  closeSmallHoles(&present, maximumHoleLength);
  const bool trackingActive = config.expectedCenterRatio >= 0.0 &&
                              config.expectedCenterRatio <= 1.0;
  const int minimumRun = trackingActive && config.allowEdgeBreakFallback
                             ? std::max(2, config.minimumRunLength / 2)
                             : std::max(1, config.minimumRunLength);
  removeShortRuns(&present, minimumRun);
  // Removing a bright speck can join adjacent dark noise into one hole. Run
  // the closing pass again so that combined hole is still rejected when its
  // total width remains below the adaptive noise scale.
  closeSmallHoles(&present, maximumHoleLength);
  if (diagnostic) {
    diagnostic->maximumHoleSamples = maximumHoleLength;
    diagnostic->minimumRunSamples = minimumRun;
    diagnostic->presentProfile = present;
  }

  const QVector<Run> runs = collectRuns(present);
  const int minimumGap = std::max(
      3, static_cast<int>(std::ceil(config.minimumGapRatio * profile.size())));
  if (runs.size() < 2) {
    return inferFromVisibleEdge(runs, minimumGap, sampleStep,
                                axisPixelLength, contrast, horizontal, config);
  }

  int totalSupport = 0;
  for (const Run& run : runs) totalSupport += run.length();
  const int minimumSideSupport = std::max(
      6, static_cast<int>(std::ceil(config.minimumSideSupportRatio * profile.size())));
  const int lineStartSample = runs.first().start;
  const int lineEndSample = runs.last().end;
  const int lineSpan = std::max(1, lineEndSample - lineStartSample);
  const bool hasExpectedAbsoluteWidth =
      config.expectedAbsoluteGapWidthRatio > 0.0 &&
      config.expectedAbsoluteGapWidthRatio <= 1.0;

  // Find the dominant physical interruption before applying temporal
  // preferences. Small residual holes may survive preprocessing, but they
  // must not capture the tracker merely because their side support is more
  // balanced or they are closer to the previous center.
  int dominantGapLength = 0;
  int supportedLeft = 0;
  for (int i = 0; i + 1 < runs.size(); ++i) {
    supportedLeft += runs[i].length();
    const int supportedRight = totalSupport - supportedLeft;
    const int gapStart = runs[i].end + 1;
    const int gapEnd = runs[i + 1].start - 1;
    const int gapLength = gapEnd - gapStart + 1;
    if (gapLength < minimumGap) continue;
    const double candidateCenter =
        ((gapStart + gapEnd) * 0.5 - lineStartSample) / lineSpan;
    const bool nearLineBoundary = candidateCenter < 0.18 ||
                                  candidateCenter > 0.82;
    const int candidateMinimumSideSupport = nearLineBoundary && trackingActive
                                                ? std::max(
                                                      3, static_cast<int>(
                                                             std::ceil(0.012 *
                                                                       profile.size())))
                                                : minimumSideSupport;
    if (supportedLeft >= candidateMinimumSideSupport &&
        supportedRight >= candidateMinimumSideSupport) {
      dominantGapLength = std::max(dominantGapLength, gapLength);
    }
  }
  const int dominantGapThreshold = std::max(
      minimumGap,
      static_cast<int>(std::ceil(
          dominantGapLength *
          std::clamp(config.dominantGapMinimumRatio, 0.0, 1.0))));

  int leftSupport = 0;
  double bestScore = -std::numeric_limits<double>::infinity();
  int bestGapStart = -1;
  int bestGapEnd = -1;
  int bestLeftSupport = 0;
  int bestRightSupport = 0;
  bool trackingCandidateSeen = false;
  bool trackingCandidateAccepted = false;
  for (int i = 0; i + 1 < runs.size(); ++i) {
    leftSupport += runs[i].length();
    const int rightSupport = totalSupport - leftSupport;
    const int gapStart = runs[i].end + 1;
    const int gapEnd = runs[i + 1].start - 1;
    const int gapLength = gapEnd - gapStart + 1;
    if (gapLength < dominantGapThreshold) {
      continue;
    }
    const double candidateCenter =
        ((gapStart + gapEnd) * 0.5 - lineStartSample) / lineSpan;
    const double candidateAbsoluteCenter = std::clamp(
        (gapStart + gapEnd) * 0.5 * sampleStep /
            std::max(1, axisPixelLength - 1),
        0.0, 1.0);
    const double candidateWidth = static_cast<double>(gapLength) /
                                  lineSpan;
    const bool nearLineBoundary = candidateCenter < 0.18 ||
                                   candidateCenter > 0.82;
    const int candidateMinimumSideSupport = nearLineBoundary && trackingActive
                                                ? std::max(
                                                      3, static_cast<int>(
                                                             std::ceil(0.012 *
                                                                       profile.size())))
                                                : minimumSideSupport;
    if (leftSupport < candidateMinimumSideSupport ||
        rightSupport < candidateMinimumSideSupport) {
      continue;
    }
    bool localCenterJump = false;
    bool absoluteCenterJump = false;
    bool hasAbsoluteReference = false;
    if (trackingActive) {
      trackingCandidateSeen = true;
      hasAbsoluteReference = config.expectedAbsoluteCenterRatio >= 0.0 &&
                             config.expectedAbsoluteCenterRatio <= 1.0;
      localCenterJump =
          std::abs(candidateCenter - config.expectedCenterRatio) >
          std::max(0.05, config.maximumTrackingCenterJumpRatio);
      absoluteCenterJump =
          hasAbsoluteReference &&
          std::abs(candidateAbsoluteCenter -
                   config.expectedAbsoluteCenterRatio) >
              std::max(0.02, config.maximumAbsoluteCenterJumpRatio);
      // The visible laser span can change when one end is clipped. In that
      // case the local normalized center may jump even though the physical
      // full-image center is continuous. Only reject a local jump when the
      // stable absolute coordinate also moved (or is unavailable).
      if (localCenterJump &&
          (!hasAbsoluteReference || absoluteCenterJump)) {
        continue;
      }
    }
    if (config.expectedAbsoluteCenterRatio >= 0.0 &&
        config.expectedAbsoluteCenterRatio <= 1.0 &&
        std::abs(candidateAbsoluteCenter -
                 config.expectedAbsoluteCenterRatio) >
        std::max(0.02, config.maximumAbsoluteCenterJumpRatio)) {
      trackingCandidateSeen = true;
      continue;
    }
    if (config.referenceAbsoluteCenterRatio >= 0.0 &&
        config.referenceAbsoluteCenterRatio <= 1.0 &&
        std::abs(candidateAbsoluteCenter -
                 config.referenceAbsoluteCenterRatio) >
            std::max(0.08, config.maximumReferenceCenterDriftRatio)) {
      trackingCandidateSeen = true;
      continue;
    }
    const double candidateWidthForTracking = hasExpectedAbsoluteWidth
        ? gapLength * sampleStep /
              static_cast<double>(std::max(1, axisPixelLength - 1))
        : candidateWidth;
    const double expectedWidthForTracking = hasExpectedAbsoluteWidth
        ? config.expectedAbsoluteGapWidthRatio
        : config.expectedGapWidthRatio;
    if (trackingActive && expectedWidthForTracking > 0.0 &&
        std::abs(candidateWidthForTracking - expectedWidthForTracking) >
            std::max(config.maximumTrackingGapWidthJumpRatio,
                     expectedWidthForTracking * 0.55)) {
      result.widthRejected = true;
      trackingCandidateSeen = true;
      continue;
    }
    if (trackingActive) trackingCandidateAccepted = true;
    const double balance = static_cast<double>(std::min(leftSupport, rightSupport)) /
                           std::max(leftSupport, rightSupport);
    double score = gapLength * std::sqrt(
        static_cast<double>(leftSupport) * rightSupport) * (0.75 + 0.25 * balance);
    if (trackingActive && expectedWidthForTracking >= 0.0) {
      const double widthError = std::abs(candidateWidthForTracking -
                                         expectedWidthForTracking);
      score *= 1.0 / (1.0 + std::max(0.0, config.trackingWidthWeight) * widthError * 20.0);
    }
    if (trackingActive && config.expectedAbsoluteCenterRatio >= 0.0 &&
        config.expectedAbsoluteCenterRatio <= 1.0) {
      const double centerError = std::abs(
          candidateAbsoluteCenter - config.expectedAbsoluteCenterRatio);
      score *= 1.0 /
               (1.0 + std::max(0.0, config.trackingAbsoluteCenterWeight) *
                            centerError * 20.0);
      // A local-ratio jump can be caused solely by clipped line endpoints.
      // Keep that candidate eligible because its absolute coordinate is
      // stable, but prefer a candidate that is also locally continuous when
      // both are otherwise plausible.
      if (localCenterJump && !absoluteCenterJump) score *= 0.65;
    }
    if (score > bestScore) {
      bestScore = score;
      bestGapStart = gapStart;
      bestGapEnd = gapEnd;
      bestLeftSupport = leftSupport;
      bestRightSupport = rightSupport;
    }
  }
  if (bestGapStart < 0) {
    LaserGapDetection fallback = inferFromVisibleEdge(
        runs, minimumGap, sampleStep, axisPixelLength, contrast, horizontal,
        config);
    if (fallback.valid) {
      fallback.widthRejected = result.widthRejected;
      return fallback;
    }
    result.continuityRejected = trackingActive && trackingCandidateSeen &&
                                !trackingCandidateAccepted;
    return result;
  }

  const double gapCenterSample = (bestGapStart + bestGapEnd) * 0.5;
  if (lineSpan <= 0) return result;

  result.valid = true;
  result.normalizedCenter = std::clamp(
      (gapCenterSample - lineStartSample) / lineSpan, 0.0, 1.0);
  result.absoluteCenterRatio = std::clamp(
      gapCenterSample * sampleStep /
          std::max(1, axisPixelLength - 1),
      0.0, 1.0);
  result.gapStartPx = bestGapStart * sampleStep;
  result.gapEndPx = std::min((bestGapEnd + 1) * sampleStep - 1,
                             axisPixelLength - 1);
  result.lineStartPx = lineStartSample * sampleStep;
  result.lineEndPx = std::min((lineEndSample + 1) * sampleStep - 1,
                              axisPixelLength - 1);
  result.supportingSamples = bestLeftSupport + bestRightSupport;

  const double contrastScore = std::clamp(contrast / 80.0, 0.0, 1.0);
  const double gapScore = std::clamp(
      static_cast<double>(bestGapEnd - bestGapStart + 1) /
          std::max(1, minimumGap * 4),
      0.0, 1.0);
  const double supportScore = std::clamp(
      static_cast<double>(std::min(bestLeftSupport, bestRightSupport)) /
          std::max(1, minimumSideSupport * 4),
      0.0, 1.0);
  const double balance = static_cast<double>(std::min(bestLeftSupport, bestRightSupport)) /
                         std::max(bestLeftSupport, bestRightSupport);
  result.confidence = contrastScore * gapScore * supportScore * std::sqrt(balance);
  return result;
}

}  // namespace

LaserGapDetection LaserGapDetector::detect(const QImage& image,
                                           const LaserGapDetectorConfig& config,
                                           LaserRawFrameDiagnostic* diagnostic) {
  if (diagnostic) *diagnostic = {};
  if (image.isNull() || image.width() < 48 || image.height() < 24) return {};

  const QImage grayscale = image.format() == QImage::Format_Grayscale8
                               ? image
                               : image.convertToFormat(QImage::Format_Grayscale8);
  constexpr int kSampleStep = 2;
  const int columns = (grayscale.width() + kSampleStep - 1) / kSampleStep;
  const int rows = (grayscale.height() + kSampleStep - 1) / kSampleStep;
  QVector<int> horizontalProfile(columns, 0);
  QVector<int> verticalProfile(rows, 0);
  QVector<std::array<int, 3>> horizontalTop(columns);
  QVector<std::array<int, 3>> verticalTop(rows);
  for (auto& samples : horizontalTop) samples.fill(0);
  for (auto& samples : verticalTop) samples.fill(0);
  std::array<int, 256> histogram{};
  int sampleCount = 0;

  for (int rowIndex = 0, y = 0; y < grayscale.height();
       ++rowIndex, y += kSampleStep) {
    const uchar* row = grayscale.constScanLine(y);
    for (int columnIndex = 0, x = 0; x < grayscale.width();
         ++columnIndex, x += kSampleStep) {
      const int value = row[x];
      keepTopSamples(&horizontalTop[columnIndex], value);
      keepTopSamples(&verticalTop[rowIndex], value);
      ++histogram[static_cast<std::size_t>(value)];
      ++sampleCount;
    }
  }

  // A maximum projection lets one hot pixel create a fake laser run. The
  // laser line spans several samples in its thickness, so averaging the top
  // three values keeps the line while suppressing isolated bright noise.
  for (int i = 0; i < horizontalTop.size(); ++i) {
    horizontalProfile[i] = (horizontalTop[i][0] + horizontalTop[i][1] +
                            horizontalTop[i][2]) /
                           3;
  }
  for (int i = 0; i < verticalTop.size(); ++i) {
    verticalProfile[i] =
        (verticalTop[i][0] + verticalTop[i][1] + verticalTop[i][2]) / 3;
  }

  const int backgroundLevel = histogramPercentile(histogram, sampleCount, 0.90);
  LaserRawFrameDiagnostic horizontalDiagnostic;
  LaserRawFrameDiagnostic verticalDiagnostic;
  const auto detectBaseline = [&](const QVector<int>& fullProfile,
                                  bool horizontal,
                                  LaserRawFrameDiagnostic* frame) {
    LaserGapDetection result;
    result.horizontal = horizontal;
    frame->horizontal = horizontal;
    frame->available = true;
    frame->sampleStepPx = kSampleStep;
    frame->backgroundLevel = backgroundLevel;
    frame->fullProjectionProfile = fullProfile;
    if ((config.expectedAxis == 1 && !horizontal) ||
        (config.expectedAxis == 2 && horizontal)) {
      return result;
    }
    const BaselineProjection baseline = projectLaserBaseline(
        grayscale, fullProfile, kSampleStep, backgroundLevel, horizontal, config);
    frame->baselineUsed = baseline.valid;
    frame->baselineOffsetPx = baseline.offsetPx;
    frame->baselineSlope = baseline.slope;
    frame->baselineHalfWidthPx = baseline.halfWidthPx;
    frame->baselineSupportRatio = baseline.supportRatio;
    frame->baselineResidualPx = baseline.residualPx;
    frame->ridgeCrossPx = baseline.ridgeCrossPx;
    frame->contourProfile = baseline.contourProfile;
    frame->contourDisplacementThresholdPx = baseline.contourDisplacementThresholdPx;
    frame->contourMinimumRunSamples = baseline.contourMinimumRunSamples;
    frame->rawProfile = baseline.profile;
    // An untrusted stripe must remain unobserved. Falling back to the
    // whole-height projection would reintroduce reflection-created gaps.
    if (!baseline.valid) return result;
    result = detectAlongAxis(baseline.profile, kSampleStep,
                             horizontal ? grayscale.width() : grayscale.height(),
                             backgroundLevel, horizontal, config, frame);
    if (baseline.contourStartSample >= 0 && baseline.contourEndSample >= 0) {
      const int axisLength = horizontal ? grayscale.width() : grayscale.height();
      result.contourSupported = true;
      result.contourStartPx = baseline.contourStartSample * kSampleStep;
      result.contourEndPx = std::min(axisLength - 1,
          (baseline.contourEndSample + 1) * kSampleStep - 1);
      result.contourConfidence = baseline.contourConfidence;
      bool contourPreferredByTrack = false;
      if (result.valid && !result.edgeBreakFallback) {
        const double gapCenter = (result.gapStartPx + result.gapEndPx) * 0.5;
        const double contourCenter =
            (result.contourStartPx + result.contourEndPx) * 0.5;
        const double gapWidth = result.gapEndPx - result.gapStartPx + 1;
        const double contourWidth = result.contourEndPx - result.contourStartPx + 1;
        result.contourAgreesWithGap =
            std::abs(gapCenter - contourCenter) <=
                std::max(6.0, std::max(gapWidth, contourWidth) * 0.15) &&
            std::min(gapWidth, contourWidth) >=
                std::max(gapWidth, contourWidth) * 0.50;
        result.contourConflict = !result.contourAgreesWithGap;
        const bool hasTrackedIdentity = config.expectedAbsoluteCenterRatio >= 0.0 ||
                                        config.expectedCenterRatio >= 0.0;
        result.contourDominant = !hasTrackedIdentity &&
            baseline.contourConfidence >= 0.060 &&
            gapWidth < contourWidth * std::clamp(config.dominantGapMinimumRatio, 0.2, 0.8);
        if (result.contourConflict &&
            (config.expectedAbsoluteGapWidthRatio > 0.0 ||
             (config.expectedGapWidthRatio > 0.0 &&
              config.expectedLineStartRatio >= 0.0 &&
              config.expectedLineEndRatio > config.expectedLineStartRatio)) &&
            config.expectedAbsoluteCenterRatio >= 0.0 &&
            config.expectedAbsoluteCenterRatio <= 1.0) {
          const double expectedWidth = config.expectedAbsoluteGapWidthRatio > 0.0
              ? config.expectedAbsoluteGapWidthRatio *
                    std::max(1, axisLength - 1)
              : config.expectedGapWidthRatio *
                    (config.expectedLineEndRatio -
                     config.expectedLineStartRatio) *
                    std::max(1, axisLength - 1);
          const double expectedCenter = config.expectedAbsoluteCenterRatio *
              std::max(1, axisLength - 1);
          const double gapWidthError =
              std::abs(gapWidth - expectedWidth) / std::max(1.0, expectedWidth);
          const double contourWidthError =
              std::abs(contourWidth - expectedWidth) / std::max(1.0, expectedWidth);
          // Candidate association may prefer a raised contour over a small
          // surviving dark hole, but only using an established seam identity.
          // A contour is deliberately capped at 0.075 confidence.  Therefore
          // it must never replace an ordinary two-edge observation merely
          // because its expected width is a better numerical match: require
          // the raw observation to be genuinely weak and the contour to have
          // strong geometric support.  The absolute-center/width gates above
          // retain continuity with the confirmed seam identity. The result
          // remains an auxiliary observation with reduced control authority.
          constexpr double kMaximumRawConfidenceForContourOverride = 0.10;
          constexpr double kMinimumContourConfidenceForOverride = 0.060;
          const bool rawObservationIsWeak =
              result.confidence < kMaximumRawConfidenceForContourOverride;
          const bool contourObservationIsStrong =
              baseline.contourConfidence >= kMinimumContourConfidenceForOverride;
          contourPreferredByTrack =
              rawObservationIsWeak && contourObservationIsStrong &&
              gapWidthError > 0.45 &&
              contourWidthError + 0.25 < gapWidthError &&
              std::abs(contourCenter - expectedCenter) <=
                  std::abs(gapCenter - expectedCenter) + axisLength * 0.01;
        }
      }
      // A trusted two-sided intensity gap remains the primary observation.
      // Use the same-image contour when that observation is missing, is a
      // single inferred edge, or is weak and disagrees with the known seam
      // identity. No stale center is manufactured: both ends below are
      // measured from the current raw image.
      if (!result.valid || result.edgeBreakFallback || contourPreferredByTrack ||
          result.contourDominant) {
        result.valid = true;
        result.contourFallback = true;
        result.edgeBreakFallback = false;
        result.continuityRejected = false;
        result.widthRejected = false;
        result.gapStartPx = result.contourStartPx;
        result.gapEndPx = result.contourEndPx;
        result.lineStartPx = baseline.contourLineStartSample * kSampleStep;
        result.lineEndPx = std::min(axisLength - 1,
            (baseline.contourLineEndSample + 1) * kSampleStep - 1);
        const double center = (result.gapStartPx + result.gapEndPx) * 0.5;
        result.absoluteCenterRatio = std::clamp(
            center / std::max(1, axisLength - 1), 0.0, 1.0);
        result.normalizedCenter = std::clamp(
            (center - result.lineStartPx) /
                std::max(1, result.lineEndPx - result.lineStartPx),
            0.0, 1.0);
        result.confidence = baseline.contourConfidence;
        result.supportingSamples = static_cast<int>(std::lround(
            baseline.supportRatio * baseline.profile.size()));
      }
    }
    result.baselineSupported = true;
    result.baselineOffsetPx = baseline.offsetPx;
    result.baselineSlope = baseline.slope;
    result.baselineHalfWidthPx = baseline.halfWidthPx;
    return result;
  };
  LaserGapDetection horizontal =
      detectBaseline(horizontalProfile, true, &horizontalDiagnostic);
  LaserGapDetection vertical =
      detectBaseline(verticalProfile, false, &verticalDiagnostic);

  const auto returnWithDiagnostic =
      [&](const LaserGapDetection& result,
          const LaserRawFrameDiagnostic& selectedDiagnostic) {
        if (diagnostic) {
          *diagnostic = selectedDiagnostic;
          diagnostic->imageWidth = grayscale.width();
          diagnostic->imageHeight = grayscale.height();
        }
        return result;
      };

  if (config.expectedAxis == 1) {
    return returnWithDiagnostic(horizontal, horizontalDiagnostic);
  }
  if (config.expectedAxis == 2) {
    return returnWithDiagnostic(vertical, verticalDiagnostic);
  }

  if (!horizontal.valid) {
    // When neither axis produces a gap, retain the profile with the stronger
    // supported laser stripe. Keep its result geometry and diagnostics on
    // the same axis, including a perfectly continuous no-gap stripe.
    const bool selectVertical =
        vertical.valid || verticalDiagnostic.lineLevel >=
                              horizontalDiagnostic.lineLevel;
    LaserGapDetection selected = selectVertical ? vertical : horizontal;
    selected.widthRejected = vertical.widthRejected || horizontal.widthRejected;
    selected.continuityRejected =
        vertical.continuityRejected || horizontal.continuityRejected;
    return returnWithDiagnostic(selected, selectVertical ? verticalDiagnostic
                                                         : horizontalDiagnostic);
  }
  if (!vertical.valid) {
    horizontal.widthRejected = horizontal.widthRejected || vertical.widthRejected;
    horizontal.continuityRejected =
        horizontal.continuityRejected || vertical.continuityRejected;
    return returnWithDiagnostic(horizontal, horizontalDiagnostic);
  }
  const double horizontalSpan = static_cast<double>(
      horizontal.lineEndPx - horizontal.lineStartPx + 1) / grayscale.width();
  const double verticalSpan = static_cast<double>(
      vertical.lineEndPx - vertical.lineStartPx + 1) / grayscale.height();
  if (horizontal.confidence * horizontalSpan >=
      vertical.confidence * verticalSpan) {
    return returnWithDiagnostic(horizontal, horizontalDiagnostic);
  }
  return returnWithDiagnostic(vertical, verticalDiagnostic);
}

}  // namespace crawling
