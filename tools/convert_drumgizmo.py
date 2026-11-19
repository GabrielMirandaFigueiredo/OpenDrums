#!/usr/bin/env python3
"""DrumGizmo kit -> OpenDrums kit.json.

  convert_drumgizmo.py <kit_dir> <out_dir> --id my-kit --name "My Kit" --license CC-BY-4.0 --author "..." [--layers 6] [--bits 16]
        [--map "Tom4=floor,KdrumR=-"] [--dump-recipe recipe.json] [--no-convert]

Understands two layouts:
  1. Native DrumGizmo: <kit_dir>/drumkit.xml (+ the instrument XMLs it points to). Every <sample power="..."> becomes one
     velocity layer (ordered by power); every <audiofile channel="..." file="..."> is one microphone of that sample.
  2. Folder layout without XML (what the SFZ ports ship): <kit_dir>/Samples/<Instrument>/<N>-<Instrument>-<Channel>.<wav|flac>,
     where N=1 is the softest sample.

Instruments are mapped to OpenDrums slots by name (Kdrum/Kick, Snare, Tom1..4, Hihat*, Crash*, Ride*, China, Splash...).
Use --map "Instrument=slot" (slot = kick, snare, tom1.., hihat:closed, hihat:open, ride:bell ... or '-' to ignore) to override.
The microphones are mixed with weights by channel name (own close mic 1.0, overheads 0.9, room 0.4, other instruments' mics 0).
The result is a resolved recipe that tools/convert_kit.py renders; --dump-recipe keeps it so it can be edited by hand.
"""
import argparse, json, re, subprocess, sys, xml.etree.ElementTree as ET
from pathlib import Path

AUDIO = (".wav", ".flac")

# instrument-name regex -> (slot, articulation, type, diameter, label, pos)
RULES = [
    (r"^(kdrum|kick|bd|bass)", ("kick", "hit", "kick", 22, "Bumbo", (0.5, 0.7))),
    (r"^snare.*(side|stick)", ("snare", "sidestick", "snare", 14, "Caixa", (0.3, 0.63))),
    (r"^snare.*rim", ("snare", "rimshot", "snare", 14, "Caixa", (0.3, 0.63))),
    (r"^snare", ("snare", "hit", "snare", 14, "Caixa", (0.3, 0.63))),
    (r"^(tom|rack)\s*1$", ("tom1", "hit", "tom", 12, "Tom 1", (0.36, 0.4))),
    (r"^(tom|rack)\s*2$", ("tom2", "hit", "tom", 13, "Tom 2", (0.51, 0.31))),
    (r"^(tom|rack)\s*3$", ("tom3", "hit", "tom", 16, "Tom 3", (0.66, 0.41))),
    (r"^(tom\s*4|floor)", ("floor", "hit", "tom", 16, "Surdo", (0.82, 0.68))),
    (r"^(hihat|hh|hat).*(open|loose)", ("hihat", "open", "hihat", 14, "Chimbal", (0.12, 0.47))),
    (r"^(hihat|hh|hat).*(pedal|foot|chick)", ("hihat", "pedal", "hihat", 14, "Chimbal", (0.12, 0.47))),
    (r"^(hihat|hh|hat)", ("hihat", "closed", "hihat", 14, "Chimbal", (0.12, 0.47))),
    (r"^crash.*l$|^crash1|^crash$", ("crash1", "hit", "crash", 17, "Ataque 1", (0.21, 0.2))),
    (r"^crash", ("crash2", "hit", "crash", 17, "Ataque 2", (0.72, 0.12))),
    (r"^ride.*bell", ("ride", "bell", "ride", 22, "Condução", (0.88, 0.27))),
    (r"^ride", ("ride", "hit", "ride", 22, "Condução", (0.88, 0.27))),
    (r"^china", ("china", "hit", "china", 18, "China", (0.4, 0.07))),
    (r"^splash", ("splash", "hit", "splash", 10, "Splash", (0.06, 0.2))),
    (r"^(cowbell|cow)", ("cowbell", "hit", "cowbell", 8, "Cowbell", (0.62, 0.2))),
    (r"^(tamb)", ("tambourine", "hit", "tambourine", 10, "Pandeiro", (0.6, 0.15))),
]
SLOT_INFO = {r[1][0]: r[1] for r in RULES}


# variants that only duplicate a main articulation (whiskers/brushes, shank hits, chained cymbals, rests...): skipped unless mapped by hand
SKIP = re.compile(r"whisker|circle|chain|without_contact|semi|rest|ghost|shank", re.I)


def classify(name, override):
    if name in override:
        o = override[name]
        if o == "-":
            return None
        slot, _, art = o.partition(":")
        info = SLOT_INFO.get(slot) or ("tom", "hit", "tom", 14, slot, (0.5, 0.4))
        return (slot, art or info[1]) + tuple(info[2:])
    if SKIP.search(name):
        return None
    for rx, info in RULES:
        if re.search(rx, name.lower()):
            return info
    return None


def mic_weight(channel, instrument):
    c, i = channel.lower(), instrument.lower()
    if re.search(r"trig|click|room.*mono", c):
        return 0.0, 0.0
    key = re.sub(r"[lr]?(bell)?$", "", i.split("_")[0])  # RideLBell -> ride, KdrumL -> kdrum (digits stay: Tom1 only matches Tom1)
    key = {"kdrum": "kdrum", "kick": "kick"}.get(key, key)
    if "oh" in c or "overhead" in c:
        w = 0.9
    elif "amb" in c or "room" in c:
        w = 0.4
    elif key and (key in c.replace("_", "") or i.startswith(c.replace("_", ""))):
        w = 1.0
        if "bottom" in c or "btm" in c or "bot" in c:
            w = 0.4
        elif "snare" in i and "snare" in c and not ("top" in c):
            w = 0.9
    else:
        w = 0.0  # bleed of other instruments: not decoded at all (faster, cleaner)
    pan = 0.0
    if re.search(r"(^|[^a-z])l$|left|amb.?l|oh.?l", c) and "bell" not in c:
        pan = -0.7
    elif re.search(r"(^|[^a-z])r$|right|amb.?r|oh.?r", c):
        pan = 0.7
    return w, pan


def read_native(kit_dir):
    """{instrument: [(power, {channel: path})]} from drumkit.xml."""
    root = ET.parse(kit_dir / "drumkit.xml").getroot()
    out = {}
    for inst in root.iter("instrument"):
        name, f = inst.get("name"), inst.get("file")
        if not name or not f:
            continue
        ipath = kit_dir / f
        if not ipath.is_file():
            print(f"  aviso: {f} não existe, instrumento {name} ignorado", file=sys.stderr)
            continue
        samples = []
        for sm in ET.parse(ipath).getroot().iter("sample"):
            mics = {}
            for af in sm.iter("audiofile"):
                p = (ipath.parent / af.get("file")).resolve()
                if p.suffix.lower() in AUDIO and p.is_file():
                    mics[af.get("channel")] = p
            if mics:
                samples.append((float(sm.get("power", len(samples) + 1)), mics))
        out[name] = sorted(samples, key=lambda s: s[0])
    return out


def read_folders(kit_dir):
    base = kit_dir / "Samples" if (kit_dir / "Samples").is_dir() else kit_dir
    out = {}
    for d in sorted(p for p in base.iterdir() if p.is_dir()):
        by_n = {}
        for f in d.iterdir():
            m = re.match(r"^(\d+)-" + re.escape(d.name) + r"-(.+)\.\w+$", f.name)
            if m and f.suffix.lower() in AUDIO:
                by_n.setdefault(int(m.group(1)), {})[m.group(2)] = f.resolve()
        if by_n:
            out[d.name] = [(float(n), mics) for n, mics in sorted(by_n.items())]
    return out


def build_recipe(instruments, args):
    override = dict(kv.split("=", 1) for kv in args.map.split(",") if "=" in kv) if args.map else {}
    pieces = {}
    mixes = {}
    for iname, samples in instruments.items():
        info = classify(iname, override)
        if info is None:
            print(f"  ignorado: {iname}")
            continue
        slot, art, typ, dia, label, pos = info
        # one mix per instrument: weights by channel name
        chans = sorted({c for _, m in samples for c in m})
        mix = {}
        for c in chans:
            w, pan = mic_weight(c, iname)
            if w > 0:
                mix[c] = {"w": w, "pan": pan} if pan else w
        mixes[iname] = mix
        spec = {"hits": [{"vl": i + 1, "rr": 1, "mics": {c: str(p) for c, p in m.items() if c in mix}}
                         for i, (_, m) in enumerate(samples)]}
        if art in ("pedal",):
            spec["maxSeconds"] = 0.6
        if art == "open":
            spec["maxSeconds"] = 3.0
        if slot in pieces:
            if art in pieces[slot]["articulations"]:
                print(f"  aviso: {iname} repete {slot}:{art}, ignorado")
                continue
            pieces[slot]["articulations"][art] = spec
            pieces[slot]["mixes"][art] = mix
        else:
            secs = {"kick": 1.6, "snare": 1.4, "tom": 2.5, "hihat": 1.0, "crash": 4.5, "ride": 3.5, "china": 3.5, "splash": 1.5}.get(typ, 1.5)
            pieces[slot] = {"slot": slot, "type": typ, "diameter": dia, "label": label,
                            "gainDb": {"hihat": -4.0, "crash": -5.0, "ride": -6.0, "china": -5.0, "splash": -6.0}.get(typ, -1.0 if typ != "kick" else 0.0),
                            "maxSeconds": secs, "pos": list(pos), "mix": "_" + slot,
                            "articulations": {art: spec}, "mixes": {art: mix}}
    # the engine takes one mix per piece: use the union (max weight) so every articulation finds its mics
    rec_mixes, plist = {}, []
    for slot, pc in pieces.items():
        union = {}
        for m in pc.pop("mixes").values():
            for c, w in m.items():
                ww = w["w"] if isinstance(w, dict) else w
                if c not in union or ww > (union[c]["w"] if isinstance(union[c], dict) else union[c]):
                    union[c] = w
        rec_mixes["_" + slot] = union
        plist.append(pc)
    order = ["kick", "snare", "tom1", "tom2", "tom3", "floor", "hihat", "crash1", "crash2", "ride", "splash", "china", "cowbell", "tambourine"]
    plist.sort(key=lambda p: order.index(p["slot"]) if p["slot"] in order else 99)
    pan = {"hihat": -0.35, "crash1": -0.5, "crash2": 0.45, "ride": 0.6, "tom1": -0.3, "tom2": -0.1, "tom3": 0.15, "floor": 0.35, "splash": -0.6, "china": -0.2}
    for p in plist:
        if p["slot"] in pan:
            p["pan"] = pan[p["slot"]]
    return {"id": args.id, "name": args.name, "author": args.author, "license": args.license,
            "description": args.description or "Convertido de um kit DrumGizmo (versão reduzida: microfones misturados em estéreo).",
            "source": args.source or "", "samplesDir": ".",
            "colors": {"shell": "#5a5a66", "rim": "#c4c4c4", "head": "#e8e2d2", "metal": "#bf9d4e"},
            "mixes": rec_mixes, "pieces": plist}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kit_dir"); ap.add_argument("out")
    ap.add_argument("--id", required=True); ap.add_argument("--name", required=True)
    ap.add_argument("--license", required=True); ap.add_argument("--author", default="")
    ap.add_argument("--source", default=""); ap.add_argument("--description", default="")
    ap.add_argument("--map", default=""); ap.add_argument("--dump-recipe")
    ap.add_argument("--no-convert", action="store_true")
    ap.add_argument("--layers", type=int, default=6); ap.add_argument("--rr", type=int, default=3)
    ap.add_argument("--bits", type=int, default=16); ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    kit = Path(a.kit_dir)
    if (kit / "drumkit.xml").is_file():
        print("layout: DrumGizmo nativo (drumkit.xml)")
        inst = read_native(kit)
    else:
        print("layout: pastas Samples/<Instrumento>/<N>-<Instrumento>-<Canal>")
        inst = read_folders(kit)
    if not inst:
        sys.exit("nenhum instrumento encontrado")
    rec = build_recipe(inst, a)
    for p in rec["pieces"]:
        print(f"  {p['slot']:9s} <- " + ", ".join(f"{k} ({len(v['hits'])} samples)" for k, v in p["articulations"].items()))
    rp = Path(a.dump_recipe or (Path(a.out) / f"{a.id}.recipe.json"))
    rp.parent.mkdir(parents=True, exist_ok=True)
    rp.write_text(json.dumps(rec, indent=1, ensure_ascii=False), encoding="utf-8")
    print("receita:", rp)
    if a.no_convert:
        return
    cmd = [sys.executable, str(Path(__file__).with_name("convert_kit.py")), str(rp), str(kit), a.out,
           "--layers", str(a.layers), "--rr", str(a.rr), "--bits", str(a.bits), "--jobs", str(a.jobs)]
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
