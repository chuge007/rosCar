# OpenCV weld recovery

The live source remains `DeviceController::correctionCameraFrameReady`: an
SDK image decoded at original dimensions, independently of desktop preview
scaling/throttling. No preview point cloud or screenshot enters perception.

The installed OpenCV is **5.0.0**, at `D:/opencv/opencv`, with x64 vc16
`opencv_world500[ d ].lib/.dll` (debug suffix has no space). `opencv.pri` is
shared by the application and tests. Override `OPENCV_ROOT` and
`OPENCV_VERSION_SUFFIX` for another compatible installation. DLL copying is
part of a future requested build; no binary was rebuilt/deployed in this change.
The release script deploys the installed unified VC x64 runtime, including
`vcruntime140_1.dll`, instead of overwriting it with the old VS2017 CRT.
Native link/toolchain compatibility has not been verified by a build.

## Detection

The existing fitted plate baseline and measured dark-gap detector remain.
When there is no measured gap (or only an inferred edge), OpenCV performs
median denoising, Otsu thresholding with a contrast floor, connected-component
filtering, short-hole morphology, and robust Huber line fitting of elevated
stripe points. Geometry always uses the original image coordinates.

A candidate requires real plate shoulders on both sides, an elevated stripe
over at least 40% of the interval, no single missing interval above 40%, and
75% consistent elevated samples. It cannot be a continuous parent stripe with
a reflection above it. Downward/vertical displacement does not qualify as
image-up weld evidence. Position/width association and ambiguous-candidate
rejection remain active. No physical height is inferred from pixel offsets.

Startup/reacquisition requires five consistent contour frames. A live OpenCV
contour updates the tracked position/width so accumulated real travel does not
leave the identity gate anchored at startup. It is not labeled a held or
single-edge observation. The existing full-loss and scan-boundary watchdogs
remain active. No automatic turn-sign reversal or stop-timeout extension is
introduced.

Diagnostics report `opencv_raised_contour`, plus `opencv_contour` and
`allowOpenCvContour` in archived JSON. A valid dark-gap result still reports
`normal_gap`; OpenCV is a geometric recovery path, not a claim of successful
recognition on every frame.

## Evidence and verification limits

The latest supplied log (11:08:50–11:09:12) contains initial missing candidates
and later valid normal-gap observations ending in `containment_not_recovering`.
Those are separate failure modes. Introducing a detector alone does not prove
that steering converges; the existing response-weight reduction is unchanged.

`python tools/verify_opencv_contour.py` uses the installed OpenCV Python binding
for seven offline geometric cases and analyzes the supplied 11:10 CSV. This is
an algorithm experiment, **not execution of the C++/Qt detector**. Its report is
`tmp/opencv_contour_verification.json`. `tests/opencv_laser_contour_tests.pro`
adds native tests for routing, candidate rejection and five-frame confirmation;
they require an explicitly requested build and have not been run.

The CSV contains 37 profiles. The RAW header reports 2048×1086 but the file is a
vendor container; its payload offset/encoding has not been verified. It is not
used as a guessed plain grayscale image. RAW and CSV timestamps differ by
3.704 seconds, so these exports are not treated as synchronized observations.
Reliable real-image replay must use SDK-decoded pixels or the application's
existing lossless PNG frame archive. No real-camera success rate is claimed.
