#!/usr/bin/env python3
"""Generates the OpenDrums starter groove library: original MIDI patterns (CC0).

Layout:  <out>/<Style>/<num>-<den> <bpm>bpm/<NN Section>/<Name NN>.mid
Everything here is written from scratch as step patterns; no third-party grooves are used.
Usage: make_grooves.py <out_dir>
"""
import os
import random
import struct
import sys

PPQ = 480
K, SN, SS, CL = 36, 38, 37, 39           # kick, snare, side stick, clap
HC, HP, HO = 42, 44, 46                  # hats closed / pedal / open
T1, T2, T3, T4 = 50, 48, 45, 43          # high tom ... floor tom
CR, RD, RB = 49, 51, 53                  # crash, ride, ride bell
CB, TB, SH, CW = 56, 54, 70, 75          # cowbell, tambourine, shaker, claves
CGH, CGL, TMH, TML = 62, 64, 65, 66      # congas / timbales
SECTIONS = ["Intro", "Verso", "Pré-refrão", "Refrão", "Ponte", "Viradas", "Especiais", "Finais"]


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def write_mid(path, events, num, den, bpm, bars):
    """events: list of (tick, note, vel). Writes a format-0 file, 1 bar = bars*ticksPerBar long."""
    bar = PPQ * 4 * num // den
    evs = []
    for t, n, v in events:
        evs.append((t, 1, n, max(1, min(127, int(v)))))
        evs.append((t + 60, 0, n, 0))
    evs.sort(key=lambda e: (e[0], e[1]))
    trk = bytearray()
    trk += vlq(0) + b"\xFF\x51\x03" + struct.pack(">I", int(60000000 / bpm))[1:]
    trk += vlq(0) + b"\xFF\x58\x04" + bytes([num, {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[den], 24, 8])
    last = 0
    for t, on, n, v in evs:
        trk += vlq(t - last) + bytes([0x99, n, v if on else 0])
        last = t
    trk += vlq(max(0, bar * bars - last)) + b"\xFF\x2F\x00"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(trk)) + bytes(trk))


class Meter:
    def __init__(self, num, den, step):
        self.num, self.den, self.step = num, den, step     # step in ticks
        self.bar = PPQ * 4 * num // den
        self.n = self.bar // step                          # steps per bar


def lane(pattern, m, note, rng, humanize=True, base=96):
    """pattern: chars per step. 'x' normal, 'X' accent, 'o' ghost, '.' rest."""
    ev = []
    vel = {"X": base + 20, "x": base, "o": 38, "g": 52}
    for i, c in enumerate(pattern[: m.n]):
        if c in vel:
            v = vel[c] + (rng.randint(-6, 6) if humanize else 0)
            ev.append((i * m.step, note, v))
    return ev


def bar_of(lanes, m, rng, offset=0):
    ev = []
    for note, pat in lanes.items():
        for t, n, v in lane(pat, m, note, rng):
            ev.append((t + offset, n, v))
    return ev


def fill_bar(m, rng, kind, offset):
    """One bar whose last beat(s) are a tom/snare run. Earlier part: simple groove."""
    ev = []
    n = m.n
    body = n * 2 // 4 if n >= 8 else n // 2
    ev += [(offset + 0, K, 105), (offset + 0, HC, 90)]
    ev += [(offset + (body // 2) * m.step, SN, 100), (offset + (body // 2) * m.step, HC, 80)]
    seq = {
        "tom_down": [SN, T1, T1, T2, T2, T3, T4, T4],
        "tom_up": [T4, T3, T2, T1, SN, T1, T2, T3],
        "snare_roll": [SN] * 8,
        "mix": [SN, SN, T1, T2, SN, T3, T4, K],
        "kick_snare": [K, SN, K, SN, T2, T3, T4, T4],
    }[kind]
    start = body
    count = n - start
    for i in range(count):
        note = seq[int(i * len(seq) / count) % len(seq)]
        v = 80 + int(40 * i / max(1, count - 1)) + rng.randint(-5, 5)
        ev.append((offset + (start + i) * m.step, note, min(127, v)))
    return ev


def make_clip(lanes_by_section, style, m, section, variant, rng):
    """Returns (events, bars). lanes_by_section: dict from the style definition."""
    lanes = lanes_by_section
    ev, bars = [], 4
    if section == "Viradas":
        kinds = ["tom_down", "tom_up", "snare_roll", "mix", "kick_snare"]
        kind = kinds[variant % len(kinds)]
        ev += bar_of(lanes["verse"], m, rng, 0)
        ev += bar_of(lanes["verse"], m, rng, m.bar)
        ev += bar_of(lanes["verse"], m, rng, 2 * m.bar)
        ev += fill_bar(m, rng, kind, 3 * m.bar)
        ev.append((4 * m.bar - 0, CR, 0))  # placeholder removed below
        ev = [e for e in ev if e[2] > 0]
        bars = 4
        # crash on the next downbeat is not part of the clip: leave the fill to resolve into the loop start
        ev.append((0, CR, 110))
    elif section == "Intro":
        bars = 2
        ev += bar_of(lanes["intro"], m, rng, 0)
        ev += fill_bar(m, rng, ["tom_down", "mix"][variant % 2], m.bar)
    elif section == "Verso":
        for b in range(4):
            src = lanes["verse"] if b % 2 == 0 else lanes.get("verse_b", lanes["verse"])
            ev += bar_of(src, m, rng, b * m.bar)
        if variant % 2 == 1:
            ev += [(3 * m.bar + (m.n - 2) * m.step, SN, 70), (3 * m.bar + (m.n - 1) * m.step, SN, 85)]
    elif section == "Pré-refrão":
        for b in range(4):
            ev += bar_of(lanes["pre"], m, rng, b * m.bar)
        ev += [(3 * m.bar + i * m.step, SN, 60 + (i - (m.n - 4)) * 14) for i in range(m.n - 4, m.n)]
    elif section == "Refrão":
        for b in range(4):
            ev += bar_of(lanes["chorus"], m, rng, b * m.bar)
        ev.append((0, CR, 118))
        ev.append((2 * m.bar, CR, 108))
        if variant % 2 == 1:
            ev = [e for e in ev if not (e[1] == HC)] + [(i * m.step, RD, 92) for i in range(0, 4 * m.n, max(1, m.n // (m.num if m.den == 4 else m.num // 2 or 1)))]
    elif section == "Ponte":
        for b in range(4):
            ev += bar_of(lanes["bridge"], m, rng, b * m.bar)
        ev.append((3 * m.bar + (m.n // 2) * m.step, T3, 90))
    elif section == "Especiais":
        bars = 2
        kind = variant % 3
        if kind == 0:   # stop on 1 + hits
            ev += [(0, K, 115), (0, CR, 115)]
            ev += [(m.bar + (m.n // 2) * m.step, SN, 100), (m.bar + (m.n // 2) * m.step, K, 100)]
        elif kind == 1:  # rim click count-in
            for i in range(m.num):
                ev.append((i * PPQ * 4 // m.den, SS, 90 + (10 if i == 0 else 0)))
            ev += bar_of(lanes["intro"], m, rng, m.bar)
        else:            # half-bar syncopated break
            ev += [(0, K, 110), (0, CR, 112)]
            for i in (3, 6, 8, 11):
                if i < m.n:
                    ev.append((i * m.step, SN if i % 2 else K, 96))
            ev += bar_of(lanes["verse"], m, rng, m.bar)
    elif section == "Finais":
        bars = 2
        ev += bar_of(lanes["verse"], m, rng, 0)
        ev += [(m.bar, K, 120), (m.bar, SN, 118), (m.bar, CR, 122)]
        if variant % 2 == 1:
            ev += [(m.bar + (m.n // 2) * m.step, K, 100)]
        else:
            ev += [(m.bar + (m.n // 4) * m.step, T1, 95), (m.bar + (m.n // 2) * m.step, T3, 100)]
    return ev, bars


def build_lanes(style_def, m):
    """Expands the pattern dictionary of a style into verse/pre/chorus/bridge/intro lane sets."""
    return style_def["lanes"]


def P(s):
    return s.replace(" ", "").replace("|", "")


# --- style definitions: 16-step (4/4 sixteenths) unless noted ------------------------------------------------
def rock_lanes():
    v = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.......x.x.....")}
    vb = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.....x.x.x.....")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.o.....X.o."), K: P("X...x...X...x...")}
    ch = {HO: P("X.x.X.x.X.x.X.x."), SN: P("....X.......X..."), K: P("X.....x.x.x.....")}
    br = {RD: P("x.x.x.x.x.x.x.x."), SS: P("....x.......x..."), K: P("X.......X.......")}
    intro = {HC: P("x...x...x...x..."), K: P("X.......X.......")}
    return dict(verse=v, verse_b=vb, pre=pre, chorus=ch, bridge=br, intro=intro)


def metal_lanes():
    v = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.xxX.x.X.xxX.x.")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("XxxxXxxxXxxxXxxx")}
    ch = {CR: P("X.......X......."), HO: P("..x...x...x...x."), SN: P("....X.......X..."), K: P("XxXxXxXxXxXxXxXx")}
    br = {RB: P("x...x...x...x..."), SN: P("........X......."), K: P("X.x.....X.x.x...")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def funk_lanes():
    v = {HC: P("xxxxxxxxxxxxxxxx"), SN: P("o..o.X.o.o.oX..o"), K: P("X..x..x..x.x..x.")}
    vb = {HC: P("xxxxxxxxxxxxxxxx"), SN: P("o..o.X.o.o.oX.oo"), K: P("X..x.x....x..x.x")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), HO: P("......x.......x."), SN: P("....X..o....X..o"), K: P("X..x..x...x..x..")}
    ch = {HO: P("X.x.X.x.X.x.X.x."), SN: P("....X..o....X.o."), K: P("X..x.xx..x.xx.x.")}
    br = {HC: P("x.x.x.x.x.x.x.x."), SS: P("....x.......x..."), K: P("X.....x.......x.")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.x.....X.......")}
    return dict(verse=v, verse_b=vb, pre=pre, chorus=ch, bridge=br, intro=intro)


def pop_lanes():
    v = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.....x...x.....")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X.xx"), K: P("X...x...X...x...")}
    ch = {HO: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X...x.x.X...x.x.")}
    br = {SS: P("....x.......x..."), K: P("X.......x.......")}
    intro = {HC: P("x...x...x...x..."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def worship_lanes():
    v = {RD: P("x...x...x...x..."), SS: P("....x.......x..."), K: P("X.......x.......")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.......X.x.....")}
    ch = {HO: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.....x.X.....x."), CR: P("X...............")}
    br = {RB: P("x...x...x...x..."), K: P("X...............")}
    intro = {SH: P("x.x.x.x.x.x.x.x."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def gospel_lanes():   # 16th-note feel, ghost-heavy
    v = {HC: P("x.x.x.x.x.x.x.x."), SN: P("..o.X..o.o.oX..o"), K: P("X..x....x.x.....")}
    pre = {HC: P("xxxxxxxxxxxxxxxx"), SN: P("o.o.X.o.o.o.X.o."), K: P("X..x..x.X..x..x.")}
    ch = {HO: P("X.x.X.x.X.x.X.x."), SN: P("..o.X..o.o.oX.oo"), K: P("X..x.xx..x.xx.x."), CR: P("X...............")}
    br = {RD: P("x.x.x.x.x.x.x.x."), SS: P("....x.......x..."), K: P("X.....x.........")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def latin_lanes():    # son-clave based pop-latin
    v = {CW: P("x..x..x...x.x..."), HC: P("x.x.x.x.x.x.x.x."), K: P("X..x..x.X..x..x."), CGH: P("..x...x...x...x."), CGL: P("....x.......x...")}
    pre = {CB: P("x...x...x...x..."), HC: P("x.x.x.x.x.x.x.x."), SS: P("..x...x...x...x."), K: P("X...x...X...x...")}
    ch = {RB: P("x.x.x.x.x.x.x.x."), TMH: P("..x...x...x...x."), SN: P("....X.......X..."), K: P("X..x..x.X..x..x."), CR: P("X...............")}
    br = {SH: P("xxxxxxxxxxxxxxxx"), CW: P("x..x..x...x.x..."), K: P("X.......X.......")}
    intro = {CW: P("x..x..x...x.x..."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def bossa_lanes():    # 2-bar-ish bossa feel inside one bar of 16ths
    v = {HC: P("x.x.x.x.x.x.x.x."), SS: P("x..x..x...x..x.."), K: P("X..x..X..x..X...")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SS: P("x..x..x...x.x..."), K: P("X..x..X..x..X.x.")}
    ch = {RD: P("x.x.x.x.x.x.x.x."), SS: P("x..x..x...x..x.."), K: P("X..x..X..x..X...")}
    br = {SH: P("xxxxxxxxxxxxxxxx"), SS: P("x..x..x...x..x.."), K: P("X.....X.....X...")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), K: P("X.....X.....X...")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def samba_lanes():
    v = {SH: P("xxxxxxxxxxxxxxxx"), SS: P("x..x..x..x..x.x."), K: P("X..xX..xX..xX..x"), TB: P("..x...x...x...x.")}
    pre = {SH: P("xxxxxxxxxxxxxxxx"), SS: P("x..x..x..x..x.x."), K: P("X..xX..xX..xX..x"), SN: P("o.o.X.o.o.o.X.o.")}
    ch = {SH: P("xxxxxxxxxxxxxxxx"), SN: P("o.oxX.o.o.oxX.o."), K: P("X..xX..xX..xX..x"), CR: P("X...............")}
    br = {SH: P("xxxxxxxxxxxxxxxx"), SS: P("x..x..x..x..x.x."), K: P("X...X...X...X...")}
    intro = {SH: P("xxxxxxxxxxxxxxxx"), K: P("X..xX..xX..xX..x")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def reggae_lanes():   # one-drop
    v = {HC: P("x.x.x.x.x.x.x.x."), SS: P("........x......."), K: P("........X.......")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SS: P("........x......."), K: P("........X.......")}
    ch = {HO: P("..x...x...x...x."), SN: P("........X......."), K: P("........X.......")}
    br = {SS: P("........x......."), K: P("........X.......")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), K: P("........X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def country_lanes():  # train beat
    v = {SN: P("x.xxx.xxx.xxx.xx"), K: P("X...X...X...X..."), HC: P("....x.......x...")}
    pre = {HC: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.......X.......")}
    ch = {HO: P("x.x.x.x.x.x.x.x."), SN: P("....X.......X..."), K: P("X.......X.x.....")}
    br = {RD: P("x.x.x.x.x.x.x.x."), SS: P("....x.......x..."), K: P("X...............")}
    intro = {SS: P("x...x...x...x..."), K: P("X.......X.......")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def hiphop_lanes():
    v = {HC: P("x.x.x.x.x.x.x.x."), SN: P("........X......."), K: P("X.....x...x.....")}
    vb = {HC: P("x.x.x.x.x.x.xxxx"), SN: P("........X......."), K: P("X..x....x.x.....")}
    pre = {HC: P("xxxxxxxxxxxxxxxx"), CL: P("........X......."), K: P("X.....x.X.......")}
    ch = {HO: P("..x...x...x...x."), SN: P("........X......X"), CL: P("........x......."), K: P("X.x...x.X.x.....")}
    br = {SH: P("x.x.x.x.x.x.x.x."), K: P("X...............")}
    intro = {HC: P("x.x.x.x.x.x.x.x."), K: P("X.......X.......")}
    return dict(verse=v, verse_b=vb, pre=pre, chorus=ch, bridge=br, intro=intro)


def electro_lanes():  # four on the floor
    v = {K: P("X...X...X...X..."), HO: P("..x...x...x...x."), CL: P("....X.......X...")}
    pre = {K: P("X...X...X...X..."), HC: P("xxxxxxxxxxxxxxxx"), CL: P("....X.......X...")}
    ch = {K: P("X...X...X...X..."), HO: P("..X...X...X...X."), CL: P("....X.......X..."), HC: P("x.x.x.x.x.x.x.x."), CR: P("X...............")}
    br = {K: P("X...............") , SH: P("x.x.x.x.x.x.x.x.")}
    intro = {K: P("X...X...X...X..."), HC: P("..x...x...x...x.")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


# shuffle / triplet grids: 12 steps per 4/4 bar (step = 160 ticks); 6/8 and 12/8 use eighth steps -------------
def blues_lanes():    # 12 steps: triplet shuffle
    v = {RD: P("x.xx.xx.xx.x"), SN: P("...X.....X.."), K: P("X.....X.....")}
    pre = {HC: P("x.xx.xx.xx.x"), SN: P("...X.....X.."), K: P("X.....X..X..")}
    ch = {HO: P("x.xx.xx.xx.x"), SN: P("...X.....X.."), K: P("X..x..X..x..")}
    br = {RD: P("x.xx.xx.xx.x"), SS: P("...x.....x.."), K: P("X...........")}
    intro = {HC: P("x..x..x..x.."), K: P("X.....X.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def jazz_lanes():     # swing ride on 12-step triplet grid
    v = {RD: P("x..x.xx..x.x"), HP: P("...X.....X.."), SN: P("..o...o...o."), K: P("x...........")}
    pre = {RD: P("x..x.xx..x.x"), HP: P("...X.....X.."), SN: P("..o..o...o.o"), K: P("x.....o.....")}
    ch = {RD: P("X..x.xX..x.x"), HP: P("...X.....X.."), SN: P("o.o..o.o.o.o"), K: P("x.....x.....")}
    br = {RD: P("x..x.xx..x.x"), HP: P("...X.....X.."), SS: P("..x.........")}
    intro = {HC: P("x..x..x..x.."), K: P("x.....x.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def sixeight_lanes():  # 6 steps of eighth notes (6/8)
    v = {HC: P("xxxxxx"), SN: P("...X.."), K: P("X.....")}
    pre = {HC: P("xxxxxx"), SN: P("...X.x"), K: P("X..x..")}
    ch = {HO: P("x.xx.x"), SN: P("...X.."), K: P("X..x.."), CR: P("X.....")}
    br = {RD: P("x.xx.x"), SS: P("...x.."), K: P("X.....")}
    intro = {HC: P("x.xx.x"), K: P("X.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def twelveeight_lanes():  # 12 steps of eighth notes (12/8 slow gospel/worship)
    v = {HC: P("xxxxxxxxxxxx"), SN: P("...,..."[:0] + "......X....."), K: P("X.....X.....")}
    v[SN] = P("......X.....")
    v[K] = P("X..x..X.....")
    pre = {HC: P("xxxxxxxxxxxx"), SN: P("...o..X..o.X"), K: P("X..x..X.x..x")}
    ch = {HO: P("x.xx.xx.xx.x"), SN: P("......X.....") , K: P("X..x..X..x.."), CR: P("X...........")}
    br = {RD: P("x..x..x..x.."), SS: P("......x....."), K: P("X...........")}
    intro = {SH: P("xxxxxxxxxxxx"), K: P("X.....X.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def waltz_lanes():    # 3/4 jazz waltz, 12 steps of triplet 8ths? use 6 steps (eighths)
    v = {RD: P("x.xx.x"), HP: P(".X.X.."), K: P("x.....")}
    pre = {RD: P("x.xx.x"), HP: P(".X.X.."), SN: P("..o..o"), K: P("x.....")}
    ch = {RD: P("X.xx.x"), SN: P("..o.o."), K: P("x..x..")}
    br = {RD: P("x.xx.x"), SS: P("..x...")}
    intro = {HC: P("x.x.x."), K: P("x.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


def threefour_lanes():  # straight 3/4 (pop/rock ballad)
    v = {HC: P("xxxxxx"), SN: P("..X.X."), K: P("X.....")}
    pre = {HC: P("xxxxxx"), SN: P("..X.X."), K: P("X..x..")}
    ch = {HO: P("x.x.x."), SN: P("..X.X."), K: P("X..x.."), CR: P("X.....")}
    br = {RD: P("x.x.x."), SS: P("..x.x."), K: P("X.....")}
    intro = {HC: P("x.x.x."), K: P("X.....")}
    return dict(verse=v, pre=pre, chorus=ch, bridge=br, intro=intro)


S16 = lambda: Meter(4, 4, PPQ // 4)
S3T = lambda: Meter(4, 4, PPQ // 3)
S68 = lambda: Meter(6, 8, PPQ // 2)
S128 = lambda: Meter(12, 8, PPQ // 2)
S34 = lambda: Meter(3, 4, PPQ // 2)
S34_8 = lambda: Meter(3, 4, PPQ // 2)

# style -> list of (meter factory, lane set function, [bpms], numerator/denominator label)
STYLES = {
    "Rock": [(S16, rock_lanes, [90, 110, 130, 150]), (S68, sixeight_lanes, [60, 76]), (S34, threefour_lanes, [90, 120])],
    "Metal": [(S16, metal_lanes, [110, 140, 170, 200])],
    "Funk": [(S16, funk_lanes, [90, 100, 110, 120])],
    "Pop": [(S16, pop_lanes, [85, 100, 115, 128]), (S34, threefour_lanes, [80, 100])],
    "Gospel": [(S16, gospel_lanes, [80, 100, 120, 140]), (S68, sixeight_lanes, [56, 66, 76]), (S128, twelveeight_lanes, [50, 60, 70])],
    "Worship": [(S16, worship_lanes, [64, 72, 80, 90, 120]), (S68, sixeight_lanes, [54, 64, 72]), (S128, twelveeight_lanes, [48, 58, 68])],
    "Latin": [(S16, latin_lanes, [90, 100, 110, 120])],
    "Bossa Nova": [(S16, bossa_lanes, [100, 120, 140])],
    "Samba": [(S16, samba_lanes, [90, 100, 110, 120])],
    "Jazz": [(S3T, jazz_lanes, [100, 130, 160, 190]), (S34, waltz_lanes, [120, 150])],
    "Blues": [(S3T, blues_lanes, [70, 90, 110]), (S68, sixeight_lanes, [50, 60, 70])],
    "Reggae": [(S16, reggae_lanes, [70, 80, 90])],
    "Country": [(S16, country_lanes, [100, 120, 140])],
    "Hip-Hop": [(S16, hiphop_lanes, [75, 85, 95])],
    "Eletrônica": [(S16, electro_lanes, [110, 124, 128, 140])],
}
VARIANTS = {"Intro": 2, "Verso": 3, "Pré-refrão": 2, "Refrão": 3, "Ponte": 2, "Viradas": 5, "Especiais": 3, "Finais": 2}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "Grooves"
    count = 0
    for style, entries in STYLES.items():
        for factory, lane_fn, bpms in entries:
            m = factory()
            lanes = lane_fn()
            for bpm in bpms:
                folder = f"{m.num}-{m.den} {bpm}bpm"
                for si, section in enumerate(SECTIONS):
                    for var in range(VARIANTS[section]):
                        rng = random.Random(hash((style, m.num, m.den, bpm, section, var)) & 0xFFFFFFFF)
                        rng = random.Random(f"{style}/{folder}/{section}/{var}")
                        ev, bars = make_clip(lanes, style, m, section, var, rng)
                        name = f"{section} {var + 1:02d}"
                        path = os.path.join(out, style, folder, f"{si + 1:02d} {section}", name + ".mid")
                        write_mid(path, ev, m.num, m.den, bpm, bars)
                        count += 1
    print(f"wrote {count} clips to {out}")


if __name__ == "__main__":
    main()
