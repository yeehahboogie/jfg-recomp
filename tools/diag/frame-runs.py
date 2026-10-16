"""How many distinct pictures a 60 fps recording holds, and how many last a single frame (in-between frames drawn by
frame interpolation; with interpolation off, each game frame repeats for two or more). Usage: frame-runs.py VIDEO"""
import subprocess
import sys

import numpy as np

raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", sys.argv[1], "-vf", "scale=160:100,format=gray",
                      "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
f = np.frombuffer(raw, np.uint8).reshape(-1, 100, 160).astype(np.float32)
runs, n = [], 1
for i in range(1, len(f)):
    if np.abs(f[i] - f[i - 1]).mean() < 0.05:
        n += 1
    else:
        runs.append(n)
        n = 1
runs.append(n)
print(f"{len(f)} frames, {len(runs)} distinct pictures, single-frame pictures {sum(1 for r in runs if r == 1)}")
