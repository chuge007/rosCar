#pragma once

#include "laser_gap_detector.h"
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <deque>
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
    ProfileWeldDetectionTrace* trace = config.profileDetectionTrace;
    if (trace && trace->enabled)
      trace->append(QStringLiteral("event=detector_begin samples=%1").arg(n));
    if (n < 32) {
      if (trace && trace->enabled)
        trace->append(QStringLiteral(
            "event=detector_exit reason=too_few_samples samples=%1").arg(n));
      return result;
    }
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
    if (trace && trace->enabled)
      trace->append(QStringLiteral(
          "event=scan_summary valid=%1 differences=%2 x_min=%3 x_max=%4")
                       .arg(valid.size())
                       .arg(differences.size())
                       .arg(scanMinimumX, 0, 'g', 10)
                       .arg(scanMaximumX, 0, 'g', 10));
    if (result.profileAxisHalfSpanX <= 0.0) {
      if (trace && trace->enabled)
        trace->append(QStringLiteral(
            "event=detector_exit reason=invalid_axis_half_span"));
      return result;
    }
    const auto xRatio = [&result](double x) {
      return std::clamp(0.5 + x / (2.0 * result.profileAxisHalfSpanX), 0.0, 1.0);
    };
    result.profileLineStartRatio = xRatio(scanMinimumX);
    result.profileLineEndRatio = xRatio(scanMaximumX);
    if (valid.size() < 24 || differences.size() < 12) {
      if (trace && trace->enabled)
        trace->append(QStringLiteral(
            "event=detector_exit reason=insufficient_valid_scan valid=%1 differences=%2")
                         .arg(valid.size())
                         .arg(differences.size()));
      return result;
    }
    // Suppress isolated SDK height spikes before estimating the parent plane
    // and raised contour. Keep the original scan indices so all detector
    // coordinates remain compatible with the control and preview layers.
    const int smoothingRadius = std::clamp(
        config.profileTuning.smoothingRadius, 1, 5);
    // Smooth only across nearby SDK slots. The valid-point list is sparse, so
    // using position distance alone would mix both sides of a long dropout.
    std::vector<double> filteredZ(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t position = 0; position < valid.size(); ++position) {
      std::vector<double> neighborhood;
      neighborhood.reserve(5);
      const int first = std::max<int>(0, static_cast<int>(position) - smoothingRadius);
      const int last = std::min<int>(static_cast<int>(valid.size()) - 1,
                                     static_cast<int>(position) + smoothingRadius);
      for (int neighbor = first; neighbor <= last; ++neighbor) {
        if (std::abs(valid[neighbor] - valid[position]) > smoothingRadius)
          continue;
        const float z = points[valid[neighbor]].z();
        if (z > 0.0F) neighborhood.push_back(z);
      }
      if (!neighborhood.empty())
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
    if (rx <= lx) {
      if (trace && trace->enabled)
        trace->append(QStringLiteral(
            "event=detector_exit reason=invalid_baseline_span left_x=%1 right_x=%2")
                         .arg(lx, 0, 'g', 10)
                         .arg(rx, 0, 'g', 10));
      return result;
    }
    double slope = (median(rightZ) - median(leftZ)) / (rx - lx);
    double offset = median(leftZ) - slope * lx;
    for (int pass = 0; pass < 4; ++pass) {
      std::vector<double> errors;
      errors.reserve(edgeCount * 2);
      for (int k = 0; k < edgeCount; ++k) {
        const int leftIndex = valid[k];
        const int rightIndex = valid[valid.size() - 1 - k];
        errors.push_back(zAt(leftIndex) - offset -
                         slope * points[leftIndex].x());
        errors.push_back(zAt(rightIndex) - offset -
                         slope * points[rightIndex].x());
      }
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
    deviations.reserve(edgeCount * 2);
    for (int k = 0; k < edgeCount; ++k) {
      const int leftIndex = valid[k];
      const int rightIndex = valid[valid.size() - 1 - k];
      deviations.push_back(std::abs(
          zAt(leftIndex) - offset - slope * points[leftIndex].x()));
      deviations.push_back(std::abs(
          zAt(rightIndex) - offset - slope * points[rightIndex].x()));
    }
    const double noise = std::max(differenceNoise, 1.4826 * median(deviations));
    result.profileNoise = noise;
    result.profileBaselineSlope = slope;
    result.profileBaselineOffset = offset;
    const double grow = noise * std::clamp(config.profileTuning.growNoiseSigma, 0.5, 12.0);
    const double seed = noise * std::max(
        std::clamp(config.profileTuning.seedNoiseSigma, 0.75, 16.0),
        std::clamp(config.profileTuning.growNoiseSigma, 0.5, 12.0));
    std::vector<double> residual(n, std::numeric_limits<double>::quiet_NaN());
    for (int i : valid) {
      if (!std::isfinite(zAt(i))) continue;
      residual[i] = zAt(i) - offset - slope*points[i].x();
    }
    const int scanLength = std::max(1, valid.back() - valid.front() + 1);
    const int minWidth = std::max(6, static_cast<int>(std::ceil(
        scanLength * std::clamp(config.profileTuning.minimumWidthRatio, 0.001, 0.20))));
    const int maxWidth = std::max(
        minWidth, 1 + static_cast<int>(std::ceil(
            std::max(0, scanLength - 1) * std::clamp(
                config.profileTuning.maximumWidthRatio, 0.05, 0.95))));
    const int maxHole = static_cast<int>(std::floor(
        scanLength * std::clamp(config.profileTuning.maximumCandidateHoleRatio, 0.0, 0.25)));
    const int maximumBaselineSamples = std::max(2, smoothingRadius);
    const int shoulder = std::max(4, static_cast<int>(std::ceil(
        scanLength * std::clamp(config.profileTuning.shoulderRatio, 0.002, 0.05))));
    if (trace && trace->enabled) {
      trace->append(QStringLiteral(
          "event=baseline noise=%1 difference_noise=%2 slope=%3 offset=%4 "
          "grow=%5 seed=%6 scan_length=%7 min_width=%8 max_width=%9 "
          "max_hole=%10 shoulder=%11")
                        .arg(noise, 0, 'g', 10)
                        .arg(differenceNoise, 0, 'g', 10)
                        .arg(slope, 0, 'g', 10)
                        .arg(offset, 0, 'g', 10)
                        .arg(grow, 0, 'g', 10)
                        .arg(seed, 0, 'g', 10)
                        .arg(scanLength)
                        .arg(minWidth)
                        .arg(maxWidth)
                        .arg(maxHole)
                        .arg(shoulder));
      appendTraceSeries(trace, QStringLiteral("event=residual_data"),
                        residual, valid);
    }
    double best = 0, runnerUp = 0;
    bool templateActive = false;
    for (const ProfileWeldTemplate& profileTemplate : config.profileTemplates) {
      if (profileTemplate.enabled &&
          profileTemplate.normalizedShape.size() >= kTemplateSamples) {
        templateActive = true;
        break;
      }
    }
    // Candidate saliency combines height above the robust parent baseline
    // with a multi-scale top-hat response. The first term preserves a whole
    // raised plateau; the second reveals narrower peaks and slope changes.
    // Their maximum covers spikes, ramps, steps, plateaus and disconnected
    // raised pieces without selecting only a plateau edge.
    std::vector<double> saliency;
    buildMultiScaleTopHat(residual, valid, noise, saliency);
    if (trace && trace->enabled)
      appendTraceSeries(trace, QStringLiteral("event=saliency_data"),
                        saliency, valid);
    std::vector<std::pair<int, int>> candidateSpans;
    for (int i = valid.front(); i <= valid.back();) {
      if (!(saliency[i] > grow)) { ++i; continue; }
      const int spanStart = i;
      int end = i, lastRaised = i;
      int baselineSamples = 0;
      for (; i <= valid.back(); ++i) {
        if (saliency[i] > grow) {
          lastRaised = end = i;
          baselineSamples = 0;
        } else {
          if (std::isfinite(saliency[i]) && ++baselineSamples >= maximumBaselineSamples) break;
          if (i-lastRaised > maxHole) break;
        }
      }
      candidateSpans.emplace_back(spanStart, end);
    }
    if (trace && trace->enabled)
      trace->append(QStringLiteral("event=candidate_spans count=%1")
                        .arg(candidateSpans.size()));
    for (const auto& span : candidateSpans) {
      const int start = span.first;
      const int end = span.second;
      int support = 0, seeds = 0, longestHole = 0, lastRaised = start;
      double area = 0, peak = 0;
      for (int i = start; i <= end; ++i) {
        if (saliency[i] > grow) {
          longestHole = std::max(longestHole, i-lastRaised-1);
          lastRaised = i;
          ++support;
          if (saliency[i] > seed) ++seeds;
          peak = std::max(peak, saliency[i]);
          area += std::min(saliency[i], seed*10);
        }
      }
      const int length = end-start+1;
      const double supportRatio = double(support) / std::max(1, length);
      const int minimumSeeds =
          std::clamp(config.profileTuning.minimumSeedCount, 1, 32);
      const double minimumSupport =
          std::clamp(config.profileTuning.minimumSupportRatio, 0.05, 0.98);
      const double maximumHoleRatio =
          std::clamp(config.profileTuning.maximumInternalHoleRatio, 0.0, 0.95);
      QString rejectReason;
      if (length < minWidth) rejectReason = QStringLiteral("too_narrow");
      else if (length > maxWidth) rejectReason = QStringLiteral("too_wide");
      else if (seeds < minimumSeeds) rejectReason = QStringLiteral("too_few_seeds");
      else if (supportRatio < minimumSupport)
        rejectReason = QStringLiteral("low_support");
      else if (longestHole > length * maximumHoleRatio)
        rejectReason = QStringLiteral("internal_hole");
      if (!rejectReason.isEmpty()) {
        if (trace && trace->enabled)
          trace->append(QStringLiteral(
              "event=candidate frame_start=%1 frame_end=%2 length=%3 support=%4 "
              "support_ratio=%5 seeds=%6 longest_hole=%7 area=%8 peak=%9 "
              "x_start=%10 x_end=%11 rejected=%12")
                            .arg(span.first)
                            .arg(span.second)
                            .arg(length)
                            .arg(support)
                            .arg(supportRatio, 0, 'g', 8)
                            .arg(seeds)
                            .arg(longestHole)
                            .arg(area, 0, 'g', 10)
                            .arg(peak, 0, 'g', 10)
                            .arg(points[start].x(), 0, 'g', 10)
                            .arg(points[end].x(), 0, 'g', 10)
                            .arg(rejectReason));
        continue;
      }
      int left = 0, right = 0;
      const double shoulderTolerance = std::max(grow, noise * 3.0);
      // Search across the same missing-data span that candidate growth may
      // bridge. This keeps a detached weld island eligible when its nearest
      // measured parent-surface samples are separated by a dropout.
      const int shoulderSearch = std::max(shoulder * 3, maxHole);
      for (int j = 1; j <= shoulderSearch; ++j) {
        if (start-j >= 0 && std::isfinite(saliency[start-j]) &&
            saliency[start-j] <= shoulderTolerance) ++left;
        if (end+j < n && std::isfinite(saliency[end+j]) &&
            saliency[end+j] <= shoulderTolerance) ++right;
        if (left >= shoulder && right >= shoulder) break;
      }
      if (std::min(left,right) < shoulder) {
        if (trace && trace->enabled)
          trace->append(QStringLiteral(
              "event=candidate frame_start=%1 frame_end=%2 length=%3 support=%4 "
              "support_ratio=%5 seeds=%6 left_shoulder=%7 right_shoulder=%8 "
              "area=%9 peak=%10 x_start=%11 x_end=%12 rejected=shoulder")
                            .arg(span.first)
                            .arg(span.second)
                            .arg(length)
                            .arg(support)
                            .arg(supportRatio, 0, 'g', 8)
                            .arg(seeds)
                            .arg(left)
                            .arg(right)
                            .arg(area, 0, 'g', 10)
                            .arg(peak, 0, 'g', 10)
                            .arg(points[start].x(), 0, 'g', 10)
                            .arg(points[end].x(), 0, 'g', 10));
        continue;
      }
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
        result.continuityRejected = true;
        if (trace && trace->enabled)
          trace->append(QStringLiteral(
              "event=candidate frame_start=%1 frame_end=%2 center_ratio=%3 "
              "rejected=continuity")
                            .arg(span.first)
                            .arg(span.second)
                            .arg(center, 0, 'g', 8));
        continue;
      }
      // Raised weld shoulders can change apparent width as the raw profile
      // crosses a sloped seam or contains invalid scan slots. Width is useful
      // for ranking candidates, but it must not erase a candidate whose center
      // and two shoulder supports remain continuous.
      const bool widthJump = config.expectedAbsoluteGapWidthRatio > 0 &&
          std::abs(absoluteWidth-config.expectedAbsoluteGapWidthRatio) >
              config.maximumTrackingGapWidthJumpRatio;
      double score = area / noise;
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
        if (candidateTemplateMatched) {
          score *= 1.0 + 0.10 * matchedTemplateSimilarity;
        } else if (trace && trace->enabled) {
          trace->append(QStringLiteral(
              "event=candidate frame_start=%1 frame_end=%2 center_ratio=%3 "
              "template_match=none")
                            .arg(span.first)
                            .arg(span.second)
                            .arg(center, 0, 'g', 8));
        }
      }
      if (score <= best) {
        runnerUp = std::max(runnerUp, score);
        if (trace && trace->enabled)
          trace->append(QStringLiteral(
              "event=candidate frame_start=%1 frame_end=%2 length=%3 area_score=%4 "
              "width_ratio=%5 center_ratio=%6 x_start=%7 x_end=%8 "
              "peak=%9 accepted=runner_up")
                            .arg(span.first)
                            .arg(span.second)
                            .arg(length)
                            .arg(score, 0, 'g', 10)
                            .arg(width, 0, 'g', 8)
                            .arg(center, 0, 'g', 8)
                            .arg(points[start].x(), 0, 'g', 10)
                            .arg(points[end].x(), 0, 'g', 10)
                            .arg(peak, 0, 'g', 10));
        continue;
      }
      runnerUp = best; best = score;
      if (trace && trace->enabled)
        trace->append(QStringLiteral(
            "event=candidate frame_start=%1 frame_end=%2 length=%3 area_score=%4 "
            "width_ratio=%5 center_ratio=%6 x_start=%7 x_end=%8 peak=%9 "
            "accepted=best")
                          .arg(span.first)
                          .arg(span.second)
                          .arg(length)
                          .arg(score, 0, 'g', 10)
                          .arg(width, 0, 'g', 8)
                          .arg(center, 0, 'g', 8)
                          .arg(points[start].x(), 0, 'g', 10)
                          .arg(points[end].x(), 0, 'g', 10)
                          .arg(peak, 0, 'g', 10));
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
    result.templateEvaluated = templateActive;
    result.templateRejected = false;
    if (result.valid) result.continuityRejected = result.widthRejected = false;
    if (trace && trace->enabled)
      trace->append(QStringLiteral(
          "event=detector_result valid=%1 start=%2 end=%3 center_x=%4 "
          "center_ratio=%5 confidence=%6 best_score=%7 runner_up=%8 "
          "x_start=%9 x_end=%10")
                        .arg(result.valid)
                        .arg(result.gapStartPx)
                        .arg(result.gapEndPx)
                        .arg(result.profileCenterX, 0, 'g', 10)
                        .arg(result.absoluteCenterRatio, 0, 'g', 10)
                        .arg(result.confidence, 0, 'g', 10)
                        .arg(best, 0, 'g', 10)
                        .arg(runnerUp, 0, 'g', 10)
                        .arg(result.valid ? points[result.gapStartPx].x() : 0.0F,
                             0, 'g', 10)
                        .arg(result.valid ? points[result.gapEndPx].x() : 0.0F,
                             0, 'g', 10));
    return result;
  }

 private:
  static constexpr int kTemplateSamples = 41;

  static void appendTraceSeries(ProfileWeldDetectionTrace* trace,
                                const QString& event,
                                const std::vector<double>& values,
                                const std::vector<int>& valid) {
    if (!trace || !trace->enabled) return;
    constexpr int kChunkSize = 128;
    QString chunk;
    chunk.reserve(4096);
    int chunkNumber = 0;
    for (int position = 0; position < static_cast<int>(valid.size());
         ++position) {
      const int index = valid[position];
      const double value = values[index];
      chunk += QStringLiteral("%1:%2;")
                   .arg(index)
                   .arg(std::isfinite(value)
                            ? QString::number(value, 'g', 10)
                            : QStringLiteral("nan"));
      if ((position + 1) % kChunkSize == 0 ||
          position + 1 == static_cast<int>(valid.size())) {
        trace->append(QStringLiteral("%1 chunk=%2 data=%3")
                          .arg(event)
                          .arg(++chunkNumber)
                          .arg(chunk));
        chunk.clear();
      }
    }
  }

  // Sliding-window extreme (monotonic deque) over one finite run. Running
  // it once for the minimum and once for the maximum gives erosion and
  // dilation, so opening = dilate(erode(x)). Linear in the run length.
  static void slidingExtreme(const std::vector<double>& samples, int radius,
                             bool maximum, std::vector<double>& output) {
    output.assign(samples.size(), 0.0);
    if (samples.empty()) return;
    std::deque<int> deque;
    const auto push = [&](int index, int first) {
      while (!deque.empty() && deque.front() < first)
        deque.pop_front();
      while (!deque.empty() &&
             (maximum ? samples[deque.back()] <= samples[index]
                      : samples[deque.back()] >= samples[index]))
        deque.pop_back();
      deque.push_back(index);
    };
    for (int right = 0; right < static_cast<int>(samples.size()); ++right) {
      const int first = std::max(0, right - radius * 2);
      push(right, first);
      const int center = right - radius;
      if (center >= 0)
        output[center] = samples[deque.front()];
    }
    const int firstTailCenter = std::max(
        0, static_cast<int>(samples.size()) - radius);
    for (int center = firstTailCenter;
         center < static_cast<int>(samples.size()); ++center) {
      const int first = std::max(0, center - radius);
      while (!deque.empty() && deque.front() < first)
        deque.pop_front();
      if (!deque.empty())
        output[center] = samples[deque.front()];
    }
  }

  // Combine positive height above the fitted parent surface with multi-scale
  // morphological white top-hat responses over each finite run.
  static void buildMultiScaleTopHat(const std::vector<double>& residual,
                                    const std::vector<int>& valid,
                                    double noise,
                                    std::vector<double>& saliency) {
    saliency.assign(residual.size(), std::numeric_limits<double>::quiet_NaN());
    Q_UNUSED(noise);
    const int validCount = static_cast<int>(valid.size());
    if (validCount < 8) return;
    // Invalid slots split the profile into finite runs. Each run is
    // processed on its own so a dropout cannot leak a fabricated baseline
    // into the neighbouring run.
    std::vector<std::pair<int, int>> runs;
    int runStart = 0;
    for (int position = 0; position < validCount; ++position) {
      const bool invalid = !std::isfinite(residual[valid[position]]);
      const bool indexGap = position > runStart &&
          valid[position] != valid[position - 1] + 1;
      if (invalid || indexGap) {
        if (position > runStart) runs.emplace_back(runStart, position);
        runStart = invalid ? position + 1 : position;
      }
    }
    if (validCount > runStart) runs.emplace_back(runStart, validCount);
    std::vector<double> samples;
    std::vector<double> eroded;
    std::vector<double> opened;
    for (const auto& run : runs) {
      const int length = run.second - run.first;
      if (length <= 0) continue;
      samples.clear();
      samples.reserve(length);
      for (int position = run.first; position < run.second; ++position) {
        const double value = residual[valid[position]];
        saliency[valid[position]] = std::max(0.0, value);
        samples.push_back(value);
      }
      const int maximumScale = std::max(4, length / 4);
      if (length < 9) {
        // Too short for any structuring element: the global parent fit
        // already marks these points as raised, so keep their full residual.
        continue;
      }
      for (int scale = 4; scale <= maximumScale; scale *= 2) {
        if (2 * scale + 1 > length) break;
        slidingExtreme(samples, scale, false, eroded);
        slidingExtreme(eroded, scale, true, opened);
        for (int offset = 0; offset < length; ++offset) {
          const int index = valid[run.first + offset];
          const double response = samples[offset] - opened[offset];
          if (response > saliency[index])
            saliency[index] = response;
        }
      }
    }
  }

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
      if (std::isfinite(value)) {
        value = std::max(0.0, value);
        maximum = std::max(maximum, value);
        output.append(static_cast<float>(value));
      } else {
        output.append(std::numeric_limits<float>::quiet_NaN());
      }
    }
    if (maximum <= 1e-9) return {};
    for (float& value : output) {
      if (std::isfinite(value)) value = static_cast<float>(value / maximum);
    }
    return output;
  }

  static double templateSimilarity(const QVector<float>& expected,
                                   const QVector<float>& observed) {
    if (expected.size() != observed.size() || expected.isEmpty()) return 0.0;
    double squaredError = 0.0;
    int compared = 0;
    for (int index = 0; index < expected.size(); ++index) {
      if (!std::isfinite(observed[index])) continue;
      const double difference = expected[index] - observed[index];
      squaredError += difference * difference;
      ++compared;
    }
    if (compared == 0) return 0.0;
    // Sparse candidates get a mild coverage penalty so a mostly-missing
    // shape cannot outrank a well-supported one, but a few dropouts no
    // longer zero the whole comparison.
    const double coverage = double(compared) / expected.size();
    return std::max(0.0, 1.0 - std::sqrt(squaredError / compared)) *
           (0.6 + 0.4 * coverage);
  }
};
}
