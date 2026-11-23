#!/usr/bin/env python3
"""Factory プリセットの音量を実測してそろえる。

polybrisk_render --levels の結果 (各プリセットを試聴フレーズで鳴らしたときのピークと、
400 ms 窓での最大 RMS) から preset.level を決め、tools/preset_levels.json に書いて
プリセットを作り直す。音色の設定には触れない (音量だけ)。

  目標: 最大 RMS を -17 dBFS にそろえる。ただしピークは -3 dBFS 以下に抑える
  (安全用リミッターのしきい値 -1.9 dBFS より下)。ゲインは 0.5 dB 刻み、-24〜+24 dB (preset.level の範囲)。

使い方: scripts/build.sh Release の後で python3 tools/level_presets.py
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
LEVELS = os.path.join(HERE, "preset_levels.json")
TARGET_LOUDNESS = -17.0
MAX_PEAK = -3.0

# 補正値の上限を個別に決める音色。試聴フレーズが単音のカテゴリ (Lead / Hook) にある、和音でも弾く Poly の音色は、
# 単音で目標に合わせると和音で安全用リミッターにかかる。全音色を 5 声の和音で鳴らすテストが通る値にする。
GAIN_CEILING = {"Staccato Hook": 7.0}


def find_binary():
    for base, _, files in os.walk(os.path.join(ROOT, "build", "Release")):
        if "polybrisk_render" in files:
            path = os.path.join(base, "polybrisk_render")
            if os.access(path, os.X_OK) and not os.path.isdir(path):
                return path
    sys.exit("polybrisk_render がありません。scripts/build.sh Release を実行してください。")


def rebuild():
    subprocess.check_call([sys.executable, os.path.join(HERE, "make_factory_presets.py")], stdout=subprocess.DEVNULL)
    subprocess.check_call(["cmake", "--build", os.path.join(ROOT, "build", "Release"), "--target", "polybrisk_render", "--parallel", "8"],
                          stdout=subprocess.DEVNULL)


def measure():
    out = subprocess.check_output([find_binary(), "--levels"], text=True)
    return json.loads(out[out.index("{"):])


def main():
    # 途中で失敗したら (Release ビルドがない、ビルドエラーなど)、元の補正値に戻す。
    # 戻さないと、補正値が空のまま全プリセットが作り直されて、測り直すまで音量がばらばらになる
    find_binary()
    previous = open(LEVELS).read() if os.path.exists(LEVELS) else None

    try:
        level()
    except BaseException:
        if previous is not None:
            open(LEVELS, "w").write(previous)
            subprocess.call([sys.executable, os.path.join(HERE, "make_factory_presets.py")], stdout=subprocess.DEVNULL)
            print("失敗したので、補正値とプリセットを元に戻しました。", file=sys.stderr)
        raise


def level():
    # まず補正なし (0 dB) の状態で測る
    json.dump({}, open(LEVELS, "w"))
    rebuild()
    raw = measure()

    gains = {}
    for name, m in raw.items():
        gain = TARGET_LOUDNESS - m["loudnessDb"]
        gain = min(gain, MAX_PEAK - m["peakDb"])
        gains[name] = max(-24.0, min(24.0, GAIN_CEILING.get(name, 24.0), round(gain * 2.0) / 2.0))

    json.dump(gains, open(LEVELS, "w"), indent=2, sort_keys=True)
    rebuild()
    final = measure()

    # 補正後にもう一度測り、ピークが上限を超えた音色だけ 0.5 dB 刻みで下げる
    # (発振器の位相が自由な音色は、測るたびにピークが少し変わる)
    over = {name: m["peakDb"] - MAX_PEAK for name, m in final.items() if m["peakDb"] > MAX_PEAK}
    if over:
        import math
        for name, excess in over.items():
            gains[name] = max(-24.0, gains[name] - math.ceil(excess * 2.0) / 2.0)
        json.dump(gains, open(LEVELS, "w"), indent=2, sort_keys=True)
        rebuild()
        final = measure()

    print(f"{'preset':24} {'gain':>6} {'loudness':>9} {'peak':>7}")
    for name in sorted(final):
        print(f"{name:24} {gains[name]:+6.1f} {final[name]['loudnessDb']:9.1f} {final[name]['peakDb']:7.1f}")

    loud = [m["loudnessDb"] for m in final.values()]
    print(f"\nloudness range: {min(loud):.1f} .. {max(loud):.1f} dBFS, max peak {max(m['peakDb'] for m in final.values()):.1f} dBFS")


if __name__ == "__main__":
    main()
