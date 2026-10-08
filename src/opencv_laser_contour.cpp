#include "opencv_laser_contour.h"
#include <opencv2/core.hpp>
#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
// OpenCV 5 moved fitLine and DistanceTypes out of imgproc.
#include <opencv2/geometry/2d.hpp>
#endif
#include <algorithm>
#include <cmath>
#include <vector>

namespace crawling {
LaserGapDetection OpenCvLaserContour::detect(
    const QImage& image, double offset, double slope, double halfWidth,
    const LaserGapDetectorConfig& config) {
  LaserGapDetection result;
  if (image.format() != QImage::Format_Grayscale8 || image.width() < 48 ||
      image.height() < 24 || !std::isfinite(offset) || !std::isfinite(slope)) return result;
  try {
    // QImage owns this storage throughout the call. Never resize or mutate it.
    const cv::Mat source(image.height(), image.width(), CV_8UC1,
        const_cast<uchar*>(image.constBits()), image.bytesPerLine());
    cv::Mat smooth, binary, labels, stats, centroids;
    cv::medianBlur(source, smooth, 3);
    const double otsu = cv::threshold(smooth, binary, 0, 255,
                                     cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::threshold(smooth, binary, std::max(double(config.minimumContrast), otsu),
                  255, cv::THRESH_BINARY);
    const int count = cv::connectedComponentsWithStats(binary, labels, stats,
                                                       centroids, 8, CV_32S);
    const int width = image.width();
    std::vector<bool> useful(count, false);
    for (int i = 1; i < count; ++i) {
      useful[i] = stats.at<int>(i, cv::CC_STAT_WIDTH) >= std::max(5, width / 200) &&
                  stats.at<int>(i, cv::CC_STAT_AREA) >= std::max(10, width / 100);
    }
    cv::Mat plate = cv::Mat::zeros(1, width, CV_8UC1);
    std::vector<double> raised(width, -1.0);
    std::vector<std::vector<double>> ridges(width);
    std::vector<double> parentResiduals;
    const double parentWindow = std::clamp(halfWidth * 0.5, 4.0, 12.0);
    for (int x = 0; x < width; ++x) {
      const double baseline = offset + slope * x;
      for (int y = 0; y < image.height();) {
        const int label = labels.at<int>(y, x);
        if (!useful[label]) { ++y; continue; }
        double weight = 0.0, moment = 0.0;
        const int start = y;
        while (y < image.height() && labels.at<int>(y, x) == label) {
          const double value = smooth.at<uchar>(y, x);
          weight += value;
          moment += value * y++;
        }
        if (weight <= 0 || y - start > std::max(12, image.height() / 12)) continue;
        const double center = moment / weight;
        ridges[x].push_back(center);
      }
      double nearest = parentWindow + 1.0;
      for (double center : ridges[x])
        if (std::abs(baseline - center) < std::abs(nearest)) nearest = baseline - center;
      if (std::abs(nearest) <= parentWindow) parentResiduals.push_back(nearest);
    }
    if (parentResiduals.size() < size_t(std::max(24, width / 20))) return result;
    // Stripe thickness is not centroid noise. Estimate noise from the parent
    // stripe so a clean, shallow displacement can still form a real gap.
    // Keep the externally fitted baseline: a wide shallow weld can occupy
    // most near-baseline samples. Use the closest quartile for parent noise.
    for (double& residual : parentResiduals) residual = std::abs(residual);
    std::sort(parentResiduals.begin(), parentResiduals.end());
    const double noise = 1.4826 * parentResiduals[parentResiduals.size() / 4];
    const double tolerance = std::max(1.5, noise * 3.0);
    const double minimumRise = std::max(3.0, tolerance + std::max(1.0, noise));
    for (int x = 0; x < width; ++x) {
      for (double center : ridges[x]) {
        const double rise = offset + slope * x - center;
        if (std::abs(rise) <= tolerance) plate.at<uchar>(0, x) = 255;
        if (rise >= minimumRise && rise <= image.height() * 0.45 &&
            (raised[x] < 0 || center > raised[x])) raised[x] = center;
      }
    }
    // Bridge only very short breaks in the plate, never the weld itself.
    cv::Mat closed;
    const int holeKernel = std::max(3, (width / 400) | 1);
    cv::morphologyEx(plate, closed, cv::MORPH_CLOSE,
        cv::getStructuringElement(cv::MORPH_RECT, cv::Size(holeKernel, 1)),
        cv::Point(-1, -1), 1, cv::BORDER_CONSTANT, cv::Scalar(0));
    int first = -1, last = -1;
    for (int x = 0; x < width; ++x) if (plate.at<uchar>(0, x)) {
      if (first < 0) first = x;
      last = x;
    }
    if (last - first < width / 4) return result;
    const int shoulder = std::max(8, width / 100);
    double best = 0.0, runnerUp = 0.0;
    for (int x = first; x <= last;) {
      if (closed.at<uchar>(0, x)) { ++x; continue; }
      const int start = x;
      while (x <= last && !closed.at<uchar>(0, x)) ++x;
      const int end = x - 1, length = end - start + 1;
      if (length < std::max(6, int(std::ceil(width * config.minimumGapRatio))) ||
          start - first < shoulder || last - end < shoulder) continue;
      int leftSupport = 0, rightSupport = 0;
      for (int j = 1; j <= shoulder * 2; ++j) {
        if (start - j >= first && plate.at<uchar>(0, start - j)) ++leftSupport;
        if (end + j <= last && plate.at<uchar>(0, end + j)) ++rightSupport;
      }
      if (std::min(leftSupport, rightSupport) < shoulder) continue;
      std::vector<cv::Point2f> points;
      int hole = 0, longestHole = 0;
      for (int j = start; j <= end; ++j) {
        if (raised[j] >= 0) {
          points.emplace_back(float(j), float(raised[j]));
          hole = 0;
        } else longestHole = std::max(longestHole, ++hole);
      }
      const double coverage = double(points.size()) / length;
      if (coverage < 0.40 || longestHole > length * 0.40) continue;
      // A broad displaced ridge can be fragmented by reflection/dropout.
      // Fit the real samples, never manufacture heights across missing pixels.
      cv::Vec4f fitted;
      cv::fitLine(points, fitted, cv::DIST_HUBER, 0, 0.01, 0.01);
      if (std::abs(fitted[0]) < 1e-6) continue;
      const double topSlope = fitted[1] / fitted[0];
      if (std::abs(topSlope - slope) > 0.30) continue;
      int inliers = 0;
      const double rise = offset + slope * fitted[2] - fitted[3];
      const bool weakGeometry = rise < 8.0 || length < width * 0.02;
      if (rise < minimumRise ||
          (weakGeometry && (coverage < 0.80 || longestHole > length * 0.15))) continue;
      const double fitTolerance = weakGeometry ? std::max(1.0, noise * 2.0) :
                                                 std::max(6.0, rise * 0.12);
      for (const auto& point : points) {
        if (std::abs(point.y - (fitted[3] + topSlope * (point.x - fitted[2]))) <=
            fitTolerance) ++inliers;
      }
      if (inliers < points.size() * (weakGeometry ? 0.90 : 0.75)) continue;
      const double center = (start + end) * 0.5 / (width - 1);
      const double normalizedWidth = double(length) / (width - 1);
      if (config.expectedAbsoluteCenterRatio >= 0 &&
          std::abs(center - config.expectedAbsoluteCenterRatio) >
              config.maximumAbsoluteCenterJumpRatio) continue;
      if (config.referenceAbsoluteCenterRatio >= 0 &&
          std::abs(center - config.referenceAbsoluteCenterRatio) >
              config.maximumReferenceCenterDriftRatio) continue;
      if (config.expectedAbsoluteGapWidthRatio > 0 &&
          std::abs(normalizedWidth - config.expectedAbsoluteGapWidthRatio) >
              config.maximumTrackingGapWidthJumpRatio) continue;
      double score = length * coverage;
      if (config.expectedAbsoluteCenterRatio >= 0)
        score /= 1.0 + 8.0 * std::abs(center - config.expectedAbsoluteCenterRatio);
      if (score <= best) { runnerUp = std::max(runnerUp, score); continue; }
      runnerUp = best;
      best = score;
      result.valid = result.baselineSupported = true;
      result.contourFallback = result.contourSupported = true;
      result.opencvContour = true;
      result.gapStartPx = result.contourStartPx = start;
      result.gapEndPx = result.contourEndPx = end;
      result.lineStartPx = first;
      result.lineEndPx = last;
      result.absoluteCenterRatio = center;
      result.normalizedCenter = ((start + end) * 0.5 - first) / (last - first);
      result.baselineOffsetPx = offset;
      result.baselineSlope = slope;
      result.baselineHalfWidthPx = halfWidth;
      result.confidence = result.contourConfidence = std::clamp(
          0.10 + 0.15 * coverage * double(inliers) / points.size(), 0.10, 0.25);
      result.supportingSamples = int(points.size());
    }
    // Ambiguous disconnected raised regions do not establish a new identity.
    if (best > 0 && runnerUp > best * 0.85) return {};
    return result;
  } catch (const cv::Exception&) {
    // A failed library operation supplies no observation to the controller.
    return {};
  }
}
}
