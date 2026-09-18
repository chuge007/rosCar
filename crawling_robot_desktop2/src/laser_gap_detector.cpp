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
  const bool trackingActive = config.expectedCenterRatio >= 0.0 &&
                              config.expectedCenterRatio <= 1.0;
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

  const double widthRatio =
      config.expectedGapWidthRatio > 0.0
          ? config.expectedGapWidthRatio
          : std::max(config.minimumGapRatio, config.edgeBreakGapRatio);
  const int gapWidth = std::clamp(
      static_cast<int>(std::lround(widthRatio * expectedLineSpan)),
      minimumGap, std::max(minimumGap, expectedLineSpan / 2));
  const double expectedCenter =
      lineStartSample + config.expectedCenterRatio * expectedLineSpan;
  const double expectedGapStart = expectedCenter - gapWidth * 0.5;
  const double expectedGapEnd = expectedGapStart + gapWidth - 1;
  const int minimumVisibleRun = std::max(
      std::max(1, config.minimumRunLength),
      static_cast<int>(std::ceil(
          std::max(0.02, config.minimumEdgeBreakRunRatio) *
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
  gapStart = std::clamp(gapStart, lineStartSample + 1,
                        std::max(lineStartSample + 1,
                                 lineEndSample - gapWidth));
  const int gapEnd = gapStart + gapWidth - 1;
  const double gapCenter = (gapStart + gapEnd) * 0.5;
  const double absoluteCenterRatio = std::clamp(
      gapCenter * sampleStep / axisLastPixel, 0.0, 1.0);
  if (config.expectedAbsoluteCenterRatio >= 0.0 &&
      config.expectedAbsoluteCenterRatio <= 1.0 &&
      std::abs(absoluteCenterRatio -
               config.expectedAbsoluteCenterRatio) >
          std::max(0.02, config.maximumAbsoluteCenterJumpRatio)) {
    return result;
  }
  if (config.referenceAbsoluteCenterRatio >= 0.0 &&
      config.referenceAbsoluteCenterRatio <= 1.0 &&
      std::abs(absoluteCenterRatio -
               config.referenceAbsoluteCenterRatio) >
          std::max(0.08, config.maximumReferenceCenterDriftRatio)) {
    return result;
  }

  result.valid = true;
  result.edgeBreakFallback = true;
  result.normalizedCenter = std::clamp(
      (gapCenter - lineStartSample) / expectedLineSpan, 0.0, 1.0);
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
  result.confidence = 0.35 * contrastScore * supportScore;
  return result;
}

LaserGapDetection detectAlongAxis(const QVector<int>& rawProfile, int sampleStep,
                                  int axisPixelLength, int backgroundLevel, bool horizontal,
                                  const LaserGapDetectorConfig& config) {
  LaserGapDetection result;
  result.horizontal = horizontal;
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
  if (contrast < config.minimumContrast) return result;

  const int threshold = backgroundLevel +
                        static_cast<int>(std::lround(contrast * 0.55));
  QVector<bool> present(profile.size(), false);
  for (int i = 0; i < profile.size(); ++i) present[i] = profile[i] >= threshold;
  closeSmallHoles(&present, std::max(0, config.maximumHoleLength));
  removeShortRuns(&present, std::max(1, config.minimumRunLength));

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

  int leftSupport = 0;
  double bestScore = -std::numeric_limits<double>::infinity();
  int bestGapStart = -1;
  int bestGapEnd = -1;
  int bestLeftSupport = 0;
  int bestRightSupport = 0;
  const bool trackingActive = config.expectedCenterRatio >= 0.0 &&
                              config.expectedCenterRatio <= 1.0;
  bool trackingCandidateSeen = false;
  bool trackingCandidateAccepted = false;
  for (int i = 0; i + 1 < runs.size(); ++i) {
    leftSupport += runs[i].length();
    const int rightSupport = totalSupport - leftSupport;
    const int gapStart = runs[i].end + 1;
    const int gapEnd = runs[i + 1].start - 1;
    const int gapLength = gapEnd - gapStart + 1;
    if (gapLength < minimumGap || leftSupport < minimumSideSupport ||
        rightSupport < minimumSideSupport) {
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
    if (trackingActive) {
      trackingCandidateSeen = true;
      if (std::abs(candidateCenter - config.expectedCenterRatio) >
          std::max(0.05, config.maximumTrackingCenterJumpRatio)) {
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
    if (trackingActive) trackingCandidateAccepted = true;
    const double balance = static_cast<double>(std::min(leftSupport, rightSupport)) /
                           std::max(leftSupport, rightSupport);
    double score = gapLength * std::sqrt(
        static_cast<double>(leftSupport) * rightSupport) * (0.75 + 0.25 * balance);
    if (trackingActive && config.expectedGapWidthRatio >= 0.0) {
      const double widthError = std::abs(candidateWidth - config.expectedGapWidthRatio);
      score *= 1.0 / (1.0 + std::max(0.0, config.trackingWidthWeight) * widthError * 20.0);
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
    if (fallback.valid) return fallback;
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
                                           const LaserGapDetectorConfig& config) {
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
  LaserGapDetection horizontal = detectAlongAxis(
      horizontalProfile, kSampleStep, grayscale.width(), backgroundLevel, true, config);
  LaserGapDetection vertical = detectAlongAxis(
      verticalProfile, kSampleStep, grayscale.height(), backgroundLevel, false, config);

  if (config.expectedAxis == 1) return horizontal;
  if (config.expectedAxis == 2) return vertical;

  if (!horizontal.valid) return vertical;
  if (!vertical.valid) return horizontal;
  const double horizontalSpan = static_cast<double>(
      horizontal.lineEndPx - horizontal.lineStartPx + 1) / grayscale.width();
  const double verticalSpan = static_cast<double>(
      vertical.lineEndPx - vertical.lineStartPx + 1) / grayscale.height();
  return horizontal.confidence * horizontalSpan >= vertical.confidence * verticalSpan
             ? horizontal
             : vertical;
}

}  // namespace crawling
