#!/usr/bin/env python3
"""Generates two electronic drum kits (808-style and 909-style) as OpenDrums kits.

Every sound is synthesised from scratch by this script with the classic analogue-drum recipes (swept sine kicks and toms,
tone + noise snares, six detuned square oscillators for the metallic sounds, filtered-noise claps), so the output is CC0.
They are NOT recordings of Roland hardware: they are original sounds built the same way.

usage: make_electronic_kits.py <output_folder>
"""
import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).parent))
from make_demo_kits import SR, svg_cymbal, svg_drum, svg_hat, svg_kick, svg_wrap, t_axis, write_wav  # noqa: E402

LAYERS5 = [(1, 30, 0.16), (31, 62, 0.28), (63, 92, 0.42), (93, 112, 0.56), (113, 127, 0.70)]
CYM_LAYERS = [(1, 60, 0.25), (61, 100, 0.40), (101, 127, 0.55)]


# ------------------------------------------------------------------ DSP helpers
def env(t, decay, attack=0.0008):
    e = np.exp(-t / decay)
    return e * np.minimum(1.0, t / attack) if attack > 0 else e


def sine_sweep(t, f_hi, f_lo, tau):
    f = f_lo + (f_hi - f_lo) * np.exp(-t / tau)
    return np.sin(2 * math.pi * np.cumsum(f) / SR)


def bandpass(x, f, q=1.0):
    sos = signal.butter(2, [f / q * 0.5 if False else f * (1 - 0.5 / q), min(f * (1 + 0.5 / q), SR * 0.45)], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def highpass(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype="high", fs=SR, output="sos"), x)


def lowpass(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype="low", fs=SR, output="sos"), x)


def drive(x, amount):
    """Soft saturation. The input is scaled to a peak of 1 first, so `amount` only changes how rounded the peaks get
    (and never flattens the decay of the tail)."""
    x = x / (np.max(np.abs(x)) or 1.0)
    a = 0.4 + 0.35 * amount
    return np.tanh(a * x) / np.tanh(a)


def squares(t, freqs, rng):
    out = np.zeros_like(t)
    for f in freqs:
        ph = rng.uniform(0, 1)
        out += np.sign(np.sin(2 * math.pi * (f * t + ph)))
    return out / len(freqs)


def finish(x, peak, fade_ms=10):
    x = x - np.mean(x)
    m = np.max(np.abs(x)) or 1.0
    x = x / m * peak
    n = int(fade_ms / 1000 * SR)
    x[-n:] *= np.linspace(1, 0, n)
    return x


METAL_808 = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
METAL_909 = [263.0, 400.0, 421.0, 474.0, 587.0, 845.0]


# ------------------------------------------------------------------ the two machines
class Machine:
    def __init__(self, kind):
        self.k = kind  # "808" or "909"
        self.metal = METAL_808 if kind == "808" else METAL_909

    def kick(self, rng, h):
        n = 1.1 if self.k == "808" else 0.6
        t = np.arange(int(n * SR)) / SR
        jit = 1 + rng.normal(0, 0.01)
        if self.k == "808":
            x = sine_sweep(t, (120 + 80 * h) * jit, 47 * jit, 0.030) * env(t, 0.55 + 0.25 * h, 0.001)
            click = highpass(rng.standard_normal(len(t)), 1800) * env(t, 0.003, 0) * 0.15 * h
            return drive(x + click, 0.8 + 1.5 * h)
        x = sine_sweep(t, (210 + 120 * h) * jit, 52 * jit, 0.016) * env(t, 0.20 + 0.10 * h, 0.0007)
        click = highpass(rng.standard_normal(len(t)), 2500) * env(t, 0.004, 0) * (0.3 + 0.4 * h)
        return drive(x * 1.4 + click, 1.5 + 2.5 * h)

    def snare(self, rng, h):
        n = 0.6
        t = np.arange(int(n * SR)) / SR
        j = 1 + rng.normal(0, 0.008)
        if self.k == "808":
            tone = np.sin(2 * math.pi * 185 * j * t) * env(t, 0.07) + 0.6 * np.sin(2 * math.pi * 330 * j * t) * env(t, 0.045)
            noise = highpass(rng.standard_normal(len(t)), 1700) * env(t, 0.13 + 0.07 * h, 0.0005)
            return drive(tone * 0.9 + noise * (0.7 + 0.5 * h), 1.0 + h)
        tone = np.sin(2 * math.pi * 180 * j * t) * env(t, 0.09) + 0.7 * np.sin(2 * math.pi * 330 * j * t) * env(t, 0.05)
        noise = highpass(rng.standard_normal(len(t)), 2200) * env(t, 0.19 + 0.08 * h, 0.0004)
        return drive(tone * 0.8 + noise * (1.0 + 0.6 * h), 1.5 + 1.5 * h)

    def rimshot(self, rng, h=0.8):
        t = np.arange(int(0.25 * SR)) / SR
        x = np.sin(2 * math.pi * 480 * t) * env(t, 0.012) + np.sin(2 * math.pi * 1700 * t) * env(t, 0.006) * 0.7
        x += bandpass(rng.standard_normal(len(t)), 3000, 1.2) * env(t, 0.005) * 0.3
        return drive(x, 1.5)

    def clave(self, rng, h=0.8):
        t = np.arange(int(0.3 * SR)) / SR
        f = 2450 * (1 + rng.normal(0, 0.005))
        return drive(np.sin(2 * math.pi * f * t) * env(t, 0.035, 0.0003) + 0.3 * np.sin(2 * math.pi * f * 1.5 * t) * env(t, 0.02), 1.2)

    def clap(self, rng, h):
        n = 0.5
        t = np.arange(int(n * SR)) / SR
        noise = bandpass(rng.standard_normal(len(t)), 1100 if self.k == "808" else 1400, 1.4)
        out = np.zeros_like(t)
        for off in (0.0, 0.009, 0.018, 0.027):
            i = int(off * SR)
            out[i:] += noise[: len(t) - i] * env(t[: len(t) - i], 0.005, 0.0003) * 0.6
        main = int(0.030 * SR)
        out[main:] += noise[: len(t) - main] * env(t[: len(t) - main], 0.11 if self.k == "808" else 0.15, 0.001)
        return drive(out, 1.0 + h)

    def tom(self, rng, f, h):
        t = np.arange(int(0.8 * SR)) / SR
        j = 1 + rng.normal(0, 0.008)
        if self.k == "808":
            x = sine_sweep(t, f * 1.5 * j, f * j, 0.045) * env(t, 0.28 + 0.12 * h, 0.001)
            return drive(x, 0.6 + h)
        x = sine_sweep(t, f * 1.9 * j, f * j, 0.025) * env(t, 0.22 + 0.10 * h, 0.0007)
        x += highpass(rng.standard_normal(len(t)), 1800) * env(t, 0.012, 0) * 0.25
        return drive(x, 1.2 + 1.5 * h)

    def hat(self, rng, kind, h):
        dec = {"closed": 0.040 + 0.03 * h, "pedal": 0.028, "open": 0.35 + 0.25 * h}[kind]
        if self.k == "909":
            dec *= 0.85 if kind != "open" else 1.1
        t = np.arange(int((dec * 6 + 0.05) * SR)) / SR
        x = squares(t, self.metal, rng)
        x = highpass(bandpass(x, 8000, 1.2), 6500, 2)
        if self.k == "909":
            x += highpass(rng.standard_normal(len(t)), 7000) * 0.35
        return drive(x * env(t, dec, 0.0004), 0.8 + h)

    def cymbal(self, rng, kind, h):
        n, dec = {"crash": (3.2, 1.1 if self.k == "808" else 1.4), "ride": (2.4, 0.8), "splash": (1.4, 0.45), "china": (2.6, 0.9), "bell": (1.6, 0.5)}[kind]
        t = np.arange(int(n * SR)) / SR
        mul = {"crash": 1.0, "ride": 1.15, "splash": 1.3, "china": 0.8, "bell": 1.6}[kind]
        x = squares(t, [f * mul * 1.9 for f in self.metal], rng)
        x = highpass(x, 4500 if kind != "bell" else 3000, 2)
        noise = highpass(rng.standard_normal(len(t)), 5500) * (0.25 if kind == "ride" else 0.5)
        mix = (x + noise) * env(t, dec, 0.001)
        if kind in ("crash", "china"):
            mix *= 0.6 + 0.4 * env(t, 0.06, 0)  # initial splash
        return drive(mix, 0.6 + h)

    def cowbell(self, rng, h):
        t = np.arange(int(0.6 * SR)) / SR
        x = np.sign(np.sin(2 * math.pi * 540 * t)) + np.sign(np.sin(2 * math.pi * 800 * t))
        x = bandpass(x, 800, 1.6)
        return drive(x * (0.7 * env(t, 0.012, 0.0003) + 0.3 * env(t, 0.20, 0.0003)), 1.0 + h)

    def maracas(self, rng, h):
        t = np.arange(int(0.2 * SR)) / SR
        return highpass(rng.standard_normal(len(t)), 5000) * env(t, 0.025 + 0.01 * h, 0.006)


def pad_svg(w, h, color, label, led):
    body = (f'<rect x="3" y="3" width="{w-6}" height="{h-6}" rx="{h*0.18:.1f}" fill="#1c1e24"/>'
            f'<rect x="{w*0.08:.1f}" y="{h*0.12:.1f}" width="{w*0.84:.1f}" height="{h*0.76:.1f}" rx="{h*0.12:.1f}" fill="{color}"/>'
            f'<rect x="{w*0.08:.1f}" y="{h*0.12:.1f}" width="{w*0.84:.1f}" height="{h*0.76:.1f}" rx="{h*0.12:.1f}" fill="none" stroke="{led}" stroke-width="4"/>'
            f'<circle cx="{w*0.88:.1f}" cy="{h*0.2:.1f}" r="{h*0.05:.1f}" fill="{led}"/>')
    return svg_wrap(w, h, body, label)


KITS = {
    "electro-808": dict(
        name="Eletrônico 808 (sintetizado)", machine="808", seed=808,
        colors=dict(shell="#1d1f26", rim="#ff5a36", head="#2a2d36", metal="#8c93a6", led="#ff5a36"),
        desc="Kit eletrônico no estilo de caixas de ritmo analógicas dos anos 80: bumbo longo e grave, caixa curta, hi-hats metálicos.",
        toms=dict(tom1=220, tom2=165, tom3=125, floor=92)),
    "electro-909": dict(
        name="Eletrônico 909 (sintetizado)", machine="909", seed=909,
        colors=dict(shell="#16181d", rim="#33d6ff", head="#262a33", metal="#a7b2c8", led="#33d6ff"),
        desc="Kit eletrônico no estilo de baterias eletrônicas de pista de dança: bumbo seco e socado, caixa brilhante, palmas largas.",
        toms=dict(tom1=240, tom2=185, tom3=140, floor=105)),
}
POS = dict(kick=(0.50, 0.70), snare=(0.30, 0.63), tom1=(0.37, 0.38), tom2=(0.50, 0.31), tom3=(0.64, 0.38), floor=(0.78, 0.66),
           hihat=(0.12, 0.47), crash1=(0.22, 0.20), crash2=(0.70, 0.14), ride=(0.88, 0.30), splash=(0.06, 0.20),
           cowbell=(0.60, 0.18), tambourine=(0.17, 0.72))


def build(out_root, kid, cfg):
    m = Machine(cfg["machine"])
    rng_master = np.random.default_rng(cfg["seed"])
    c = cfg["colors"]
    kit_dir = out_root / kid
    pieces = []

    def files_for(pid, role, li, maker, peak, n_rr):
        fl = []
        for rr in range(n_rr):
            rng = np.random.default_rng(rng_master.integers(1 << 31))
            rel = f"samples/{pid}_{role}_L{li}_rr{rr + 1}.wav"
            write_wav(kit_dir / rel, finish(maker(rng), peak))
            fl.append(rel)
        return fl

    def layered(pid, role, maker, layers=LAYERS5, n_rr=3):
        out = []
        for i, (lo, hi, peak) in enumerate(layers):
            h = (i + 1) / len(layers)
            out.append(dict(velMin=lo, velMax=hi, files=files_for(pid, role, i + 1, lambda rng, h=h: maker(rng, h), peak, n_rr)))
        return {"layers": out}

    def single(pid, role, maker, peak=0.9, n_rr=3):
        return {"layers": [dict(velMin=1, velMax=127, files=files_for(pid, role, 1, maker, peak, n_rr))]}

    def img(pid, svg):
        (kit_dir / "images").mkdir(parents=True, exist_ok=True)
        (kit_dir / f"images/{pid}.svg").write_text(svg, encoding="utf-8")
        return f"images/{pid}.svg"

    def piece(pid, typ, label, dia, svg, arts, gain=0.0, pan=0.0):
        d = dict(id=pid, type=typ, label=label, diameter=dia, image=img(pid, svg), gainDb=gain, articulations=arts)
        if pan:
            d["pan"] = pan
        pieces.append(d)

    piece("kick", "kick", "Bumbo eletrônico", 22, svg_kick(22, c["shell"], c["rim"], c["head"], "Bumbo"), {"hit": layered("kick", "hit", m.kick)}, 0.0)
    piece("snare", "snare", "Caixa eletrônica", 14, svg_drum(14, 5.0, c["shell"], c["rim"], c["head"], 8, "Caixa"),
          {"hit": layered("snare", "hit", m.snare), "rimshot": single("snare", "rimshot", lambda r: m.rimshot(r), 0.9, 3),
           "sidestick": single("snare", "sidestick", lambda r: m.clave(r), 0.8, 3)}, -1.0)
    for tid, f in cfg["toms"].items():
        dia = {"tom1": 8, "tom2": 10, "tom3": 12, "floor": 14}[tid]
        piece(tid, "tom", f"Tom eletrônico {dia}\"", dia, svg_drum(dia, dia * 0.7, c["shell"], c["rim"], c["head"], 6, f"Tom {dia}"),
              {"hit": layered(tid, "hit", lambda r, h, f=f: m.tom(r, f, h))}, -1.0, {"tom1": -0.3, "tom2": -0.1, "tom3": 0.15, "floor": 0.35}[tid])
    piece("hihat", "hihat", "Chimbal eletrônico", 13, svg_hat(13, c["metal"], "Chimbal"),
          {"closed": layered("hihat", "closed", lambda r, h: m.hat(r, "closed", h)),
           "pedal": single("hihat", "pedal", lambda r: m.hat(r, "pedal", 0.4), 0.5, 3),
           "open": layered("hihat", "open", lambda r, h: m.hat(r, "open", h), LAYERS5[1:], 2)}, -4.0, -0.35)
    for cid, kind, dia, pan in (("crash1", "crash", 16, -0.5), ("crash2", "crash", 18, 0.5), ("splash", "splash", 10, -0.6)):
        piece(cid, "splash" if kind == "splash" else "crash", f"Prato eletrônico {dia}\"", dia,
              svg_cymbal(dia, c["metal"], "Prato", 0.1), {"hit": layered(cid, "hit", lambda r, h, k=kind: m.cymbal(r, k, h), CYM_LAYERS, 2)}, -5.0, pan)
    piece("ride", "ride", "Condução eletrônica 20\"", 20, svg_cymbal(20, c["metal"], "Condução", 0.07),
          {"hit": layered("ride", "hit", lambda r, h: m.cymbal(r, "ride", h), CYM_LAYERS, 2),
           "bell": single("ride", "bell", lambda r: m.cymbal(r, "bell", 0.8), 0.8, 2)}, -6.0, 0.6)
    piece("cowbell", "cowbell", "Cowbell eletrônico", 8, pad_svg(80, 60, c["shell"], "Cowbell", c["led"]),
          {"hit": layered("cowbell", "hit", lambda r, h: m.cowbell(r, h), CYM_LAYERS, 2)}, -3.0)
    piece("tambourine", "tambourine", "Palmas (clap)", 10, pad_svg(100, 70, c["shell"], "Clap", c["led"]),
          {"hit": layered("tambourine", "hit", lambda r, h: m.clap(r, h), LAYERS5, 3)}, -2.0, -0.2)

    layout = [dict(slot=p["id"], piece=p["id"], x=POS[p["id"]][0], y=POS[p["id"]][1]) for p in pieces]
    doc = dict(format=1, id=kid, name=cfg["name"], author="OpenDrums (gerado por tools/make_electronic_kits.py)", license="CC0-1.0",
               description=cfg["desc"] + " Sons sintetizados do zero (não são gravações de equipamentos Roland).", pieces=pieces, layout=layout)
    (kit_dir / "kit.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  {kid}: {len(pieces)} peças, {len(list((kit_dir / 'samples').glob('*.wav')))} samples")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for kid, cfg in KITS.items():
        build(out, kid, cfg)


if __name__ == "__main__":
    main()
