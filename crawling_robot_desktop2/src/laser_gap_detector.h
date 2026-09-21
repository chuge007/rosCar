#pragma once

#include <QImage>
#include <QMetaType>
#include <QVector>

namespace crawling {

struct LaserGapDetectorConfig {
  double minimumGapRatio = 0.006;
  double minimumSideSupportRatio = 0.03;
  int minimumContrast = 24;
  int maximumHoleLength = 2;
  // Fill short dark interruptions before gap extraction. This scales with
  // the raw-image axis, so sensor speckle and broken laser pixels do not
  // become competing weld-gap candidates at higher camera resolutions.
  double smallGapFillRatio = 0.012;
  int minimumRunLength = 5;
  // Only gaps comparable with the largest supported gap are eligible for
  // tracking. Temporal continuity then chooses between similarly sized gaps
  // instead of allowing a well-balanced small defect to beat the main gap.
  double dominantGapMinimumRatio = 0.60;
  // When set, prefer the same gap as the previous frame. Ratios are measured
  // inside the detected laser-line span, not against the full image width.
  double expectedCenterRatio = -1.0;
  double expectedGapWidthRatio = -1.0;
  double maximumTrackingCenterJumpRatio = 0.10;
  // Full-image ratios provide a stable coordinate system when the detected
  // laser-line endpoints change because of reflections or partial occlusion.
  double expectedAbsoluteCenterRatio = -1.0;
  // Gap width in the full raw-image axis. This is intentionally separate from
  // expectedGapWidthRatio, which is relative to the currently visible laser
  // span. When an optical end is clipped, the visible-span ratio changes even
  // though the physical weld width has not changed.
  double expectedAbsoluteGapWidthRatio = -1.0;
  double referenceAbsoluteCenterRatio = -1.0;
  // Keep the full-image continuity gate strict even at an optical edge. A
  // clipped edge is handled by inferFromVisibleEdge(), but it still must move
  // continuously from the previous raw-image position.
  double maximumAbsoluteCenterJumpRatio = 0.06;
  // Optional stationary-scene reference gate for external callers. Vehicle
  // correction leaves referenceAbsoluteCenterRatio disabled in every phase:
  // accumulated motion across a slanted seam is a valid observation.
  double maximumReferenceCenterDriftRatio = 0.10;
  // Prefer the candidate nearest to the previous raw-image center when more
  // than one bright run pair passes the local continuity gates.
  double trackingAbsoluteCenterWeight = 2.5;
  double trackingWidthWeight = 1.5;
  // If one weld edge is occluded or broken, the visible side can be used with
  // the previous line span/gap geometry to keep the trajectory continuous.
  bool allowEdgeBreakFallback = true;
  // The baseline interruption and its two measured edges are the normal
  // weld locator. Displaced ridges are disabled by default; optional use is
  // limited to low-confidence recovery when no intensity gap is available.
  bool allowDisplacedContourFallback = false;
  // OpenCV checks a fragmented elevated stripe between measured shoulders.
  // This is independent of the older strict/continuous contour fallback.
  bool allowOpenCvContour = true;
  double edgeBreakGapRatio = 0.04;
  // At a laser boundary only a short portion of one side may remain visible.
  double minimumEdgeBreakRunRatio = 0.025;
  double maximumTrackingGapWidthJumpRatio = 0.08;
  // Ratios against the full image axis, supplied by the previous frame.
  double expectedLineStartRatio = -1.0;
  double expectedLineEndRatio = -1.0;
  // 0 = no axis lock, 1 = horizontal laser line, 2 = vertical laser line.
  // Once tracking starts, locking the axis prevents a noisy fallback profile
  // from changing the coordinate system between frames.
  int expectedAxis = 0;
};

struct LaserGapDetection {
  bool valid = false;
  bool horizontal = true;
  // Geometry of the broadly supported original laser stripe. Its normal
  // coordinate is offset + slope * scan-axis pixel; a weld is an interruption
  // of this stripe even when displaced reflections remain bright there.
  bool baselineSupported = false;
  double baselineOffsetPx = 0.0;
  double baselineSlope = 0.0;
  double baselineHalfWidthPx = 0.0;
  // normalizedCenter is relative to [lineStartPx, lineEndPx]. The absolute
  // value is relative to the full raw image axis and is stable when the
  // visible laser span is clipped by glare or occlusion.
  double normalizedCenter = 0.5;
  double absoluteCenterRatio = 0.5;
  double confidence = 0.0;
  int gapStartPx = -1;
  int gapEndPx = -1;
  int lineStartPx = -1;
  int lineEndPx = -1;
  int supportingSamples = 0;
  bool continuityRejected = false;
  bool edgeBreakFallback = false;
  // Auxiliary geometry from original camera pixels. OpenCV recovery is on
  // by default; the older strict continuous-ridge fallback remains opt-in.
  // A ridge never replaces a valid measured gap. Pixel displacement is not
  // calibrated physical height.
  bool contourSupported = false;
  bool contourFallback = false;
  bool opencvContour = false;
  bool profileContour = false; // SDK X/Z scan; *Px fields denote scan indices.
  double profileNoise = 0.0; // Native Z units, not pixel height or probability.
  double profileBaselineSlope = 0.0;
  double profileBaselineOffset = 0.0;
  bool contourAgreesWithGap = false;
  bool contourConflict = false;
  // Retained for source compatibility. Auxiliary contours are never dominant.
  bool contourDominant = false;
  double contourConfidence = 0.0;
  int contourStartPx = -1;
  int contourEndPx = -1;
  bool widthRejected = false;
};

// A compact, detector-facing representation of one original camera frame.
// rawProfile is the exact baseline-band intensity projection used by the gap
// detector; filteredProfile and presentProfile are its preprocessing stages.
// fullProjectionProfile preserves the former whole-image projection, so a
// displaced reflection can be distinguished from light on the baseline.
// Keeping this data separate from LaserGapDetection avoids treating diagnostic
// samples as measurements while still allowing a logged frame to be replayed.
struct LaserRawFrameDiagnostic {
  bool available = false;
  bool horizontal = true;
  int imageWidth = 0;
  int imageHeight = 0;
  int sampleStepPx = 1;
  int backgroundLevel = 0;
  int lineLevel = 0;
  int threshold = -1;
  int maximumHoleSamples = 0;
  int minimumRunSamples = 0;
  bool baselineUsed = false;
  double baselineOffsetPx = 0.0;
  double baselineSlope = 0.0;
  double baselineHalfWidthPx = 0.0;
  double baselineSupportRatio = 0.0;
  double baselineResidualPx = 0.0;
  QVector<int> fullProjectionProfile;
  // Measured cross-axis ridge coordinate per sample, -1 for an absent ridge.
  QVector<int> ridgeCrossPx;
  QVector<int> rawProfile;
  QVector<int> filteredProfile;
  QVector<bool> presentProfile;
  // Samples with a persistent ridge along the baseline's image-up normal.
  // Downward returns and dark holes are not raised-contour measurements.
  QVector<bool> contourProfile;
  int contourMinimumRunSamples = 0;
  int contourDisplacementThresholdPx = 0;
};

class LaserGapDetector final {
 public:
  static LaserGapDetection detect(const QImage& image,
                                  const LaserGapDetectorConfig& config = {},
                                  LaserRawFrameDiagnostic* diagnostic = nullptr);
};

}  // namespace crawling

Q_DECLARE_METATYPE(crawling::LaserGapDetection)
