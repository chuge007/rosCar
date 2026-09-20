"""Numerical prototype of ProfileWeldDetector; NOT execution of the Qt binary.
CSV retains original scan order and zero/invalid slots. Z orientation is up.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def median(values):
    return float(np.partition(values, len(values)//2)[len(values)//2]) if len(values) else 0.


def detect(p):
    n = len(p)
    valid, diffs = [], []
    for i, (x, _, z) in enumerate(p):
        if not np.isfinite(x+z) or z <= 0:
            continue
        if valid and x <= p[valid[-1], 0]:
            continue
        if valid and i == valid[-1]+1:
            diffs.append(z-p[valid[-1], 2])
        valid.append(i)
    if n < 32 or len(valid) < 24 or len(diffs) < 12:
        return None
    noise = max(1e-6, 1.4826*median(np.abs(np.array(diffs)-median(diffs)))/np.sqrt(2))
    ec = max(6, len(valid)//10)
    lx, rx = median(p[valid[:ec], 0]), median(p[valid[-ec:], 0])
    if rx <= lx:
        return None
    slope = (median(p[valid[-ec:], 2])-median(p[valid[:ec], 2]))/(rx-lx)
    offset = median(p[valid[:ec], 2])-slope*lx
    for _ in range(4):
        edges = valid[:ec]+valid[-ec:]
        errors = p[edges,2]-offset-slope*p[edges,0]
        bias = median(errors)
        parent_noise = max(noise,1.4826*median(np.abs(errors-bias)))
        offset += bias
        keep = [i for i in valid if abs(p[i, 2]-offset-slope*p[i, 0]) <= parent_noise*3]
        x, z = p[keep, 0]-lx, p[keep, 2]
        count = len(keep)
        den = count*np.dot(x,x)-sum(x)**2
        if count < 12 or den <= 1e-12:
            break
        slope = (count*np.dot(x,z)-sum(x)*sum(z))/den
        offset = (sum(z)-slope*sum(x))/count-slope*lx
    noise = max(noise,1.4826*median(np.abs(p[edges,2]-offset-slope*p[edges,0])))
    grow, seed = noise*3, noise*5
    residual = np.full(n, np.nan)
    residual[valid] = p[valid,2]-offset-slope*p[valid,0]
    shoulder = max(4, n//200)
    max_hole = max(2,min(32,n//64))
    candidates = []
    i = valid[0]
    while i <= valid[-1]:
        if not residual[i] > grow:
            i += 1
            continue
        start = end = last = i
        support = seeds = longest = parent_run = 0
        area = 0.
        while i <= valid[-1]:
            if residual[i] > grow:
                parent_run = 0
                longest = max(longest,i-last-1)
                last = end = i
                support += 1
                seeds += residual[i] > seed
                area += min(residual[i],seed*10)
            else:
                parent_run = parent_run+1 if abs(residual[i]) <= grow else 0
                if i-last > max_hole or parent_run >= shoulder:
                    break
            i += 1
        length = end-start+1
        if length < max(6,int(np.ceil(n*.006))) or seeds < 3 or support < length*.7 or longest > length*.2:
            continue
        left = sum(abs(residual[j]) <= grow for j in range(max(0,start-shoulder*3),start))
        right = sum(abs(residual[j]) <= grow for j in range(end+1,min(n,end+1+shoulder*3)))
        if min(left,right) < shoulder:
            continue
        candidates.append(dict(start=int(start),end=int(end),center=(start+end)*.5/(n-1),
                               score=float(area/seed),noise=float(noise)))
    candidates.sort(key=lambda c: c['score'],reverse=True)
    if not candidates or (len(candidates)>1 and candidates[1]['score']>candidates[0]['score']*.85):
        return None
    return candidates[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    cases = {}
    x = np.arange(800,dtype=float)
    rng = np.random.RandomState(2026)
    for width in (10,30,120,420):
        for height in (1.,5.,80.):
            for shape in ('plateau','mountain','floating'):
                p = np.column_stack((x,np.zeros(800),100+.02*x+rng.normal(0,.03,800)))
                a,b = 190,190+width
                bump = np.ones(width) if shape != 'mountain' else np.sin(np.linspace(.1,np.pi-.1,width))
                p[a:b,2] += height*bump
                if shape == 'floating' and width >= 30:
                    p[a+width//2:a+width//2+2,2] = np.nan
                r = detect(p)
                assert r and abs(r['center']-(a+b-1)*.5/799)<.015, (width,height,shape,r)
                cases['%s_w%d_h%g' % (shape,width,height)] = r
    for shape in ('flat','spike','downward','two_equal'):
        p = np.column_stack((x,np.zeros(800),100+.02*x+rng.normal(0,.03,800)))
        if shape == 'spike': p[400,2] += 20
        if shape == 'downward': p[300:400,2] -= 20
        if shape == 'two_equal':
            p[200:250,2] += 10
            p[550:600,2] += 10
        assert detect(p) is None, shape
        cases[shape] = None
    for kind in ('missing','downward_outlier'):
        xx = np.arange(2048,dtype=float)
        p = np.column_stack((xx,np.zeros(2048),100+.02*xx+.02*np.sin(xx*.7)))
        p[750:1420,2] += 5
        p[1200:1221,2] = np.nan if kind == 'missing' else 10.
        p[1310:1335,2] = np.nan if kind == 'missing' else 10.
        r=detect(p)
        assert r and r['start']==750 and r['end']==1419,(kind,r)
        cases[kind]=r
    p = np.loadtxt(args.csv,delimiter=',')
    # CSV exports contiguous scans followed by zero padding in this dataset.
    resets = np.flatnonzero((np.diff(p[:,0]) < -1000) & (p[1:,2]>0))+1
    frames = np.split(p,resets)
    frames = [f[:np.flatnonzero(np.any(f!=0,axis=1))[-1]+1] for f in frames if np.any(f!=0)]
    results = [detect(f) for f in frames]
    Path(args.output).write_text(json.dumps(dict(synthetic=cases,profiles=results,
        cpp_executed=False, ground_truth_labeled=False),indent=2),encoding='utf-8')
    print('Synthetic:',len(cases),'passed; CSV candidates:',sum(r is not None for r in results),'/',len(results))
    print(results[:3])


if __name__ == '__main__':
    main()
