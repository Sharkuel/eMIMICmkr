"""Turn folders of rendered drum hits (WAV) into Pearl Mimic instruments (.mci).

Reads the layout that SDSE and similar renderers write: one folder per zone,
files named "<piece> - <ZONE> - <hit>v<velocity>.wav", for example

    22 ZILDJIAN K Dark Medium Ride_DEH9 - RIDE1 BELL - 001v127.wav

Folders that share a piece name become one instrument. Several folders given
together (say a ride's BOW, BELL and EDGE folders) are read as one input. Usage:

    python3 tools/mci/wav2mci.py INPUT.zip|FOLDER [FOLDER...] [--out DIR] [--name NAME]

Hits are sorted by velocity and split into velocity layers of about 4 to 6
round robins each, depending on the zone (see round_robins), at most 20 layers
of 6; hits beyond that are thinned out evenly across velocities. Each layer's
minimum velocity is the lowest velocity recorded in it, so the Mimic follows
the plugin's own velocity response. Cymbals with no choke recording get one
made from the hardest edge hit (see mci.make_choke).

Hi-hat folders ("HATS BOW OPEN3", "HATS PEDAL CLOSED", "HATS FOOT SPLASH")
become a Mimic hi-hat: five openness steps per playing area, picked evenly from
closed to the most open recording, with 8 layers of up to 32 hits each.
--hat-as-ride also writes a 3-zone ride-type version (closed bow, bell, edge)
for 3-zone hi-hat pads.

Each hit is cut to a length that suits its piece (see max_seconds) and faded out
over its last half second, because long tails make the Mimic glitch once several
instruments are loaded. --full-length keeps every tail as recorded.
"""
from __future__ import annotations

import argparse
import io
import os
import re
import struct
import sys
import wave
import zipfile
import zlib
from collections import defaultdict
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mci  # noqa: E402

SAMPLE_RATE = 44100      # the Mimic's rate, judging by Pearl's factory content
MAX_LAYERS = 20          # what the Editor's and Pearl's cymbal files use
MAX_ROUND_ROBINS = 6     # per velocity layer, for kicks, snares, toms and cymbals
# Wave cells the Mimic allows per instrument, shared by its zones (Alexandre, 2026-10-07).
# Cymbals and rides keep one of theirs for the choke ring. Hi-hats: not known yet.
# Cymbals and rides are written in stereo, and a stereo cymbal with the full 200 cells
# crashed the Mimic while 120 played clean (Alexandre, 2026-10-13), so they get 120,
# like the purchased Meinl stereo rides (77-125).
CELL_LIMITS = {"Kick": 64, "Snare": 300, "Tom": 200, "Cymbal": 120, "Ride": 120, "Perc": 200}
# Longest hit kept per piece, in seconds; ride-type cymbals (with a bell) get more.
MAX_SECONDS = {"kick": 2.0, "snare": 3.0, "tom": 3.0, "hat": 3.0, "perc": 3.0, "cymbal": 6.0, "ride": 8.0}
FADE_SECONDS = 0.5       # fade-out at the end of a hit that had to be cut
HIT_NAME = re.compile(r"^(?P<piece>.+?) - (?P<zone>[^-]+?) - (?P<hit>\d+)v(?P<vel>\d+)\.wav$", re.I)

CYMBAL_WORDS = ("RIDE", "CRASH", "SPLASH", "CHINA", "STACK", "CYMBAL", "BELL")
ZONE_WORDS = {"BOW": "bow", "BELL": "bell", "EDGE": "edge", "CHOKE": "choke",
              "CENTER": "center", "CENTRE": "center", "HEAD": "center", "HIT": "center",
              "RIMSHOT": "rimshot", "RIM": "rimshot", "RIMCLICK": "rimclick", "CROSSSTICK": "sidestick",
              "SIDESTICK": "sidestick", "XSTICK": "sidestick"}


@dataclass
class Hit:
    velocity: int
    path: str
    blend: str | None = None     # a second hit mixed 50/50 with this one (made-up side rimshots)


@dataclass
class Piece:
    name: str
    family: str                                   # "cymbal", "kick", "snare", "tom"
    zones: dict[str, list[Hit]] = field(default_factory=dict)


def common_folder(paths: list[str]) -> str | None:
    """The deepest folder holding every path (absolute), None when there is none."""
    try:
        return os.path.commonpath([os.path.abspath(p) for p in paths])
    except ValueError:      # different drives, or no paths
        return None


class Source:
    """A folder or a zip, read the same way. Several inputs are read as one: every file
    under each folder, named relative to the folder holding them all, so a ride's BOW,
    BELL and EDGE folders given together read like their parent folder. A zip or anything
    else that is not a folder among several inputs is skipped (see .skipped)."""

    def __init__(self, paths: str | list[str]):
        paths = [paths] if isinstance(paths, str) else list(paths)
        self.skipped: list[str] = []
        self.zip = None
        if len(paths) == 1:
            self.path, self.roots = paths[0], [paths[0]]
            self.zip = zipfile.ZipFile(paths[0]) if zipfile.is_zipfile(paths[0]) else None
            return
        self.roots = []
        for p in paths:
            if os.path.isdir(p):
                self.roots.append(os.path.abspath(p))
            else:
                why = ("a .zip is converted on its own, not with other inputs" if zipfile.is_zipfile(p)
                       else "not a folder")
                self.skipped.append(f"{os.path.abspath(p)}: {why}")
        self.path = common_folder(self.roots) if self.roots else None

    def names(self) -> list[str]:
        if self.zip:
            return [n for n in self.zip.namelist() if not n.endswith("/")]
        if self.path is None:
            return []
        out, seen = [], set()
        for top in self.roots:
            for root, _, files in os.walk(top):
                for f in files:
                    name = os.path.relpath(os.path.join(root, f), self.path)
                    if name not in seen:      # a folder given inside another one
                        seen.add(name)
                        out.append(name)
        return out

    def read(self, name: str) -> bytes:
        if self.zip:
            return self.zip.read(name)
        with open(os.path.join(self.path, name), "rb") as fh:
            return fh.read()

    def top_folder(self) -> str | None:
        tops = {n.replace("\\", "/").split("/")[0] for n in self.names() if "/" in n.replace("\\", "/")}
        return tops.pop() if len(tops) == 1 else None


def _float_wav(data: bytes):
    """(channels, rate, raw) of a 32-bit float WAV, or None for any other WAV."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    pos, fmt = 12, None
    while pos + 8 <= len(data):
        kind, size = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 8], "little")
        body = data[pos + 8:pos + 8 + size]
        if kind == b"fmt ":
            tag, ch, rate = struct.unpack_from("<HHI", body)
            bits = struct.unpack_from("<H", body, 14)[0]
            if tag == 0xFFFE and len(body) >= 40:
                tag = struct.unpack_from("<H", body, 24)[0]
            if tag != 3:
                return None
            if bits != 32:
                raise ValueError(f"unsupported float sample width {bits} bit")
            fmt = (ch, rate)
        elif kind == b"data" and fmt is not None:
            return fmt[0], fmt[1], body[:len(body) // (4 * fmt[0]) * 4 * fmt[0]]
        pos += 8 + size + (size & 1)
    raise ValueError("WAV fmt chunk and/or data chunk missing")


def read_wav(data: bytes) -> list[int]:
    """Mono samples on the 24-bit scale. Stereo is averaged; 16-bit lands on the Editor's grid.

    32-bit float WAVs (what the recorder writes) may go past full scale; build_instrument()
    turns such an instrument down as a whole.
    """
    flt = _float_wav(data)
    if flt is not None:
        ch, rate, raw = flt
        if rate != SAMPLE_RATE:
            raise ValueError(f"{rate} Hz; render at {SAMPLE_RATE} Hz (resampling is not built yet)")
        # Kept within 2**30, so a wild value can't overflow (nor NaN stop the conversion).
        vals = [round(max(-2.0 ** 30, min(2.0 ** 30, v * 2 ** 23))) if v == v else 0
                for v in struct.unpack(f"<{len(raw) // 4}f", raw)]
    else:
        w = wave.open(io.BytesIO(data))
        if w.getframerate() != SAMPLE_RATE:
            raise ValueError(f"{w.getframerate()} Hz; render at {SAMPLE_RATE} Hz (resampling is not built yet)")
        width, ch = w.getsampwidth(), w.getnchannels()
        raw = w.readframes(w.getnframes())
        if width == 2:
            vals = [v << 8 for v in struct.unpack(f"<{len(raw) // 2}h", raw)]
        elif width == 3:
            vals = [int.from_bytes(raw[i:i + 3], "little", signed=True) for i in range(0, len(raw), 3)]
        elif width == 4:
            vals = [v >> 8 for v in struct.unpack(f"<{len(raw) // 4}i", raw)]
        else:
            raise ValueError(f"unsupported sample width {width * 8} bit")
    if ch > 1:
        vals = [sum(vals[i:i + ch]) // ch for i in range(0, len(vals), ch)]
    return vals


HAT_HEADS = ("HATS", "HAT", "HIHAT", "HIHATS", "HH")


def classify_hat(zone_label: str) -> str | None:
    """'HATS BOW OPEN3' -> 'bow:open3', 'HATS PEDAL CLOSED' -> 'pedal', else None."""
    words = zone_label.upper().replace("-", " ").split()
    if not words or re.sub(r"\d+$", "", words[0]) not in HAT_HEADS:
        return None
    rest = words[1:]
    if rest[:1] == ["PEDAL"]:
        return "pedal"
    if rest[:2] == ["FOOT", "SPLASH"] or rest[:1] in (["FOOTSPLASH"], ["SPLASH"]):
        return "footsplash"
    if len(rest) >= 2 and rest[0] in ("BOW", "TIP", "EDGE", "SHANK", "BELL"):
        return f"{rest[0].lower()}:{''.join(rest[1:]).lower()}"
    raise ValueError(f"unknown hi-hat zone {zone_label!r}")


def openness_rank(level: str) -> float:
    """TIGHT < CLOSED < CLOSED2 < OPEN0 < OPEN1 < ...; unknown words sort last."""
    m = re.fullmatch(r"(tight|closed|half|halfopen|open)(\d*)", level)
    if not m:
        return 1000.0
    n = int(m[2] or 0)
    return {"tight": -2, "closed": -1 + n / 100, "half": 2.5, "halfopen": 2.5, "open": n}[m[1]]


def pick_openness(levels: list[str], steps: int = 5) -> list[str]:
    """Five levels from closed to open. TIGHT stands in only when there is no CLOSED."""
    levels = sorted(levels, key=openness_rank)
    if len(levels) > steps and levels[0] == "tight" and any(l.startswith("closed") for l in levels):
        levels = levels[1:]
    if len(levels) >= steps:
        return [levels[round(i * (len(levels) - 1) / (steps - 1))] for i in range(steps)]
    return [levels[min(i, len(levels) - 1)] for i in range(steps)]   # repeat the most open


def classify(zone_label: str) -> tuple[str, str]:
    """('cymbal', 'bell') from labels like 'RIDE1 BELL', 'SPLASH1 EDGE', 'KICK1'."""
    hat = classify_hat(zone_label)
    if hat:
        return "hat", hat
    words = re.sub(r"\d+", " ", zone_label.upper()).split()
    if not words:
        raise ValueError(f"cannot read zone {zone_label!r}")
    head = words[0]
    if head in ("KICK", "BD", "BASSDRUM"):
        return "kick", "kick"
    family = ("cymbal" if head in CYMBAL_WORDS else "snare" if head in ("SNARE", "SD")
              else "tom" if head in ("TOM", "FLOORTOM", "RACKTOM") else None)
    if family is None:
        raise ValueError(f"unknown instrument in zone {zone_label!r}")
    zone = next((ZONE_WORDS[w] for w in words[1:] if w in ZONE_WORDS), None)
    if family == "snare" and ("SHALLOW" in words or (zone == "rimshot" and "SIDE" in words)):
        zone = "rimshotside"
    if zone is None:
        zone = "bow" if family == "cymbal" else "center"
    return family, zone


def scan(src: Source) -> tuple[list[Piece], list[str]]:
    pieces: dict[str, Piece] = {}
    skipped = list(src.skipped)
    for name in src.names():
        base = name.replace("\\", "/").rsplit("/", 1)[-1]
        m = HIT_NAME.match(base)
        if not m:
            if base.lower().endswith(".wav"):
                skipped.append(f"{name}: name not in '<piece> - <ZONE> - <hit>v<velocity>.wav' form")
            continue
        try:
            family, zone = classify(m["zone"])
        except ValueError as e:
            skipped.append(f"{name}: {e}")
            continue
        p = pieces.setdefault(m["piece"], Piece(m["piece"], family))
        p.zones.setdefault(zone, []).append(Hit(int(m["vel"]), name))
    return list(pieces.values()), skipped


def share_cells(budget: int, available: dict[str, int]) -> dict[str, int]:
    """Split a cell budget across zones as evenly as possible. A zone with fewer hits
    than its share keeps them all and hands the rest to the others."""
    out, left, remaining = {}, dict(available), budget
    while left:
        each = remaining // len(left)
        small = {k: v for k, v in left.items() if v <= each}
        if not small:
            extra = remaining - each * len(left)
            for i, k in enumerate(left):
                out[k] = each + (1 if i < extra else 0)
            break
        for k, v in small.items():
            out[k] = v
            remaining -= v
            del left[k]
    return out


def split_layers(hits: list[Hit], max_layers: int = MAX_LAYERS) -> list[list[Hit]]:
    """Softest to hardest, as even as possible, every layer at least one hit."""
    hits = sorted(hits, key=lambda h: h.velocity)
    n = min(max_layers, len(hits))
    bounds = [round(i * len(hits) / n) for i in range(n + 1)]
    return [hits[bounds[i]:bounds[i + 1]] for i in range(n)]


def round_robins(family: str, zone: str) -> int:
    """Round robins wanted per velocity layer: 6 on snare heads and rims, 5 on toms', 4
    elsewhere (kick, sidestick, rimclick, cymbals)."""
    if zone in ("center", "rimshot", "rimshotside") and family in ("snare", "tom"):
        return 6 if family == "snare" else 5
    return 4


def even_spread(hits: list[Hit], count: int) -> list[Hit]:
    """count of the hits, spread evenly over their velocities (the hardest alone for 1)."""
    hits = sorted(hits, key=lambda h: h.velocity)
    if count == 1:
        return [hits[-1]]
    return [hits[round(i * (len(hits) - 1) / (count - 1))] for i in range(count)]


def round_robin_layers(hits: list[Hit], rr: int) -> list[list[Hit]]:
    """About rr round robins per velocity layer: round(n / rr) layers, 1 to MAX_LAYERS.
    Hits beyond MAX_ROUND_ROBINS per layer are thinned out evenly across velocities."""
    layers = min(MAX_LAYERS, max(1, round(len(hits) / rr)))
    if len(hits) > layers * MAX_ROUND_ROBINS:
        hits = even_spread(hits, layers * MAX_ROUND_ROBINS)
    return split_layers(hits, layers)


def min_velocities(layers: list[list[Hit]]) -> list[int]:
    out = [0] + [layer[0].velocity for layer in layers[1:]]
    for i in range(1, len(out)):          # strictly rising, as the Mimic expects
        out[i] = min(127, max(out[i], out[i - 1] + 1))
    return out


def thumbnail(width: int = 76, height: int = 63) -> bytes:
    """A plain dark PNG, the size of the Editor's thumbnails."""
    row = b"\x00" + bytes((40, 44, 52)) * width
    raw = zlib.compress(row * height)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", raw) + chunk(b"IEND", b""))


def piece_title(piece: Piece, src: Source) -> str:
    """The zip's top folder when it names this piece (same size prefix), else the
    piece name without the renderer's preset code ("_DEH9")."""
    short = re.sub(r"_[A-Za-z0-9]+$", "", piece.name).strip()
    top = src.top_folder()
    if top and " - " not in top and top.split()[0] == short.split()[0]:
        return top
    return short


def max_seconds(piece: Piece) -> float:
    if piece.family == "cymbal" and "bell" in piece.zones:
        return MAX_SECONDS["ride"]
    return MAX_SECONDS.get(piece.family, MAX_SECONDS["cymbal"])


def fit_length(x: list[int], max_frames: int, fade_frames: int = round(FADE_SECONDS * SAMPLE_RATE)) -> list[int]:
    """x cut to max_frames, its last fade_frames faded out linearly to 0; x itself when shorter."""
    if len(x) <= max_frames:
        return x
    x = x[:max_frames]
    fade = min(fade_frames, max_frames)
    start = max_frames - fade
    for i in range(fade):
        x[start + i] = x[start + i] * (fade - 1 - i) // fade
    return x


def build(piece: Piece, src: Source, name: str, choke: bool = True, log=print,
          hat_as_ride: bool = False, full_length: bool = False, cells_percent: int = 100) -> mci.MciFile:
    max_frames = None if full_length else round(max_seconds(piece) * SAMPLE_RATE)

    def cells(kind):     # the instrument's wave-cell budget, cut to cells_percent
        return max(2, CELL_LIMITS[kind] * cells_percent // 100)

    def read(h):
        x = read_wav(src.read(h.path))
        if h.blend is None:
            return x
        y = read_wav(src.read(h.blend))
        if len(y) > len(x):
            x, y = y, x
        return [(a + (y[i] if i < len(y) else 0)) // 2 for i, a in enumerate(x)]

    def load(hits_layers):
        if max_frames is None:
            return [[read(h) for h in layer] for layer in hits_layers]
        return [[fit_length(read(h), max_frames) for h in layer] for layer in hits_layers]

    zones = dict(piece.zones)
    if piece.family == "snare" and "rimshotside" not in zones and "rimshot" in zones:
        # No shallow (side) rimshots recorded: mix each rimshot with the rim click (or the
        # head hit) nearest its velocity, so the Mimic's Rim Shot Side zone isn't silent.
        other = "rimclick" if "rimclick" in zones else "center" if "center" in zones else None
        if other:
            zones["rimshotside"] = [Hit(h.velocity, h.path, min(zones[other], key=lambda o: abs(o.velocity - h.velocity)).path)
                                    for h in zones["rimshot"]]
            log(f"  {name}: no side rimshot recording, blending rimshot and {other} for it")

    def zone(key, max_layers=MAX_LAYERS, max_hits=None, rr=None):
        hits = zones[key]
        if max_hits and len(hits) > max_hits:     # keep an even spread of velocities
            hits = even_spread(hits, max_hits)
        layers = round_robin_layers(hits, rr) if rr else split_layers(hits, max_layers)
        return load(layers), min_velocities(layers)

    if piece.family == "hat":
        return build_hat(piece, zone, name, hat_as_ride, log, hat_hits=max(1, HAT_HITS * cells_percent // 100))

    if piece.family == "kick":
        audio, vel = zone("kick", max_hits=cells("Kick"), rr=round_robins("kick", "kick"))
        arts = [mci.Articulation("Kick", mci.ZONE_TYPES["kick"], audio, vel)]
        return mci.build_instrument(name, "Kick", arts, thumbnail(), mic="Kick")

    if piece.family == "cymbal":
        z = piece.zones
        if "edge" not in z and "bow" not in z and "bell" not in z:
            raise ValueError(f"{piece.name}: a cymbal needs an edge, bow or bell recording")
        bell_only = "edge" not in z and "bow" not in z     # say a BELL folder converted on its own
        edge_key = "edge" if "edge" in z else "bow" if "bow" in z else "bell"
        bow_key = "bow" if "bow" in z else "edge" if "edge" in z else "bell"
        if bell_only:
            log(f"  {name}: only a bell recording, using it for the edge and bow zones")
        elif edge_key != "edge" or bow_key != "bow":
            log(f"  {name}: no {'bow' if bow_key == 'edge' else 'edge'} recording, reusing the "
                f"{bow_key if bow_key == 'edge' else edge_key} for that zone")
        has_bell = "bell" in z and not bell_only
        kind = "Ride" if has_bell else "Cymbal"
        shares = share_cells(cells(kind) - 1, {"edge": len(z[edge_key]), "bow": len(z[bow_key]),
                                                     **({"bell": len(z["bell"])} if has_bell else {})})
        edge, edge_vel = zone(edge_key, max_hits=shares["edge"], rr=round_robins("cymbal", "edge"))
        bow, bow_vel = zone(bow_key, max_hits=shares["bow"], rr=round_robins("cymbal", "bow"))
        choke_layers = None
        if "choke" in z:      # the Mimic keeps one choke-ring cell: the hardest recording
            hardest = max(z["choke"], key=lambda h: h.velocity)
            choke_layers = [[read_wav(src.read(hardest.path))]]
        elif choke:
            hardest = max(z[edge_key], key=lambda h: h.velocity)
            x = read_wav(src.read(hardest.path))
            top = 2 ** 23 - 1
            choke_layers = [[[max(-top, min(top, round(v))) for v in mci.make_choke(x, SAMPLE_RATE)]]]
        if has_bell:
            bell, bell_vel = zone("bell", max_hits=shares["bell"], rr=round_robins("cymbal", "bell"))
            arts = mci.ride_articulations(bow, bell, edge, choke=choke_layers)
            vels = [bow_vel, bell_vel, edge_vel]
        else:
            arts = mci.cymbal_articulations(edge, bow, choke=choke_layers)
            vels = [edge_vel, bow_vel]
        for art, v in zip(arts, vels):
            art.min_velocities = v
        return mci.build_instrument(name, kind, arts, thumbnail(), mic=kind)

    zone_ids = ({"center": "drum center", "rimshot": "drum rimshot", "rimclick": "drum rimclick",
                 "rimshotside": "snare rimshot side", "sidestick": "snare sidestick"})
    names = {"center": "Center", "rimshot": "Rimshot", "rimclick": "Rimclick", "rimshotside": "Rimshot Side",
             "sidestick": "Sidestick"}
    kind = "Snare" if piece.family == "snare" else "Tom"
    keys = [k for k in ["center", "rimclick", "rimshot", "rimshotside", "sidestick"]
            if k in zones and (k not in ("sidestick", "rimshotside") or piece.family == "snare")]
    shares = share_cells(cells(kind), {k: len(zones[k]) for k in keys})
    arts = []
    for key in keys:
        audio, vel = zone(key, max_hits=shares[key], rr=round_robins(piece.family, key))
        label = f"{'Snare' if piece.family == 'snare' else 'Tom'} {names[key]}"
        arts.append(mci.Articulation(label, mci.ZONE_TYPES[zone_ids[key]], audio, vel))
    return mci.build_instrument(name, kind, arts, thumbnail(), mic=kind,
                                sample_volume=0.599 if kind == "Snare" else 0.52)


HAT_LAYERS, HAT_HITS = 8, 32     # per articulation; 17 of them would otherwise run to gigabytes


def build_hat(piece: Piece, zone, name: str, as_ride: bool, log=print, hat_hits: int = HAT_HITS) -> mci.MciFile:
    """SDSE hi-hat folders -> the factory Hat layout, or a ride-style 3-zone instrument.

    Mapping (inferred, not from Pearl): the Mimic's tip zone plays BOW, its shank
    zone plays EDGE, and its third (edge) controller zone plays BELL when there is
    one, else EDGE again.
    """
    areas: dict[str, list[str]] = defaultdict(list)
    for key in piece.zones:
        if ":" in key:
            area, level = key.split(":", 1)
            areas[{"tip": "bow", "shank": "edge"}.get(area, area)].append(level)
    if "bow" not in areas and "edge" not in areas:
        raise ValueError(f"{piece.name}: a hi-hat needs BOW or EDGE folders")
    tip_area = "bow" if "bow" in areas else "edge"
    shank_area = "edge" if "edge" in areas else "bow"
    third_area = "bell" if "bell" in areas else shank_area

    def steps(area):
        levels = pick_openness(areas[area])
        log(f"  {name}: {area} closed->open uses {', '.join(l.upper() for l in levels)}")
        return [zone(f"{area}:{l}", HAT_LAYERS, hat_hits)[0] for l in levels]

    if as_ride:
        def closed(area):
            level = pick_openness(areas[area])[0]
            return zone(f"{area}:{level}")
        bow, bow_vel = closed(tip_area)
        edge, edge_vel = closed(shank_area)
        bell, bell_vel = closed(third_area)
        arts = mci.ride_articulations(bow, bell, edge)
        for art, v in zip(arts, [bow_vel, bell_vel, edge_vel]):
            art.min_velocities = v
        return mci.build_instrument(name, "Ride", arts, thumbnail(), mic="Ride")

    tip, shank, edge = steps(tip_area), steps(shank_area), steps(third_area)
    pedal = zone("pedal", HAT_LAYERS, hat_hits)[0] if "pedal" in piece.zones else []
    splash = zone("footsplash", HAT_LAYERS, hat_hits)[0] if "footsplash" in piece.zones else []
    for missing, got in (("pedal", pedal), ("foot splash", splash)):
        if not got:
            log(f"  {name}: no {missing} recording, that zone stays silent")
    arts, ctrl = mci.hat_articulations(tip, shank, edge, pedal, splash)
    return mci.build_instrument(name, "Hat", arts, thumbnail(), mic="Hi-Hat", controller_zones=ctrl)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input", nargs="+",
                    help="a .zip or a folder of rendered WAVs; several folders are read as one input")
    ap.add_argument("--out", help="output folder (default: next to the input, in the folder holding them all)")
    ap.add_argument("--name", help="instrument name (only when the input holds one piece)")
    ap.add_argument("--no-choke", action="store_true", help="leave the Choke Ring empty")
    ap.add_argument("--hat-as-ride", action="store_true",
                    help="also build each hi-hat as a 3-zone ride-type instrument (closed; bow, bell, edge)")
    ap.add_argument("--cells-percent", type=int, default=100,
                    help="use this percentage of each instrument's wave-cell limit (fewer hits, lighter instruments)")
    ap.add_argument("--full-length", action="store_true",
                    help="keep every hit's whole tail instead of cutting it to suit the piece")
    args = ap.parse_args(argv)

    src = Source(args.input)
    if len(args.input) > 1 and src.roots and src.path is None:
        print("the inputs share no folder")
        return 1
    pieces, skipped = scan(src)
    for s in skipped:
        print("skipped", s)
    if not pieces:
        print("no recognisable hits found")
        return 1
    out_dir = (args.out or common_folder([os.path.dirname(os.path.abspath(p)) for p in args.input])
               or os.path.dirname(os.path.abspath(args.input[0])))
    os.makedirs(out_dir, exist_ok=True)
    for piece in pieces:
        name = args.name if args.name and len(pieces) == 1 else piece_title(piece, src)
        counts = ", ".join(f"{z} {len(h)}" for z, h in piece.zones.items())
        print(f"{name}: {counts}")
        builds = [(name, False)]
        if piece.family == "hat" and args.hat_as_ride:
            builds.append((f"{name} 3-zone", True))
        for title, as_ride in builds:
            f = build(piece, src, title, choke=not args.no_choke, hat_as_ride=as_ride,
                      full_length=args.full_length, cells_percent=args.cells_percent)
            path = os.path.join(out_dir, f"{title}.mci")
            with open(path, "wb") as fh:
                fh.write(mci.write_mci(f))
            print(f"  wrote {path} ({f.get('INST0instyp')}, {f.get('INST0pool0psz')} samples)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
