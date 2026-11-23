#!/usr/bin/env python3
"""Factory プリセットの定義 (人が書いた値) を presets/factory/*.json に書き出す。

ここにあるのは 1 音色ずつ意図を決めて書いた設定で、乱数や既存音色の数値ずらしによる生成は行わない
(CLAUDE.md ルール 8)。記載のないパラメーターは既定値。値は docs/PARAMETER_IDS.md の実値、
選択肢は名前で書く。音量は tools/level_presets.py が実測に基づいて preset.level を調整する (output.gain はユーザーのもので、プリセットには入れない)。

使い方: python3 tools/make_factory_presets.py
"""
import json
import os
import re
import uuid

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "presets", "factory")
NAMESPACE = uuid.UUID("6f1d3c52-8a0e-4f6e-9d57-2b9f4c1d7a10")


def osc(which, wave, **kw):
    """OSC A / B の設定。kw: octave, semitone, fine, level, voices, detune, spread, phase, pw, mix"""
    names = {"voices": "unisonVoices", "phase": "phaseMode", "pw": "pulseWidth", "mix": "unisonMix"}
    out = {f"osc.{which}.enabled": True, f"osc.{which}.wave": wave}
    for key, value in kw.items():
        out[f"osc.{which}.{names.get(key, key)}"] = value
    return out


def group(prefix, **kw):
    return {f"{prefix}.{key}": value for key, value in kw.items()}


def fx(name, **kw):
    out = {f"fx.{name}.enabled": True}
    out.update({f"fx.{name}.{key}": value for key, value in kw.items()})
    return out


def macro(name, *mappings):
    return {"name": name, "mappings": [{"target": t, "deltaMin": lo, "deltaMax": hi, "curve": "linear"} for t, lo, hi in mappings]}


BRIGHT = ("filter.cutoff", -0.35, 0.2)

PRESETS = []


# タグの語彙 (D-047)。ブラウザーのタグのボタンになるので、種類を絞って基準をそろえる。
#   手で付けるもの: 明るさ (Bright / Warm / Dark のどれか 1 つを先頭に)、質感と用途、Wide (書き出しの左右差を測って決める)
#   設定から自動で付くもの: derive_tags() を参照
TONE_TAGS = ["Bright", "Warm", "Dark"]
HAND_TAGS = TONE_TAGS + ["Big", "Punchy", "Soft", "Aggressive", "Clean", "Hollow", "Airy", "Classic", "Mix Ready", "Tight",
                         "FX", "Wide", "Dynamic"]
AUTO_TAGS = ["Gate", "Arp", "Stereo", "Motion", "Mono", "Layered", "Slow", "Dry", "Dynamic"]


def derive_tags(p):
    """設定値から決まるタグ。手で付けると漏れるので、ここで機械的に付ける。"""
    g = p.get
    tags = []
    if g("gate.enabled"):
        tags.append("Gate")                       # Trance Gate が刻む
    if g("arp.enabled"):
        tags.append("Arp")                        # Arpeggiator が動く
    if g("gate.enabled") and g("gate.stereo"):
        tags.append("Stereo")                     # 左右で別のパターンの Gate
    if any(g(k, 0) for k in ("lfo.cutoff", "lfo.pulseWidth", "lfo.pitch", "lfo.amp")) or g("fx.phaser.enabled"):
        tags.append("Motion")                     # LFO か Phaser / Flanger で音が動く
    if g("voice.mode", "Poly") != "Poly":
        tags.append("Mono")                       # Mono / Legato (単音)
    interval = g("osc.b.octave", 0) * 12 + g("osc.b.semitone", 0)
    if g("sub.level", 0) > 0 or (g("osc.b.enabled") and abs(interval) in (7, 19, 24)):
        tags.append("Layered")                    # Sub、5 度、2 オクターブの重ね
    if g("amp.attack", 2) >= 40:
        tags.append("Slow")                       # 立ち上がりが遅い
    reverb = g("fx.reverb.mix", 20) if g("fx.reverb.enabled") else 0
    delay = g("fx.delay.mix", 20) if g("fx.delay.enabled") else 0
    if reverb <= 9 and delay == 0:
        tags.append("Dry")                        # 残響がほとんどない
    if g("performance.aftertouchVibrato", 0) or g("performance.aftertouchCutoff", 0):
        tags.append("Dynamic")                    # アフタータッチに反応する
    return tags


def preset(name, category, tags, description, macros, *parts, pattern=None):
    parameters = {}
    for part in parts:
        parameters.update(part)

    assert tags[0] in TONE_TAGS and sum(t in TONE_TAGS for t in tags) == 1, f"{name}: 明るさのタグは先頭に 1 つ"
    assert all(t in HAND_TAGS for t in tags), f"{name}: 語彙にないタグ {tags}"
    if category == "Octave / Layered":
        tags = tags + ["Layered"]
    derived = derive_tags(parameters)
    if isinstance(pattern, str) and PATTERNS[pattern][1] is not None and "Gate" in derived:
        derived.insert(derived.index("Gate") + 1, "Stereo")   # ステレオの収録パターン (gate.stereo は main() で入る)
    tags = tags + [t for t in derived if t not in tags]
    PRESETS.append({"name": name, "category": category, "tags": tags, "description": description,
                    "parameters": parameters, "macros": macros, "pattern": pattern})


def load_factory_patterns():
    """収録 Gate パターンを src/rhythm/GatePattern.h から読む (定義を二重に持たない)。
    戻り値: 名前 -> (左 16 ステップ, 右 16 ステップ or None)。値は 0 / 0.25 / 0.5 / 0.75 / 1。"""
    header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "rhythm", "GatePattern.h")
    level = {"1": 1, "x": 1, "h": 0.75, "m": 0.5, "l": 0.25}
    patterns = {}
    for name, left, right in re.findall(r'\{\s*"[^"]+",\s*"([^"]+)",\s*"([^"]{16})"(?:,\s*"([^"]{16})")?', open(header).read()):
        patterns[name] = ([level.get(c, 0) for c in left], [level.get(c, 0) for c in right] if right else None)
    return patterns


PATTERNS = load_factory_patterns()

# ----------------------------------------------------------------------------- SuperSAW
preset("Anthem Saw", "SuperSAW", ["Bright", "Big", "Wide"],
       "Wide two-octave supersaw for big chorus chords. Plate reverb and a touch of chorus.",
       [macro("Bright", BRIGHT, ("fx.eq.highGain", -0.1, 0.15)),
        macro("Detune", ("osc.a.detune", -0.3, 0.35), ("osc.b.detune", -0.25, 0.3), ("osc.a.unisonMix", -0.20, 0.25), ("osc.b.unisonMix", -0.20, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.35), ("fx.reverb.decay", -0.1, 0.25)),
        macro("Release", ("amp.release", -0.2, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=40, spread=85, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=7, detune=32, spread=100, level=50, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=9000, keyTrack=30),
       group("amp", attack=6, decay=800, sustain=85, release=380),
       fx("chorus", type="Wide", rate=0.5, depth=40, mix=25),
       fx("eq", lowGain=-2, highGain=3),
       fx("imager", width=120, monoBass=True),
       fx("reverb", type="Plate", decay=2.2, mix=22, tone=65))

preset("Tight Saw Chords", "SuperSAW", ["Bright", "Tight", "Mix Ready"],
       "Narrower, drier supersaw with a short release. Sits in a busy mix without washing it out.",
       [macro("Bright", BRIGHT),
        macro("Width", ("osc.a.spread", -0.5, 0.4), ("fx.imager.width", -0.25, 0.25)),
        macro("Tail", ("amp.release", -0.15, 0.35), ("fx.reverb.mix", 0.0, 0.3)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25))],
       osc("a", "SuperSAW", voices=5, detune=28, spread=60, mix=32, phase="Free"),
       osc("b", "Saw", fine=5, level=40, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=9000, resonance=5, keyTrack=25),
       group("amp", attack=2, decay=500, sustain=70, release=160),
       fx("eq", lowGain=-3, midFreq=2800, midGain=1.5),
       fx("imager", width=100, monoBass=True),
       fx("reverb", type="Room", decay=0.6, mix=8))

# D-066: 試聴で Anthem Saw と似ているとの指摘を受けて作り直した。最初の案 (乾いた硬い音) は「アイドル感がない、
# もっとキラキラした感じ」との指摘で取りやめ。高域のきらめきを主役にする: 2 オクターブ上の層を大きく、
# 16 分で左右に跳ねる短いエコー、明るい Plate、空気のノイズ。低域は EQ で引いて Anthem Saw より軽くする。
preset("Idol Hyper Saw", "SuperSAW", ["Bright", "Big", "Airy", "Wide"],
       "A glittering hyper saw: a bright layer two octaves up, sixteenth-note echoes that sparkle left and right, and a shiny plate. Light in the lows on purpose. For the sweetest, brightest hook of an idol-pop chorus.",
       [macro("Sparkle", ("osc.b.level", -0.4, 0.3), ("fx.eq.highGain", -0.2, 0.15)),
        macro("Hype", ("osc.a.detune", -0.25, 0.3), ("osc.a.unisonMix", -0.20, 0.22)),
        macro("Twinkle", ("fx.delay.mix", -0.2, 0.25), ("fx.delay.feedback", -0.2, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.reverb.decay", -0.1, 0.25))],
       osc("a", "SuperSAW", voices=9, detune=50, spread=100, mix=68, phase="Free"),
       osc("b", "SuperSAW", octave=2, voices=7, detune=38, spread=100, level=62, mix=72, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=18000, keyTrack=20),
       group("amp", attack=2, decay=700, sustain=85, release=260),
       group("transient", enabled=True, mode="Noise Burst", amount=26, duration=10, noiseColor=90),
       group("noise", level=12, color=92),
       fx("chorus", type="Wide", rate=1.1, depth=40, mix=26),
       fx("eq", lowGain=-5, midFreq=4500, midGain=2, highGain=6),
       fx("imager", width=140, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/16", feedback=38, tone=85, mix=20),
       fx("reverb", type="Plate", decay=2.0, tone=90, mix=22))

preset("Soft Saw Pad", "Pad / Strings", ["Warm", "Soft", "Wide"],
       "Slow, warm supersaw pad. The filter opens gently and a long hall keeps it floating.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Swell", ("amp.attack", -0.3, 0.3), ("filterEnv.attack", -0.25, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3), ("fx.reverb.decay", -0.15, 0.2)),
        macro("Motion", ("fx.chorus.depth", -0.4, 0.4), ("fx.chorus.rate", -0.1, 0.25))],
       osc("a", "SuperSAW", voices=7, detune=30, spread=80, mix=66, phase="Free"),
       osc("b", "Triangle", octave=-1, level=45, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=2200, resonance=10, envAmount=20, keyTrack=40),
       group("filterEnv", attack=400, decay=1500, sustain=60, release=900),
       group("amp", attack=180, decay=1200, sustain=90, release=900),
       fx("chorus", type="Wide", rate=0.4, depth=55, mix=40),
       fx("eq", lowGain=-1.5),
       fx("reverb", type="Hall", decay=4.5, mix=35, preDelay=25, tone=50))

# ----------------------------------------------------------------------------- Synth Brass
BRASS_BRIGHT = ("filter.cutoff", -0.2, 0.25)

preset("Pop Brass", "Synth Brass", ["Warm", "Punchy", "Classic"],
       "The standard synth brass: two saws through the analog filter, velocity opens it up.",
       [macro("Bright", BRASS_BRIGHT, ("filter.envAmount", -0.1, 0.1)),
        macro("Attack", ("filterEnv.attack", -0.25, 0.3), ("amp.attack", -0.1, 0.2)),
        macro("Bite", ("filter.resonance", -0.1, 0.3), ("filter.drive", -0.2, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Saw", voices=3, detune=16, spread=40, phase="Free"),
       osc("b", "Saw", fine=7, level=70, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=480, resonance=12, drive=20, envAmount=46, keyTrack=50, velocity=45),
       group("filterEnv", attack=40, decay=360, sustain=45, release=240),
       group("amp", attack=10, decay=400, sustain=90, release=200),
       group("performance", vibratoDepth=25, vibratoDelay=350),
       fx("eq", midFreq=2500, midGain=2),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Brass Section Hit", "Synth Brass", ["Bright", "Big", "Punchy", "Classic"],
       "A whole section hitting one short chord: stacked octaves, a low square underneath, a blast of air at the start and a hall tail. The orchestra-hit style accent.",
       [macro("Bright", BRASS_BRIGHT, ("fx.eq.highGain", -0.1, 0.15)),
        macro("Blast", ("transient.amount", -0.3, 0.4), ("filter.envAmount", -0.2, 0.2)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("filterEnv.decay", -0.15, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3), ("fx.reverb.decay", -0.08, 0.2))],
       osc("a", "Saw", voices=7, detune=22, spread=80, mix=55, phase="Free"),
       osc("b", "Saw", octave=1, voices=5, detune=18, spread=100, level=65, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=900, resonance=12, drive=25, envAmount=60, keyTrack=40, velocity=45),
       group("filterEnv", attack=2, decay=320, sustain=25, release=260),
       group("amp", attack=1, decay=420, sustain=35, release=260),
       group("sub", level=50, octave="-1 Oct", wave="Square"),
       group("transient", enabled=True, mode="Both", amount=45, duration=35, noiseColor=60, pitchSnap=-3),
       fx("drive", type="Tape Saturation", drive=25, mix=70),
       fx("eq", lowGain=2, midFreq=1500, midGain=3, highGain=2),
       fx("imager", width=120, monoBass=True),
       fx("reverb", type="Hall", decay=1.8, preDelay=15, mix=24))

preset("Soft Horns", "Synth Brass", ["Warm", "Soft", "Airy", "Wide"],
       "Mellow horn pad with a little breath noise. Press into the keys (aftertouch) to open it up and add vibrato.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Swell", ("filterEnv.attack", -0.2, 0.35), ("amp.attack", -0.15, 0.3)),
        macro("Breath", ("noise.level", -0.12, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Triangle", voices=2, detune=8, spread=50, phase="Free"),
       osc("b", "Saw", level=30, phase="Free"),
       group("filter", character="Analog", mode="LP12", cutoff=420, envAmount=26, keyTrack=60, velocity=30),
       group("filterEnv", attack=140, decay=700, sustain=60, release=400),
       group("amp", attack=70, decay=600, sustain=90, release=420),
       group("noise", level=12, color=40),
       group("performance", vibratoDepth=20, vibratoDelay=600, aftertouchVibrato=25, aftertouchCutoff=30),
       fx("chorus", type="Classic", rate=0.5, depth=40, mix=18),
       fx("reverb", type="Hall", decay=2.8, mix=26))

preset("Brass Octaves", "Synth Brass", ["Bright", "Big"],
       "Brass doubled an octave up for melodies and riffs that need to cut through.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Octave", ("osc.b.level", -0.6, 0.3)),
        macro("Attack", ("filterEnv.attack", -0.2, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.08, 0.3), ("fx.reverb.mix", -0.1, 0.2))],
       osc("a", "Saw", voices=1, phase="Free"),
       osc("b", "Saw", octave=1, voices=3, detune=14, spread=60, level=60, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=900, resonance=8, envAmount=40, keyTrack=50, velocity=40),
       group("filterEnv", attack=25, decay=300, sustain=50, release=220),
       group("amp", attack=6, decay=450, sustain=88, release=220),
       fx("imager", width=115, monoBass=True),
       fx("delay", division="1/8", feedback=25, mix=8),
       fx("reverb", type="Plate", decay=1.4, mix=15))

# D-051: 試聴で Brass Octaves / Power Brass と似すぎとの指摘を受けて作り直した。
# 低いオクターブの重ねをやめ、レゾナンスを効かせたフィルターがゆっくり開く「ブワー」という立ち上がりを特徴にする。
preset("Analog Fanfare", "Synth Brass", ["Warm", "Big", "Classic", "Wide"],
       "Each note opens slowly with a resonant sweep, from dark to bright. Long held chords for intros and endings; not for fast lines.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Sweep", ("filter.resonance", -0.3, 0.25), ("filter.envAmount", -0.25, 0.2)),
        macro("Swell", ("filterEnv.attack", -0.35, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3), ("fx.reverb.decay", -0.1, 0.2))],
       osc("a", "Saw", voices=5, detune=24, spread=75, mix=58, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=220, resonance=42, drive=20, envAmount=72, keyTrack=40, velocity=25),
       group("filterEnv", attack=260, decay=1400, sustain=70, release=420),
       group("amp", attack=25, decay=800, sustain=95, release=380),
       group("performance", vibratoDepth=30, vibratoDelay=600),
       fx("chorus", type="Wide", rate=0.4, depth=40, mix=24),
       fx("reverb", type="Hall", decay=3.2, mix=24))

# ----------------------------------------------------------------------------- Stab / Chord Hit
preset("Bright Pop Stab", "Stab / Chord Hit", ["Bright", "Punchy", "Tight"],
       "Snappy chord stab: punch filter, fast envelope and a noise + pitch transient.",
       [macro("Punch", ("filter.envAmount", -0.25, 0.3), ("transient.amount", -0.1, 0.25)),
        macro("Bright", BRASS_BRIGHT),
        macro("Length", ("amp.decay", -0.15, 0.25), ("filterEnv.decay", -0.15, 0.25)),
        macro("Room", ("fx.reverb.mix", -0.14, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=25, spread=70, mix=35, phase="Retrigger"),
       osc("b", "Square", octave=-1, level=45, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=700, resonance=20, drive=30, envAmount=55, keyTrack=40, velocity=30),
       group("filterEnv", attack=0.5, decay=140, sustain=0, release=120),
       group("amp", attack=0.5, decay=260, sustain=0, release=140),
       group("transient", enabled=True, mode="Both", amount=45, duration=18, noiseColor=60, pitchSnap=12),
       fx("drive", type="Soft Clip", drive=20),
       fx("reverb", type="Room", decay=0.7, mix=14))

preset("House Chord Stab", "Stab / Chord Hit", ["Warm", "Classic"],
       "Root-and-fifth saw stab with resonance, chorus and a dotted ping-pong delay.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Squelch", ("filter.resonance", -0.2, 0.35), ("filter.envAmount", -0.15, 0.2)),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3), ("fx.delay.feedback", -0.2, 0.3))],
       osc("a", "Saw", phase="Retrigger"),
       osc("b", "Saw", semitone=7, level=60, phase="Retrigger"),
       group("filter", character="Analog", mode="LP24", cutoff=1200, resonance=25, envAmount=35, keyTrack=40, velocity=30),
       group("filterEnv", attack=0.5, decay=180, sustain=0, release=140),
       group("amp", attack=1, decay=320, sustain=0, release=100),
       fx("chorus", type="Classic", rate=0.7, depth=45, mix=25),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=35, mix=20),
       fx("reverb", type="Plate", decay=1.2, mix=12))

preset("Rave Hit", "Stab / Chord Hit", ["Bright", "Aggressive", "Big"],
       "Hard-clipped detuned hit with a loud noise burst. Play single chords and let the hall ring.",
       [macro("Drive", ("fx.drive.drive", -0.3, 0.35), ("filter.drive", -0.3, 0.3)),
        macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Noise", ("transient.amount", -0.5, 0.4), ("transient.duration", -0.15, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.18, 0.35))],
       osc("a", "SuperSAW", voices=7, detune=45, spread=90, mix=62, phase="Retrigger"),
       osc("b", "Pulse", octave=1, pw=30, level=50, phase="Retrigger"),
       group("filter", character="Punch", mode="LP12", cutoff=2500, resonance=30, drive=50, envAmount=40, keyTrack=30),
       group("filterEnv", attack=0.5, decay=200, sustain=10, release=180),
       group("amp", attack=0.5, decay=400, sustain=0, release=180),
       group("transient", enabled=True, mode="Noise Burst", amount=60, duration=30, noiseColor=80),
       fx("drive", type="Hard Clip", drive=45, tone=70),
       fx("eq", lowGain=-2),
       fx("reverb", type="Hall", decay=2.0, mix=18))

preset("Tiny Tight Stab", "Stab / Chord Hit", ["Warm", "Tight", "Mix Ready"],
       "Very short, dry stab for 16th-note comping. No reverb: add your own in the mix.",
       [macro("Length", ("amp.decay", -0.1, 0.3), ("filterEnv.decay", -0.1, 0.3)),
        macro("Bright", BRASS_BRIGHT),
        macro("Click", ("transient.amount", -0.25, 0.5)),
        macro("Body", ("fx.eq.lowGain", -0.2, 0.3), ("osc.b.level", -0.3, 0.3))],
       osc("a", "Square", phase="Retrigger"),
       osc("b", "Saw", fine=-6, level=50, phase="Retrigger"),
       group("filter", character="Clean", mode="LP24", cutoff=1500, resonance=10, envAmount=45, keyTrack=40, velocity=40),
       group("filterEnv", attack=0.5, decay=90, sustain=0, release=70),
       group("amp", attack=0.5, decay=150, sustain=0, release=60),
       group("transient", enabled=True, mode="Noise Burst", amount=25, duration=6, noiseColor=60),
       fx("eq", lowGain=-4, midFreq=3000, midGain=1.5))

preset("Wide Chord Hit", "Stab / Chord Hit", ["Bright", "Big", "Wide"],
       "A longer, very wide supersaw hit for the first beat of a section.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Width", ("fx.imager.width", -0.4, 0.25), ("osc.a.spread", -0.5, 0.0)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("amp.sustain", -0.2, 0.4)),
        macro("Plate", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=30, spread=100, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=26, spread=100, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=5000, envAmount=30, keyTrack=30, velocity=25),
       group("filterEnv", attack=1, decay=260, sustain=15, release=250),
       group("amp", attack=1, decay=500, sustain=20, release=250),
       fx("chorus", type="Wide", rate=0.6, depth=35, mix=20),
       fx("imager", width=150, monoBass=True),
       fx("reverb", type="Plate", decay=1.6, mix=25))

preset("Arp Pulse Stab", "Stab / Chord Hit", ["Bright", "Tight", "Wide"],
       "Hold a chord: a two-octave 16th arpeggio of short pulse stabs with a ping-pong echo.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Arp Gate", ("arp.gate", -0.4, 0.45)),
        macro("Swing", ("arp.swing", 0.0, 0.8)),
        macro("Echo", ("fx.delay.mix", -0.22, 0.3))],
       osc("a", "Pulse", pw=40, voices=3, detune=12, spread=50, phase="Retrigger"),
       osc("b", "Saw", octave=-1, level=40, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=900, resonance=22, envAmount=50, keyTrack=45, velocity=30),
       group("filterEnv", attack=0.5, decay=120, sustain=0, release=90),
       group("amp", attack=0.5, decay=180, sustain=0, release=90),
       group("arp", enabled=True, mode="Up", rate="1/16", octaves=2, gate=55),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=35, mix=22),
       fx("reverb", type="Room", decay=0.8, mix=10))

# ----------------------------------------------------------------------------- Bright Poly
preset("Glass Poly", "Bright Poly", ["Bright", "Clean", "Wide"],
       "Thin pulse with a triangle an octave up. Glassy poly for arpeggiated chords and intros.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Pulse", ("osc.a.pulseWidth", -0.2, 0.3)),
        macro("Chorus", ("fx.chorus.mix", -0.3, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.14, 0.2))],
       osc("a", "Pulse", pw=25, voices=2, detune=8, spread=60, phase="Free"),
       osc("b", "Triangle", octave=1, level=55, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=6500, envAmount=20, keyTrack=60, velocity=30),
       group("filterEnv", attack=1, decay=700, sustain=30, release=400),
       group("amp", attack=3, decay=900, sustain=55, release=400),
       fx("chorus", type="Classic", rate=0.8, depth=45, mix=30),
       fx("delay", division="1/4", feedback=28, mix=14),
       fx("reverb", type="Plate", decay=1.8, mix=22))

preset("Pop Keys", "Bright Poly", ["Warm", "Punchy", "Mix Ready"],
       "Velocity-sensitive saw and square keys for chord comping. Play softly for a darker tone.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.25)),
        macro("Dynamics", ("filter.velocity", -0.5, 0.4), ("amp.velocity", -0.5, 0.0)),
        macro("Decay", ("amp.decay", -0.2, 0.3), ("amp.sustain", -0.3, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Saw", voices=2, detune=10, spread=45, phase="Free"),
       osc("b", "Square", fine=4, level=45, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=2400, resonance=10, envAmount=30, keyTrack=50, velocity=50),
       group("filterEnv", attack=1, decay=450, sustain=35, release=260),
       group("amp", attack=2, decay=700, sustain=60, release=260),
       fx("eq", lowGain=-1.5, highGain=2),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Shimmer Poly", "Bright Poly", ["Bright", "Airy", "Wide"],
       "High-passed supersaw with a sine two octaves up. Airy top layer over a fuller sound.",
       [macro("Air", ("filter.cutoff", -0.2, 0.35), ("osc.b.level", -0.3, 0.4)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Width", ("fx.imager.width", -0.3, 0.3), ("fx.chorus.mix", -0.3, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.3, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=20, spread=90, mix=62, phase="Free"),
       osc("b", "Sine", octave=2, level=30, phase="Free"),
       group("filter", character="Clean", mode="HP12", cutoff=180, resonance=5, keyTrack=20),
       group("amp", attack=8, decay=1200, sustain=70, release=600),
       fx("chorus", type="Wide", rate=0.5, depth=45, mix=35),
       fx("imager", width=130, monoBass=True),
       fx("delay", division="1/8 D", feedback=30, mix=15),
       fx("reverb", type="Hall", decay=4.0, mix=32))

# ----------------------------------------------------------------------------- Synth Pluck
preset("Saw Pluck", "Synth Pluck", ["Warm", "Punchy", "Classic"],
       "Resonant saw pluck that closes quickly. Dotted ping-pong delay fills the gaps.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Pluck", ("filter.envAmount", -0.25, 0.25), ("filterEnv.decay", -0.15, 0.25)),
        macro("Length", ("amp.decay", -0.2, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.24, 0.3), ("fx.delay.feedback", -0.2, 0.25))],
       osc("a", "Saw", voices=3, detune=14, spread=50, mix=38, phase="Retrigger"),
       osc("b", "Square", octave=-1, level=30, phase="Retrigger"),
       group("filter", character="Analog", mode="LP24", cutoff=350, resonance=22, envAmount=62, keyTrack=55, velocity=45),
       group("filterEnv", attack=0.5, decay=190, sustain=0, release=160),
       group("amp", attack=0.5, decay=420, sustain=0, release=220),
       group("transient", enabled=True, mode="Pitch Snap", amount=20, duration=10, pitchSnap=12),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=42, mix=24),
       fx("reverb", type="Plate", decay=1.5, mix=16))

preset("Arp Pluck Lights", "Synth Pluck", ["Bright", "Clean"],
       "Up-and-down 16th arpeggio of a soft triangle pluck. Hold two or three notes.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Arp Gate", ("arp.gate", -0.3, 0.5)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("amp.release", -0.15, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3), ("fx.delay.mix", -0.2, 0.25))],
       osc("a", "Triangle", phase="Retrigger"),
       osc("b", "Pulse", octave=1, pw=20, level=45, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=1800, envAmount=45, keyTrack=55, velocity=30),
       group("filterEnv", attack=0.5, decay=140, sustain=0, release=150),
       group("amp", attack=0.5, decay=260, sustain=0, release=180),
       group("arp", enabled=True, mode="Up-Down", rate="1/16", octaves=2, gate=45),
       fx("delay", division="1/8", feedback=35, mix=22),
       fx("reverb", type="Hall", decay=2.6, mix=25))

# ----------------------------------------------------------------------------- Lead / Hook
preset("Hook Lead", "Lead / Hook", ["Bright", "Big"],
       "A five-voice supersaw played one note at a time. The wide lead that carries a chorus melody; every note re-attacks.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.25)),
        macro("Detune", ("osc.a.detune", -0.25, 0.35), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Vibrato", ("performance.vibratoDepth", -0.2, 0.4), ("performance.vibratoRate", -0.1, 0.15)),
        macro("Echo", ("fx.delay.mix", -0.22, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=26, spread=70, mix=35, phase="Free"),
       osc("b", "Saw", octave=1, level=30, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=7500, keyTrack=50),
       group("amp", attack=2, decay=400, sustain=90, release=160),
       group("voice", mode="Mono", glide=0),
       group("performance", vibratoDepth=35, vibratoRate=5.8, vibratoDelay=250),
       fx("eq", lowGain=-2, highGain=2),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=35, mix=20, ducking=40),
       fx("reverb", type="Plate", decay=1.6, mix=16))

# ----------------------------------------------------------------------------- Octave / Layered
preset("Octave Stack", "Octave / Layered", ["Bright", "Big"],
       "Supersaw on top of a three-voice saw an octave below. One finger sounds like two hands.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Low Layer", ("osc.b.level", -0.8, 0.2)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.b.detune", -0.1, 0.2), ("osc.a.unisonMix", -0.20, 0.25), ("osc.b.unisonMix", -0.20, 0.25)),
        macro("Plate", ("fx.reverb.mix", -0.18, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=26, spread=80, phase="Free"),
       osc("b", "Saw", octave=-1, voices=3, detune=12, spread=30, level=80, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=4500, envAmount=15, keyTrack=35, velocity=25),
       group("filterEnv", attack=2, decay=500, sustain=40, release=300),
       group("amp", attack=4, decay=700, sustain=80, release=300),
       group("transient", enabled=True, mode="Noise Burst", amount=15, duration=10, noiseColor=55),
       fx("eq", lowGain=2, highGain=2),
       fx("imager", width=125, monoBass=True),
       fx("reverb", type="Plate", decay=1.6, mix=18))


# =============================================================================
# ここから下は Factory 64 (ベータライブラリ) で追加した音色 (D-042 の見直しで一部を作り直した)。
# Starter 24 と役割が重ならないよう、音色ごとに「何のための音か」を先に決めている。
# =============================================================================

# ----------------------------------------------------------------------------- SuperSAW
preset("Stadium Unison", "SuperSAW", ["Warm", "Punchy", "Tight", "Mix Ready"],
       "One nine-voice oscillator kept narrow, with a square sub and soft-clip drive. Power chords in the low register that survive a mono sum.",
       [macro("Bright", BRIGHT),
        macro("Detune", ("osc.a.detune", -0.3, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Width", ("osc.a.spread", -0.4, 0.5)),
        macro("Grit", ("filter.drive", 0.0, 0.5), ("fx.drive.drive", -0.2, 0.3))],
       osc("a", "SuperSAW", voices=9, detune=30, spread=40, mix=30, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=4200, resonance=6, drive=30, keyTrack=20),
       group("amp", attack=2, decay=600, sustain=90, release=140),
       group("sub", level=40, octave="-1 Oct", wave="Square"),
       fx("drive", type="Soft Clip", drive=30, mix=70),
       fx("eq", lowGain=1.5, midFreq=1800, midGain=1.5),
       fx("reverb", type="Room", decay=0.8, mix=9))

preset("Dark Saw Bed", "SuperSAW", ["Dark", "Mix Ready"],
       "Low-passed supersaw that stays behind the vocal. A verse bed, not a hook.",
       [macro("Open", ("filter.cutoff", -0.15, 0.4)),
        macro("Warmth", ("filter.drive", -0.2, 0.4), ("fx.eq.lowGain", -0.1, 0.15)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=26, spread=65, phase="Free"),
       osc("b", "Saw", octave=-1, level=35, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1200, resonance=6, drive=20, keyTrack=45),
       group("amp", attack=12, decay=900, sustain=85, release=320),
       fx("eq", highGain=-2),
       fx("reverb", type="Room", decay=1.1, mix=12))

preset("Fifth Saw", "SuperSAW", ["Bright", "Big", "Punchy"],
       "Second oscillator a fifth up, kept narrow and pushed through tape drive. One note is already a power chord: play single lines.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Fifth", ("osc.b.level", -0.6, 0.3)),
        macro("Drive", ("fx.drive.drive", -0.25, 0.4), ("filter.drive", -0.2, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=24, spread=45, mix=40, phase="Free"),
       osc("b", "Saw", semitone=7, voices=3, detune=14, spread=55, level=65, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=5200, resonance=8, drive=25, keyTrack=30),
       group("amp", attack=2, decay=600, sustain=85, release=180),
       fx("drive", type="Tape Saturation", drive=30, mix=70),
       fx("eq", midFreq=1600, midGain=2),
       fx("reverb", type="Room", decay=0.9, mix=10))

preset("Saw Swell", "Pad / Strings", ["Bright", "Wide"],
       "Fades in over half a second while the filter opens. For the bar before the chorus.",
       [macro("Swell Time", ("amp.attack", -0.25, 0.25), ("filterEnv.attack", -0.25, 0.25)),
        macro("Bright", ("filter.envAmount", -0.3, 0.3)),
        macro("Detune", ("osc.a.detune", -0.2, 0.35), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Hall", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=38, spread=90, mix=64, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=30, spread=100, level=40, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=500, envAmount=55, keyTrack=30),
       group("filterEnv", attack=900, decay=2000, sustain=100, release=600),
       group("amp", attack=600, decay=1000, sustain=100, release=500),
       fx("chorus", type="Wide", rate=0.35, depth=40, mix=25),
       fx("reverb", type="Hall", decay=3.8, mix=30, preDelay=20))

preset("Detuned Wall", "SuperSAW", ["Warm", "Big", "Aggressive", "Wide"],
       "Extreme detune, chorus and a wide imager: a wall of saws for a drop. Too much for busy chords.",
       [macro("Detune", ("osc.a.detune", -0.5, 0.25), ("osc.b.detune", -0.5, 0.25), ("osc.a.unisonMix", -0.20, 0.15), ("osc.b.unisonMix", -0.20, 0.20)),
        macro("Bright", BRIGHT),
        macro("Width", ("fx.imager.width", -0.4, 0.2)),
        macro("Drive", ("fx.drive.drive", -0.25, 0.4))],
       osc("a", "SuperSAW", voices=9, detune=70, spread=100, mix=75, phase="Free"),
       osc("b", "SuperSAW", octave=-1, voices=7, detune=62, spread=100, level=65, mix=70, phase="Free"),
       group("filter", character="Punch", mode="LP12", cutoff=11000, keyTrack=20),
       group("amp", attack=4, decay=800, sustain=90, release=300),
       fx("drive", type="Tape Saturation", drive=25, mix=70),
       fx("chorus", type="Wide", rate=0.7, depth=50, mix=30),
       fx("imager", width=160, monoBass=True),
       fx("reverb", type="Hall", decay=2.6, mix=20))

preset("Phase Sweep Saw", "SuperSAW", ["Warm", "Classic", "Wide"],
       "Supersaw through a slow six-stage phaser. The sweep keeps long chords moving without touching the filter.",
       [macro("Bright", BRIGHT),
        macro("Sweep", ("fx.phaser.depth", -0.5, 0.3), ("fx.phaser.mix", -0.4, 0.3)),
        macro("Speed", ("fx.phaser.rate", -0.15, 0.4)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=30, spread=80, phase="Free"),
       osc("b", "Saw", octave=-1, level=40, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=8000, keyTrack=30),
       group("amp", attack=8, decay=900, sustain=90, release=420),
       fx("phaser", type="Phaser", rate=0.18, depth=80, feedback=55, mix=55),
       fx("eq", lowGain=-1.5),
       fx("imager", width=115, monoBass=True),
       fx("reverb", type="Plate", decay=2.0, mix=18))

preset("Jet Saw", "SuperSAW", ["Bright", "Aggressive", "Wide"],
       "Bright supersaw into a flanger with high feedback: the jet-plane sweep. For drops and one-bar accents.",
       [macro("Jet", ("fx.phaser.feedback", -0.6, 0.15), ("fx.phaser.mix", -0.4, 0.3)),
        macro("Speed", ("fx.phaser.rate", -0.1, 0.5)),
        macro("Bright", BRIGHT),
        macro("Drive", ("fx.drive.drive", -0.2, 0.4))],
       osc("a", "SuperSAW", voices=9, detune=44, spread=90, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=36, spread=100, level=35, phase="Free"),
       group("filter", character="Punch", mode="LP12", cutoff=14000, keyTrack=20),
       group("amp", attack=3, decay=700, sustain=90, release=240),
       group("noise", level=14, color=80),
       fx("drive", type="Soft Clip", drive=20, mix=60),
       fx("phaser", type="Flanger", rate=0.12, depth=90, feedback=78, mix=60),
       fx("eq", lowGain=-2, highGain=2),
       fx("reverb", type="Hall", decay=2.2, mix=14))

preset("Tidal Saw", "Pad / Strings", ["Warm", "Wide"],
       "The filter rises and falls once every two bars, in time with the song. A pad that breathes with the tempo.",
       [macro("Tide", ("lfo.cutoff", -0.2, 0.25)),
        macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Resonance", ("filter.resonance", -0.2, 0.35)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.1, 0.2))],
       osc("a", "SuperSAW", voices=7, detune=32, spread=85, mix=62, phase="Free"),
       osc("b", "Pulse", pw=30, octave=-1, level=35, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1400, resonance=22, drive=10, keyTrack=35),
       group("amp", attack=60, decay=1000, sustain=95, release=700),
       group("lfo", wave="Triangle", sync=True, division="2 Bars", cutoff=40),
       fx("chorus", type="Classic", rate=0.4, depth=35, mix=20),
       fx("delay", mode="Ping Pong", division="1/4", feedback=30, mix=10),
       fx("reverb", type="Hall", decay=3.4, mix=26))

preset("High Saw Sparkle", "SuperSAW", ["Bright", "Airy", "Wide"],
       "High-passed supersaw one and two octaves up. A top layer only: it has no body on purpose.",
       [macro("Thin", ("filter.cutoff", -0.2, 0.3)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Shimmer", ("fx.chorus.mix", -0.25, 0.35), ("fx.eq.highGain", -0.1, 0.15)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "SuperSAW", octave=1, voices=7, detune=32, spread=100, phase="Free"),
       osc("b", "SuperSAW", octave=2, voices=5, detune=28, spread=100, level=40, mix=70, phase="Free"),
       group("filter", character="Clean", mode="HP12", cutoff=400, resonance=4, keyTrack=30),
       group("amp", attack=10, decay=900, sustain=80, release=450),
       fx("chorus", type="Wide", rate=0.6, depth=40, mix=28),
       fx("eq", highGain=3),
       fx("imager", width=140),
       fx("reverb", type="Plate", decay=2.4, mix=28))

preset("Vintage Ensemble", "Pad / Strings", ["Warm", "Classic", "Wide"],
       "Three saws through a deep classic chorus, in the manner of an old string ensemble.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Ensemble", ("fx.chorus.depth", -0.5, 0.3), ("fx.chorus.mix", -0.4, 0.2)),
        macro("Attack", ("amp.attack", -0.2, 0.35)),
        macro("Room", ("fx.reverb.mix", -0.15, 0.3))],
       osc("a", "Saw", voices=3, detune=20, spread=60, mix=62, phase="Free"),
       osc("b", "Saw", octave=1, fine=-5, level=40, phase="Free"),
       group("filter", character="Analog", mode="LP12", cutoff=3800, resonance=5, keyTrack=40),
       group("amp", attack=40, decay=900, sustain=90, release=500),
       fx("chorus", type="Classic", rate=0.55, depth=80, mix=55),
       fx("eq", lowGain=-2, highGain=-1),
       fx("reverb", type="Room", decay=1.3, mix=15))

# ----------------------------------------------------------------------------- Synth Brass
# D-051: 試聴で Brass Octaves / Analog Fanfare と似すぎとの指摘を受けて作り直した。
# 強く歪ませた中域寄りの音にし、ピッチが上から落ちる短いアタックと、すぐ閉じるフィルターで「ブラッ」と鳴らす。
preset("Power Brass", "Synth Brass", ["Bright", "Aggressive", "Punchy"],
       "Hard-clipped and mid-heavy, with a pitch blip on the attack and a filter that snaps shut. Short loud riffs, not chords.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Drive", ("fx.drive.drive", -0.35, 0.35), ("filter.drive", -0.3, 0.3)),
        macro("Bite", ("transient.amount", -0.3, 0.4), ("filterEnv.decay", -0.15, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.08, 0.3))],
       osc("a", "Saw", voices=3, detune=10, spread=30, mix=36, phase="Retrigger"),
       osc("b", "Square", octave=-1, level=45, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=650, resonance=26, drive=55, envAmount=62, keyTrack=50, velocity=45),
       group("filterEnv", attack=3, decay=170, sustain=38, release=140),
       group("amp", attack=2, decay=350, sustain=85, release=120),
       group("transient", enabled=True, mode="Pitch Snap", amount=30, duration=28, pitchSnap=7),
       fx("drive", type="Hard Clip", drive=48, tone=65, mix=85),
       fx("eq", lowGain=-1.5, midFreq=1500, midGain=4, highGain=-1),
       fx("reverb", type="Room", decay=0.6, mix=8))

preset("Sforzando Brass", "Synth Brass", ["Dark", "Punchy", "Dynamic"],
       "Bright for a moment, then dark: the filter drops to a low sustain over a low octave. Accented long notes.",
       [macro("Accent", ("filter.envAmount", -0.3, 0.2), ("filterEnv.decay", -0.15, 0.2)),
        macro("Body", ("filter.cutoff", -0.15, 0.3)),
        macro("Dynamics", ("filter.velocity", -0.5, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.14, 0.3))],
       osc("a", "Saw", voices=3, detune=14, spread=40, mix=40, phase="Free"),
       osc("b", "Saw", octave=-1, fine=-6, level=70, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=260, resonance=10, drive=15, envAmount=68, keyTrack=50, velocity=55),
       group("filterEnv", attack=4, decay=420, sustain=6, release=260),
       group("amp", attack=4, decay=600, sustain=85, release=260),
       fx("reverb", type="Hall", decay=2.4, mix=20))

preset("Brass Swell", "Synth Brass", ["Warm"],
       "A slow crescendo: the filter takes half a second to open, and aftertouch pushes it further. Long notes.",
       [macro("Swell Time", ("filterEnv.attack", -0.25, 0.3), ("amp.attack", -0.15, 0.25)),
        macro("Bright", ("filter.envAmount", -0.25, 0.25)),
        macro("Vibrato", ("performance.vibratoDepth", -0.15, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=3, detune=16, spread=50, phase="Free"),
       osc("b", "Saw", octave=-1, level=45, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=320, resonance=8, envAmount=55, keyTrack=50, velocity=30),
       group("filterEnv", attack=550, decay=1200, sustain=80, release=400),
       group("amp", attack=120, decay=800, sustain=95, release=400),
       group("performance", vibratoDepth=30, vibratoDelay=900, aftertouchCutoff=25),
       fx("chorus", type="Classic", rate=0.4, depth=30, mix=15),
       fx("reverb", type="Hall", decay=3.0, mix=24))

preset("PWM Brass", "Synth Brass", ["Warm", "Hollow", "Classic"],
       "Pulse waves whose width is swept by the LFO. Hollower than saw brass, and it never sits still.",
       [macro("Bright", BRASS_BRIGHT),
        macro("PWM", ("lfo.pulseWidth", -0.5, 0.3), ("lfo.rate", -0.05, 0.15)),
        macro("Attack", ("filterEnv.attack", -0.2, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Pulse", pw=50, phase="Free"),
       osc("b", "Pulse", pw=30, octave=1, level=35, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=2200, resonance=6, envAmount=24, keyTrack=55, velocity=40),
       group("filterEnv", attack=25, decay=380, sustain=55, release=220),
       group("amp", attack=8, decay=400, sustain=88, release=200),
       group("sub", level=20, octave="-1 Oct", wave="Square"),
       group("lfo", wave="Triangle", rate=0.9, pulseWidth=75),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Fifth Brass", "Synth Brass", ["Bright", "Big"],
       "Open, bright brass with the second oscillator a fifth up. Fanfare lines played with one finger.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Fifth", ("osc.b.level", -0.6, 0.3)),
        macro("Attack", ("filterEnv.attack", -0.2, 0.3)),
        macro("Plate", ("fx.reverb.mix", -0.15, 0.3))],
       osc("a", "Saw", voices=3, detune=14, spread=45, phase="Free"),
       osc("b", "Saw", semitone=7, voices=3, detune=12, spread=45, level=60, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=1500, resonance=6, envAmount=38, keyTrack=50, velocity=40),
       group("filterEnv", attack=8, decay=300, sustain=60, release=220),
       group("amp", attack=3, decay=450, sustain=90, release=200),
       fx("eq", lowGain=-2, midFreq=3000, midGain=2.5, highGain=2),
       fx("delay", division="1/8", feedback=20, mix=8),
       fx("reverb", type="Plate", decay=1.6, mix=18))

preset("Growl Brass", "Synth Brass", ["Warm", "Aggressive", "Dynamic"],
       "Hold a note and a fast filter flutter fades in, like a player growling into the horn. Short notes stay clean.",
       [macro("Growl", ("lfo.cutoff", -0.2, 0.2), ("lfo.rate", -0.1, 0.15)),
        macro("Bright", BRASS_BRIGHT),
        macro("Fade In", ("lfo.fade", -0.1, 0.3)),
        macro("Drive", ("fx.drive.drive", -0.2, 0.4))],
       osc("a", "Saw", voices=2, detune=12, spread=40, phase="Free"),
       osc("b", "Pulse", pw=25, level=50, phase="Free"),
       group("filter", character="Punch", mode="LP12", cutoff=1300, resonance=30, drive=35, envAmount=30, keyTrack=45, velocity=40),
       group("filterEnv", attack=15, decay=300, sustain=60, release=180),
       group("amp", attack=5, decay=400, sustain=92, release=160),
       group("lfo", wave="Saw", rate=13, fade=220, retrigger=True, cutoff=30),
       fx("drive", type="Tube Saturation", drive=30, mix=70),
       fx("reverb", type="Room", decay=0.7, mix=9))

preset("Wide Chorus Brass", "Synth Brass", ["Warm", "Classic", "Wide"],
       "Eighties brass: one saw per side of a wide chorus, with a plate behind it.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Chorus", ("fx.chorus.mix", -0.35, 0.25), ("fx.chorus.depth", -0.3, 0.3)),
        macro("Attack", ("filterEnv.attack", -0.2, 0.3)),
        macro("Plate", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=2, detune=12, spread=80, phase="Free"),
       osc("b", "Saw", fine=9, level=60, phase="Free"),
       group("filter", character="Analog", mode="LP12", cutoff=900, resonance=8, envAmount=42, keyTrack=55, velocity=35),
       group("filterEnv", attack=35, decay=400, sustain=55, release=260),
       group("amp", attack=10, decay=500, sustain=88, release=240),
       fx("chorus", type="Wide", rate=0.6, depth=55, mix=40),
       fx("imager", width=125, monoBass=True),
       fx("reverb", type="Plate", decay=1.9, mix=20))

# ----------------------------------------------------------------------------- Stab / Chord Hit
preset("Organ Stab", "Stab / Chord Hit", ["Warm", "Clean", "Classic", "Tight"],
       "Sine and triangle an octave apart with a click: a percussive organ chord for house piano parts.",
       [macro("Click", ("transient.amount", -0.3, 0.5)),
        macro("Upper", ("osc.b.level", -0.5, 0.4)),
        macro("Length", ("amp.decay", -0.15, 0.3), ("amp.sustain", -0.2, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "Sine", phase="Retrigger"),
       osc("b", "Triangle", octave=1, level=55, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=6000, keyTrack=40),
       group("amp", attack=0.5, decay=300, sustain=20, release=90),
       group("transient", enabled=True, mode="Noise Burst", amount=30, duration=5, noiseColor=70),
       fx("drive", type="Warm Saturation", drive=25, mix=50),
       fx("reverb", type="Room", decay=0.7, mix=10))

preset("Nasal Stab", "Stab / Chord Hit", ["Bright", "Classic", "Mix Ready"],
       "Band-pass filtered saws: a narrow, nasal chord stab that leaves room for bass and vocal.",
       [macro("Tone", ("filter.cutoff", -0.25, 0.25)),
        macro("Resonance", ("filter.resonance", -0.3, 0.3)),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "Saw", voices=3, detune=16, spread=60, phase="Retrigger"),
       osc("b", "Saw", octave=1, level=45, phase="Retrigger"),
       group("filter", character="Analog", mode="BP12", cutoff=1400, resonance=35, envAmount=25, keyTrack=60, velocity=20),
       group("filterEnv", attack=0.5, decay=200, sustain=0, release=140),
       group("amp", attack=1, decay=300, sustain=0, release=120),
       fx("delay", mode="Ping Pong", division="1/8", feedback=30, mix=15),
       fx("reverb", type="Plate", decay=1.0, mix=10))

preset("Noise Snap Stab", "Stab / Chord Hit", ["Bright", "Aggressive", "Punchy", "Tight", "Wide"],
       "A bright noise snap in front of a square chord. Cuts through like a clap layered on the stab.",
       [macro("Snap", ("transient.amount", -0.5, 0.3), ("transient.noiseColor", -0.3, 0.2)),
        macro("Bright", BRASS_BRIGHT),
        macro("Length", ("amp.decay", -0.12, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "Square", voices=3, detune=14, spread=60, phase="Retrigger"),
       osc("b", "Saw", octave=1, level=35, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=1100, resonance=12, envAmount=48, keyTrack=40, velocity=30),
       group("filterEnv", attack=0.5, decay=110, sustain=0, release=90),
       group("amp", attack=0.5, decay=220, sustain=0, release=90),
       group("transient", enabled=True, mode="Noise Burst", amount=70, duration=22, noiseColor=85),
       fx("reverb", type="Room", decay=0.6, mix=10))

preset("Pitch Drop Hit", "Stab / Chord Hit", ["Bright", "Aggressive", "Big", "FX"],
       "Every note starts two octaves high and falls into place. A zap for fills and transitions.",
       [macro("Drop", ("transient.amount", -0.7, 0.0), ("transient.duration", -0.3, 0.3)),
        macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Length", ("amp.decay", -0.2, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=5, detune=24, spread=80, phase="Retrigger"),
       osc("b", "Square", octave=-1, level=50, phase="Retrigger"),
       group("filter", character="Punch", mode="LP12", cutoff=4000, resonance=15, envAmount=30, keyTrack=30),
       group("filterEnv", attack=0.5, decay=300, sustain=10, release=200),
       group("amp", attack=0.5, decay=500, sustain=0, release=200),
       group("transient", enabled=True, mode="Pitch Snap", amount=100, duration=80, pitchSnap=24),
       fx("delay", division="1/8 D", feedback=35, mix=14),
       fx("reverb", type="Hall", decay=2.4, mix=20))

preset("Soft Felt Stab", "Stab / Chord Hit", ["Warm", "Soft", "Mix Ready"],
       "Dark triangle stab with no edge. Comping under a vocal where a saw stab would be too much.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.35)),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Thump", ("transient.amount", -0.2, 0.5)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Triangle", voices=2, detune=8, spread=40, phase="Retrigger"),
       osc("b", "Sine", octave=-1, level=50, phase="Retrigger"),
       group("filter", character="Analog", mode="LP24", cutoff=900, resonance=5, envAmount=35, keyTrack=50, velocity=50),
       group("filterEnv", attack=0.5, decay=160, sustain=0, release=120),
       group("amp", attack=1, decay=340, sustain=0, release=140),
       group("transient", enabled=True, mode="Noise Burst", amount=20, duration=10, noiseColor=20),
       fx("reverb", type="Room", decay=1.0, mix=12))

preset("Hard Square Hit", "Stab / Chord Hit", ["Dark", "Aggressive", "Big", "Hollow"],
       "Dark, hard-clipped squares over a square sub. A heavy low hit for the downbeat.",
       [macro("Drive", ("fx.drive.drive", -0.4, 0.3)),
        macro("Bright", ("filter.cutoff", -0.3, 0.3)),
        macro("Length", ("amp.decay", -0.2, 0.3)),
        macro("Sub", ("sub.level", -0.6, 0.3))],
       osc("a", "Square", voices=3, detune=12, spread=50, phase="Retrigger"),
       osc("b", "Pulse", pw=30, octave=-1, level=60, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=700, resonance=10, drive=45, envAmount=30, keyTrack=30),
       group("filterEnv", attack=0.5, decay=260, sustain=10, release=180),
       group("amp", attack=0.5, decay=480, sustain=0, release=180),
       group("sub", level=65, octave="-1 Oct", wave="Square"),
       fx("drive", type="Hard Clip", drive=45, tone=45),
       fx("eq", lowGain=3, highGain=-3),
       fx("reverb", type="Room", decay=0.9, mix=8))

preset("Flange Hit", "Stab / Chord Hit", ["Bright", "FX", "Wide"],
       "A noisy saw hit through a resonant flanger. Each chord rings with a hollow, metallic tail.",
       [macro("Metal", ("fx.phaser.feedback", -0.6, 0.1), ("fx.phaser.mix", -0.4, 0.25)),
        macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Plate", ("fx.reverb.mix", -0.15, 0.3))],
       osc("a", "Saw", voices=5, detune=22, spread=80, phase="Retrigger"),
       osc("b", "Noise", level=30),
       group("filter", character="Clean", mode="LP12", cutoff=5500, envAmount=25, keyTrack=30, velocity=25),
       group("filterEnv", attack=0.5, decay=220, sustain=0, release=160),
       group("amp", attack=0.5, decay=340, sustain=0, release=160),
       fx("phaser", type="Flanger", rate=0.35, depth=45, feedback=82, mix=65),
       fx("imager", width=125, monoBass=True),
       fx("reverb", type="Plate", decay=1.3, mix=15))

preset("Reverb Throw Stab", "Stab / Chord Hit", ["Warm", "Big", "FX", "Wide"],
       "A dry, short hit into a long hall. Play one chord and let five seconds of reverb answer it.",
       [macro("Throw", ("fx.reverb.mix", -0.35, 0.2), ("fx.reverb.decay", -0.2, 0.2)),
        macro("Bright", BRASS_BRIGHT),
        macro("Length", ("amp.decay", -0.1, 0.3)),
        macro("Pre-Delay", ("fx.reverb.preDelay", -0.3, 0.5))],
       osc("a", "SuperSAW", voices=7, detune=30, spread=90, mix=64, phase="Retrigger"),
       osc("b", "Square", octave=-1, level=40, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=1300, resonance=12, envAmount=50, keyTrack=40),
       group("filterEnv", attack=0.5, decay=170, sustain=0, release=120),
       group("amp", attack=0.5, decay=260, sustain=0, release=120),
       fx("reverb", type="Hall", size=80, decay=5.0, preDelay=60, tone=55, mix=40))

preset("Dice Stab", "Stab / Chord Hit", ["Bright", "Wide"],
       "Every chord lands at a different brightness: a sample-and-hold LFO resets the filter on each hit, and dotted echoes follow.",
       [macro("Dice", ("lfo.cutoff", -0.3, 0.2)),
        macro("Bright", BRASS_BRIGHT),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.25, 0.25), ("fx.delay.feedback", -0.3, 0.3))],
       osc("a", "Saw", voices=3, detune=18, spread=65, phase="Retrigger"),
       osc("b", "Pulse", pw=35, octave=1, level=40, phase="Retrigger"),
       group("filter", character="Analog", mode="LP24", cutoff=1300, resonance=24, envAmount=30, keyTrack=45),
       group("filterEnv", attack=0.5, decay=180, sustain=0, release=140),
       group("amp", attack=0.5, decay=320, sustain=0, release=140),
       group("lfo", wave="S&H", rate=7, cutoff=38),
       fx("delay", mode="Stereo", division="1/4 D", feedback=50, tone=45, mix=26),
       fx("reverb", type="Plate", decay=1.2, mix=8))

# ----------------------------------------------------------------------------- Bright Poly
preset("Bell Poly", "Bright Poly", ["Bright", "Clean", "Airy"],
       "Two sines an octave and a fifth apart with a quick decay: a simple bell for countermelodies.",
       [macro("Overtone", ("osc.b.level", -0.5, 0.4)),
        macro("Decay", ("amp.decay", -0.2, 0.3), ("amp.release", -0.15, 0.3)),
        macro("Chorus", ("fx.chorus.mix", -0.2, 0.4)),
        macro("Hall", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "Sine", phase="Retrigger"),
       osc("b", "Sine", octave=1, semitone=7, level=45, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=12000, keyTrack=30),
       group("amp", attack=1, decay=1100, sustain=0, release=700),
       group("transient", enabled=True, mode="Noise Burst", amount=12, duration=4, noiseColor=80),
       fx("chorus", type="Classic", rate=0.6, depth=30, mix=20),
       fx("delay", division="1/8 D", feedback=30, mix=12),
       fx("reverb", type="Hall", decay=3.0, mix=28))

preset("Digital Keys", "Bright Poly", ["Bright", "Clean"],
       "Square with a very narrow pulse on top. Hard, glassy digital keys for fast chord parts.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Thin", ("osc.b.pulseWidth", -0.08, 0.3), ("osc.b.level", -0.3, 0.3)),
        macro("Decay", ("amp.decay", -0.2, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Square", phase="Free"),
       osc("b", "Pulse", pw=10, octave=1, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=7000, envAmount=20, keyTrack=60, velocity=40),
       group("filterEnv", attack=0.5, decay=350, sustain=30, release=200),
       group("amp", attack=1, decay=600, sustain=45, release=220),
       fx("eq", lowGain=-2, highGain=2),
       fx("reverb", type="Room", decay=0.8, mix=12))

preset("Hollow Poly", "Bright Poly", ["Warm", "Hollow", "Soft"],
       "Triangle over a square an octave below. Round and hollow, for chords in the middle register.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Hollow", ("osc.b.level", -0.5, 0.3)),
        macro("Chorus", ("fx.chorus.mix", -0.25, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.18, 0.3))],
       osc("a", "Triangle", voices=2, detune=8, spread=50, phase="Free"),
       osc("b", "Square", octave=-1, level=45, phase="Free"),
       group("filter", character="Analog", mode="LP12", cutoff=2800, resonance=6, envAmount=15, keyTrack=50, velocity=30),
       group("filterEnv", attack=2, decay=600, sustain=40, release=300),
       group("amp", attack=4, decay=800, sustain=65, release=350),
       fx("chorus", type="Classic", rate=0.5, depth=40, mix=25),
       fx("reverb", type="Plate", decay=1.8, mix=18))

preset("PWM Strings", "Pad / Strings", ["Warm", "Hollow", "Classic", "Wide"],
       "Two pulse waves an octave apart whose width moves slowly and deeply, with no chorus on top. Darker and slower than an ensemble: a hollow, swaying string pad.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Attack", ("amp.attack", -0.25, 0.3)),
        macro("PWM", ("lfo.pulseWidth", -0.5, 0.3), ("lfo.rate", -0.03, 0.1)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Pulse", pw=50, voices=2, detune=6, spread=50, phase="Free"),
       osc("b", "Pulse", pw=30, octave=-1, level=50, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1500, resonance=12, envAmount=20, keyTrack=45),
       group("filterEnv", attack=600, decay=2000, sustain=70, release=900),
       group("amp", attack=350, decay=1200, sustain=90, release=1100),
       group("lfo", wave="Sine", rate=0.35, pulseWidth=85),
       fx("reverb", type="Hall", decay=3.8, mix=28))

preset("Tremolo Keys", "Bright Poly", ["Warm", "Soft", "Classic"],
       "Round triangle keys with a steady amplitude tremolo, like an electric piano through its vibrato circuit.",
       [macro("Tremolo", ("lfo.amp", -0.45, 0.35)),
        macro("Speed", ("lfo.rate", -0.1, 0.15)),
        macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Triangle", phase="Retrigger"),
       osc("b", "Sine", octave=1, level=22, phase="Retrigger"),
       group("filter", character="Analog", mode="LP12", cutoff=2600, envAmount=22, keyTrack=55, velocity=55),
       group("filterEnv", attack=0.5, decay=500, sustain=20, release=300),
       group("amp", attack=1, decay=1400, sustain=35, release=320, velocity=70),
       group("transient", enabled=True, mode="Noise Burst", amount=14, duration=6, noiseColor=40),
       group("lfo", wave="Sine", rate=5.2, amp=45),
       fx("drive", type="Warm Saturation", drive=18, mix=50),
       fx("phaser", type="Phaser", rate=0.25, depth=40, feedback=20, mix=25),
       fx("reverb", type="Room", decay=1.0, mix=14))

# ----------------------------------------------------------------------------- Synth Pluck
preset("Sine Pluck", "Synth Pluck", ["Warm", "Soft", "Clean", "Mix Ready"],
       "A plain sine pluck with a small click. Quiet melodic parts and doubling a vocal line.",
       [macro("Click", ("transient.amount", -0.25, 0.5)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("amp.release", -0.15, 0.3)),
        macro("Overtone", ("osc.b.level", -0.25, 0.5)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.14, 0.25))],
       osc("a", "Sine", phase="Retrigger"),
       osc("b", "Triangle", octave=1, level=25, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=9000, keyTrack=40),
       group("amp", attack=0.5, decay=380, sustain=0, release=260),
       group("transient", enabled=True, mode="Noise Burst", amount=25, duration=4, noiseColor=65),
       fx("delay", division="1/8", feedback=30, mix=14),
       fx("reverb", type="Plate", decay=1.6, mix=20))

preset("Square Pluck Echo", "Synth Pluck", ["Bright", "Hollow", "Classic"],
       "Short square pluck with sixteenth-note ping-pong echoes filling the bar.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.3, 0.2), ("fx.delay.feedback", -0.3, 0.25)),
        macro("Length", ("amp.decay", -0.15, 0.3)),
        macro("Pluck", ("filter.envAmount", -0.25, 0.25))],
       osc("a", "Square", phase="Retrigger"),
       osc("b", "Square", octave=1, fine=5, level=35, phase="Retrigger"),
       group("filter", character="Clean", mode="LP24", cutoff=600, resonance=15, envAmount=55, keyTrack=55, velocity=35),
       group("filterEnv", attack=0.5, decay=150, sustain=0, release=130),
       group("amp", attack=0.5, decay=300, sustain=0, release=160),
       fx("delay", mode="Ping Pong", division="1/16 D", feedback=50, tone=55, mix=30),
       fx("reverb", type="Room", decay=0.9, mix=10))

preset("Metal Pluck", "Synth Pluck", ["Bright", "Aggressive"],
       "Resonant band-pass pluck with a pitch snap. Thin, metallic and percussive.",
       [macro("Tone", ("filter.cutoff", -0.25, 0.25)),
        macro("Ring", ("filter.resonance", -0.4, 0.2)),
        macro("Snap", ("transient.amount", -0.4, 0.4)),
        macro("Space", ("fx.reverb.mix", -0.18, 0.3))],
       osc("a", "Pulse", pw=22, voices=2, detune=10, spread=50, phase="Retrigger"),
       osc("b", "Saw", octave=1, level=40, phase="Retrigger"),
       group("filter", character="Punch", mode="BP12", cutoff=2200, resonance=55, envAmount=40, keyTrack=70, velocity=30),
       group("filterEnv", attack=0.5, decay=110, sustain=0, release=100),
       group("amp", attack=0.5, decay=260, sustain=0, release=160),
       group("transient", enabled=True, mode="Pitch Snap", amount=45, duration=14, pitchSnap=19),
       fx("delay", division="1/8 T", feedback=30, mix=12),
       fx("reverb", type="Plate", decay=1.4, mix=18))

preset("Bass Pluck", "Bass", ["Dark", "Punchy", "Tight"],
       "A low pluck with a square sub underneath. Plays bass lines and low riffs; it is thin above middle C on purpose.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.35)),
        macro("Pluck", ("filter.envAmount", -0.25, 0.25), ("filterEnv.decay", -0.15, 0.25)),
        macro("Sub", ("sub.level", -0.6, 0.3)),
        macro("Drive", ("filter.drive", -0.3, 0.5))],
       osc("a", "Saw", octave=-1, phase="Retrigger"),
       osc("b", "Pulse", pw=35, octave=-1, fine=6, level=45, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=220, resonance=18, drive=30, envAmount=58, keyTrack=40, velocity=45),
       group("filterEnv", attack=0.5, decay=170, sustain=5, release=120),
       group("amp", attack=0.5, decay=360, sustain=15, release=110),
       group("sub", level=60, octave="-1 Oct", wave="Square"),
       fx("eq", lowGain=2, midFreq=900, midGain=1.5))

# ----------------------------------------------------------------------------- Lead / Hook
preset("Square Lead", "Lead / Hook", ["Warm", "Hollow", "Classic"],
       "A plain square lead that slides between overlapping notes. Play legato for portamento, detached for clean steps.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.25)),
        macro("Glide", ("voice.glide", -0.25, 0.3)),
        macro("Vibrato", ("performance.vibratoDepth", -0.15, 0.4)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3))],
       osc("a", "Square", phase="Free"),
       osc("b", "Square", octave=-1, level=30, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=2200, resonance=10, envAmount=15, keyTrack=70),
       group("filterEnv", attack=2, decay=250, sustain=50, release=200),
       group("amp", attack=3, decay=300, sustain=90, release=160),
       group("voice", mode="Legato", glide=110),
       group("performance", bendUp=12, vibratoDepth=45, vibratoRate=6.0, vibratoDelay=300),
       fx("delay", division="1/4", feedback=30, mix=18, ducking=50),
       fx("reverb", type="Room", decay=1.0, mix=10))

preset("Screamer Lead", "Lead / Hook", ["Bright", "Aggressive"],
       "Resonant saw into a wavefolder. Harsh on purpose; press harder (aftertouch) for vibrato and more scream.",
       [macro("Fold", ("fx.drive.drive", -0.4, 0.3)),
        macro("Scream", ("filter.resonance", -0.3, 0.3), ("filter.cutoff", -0.2, 0.2)),
        macro("Vibrato", ("performance.vibratoDepth", -0.2, 0.4)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=2, detune=8, spread=20, phase="Free"),
       osc("b", "Pulse", pw=20, octave=1, level=30, phase="Free"),
       group("filter", character="Punch", mode="BP12", cutoff=1700, resonance=50, drive=35, envAmount=30, keyTrack=80),
       group("filterEnv", attack=2, decay=350, sustain=60, release=200),
       group("amp", attack=2, decay=300, sustain=90, release=140),
       group("voice", mode="Legato", glide=45),
       group("performance", vibratoDepth=50, vibratoRate=6.2, vibratoDelay=200, aftertouchVibrato=40, aftertouchCutoff=20),
       fx("drive", type="Wavefold", drive=45, tone=65, mix=85),
       fx("eq", lowGain=-3, midFreq=2500, midGain=2),
       fx("delay", division="1/8 D", feedback=40, mix=18, ducking=45),
       fx("reverb", type="Plate", decay=1.5, mix=12))

preset("Soft Sine Lead", "Lead / Hook", ["Warm", "Soft", "Clean"],
       "Sine with a little triangle. A gentle whistle-like lead for a slow melody.",
       [macro("Overtone", ("osc.b.level", -0.3, 0.5)),
        macro("Vibrato", ("performance.vibratoDepth", -0.15, 0.3), ("performance.vibratoRate", -0.1, 0.1)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Sine", phase="Free"),
       osc("b", "Triangle", octave=1, level=30, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=8000, keyTrack=50),
       group("amp", attack=15, decay=400, sustain=90, release=260),
       group("performance", vibratoDepth=30, vibratoRate=5.2, vibratoDelay=400, aftertouchVibrato=30),
       fx("chorus", type="Classic", rate=0.4, depth=25, mix=15),
       fx("delay", division="1/4", feedback=35, mix=22, ducking=30),
       fx("reverb", type="Hall", decay=2.8, mix=24))

preset("Glide Saw Lead", "Lead / Hook", ["Warm", "Classic"],
       "One saw, one pulse and a sub, with a long glide between overlapping notes. The singing portamento lead.",
       [macro("Glide", ("voice.glide", -0.3, 0.3)),
        macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("PWM", ("lfo.pulseWidth", -0.3, 0.5)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3))],
       osc("a", "Saw", phase="Free"),
       osc("b", "Pulse", pw=45, fine=6, level=60, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1500, resonance=22, drive=20, envAmount=28, keyTrack=75),
       group("filterEnv", attack=3, decay=500, sustain=40, release=220),
       group("amp", attack=3, decay=400, sustain=92, release=180),
       group("voice", mode="Legato", glide=220),
       group("sub", level=35, octave="-1 Oct", wave="Sine"),
       group("lfo", wave="Triangle", rate=1.2, pulseWidth=30),
       group("performance", vibratoDepth=40, vibratoRate=5.5, vibratoDelay=350),
       fx("chorus", type="Classic", rate=0.5, depth=30, mix=15),
       fx("delay", division="1/8 D", feedback=35, mix=20, ducking=40),
       fx("reverb", type="Plate", decay=1.8, mix=14))

preset("Wah Lead", "Lead / Hook", ["Bright", "Aggressive"],
       "A pulse through a resonant band-pass that sweeps on every quarter note. A talking, wah-pedal lead for funky hooks.",
       [macro("Wah", ("lfo.cutoff", -0.25, 0.2)),
        macro("Tone", ("filter.cutoff", -0.2, 0.25)),
        macro("Resonance", ("filter.resonance", -0.3, 0.25)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "Pulse", pw=30, phase="Free"),
       osc("b", "Saw", octave=-1, level=50, phase="Free"),
       group("filter", character="Analog", mode="BP12", cutoff=900, resonance=48, drive=25, keyTrack=60),
       group("amp", attack=2, decay=300, sustain=90, release=120),
       group("voice", mode="Mono", glide=25),
       group("lfo", wave="Triangle", sync=True, division="1/4", retrigger=True, cutoff=28),
       fx("drive", type="Tube Saturation", drive=25, mix=60),
       fx("delay", division="1/8", feedback=25, mix=12),
       fx("reverb", type="Room", decay=0.8, mix=10))

# ----------------------------------------------------------------------------- Octave / Layered
preset("Sub Octave Saw", "Octave / Layered", ["Warm", "Big"],
       "Three layers from one key: saws, a saw an octave up and a sine sub two octaves down. A melody that carries its own bass.",
       [macro("Sub", ("sub.level", -0.7, 0.25)),
        macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Top", ("osc.b.level", -0.35, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Saw", voices=3, detune=18, spread=60, phase="Free"),
       osc("b", "Saw", octave=1, level=35, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=3500, resonance=8, envAmount=20, keyTrack=40, velocity=25),
       group("filterEnv", attack=2, decay=400, sustain=45, release=260),
       group("amp", attack=3, decay=600, sustain=85, release=240),
       group("sub", level=70, octave="-2 Oct", wave="Sine"),
       fx("imager", width=110, monoBass=True),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Three Octave Keys", "Octave / Layered", ["Bright", "Punchy"],
       "A decaying key sound spread over three octaves: square sub, pulse and a thin pulse on top. Riffs that need to be heard on small speakers.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Top", ("osc.b.level", -0.5, 0.4)),
        macro("Sub", ("sub.level", -0.5, 0.4)),
        macro("Decay", ("amp.decay", -0.2, 0.3), ("amp.sustain", -0.25, 0.3))],
       osc("a", "Pulse", pw=40, voices=2, detune=8, spread=40, phase="Retrigger"),
       osc("b", "Pulse", pw=15, octave=1, level=50, phase="Retrigger"),
       group("filter", character="Clean", mode="LP24", cutoff=1800, resonance=12, envAmount=42, keyTrack=50, velocity=40),
       group("filterEnv", attack=0.5, decay=320, sustain=25, release=200),
       group("amp", attack=1, decay=700, sustain=40, release=200),
       group("sub", level=50, octave="-1 Oct", wave="Square"),
       group("transient", enabled=True, mode="Noise Burst", amount=18, duration=6, noiseColor=65),
       fx("eq", midFreq=2000, midGain=2, highGain=1.5),
       fx("delay", division="1/8", feedback=22, mix=8),
       fx("reverb", type="Plate", decay=1.2, mix=12))

# =============================================================================
# Trance Gate カテゴリ (D-042)。コードを押さえたままにして、ゲートにリズムを刻ませる音色。
# 既存の 5 音色 (Gate Saw Runner / Pump Saw / Triplet Gate Saw / Build Gate Saw / Chopped Chords) を
# ここへ移し、音色とパターンの組み合わせが重ならない 12 音色を足した。
# パターンは src/rhythm/GatePattern.h の収録パターンを名前で指定する (ステレオのものは Stereo が ON になる)。
# =============================================================================
GATE_DEPTH = ("gate.depth", -0.7, 0.0)

# カテゴリの先頭に置く代表の音色。Factory はカテゴリ内でこのファイルに書いた順に並ぶ (D-043)。
preset("Supersaw Gate", "Trance Gate", ["Bright", "Big", "Classic", "Wide"],
       "The classic pairing: a wide two-octave supersaw chopped by the Shifted Long pattern at 1/32. Hold a chord for 16th-note trance chops with two longer notes per half bar.",
       [macro("Bright", BRIGHT),
        macro("Gate Depth", GATE_DEPTH),
        macro("Smooth", ("gate.smooth", -0.05, 0.35)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.14, 0.25))],
       osc("a", "SuperSAW", voices=9, detune=42, spread=100, phase="Free"),
       osc("b", "SuperSAW", octave=-1, voices=7, detune=34, spread=70, level=60, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=12000, resonance=6, keyTrack=40),
       group("amp", attack=1, decay=600, sustain=100, release=180),
       group("gate", enabled=True, steps="16", rate="1/32", depth=100, smooth=2.5),
       fx("eq", lowGain=-1.5, midFreq=3200, midGain=1.5, highGain=3),
       fx("imager", width=125, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/8", feedback=30, mix=14),
       fx("reverb", type="Hall", decay=2.6, mix=20),
       pattern="Shifted Long")

preset("Gate Saw Runner", "Trance Gate", ["Bright", "Wide"],
       "Supersaw chopped by a syncopated 16th trance gate. Hold a chord and let the gate do the rhythm.",
       [macro("Bright", BRIGHT),
        macro("Gate Depth", ("gate.depth", -0.7, 0.0)),
        macro("Swing", ("gate.swing", 0.0, 0.8)),
        macro("Echo", ("fx.delay.mix", -0.18, 0.3), ("fx.delay.feedback", -0.2, 0.25))],
       osc("a", "SuperSAW", voices=7, detune=36, spread=85, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=30, spread=100, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=9000, keyTrack=30),
       group("amp", attack=2, decay=600, sustain=100, release=250),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=4),
       fx("eq", lowGain=-2, highGain=2),
       fx("imager", width=115, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=40, mix=18),
       fx("reverb", type="Plate", decay=1.8, mix=18),
       pattern="Syncopated")

preset("Pump Saw", "Trance Gate", ["Warm", "Wide"],
       "Offbeat gate with a long smooth time: a side-chain style pump without a compressor.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Pump", ("gate.depth", -0.6, 0.15), ("gate.smooth", -0.5, 0.15)),
        macro("Detune", ("osc.a.detune", -0.25, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=34, spread=75, mix=60, phase="Free"),
       osc("b", "Saw", octave=-1, level=50, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=6000, resonance=8, keyTrack=30),
       group("amp", attack=8, decay=900, sustain=100, release=350),
       group("gate", enabled=True, steps="8", rate="1/8", depth=85, smooth=25),
       fx("chorus", type="Classic", rate=0.6, depth=40, mix=22),
       fx("imager", width=110, monoBass=True),
       fx("reverb", type="Plate", decay=2.0, mix=20),
       pattern="Offbeat")

preset("Triplet Gate Saw", "Trance Gate", ["Bright", "Airy"],
       "Eight-step gate at triplet eighths with accents, on a high-passed saw with no low end. A shuffled pulse for 6/8 or swung tracks.",
       [macro("Bright", BRIGHT),
        macro("Gate Depth", ("gate.depth", -0.7, 0.0)),
        macro("Smooth", ("gate.smooth", -0.1, 0.6)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=34, spread=90, phase="Free"),
       osc("b", "Triangle", octave=1, level=50, phase="Free"),
       group("filter", character="Clean", mode="HP12", cutoff=320, resonance=10, keyTrack=30),
       group("amp", attack=2, decay=600, sustain=100, release=220),
       group("gate", enabled=True, steps="8", rate="1/8T", depth=100, smooth=5),
       fx("delay", division="1/4 T", feedback=30, mix=15),
       fx("reverb", type="Plate", decay=1.6, mix=16),
       pattern=("Triplet Accent", [1, 0.5, 0.5, 1, 0, 0.5, 1, 0.5]))

preset("Build Gate Saw", "Trance Gate", ["Bright", "Wide"],
       "The gate pattern gets denser through the bar while the filter opens. One-bar build-ups.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.3)),
        macro("Gate Depth", ("gate.depth", -0.6, 0.0)),
        macro("Rise", ("filter.envAmount", -0.3, 0.3), ("filterEnv.attack", -0.2, 0.2)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=40, spread=90, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=34, spread=100, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=1500, envAmount=40, keyTrack=30),
       group("filterEnv", attack=1800, decay=2000, sustain=100, release=400),
       group("amp", attack=2, decay=600, sustain=100, release=300),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=3, restart="Note On"),
       fx("reverb", type="Hall", decay=3.2, mix=24),
       pattern="Build Up")

preset("Chopped Chords", "Trance Gate", ["Warm", "Punchy"],
       "A sustaining chord cut into syncopated 16ths with accents. Hold the chord; the gate plays the stabs.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Chop", ("gate.depth", -0.7, 0.0), ("gate.smooth", -0.1, 0.4)),
        macro("Swing", ("gate.swing", 0.0, 0.8)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=24, spread=75, mix=38, phase="Free"),
       osc("b", "Square", octave=-1, level=40, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=2600, resonance=12, keyTrack=40),
       group("amp", attack=1, decay=500, sustain=85, release=120),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=2),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=30, mix=14),
       fx("reverb", type="Room", decay=0.8, mix=10),
       pattern=("Chop Accent", [1, 0, 0.6, 1, 0, 1, 0, 0.6, 1, 0, 0, 1, 0.6, 0, 1, 0]))

preset("Ping Pong Gate Pad", "Trance Gate", ["Warm", "Soft", "Wide"],
       "A warm pad cut into eighth notes that alternate left and right. Wide and gentle; lower Width to bring it back to the centre.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Gate Width", ("gate.width", -0.8, 0.0)),
        macro("Gate Depth", GATE_DEPTH, ("gate.smooth", 0.0, 0.4)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=3, detune=18, spread=40, mix=62, phase="Free"),
       osc("b", "Triangle", octave=-1, level=55, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1800, resonance=10, keyTrack=45),
       group("amp", attack=30, decay=900, sustain=100, release=500),
       group("gate", enabled=True, steps="16", rate="1/16", depth=90, smooth=14, width=100),
       fx("chorus", type="Classic", rate=0.45, depth=40, mix=22),
       fx("reverb", type="Hall", decay=3.0, mix=26),
       pattern="Ping Pong 8")

preset("Sweep Gate Trance", "Trance Gate", ["Bright", "Wide"],
       "The classic trance gate: a galloping supersaw while the filter opens and closes over four bars and a phaser turns underneath.",
       [macro("Sweep", ("lfo.cutoff", -0.3, 0.15)),
        macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Gate Depth", GATE_DEPTH),
        macro("Echo", ("fx.delay.mix", -0.18, 0.3), ("fx.delay.feedback", -0.2, 0.25))],
       osc("a", "SuperSAW", voices=7, detune=38, spread=90, phase="Free"),
       osc("b", "SuperSAW", octave=-1, voices=5, detune=28, spread=70, level=45, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1600, resonance=28, keyTrack=30),
       group("amp", attack=2, decay=600, sustain=100, release=260),
       group("lfo", wave="Triangle", sync=True, division="4 Bars", cutoff=45),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=3),
       fx("phaser", type="Phaser", rate=0.1, depth=60, feedback=45, mix=35),
       fx("imager", width=120, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=42, mix=18),
       fx("reverb", type="Plate", decay=2.0, mix=16),
       pattern="Gallop")

preset("Stutter Brass Gate", "Trance Gate", ["Warm", "Punchy"],
       "Synth brass chopped into a 3+3+2 figure. The pattern restarts with every chord, so the brass attack always lands on the first hit.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Gate Depth", GATE_DEPTH),
        macro("Attack", ("filterEnv.attack", -0.1, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "Saw", voices=3, detune=16, spread=45, mix=38, phase="Free"),
       osc("b", "Saw", fine=7, level=65, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=650, resonance=14, drive=25, envAmount=48, keyTrack=50, velocity=40),
       group("filterEnv", attack=20, decay=500, sustain=65, release=200),
       group("amp", attack=4, decay=500, sustain=95, release=140),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=2, restart="Note On"),
       fx("eq", midFreq=2500, midGain=2),
       fx("reverb", type="Room", decay=0.8, mix=12),
       pattern="Trance Drive")

preset("Acid Gate Pulse", "Trance Gate", ["Dark", "Aggressive"],
       "A resonant square line: the gate rolls in sixteenths while a saw LFO drops the filter once per bar. Play low, one or two notes.",
       [macro("Acid", ("filter.resonance", -0.4, 0.2), ("lfo.cutoff", -0.2, 0.2)),
        macro("Tone", ("filter.cutoff", -0.25, 0.3)),
        macro("Gate Depth", GATE_DEPTH),
        macro("Drive", ("fx.drive.drive", -0.3, 0.4))],
       osc("a", "Square", phase="Retrigger"),
       osc("b", "Saw", octave=-1, level=55, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=700, resonance=58, drive=30, keyTrack=40),
       group("amp", attack=1, decay=400, sustain=100, release=80),
       group("lfo", wave="Saw", sync=True, division="1 Bar", cutoff=42),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=1.5),
       fx("drive", type="Tube Saturation", drive=40, tone=60, mix=80),
       fx("delay", division="1/8 D", feedback=30, mix=12),
       pattern="Rolling 16")

preset("Fast Chop Glass", "Trance Gate", ["Bright", "Clean", "Wide"],
       "Glassy pulse chords under a thirty-second-note gate with longer notes mixed in. A busy, glittering top line.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Gate Depth", GATE_DEPTH),
        macro("Smooth", ("gate.smooth", -0.05, 0.4)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.1, 0.2))],
       osc("a", "Pulse", pw=22, voices=2, detune=8, spread=70, phase="Free"),
       osc("b", "Triangle", octave=1, level=50, phase="Free"),
       group("filter", character="Clean", mode="HP12", cutoff=300, resonance=8, keyTrack=30),
       group("amp", attack=1, decay=600, sustain=100, release=180),
       group("gate", enabled=True, steps="16", rate="1/32", depth=100, smooth=1.5),
       fx("chorus", type="Wide", rate=0.7, depth=35, mix=25),
       fx("delay", division="1/8", feedback=25, mix=10),
       fx("reverb", type="Plate", decay=1.8, mix=22),
       pattern="Long Start")

preset("Split Gallop Organ", "Trance Gate", ["Warm", "Clean", "Wide"],
       "A drawbar-style organ: the downbeats hit on the left and the gallop answers on the right. Rhythm guitar for a keyboard player.",
       [macro("Upper", ("osc.b.level", -0.5, 0.4)),
        macro("Gate Width", ("gate.width", -0.8, 0.0)),
        macro("Gate Depth", GATE_DEPTH),
        macro("Drive", ("fx.drive.drive", -0.2, 0.5))],
       osc("a", "Sine", phase="Free"),
       osc("b", "Triangle", octave=1, semitone=7, level=40, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=5000, keyTrack=40),
       group("amp", attack=1, decay=300, sustain=100, release=60),
       group("sub", level=40, octave="-1 Oct", wave="Sine"),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=2, width=85),
       fx("drive", type="Warm Saturation", drive=35, mix=60),
       fx("phaser", type="Phaser", rate=0.8, depth=35, feedback=20, mix=25),
       fx("reverb", type="Room", decay=0.8, mix=12),
       pattern="Split Gallop")

preset("Ghost Pulse Bed", "Trance Gate", ["Dark", "Soft"],
       "A dark bed that never fully closes: the gate only leans on the beat and leaves ghost notes between. Movement under a verse.",
       [macro("Open", ("filter.cutoff", -0.15, 0.4)),
        macro("Gate Depth", ("gate.depth", -0.6, 0.3)),
        macro("Smooth", ("gate.smooth", -0.3, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.15, 0.3))],
       osc("a", "SuperSAW", voices=5, detune=24, spread=60, mix=64, phase="Free"),
       osc("b", "Square", octave=-1, level=40, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=900, resonance=8, drive=20, keyTrack=45),
       group("amp", attack=15, decay=900, sustain=100, release=350),
       group("gate", enabled=True, steps="16", rate="1/16", depth=70, smooth=10),
       fx("eq", highGain=-2),
       fx("reverb", type="Room", decay=1.2, mix=14),
       pattern="Ghost Gallop")

preset("Tresillo Fifths", "Trance Gate", ["Bright", "Punchy"],
       "Root-and-fifth saws hit in a 3+3+2 tresillo. A Latin-pop chord rhythm from one held chord.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Squelch", ("filter.resonance", -0.2, 0.35)),
        macro("Swing", ("gate.swing", 0.0, 0.8)),
        macro("Echo", ("fx.delay.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=2, detune=12, spread=60, phase="Free"),
       osc("b", "Saw", semitone=7, level=60, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=2200, resonance=26, keyTrack=40),
       group("amp", attack=1, decay=500, sustain=100, release=100),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=2),
       fx("chorus", type="Classic", rate=0.7, depth=40, mix=22),
       fx("delay", mode="Ping Pong", division="1/16", feedback=30, mix=14),
       fx("reverb", type="Plate", decay=1.2, mix=12),
       pattern="Tresillo")

preset("Gated Noise Wash", "Trance Gate", ["Bright", "Airy", "FX"],
       "Mostly filtered noise with a thin saw inside it, opened on the offbeats. A pitched hi-hat wash for builds and breakdowns.",
       [macro("Noise", ("noise.level", -0.5, 0.3)),
        macro("Tone", ("filter.cutoff", -0.3, 0.25)),
        macro("Gate Depth", GATE_DEPTH),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "SuperSAW", octave=1, voices=5, detune=30, spread=100, level=45, phase="Free"),
       group("filter", character="Clean", mode="BP12", cutoff=3800, resonance=30, keyTrack=40),
       group("amp", attack=2, decay=600, sustain=100, release=200),
       group("noise", level=70, color=75),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=4),
       fx("imager", width=140),
       fx("reverb", type="Hall", decay=2.8, mix=26),
       pattern="Accent Offbeat")

preset("Cross Pulse Keys", "Trance Gate", ["Warm", "Clean"],
       "Hard digital keys with the gaps of the gate placed differently on each side, plus swing. A shuffled, bouncing chord part.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.25)),
        macro("Swing", ("gate.swing", -0.6, 0.4)),
        macro("Gate Width", ("gate.width", -0.8, 0.0)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Square", phase="Free"),
       osc("b", "Pulse", pw=12, octave=1, level=50, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=7000, keyTrack=60),
       group("amp", attack=1, decay=500, sustain=100, release=120),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=3, swing=30),
       fx("eq", lowGain=-1.5, highGain=2),
       fx("reverb", type="Room", decay=0.9, mix=12),
       pattern="Cross Pulse")

preset("Call Response Square", "Trance Gate", ["Warm", "Hollow", "Wide"],
       "A hollow square phrase on the left answered by the same chord on the right. Leave it alone for two bars and it plays a duet.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("PWM", ("lfo.pulseWidth", -0.3, 0.5)),
        macro("Gate Width", ("gate.width", -0.8, 0.0)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "Pulse", pw=50, phase="Free"),
       osc("b", "Pulse", pw=50, octave=1, fine=5, level=35, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=2600, resonance=15, keyTrack=55),
       group("amp", attack=2, decay=500, sustain=100, release=160),
       group("lfo", wave="Sine", rate=0.7, pulseWidth=35),
       group("gate", enabled=True, steps="16", rate="1/16", depth=100, smooth=4, width=100),
       fx("delay", division="1/4", feedback=25, mix=10),
       fx("reverb", type="Plate", decay=1.6, mix=16),
       pattern="Call Response")

preset("Dotted Arp Gate", "Trance Gate", ["Bright", "Wide"],
       "Arpeggiator and gate together: a sixteenth-note arpeggio with a three-step gate over it, so the accent moves through the notes.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Gate Depth", ("gate.depth", -0.6, 0.2)),
        macro("Arp Gate", ("arp.gate", -0.3, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.15, 0.2))],
       osc("a", "Saw", voices=2, detune=10, spread=50, phase="Retrigger"),
       osc("b", "Triangle", octave=1, level=45, phase="Retrigger"),
       group("filter", character="Clean", mode="LP24", cutoff=1500, resonance=18, envAmount=40, keyTrack=55),
       group("filterEnv", attack=0.5, decay=200, sustain=20, release=160),
       group("amp", attack=0.5, decay=400, sustain=60, release=180),
       group("arp", enabled=True, mode="Up-Down", rate="1/16", octaves=2, gate=85),
       group("gate", enabled=True, steps="16", rate="1/16", depth=75, smooth=3),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=35, mix=16),
       fx("reverb", type="Plate", decay=1.8, mix=18),
       pattern="Dotted 8ths")


# =============================================================================
# Arpeggiator を使う音色の追加 (D-053)。和音を押さえたままにすると Arp がフレーズを作る。
# モード (Up / Down / Random / As Played)、Rate (1/16 / 1/8 / 3 連)、音の長さがどれも重ならないようにした。
# =============================================================================
preset("Trance Arp Saw", "SuperSAW", ["Bright", "Big", "Classic", "Wide"],
       "Hold a chord: a rising two-octave 16th arpeggio of short supersaw notes with a dotted ping-pong echo.",
       [macro("Bright", BRIGHT),
        macro("Arp Gate", ("arp.gate", -0.4, 0.35)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Echo", ("fx.delay.mix", -0.22, 0.25), ("fx.reverb.mix", -0.15, 0.2))],
       osc("a", "SuperSAW", voices=5, detune=26, spread=80, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=3, detune=20, spread=100, level=40, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=1400, resonance=15, envAmount=50, keyTrack=45, velocity=30),
       group("filterEnv", attack=0.5, decay=180, sustain=25, release=200),
       group("amp", attack=1, decay=300, sustain=40, release=260),
       group("arp", enabled=True, mode="Up", rate="1/16", octaves=2, gate=60),
       fx("imager", width=115, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=40, mix=22),
       fx("reverb", type="Plate", decay=2.2, mix=20))

preset("Acid Arp Line", "Lead / Hook", ["Bright", "Aggressive", "Classic"],
       "A falling 16th arpeggio through a squelchy resonant filter that opens and closes over four bars. Hold two or three notes.",
       [macro("Cutoff", ("filter.cutoff", -0.3, 0.3)),
        macro("Squelch", ("filter.resonance", -0.4, 0.25), ("filter.envAmount", -0.2, 0.25)),
        macro("Arp Gate", ("arp.gate", -0.3, 0.4)),
        macro("Drive", ("fx.drive.drive", -0.3, 0.4))],
       osc("a", "Saw", phase="Retrigger"),
       osc("b", "Square", octave=-1, level=35, phase="Retrigger"),
       group("filter", character="Analog", mode="LP24", cutoff=350, resonance=62, drive=40, envAmount=55, keyTrack=40, velocity=40),
       group("filterEnv", attack=0.5, decay=130, sustain=10, release=90),
       group("amp", attack=0.5, decay=220, sustain=50, release=80),
       group("lfo", wave="Triangle", sync=True, division="4 Bars", retrigger=False, cutoff=45),
       group("arp", enabled=True, mode="Down", rate="1/16", octaves=1, gate=50),
       fx("drive", type="Tube Saturation", drive=30, mix=80),
       fx("delay", division="1/8 D", feedback=30, mix=12),
       fx("reverb", type="Room", decay=0.8, mix=8))

preset("Random Glass Arp", "Synth Pluck", ["Bright", "Clean", "Airy"],
       "Glassy plucks scattered at random over three octaves, with a long hall and a ping-pong echo. Hold a chord and let it sparkle.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Arp Gate", ("arp.gate", -0.25, 0.5)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("amp.release", -0.2, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.25), ("fx.delay.mix", -0.25, 0.2))],
       osc("a", "Pulse", pw=30, phase="Retrigger"),
       osc("b", "Sine", octave=2, level=40, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=3000, envAmount=30, keyTrack=60, velocity=35),
       group("filterEnv", attack=0.5, decay=160, sustain=0, release=200),
       group("amp", attack=0.5, decay=320, sustain=0, release=300),
       group("arp", enabled=True, mode="Random", rate="1/16", octaves=3, gate=40),
       fx("delay", mode="Ping Pong", division="1/8", feedback=45, mix=28),
       fx("reverb", type="Hall", decay=3.0, mix=28))

preset("Triplet Harp", "Synth Pluck", ["Warm", "Soft", "Classic"],
       "A rising triplet arpeggio of soft plucks that ring into each other, like a strummed harp. For 6/8 and slow songs.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Ring", ("amp.decay", -0.3, 0.3), ("amp.release", -0.3, 0.3)),
        macro("Arp Gate", ("arp.gate", -0.5, 0.1)),
        macro("Hall", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "Triangle", phase="Retrigger"),
       osc("b", "Saw", level=30, phase="Retrigger"),
       group("filter", character="Analog", mode="LP12", cutoff=1200, envAmount=40, keyTrack=55, velocity=45),
       group("filterEnv", attack=0.5, decay=250, sustain=0, release=400),
       group("amp", attack=1, decay=700, sustain=0, release=600),
       group("arp", enabled=True, mode="Up", rate="1/8T", octaves=2, gate=90),
       fx("chorus", type="Classic", rate=0.5, depth=30, mix=18),
       fx("reverb", type="Hall", decay=2.8, mix=26))

preset("Swing Keys Arp", "Stab / Chord Hit", ["Warm", "Punchy", "Classic"],
       "Plays the notes in the order you press them, in swung eighths. Short electric-piano-like stabs for house and funk grooves.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Swing", ("arp.swing", -0.7, 0.3)),
        macro("Arp Gate", ("arp.gate", -0.3, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.12, 0.3))],
       osc("a", "Triangle", phase="Retrigger"),
       osc("b", "Pulse", pw=25, level=30, phase="Retrigger"),
       group("filter", character="Punch", mode="LP12", cutoff=1600, resonance=10, envAmount=35, keyTrack=50, velocity=50),
       group("filterEnv", attack=0.5, decay=180, sustain=15, release=120),
       group("amp", attack=0.5, decay=350, sustain=20, release=130),
       group("arp", enabled=True, mode="As Played", rate="1/8", octaves=1, gate=55, swing=35),
       fx("drive", type="Warm Saturation", drive=18, mix=60),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Octave Bounce Bass", "Bass", ["Dark", "Punchy", "Tight"],
       "Hold one low note: the arpeggiator bounces it between two octaves in eighths. A dry, centred bass line for under a pad or supersaw.",
       [macro("Cutoff", ("filter.cutoff", -0.25, 0.35)),
        macro("Punch", ("filter.envAmount", -0.3, 0.3), ("filterEnv.decay", -0.15, 0.3)),
        macro("Arp Gate", ("arp.gate", -0.3, 0.4)),
        macro("Sub", ("sub.level", -0.4, 0.4))],
       osc("a", "Saw", phase="Retrigger"),
       osc("b", "Pulse", pw=35, octave=-1, level=40, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=500, resonance=16, drive=25, envAmount=45, keyTrack=40, velocity=35),
       group("filterEnv", attack=0.5, decay=160, sustain=10, release=90),
       group("amp", attack=0.5, decay=260, sustain=30, release=90),
       group("sub", level=45, octave="-1 Oct", wave="Sine"),
       group("arp", enabled=True, mode="Up", rate="1/8", octaves=2, gate=60),
       fx("imager", width=60, monoBass=True))

# =============================================================================
# Bass カテゴリ (D-054)。単音で弾くベース。低域が左右に散らないよう、どれも Imager の Mono Bass を使う。
# 役割が重ならないようにした: 純音に近いサブ / フィルターを開いた明るいソー / うねるリース / 短いドンク / LFO で揺れるワブル。
# =============================================================================
preset("Deep Sub Bass", "Bass", ["Dark", "Clean", "Mix Ready"],
       "A sine with a little triangle an octave up so it still reads on small speakers. Smooth legato glide. Sits under anything.",
       [macro("Tone", ("filter.cutoff", -0.25, 0.35)),
        macro("Harmonics", ("osc.b.level", -0.25, 0.4), ("fx.drive.drive", -0.15, 0.4)),
        macro("Glide", ("voice.glide", -0.1, 0.3)),
        macro("Release", ("amp.release", -0.1, 0.3))],
       osc("a", "Sine", phase="Retrigger"),
       osc("b", "Triangle", octave=1, level=25, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=700, keyTrack=50),
       group("amp", attack=3, decay=400, sustain=90, release=120),
       group("voice", mode="Legato", glide=60),
       fx("drive", type="Warm Saturation", drive=15, mix=50),
       fx("imager", width=50, monoBass=True))

preset("Saw Roller Bass", "Bass", ["Bright", "Big"],
       "Three detuned saws over a saw an octave down, with the filter left open and full sustain. The bright, rolling off-beat bass of trance and EDM.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.3)),
        macro("Detune", ("osc.a.detune", -0.15, 0.3), ("osc.a.spread", -0.3, 0.3)),
        macro("Bite", ("filter.envAmount", -0.15, 0.4), ("filter.resonance", 0.0, 0.3)),
        macro("Release", ("amp.release", -0.05, 0.3))],
       osc("a", "Saw", voices=3, detune=18, spread=50, phase="Free"),
       osc("b", "Saw", octave=-1, level=60, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=1100, resonance=8, envAmount=20, keyTrack=60, velocity=25),
       group("filterEnv", attack=0.5, decay=300, sustain=40, release=120),
       group("amp", attack=1, decay=400, sustain=85, release=70),
       group("voice", mode="Mono"),
       fx("eq", lowGain=1.5, highGain=2),
       fx("imager", width=100, monoBass=True))

preset("Reese Bass", "Bass", ["Dark", "Aggressive"],
       "Detuned saws that beat against each other, with a slow phaser moving through the top. The growling held bass of drum and bass and garage.",
       [macro("Cutoff", ("filter.cutoff", -0.25, 0.4)),
        macro("Detune", ("osc.a.detune", -0.25, 0.4), ("osc.b.fine", -0.05, 0.1)),
        macro("Phase", ("fx.phaser.mix", -0.35, 0.35), ("fx.phaser.rate", 0.0, 0.2)),
        macro("Drive", ("fx.drive.drive", -0.3, 0.4))],
       osc("a", "Saw", voices=2, detune=35, spread=40, phase="Free"),
       osc("b", "Saw", fine=9, level=80, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=600, resonance=10, keyTrack=40),
       group("amp", attack=4, decay=600, sustain=95, release=160),
       group("voice", mode="Legato", glide=90),
       fx("drive", type="Soft Clip", drive=30, mix=70),
       fx("phaser", type="Phaser", rate=0.15, depth=50, feedback=30, mix=35),
       fx("imager", width=110, monoBass=True))

preset("House Donk Bass", "Bass", ["Warm", "Hollow", "Tight"],
       "A square and a sine a twelfth above, with a pitch drop at the start and almost no sustain: a short, hollow organ knock. For off-beat house and garage bass lines.",
       [macro("Tone", ("filter.cutoff", -0.25, 0.35)),
        macro("Donk", ("transient.amount", -0.3, 0.5), ("filter.envAmount", -0.2, 0.25)),
        macro("Length", ("amp.decay", -0.15, 0.35), ("filterEnv.decay", -0.1, 0.25)),
        macro("Room", ("fx.reverb.mix", -0.06, 0.25))],
       osc("a", "Square", phase="Retrigger"),
       osc("b", "Sine", octave=1, semitone=7, level=55, phase="Retrigger"),
       group("filter", character="Punch", mode="LP12", cutoff=400, resonance=8, envAmount=60, keyTrack=55, velocity=30),
       group("filterEnv", attack=0.5, decay=110, sustain=0, release=80),
       group("amp", attack=0.5, decay=240, sustain=0, release=80),
       group("transient", enabled=True, mode="Pitch Snap", amount=30, duration=15, pitchSnap=12),
       group("voice", mode="Mono"),
       fx("imager", width=60, monoBass=True),
       fx("reverb", type="Room", decay=0.5, mix=6))

preset("Wobble Bass", "Bass", ["Dark", "Aggressive"],
       "A driven resonant low-pass opened and closed by a tempo-synced LFO, twice per beat. Hold a note and it talks in time with the song.",
       [macro("Wobble", ("lfo.cutoff", -0.3, 0.2)),
        macro("Cutoff", ("filter.cutoff", -0.2, 0.35)),
        macro("Growl", ("filter.resonance", -0.3, 0.3), ("fx.drive.drive", -0.3, 0.4)),
        macro("Glide", ("voice.glide", -0.05, 0.3))],
       osc("a", "Saw", phase="Free"),
       osc("b", "Square", octave=-1, level=70, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=260, resonance=40, drive=45, keyTrack=30),
       group("amp", attack=2, decay=500, sustain=95, release=110),
       group("voice", mode="Legato", glide=40),
       group("lfo", wave="Sine", sync=True, division="1/8", retrigger=False, cutoff=55),
       fx("drive", type="Tube Saturation", drive=40, mix=75),
       fx("imager", width=80, monoBass=True))

# =============================================================================
# Pad / Strings の追加 (D-058)。和音を長く伸ばす音色。既存の 5 つ (SuperSAW のパッド 3 つ、ソーのアンサンブル、PWM) と
# 音の素材が重ならないようにした: 純音のガラス / スクエアの暗い空洞 / トレモロの弦 / 5 度のドローン / 息のノイズ。
# =============================================================================
preset("Glass Pad", "Pad / Strings", ["Bright", "Clean", "Airy", "Wide"],
       "A triangle with a sine two octaves above, slow to arrive, in a long hall with a quiet echo. Clear and weightless; leaves room for a vocal.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2), ("osc.b.level", -0.2, 0.3)),
        macro("Swell", ("amp.attack", -0.15, 0.35)),
        macro("Shimmer", ("fx.chorus.mix", -0.25, 0.35), ("fx.chorus.depth", -0.2, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3), ("fx.delay.mix", -0.14, 0.25))],
       osc("a", "Triangle", voices=2, detune=8, spread=60, phase="Free"),
       osc("b", "Sine", octave=2, level=35, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=5000, keyTrack=50),
       group("amp", attack=250, decay=1500, sustain=85, release=1400),
       group("noise", level=6, color=90),
       fx("chorus", type="Wide", rate=0.3, depth=45, mix=30),
       fx("delay", mode="Ping Pong", division="1/4 D", feedback=35, mix=14),
       fx("reverb", type="Hall", decay=5.5, preDelay=30, mix=38, tone=60))

preset("Hollow Night Pad", "Pad / Strings", ["Dark", "Hollow", "Soft", "Wide"],
       "Two squares an octave apart, slightly out of tune, behind a low filter that drifts slowly on its own. A dark, hollow bed for verses and intros.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.4)),
        macro("Drift", ("lfo.cutoff", -0.2, 0.4), ("lfo.rate", -0.02, 0.1)),
        macro("Swell", ("amp.attack", -0.2, 0.3), ("filterEnv.attack", -0.1, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "Square", phase="Free"),
       osc("b", "Square", octave=-1, fine=7, level=65, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=520, resonance=14, envAmount=18, keyTrack=45),
       group("filterEnv", attack=900, decay=2500, sustain=70, release=1200),
       group("amp", attack=350, decay=1500, sustain=95, release=1100),
       group("lfo", wave="Sine", rate=0.12, cutoff=22),
       fx("chorus", type="Classic", rate=0.25, depth=35, mix=25),
       fx("reverb", type="Hall", decay=4.2, mix=30, tone=40))

preset("Tremolo Strings", "Pad / Strings", ["Warm", "Classic", "Wide"],
       "A saw section whose volume trembles quickly, like bows shaking on the string. Tension under a scene; the mod wheel adds vibrato on top.",
       [macro("Tremolo", ("lfo.amp", -0.45, 0.3)),
        macro("Speed", ("lfo.rate", -0.12, 0.15)),
        macro("Bright", ("filter.cutoff", -0.25, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=5, detune=16, spread=70, mix=60, phase="Free"),
       osc("b", "Saw", octave=1, voices=3, detune=12, spread=90, level=35, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=2600, keyTrack=45, velocity=20),
       group("amp", attack=90, decay=900, sustain=90, release=450),
       group("lfo", wave="Triangle", rate=6.5, retrigger=False, amp=55),
       group("performance", vibratoDepth=25, vibratoRate=5.8, vibratoDelay=300),
       fx("eq", lowGain=-2, midFreq=2500, midGain=1.5),
       fx("reverb", type="Hall", decay=2.6, preDelay=20, mix=26))

preset("Fifth Drone Pad", "Pad / Strings", ["Warm", "Big", "Wide"],
       "Saws a fifth apart over a sub, opening very slowly over several seconds. Hold one or two notes and let it grow under a build.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.35)),
        macro("Rise", ("filterEnv.attack", -0.3, 0.3), ("filter.envAmount", -0.25, 0.25)),
        macro("Sub", ("sub.level", -0.4, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3), ("fx.reverb.decay", -0.1, 0.25))],
       osc("a", "Saw", voices=3, detune=14, spread=50, phase="Free"),
       osc("b", "Saw", semitone=7, voices=3, detune=14, spread=100, level=55, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=350, resonance=20, drive=15, envAmount=50, keyTrack=30),
       group("filterEnv", attack=3500, decay=4000, sustain=80, release=1500),
       group("amp", attack=500, decay=2000, sustain=100, release=1500),
       group("sub", level=40, octave="-1 Oct", wave="Sine"),
       fx("imager", width=120, monoBass=True),
       fx("reverb", type="Hall", decay=5.0, mix=30))

preset("Breath Pad", "Pad / Strings", ["Warm", "Airy", "Soft", "Wide"],
       "A sine and a soft triangle with a layer of breath: noise through a band-pass that follows the keys. A gentle, flute-like pad; press into the keys to open it.",
       [macro("Air", ("noise.level", -0.25, 0.3), ("filter.resonance", -0.2, 0.2)),
        macro("Tone", ("filter.cutoff", -0.2, 0.25)),
        macro("Swell", ("amp.attack", -0.15, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3))],
       osc("a", "Sine", level=90, phase="Free"),
       osc("b", "Triangle", octave=1, level=30, phase="Free"),
       group("filter", character="Clean", mode="BP12", cutoff=900, resonance=30, keyTrack=100),
       group("amp", attack=200, decay=1200, sustain=90, release=800),
       group("noise", level=28, color=60),
       group("performance", aftertouchCutoff=30, vibratoDepth=20, vibratoDelay=400),
       fx("chorus", type="Wide", rate=0.35, depth=30, mix=22),
       fx("reverb", type="Hall", decay=3.6, preDelay=20, mix=32))

# =============================================================================
# Lead / Hook の追加 (D-059)。既存の 7 つ (SuperSAW の単音、スクエアのレガート、ウェーブフォールド、サイン、
# グライドのソー、ワウ、アシッドの Arp) と役割が重ならないようにした:
# 5 度のパワーリード / 短く切れるフック / オクターブを往復するチップ音 / うなり上がるフーバー / 自動ビブラートの口笛。
# =============================================================================
preset("Fifth Power Lead", "Lead / Hook", ["Bright", "Big", "Aggressive"],
       "Two saws a fifth apart through tube drive: every single note is already a power chord. For riffs and loud, simple hooks.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Drive", ("fx.drive.drive", -0.3, 0.4), ("filter.drive", -0.2, 0.3)),
        macro("Fifth", ("osc.b.level", -0.6, 0.2)),
        macro("Echo", ("fx.delay.mix", -0.15, 0.3))],
       osc("a", "Saw", voices=2, detune=10, spread=30, phase="Free"),
       osc("b", "Saw", semitone=7, level=65, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=4200, resonance=12, drive=30, envAmount=20, keyTrack=60),
       group("filterEnv", attack=1, decay=300, sustain=50, release=180),
       group("amp", attack=1, decay=300, sustain=90, release=130),
       group("voice", mode="Mono", glide=15),
       group("performance", vibratoDepth=30, vibratoRate=6.0, vibratoDelay=250),
       fx("drive", type="Tube Saturation", drive=45, tone=60, mix=85),
       fx("eq", lowGain=-2, midFreq=1800, midGain=2),
       fx("delay", division="1/8", feedback=28, mix=14, ducking=50),
       fx("reverb", type="Room", decay=0.9, mix=10))

preset("Staccato Hook", "Lead / Hook", ["Bright", "Punchy", "Clean"],
       "A short, bright note that dies away by itself and leaves a dotted echo behind. For fast melodic hooks; it can also play two-note harmonies.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Length", ("amp.decay", -0.2, 0.35), ("filterEnv.decay", -0.15, 0.3)),
        macro("Snap", ("filter.envAmount", -0.3, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.24, 0.25), ("fx.delay.feedback", -0.2, 0.2))],
       osc("a", "Saw", phase="Retrigger"),
       osc("b", "Square", octave=1, level=40, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=1800, resonance=18, envAmount=50, keyTrack=65, velocity=35),
       group("filterEnv", attack=0.5, decay=200, sustain=10, release=160),
       group("amp", attack=0.5, decay=280, sustain=10, release=160),
       fx("eq", lowGain=-2, highGain=2),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=42, mix=26),
       fx("reverb", type="Plate", decay=1.4, mix=14))

preset("Chip Octave Lead", "Lead / Hook", ["Bright", "Hollow", "Classic"],
       "A narrow pulse that flips up an octave and back on every 16th, in time with the song. The dry, bleeping lead of old game music.",
       [macro("Width", ("osc.a.pulseWidth", -0.3, 0.5)),
        macro("Trill", ("lfo.pitch", -1.0, 0.0)),
        macro("Soften", ("filter.cutoff", -0.5, 0.0)),
        macro("Echo", ("fx.delay.mix", 0.0, 0.4))],
       osc("a", "Pulse", pw=25, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=14000, keyTrack=30),
       group("amp", attack=0.5, decay=200, sustain=85, release=40),
       group("voice", mode="Mono"),
       group("lfo", wave="Square", sync=True, division="1/16", retrigger=True, pitch=1200),
       fx("delay", division="1/8", feedback=30, mix=0))

preset("Hoover Lead", "Lead / Hook", ["Dark", "Aggressive", "Big", "Classic", "Wide"],
       "Thick detuned pulses that swoop up into every note, with their width churning underneath. The growling rave lead; slides between overlapping notes.",
       [macro("Bright", ("filter.cutoff", -0.25, 0.35)),
        macro("Swoop", ("transient.amount", -0.8, 0.0), ("transient.duration", -0.3, 0.3)),
        macro("Churn", ("lfo.pulseWidth", -0.5, 0.3), ("osc.a.detune", -0.2, 0.3)),
        macro("Space", ("fx.reverb.mix", -0.15, 0.3))],
       osc("a", "Pulse", pw=50, voices=5, detune=34, spread=80, mix=60, phase="Free"),
       osc("b", "Saw", octave=-1, voices=3, detune=22, spread=60, level=60, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=2400, resonance=10, drive=30, keyTrack=50),
       group("amp", attack=4, decay=400, sustain=95, release=180),
       group("transient", enabled=True, mode="Pitch Snap", amount=100, duration=90, pitchSnap=-12),
       group("voice", mode="Legato", glide=70),
       group("lfo", wave="Sine", rate=4.5, pulseWidth=60),
       fx("chorus", type="Classic", rate=0.6, depth=45, mix=30),
       fx("eq", lowGain=-2, midFreq=1200, midGain=2),
       fx("reverb", type="Hall", decay=1.8, mix=16))

preset("Whistle Glide Lead", "Lead / Hook", ["Warm", "Hollow", "Classic"],
       "A high, slightly nasal tone that slides slowly from note to note, with a wide vibrato that arrives by itself after a moment. An eerie, singing lead in the manner of a theremin.",
       [macro("Glide", ("voice.glide", -0.1, 0.3)),
        macro("Vibrato", ("lfo.pitch", -0.03, 0.05), ("lfo.rate", -0.05, 0.08)),
        macro("Tone", ("osc.b.level", -0.18, 0.4), ("filter.cutoff", -0.15, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.25, 0.3), ("fx.reverb.decay", -0.1, 0.2))],
       osc("a", "Triangle", octave=1, phase="Free"),
       osc("b", "Saw", octave=1, level=18, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=1900, resonance=20, keyTrack=80),
       group("amp", attack=60, decay=400, sustain=95, release=350),
       group("voice", mode="Legato", glide=320),
       group("lfo", wave="Sine", rate=5.8, fade=600, retrigger=True, pitch=38),
       group("performance", aftertouchVibrato=40),
       fx("reverb", type="Hall", decay=3.2, preDelay=20, mix=30))


# =============================================================================
# Synth Brass の追加 (D-061)。128 音色への絞り込み (D-060) のあとの最初の分。既存の 12 と役割が重ならないようにした:
# 短く乾いたホーンのスタブ / 明るく開いたアリーナ系の和音 / 単音のソロ / 低音域のセクション /
# ミュートを付けた細い音 / スーパーソーを重ねた現代的なブラス。
# =============================================================================
preset("Tight Horn Stabs", "Synth Brass", ["Bright", "Punchy", "Tight", "Mix Ready"],
       "Short, dry horn-section jabs that die away by themselves even if you hold the keys. Funk and pop accents on the off-beats.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Length", ("amp.decay", -0.15, 0.35), ("filterEnv.decay", -0.12, 0.3)),
        macro("Spit", ("transient.amount", -0.3, 0.4), ("filter.envAmount", -0.15, 0.2)),
        macro("Room", ("fx.reverb.mix", -0.06, 0.3))],
       osc("a", "Saw", voices=2, detune=9, spread=35, phase="Retrigger"),
       osc("b", "Pulse", pw=35, level=45, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=700, resonance=14, drive=20, envAmount=58, keyTrack=55, velocity=50),
       group("filterEnv", attack=6, decay=150, sustain=10, release=70),
       group("amp", attack=2, decay=240, sustain=8, release=60),
       group("transient", enabled=True, mode="Noise Burst", amount=22, duration=14, noiseColor=70),
       fx("eq", lowGain=-2, midFreq=2200, midGain=3, highGain=1),
       fx("reverb", type="Room", decay=0.4, mix=6))

preset("Arena Brass", "Synth Brass", ["Bright", "Big", "Classic"],
       "Bright, open brass chords that speak at once and stay bright while held. The big keyboard-riff sound of eighties arena rock.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.b.fine", -0.05, 0.08)),
        macro("Attack", ("filterEnv.attack", -0.05, 0.3), ("amp.attack", -0.02, 0.15)),
        macro("Plate", ("fx.reverb.mix", -0.16, 0.3))],
       osc("a", "Saw", voices=4, detune=20, spread=70, mix=50, phase="Free"),
       osc("b", "Saw", fine=-8, level=75, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=2600, resonance=6, envAmount=34, keyTrack=45, velocity=30),
       group("filterEnv", attack=12, decay=500, sustain=72, release=260),
       group("amp", attack=4, decay=600, sustain=95, release=230),
       fx("drive", type="Tape Saturation", drive=18, mix=60),
       fx("eq", lowGain=1, midFreq=2800, midGain=2, highGain=2.5),
       fx("imager", width=115, monoBass=True),
       fx("reverb", type="Plate", decay=1.7, preDelay=20, mix=17))

preset("Solo Brass Lead", "Synth Brass", ["Warm", "Classic"],
       "One player, not a section: a single-voice brass line that slurs between overlapping notes and adds vibrato on held ones. For melodies and solos.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Vibrato", ("lfo.pitch", -0.02, 0.03), ("lfo.fade", -0.1, 0.15)),
        macro("Tongue", ("filterEnv.attack", -0.06, 0.25), ("filter.envAmount", -0.2, 0.2)),
        macro("Echo", ("fx.delay.mix", -0.12, 0.3), ("fx.reverb.mix", -0.1, 0.2))],
       osc("a", "Saw", phase="Free"),
       osc("b", "Pulse", pw=30, level=35, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=620, resonance=16, drive=25, envAmount=52, keyTrack=65, velocity=50),
       group("filterEnv", attack=30, decay=450, sustain=58, release=180),
       group("amp", attack=8, decay=400, sustain=92, release=150),
       group("voice", mode="Legato", glide=45),
       group("lfo", wave="Sine", rate=5.4, fade=450, retrigger=True, pitch=16),
       group("performance", aftertouchCutoff=25),
       fx("eq", midFreq=1600, midGain=2.5),
       fx("delay", division="1/8", feedback=24, mix=12, ducking=50),
       fx("reverb", type="Plate", decay=1.5, mix=14))

preset("Low Brass Section", "Synth Brass", ["Dark", "Big"],
       "Trombones and tubas: sounds an octave below the keys, round and heavy, with a slow breath into each note. Low lines and sustained roots under a track.",
       [macro("Bright", ("filter.cutoff", -0.15, 0.3)),
        macro("Weight", ("sub.level", -0.4, 0.4)),
        macro("Breath", ("filterEnv.attack", -0.12, 0.3), ("amp.attack", -0.08, 0.2)),
        macro("Hall", ("fx.reverb.mix", -0.14, 0.3))],
       osc("a", "Saw", octave=-1, voices=3, detune=11, spread=30, mix=40, phase="Free"),
       osc("b", "Triangle", octave=-1, level=55, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=300, resonance=8, drive=30, envAmount=44, keyTrack=40, velocity=40),
       group("filterEnv", attack=70, decay=600, sustain=55, release=280),
       group("amp", attack=30, decay=600, sustain=92, release=260),
       group("sub", level=40, octave="-1 Oct", wave="Square"),
       fx("eq", lowGain=2, midFreq=700, midGain=2, highGain=-2),
       fx("imager", width=90, monoBass=True),
       fx("reverb", type="Hall", decay=2.0, mix=14))

preset("Muted Brass", "Synth Brass", ["Bright", "Hollow", "Tight"],
       "Thin, nasal and buzzy, like trumpets with mutes in. Small enough to sit behind a vocal; quiet riffs and answering phrases.",
       [macro("Mute", ("filter.cutoff", -0.2, 0.25)),
        macro("Buzz", ("filter.resonance", -0.3, 0.3), ("filter.drive", -0.2, 0.4)),
        macro("Length", ("amp.decay", -0.2, 0.3), ("amp.sustain", -0.4, 0.3)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "Pulse", pw=18, phase="Retrigger"),
       osc("b", "Saw", level=50, phase="Retrigger"),
       group("filter", character="Punch", mode="BP12", cutoff=1700, resonance=38, drive=30, envAmount=22, keyTrack=70, velocity=40),
       group("filterEnv", attack=10, decay=220, sustain=40, release=120),
       group("amp", attack=4, decay=350, sustain=60, release=110),
       fx("eq", lowGain=-4, midFreq=2000, midGain=2),
       fx("reverb", type="Room", decay=0.7, mix=10))

preset("Hyper Brass", "Synth Brass", ["Bright", "Big", "Aggressive", "Wide"],
       "Brass built from two supersaw stacks: a hard filter punch on every chord, then a wide wall. Modern dance-pop choruses and drops.",
       [macro("Bright", BRASS_BRIGHT),
        macro("Punch", ("filter.envAmount", -0.3, 0.2), ("filterEnv.decay", -0.1, 0.25)),
        macro("Width", ("osc.a.spread", -0.6, 0.1), ("fx.imager.width", -0.2, 0.15)),
        macro("Space", ("fx.reverb.mix", -0.14, 0.3), ("fx.delay.mix", -0.06, 0.2))],
       osc("a", "Saw", voices=7, detune=30, spread=90, mix=65, phase="Free"),
       osc("b", "Saw", octave=1, voices=7, detune=26, spread=100, mix=65, level=45, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=1100, resonance=10, drive=30, envAmount=56, keyTrack=45, velocity=35),
       group("filterEnv", attack=3, decay=260, sustain=62, release=240),
       group("amp", attack=2, decay=500, sustain=92, release=220),
       group("sub", level=25, octave="-1 Oct", wave="Square"),
       fx("eq", lowGain=1, midFreq=3200, midGain=1.5, highGain=3),
       fx("imager", width=130, monoBass=True),
       fx("delay", division="1/8", feedback=18, mix=6, ducking=60),
       fx("reverb", type="Plate", decay=1.6, mix=14))


# =============================================================================
# SuperSAW の追加 (D-062)。既存の 11 (広い 2 オクターブ、乾いた和音、最大音量、狭い低音、暗い下地、5 度、壁、
# フェイザー、フランジャー、高音だけ、Arp) と役割が重ならないようにした:
# 短く消えるプラック / テンポに合わせてうねる和音 / 強く歪ませた音 / 下から滑り込む音 /
# エコーが返る短い和音 / テープのように揺れる古びた音 / 弾く強さで明るさが変わる音。
# =============================================================================
preset("Trance Pluck Saw", "SuperSAW", ["Bright", "Punchy", "Classic", "Wide"],
       "A supersaw chord that snaps bright and falls away at once, leaving reverb behind. The plucked chord of trance and dance-pop; play rhythms, not held notes.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Length", ("filterEnv.decay", -0.15, 0.35), ("amp.decay", -0.15, 0.35)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.delay.mix", -0.1, 0.25))],
       osc("a", "SuperSAW", voices=7, detune=30, spread=85, phase="Free"),
       osc("b", "Saw", octave=1, level=30, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=500, resonance=10, envAmount=70, keyTrack=45, velocity=30),
       group("filterEnv", attack=0.5, decay=260, sustain=0, release=220),
       group("amp", attack=1, decay=420, sustain=0, release=240),
       fx("eq", lowGain=-2, highGain=2),
       fx("imager", width=115, monoBass=True),
       fx("delay", division="1/8", feedback=25, mix=10),
       fx("reverb", type="Plate", decay=2.0, mix=22))

preset("Wub Saw Chords", "SuperSAW", ["Bright", "Big", "Wide"],
       "Hold a chord and the filter opens and closes on every eighth note, in time with the song. The swelling chords of future bass, with no sidechain to set up.",
       [macro("Wub", ("lfo.cutoff", -0.3, 0.2)),
        macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Detune", ("osc.a.detune", -0.25, 0.3), ("osc.b.detune", -0.2, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.18, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=38, spread=90, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=5, detune=30, spread=100, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP24", cutoff=900, resonance=8, keyTrack=40),
       group("amp", attack=4, decay=800, sustain=92, release=200),
       group("sub", level=22, octave="-1 Oct", wave="Square"),
       group("lfo", wave="Saw", sync=True, division="1/8", retrigger=True, cutoff=60, amp=25),
       fx("chorus", type="Wide", rate=0.5, depth=35, mix=20),
       fx("imager", width=125, monoBass=True),
       fx("reverb", type="Hall", decay=2.2, mix=18))

preset("Overdrive Saw", "SuperSAW", ["Bright", "Aggressive", "Big"],
       "A supersaw pushed hard into a clipper until it screams: mid-heavy, flat and loud. Hard dance leads and two-note power intervals; full chords turn to mud.",
       [macro("Drive", ("fx.drive.drive", -0.4, 0.3), ("filter.drive", -0.3, 0.3)),
        macro("Bright", ("filter.cutoff", -0.3, 0.2), ("fx.drive.tone", -0.3, 0.2)),
        macro("Detune", ("osc.a.detune", -0.25, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Room", ("fx.reverb.mix", -0.1, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=34, spread=60, phase="Free"),
       osc("b", "Square", octave=-1, level=35, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=6500, resonance=14, drive=50, keyTrack=30),
       group("amp", attack=2, decay=600, sustain=90, release=150),
       fx("drive", type="Hard Clip", drive=62, tone=60, mix=90),
       fx("eq", lowGain=-2, midFreq=2000, midGain=4, highGain=-1),
       fx("reverb", type="Room", decay=0.8, mix=10))

preset("Scoop Saw", "SuperSAW", ["Bright", "Big", "Wide"],
       "Every chord slides up into pitch from an octave below while the filter opens behind it, like a short riser built into the note. For the first chord of a chorus and big, spaced-out hits.",
       [macro("Scoop", ("transient.duration", -0.4, 0.4), ("transient.amount", -0.5, 0.0)),
        macro("Open", ("filter.cutoff", -0.1, 0.35), ("filterEnv.attack", -0.3, 0.2)),
        macro("Detune", ("osc.a.detune", -0.25, 0.3), ("osc.b.detune", -0.2, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3), ("fx.reverb.decay", -0.1, 0.2))],
       osc("a", "SuperSAW", voices=7, detune=36, spread=90, phase="Free"),
       osc("b", "SuperSAW", octave=-1, voices=5, detune=28, spread=70, level=50, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=700, resonance=18, drive=15, envAmount=62, keyTrack=30),
       group("filterEnv", attack=280, decay=900, sustain=60, release=300),
       group("amp", attack=20, decay=900, sustain=90, release=350),
       group("transient", enabled=True, mode="Pitch Snap", amount=100, duration=100, pitchSnap=-12),
       group("noise", level=8, color=80),
       fx("imager", width=125, monoBass=True),
       fx("reverb", type="Hall", decay=2.8, mix=24))

preset("Echo Saw Chords", "SuperSAW", ["Bright", "Airy", "Wide"],
       "Short supersaw chords answered by a long dotted echo that bounces left and right. Play one chord and leave room: the echoes fill the bar.",
       [macro("Echo", ("fx.delay.mix", -0.3, 0.2), ("fx.delay.feedback", -0.25, 0.2)),
        macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Length", ("amp.decay", -0.15, 0.35), ("amp.sustain", -0.25, 0.4)),
        macro("Dark Echo", ("fx.delay.tone", -0.4, 0.2))],
       osc("a", "SuperSAW", voices=5, detune=28, spread=75, phase="Free"),
       osc("b", "SuperSAW", octave=1, voices=3, detune=22, spread=100, level=30, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=4500, envAmount=25, keyTrack=35, velocity=25),
       group("filterEnv", attack=1, decay=350, sustain=30, release=200),
       group("amp", attack=2, decay=450, sustain=25, release=180),
       fx("eq", lowGain=-3, highGain=1.5),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=55, tone=55, mix=34),
       fx("reverb", type="Plate", decay=1.8, mix=14))

preset("Lo-Fi Tape Saw", "SuperSAW", ["Warm", "Soft", "Classic", "Mix Ready"],
       "A boxy, narrow supersaw whose pitch drifts like a worn cassette, with no deep lows or highs. Chords fade slowly while held, like a piano sampled off tape. For lo-fi and chill tracks.",
       [macro("Wow", ("lfo.pitch", -0.012, 0.02), ("lfo.rate", -0.02, 0.06)),
        macro("Tone", ("filter.cutoff", -0.2, 0.3)),
        macro("Hiss", ("noise.level", -0.08, 0.3)),
        macro("Tape", ("fx.drive.drive", -0.3, 0.4))],
       osc("a", "SuperSAW", voices=5, detune=24, spread=40, mix=45, phase="Free"),
       osc("b", "Triangle", octave=1, level=40, phase="Free"),
       group("filter", character="Analog", mode="LP12", cutoff=2600, resonance=10, drive=30, envAmount=20, keyTrack=40, velocity=25),
       group("filterEnv", attack=2, decay=900, sustain=20, release=300),
       group("amp", attack=4, decay=1800, sustain=30, release=300),
       group("noise", level=10, color=35),
       group("lfo", wave="Sine", rate=0.8, pitch=20),
       fx("drive", type="Tape Saturation", drive=45, tone=40, mix=85),
       fx("eq", lowGain=-5, midFreq=900, midGain=3.5, highGain=-5),
       fx("imager", width=60),
       fx("reverb", type="Room", decay=0.9, mix=10))

preset("Velocity Saw", "SuperSAW", ["Warm", "Punchy", "Dynamic"],
       "Play softly and it stays dark and quiet; hit hard and it opens into a full bright supersaw. Pressing into held keys opens it further. For parts played by hand with real dynamics.",
       [macro("Range", ("filter.velocity", -0.6, 0.1), ("amp.velocity", -0.5, 0.0)),
        macro("Bright", ("filter.cutoff", -0.15, 0.3)),
        macro("Detune", ("osc.a.detune", -0.2, 0.3), ("osc.a.unisonMix", -0.20, 0.25)),
        macro("Room", ("fx.reverb.mix", -0.14, 0.3))],
       osc("a", "SuperSAW", voices=7, detune=28, spread=70, phase="Free"),
       osc("b", "Saw", octave=-1, level=30, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=350, resonance=8, drive=15, envAmount=35, keyTrack=50, velocity=90),
       group("filterEnv", attack=2, decay=500, sustain=55, release=250),
       group("amp", attack=2, decay=700, sustain=85, release=240),
       group("performance", aftertouchCutoff=35),
       fx("eq", midFreq=2500, midGain=1.5),
       fx("reverb", type="Plate", decay=1.4, mix=14))


# =============================================================================
# Bright Poly の追加 (D-063)。既存の 7 (細いパルスのガラス、ソー + スクエアの鍵盤、高音の層、ベル、硬いデジタル、
# 丸く空洞の音、トレモロの鍵盤) と役割が重ならないようにした:
# 伸び続けるオルガン / 定番のソーのポリシンセ / 短く跳ねるクラビ風 / デチューンしたスクエアの和音 /
# レゾナンスが下りてくるスイープ。
# =============================================================================
preset("Chorus Organ", "Bright Poly", ["Warm", "Clean", "Classic", "Wide"],
       "A plain synth organ: full level the instant a key goes down, gone the instant it comes up, with a fast chorus swirling around it. Held chords and gliding riffs.",
       [macro("Upper", ("osc.b.level", -0.5, 0.4)),
        macro("Swirl", ("fx.chorus.mix", -0.4, 0.3), ("fx.chorus.rate", -0.1, 0.25)),
        macro("Click", ("transient.amount", -0.2, 0.5)),
        macro("Grit", ("fx.drive.drive", -0.2, 0.5))],
       osc("a", "Sine", phase="Free"),
       osc("b", "Triangle", octave=1, level=50, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=9000, keyTrack=30),
       group("amp", attack=1, decay=200, sustain=100, release=35, velocity=20),
       group("sub", level=45, octave="-1 Oct", wave="Sine"),
       group("transient", enabled=True, mode="Noise Burst", amount=18, duration=5, noiseColor=65),
       fx("drive", type="Tube Saturation", drive=20, mix=60),
       fx("chorus", type="Wide", rate=1.4, depth=50, mix=45),
       fx("reverb", type="Room", decay=0.9, mix=12))

preset("Classic Saw Poly", "Bright Poly", ["Bright", "Classic", "Mix Ready"],
       "The everyday polysynth: two slightly detuned saws, a filter that opens a little on each note, and a touch of chorus. Sustained chords for almost any song.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.25)),
        macro("Detune", ("osc.b.fine", -0.04, 0.08), ("fx.chorus.mix", -0.15, 0.3)),
        macro("Release", ("amp.release", -0.15, 0.35)),
        macro("Space", ("fx.reverb.mix", -0.14, 0.3))],
       osc("a", "Saw", phase="Free"),
       osc("b", "Saw", fine=8, level=85, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=3200, resonance=12, envAmount=24, keyTrack=50, velocity=30),
       group("filterEnv", attack=3, decay=600, sustain=55, release=300),
       group("amp", attack=3, decay=800, sustain=85, release=300),
       fx("chorus", type="Classic", rate=0.6, depth=35, mix=22),
       fx("eq", lowGain=-1, highGain=1.5),
       fx("reverb", type="Plate", decay=1.4, mix=14))

preset("Funk Clav Keys", "Bright Poly", ["Bright", "Punchy", "Tight"],
       "A thin, biting pulse with a quick resonant snap on every note and no tail: the clavinet-style part. Sixteenth-note funk comping; hit harder for more bite.",
       [macro("Bite", ("filter.envAmount", -0.3, 0.25), ("filter.resonance", -0.2, 0.25)),
        macro("Thin", ("osc.a.pulseWidth", -0.1, 0.3)),
        macro("Length", ("amp.decay", -0.15, 0.35), ("filterEnv.decay", -0.1, 0.3)),
        macro("Phase", ("fx.phaser.mix", 0.0, 0.5))],
       osc("a", "Pulse", pw=14, phase="Retrigger"),
       osc("b", "Saw", level=35, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=800, resonance=26, drive=25, envAmount=55, keyTrack=70, velocity=60),
       group("filterEnv", attack=0.5, decay=150, sustain=15, release=60),
       group("amp", attack=0.5, decay=450, sustain=25, release=45),
       fx("phaser", type="Phaser", rate=0.4, depth=60, feedback=35, mix=0),
       fx("eq", lowGain=-3, midFreq=1800, midGain=3, highGain=1),
       fx("reverb", type="Room", decay=0.4, mix=5))

preset("Wide Square Poly", "Bright Poly", ["Bright", "Hollow", "Big", "Wide"],
       "Five detuned square waves per note, spread wide: hollow but huge. Bright, open chords with the colour of an old game console turned up to anthem size.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Detune", ("osc.a.detune", -0.25, 0.3), ("osc.a.unisonMix", -0.25, 0.2)),
        macro("Octave", ("osc.b.level", -0.35, 0.4)),
        macro("Space", ("fx.reverb.mix", -0.18, 0.3), ("fx.delay.mix", -0.08, 0.2))],
       osc("a", "Square", voices=5, detune=26, spread=90, mix=60, phase="Free"),
       osc("b", "Square", octave=1, voices=3, detune=18, spread=100, level=35, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=7500, envAmount=12, keyTrack=40),
       group("amp", attack=4, decay=900, sustain=85, release=300),
       fx("eq", lowGain=-1.5, highGain=2),
       fx("imager", width=125, monoBass=True),
       fx("delay", division="1/8", feedback=22, mix=8),
       fx("reverb", type="Plate", decay=1.9, mix=18))

preset("Resonant Sweep Poly", "Bright Poly", ["Bright", "Classic"],
       "Each note starts with the filter wide open and whistles its way down over a second. The falling, vowel-like sweep of vintage polysynths; best on slow chords and single held notes.",
       [macro("Sweep Time", ("filterEnv.decay", -0.3, 0.3)),
        macro("Whistle", ("filter.resonance", -0.4, 0.2)),
        macro("Depth", ("filter.envAmount", -0.4, 0.15), ("filter.cutoff", -0.1, 0.25)),
        macro("Space", ("fx.reverb.mix", -0.16, 0.3), ("fx.delay.mix", -0.12, 0.25))],
       osc("a", "Saw", voices=2, detune=10, spread=50, phase="Free"),
       osc("b", "Pulse", pw=40, octave=-1, level=40, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=350, resonance=62, envAmount=80, keyTrack=40, velocity=20),
       group("filterEnv", attack=1, decay=1300, sustain=10, release=500),
       group("amp", attack=3, decay=1500, sustain=75, release=500),
       fx("chorus", type="Classic", rate=0.4, depth=30, mix=15),
       fx("delay", division="1/4", feedback=25, mix=12),
       fx("reverb", type="Plate", decay=1.8, mix=16))


# =============================================================================
# 残りの 7 音色 (D-064)。これで 128 音色。カテゴリごとに、まだない役割を足した:
# Synth Pluck: ミュートしたギター風 / 木のマレット / 長い残響に溶ける柔らかい音
# Stab / Chord Hit: 暗い和音に長いエコー (ダブ) / 短い弦のスタブ
# Bass: 頭で音程が落ちて長く減衰するサブ
# Octave / Layered: オクターブを重ねた単音のリード
# =============================================================================
preset("Palm Mute Pluck", "Synth Pluck", ["Warm", "Punchy", "Tight", "Mix Ready"],
       "A very short, choked note with a dull thump, like a guitar string muted with the palm. Dry sixteenth-note patterns that drive a verse without filling it up.",
       [macro("Open", ("filter.cutoff", -0.15, 0.35), ("filterEnv.decay", -0.1, 0.3)),
        macro("Length", ("amp.decay", -0.1, 0.35)),
        macro("Drive", ("fx.drive.drive", -0.3, 0.4)),
        macro("Echo", ("fx.delay.mix", 0.0, 0.35))],
       osc("a", "Saw", phase="Retrigger"),
       osc("b", "Pulse", pw=30, fine=5, level=50, phase="Retrigger"),
       group("filter", character="Punch", mode="LP24", cutoff=420, resonance=10, drive=30, envAmount=42, keyTrack=60, velocity=45),
       group("filterEnv", attack=0.5, decay=70, sustain=0, release=60),
       group("amp", attack=0.5, decay=160, sustain=0, release=60),
       fx("drive", type="Tube Saturation", drive=35, tone=45, mix=75),
       fx("eq", lowGain=-2, midFreq=900, midGain=2.5),
       fx("delay", division="1/8 D", feedback=30, mix=0),
       fx("reverb", type="Room", decay=0.4, mix=6))

preset("Wood Mallet Pluck", "Synth Pluck", ["Warm", "Clean", "Soft"],
       "A round, woody knock with a short ring, like a marimba or a thumb piano. Repeating patterns and gentle melodies.",
       [macro("Ring", ("amp.decay", -0.2, 0.35), ("amp.release", -0.15, 0.3)),
        macro("Overtone", ("osc.b.level", -0.3, 0.45)),
        macro("Knock", ("transient.amount", -0.3, 0.4)),
        macro("Room", ("fx.reverb.mix", -0.14, 0.3), ("fx.delay.mix", 0.0, 0.25))],
       osc("a", "Triangle", phase="Retrigger"),
       osc("b", "Sine", octave=2, level=28, phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=2200, envAmount=35, keyTrack=70, velocity=45),
       group("filterEnv", attack=0.5, decay=120, sustain=0, release=120),
       group("amp", attack=0.5, decay=420, sustain=0, release=260, velocity=80),
       group("transient", enabled=True, mode="Both", amount=30, duration=6, noiseColor=25, pitchSnap=5),
       fx("delay", division="1/8", feedback=25, mix=0),
       fx("reverb", type="Room", decay=1.0, mix=15))

preset("Dream Pluck", "Synth Pluck", ["Bright", "Airy", "Soft", "Wide"],
       "A soft-edged pluck that melts into a long, wide hall. Slow arpeggios and sparse notes in intros and breakdowns; too washy for fast parts.",
       [macro("Bright", ("filter.cutoff", -0.2, 0.3)),
        macro("Soft", ("amp.attack", -0.02, 0.25), ("filter.envAmount", -0.2, 0.2)),
        macro("Shimmer", ("osc.b.level", -0.3, 0.4), ("fx.chorus.mix", -0.2, 0.3)),
        macro("Hall", ("fx.reverb.mix", -0.3, 0.2), ("fx.reverb.decay", -0.15, 0.25))],
       osc("a", "Saw", voices=3, detune=18, spread=80, mix=50, phase="Free"),
       osc("b", "Triangle", octave=1, level=45, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=900, envAmount=45, keyTrack=55, velocity=30),
       group("filterEnv", attack=4, decay=500, sustain=10, release=600),
       group("amp", attack=6, decay=900, sustain=0, release=900),
       fx("chorus", type="Wide", rate=0.4, depth=45, mix=30),
       fx("imager", width=130, monoBass=True),
       fx("delay", mode="Ping Pong", division="1/4", feedback=35, mix=14),
       fx("reverb", type="Hall", decay=5.0, preDelay=25, mix=38))

preset("Dub Chord Stab", "Stab / Chord Hit", ["Dark", "Classic", "Wide"],
       "A dark, muffled chord that barely speaks, then echoes away for bars, each repeat duller than the last. The chord of dub techno; play one and wait.",
       [macro("Open", ("filter.cutoff", -0.15, 0.35), ("filter.envAmount", -0.2, 0.25)),
        macro("Echo", ("fx.delay.feedback", -0.3, 0.15), ("fx.delay.mix", -0.25, 0.2)),
        macro("Dark Echo", ("fx.delay.tone", -0.25, 0.4)),
        macro("Space", ("fx.reverb.mix", -0.2, 0.3))],
       osc("a", "Saw", voices=3, detune=16, spread=60, mix=45, phase="Free"),
       osc("b", "Pulse", pw=35, level=50, phase="Free"),
       group("filter", character="Analog", mode="LP24", cutoff=380, resonance=28, drive=20, envAmount=40, keyTrack=40, velocity=40),
       group("filterEnv", attack=1, decay=180, sustain=10, release=160),
       group("amp", attack=1, decay=260, sustain=0, release=160),
       fx("chorus", type="Classic", rate=0.3, depth=40, mix=25),
       fx("delay", mode="Ping Pong", division="1/8 D", feedback=68, tone=30, mix=42),
       fx("reverb", type="Hall", decay=3.5, mix=24))

preset("String Stab", "Stab / Chord Hit", ["Bright", "Classic", "Airy", "Wide"],
       "A short bowed-string jab, high and thin, with a soft front edge instead of a click. Disco and pop string accents; play them an octave above the chords.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Bow", ("amp.attack", -0.04, 0.2)),
        macro("Length", ("amp.decay", -0.15, 0.35), ("amp.sustain", -0.2, 0.4)),
        macro("Plate", ("fx.reverb.mix", -0.18, 0.3))],
       osc("a", "SuperSAW", octave=1, voices=7, detune=24, spread=80, phase="Free"),
       osc("b", "Saw", level=40, phase="Free"),
       group("filter", character="Clean", mode="LP12", cutoff=5500, keyTrack=40, velocity=25),
       group("amp", attack=22, decay=280, sustain=20, release=180),
       fx("chorus", type="Classic", rate=0.5, depth=35, mix=22),
       fx("eq", lowGain=-4, midFreq=3000, midGain=1.5, highGain=2),
       fx("reverb", type="Plate", decay=1.6, mix=20))

preset("Boom Sub Bass", "Bass", ["Dark", "Big", "Punchy"],
       "A distorted sine that falls two octaves into pitch with a hard thump and then dies away over a second and a half. The tuned kick-bass of trap and hip-hop: every note hits again, and overlapping notes slide.",
       [macro("Length", ("amp.decay", -0.3, 0.3)),
        macro("Thump", ("transient.amount", -0.5, 0.0), ("transient.duration", -0.15, 0.25)),
        macro("Dirt", ("fx.drive.drive", -0.3, 0.5)),
        macro("Glide", ("voice.glide", -0.1, 0.3))],
       osc("a", "Sine", phase="Retrigger"),
       group("filter", character="Clean", mode="LP12", cutoff=1000, keyTrack=40),
       group("amp", attack=0.5, decay=1500, sustain=0, release=120),
       group("transient", enabled=True, mode="Both", amount=100, duration=60, noiseColor=20, pitchSnap=24),
       group("voice", mode="Mono", glide=60),
       fx("drive", type="Soft Clip", drive=50, tone=45, mix=85),
       fx("imager", width=40, monoBass=True))

preset("Octave Lead", "Octave / Layered", ["Bright", "Big", "Classic"],
       "One voice in two octaves: a narrow supersaw with a saw an octave above, sliding slightly between overlapping notes. A melody line that stays in front of a full mix.",
       [macro("Bright", ("filter.cutoff", -0.3, 0.2)),
        macro("Octave", ("osc.b.level", -0.6, 0.3)),
        macro("Glide", ("voice.glide", -0.03, 0.3)),
        macro("Echo", ("fx.delay.mix", -0.18, 0.25), ("fx.reverb.mix", -0.1, 0.2))],
       osc("a", "SuperSAW", voices=5, detune=20, spread=45, mix=40, phase="Free"),
       osc("b", "Saw", octave=1, level=60, phase="Free"),
       group("filter", character="Punch", mode="LP24", cutoff=5200, resonance=10, drive=15, envAmount=18, keyTrack=50, velocity=25),
       group("filterEnv", attack=1, decay=400, sustain=50, release=200),
       group("amp", attack=2, decay=500, sustain=88, release=160),
       group("voice", mode="Legato", glide=30),
       group("performance", vibratoDepth=25, vibratoDelay=300),
       fx("eq", lowGain=-2, midFreq=2400, midGain=2),
       fx("delay", division="1/8 D", feedback=30, mix=18, ducking=55),
       fx("reverb", type="Plate", decay=1.5, mix=12))


def main():
    os.makedirs(ROOT, exist_ok=True)
    levels_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "preset_levels.json")
    levels = json.load(open(levels_path)) if os.path.exists(levels_path) else {}

    # 先に全部を組み立て、最後にまとめて入れ替える (途中で失敗しても、フォルダーが半端な状態にならない)
    files = {}

    for number, p in enumerate(PRESETS, start=1):
        parameters = dict(p["parameters"])

        # 音量の整合: 実測に基づく補正値 (tools/level_presets.py が書く) を preset.level に入れる
        if p["name"] in levels:
            parameters["preset.level"] = levels[p["name"]]

        data = {
            "schemaVersion": 1,
            "id": str(uuid.uuid5(NAMESPACE, p["name"])),
            "name": p["name"],
            "category": p["category"],
            "tags": p["tags"],
            "author": "POLYBRISK",
            "description": p["description"],
            "parameters": parameters,
            "macros": [dict(index=i + 1, **m) for i, m in enumerate(p["macros"])],
        }

        if isinstance(p["pattern"], str):
            # 収録パターンを名前で指定。右チャンネルを持つものは Gate の Stereo を ON にする
            left, right = PATTERNS[p["pattern"]]
            data["rhythm"] = {"gatePatternName": p["pattern"], "gatePatternEdited": False, "gatePattern": left * 2}
            if right is not None:
                data["rhythm"]["gatePatternRight"] = right * 2
                parameters["gate.stereo"] = True
        elif p["pattern"]:
            # 音色専用のパターン (名前, ステップ値の並び)。32 ステップになるまで繰り返す
            name, values = p["pattern"]
            repeated = (list(values) * (32 // len(values) + 1))[:32]
            data["rhythm"] = {"gatePatternName": name, "gatePatternEdited": True, "gatePattern": repeated}

        slug = "".join(c if c.isalnum() else "_" for c in p["name"]).strip("_")
        files[f"{number:03d}_{slug}.json"] = json.dumps(data, indent=2, ensure_ascii=False) + "\n"

    for existing in os.listdir(ROOT):
        if existing.endswith(".json") and existing not in files:
            os.remove(os.path.join(ROOT, existing))

    for name, text in files.items():
        with open(os.path.join(ROOT, name), "w") as f:
            f.write(text)

    print(f"{len(PRESETS)} presets written to {os.path.normpath(ROOT)}")


if __name__ == "__main__":
    main()
