"""Find bursts of static in a sound capture: 0.1 s windows that are loud and have a high zero-crossing rate.
Usage: audio-scan.py FILE...   (FILE is a JFG_AUDIO_DUMP .raw, signed 16-bit stereo at 22020 Hz, or a .wav)"""
import sys, wave
import numpy as np

for path in sys.argv[1:]:
    if path.endswith('.wav'):
        w = wave.open(path)
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')
    else:
        rate = 22020
        data = np.fromfile(path, dtype='<i2')
    left = data[0::2].astype(np.float32) / 32768.0
    win = int(0.1 * rate)
    n = len(left) // win
    w = left[:n * win].reshape(n, win)
    rms = np.sqrt((w ** 2).mean(axis=1))
    zcr = (np.diff(np.sign(w), axis=1) != 0).mean(axis=1)
    noisy = np.where((rms > 0.15) & (zcr > 0.3))[0]
    bursts = []
    for i in noisy:
        if bursts and i - bursts[-1][1] <= 2:
            bursts[-1][1] = i
        else:
            bursts.append([i, i])
    print(f"{path}: {n / 10:.1f} s, {len(bursts)} bursts")
    for a, b in bursts:
        seg = slice(a, b + 1)
        print(f"  {a / 10:7.1f}-{(b + 1) / 10:7.1f} s  rms {rms[seg].max():.2f}  zcr {zcr[seg].max():.2f}")
