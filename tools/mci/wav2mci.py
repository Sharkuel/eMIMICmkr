"""Turn folders of rendered drum hits (WAV) into Pearl Mimic instruments (.mci).

Reads the layout that SDSE and similar renderers write: one folder per zone,
files named "<piece> - <ZONE> - <hit>v<velocity>.wav", for example

    22 ZILDJIAN K Dark Medium Ride_DEH9 - RIDE1 BELL - 001v127.wav

Folders that share a piece name become one instrument. Usage:

    python3 tools/mci/wav2mci.py INPUT.zip|FOLDER [--out DIR] [--name NAME]

Hits are sorted by velocity and split into up to 20 layers, the rest of each
layer becoming round robins, as in the Editor's own files. Each layer's
minimum velocity is the lowest velocity recorded in it, so the Mimic follows
the plugin's own velocity response. Cymbals with no choke recording get one
made from the hardest edge hit (see mci.make_choke).
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


@dataclass
class Piece:
    name: str
    family: str                                   # "cymbal", "kick", "snare", "tom"
    zones: dict[str, list[Hit]] = field(default_factory=dict)


class Source:
    """A folder or a zip, read the same way."""

    def __init__(self, path: str):
        self.path = path
        self.zip = zipfile.ZipFile(path) if zipfile.is_zipfile(path) else None

    def names(self) -> list[str]:
        if self.zip:
            return [n for n in self.zip.namelist() if not n.endswith("/")]
        out = []
        for root, _, files in os.walk(self.path):
            out += [os.path.relpath(os.path.join(root, f), self.path) for f in files]
        return out

    def read(self, name: str) -> bytes:
        if self.zip:
            return self.zip.read(name)
        with open(os.path.join(self.path, name), "rb") as fh:
            return fh.read()

    def top_folder(self) -> str | None:
        tops = {n.replace("\\", "/").split("/")[0] for n in self.names() if "/" in n.replace("\\", "/")}
        return tops.pop() if len(tops) == 1 else None


def read_wav(data: bytes) -> list[int]:
    """Mono samples on the 24-bit scale. Stereo is averaged; 16-bit lands on the Editor's grid."""
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


def classify(zone_label: str) -> tuple[str, str]:
    """('cymbal', 'bell') from labels like 'RIDE1 BELL', 'SPLASH1 EDGE', 'KICK1'."""
    words = re.sub(r"\d+", " ", zone_label.upper()).split()
    if not words:
        raise ValueError(f"cannot read zone {zone_label!r}")
    head = words[0]
    if head in ("KICK", "BD", "BASSDRUM"):
        return "kick", "kick"
    if head in ("HIHAT", "HH", "HAT"):
        raise ValueError(f"hi-hat folders ({zone_label!r}) are not mapped yet")
    family = ("cymbal" if head in CYMBAL_WORDS else "snare" if head in ("SNARE", "SD")
              else "tom" if head in ("TOM", "FLOORTOM", "RACKTOM") else None)
    if family is None:
        raise ValueError(f"unknown instrument in zone {zone_label!r}")
    zone = next((ZONE_WORDS[w] for w in words[1:] if w in ZONE_WORDS), None)
    if zone is None:
        zone = "bow" if family == "cymbal" else "center"
    return family, zone


def scan(src: Source) -> tuple[list[Piece], list[str]]:
    pieces: dict[str, Piece] = {}
    skipped = []
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


def split_layers(hits: list[Hit], max_layers: int = MAX_LAYERS) -> list[list[Hit]]:
    """Softest to hardest, as even as possible, every layer at least one hit."""
    hits = sorted(hits, key=lambda h: h.velocity)
    n = min(max_layers, len(hits))
    bounds = [round(i * len(hits) / n) for i in range(n + 1)]
    return [hits[bounds[i]:bounds[i + 1]] for i in range(n)]


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


def build(piece: Piece, src: Source, name: str, choke: bool = True, log=print) -> mci.MciFile:
    def load(hits_layers):
        return [[read_wav(src.read(h.path)) for h in layer] for layer in hits_layers]

    def zone(key):
        layers = split_layers(piece.zones[key])
        return load(layers), min_velocities(layers)

    if piece.family == "kick":
        audio, vel = zone("kick")
        arts = [mci.Articulation("Kick", mci.ZONE_TYPES["kick"], audio, vel)]
        return mci.build_instrument(name, "Kick", arts, thumbnail(), mic="Kick")

    if piece.family == "cymbal":
        z = piece.zones
        if "edge" not in z and "bow" not in z:
            raise ValueError(f"{piece.name}: a cymbal needs an edge or bow recording")
        edge_key = "edge" if "edge" in z else "bow"
        bow_key = "bow" if "bow" in z else "edge"
        if edge_key != "edge" or bow_key != "bow":
            log(f"  {name}: no {'bow' if bow_key == 'edge' else 'edge'} recording, reusing the "
                f"{bow_key if bow_key == 'edge' else edge_key} for that zone")
        edge, edge_vel = zone(edge_key)
        bow, bow_vel = zone(bow_key)
        choke_layers = None
        if "choke" in z:
            hits = sorted(z["choke"], key=lambda h: h.velocity)[-mci.MAX_ROUND_ROBINS:]
            choke_layers = [[read_wav(src.read(h.path)) for h in hits]]
        elif choke:
            hardest = max(z[edge_key], key=lambda h: h.velocity)
            x = read_wav(src.read(hardest.path))
            top = 2 ** 23 - 1
            choke_layers = [[[max(-top, min(top, round(v))) for v in mci.make_choke(x, SAMPLE_RATE)]]]
        if "bell" in z:
            bell, bell_vel = zone("bell")
            arts = mci.ride_articulations(bow, bell, edge, choke=choke_layers)
            vels, kind = [bow_vel, bell_vel, edge_vel], "Ride"
        else:
            arts = mci.cymbal_articulations(edge, bow, choke=choke_layers)
            vels, kind = [edge_vel, bow_vel], "Cymbal"
        for art, v in zip(arts, vels):
            art.min_velocities = v
        return mci.build_instrument(name, kind, arts, thumbnail(), mic=kind)

    zone_ids = ({"center": "drum center", "rimshot": "drum rimshot", "rimclick": "drum rimclick",
                 "sidestick": "snare sidestick"})
    names = {"center": "Center", "rimshot": "Rimshot", "rimclick": "Rimclick", "sidestick": "Sidestick"}
    arts = []
    for key in ["center", "rimclick", "rimshot", "sidestick"]:
        if key in piece.zones and (key != "sidestick" or piece.family == "snare"):
            audio, vel = zone(key)
            label = f"{'Snare' if piece.family == 'snare' else 'Tom'} {names[key]}"
            arts.append(mci.Articulation(label, mci.ZONE_TYPES[zone_ids[key]], audio, vel))
    kind = "Snare" if piece.family == "snare" else "Tom"
    return mci.build_instrument(name, kind, arts, thumbnail(), mic=kind,
                                sample_volume=0.599 if kind == "Snare" else 0.52)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input", help="a .zip or a folder of rendered WAVs")
    ap.add_argument("--out", help="output folder (default: next to the input)")
    ap.add_argument("--name", help="instrument name (only when the input holds one piece)")
    ap.add_argument("--no-choke", action="store_true", help="leave the Choke Ring empty")
    args = ap.parse_args(argv)

    src = Source(args.input)
    pieces, skipped = scan(src)
    for s in skipped:
        print("skipped", s)
    if not pieces:
        print("no recognisable hits found")
        return 1
    out_dir = args.out or os.path.dirname(os.path.abspath(args.input))
    os.makedirs(out_dir, exist_ok=True)
    for piece in pieces:
        name = args.name if args.name and len(pieces) == 1 else piece_title(piece, src)
        counts = ", ".join(f"{z} {len(h)}" for z, h in piece.zones.items())
        print(f"{name}: {counts}")
        f = build(piece, src, name, choke=not args.no_choke)
        path = os.path.join(out_dir, f"{name}.mci")
        with open(path, "wb") as fh:
            fh.write(mci.write_mci(f))
        print(f"  wrote {path} ({f.get('INST0instyp')}, {f.get('INST0pool0psz')} samples)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
