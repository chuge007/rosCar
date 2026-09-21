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
    for (int i = 0; i < n; ++i) {
      const auto& p = points[i];
      if (!std::isfinite(p.x()) || !std::isfinite(p.z()) || p.z() <= 0) continue;
      if (!valid.empty() && p.x() <= points[valid.back()].x()) continue;
      if (!valid.empty() && i == valid.back() + 1)
        differences.push_back(p.z() - points[valid.back()].z());
      valid.push_back(i);
    }
    if (valid.size() < 24 || differences.size() < 12) return result;
    const double step = median(differences);
    for (double& d : differences) d = std::abs(d - step);
    // Units cancel: never assume the exported CSV and SDK share a height unit.
    double noise = std::max(1e-6, 1.4826 * median(differences) / std::sqrt(2.0));
    const int edgeCount = std::max(6, int(valid.size() / 10));
    std::vector<double> leftX, leftZ, rightX, rightZ;
    for (int k = 0; k < edgeCount; ++k) {
      const auto& l = points[valid[k]];
      const auto& r = points[valid[valid.size() - 1 - k]];
      leftX.push_back(l.x()); leftZ.push_back(l.z());
      rightX.push_back(r.x()); rightZ.push_back(r.z());
    }
    const double lx = median(leftX), rx = median(rightX);
    if (rx <= lx) return result;
    double slope = (median(rightZ) - median(leftZ)) / (rx - lx);
    double offset = median(leftZ) - slope * lx;
    // Fit only supported parent points, iteratively excluding raised regions
    // and downward dropouts. Shoulder initialization avoids fitting the peak.
    for (int pass = 0; pass < 4; ++pass) {
      std::vector<double> errors;
      for (int k = 0; k < edgeCount; ++k) {
        for (int i : {valid[k],valid[valid.size()-1-k]})
          errors.push_back(points[i].z()-offset-slope*points[i].x());
      }
      const double bias = median(errors);
      for (double& e : errors) e = std::abs(e-bias);
      const double parentNoise = std::max(noise,1.4826*median(errors));
      offset += bias;
      double sx = 0, sz = 0, sxx = 0, sxz = 0, count = 0;
      for (int i : valid) {
        const double x = points[i].x() - lx;
        const double z = points[i].z();
        if (std::abs(z - offset - slope * points[i].x()) > parentNoise * 3) continue;
        sx += x; sz += z; sxx += x*x; sxz += x*z; ++count;
      }
      const double denominator = count*sxx - sx*sx;
      if (count < 12 || denominator <= 1e-12) break;
      slope = (count*sxz - sx*sz) / denominator;
      offset = (sz - slope*sx) / count - slope*lx;
    }
    std::vector<double> deviations;
    for (int k = 0; k < edgeCount; ++k) {
      for (int i : {valid[k],valid[valid.size()-1-k]})
        deviations.push_back(std::abs(points[i].z()-offset-slope*points[i].x()));
    }
    noise = std::max(noise,1.4826*median(deviations));
    result.profileNoise = noise;
    result.profileBaselineSlope = slope;
    result.profileBaselineOffset = offset;
    const double grow = noise * 3, seed = noise * 5;
    std::vector<double> residual(n, std::numeric_limits<double>::quiet_NaN());
    for (int i : valid) residual[i] = points[i].z() - offset - slope*points[i].x();
    const int minWidth = std::max(6, int(std::ceil(n*config.minimumGapRatio)));
    const int maxHole = std::max(2, std::min(32, n/64));
    const int shoulder = std::max(4, n/200);
    double best = 0, runnerUp = 0;
    for (int i = valid.front(); i <= valid.back();) {
      if (!(residual[i] > grow)) { ++i; continue; }
      const int start = i;
      int end = i, lastRaised = i, support = 0, seeds = 0, longestHole = 0, parentRun = 0;
      double area = 0;
      for (; i <= valid.back(); ++i) {
        if (residual[i] > grow) {
          parentRun = 0;
          longestHole = std::max(longestHole, i-lastRaised-1);
          lastRaised = end = i;
          ++support;
          if (residual[i] > seed) ++seeds;
          area += std::min(residual[i], seed*10);
        } else {
          parentRun = std::abs(residual[i]) <= grow ? parentRun+1 : 0;
          if (i-lastRaised > maxHole || parentRun >= shoulder) break;
        }
      }
      const int length = end-start+1;
      if (length < minWidth || seeds < 3 || support < length*.70 ||
          longestHole > length*.20) continue;
      int left = 0, right = 0;
      // Both shoulders must return to the same tilted parent baseline.
      for (int j = 1; j <= shoulder*3; ++j) {
        if (start-j >= 0 && std::abs(residual[start-j]) <= grow) ++left;
        if (end+j < n && std::abs(residual[end+j]) <= grow) ++right;
      }
      if (std::min(left,right) < shoulder) continue;
      const double center = (start+end)*.5/(n-1);
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
          std::abs(width-config.expectedAbsoluteGapWidthRatio) >
              config.maximumTrackingGapWidthJumpRatio;
      double score = area / seed;
      if (widthJump) score *= 0.45;
      if (config.expectedAbsoluteCenterRatio >= 0)
        score /= 1 + 12*std::abs(center-config.expectedAbsoluteCenterRatio);
      if (score <= best) { runnerUp = std::max(runnerUp,score); continue; }
      runnerUp = best; best = score;
      result.valid = result.baselineSupported = result.contourSupported = true;
      result.gapStartPx = result.contourStartPx = start;
      result.gapEndPx = result.contourEndPx = end;
      result.lineStartPx = valid.front(); result.lineEndPx = valid.back();
      // Use footprint midpoint, not the highest point: asymmetric peaks must
      // not move the steering target when their height changes.
      result.absoluteCenterRatio = center;
      result.normalizedCenter = ((start+end)*.5-valid.front())/(valid.back()-valid.front());
      result.confidence = result.contourConfidence = .5 + .4*support/length;
      result.supportingSamples = support;
    }
    if (best > 0 && runnerUp > best*.85) result.valid = false;
    if (result.valid) result.continuityRejected = result.widthRejected = false;
    return result;
  }
};
}
