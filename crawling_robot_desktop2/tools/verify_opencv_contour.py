"""Offline OpenCV experiment; does not compile/run the Qt C++ implementation.

Uses the installed OpenCV Python binding and equivalent geometric gates.
RAW files are inspected as containers, never guessed to be plain pixel arrays.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys

root = Path(__file__).resolve().parents[1]
opencv = Path(os.environ.get('OPENCV_ROOT', 'D:/opencv/opencv')) / 'build'
dll_dir = os.add_dll_directory(str(opencv / 'x64/vc16/bin'))
sys.path.insert(0, str(opencv / ('python/cv2/python-%d.%d' % sys.version_info[:2])))
import cv2
import numpy as np


def detect(gray, offset=160., slope=0., half_width=6., expected_center=None,
           expected_width=None):
    height, width = gray.shape
    smooth = cv2.medianBlur(gray, 3)
    otsu, _ = cv2.threshold(smooth, 0, 255, cv2.THRESH_BINARY | cv2.THRESH_OTSU)
    _, binary = cv2.threshold(smooth, max(24, otsu), 255, cv2.THRESH_BINARY)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(binary, connectivity=8)
    useful = np.zeros(count, bool)
    useful[1:] = ((stats[1:, cv2.CC_STAT_WIDTH] >= max(5, width // 200)) &
                  (stats[1:, cv2.CC_STAT_AREA] >= max(10, width // 100)))
    plate = np.zeros(width, np.uint8)
    raised = np.full(width, -1.)
    tolerance = np.clip(half_width * .5, 4, 12)
    for x in range(width):
        column = labels[:, x]
        cuts = np.r_[0, np.flatnonzero(column[1:] != column[:-1]) + 1, height]
        for a, b in zip(cuts[:-1], cuts[1:]):
            if not useful[column[a]] or b - a > max(12, height // 12):
                continue
            weights = smooth[a:b, x].astype(float)
            y = np.dot(np.arange(a, b), weights) / weights.sum()
            rise = offset + slope * x - y
            if abs(rise) <= tolerance:
                plate[x] = 255
            if max(8., tolerance * 1.5) <= rise <= height * .45:
                raised[x] = max(raised[x], y)
    present = np.flatnonzero(plate)
    if len(present) == 0 or present[-1] - present[0] < width // 4:
        return None
    first, last = int(present[0]), int(present[-1])
    closed = cv2.morphologyEx(plate[None, :], cv2.MORPH_CLOSE,
        np.ones((1, max(3, (width // 400) | 1)), np.uint8),
        borderType=cv2.BORDER_CONSTANT, borderValue=0)[0]
    transitions = np.diff(np.r_[False, closed[first:last+1] == 0, False].astype(int))
    candidates = []
    shoulder = max(8, width // 100)
    for a, b in zip(np.flatnonzero(transitions == 1), np.flatnonzero(transitions == -1)):
        a, b = int(a+first), int(b+first-1)
        length = b-a+1
        if length < max(12, int(width*.02)) or a-first < shoulder or last-b < shoulder:
            continue
        if min(np.count_nonzero(plate[max(first,a-2*shoulder):a]),
               np.count_nonzero(plate[b+1:min(last+1,b+1+2*shoulder)])) < shoulder:
            continue
        xs = np.flatnonzero(raised[a:b+1] >= 0) + a
        coverage = len(xs)/length
        holes = np.diff(np.r_[a-1, xs, b+1])-1
        if coverage < .4 or holes.max() > length*.4:
            continue
        points = np.column_stack((xs, raised[xs])).astype(np.float32)
        vx, vy, cx, cy = cv2.fitLine(points, cv2.DIST_HUBER, 0, .01, .01).ravel()
        if abs(vx) < 1e-6 or abs(vy/vx - slope) > .3:
            continue
        residual = np.abs(points[:,1] - (cy + vy/vx * (points[:,0]-cx)))
        inliers = np.count_nonzero(residual <= max(6., (offset+slope*cx-cy)*.12))
        if inliers < len(xs)*.75:
            continue
        center = (a+b)*.5/(width-1)
        if expected_center is not None and abs(center-expected_center) > .06:
            continue
        if expected_width is not None and abs(length/(width-1)-expected_width) > .08:
            continue
        score = length*coverage / (1 if expected_center is None else 1+8*abs(center-expected_center))
        candidates.append(dict(start=a, end=b, center=center, score=score, coverage=coverage))
    candidates.sort(key=lambda item: item['score'], reverse=True)
    if not candidates or (len(candidates)>1 and candidates[1]['score']>candidates[0]['score']*.85):
        return None
    return candidates[0]


def weld(parent=False, downward=False, end=760):
    gray = np.full((240,800), 12, np.uint8)
    gray[158:163,20:781] = 210
    if not parent:
        gray[158:163,420:end+1] = 12
    top = 205 if downward else 95
    gray[top-2:top+3,420:end+1] = 235
    gray[top-2:top+3,480:541] = 12
    return gray


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default=str(root/'tmp/opencv_contour_verification.json'))
    args = parser.parse_args()
    tests = {
        'fragmented_short_shoulder': (weld(), {}, True),
        'blank': (np.full((240,800),12,np.uint8), {}, False),
        'reflection_with_parent': (weld(parent=True), {}, False),
        'downward': (weld(downward=True), {}, False),
        'clipped_shoulder': (weld(end=780), {}, False),
        'wrong_identity': (weld(), {'expected_center': .2}, False),
        'wrong_width': (weld(), {'expected_width': .1}, False),
    }
    results = {}
    for name, (gray, kwargs, expected) in tests.items():
        result = detect(gray, **kwargs)
        assert bool(result) == expected, (name, result)
        results[name] = result
    contour = results['fragmented_short_shoulder']
    assert abs(contour['start']-420)<=2 and abs(contour['end']-760)<=2
    # Independently inspect supplied physical profiles; do not map their X
    # into image pixels without calibration or assume these files are synced.
    csv = root/'release/logs/Image_Profile_20260920_11_10_41_929.csv'
    points = np.loadtxt(csv, delimiter=',')
    valid = points[np.isfinite(points).all(axis=1) & np.any(points != 0, axis=1)]
    resets = np.flatnonzero(np.diff(valid[:,0]) < -1000)+1
    frames = np.split(valid, resets)
    summaries = []
    for frame in frames:
        good = frame[frame[:,2] > 0]
        if len(good) < 20: continue
        baseline = float(np.median(good[:,2]))
        raised = good[good[:,2] > baseline+1500]
        summaries.append(dict(points=len(frame), baseline=baseline,
            raised_points=len(raised),
            raised_x_range=[float(raised[:,0].min()),float(raised[:,0].max())] if len(raised) else None))
    raw = root/'release/logs/Image_Origin_20260920_11_10_45_633.raw'
    data = raw.read_bytes()
    report = dict(opencv=cv2.__version__, synthetic_cases=results,
        cpp_tests_executed=False, profiles=summaries,
        raw=dict(bytes=len(data), header_dimensions=struct.unpack_from('<II',data,0x70),
                 decoded=False, reason='Vendor container; no verified RAW decoder. SDK live frames are decoded separately.'),
        files_synchronized=False, timestamp_difference_seconds=3.704)
    Path(args.output).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('OpenCV',cv2.__version__,':',len(tests),'offline cases passed; profiles:',len(summaries))
    print('Report:',args.output)


if __name__ == '__main__':
    main()
