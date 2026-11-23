#!/usr/bin/env python3
"""Factory プリセット同士の「聴いた感じの近さ」を数値で調べる (似た音色の洗い出し用)。

scripts/render_presets.sh が build/renders/ に書き出した WAV から、帯域 × 時間のレベル分布
(24 帯域 × 48 区間、最大値基準の dB、-48 dB で打ち切り) と左右の広がりを取り、
同じカテゴリの中で差が小さい組を近い順に並べる。位相や波形ではなく、音色・時間変化・広がりを比べる。

  距離 = 帯域×時間の平均絶対差 [dB] + 広がりの差 × 6
  目安: 3 未満はほぼ同じ役割、4.5 未満は要確認。

使い方: scripts/render_presets.sh の後で python3 tools/preset_similarity.py [しきい値]
人の耳の代わりにはならない。候補を挙げるだけ。
"""
import glob
import json
import os
import sys

import numpy as np
from scipy.io import wavfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BANDS, FRAMES, FLOOR = 24, 48, -48.0


def features(path):
    rate, data = wavfile.read(path)
    x = data.astype(np.float64)
    if data.dtype.kind == "i":
        x /= float(np.iinfo(data.dtype).max)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    mid, side = 0.5 * (x[:, 0] + x[:, 1]), 0.5 * (x[:, 0] - x[:, 1])
    width = float(np.sqrt(np.mean(side ** 2)) / max(np.sqrt(np.mean(mid ** 2)), 1e-9))

    size, hop = 2048, 512
    count = max(1, (len(mid) - size) // hop)
    window = np.hanning(size)
    spec = np.abs(np.fft.rfft(np.stack([mid[i * hop:i * hop + size] * window for i in range(count)]), axis=1)) ** 2
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    edges = np.geomspace(40.0, 16000.0, BANDS + 1)
    banded = np.stack([spec[:, (freqs >= lo) & (freqs < hi)].sum(axis=1) for lo, hi in zip(edges[:-1], edges[1:])], axis=1)
    chunks = np.array_split(banded, FRAMES, axis=0)
    grid = np.stack([c.mean(axis=0) for c in chunks])
    db = 10.0 * np.log10(np.maximum(grid, 1e-20))
    db = np.maximum(db - db.max(), FLOOR)
    return db, width


def main():
    threshold = float(sys.argv[1]) if len(sys.argv) > 1 else 4.5
    categories = {}
    for path in glob.glob(os.path.join(ROOT, "presets", "factory", "*.json")):
        p = json.load(open(path))
        categories[p["name"]] = p["category"]

    items = []
    for path in sorted(glob.glob(os.path.join(ROOT, "build", "renders", "*.wav"))):
        name = os.path.basename(path)[4:-4].replace("_", " ")
        match = [n for n in categories if n.replace("-", " ").replace("/", " ") == name or n == name]
        if not match:
            print("no preset for", path)
            continue
        items.append((match[0], categories[match[0]], *features(path)))

    pairs = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, b = items[i], items[j]
            if a[1] != b[1]:
                continue
            distance = float(np.mean(np.abs(a[2] - b[2]))) + abs(a[3] - b[3]) * 6.0
            pairs.append((distance, a[1], a[0], b[0]))

    pairs.sort()
    print(f"{len(items)} presets. Pairs in the same category closer than {threshold}:")
    for distance, category, a, b in pairs:
        if distance >= threshold:
            break
        print(f"  {distance:5.2f}  {category:18} {a}  /  {b}")
    nearest = {}
    for distance, _, a, b in pairs:
        nearest.setdefault(a, (distance, b))
        nearest.setdefault(b, (distance, a))
    print(f"closest pair: {pairs[0][0]:.2f}, median nearest-neighbour distance: {np.median([d for d, _ in nearest.values()]):.2f}")


if __name__ == "__main__":
    main()
