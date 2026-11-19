#!/usr/bin/env python3
"""Converts a multi-microphone sample folder into an OpenDrums kit (kit.json + WAV + SVG).

Modern open kits ship several microphones per hit, as FLAC, with hundreds of files per drum. Loading all of
that into RAM is not practical for a plugin, so this tool makes a "lite" version: it mixes the microphones
into one stereo WAV per hit, keeps a few velocity layers and round robins, and trims the tail.

Usage:   convert_kit.py <recipe.json> <source_dir> <out_dir> [--layers 6] [--rr 3] [--bits 16|24] [--jobs 8]
Layout expected under <source_dir>/<samplesDir>:   <dir>/<mic>/<name>_vl<N>_rr<M>.<flac|wav>
(files without "vl" are one velocity layer; files without "rr" are one take). See recipes/ for the format.
Requires: ffmpeg on PATH, numpy.
"""
import argparse
import json
import re
import subprocess
import sys
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import make_demo_kits as art  # SVG pieces (same cartoon drawings as the demo kits)

RATE = 44100


def decode(path: Path, seconds: float) -> np.ndarray:
    """Returns float32 stereo (n, 2) at RATE, at most `seconds` long."""
    cmd = ["ffmpeg", "-v", "error", "-i", str(path), "-t", f"{seconds:.3f}", "-f", "f32le", "-ac", "2", "-ar", str(RATE), "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2)


def write_wav(path: Path, x: np.ndarray, bits: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    x = np.clip(x, -1.0, 1.0)
    rng = np.random.default_rng(1)
    if bits == 16:
        dither = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0  # TPDF
        data = np.round((x + dither) * 32767.0).astype("<i2").tobytes()
        width = 2
    else:
        v = np.round(x * 8388607.0).astype("<i4")
        data = np.stack([(v >> 0) & 255, (v >> 8) & 255, (v >> 16) & 255], axis=-1).astype(np.uint8).tobytes()
        width = 3
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(width)
        w.setframerate(RATE)
        w.writeframes(data)


def scan_hits(root: Path, prefix: str = None):
    """{(vl, rr): {mic: path}} for one articulation.
    Layouts understood: <root>/<mic>/<file> (Big Rusty), <root>/<mic>/rrN/<file> (Unruly) and <root>/<file> (single mic, e.g. VCSL).
    `prefix` keeps only files named <prefix>[_vN|_vlN][_rrN][_Suffix].<ext> (when one folder holds several articulations)."""
    hits = {}
    pat = re.compile(r"^" + re.escape(prefix) + r"(_vl?\d+)?(_rr\d+)?(_[A-Za-z]+)?\.(flac|wav)$", re.I) if prefix else None

    def add(mic, f):
        if not f.is_file() or f.suffix.lower() not in (".flac", ".wav") or (pat and not pat.match(f.name)):
            return
        vl = re.search(r"_vl?(\d+)(?=_|\.)", f.name)
        rr = re.search(r"_rr(\d+)", f.name)
        key = (int(vl.group(1)) if vl else 1, int(rr.group(1)) if rr else 1)
        hits.setdefault(key, {})[mic] = f

    for f in root.iterdir():
        add("main", f)
    for mic_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        for f in mic_dir.rglob("*"):
            add(mic_dir.name, f)
    return hits


def percussion_svg(look, d, colors, label):
    """Pictures for hand percussion: bell, tambourine, shaker, triangle."""
    import make_demo_kits as art
    w = d * 10
    c, rim, metal = colors["shell"], colors["rim"], colors["metal"]
    if look == "bell":      # cowbell / agogo
        h = w * 0.8
        body = (f'<path d="M{w*0.18:.1f} {h*0.9:.1f} L{w*0.30:.1f} {h*0.12:.1f} L{w*0.70:.1f} {h*0.12:.1f} L{w*0.82:.1f} {h*0.9:.1f} Z" fill="{metal}"/>'
                f'<path d="M{w*0.30:.1f} {h*0.12:.1f} L{w*0.70:.1f} {h*0.12:.1f} L{w*0.74:.1f} {h*0.3:.1f} L{w*0.26:.1f} {h*0.3:.1f} Z" fill="#ffffff" opacity="0.25"/>'
                f'<ellipse cx="{w/2:.1f}" cy="{h*0.9:.1f}" rx="{w*0.32:.1f}" ry="{h*0.06:.1f}" fill="#3a2a10"/>')
        return art.svg_wrap(w, h, body, label)
    if look == "tambourine":
        body = f'<circle cx="{w/2:.1f}" cy="{w/2:.1f}" r="{w/2-4:.1f}" fill="{c}"/><circle cx="{w/2:.1f}" cy="{w/2:.1f}" r="{w/2-12:.1f}" fill="{colors["head"]}"/>'
        for i in range(6):
            import math
            a = i * math.pi / 3
            body += f'<circle cx="{w/2+math.cos(a)*(w/2-8):.1f}" cy="{w/2+math.sin(a)*(w/2-8):.1f}" r="{w*0.045:.1f}" fill="{metal}" stroke="#444" stroke-width="1.5"/>'
        return art.svg_wrap(w, w, body, label)
    if look == "shaker":
        h = w * 0.55
        body = (f'<rect x="{w*0.06:.1f}" y="{h*0.15:.1f}" width="{w*0.88:.1f}" height="{h*0.7:.1f}" rx="{h*0.35:.1f}" fill="{c}"/>'
                f'<rect x="{w*0.06:.1f}" y="{h*0.15:.1f}" width="{w*0.88:.1f}" height="{h*0.7:.1f}" rx="{h*0.35:.1f}" fill="none" stroke="{rim}" stroke-width="4"/>'
                + "".join(f'<circle cx="{w*(0.2+0.12*i):.1f}" cy="{h*(0.4+0.2*(i%2)):.1f}" r="{h*0.07:.1f}" fill="{metal}"/>' for i in range(6)))
        return art.svg_wrap(w, h, body, label)
    if look == "triangle":
        h = w * 0.85
        body = (f'<path d="M{w/2:.1f} {h*0.08:.1f} L{w*0.92:.1f} {h*0.88:.1f} L{w*0.08:.1f} {h*0.88:.1f} Z" fill="none" stroke="{metal}" stroke-width="{w*0.07:.1f}" stroke-linejoin="round"/>'
                f'<path d="M{w/2:.1f} {h*0.08:.1f}" stroke="#555"/>')
        return art.svg_wrap(w, h, body, label)
    return art.svg_cymbal(d, metal, label)


def pick_layers(vls, max_layers):
    """Chooses up to max_layers velocity layers, evenly spread, and returns [(vl, vel_min, vel_max)]."""
    vls = sorted(vls)
    n = len(vls)
    if n > max_layers:
        idx = sorted({round(i * (n - 1) / (max_layers - 1)) for i in range(max_layers)}) if max_layers > 1 else [n // 2]
        chosen = [vls[i] for i in idx]
    else:
        chosen = vls
    # centre of each original layer on the 1..127 axis; soft layers are narrower than hard ones
    centre = lambda v: 127.0 * ((vls.index(v) + 0.5) / n) ** 1.15
    cs = [centre(v) for v in chosen]
    edges = [0.0] + [(a + b) / 2 for a, b in zip(cs, cs[1:])] + [127.0]
    out = []
    for i, v in enumerate(chosen):
        lo = 1 if i == 0 else int(round(edges[i])) + 1
        hi = 127 if i == len(chosen) - 1 else int(round(edges[i + 1]))
        out.append((v, lo, hi))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recipe")
    ap.add_argument("source")
    ap.add_argument("out")
    ap.add_argument("--layers", type=int, default=6)
    ap.add_argument("--rr", type=int, default=3)
    ap.add_argument("--bits", type=int, default=16, choices=(16, 24))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--extra-source", dest="extra", action="append", default=[], metavar="NAME=DIR",
                    help="extra sample source (a piece spec picks it with \"src\": \"NAME\")")
    a = ap.parse_args()

    rec = json.loads(Path(a.recipe).read_text(encoding="utf-8"))
    src = Path(a.source) / rec.get("samplesDir", "Samples")
    sources = {"main": src}
    for kv in a.extra:
        name, _, d = kv.partition("=")
        sources[name] = Path(d)
    out = Path(a.out) / rec["id"]
    colors = rec["colors"]
    pieces, layout, total = [], [], 0

    with ThreadPoolExecutor(a.jobs) as pool:
        for pc in rec["pieces"]:
            mix = rec["mixes"][pc["mix"]]
            arts_out = {}
            jobs = []  # (art_name, layer_index, rr_index, [(path, weight)], seconds)
            layer_defs = {}
            for art_name, spec in pc["articulations"].items():
                spec = {"dir": spec} if isinstance(spec, str) else spec
                seconds = spec.get("maxSeconds", pc.get("maxSeconds", 2.0))
                if "hits" in spec:  # resolved by convert_drumgizmo.py: [{"vl":1,"rr":1,"mics":{"AmbL":"/abs/file"}}]
                    hits = {(h["vl"], h.get("rr", 1)): {m: Path(f) for m, f in h["mics"].items()} for h in spec["hits"]}
                else:
                    hits = scan_hits(sources[spec.get("src", "main")] / spec["dir"], spec.get("prefix"))
                if not hits:
                    raise SystemExit(f"{pc['slot']}/{art_name}: nenhum sample em {spec.get('dir')} (prefixo {spec.get('prefix')})")
                vls = {vl for vl, _ in hits}
                layers = pick_layers(vls, spec.get("maxLayers", a.layers))
                layer_defs[art_name] = []
                for li, (vl, lo, hi) in enumerate(layers):
                    rrs = sorted(rr for (v, rr) in hits if v == vl)[: a.rr]
                    entry = {"velMin": lo, "velMax": hi, "files": []}
                    layer_defs[art_name].append(entry)
                    for ri, rr in enumerate(rrs):
                        mics = [(p, mix[m]) for m, p in hits[(vl, rr)].items() if m in mix]
                        if not mics:
                            raise SystemExit(f"{pc['slot']}/{art_name}: no mic of {list(mix)} in {list(hits[(vl, rr)])}")
                        jobs.append((art_name, li, ri, mics, seconds))

            def work(job):
                _, _, _, mics, seconds = job
                mixed = None
                for path, weight in mics:
                    pan = 0.0
                    if isinstance(weight, dict):  # {"w": 0.5, "pan": -0.7}: mono mic placed in the stereo field
                        pan, weight = weight.get("pan", 0.0), weight["w"]
                    x = decode(path, seconds) * weight
                    if pan:
                        ang = (pan + 1.0) * np.pi / 4
                        mono = x.mean(axis=1)
                        x = np.stack([mono * np.cos(ang), mono * np.sin(ang)], axis=1) * np.sqrt(2.0)
                    if mixed is None:
                        mixed = x
                    else:
                        n = max(len(mixed), len(x))
                        mixed = np.pad(mixed, ((0, n - len(mixed)), (0, 0)))
                        mixed += np.pad(x, ((0, n - len(x)), (0, 0)))
                # fade the tail out so the trim does not click
                fade = min(len(mixed), int(0.08 * RATE)) if len(mixed) >= int(seconds * RATE) - 2 else min(len(mixed), int(0.005 * RATE))
                mixed[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
                return mixed

            results = list(pool.map(work, jobs))
            peak = max(float(np.abs(r).max()) for r in results) or 1.0
            scale = 0.89 / peak  # same factor for the whole piece: keeps velocity layers and articulations in proportion
            for job, audio in zip(jobs, results):
                art_name, li, ri, _, _ = job
                rel = f"samples/{pc['slot']}_{art_name}_L{li + 1}_rr{ri + 1}.wav"
                write_wav(out / rel, audio * scale, a.bits)
                layer_defs[art_name][li]["files"].append(rel)
                total += 1
            for art_name, layers in layer_defs.items():
                arts_out[art_name] = {"layers": layers}

            t, d, lab = pc["type"], pc["diameter"], pc["label"]
            look = pc.get("look")
            if look:
                svg = percussion_svg(look, d, colors, lab)
            elif t == "kick":
                svg = art.svg_kick(d, colors["shell"], colors["rim"], colors["head"], lab)
            elif t in ("snare", "tom"):
                svg = art.svg_drum(d, 5.5 if t == "snare" else d * 0.85, colors["shell"], colors["rim"], colors["head"], 8 if t == "snare" else 6, lab, t == "snare")
            elif t == "hihat":
                svg = art.svg_hat(d, colors["metal"], lab)
            else:
                svg = art.svg_cymbal(d, colors["metal"], lab, 0.1 if t != "ride" else 0.07)
            (out / "images").mkdir(parents=True, exist_ok=True)
            (out / "images" / f"{pc['slot']}.svg").write_text(svg, encoding="utf-8")

            piece = {"id": pc["slot"], "type": t, "label": lab, "image": f"images/{pc['slot']}.svg", "diameter": d,
                     "gainDb": pc.get("gainDb", 0.0), "articulations": arts_out}
            if "pan" in pc:
                piece["pan"] = pc["pan"]
            pieces.append(piece)
            layout.append({"slot": pc["slot"], "piece": pc["slot"], "x": pc["pos"][0], "y": pc["pos"][1]})
            print(f"  {pc['slot']}: {sum(len(l['files']) for ls in layer_defs.values() for l in ls)} samples", flush=True)

    doc = {"format": 1, "id": rec["id"], "name": rec["name"], "author": rec["author"], "license": rec["license"],
           "description": rec.get("description", "") + (f" Fonte: {rec['source']}" if rec.get("source") else ""),
           "pieces": pieces, "layout": layout}
    (out / "kit.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    mb = sum(f.stat().st_size for f in out.rglob("*") if f.is_file()) / 1e6
    print(f"{rec['name']}: {len(pieces)} peças, {total} samples, {mb:.0f} MB em {out}")


if __name__ == "__main__":
    main()
