"""Flicker metric for a recording: counts frames that differ strongly from both neighbours while the neighbours
resemble each other (a frame 'popping' out of sequence). Usage: flicker.py VIDEO"""
import subprocess, sys
import numpy as np

W, H = 160, 100
raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", sys.argv[1], "-vf", f"scale={W}:{H},format=gray", "-f", "rawvideo", "-"],
                     capture_output=True, check=True).stdout
frames = np.frombuffer(raw, np.uint8).reshape(-1, H, W).astype(np.float32)
d = lambda a, b: float(np.abs(frames[a] - frames[b]).mean())
steps = [d(i, i + 1) for i in range(len(frames) - 1)]
spikes = 0
for i in range(1, len(frames) - 1):
    a, b, c = d(i - 1, i), d(i, i + 1), d(i - 1, i + 1)
    if a > 6 and b > 6 and c < 0.5 * min(a, b):
        spikes += 1
dupes = sum(1 for s in steps if s < 0.05)
print(f"{len(frames)} frames, flicker spikes {spikes}, duplicate frames {dupes}, mean step {np.mean(steps):.2f}, max step {max(steps):.1f}")
