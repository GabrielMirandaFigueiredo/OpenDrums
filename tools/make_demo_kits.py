#!/usr/bin/env python3
"""Generates two small synthetic demo kits (samples + piece pictures + kit.json).

Everything is synthesised from scratch by this script, so the output is CC0 (public domain).
They are placeholders to test the plugin (layers, round robin, choke, piece swapping,
visual kit view); real kits (DrumGizmo/AVL/etc.) plug into the same kit.json format.

usage: make_demo_kits.py <output_folder>
"""
import json
import math
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100
LAYERS = [(1, 50, 0.30), (51, 95, 0.60), (96, 127, 0.95)]  # velMin, velMax, peak level


# ------------------------------------------------------------------ synthesis helpers
def t_axis(seconds):
    return np.arange(int(seconds * SR)) / SR


def hp(x, a=0.95):
    """One-pole high-pass (differencing) to make noise brighter."""
    y = np.zeros_like(x)
    prev_x = 0.0
    prev_y = 0.0
    for i, v in enumerate(x):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y[i] = prev_y
    return y


def lp(x, a=0.2):
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += a * (v - acc)
        y[i] = acc
    return y


def env(t, decay, attack=0.0015):
    e = np.exp(-t / decay)
    if attack > 0:
        e *= np.minimum(1.0, t / attack)
    return e


def sweep(t, f_start, f_end, rate):
    f = f_end + (f_start - f_end) * np.exp(-t * rate)
    phase = 2 * math.pi * np.cumsum(f) / SR
    return np.sin(phase)


def finish(x, peak, fade_ms=8):
    x = x - np.mean(x)
    m = np.max(np.abs(x)) or 1.0
    x = x / m * peak
    n = int(fade_ms / 1000 * SR)
    x[-n:] *= np.linspace(1, 0, n)
    return x


def write_wav(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)


# ------------------------------------------------------------------ instruments
def kick(rng, p, hardness, rr):
    t = t_axis(p["kick_len"])
    f0 = p["kick_f"] * (1 + 0.01 * (rr - 1))
    body = sweep(t, f0 * 3.2, f0, 38) * env(t, p["kick_decay"], 0.001)
    click = hp(rng.standard_normal(len(t))) * env(t, 0.006, 0.0002) * (0.15 + 0.5 * hardness)
    return body + click


def snare(rng, p, hardness, rr):
    t = t_axis(p["snare_len"])
    f = p["snare_f"] * (1 + 0.008 * (rr - 1))
    tone = (np.sin(2 * math.pi * f * t) + 0.5 * np.sin(2 * math.pi * f * 1.6 * t)) * env(t, 0.07)
    noise = hp(rng.standard_normal(len(t)), 0.9) * env(t, p["snare_decay"])
    return tone * 0.7 + noise * (0.5 + 0.6 * hardness)


def rimshot(rng, p, rr):
    t = t_axis(0.18)
    tone = np.sin(2 * math.pi * p["snare_f"] * 1.9 * t) * env(t, 0.03)
    noise = hp(rng.standard_normal(len(t)), 0.9) * env(t, 0.06)
    return tone + noise * 0.8


def sidestick(rng, p, rr):
    t = t_axis(0.12)
    tone = np.sin(2 * math.pi * p["snare_f"] * 2.6 * t) * env(t, 0.012)
    click = hp(rng.standard_normal(len(t))) * env(t, 0.01)
    return tone + click * 0.6


def tom(rng, p, freq, hardness, rr):
    t = t_axis(0.7)
    f = freq * (1 + 0.006 * (rr - 1))
    body = sweep(t, f * 1.5, f, 22) * env(t, p["tom_decay"], 0.001)
    stick = hp(rng.standard_normal(len(t))) * env(t, 0.008, 0.0002) * (0.1 + 0.4 * hardness)
    return body + stick


def hat(rng, p, kind, hardness, rr):
    length = {"closed": 0.18, "pedal": 0.12, "open": 0.9}[kind]
    decay = {"closed": 0.03, "pedal": 0.02, "open": 0.28}[kind]
    t = t_axis(length)
    n = hp(rng.standard_normal(len(t)), 0.93)
    metal = sum(np.sin(2 * math.pi * f * (1 + 0.004 * rr) * t) for f in (3470, 4790, 6230, 7840, 9300))
    return (n * 0.8 + metal * 0.08 * (0.5 + hardness)) * env(t, decay * p["hat_decay_mul"], 0.0005)


def cymbal(rng, p, kind, hardness, rr):
    length = {"crash": 2.2, "ride": 1.6, "bell": 1.0}[kind]
    t = t_axis(length)
    base = {"crash": 330.0, "ride": 420.0, "bell": 620.0}[kind] * p["cymbal_pitch"]
    n = hp(rng.standard_normal(len(t)), 0.94)
    partials = sum(
        np.sin(2 * math.pi * base * m * (1 + 0.003 * rr) * t + k)
        for k, m in enumerate((1.0, 1.47, 2.09, 2.56, 3.34, 4.18))
    )
    if kind == "bell":
        return partials * env(t, 0.5, 0.0008) + n * env(t, 0.02, 0.0005) * 0.3
    decay = {"crash": 0.7, "ride": 0.45}[kind] * p["cymbal_decay_mul"]
    shimmer = n * env(t, decay, 0.001)
    return shimmer * 0.7 + partials * 0.05 * env(t, decay * 0.8, 0.001) * (0.5 + hardness)


# ------------------------------------------------------------------ pictures (SVG)
# The image is exactly "diameter*10" units wide, so the editor can size it by diameter.
def svg_wrap(w, h, body, label):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" '
        f'width="{w:.0f}" height="{h:.0f}"><title>{label}</title>{body}</svg>\n'
    )


def svg_drum(d, depth, shell, rim, head, lugs, label, snare_wires=False):
    w = d * 10
    top_h = w * 0.34
    h = top_h + depth * 10 + 6
    cx = w / 2
    body = (
        f'<path d="M3 {top_h/2:.1f} L3 {top_h/2 + depth*10:.1f} A{w/2-3:.1f} {top_h/2:.1f} 0 0 0 {w-3:.1f} '
        f'{top_h/2 + depth*10:.1f} L{w-3:.1f} {top_h/2:.1f} Z" fill="{shell}"/>'
        f'<ellipse cx="{cx:.1f}" cy="{top_h/2:.1f}" rx="{w/2-3:.1f}" ry="{top_h/2-2:.1f}" fill="{rim}"/>'
        f'<ellipse cx="{cx:.1f}" cy="{top_h/2:.1f}" rx="{w/2-9:.1f}" ry="{top_h/2-7:.1f}" fill="{head}"/>'
    )
    for i in range(lugs):
        x = 14 + (w - 28) * (i + 0.5) / lugs
        body += f'<rect x="{x-3:.1f}" y="{top_h/2 + depth*5:.1f}" width="6" height="{depth*2.2:.1f}" rx="2" fill="{rim}"/>'
    if snare_wires:
        body += f'<rect x="{w*0.18:.1f}" y="{top_h/2 + depth*10 + 1:.1f}" width="{w*0.64:.1f}" height="3" fill="#bdbdbd"/>'
    return svg_wrap(w, h, body, label)


def svg_kick(d, shell, rim, head, label):
    w = d * 10
    body = (
        f'<circle cx="{w/2:.1f}" cy="{w/2:.1f}" r="{w/2-2:.1f}" fill="{rim}"/>'
        f'<circle cx="{w/2:.1f}" cy="{w/2:.1f}" r="{w/2-12:.1f}" fill="{head}"/>'
        f'<circle cx="{w/2:.1f}" cy="{w/2:.1f}" r="{w/2-12:.1f}" fill="none" stroke="{shell}" stroke-width="6"/>'
        f'<ellipse cx="{w/2:.1f}" cy="{w*0.64:.1f}" rx="{w*0.09:.1f}" ry="{w*0.09:.1f}" fill="{shell}" opacity="0.55"/>'
    )
    return svg_wrap(w, w, body, label)


def svg_hat(d, metal, label):
    w = d * 10
    h = w * 0.28
    body = (
        f'<ellipse cx="{w/2:.1f}" cy="{h*0.62:.1f}" rx="{w/2-2:.1f}" ry="{h*0.30:.1f}" fill="{metal}" opacity="0.75"/>'
        f'<ellipse cx="{w/2:.1f}" cy="{h*0.38:.1f}" rx="{w/2-2:.1f}" ry="{h*0.32:.1f}" fill="{metal}"/>'
        f'<ellipse cx="{w/2:.1f}" cy="{h*0.34:.1f}" rx="{w*0.07:.1f}" ry="{h*0.1:.1f}" fill="#333"/>'
    )
    return svg_wrap(w, h, body, label)


def svg_cymbal(d, metal, label, bell=0.16):
    w = d * 10
    h = w * 0.30
    body = (
        f'<ellipse cx="{w/2:.1f}" cy="{h/2:.1f}" rx="{w/2-2:.1f}" ry="{h/2-2:.1f}" fill="{metal}"/>'
        f'<ellipse cx="{w/2:.1f}" cy="{h/2:.1f}" rx="{w*bell:.1f}" ry="{h*bell*1.5:.1f}" fill="#ffffff" opacity="0.30"/>'
        f'<ellipse cx="{w/2:.1f}" cy="{h/2:.1f}" rx="{w*0.03:.1f}" ry="{h*0.07:.1f}" fill="#333"/>'
    )
    return svg_wrap(w, h, body, label)


# ------------------------------------------------------------------ kit definitions
KITS = {
    "demo-rock": {
        "name": "Demo Rock (sintético)",
        "seed": 11,
        "params": dict(kick_f=52, kick_decay=0.22, kick_len=0.7, snare_f=185, snare_decay=0.18, snare_len=0.5,
                       tom_decay=0.34, hat_decay_mul=1.0, cymbal_pitch=1.0, cymbal_decay_mul=1.0),
        "colors": dict(shell="#7a1f1f", rim="#c9c9c9", head="#efe7d8", metal="#c8a24a"),
        "toms": dict(tom1=170, tom2=140, floor=95),
        "sizes": dict(kick=22, snare=14, tom1=10, tom2=12, floor=16, hihat=14, crash1=16, crash2=18, ride=20),
        "labels": dict(kick='Bumbo 22" (sintético)', snare='Caixa 14" (sintético)', tom1='Tom 10" (sintético)',
                       tom2='Tom 12" (sintético)', floor='Surdo 16" (sintético)', hihat='Chimbal 14" (sintético)',
                       crash1='Prato de ataque 16" (sintético)', crash2='Prato de ataque 18" (sintético)',
                       ride='Prato de condução 20" (sintético)'),
        "pos": dict(kick=(0.50, 0.70), snare=(0.31, 0.63), tom1=(0.43, 0.36), tom2=(0.58, 0.36),
                    floor=(0.73, 0.64), hihat=(0.13, 0.47), crash1=(0.26, 0.20), crash2=(0.66, 0.16),
                    ride=(0.87, 0.40)),
    },
    "demo-tight": {
        "name": "Demo Tight (sintético)",
        "seed": 23,
        "params": dict(kick_f=63, kick_decay=0.13, kick_len=0.45, snare_f=238, snare_decay=0.11, snare_len=0.35,
                       tom_decay=0.2, hat_decay_mul=0.7, cymbal_pitch=1.18, cymbal_decay_mul=0.7),
        "colors": dict(shell="#1f3b57", rim="#8c8f93", head="#f4f4ee", metal="#b9b9c4"),
        "toms": dict(tom1=210, tom2=165, floor=118),
        "sizes": dict(kick=20, snare=13, tom1=8, tom2=10, floor=14, hihat=13, crash1=14, crash2=16, ride=18),
        "labels": dict(kick='Bumbo 20" (sintético)', snare='Caixa 13" (sintético)', tom1='Tom 8" (sintético)',
                       tom2='Tom 10" (sintético)', floor='Surdo 14" (sintético)', hihat='Chimbal 13" (sintético)',
                       crash1='Prato de ataque 14" (sintético)', crash2='Prato de ataque 16" (sintético)',
                       ride='Prato de condução 18" (sintético)'),
        "pos": dict(kick=(0.50, 0.70), snare=(0.31, 0.63), tom1=(0.43, 0.36), tom2=(0.58, 0.36),
                    floor=(0.73, 0.64), hihat=(0.13, 0.47), crash1=(0.26, 0.20), crash2=(0.66, 0.16),
                    ride=(0.87, 0.40)),
    },
}


def build_kit(out_root: Path, kit_id: str, cfg: dict):
    kit_dir = out_root / kit_id
    rng_master = np.random.default_rng(cfg["seed"])
    p = cfg["params"]
    c = cfg["colors"]
    pieces = []
    layout = []

    def rr_files(piece_id, role, layer_idx, maker, peak, n_rr=3):
        files = []
        for rr in range(n_rr):
            rng = np.random.default_rng(rng_master.integers(1 << 31))
            x = finish(maker(rng, rr), peak)
            rel = f"samples/{piece_id}_{role}_L{layer_idx}_rr{rr + 1}.wav"
            write_wav(kit_dir / rel, x)
            files.append(rel)
        return files

    def layered(piece_id, role, maker_for_hardness, n_rr=3, layers=LAYERS):
        out = []
        for i, (lo, hi, peak) in enumerate(layers):
            hardness = (i + 1) / len(layers)
            out.append(
                dict(velMin=lo, velMax=hi,
                     files=rr_files(piece_id, role, i + 1, lambda rng, rr, h=hardness: maker_for_hardness(rng, rr, h), peak, n_rr))
            )
        return {"layers": out}

    def single(piece_id, role, maker, peak=0.9, n_rr=3):
        return {"layers": [dict(velMin=1, velMax=127, files=rr_files(piece_id, role, 1, maker, peak, n_rr))]}

    def add_image(piece_id, svg):
        rel = f"images/{piece_id}.svg"
        (kit_dir / "images").mkdir(parents=True, exist_ok=True)
        (kit_dir / rel).write_text(svg, encoding="utf-8")
        return rel

    sizes, labels = cfg["sizes"], cfg["labels"]

    # kick
    pieces.append(dict(
        id="kick", type="kick", label=labels["kick"], diameter=sizes["kick"],
        image=add_image("kick", svg_kick(sizes["kick"], c["shell"], c["rim"], c["head"], labels["kick"])),
        gainDb=0.0,
        articulations={"hit": layered("kick", "hit", lambda rng, rr, h: kick(rng, p, h, rr))}))
    # snare
    pieces.append(dict(
        id="snare", type="snare", label=labels["snare"], diameter=sizes["snare"],
        image=add_image("snare", svg_drum(sizes["snare"], 5.5, c["shell"], c["rim"], c["head"], 8, labels["snare"], True)),
        gainDb=-1.0,
        articulations={
            "hit": layered("snare", "hit", lambda rng, rr, h: snare(rng, p, h, rr)),
            "rimshot": single("snare", "rimshot", lambda rng, rr: rimshot(rng, p, rr), 0.95, 2),
            "sidestick": single("snare", "sidestick", lambda rng, rr: sidestick(rng, p, rr), 0.8, 2)}))
    # toms
    for tid in ("tom1", "tom2", "floor"):
        pieces.append(dict(
            id=tid, type="tom", label=labels[tid], diameter=sizes[tid],
            image=add_image(tid, svg_drum(sizes[tid], sizes[tid] * 0.85, c["shell"], c["rim"], c["head"], 6, labels[tid])),
            gainDb=-1.0, pan={"tom1": -0.25, "tom2": 0.1, "floor": 0.35}[tid],
            articulations={"hit": layered(tid, "hit", lambda rng, rr, h, f=cfg["toms"][tid]: tom(rng, p, f, h, rr))}))
    # hi-hat
    pieces.append(dict(
        id="hihat", type="hihat", label=labels["hihat"], diameter=sizes["hihat"],
        image=add_image("hihat", svg_hat(sizes["hihat"], c["metal"], labels["hihat"])),
        gainDb=-4.0, pan=-0.35,
        articulations={
            "closed": layered("hihat", "closed", lambda rng, rr, h: hat(rng, p, "closed", h, rr)),
            "pedal": single("hihat", "pedal", lambda rng, rr: hat(rng, p, "pedal", 0.4, rr), 0.5, 2),
            "open": layered("hihat", "open", lambda rng, rr, h: hat(rng, p, "open", h, rr), 2, LAYERS[1:])}))
    # cymbals
    cym_layers = [(1, 80, 0.5), (81, 127, 0.9)]
    for cid, kind in (("crash1", "crash"), ("crash2", "crash"), ("ride", "ride")):
        arts = {"hit": layered(cid, "hit", lambda rng, rr, h, k=kind: cymbal(rng, p, k, h, rr), 2, cym_layers)}
        if kind == "ride":
            arts["bell"] = single(cid, "bell", lambda rng, rr: cymbal(rng, p, "bell", 0.8, rr), 0.8, 2)
        pieces.append(dict(
            id=cid, type=kind, label=labels[cid], diameter=sizes[cid],
            image=add_image(cid, svg_cymbal(sizes[cid], c["metal"], labels[cid], 0.1 if kind == "crash" else 0.07)),
            gainDb=-5.0, pan={"crash1": -0.5, "crash2": 0.55, "ride": 0.6}[cid], articulations=arts))

    for slot, (x, y) in cfg["pos"].items():
        layout.append(dict(slot=slot, piece=slot, x=x, y=y))

    doc = dict(
        format=1, id=kit_id, name=cfg["name"], author="OpenDrums (gerado por tools/make_demo_kits.py)",
        license="CC0-1.0",
        description="Kit sintético de demonstração: sons gerados por código, só para testar o plugin.",
        pieces=pieces, layout=layout)
    (kit_dir / "kit.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    n = len(list((kit_dir / "samples").glob("*.wav")))
    print(f"  {kit_id}: {len(pieces)} peças, {n} samples")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    print(f"Gerando kits de demonstração em {out}")
    for kit_id, cfg in KITS.items():
        build_kit(out, kit_id, cfg)


if __name__ == "__main__":
    main()
