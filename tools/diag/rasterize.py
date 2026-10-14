"""Render dumped clip-space triangles with near clipping and a z-buffer. Usage: rasterize.py TRIS OUT.png FIRST COUNT"""
import sys
import numpy as np
from PIL import Image

path, out, first, count = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
W, H, NEAR = 640, 480, 1e-2
color = np.zeros((H, W, 3), np.uint8)
depth = np.full((H, W), np.inf)
ys, xs = np.mgrid[0:H, 0:W]

def clip(poly):  # clip against w >= NEAR
    res = []
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        ina, inb = a[3] >= NEAR, b[3] >= NEAR
        if ina: res.append(a)
        if ina != inb:
            t = (NEAR - a[3]) / (b[3] - a[3])
            res.append(a + t * (b - a))
    return res

lines = open(path).read().splitlines()[first:first + count]
drawn = 0
for line in lines:
    f = line.split()
    if len(f) < 15: continue
    verts = [np.array([float(x) for x in f[i:i + 4]]) for i in (0, 5, 10)]
    col = np.mean([[(int(f[i + 4], 16) >> s) & 255 for s in (24, 16, 8)] for i in (0, 5, 10)], axis=0)
    poly = clip(verts)
    if len(poly) < 3: continue
    pts = [((p[0] / p[3] * 0.5 + 0.5) * W, (0.5 - p[1] / p[3] * 0.5) * H, p[2] / p[3]) for p in poly]
    for k in range(1, len(pts) - 1):
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts[0], pts[k], pts[k + 1]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(area) < 1e-9: continue
        xa, xb = int(max(0, np.floor(min(x0, x1, x2)))), int(min(W - 1, np.ceil(max(x0, x1, x2))))
        ya, yb = int(max(0, np.floor(min(y0, y1, y2)))), int(min(H - 1, np.ceil(max(y0, y1, y2))))
        if xa > xb or ya > yb: continue
        X, Y = xs[ya:yb + 1, xa:xb + 1] + 0.5, ys[ya:yb + 1, xa:xb + 1] + 0.5
        w0 = ((x1 - X) * (y2 - Y) - (x2 - X) * (y1 - Y)) / area
        w1 = ((x2 - X) * (y0 - Y) - (x0 - X) * (y2 - Y)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * z0 + w1 * z1 + w2 * z2
        dz = depth[ya:yb + 1, xa:xb + 1]
        m = inside & (z < dz)
        dz[m] = z[m]
        color[ya:yb + 1, xa:xb + 1][m] = col
        drawn += 1
Image.fromarray(color).save(out)
print(out, "triangles rasterised:", drawn)
