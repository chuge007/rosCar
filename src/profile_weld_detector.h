#pragma once

#include "laser_gap_detector.h"
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace crawling {
// Input is ONE SDK scan, with invalid slots retained. X/Z are SDK coordinates;
// output *Px fields are original scan indices for the existing control API.
// No rasterization, preview sampling, dark-gap requirement or flat-top fit.
class ProfileWeldDetector final {
 public:
  static ProfileWeldTemplate captureTemplate(
      const QVector<QVector3D>& points, const LaserGapDetection& detection,
      const QString& name = QString()) {
    ProfileWeldTemplate output;
    output.name = name;
    if (!detection.valid || !detection.profileContour ||
        detection.contourStartPx < 0 ||
        detection.contourEndPx <= detection.contourStartPx ||
        detection.contourEndPx >= points.size()) {
      return output;
    }
    std::vector<double> residual(
        static_cast<std::size_t>(points.size()),
        std::numeric_limits<double>::quiet_NaN());
    std::vector<int> valid;
    for (int index = 0; index < points.size(); ++index) {
      const QVector3D& point = points[index];
      if (!std::isfinite(point.x()) || !std::isfinite(point.z()) ||
          point.z() <= 0.0F) {
        continue;
      }
      valid.push_back(index);
    }
    if (valid.size() < 5) return output;
    const auto median = [](std::vector<double> values) {
      const auto middle = values.begin() + values.size() / 2;
      std::nth_element(values.begin(), middle, values.end());
      return *middle;
    };
    for (std::size_t position = 0; position < valid.size(); ++position) {
      std::vector<double> neighborhood;
      const int first = std::max(0, static_cast<int>(position) - 2);
      const int last = std::min(static_cast<int>(valid.size()) - 1,
                                static_cast<int>(position) + 2);
      for (int neighbor = first; neighbor <= last; ++neighbor)
        neighborhood.push_back(points[valid[neighbor]].z());
      const int index = valid[position];
      residual[index] = median(neighborhood) - detection.profileBaselineOffset -
                        detection.profileBaselineSlope * points[index].x();
    }
    output.normalizedShape = normalizedRaisedShape(
        residual, detection.contourStartPx, detection.contourEndPx);
    output.widthRatio =
        double(detection.contourEndPx - detection.contourStartPx + 1) /
        std::max(1, valid.back() - valid.front());
    return output;
  }

  static ProfileWeldTemplate captureTemplate(
      const QVector<QVector3D>& points, double selectionMinimumX,
      double selectionMaximumX, const QString& name = QString()) {
    ProfileWeldTemplate output;
    if (!std::isfinite(selectionMinimumX) || !std::isfinite(selectionMaximumX)) {
      return output;
    }
    const double minimumX = std::min(selectionMinimumX, selectionMaximumX);
    const double maximumX = std::max(selectionMinimumX, selectionMaximumX);
    std::vector<int> valid;
    double scanMinimumX = 0.0;
    double scanMaximumX = 0.0;
    for (int index = 0; index < points.size(); ++index) {
      const QVector3D& point = points[index];
      if (!std::isfinite(point.x()) || !std::isfinite(point.z()) ||
          point.z() <= 0.0F) {
        continue;
      }
      if (!valid.empty() && point.x() <= points[valid.back()].x()) {
        const double resetThreshold = std::max(
            100.0, (scanMaximumX - scanMinimumX) * 0.25);
        if (points[valid.back()].x() - point.x() > resetThreshold) break;
        continue;
      }
      if (valid.empty()) scanMinimumX = scanMaximumX = point.x();
      scanMinimumX = std::min(scanMinimumX, double(point.x()));
      scanMaximumX = std::max(scanMaximumX, double(point.x()));
      valid.push_back(index);
    }
    if (valid.size() < 24) return output;
    const auto median = [](std::vector<double> values) {
      if (values.empty()) return 0.0;
      const auto middle = values.begin() + values.size() / 2;
      std::nth_element(values.begin(), middle, values.end());
      return *middle;
    };
    const int edgeCount = std::max(6, static_cast<int>(valid.size() / 10));
    std::vector<double> leftX, leftZ, rightX, rightZ;
    for (int index = 0; index < edgeCount; ++index) {
      const int leftIndex = valid[index];
      const int rightIndex = valid[valid.size() - 1 - index];
      leftX.push_back(points[leftIndex].x());
      leftZ.push_back(points[leftIndex].z());
      rightX.push_back(points[rightIndex].x());
      rightZ.push_back(points[rightIndex].z());
    }
    const double leftCenterX = median(leftX);
    const double rightCenterX = median(rightX);
    if (rightCenterX <= leftCenterX) return output;
    LaserGapDetection selected;
    selected.valid = true;
    selected.profileContour = true;
    selected.profileBaselineSlope =
        (median(rightZ) - median(leftZ)) / (rightCenterX - leftCenterX);
    selected.profileBaselineOffset =
        median(leftZ) - selected.profileBaselineSlope * leftCenterX;
    for (int index : valid) {
      if (points[index].x() < minimumX) continue;
      if (points[index].x() > maximumX) break;
      if (selected.contourStartPx < 0) selected.contourStartPx = index;
      selected.contourEndPx = index;
    }
    selected.gapStartPx = selected.contourStartPx;
    selected.gapEndPx = selected.contourEndPx;
    if (selected.contourStartPx < 0 ||
        selected.contourEndPx - selected.contourStartPx < 5) {
      return output;
    }
    output = captureTemplate(points, selected, name);
    return output;
  }

  static LaserGapDetection detect(const QVector<QVector3D>& points,
                                 const LaserGapDetectorConfig& config = {}) {
    LaserGapDetection result;
    result.profileContour = true;
    const int n = points.size();
    if (n < 32) return result;
    const auto median = [](std::vector<double> values) {
      if (values.empty()) return 0.0;
      const auto middle = values.begin() + values.size() / 2;
      std::nth_element(values.begin(), middle, values.end());
      return *middle;
    };
    std::vector<int> valid;
    std::vector<double> differences;
    double scanMinimumX = 0.0;
    double scanMaximumX = 0.0;
    for (int i = 0; i < n; ++i) {
      const auto& p = points[i];
      if (!std::isfinite(p.x()) || !std::isfinite(p.z()) || p.z() <= 0) continue;
      if (!valid.empty() && p.x() <= points[valid.back()].x()) {
        const double resetThreshold = std::max(
            100.0, (scanMaximumX - scanMinimumX) * 0.25);
        if (points[valid.back()].x() - p.x() > resetThreshold) break;
        continue;
      }
      if (valid.empty()) scanMinimumX = scanMaximumX = p.x();
      scanMinimumX = std::min(scanMinimumX, double(p.x()));
      scanMaximumX = std::max(scanMaximumX, double(p.x()));
      if (!valid.empty() && i == valid.back() + 1)
        differences.push_back(p.z() - points[valid.back()].z());
      valid.push_back(i);
    }
    const double observedHalfSpanX =
        std::max(std::abs(scanMinimumX), std::abs(scanMaximumX));
    result.profileAxisHalfSpanX =
        std::isfinite(config.profileAxisHalfSpanX) && config.profileAxisHalfSpanX > 0.0
            ? config.profileAxisHalfSpanX : observedHalfSpanX;
    if (!config.profileAxisLocked)
      result.profileAxisHalfSpanX =
          std::max(result.profileAxisHalfSpanX, observedHalfSpanX);
    if (result.profileAxisHalfSpanX <= 0.0) return result;
    const auto xRatio = [&result](double x) {
      return std::clamp(0.5 + x / (2.0 * result.profileAxisHalfSpanX), 0.0, 1.0);
    };
    result.profileLineStartRatio = xRatio(scanMinimumX);
    result.profileLineEndRatio = xRatio(scanMaximumX);
    if (valid.size() < 24 || differences.size() < 12) return result;
    // Suppress isolated SDK height spikes before estimating the parent plane
    // and raised contour. Keep the original scan indices so all detector
    // coordinates remain compatible with the control and preview layers.
    const int smoothingRadius = std::clamp(
        config.profileTuning.smoothingRadius, 1, 5);
    std::vector<double> filteredZ(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t position = 0; position < valid.size(); ++position) {
      std::vector<double> neighborhood;
      neighborhood.reserve(5);
      const int first = std::max<int>(0, static_cast<int>(position) - smoothingRadius);
      const int last = std::min<int>(static_cast<int>(valid.size()) - 1,
                                     static_cast<int>(position) + smoothingRadius);
      for (int neighbor = first; neighbor <= last; ++neighbor)
        neighborhood.push_back(points[valid[neighbor]].z());
      filteredZ[valid[position]] = median(neighborhood);
    }
    const auto zAt = [&filteredZ](int index) { return filteredZ[index]; };
    const double step = median(differences);
    for (double& d : differences) d = std::abs(d - step);
    const double differenceNoise =
        std::max(1e-6, 1.4826 * median(differences) / std::sqrt(2.0));
    const int edgeCount = std::max(6, static_cast<int>(std::ceil(
        valid.size() * std::clamp(config.profileTuning.baselineEdgeRatio, 0.03, 0.30))));
    std::vector<double> leftX, leftZ, rightX, rightZ;
    for (int k = 0; k < edgeCount; ++k) {
      const auto& l = points[valid[k]];
      const auto& r = points[valid[valid.size() - 1 - k]];
      leftX.push_back(l.x()); leftZ.push_back(zAt(valid[k]));
      rightX.push_back(r.x()); rightZ.push_back(zAt(valid[valid.size() - 1 - k]));
    }
    const double lx = median(leftX), rx = median(rightX);
    if (rx <= lx) return result;
    double slope = (median(rightZ) - median(leftZ)) / (rx - lx);
    double offset = median(leftZ) - slope * lx;
    for (int pass = 0; pass < 4; ++pass) {
      std::vector<double> errors;
      errors.reserve(valid.size());
      for (int index : valid)
        errors.push_back(zAt(index) - offset - slope * points[index].x());
      const double bias = median(errors);
      for (double& error : errors) error = std::abs(error - bias);
      const double parentNoise = std::max(differenceNoise, 1.4826 * median(errors));
      offset += bias;
      double sx = 0, sz = 0, sxx = 0, sxz = 0, count = 0;
      for (int i : valid) {
        const double x = points[i].x() - lx;
        const double z = zAt(i);
        if (std::abs(z - offset - slope * points[i].x()) > parentNoise * 3) continue;
        sx += x; sz += z; sxx += x*x; sxz += x*z; ++count;
      }
      const double denominator = count*sxx - sx*sx;
      if (count < 12 || denominator <= 1e-12) break;
      slope = (count*sxz - sx*sz) / denominator;
      offset = (sz - slope*sx) / count - slope*lx;
    }
    std::vector<double> deviations;
    deviations.reserve(valid.size());
    for (int index : valid)
      deviations.push_back(std::abs(zAt(index) - offset - slope * points[index].x()));
    const double noise = std::max(differenceNoise, 1.4826 * median(deviations));
    result.profileNoise = noise;
    result.profileBaselineSlope = slope;
    result.profileBaselineOffset = offset;
    const double grow = noise * std::clamp(config.profileTuning.growNoiseSigma, 0.5, 12.0);
    const double seed = noise * std::max(
        std::clamp(config.profileTuning.seedNoiseSigma, 0.75, 16.0),
        std::clamp(config.profileTuning.growNoiseSigma, 0.5, 12.0));
    std::vector<double> residual(n, std::numeric_limits<double>::quiet_NaN());
    for (int i : valid) residual[i] = zAt(i) - offset - slope*points[i].x();
    const int scanLength = std::max(1, valid.back() - valid.front() + 1);
    const int minWidth = std::max(6, static_cast<int>(std::ceil(
        scanLength * std::clamp(config.profileTuning.minimumWidthRatio, 0.001, 0.20))));
    const int maxHole = static_cast<int>(std::floor(
        scanLength * std::clamp(config.profileTuning.maximumCandidateHoleRatio, 0.0, 0.25)));
    const int maximumBaselineSamples = std::max(2, smoothingRadius);
    const int shoulder = std::max(4, static_cast<int>(std::ceil(
        scanLength * std::clamp(config.profileTuning.shoulderRatio, 0.002, 0.05))));
    double best = 0, runnerUp = 0;
    bool templateActive = false;
    bool templateRejected = false;
    for (const ProfileWeldTemplate& profileTemplate : config.profileTemplates) {
      if (profileTemplate.enabled &&
          profileTemplate.normalizedShape.size() >= kTemplateSamples) {
        templateActive = true;
        break;
      }
    }
    for (int i = valid.front(); i <= valid.back();) {
      if (!(residual[i] > grow)) { ++i; continue; }
      const int start = i;
      int end = i, lastRaised = i, support = 0, seeds = 0, longestHole = 0;
      int baselineSamples = 0;
      double area = 0;
      for (; i <= valid.back(); ++i) {
        if (residual[i] > grow) {
          longestHole = std::max(longestHole, i-lastRaised-1);
          lastRaised = end = i;
          baselineSamples = 0;
          ++support;
          if (residual[i] > seed) ++seeds;
          area += std::min(residual[i], seed*10);
        } else {
          if (std::isfinite(residual[i]) && ++baselineSamples >= maximumBaselineSamples) break;
          if (i-lastRaised > maxHole) break;
        }
      }
      const int length = end-start+1;
      const double supportRatio = double(support) / std::max(1, length);
      if (length < minWidth ||
          seeds < std::clamp(config.profileTuning.minimumSeedCount, 1, 32) ||
          supportRatio < std::clamp(config.profileTuning.minimumSupportRatio, 0.05, 0.98) ||
          longestHole > length * std::clamp(config.profileTuning.maximumInternalHoleRatio, 0.0, 0.95)) {
        continue;
      }
      int left = 0, right = 0;
      const double shoulderTolerance = std::max(grow, noise * 3.0);
      // Both shoulders must return to the same tilted parent baseline.
      for (int j = 1; j <= shoulder*3; ++j) {
        if (start-j >= 0 && std::abs(residual[start-j]) <= shoulderTolerance) ++left;
        if (end+j < n && std::abs(residual[end+j]) <= shoulderTolerance) ++right;
      }
      if (std::min(left,right) < shoulder) continue;
      const double centerX = (double(points[start].x()) + points[end].x()) * 0.5;
      const double center = xRatio(centerX);
      const double absoluteWidth = xRatio(points[end].x()) - xRatio(points[start].x());
      // Profile exports can contain invalid slots and zero padding after the
      // last real scan sample. Normalize the raised footprint by the valid
      // scan extent, otherwise padding makes an unchanged weld appear to
      // change width from frame to frame.
      const double validAxisSpan =
          std::max(1, valid.back() - valid.front());
      const double width = double(length) / validAxisSpan;
      if ((config.expectedAbsoluteCenterRatio >= 0 &&
           std::abs(center-config.expectedAbsoluteCenterRatio) > config.maximumAbsoluteCenterJumpRatio) ||
          (config.referenceAbsoluteCenterRatio >= 0 &&
           std::abs(center-config.referenceAbsoluteCenterRatio) > config.maximumReferenceCenterDriftRatio)) {
        result.continuityRejected = true; continue;
      }
      // Raised weld shoulders can change apparent width as the raw profile
      // crosses a sloped seam or contains invalid scan slots. Width is useful
      // for ranking candidates, but it must not erase a candidate whose center
      // and two shoulder supports remain continuous.
      const bool widthJump = config.expectedAbsoluteGapWidthRatio > 0 &&
          std::abs(absoluteWidth-config.expectedAbsoluteGapWidthRatio) >
              config.maximumTrackingGapWidthJumpRatio;
      double score = area / seed;
      if (widthJump) score *= 0.45;
      if (config.expectedAbsoluteCenterRatio >= 0)
        score /= 1 + 12*std::abs(center-config.expectedAbsoluteCenterRatio);
      QString matchedTemplateName;
      double matchedTemplateSimilarity = 0.0;
      double matchedTemplateWidthScale = 0.0;
      bool candidateTemplateMatched = false;
      if (templateActive) {
        const QVector<float> candidateShape = normalizedRaisedShape(residual, start, end);
        for (const ProfileWeldTemplate& profileTemplate : config.profileTemplates) {
          if (!profileTemplate.enabled ||
              profileTemplate.normalizedShape.size() < kTemplateSamples ||
              candidateShape.size() != profileTemplate.normalizedShape.size()) {
            continue;
          }
          const double widthScale = profileTemplate.widthRatio > 1e-9
              ? width / profileTemplate.widthRatio : 1.0;
          if (widthScale < profileTemplate.minimumWidthScale ||
              widthScale > profileTemplate.maximumWidthScale) {
            continue;
          }
          const double similarity = templateSimilarity(
              profileTemplate.normalizedShape, candidateShape);
          if (similarity < profileTemplate.minimumSimilarity ||
              similarity <= matchedTemplateSimilarity) {
            continue;
          }
          matchedTemplateName = profileTemplate.name;
          matchedTemplateSimilarity = similarity;
          matchedTemplateWidthScale = widthScale;
          candidateTemplateMatched = true;
        }
        if (!candidateTemplateMatched) {
          templateRejected = true;
          continue;
        }
        if (candidateTemplateMatched)
          score *= 0.50 + 0.50 * matchedTemplateSimilarity;
      }
      if (score <= best) { runnerUp = std::max(runnerUp,score); continue; }
      runnerUp = best; best = score;
      result.valid = result.baselineSupported = result.contourSupported = true;
      result.gapStartPx = result.contourStartPx = start;
      result.gapEndPx = result.contourEndPx = end;
      result.lineStartPx = valid.front(); result.lineEndPx = valid.back();
      // Use footprint midpoint, not the highest point: asymmetric peaks must
      // not move the steering target when their height changes.
      result.absoluteCenterRatio = center;
      result.profileCenterX = centerX;
      result.profileGapStartRatio = xRatio(points[start].x());
      result.profileGapEndRatio = xRatio(points[end].x());
      result.normalizedCenter = std::clamp(
          (centerX - scanMinimumX) / (scanMaximumX - scanMinimumX), 0.0, 1.0);
      result.confidence = result.contourConfidence = .5 + .4*support/length;
      result.supportingSamples = support;
      result.templateEvaluated = templateActive;
      result.templateMatched = candidateTemplateMatched;
      result.templateRejected = false;
      result.templateName = matchedTemplateName;
      result.templateSimilarity = matchedTemplateSimilarity;
      result.templateWidthScale = matchedTemplateWidthScale;
    }
    if (best > 0 && runnerUp > best*.85) result.valid = false;
    result.templateEvaluated = templateActive;
    result.templateRejected = templateActive && !result.valid && templateRejected;
    if (result.valid) result.continuityRejected = result.widthRejected = false;
    return result;
  }

 private:
  static constexpr int kTemplateSamples = 41;

  static QVector<float> normalizedRaisedShape(const std::vector<double>& residual,
                                              int start, int end) {
    QVector<float> output;
    if (start < 0 || end <= start ||
        end >= static_cast<int>(residual.size())) return output;
    output.reserve(kTemplateSamples);
    double maximum = 0.0;
    for (int sample = 0; sample < kTemplateSamples; ++sample) {
      const double position = start +
          double(end - start) * sample / double(kTemplateSamples - 1);
      const int left = std::max(start, static_cast<int>(std::floor(position)));
      const int right = std::min(end, static_cast<int>(std::ceil(position)));
      double value = residual[left];
      if (!std::isfinite(value) && right != left) value = residual[right];
      if (std::isfinite(value) && right != left && std::isfinite(residual[right])) {
        value += (residual[right] - value) * (position - left);
      }
      value = std::isfinite(value) ? std::max(0.0, value) : 0.0;
      maximum = std::max(maximum, value);
      output.append(static_cast<float>(value));
    }
    if (maximum <= 1e-9) return {};
    for (float& value : output) value = static_cast<float>(value / maximum);
    return output;
  }

  static double templateSimilarity(const QVector<float>& expected,
                                   const QVector<float>& observed) {
    if (expected.size() != observed.size() || expected.isEmpty()) return 0.0;
    double squaredError = 0.0;
    for (int index = 0; index < expected.size(); ++index) {
      const double difference = expected[index] - observed[index];
      squaredError += difference * difference;
    }
    return std::max(0.0, 1.0 - std::sqrt(squaredError / expected.size()));
  }
};
}
