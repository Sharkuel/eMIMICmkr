"""Reader, writer and builder for Pearl Mimic instrument files (.mci).

The format was reverse-engineered from files saved by the Mimic Instrument
Editor (container version 3). See docs/mci-format.md for the full description.

Samples are handled here as Python ints on a 24-bit scale (full scale =
2**23). Files written by the Editor only ever hold 16-bit mono audio, so the
low 8 bits are always zero there; build_instrument() quantizes to that grid.
Pearl's factory instruments hold true 24-bit audio, and some interleave
several channels (see pool0nchn).

The factory library splits an instrument in two: a .bin holding the metadata
and a .drd holding the RAM heads and compressed data. read_split() reads that.
"""
from __future__ import annotations

import hashlib
import math
import struct
import zlib
from dataclasses import dataclass, field

MAGIC = b"mimic_storage\x00"
TRAILER_TAG = b"MMKCSM\x00"
VERSION = 3

BLOCK_SIZE = 1024          # every compressed block is exactly this many bytes
BLOCK_HEADER = 8           # u8 bits, 24-bit first sample (u8 low, i16 high), u32 frame count
PAYLOAD_BITS = (BLOCK_SIZE - BLOCK_HEADER) * 8

HEAD_PAD = 63              # zero frames in front of every RAM head
HEAD_MAX_FRAMES = 32768    # a head never exceeds this many frames
HEAD_MAX_BLOCKS = 64       # ...nor this many compressed blocks

MAX_ROUND_ROBINS = 16      # length of every veloLay#smpidx array
RT_ARRAY_LEN = 8           # length of every release-time array

# Metadata sections, in file order. The sixth section has never been seen
# non-empty, so its value type is unknown.
I32, I32_ARRAY, F32, F32_ARRAY, U8, UNKNOWN, STRING = range(7)
SECTION_ORDER = (I32, I32_ARRAY, F32, F32_ARRAY, U8, UNKNOWN, STRING)


class MciError(Exception):
    pass


# ---------------------------------------------------------------------------
# Container
# ---------------------------------------------------------------------------

@dataclass
class MciFile:
    """A .mci file split into its parts. Round-trips byte for byte."""

    meta: dict[str, tuple[int, object]]   # key -> (section type, value)
    image: bytes                          # PNG thumbnail shown by the Editor
    heads: bytes                          # float32 RAM preload of every sample
    data: bytes                           # compressed blocks of every sample
    version: int = VERSION

    def get(self, key, default=None):
        entry = self.meta.get(key)
        return default if entry is None else entry[1]

    def set(self, key, section, value):
        self.meta[key] = (section, value)


def _read_meta(raw: bytes):
    off = 0

    def u32():
        nonlocal off
        (v,) = struct.unpack_from("<I", raw, off)
        off += 4
        return v

    def key():
        nonlocal off
        n = u32()
        k = raw[off:off + n].rstrip(b"\x00").decode("ascii")
        off += n
        return k

    meta = {}
    for section in SECTION_ORDER:
        for _ in range(u32()):
            k = key()
            if section == I32:
                (v,) = struct.unpack_from("<i", raw, off)
                off += 4
            elif section == I32_ARRAY:
                n = u32()
                v = list(struct.unpack_from(f"<{n}i", raw, off))
                off += 4 * n
            elif section == F32:
                (v,) = struct.unpack_from("<f", raw, off)
                off += 4
            elif section == F32_ARRAY:
                n = u32()
                v = list(struct.unpack_from(f"<{n}f", raw, off))
                off += 4 * n
            elif section == U8:
                v = raw[off]
                off += 1
            elif section == STRING:
                n = u32()
                v = raw[off:off + n].rstrip(b"\x00").decode("latin-1")
                off += n
            else:
                raise MciError(f"value type of metadata section {section} is unknown (key {k})")
            meta[k] = (section, v)
    image_len = u32()
    image = raw[off:off + image_len]
    off += image_len
    if off != len(raw):
        raise MciError(f"{len(raw) - off} unexpected bytes after the metadata")
    return meta, image


def _write_meta(meta, image: bytes) -> bytes:
    out = bytearray()

    def key(k):
        b = k.encode("ascii") + b"\x00"
        out.extend(struct.pack("<I", len(b)) + b)

    for section in SECTION_ORDER:
        items = sorted((k, v) for k, (s, v) in meta.items() if s == section)
        out.extend(struct.pack("<I", len(items)))
        for k, v in items:
            key(k)
            if section == I32:
                out.extend(struct.pack("<i", v))
            elif section == I32_ARRAY:
                out.extend(struct.pack(f"<I{len(v)}i", len(v), *v))
            elif section == F32:
                out.extend(struct.pack("<f", v))
            elif section == F32_ARRAY:
                out.extend(struct.pack(f"<I{len(v)}f", len(v), *v))
            elif section == U8:
                out.append(v)
            elif section == STRING:
                b = v.encode("latin-1") + b"\x00"
                out.extend(struct.pack("<I", len(b)) + b)
    out.extend(struct.pack("<I", len(image)) + image)
    return bytes(out)


def read_mci(path_or_bytes) -> MciFile:
    d = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, "rb").read()
    if d[:14] != MAGIC:
        raise MciError("not a Mimic file (bad magic)")
    version, meta_len = struct.unpack_from("<II", d, 14)
    if version != VERSION:
        raise MciError(f"unsupported container version {version}")
    tail = d[-23:]
    if tail[:7] != TRAILER_TAG:
        raise MciError("missing MMKCSM trailer")
    if hashlib.md5(d[:-23]).digest() != tail[7:]:
        raise MciError("MD5 checksum mismatch")
    z = zlib.decompressobj()
    raw = z.decompress(d[22:-23])
    if len(raw) != meta_len:
        raise MciError("metadata length mismatch")
    base = len(d) - 23 - len(z.unused_data)
    meta, image = _read_meta(raw)
    head_len = sum(meta["INST0pool0desc"][1]) * 4
    return MciFile(meta=meta, image=image, heads=d[base:base + head_len],
                   data=d[base + head_len:-23], version=version)


def read_split(bin_path_or_bytes, drd_path_or_bytes) -> MciFile:
    """Read a factory instrument stored as a .bin (metadata) + .drd (audio) pair.

    The .bin is the same header and zlib metadata as a .mci, with no audio and
    no MMKCSM trailer. The .drd is the RAM heads followed by the compressed
    data, with offsets counted from its first byte. Factory libraries carry a
    checksum.dat with the MD5 of every whole file instead of a trailer.
    """
    def load(x):
        return x if isinstance(x, (bytes, bytearray)) else open(x, "rb").read()
    d, drd = load(bin_path_or_bytes), load(drd_path_or_bytes)
    if d[:14] != MAGIC:
        raise MciError("not a Mimic file (bad magic)")
    version, meta_len = struct.unpack_from("<II", d, 14)
    raw = zlib.decompressobj().decompress(d[22:])
    if len(raw) != meta_len:
        raise MciError("metadata length mismatch")
    meta, image = _read_meta(raw)
    head_len = sum(meta["INST0pool0desc"][1]) * 4
    return MciFile(meta=meta, image=image, heads=drd[:head_len], data=drd[head_len:],
                   version=version)


def channels(f: MciFile, index: int) -> list[list[int]]:
    """Decode one pool sample and split its interleaved channels."""
    x = sample(f, index)
    n = f.get("INST0pool0nchn")[index]
    return [x[c::n] for c in range(n)]


def write_mci(f: MciFile) -> bytes:
    raw = _write_meta(f.meta, f.image)
    body = (MAGIC + struct.pack("<II", f.version, len(raw)) + zlib.compress(raw, 1)
            + f.heads + f.data)
    return body + TRAILER_TAG + hashlib.md5(body).digest()


# ---------------------------------------------------------------------------
# Audio codec
# ---------------------------------------------------------------------------

def _bits_for(delta: int) -> int:
    """Width the Editor uses for a delta: |delta| < 2**(bits-1)."""
    return max(1, abs(delta).bit_length() + 1)


def decode_blocks(blob: bytes) -> list[int]:
    out = []
    for off in range(0, len(blob), BLOCK_SIZE):
        bits, low, high, n = struct.unpack_from("<BBhI", blob, off)
        payload = blob[off + BLOCK_HEADER:off + BLOCK_SIZE]
        # Deltas are packed most-significant bit first into little-endian u32 words.
        swapped = b"".join(payload[i:i + 4][::-1] for i in range(0, len(payload), 4))
        v = int.from_bytes(swapped, "big")
        mask, half = (1 << bits) - 1, 1 << (bits - 1)
        x = (high << 8) | low
        out.append(x)
        for k in range(n - 1):
            dlt = (v >> (PAYLOAD_BITS - (k + 1) * bits)) & mask
            if dlt >= half:
                dlt -= 1 << bits
            x += dlt
            out.append(x)
    return out


def encode_blocks(x: list[int]) -> tuple[bytes, list[int]]:
    """Compress samples on the 24-bit scale.

    Returns the blob and the frame count of every block. A block of n frames
    stores n deltas; the last one bridges to the next block's first frame.
    """
    out = bytearray()
    frames = []
    p, total = 0, len(x)
    need = [_bits_for(x[k + 1] - x[k]) for k in range(total - 1)]
    while p < total:
        for bits in range(1, 33):
            n = min(PAYLOAD_BITS // bits, total - p)
            if max(need[p:p + n], default=1) <= bits:
                break
        else:
            raise MciError("delta does not fit in 32 bits")
        deltas = [x[p + k + 1] - x[p + k] for k in range(n) if p + k + 1 < total]
        v = 0
        for dlt in deltas:
            v = (v << bits) | (dlt & ((1 << bits) - 1))
        v <<= PAYLOAD_BITS - len(deltas) * bits
        be = v.to_bytes(PAYLOAD_BITS // 8, "big")
        payload = b"".join(be[i:i + 4][::-1] for i in range(0, len(be), 4))
        out += struct.pack("<BBhI", bits, x[p] & 0xFF, x[p] >> 8, n) + payload
        frames.append(n)
        p += n
    return bytes(out), frames


def head_layout(block_frames: list[int]) -> tuple[int, int]:
    """(nobu, desc): blocks preloaded into RAM and the head length in frames."""
    cum = nobu = 0
    while (nobu < len(block_frames) and nobu < HEAD_MAX_BLOCKS
           and HEAD_PAD + 1 + cum + block_frames[nobu] <= HEAD_MAX_FRAMES):
        cum += block_frames[nobu]
        nobu += 1
    if nobu < len(block_frames):
        return nobu, HEAD_PAD + cum + 1   # includes the next block's first frame
    return nobu, HEAD_PAD + cum


def encode_head(samples: list[int], desc: int) -> bytes:
    frames = [0] * HEAD_PAD + samples[:desc - HEAD_PAD]
    return struct.pack(f"<{len(frames)}f", *(s / 2 ** 23 for s in frames))


def rms_envelope(samples: list[int]) -> list[int]:
    """Loudness envelope, one value per 1024 frames, on the 24-bit scale.

    The Editor's exact formula is not known. A one-pole follower of |x| with a
    1024-frame time constant, read at the end of each window and scaled by 1.04,
    lands within about 5-10% of its values on every Editor file we have (a plain
    1024-frame RMS was 12-30% off).
    """
    a = math.exp(-1 / 1024)
    out, e = [], 0.0
    for i in range((len(samples) // 1024) * 1024):
        e = a * e + (1 - a) * abs(samples[i])
        if i % 1024 == 1023:
            out.append(int(e * 1.04))
    return out


def sample(f: MciFile, index: int) -> list[int]:
    """Decode one pool sample."""
    ofs = f.get("INST0pool0dofs")[index] - len(f.heads)
    return decode_blocks(f.data[ofs:ofs + f.get("INST0pool0dlen")[index]])


# ---------------------------------------------------------------------------
# Building instruments
# ---------------------------------------------------------------------------

@dataclass
class Articulation:
    name: str                       # e.g. "Bow", "Snare Rimshot"
    zone_type: int | None           # see ZONE_TYPES; None = only reached through a controller zone
    layers: list[list[list[int]]]   # layers soft->hard, each a list of round robins
    min_velocities: list[int] | None = None   # None = computed from loudness
    release_bottom: tuple[float, float] = (0.3, 0.5)   # (minrtb, minrtt)
    release_top: tuple[float, float] = (1.5, 3.0)      # (maxrtb, maxrtt)
    choke: bool = False
    artid: int | None = None        # None = the articulation's index
    noteon: int = 0                 # MIDI note; factory hi-hats set it, the Editor writes 0
    mute: bool | None = None        # None = same as choke


# Zone type ids seen in Editor files.
ZONE_TYPES = {
    "kick": 1,
    "drum center": 1, "drum rimclick": 2, "drum rimshot": 3,
    "snare sidestick": 4, "snare rimshot side": 5, "snare side": 6,
    "ride bow": 40, "ride choke": 41, "ride bell": 42, "ride edge": 43,   # also 2-zone cymbals
    "perc main": 200, "perc secondary": 201, "perc additional 1": 202, "perc additional 2": 203,
}


# (micpos, mict) the Editor writes per instrument type; anything else gets (0, 0).
MIC_DEFAULTS = {"Kick": (2, 0), "Ride": (5, 1), "Cymbal": (5, 1), "Hat": (0, 0)}


def _cymbal_zones(zones, choke, release_bottom, release_top) -> list[Articulation]:
    arts = [Articulation(name, ZONE_TYPES[key], layers, None, release_bottom, release_top)
            for name, key, layers in zones]
    if choke:
        arts.append(Articulation("Choke Ring", ZONE_TYPES["ride choke"], choke, None,
                                 release_bottom, release_top, choke=True))
    else:
        arts.append(Articulation("Choke Ring", ZONE_TYPES["ride choke"], [], None,
                                 (1.0, 1.0), (1.0, 1.0), choke=True))
    return arts


def ride_articulations(bow, bell, edge, choke=None, release_bottom=(0.5, 0.5),
                       release_top=(3.0, 5.0)) -> list[Articulation]:
    """Bow/Bell/Edge/Choke Ring in the order the Editor saves a ride (type "Ride").

    This is also how a 3-zone crash is built: the Mimic lists it in the ride menu.
    Each zone is a list of layers (soft->hard), each a list of round-robin samples.
    `choke` is optional; the Editor leaves the Choke Ring empty unless given a
    sample, and then uses the same release times as the other zones.
    """
    return _cymbal_zones([("Bow", "ride bow", bow), ("Bell", "ride bell", bell),
                          ("Edge", "ride edge", edge)], choke, release_bottom, release_top)


def cymbal_articulations(edge, bow, choke=None, release_bottom=(0.5, 0.5),
                         release_top=(3.0, 5.0)) -> list[Articulation]:
    """Edge/Bow/Choke Ring for a 2-zone crash or splash (type "Cymbal").

    The Mimic lists these in its crash menu. Same zone ids as a ride, minus the
    bell, with the edge first.
    """
    return _cymbal_zones([("Edge", "ride edge", edge), ("Bow", "ride bow", bow)],
                         choke, release_bottom, release_top)


# Hi-hat zone ids, from Pearl's factory hi-hats. The struck zones are
# controller zones: the pedal position picks one of five articulations.
HAT_ZONES = {"edge": 80, "tip": 81, "shank": 82, "pedal": 83, "footsplash": 84}
HAT_OPENNESS = ["Closed", "1/4 Open", "2/4 Open", "3/4 Open", "Open"]
# Release ranges per openness step: ((minrtb, minrtt), (maxrtb, maxrtt)).
_HAT_SHORT = ((0.5, 0.3), (1.0, 1.5))
_HAT_RELEASE = [_HAT_SHORT, _HAT_SHORT, _HAT_SHORT, ((0.5, 0.3), (2.0, 3.0)), ((0.5, 0.3), (2.0, 4.0))]


def hat_articulations(tip, shank, edge, pedal, footsplash, transition=None
                      ) -> tuple[list[Articulation], list[tuple[int, list[int]]]]:
    """A hi-hat in the factory layout (type "Hat").

    tip, shank and edge are lists of five zones, closed -> open, and every zone
    is a list of layers (soft->hard) of round-robin samples. Returns the
    articulations and the controller zones to pass to build_instrument().
    Artids and MIDI notes follow the factory files.
    """
    arts: list[Articulation] = []
    ctrl = []
    groups = [("Tip", "tip", tip, 5, [42, 61, 62, 63, 64]),
              ("Shank", "shank", shank, 0, [22, 66, 67, 68, 69]),
              ("", "edge", edge, 12, [108, 109, 110, 111, 112])]
    for label, key, steps, first_id, notes in groups:
        if len(steps) != len(HAT_OPENNESS):
            raise MciError(f"hi-hat {key}: expected {len(HAT_OPENNESS)} openness steps")
        ids = []
        for k, layers in enumerate(steps):
            name = " ".join(w for w in ["Hi-Hat", label, HAT_OPENNESS[k]] if w)
            bottom, top = _HAT_RELEASE[k]
            arts.append(Articulation(name, None, layers, None, bottom, top,
                                     artid=first_id + k, noteon=notes[k], mute=k < 2))
            ids.append(first_id + k)
        ctrl.append((HAT_ZONES[key], ids))
    arts.append(Articulation("Hi-Hat Pedal", HAT_ZONES["pedal"], pedal, None, *_HAT_SHORT,
                             artid=10, noteon=44, mute=True))
    arts.append(Articulation("Hi-Hat Footsplash", HAT_ZONES["footsplash"], footsplash, None,
                             (0.5, 0.3), (2.0, 4.0), artid=11, noteon=70, mute=True))
    if transition:
        arts.append(Articulation("Hi-Hat Transition", None, transition, None,
                                 (0.5, 0.3), (2.0, 3.0), artid=17, noteon=23))
    arts.sort(key=lambda a: a.artid)
    return arts, ctrl


def make_choke(samples: list[float], rate: int = 48000, length: float = 0.5187,
               hold: float = 0.0446, start_gain: float = 1.3862,
               end: float = 0.4685, end_gain: float = 0.2520) -> list[float]:
    """Turn an open cymbal hit into a choke (grabbed cymbal) sample.

    Follows Alexandre's Audacity template: keep the first ~0.52 s, boost by
    start_gain until `hold`, then fade in dB to end_gain at `end`. Pass the
    hardest edge hit. Times are in seconds.
    """
    n = min(len(samples), int(round(length * rate)))
    g0, g1 = math.log(start_gain), math.log(end_gain)
    out = []
    for i in range(n):
        t = i / rate
        u = min(max((t - hold) / (end - hold), 0.0), 1.0)
        out.append(samples[i] * math.exp(g0 + (g1 - g0) * u))
    return out


def quantize(samples) -> list[int]:
    """Floats in [-1, 1] -> 16-bit grid on the 24-bit scale, as the Editor stores them."""
    return [max(-32768, min(32767, round(s * 32768))) << 8 for s in samples]


def velocity_thresholds(loudness: list[float]) -> list[int]:
    """Map per-layer loudness (linear, soft->hard) to minimum MIDI velocities.

    Our own mapping: velocity = 127 * sqrt(level / loudest level), which tracks
    the Editor's thresholds closely on the ride sample. The Editor's exact curve
    is not known. Thresholds always rise and stay within 1..127.
    """
    n = len(loudness)
    top = max(loudness, default=0.0) or 1.0   # a zone with no layers (empty Choke Ring) is fine
    out = [0] + [int(round(127 * math.sqrt(max(level, 0.0) / top))) for level in loudness[1:]]
    for i in range(1, n):                 # strictly rising...
        out[i] = max(out[i], out[i - 1] + 1)
    for i in range(n - 1, 0, -1):         # ...and room left for every layer above
        out[i] = min(out[i], 127 - (n - 1 - i))
        if i < n - 1:
            out[i] = min(out[i], out[i + 1] - 1)
    return out


def build_instrument(name: str, instrument_type: str, articulations: list[Articulation],
                     image: bytes, library: str = "", mic: str = "",
                     sample_volume: float = 0.52,
                     controller_zones: list[tuple[int, list[int]]] = ()) -> MciFile:
    """Build a complete instrument.

    controller_zones: (zone type, artids closed->open) for hi-hat zones whose
    articulation is picked by the pedal position; see hat_articulations().
    """
    m: dict[str, tuple[int, object]] = {}

    def put(k, s, v):
        m[k] = (s, v)

    pool: list[list[int]] = []
    pool_rr: list[int] = []   # round-robin position of every pool entry
    zones = []
    for a, art in enumerate(articulations):
        P = f"INST0artic{a}"
        artid = a if art.artid is None else art.artid
        put(P + "artid", I32, artid)
        put(P + "noteon", I32, art.noteon)
        put(P + "numlay", I32, len(art.layers))
        put(P + "vol", F32, 1.0)
        put(P + "artn", STRING, art.name)
        put(P + "chk", U8, int(art.choke))
        put(P + "fak", U8, 0)
        put(P + "mut", U8, int(art.choke if art.mute is None else art.mute))
        put(P + "minrtb", F32_ARRAY, [art.release_bottom[0]] + [1.0] * (RT_ARRAY_LEN - 1))
        put(P + "minrtt", F32_ARRAY, [art.release_bottom[1]] + [1.0] * (RT_ARRAY_LEN - 1))
        put(P + "maxrtb", F32_ARRAY, [art.release_top[0]] + [1.0] * (RT_ARRAY_LEN - 1))
        put(P + "maxrtt", F32_ARRAY, [art.release_top[1]] + [1.0] * (RT_ARRAY_LEN - 1))
        if art.zone_type is not None:
            zones.append((art.zone_type, artid))

        loud = []
        for layer in art.layers:
            peaks = [max(rms_envelope(s) or [0]) for s in layer]
            loud.append(sum(peaks) / len(peaks))
        minvel = art.min_velocities or velocity_thresholds(loud)
        nl = len(art.layers)
        for l, layer in enumerate(art.layers):
            if len(layer) > MAX_ROUND_ROBINS:
                raise MciError(f"{art.name} layer {l}: more than {MAX_ROUND_ROBINS} round robins")
            L = f"{P}veloLay{l}"
            idx = list(range(len(pool), len(pool) + len(layer)))
            pool.extend(layer)
            pool_rr.extend(range(len(layer)))
            put(L + "nums", I32, len(layer))
            put(L + "smpidx", I32_ARRAY, idx + [-1] * (MAX_ROUND_ROBINS - len(idx)))
            put(L + "minvel", I32, minvel[l])
            put(L + "minvol", F32, 0.0 if l == 0 else 0.95)
            put(L + "maxvol", F32, 1.0)
            t = l / (nl - 1) if nl > 1 else 0.0   # a single layer takes the bottom values
            lo = art.release_bottom[0] + t * (art.release_bottom[1] - art.release_bottom[0])
            hi = art.release_top[0] + t * (art.release_top[1] - art.release_top[0])
            put(L + "minrtl", F32_ARRAY, [lo] + [1.0] * (RT_ARRAY_LEN - 1))
            put(L + "maxrtl", F32_ARRAY, [hi] + [1.0] * (RT_ARRAY_LEN - 1))

    # Pool: compress every sample, then lay out the RAM heads.
    blobs, layouts = [], []
    for s in pool:
        blob, frames = encode_blocks(s)
        blobs.append(blob)
        layouts.append(head_layout(frames))
    head_order = sorted(range(len(pool)), key=lambda i: (pool_rr[i], i))
    dedo = [0] * len(pool)
    heads = bytearray()
    for i in head_order:
        dedo[i] = len(heads)
        heads += encode_head(pool[i], layouts[i][1])
    dofs, pos = [], len(heads)
    for b in blobs:
        dofs.append(pos)
        pos += len(b)

    Q = "INST0pool0"
    put(Q + "psz", I32, len(pool))
    put(Q + "dofs", I32_ARRAY, dofs)
    put(Q + "dlen", I32_ARRAY, [len(b) for b in blobs])
    put(Q + "fcnt", I32_ARRAY, [len(s) for s in pool])
    put(Q + "nchn", I32_ARRAY, [1] * len(pool))
    put(Q + "dedo", I32_ARRAY, dedo)
    put(Q + "desc", I32_ARRAY, [l[1] for l in layouts])
    put(Q + "nobu", I32_ARRAY, [l[0] for l in layouts])
    put(Q + "svol", F32_ARRAY, [sample_volume] * len(pool))
    for i, s in enumerate(pool):
        env = rms_envelope(s) or [0]
        put(f"{Q}rmsenv{i}rmse", I32_ARRAY, env)
        put(f"{Q}rmsenv{i}rmsl", I32, len(env))
        put(f"{Q}rmsenv{i}mrms", F32, float(max(env)))

    for z, (zone_type, artid) in enumerate(zones):
        put(f"INST0zoneArtId{z}", I32, artid)
        put(f"INST0zoneType{z}", I32, zone_type)
    for c, (zone_type, artids) in enumerate(controller_zones):
        put(f"INST0hctrlzn{c}ctrzone", I32, zone_type)
        put(f"INST0hctrlzn{c}stznsz", I32, len(artids))
        put(f"INST0hctrlzn{c}trzone", I32_ARRAY, list(artids))

    # Instrument-wide values, as written by the Editor.
    for k, v in {"INST0chkn": 0, "INST0ecov": 0, "INST0insv": 1, "INST0micnt": 1,
                 "INST0numart": len(articulations), "INST0nznctrl": len(controller_zones),
                 "INST0zonecnt": len(zones),
                 "INST0micinf0micpos": MIC_DEFAULTS.get(instrument_type, (0, 0))[0],
                 "INST0micinf0mict": MIC_DEFAULTS.get(instrument_type, (0, 0))[1]}.items():
        put(k, I32, v)
    for k, v in {"att": 0.0, "att2": 0.0, "dmp": 0.0, "dmp2": 0.0, "micp": 0.5, "micpz": 1.0,
                 "micrs": 0.0, "mictn": 0.5, "micv": 1.0, "rel": 1.0, "rel2": 1.0,
                 "sus": 1.0, "sus2": 1.0}.items():
        put("INST0micinf0" + k, F32, v)
    for k, v in {"INST0dav2": 1, "INST0micinf0enven": 1, "INST0micinf0isst": 0,
                 "INST0micinf0micen": 1, "INST0micinf0micm": 0, "INST0tmr": 1,
                 "INSTedtr": 1, "INSTmnlth": 1}.items():
        put(k, U8, v)
    for k, v in {"INST0insdat": "", "INST0insimj": "", "INST0insnam": name,
                 "INST0instyp": instrument_type, "INST0libnam": library,
                 "INST0micinf0micn": mic or instrument_type}.items():
        put(k, STRING, v)

    return MciFile(meta=m, image=image, heads=bytes(heads), data=b"".join(blobs))
