#!/usr/bin/env python3
"""Compare glretrace snapshot folders (ref vs gl4es): per frame, how many pixels differ; side-by-side PNGs
for the frames that differ. usage: replay_compare.py REFDIR GOTDIR DIFFDIR [tol]"""
import os, sys
import numpy as np
from PIL import Image


def main():
    ref, got, out = sys.argv[1:4]
    tol = int(sys.argv[4]) if len(sys.argv) > 4 else 4
    os.makedirs(out, exist_ok=True)
    frames = sorted(f for f in os.listdir(ref) if f.endswith(('.png', '.ppm')))
    print(f'| snapshot | differing pixels | max diff |\n|--|--|--|')
    bad = 0
    for f in frames:
        g = os.path.join(got, f)
        if not os.path.exists(g):
            print(f'| {f} | missing on gl4es | |'); bad += 1; continue
        A = np.asarray(Image.open(os.path.join(ref, f)).convert('RGBA')).astype(int)
        B = np.asarray(Image.open(g).convert('RGBA')).astype(int)
        if A.shape != B.shape:
            print(f'| {f} | size {A.shape} vs {B.shape} | |'); bad += 1; continue
        d = np.abs(A[..., :3] - B[..., :3]).max(axis=2)
        frac = (d > tol).mean()
        if frac > 0.001:
            bad += 1
            D = np.zeros(A.shape[:2] + (3,), np.uint8); D[d > tol] = (255, 0, 255)
            pic = np.concatenate([A[..., :3], np.full((A.shape[0], 4, 3), 255), B[..., :3], np.full((A.shape[0], 4, 3), 255), D], axis=1)
            Image.fromarray(pic.astype(np.uint8)).save(os.path.join(out, f.rsplit('.', 1)[0] + '.png'))
        print(f'| {f} | {frac * 100:.2f}% | {int(d.max())} |')
    print(f'\n{bad} of {len(frames)} snapshots differ')


if __name__ == '__main__':
    main()
