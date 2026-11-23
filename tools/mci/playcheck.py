"""Check that the Mimic plays eMIMICmkr's instruments frame for frame.

Two steps, with a Mimic running Mimic OS 2 (mimic-rec records its outputs):

    python3 tools/mci/playcheck.py make DIR
        Writes "Playback Check.mci" into DIR: 3 s of noise at -6 dB, built by
        eMIMICmkr's own converter. Copy it to the Mimic (Send to Mimic, or
        ~/Mimic/Instruments), put it in a kit's Kick slot, and turn that slot's
        effects off.

    python3 tools/mci/playcheck.py check "Playback Check.mci" TAKE.wav
        Hit the pad a few times, a second or more apart, while HOME > Record
        records the output the Kick goes to; TAKE.wav is that output's file.
        For every hit it lines the recording up with what eMIMICmkr encoded
        and reports any frame the Mimic dropped or played twice, and where.

The first frames of every sample come from a RAM head (eMIMICmkr writes them
as floats), the rest is streamed from the compressed blocks, so the place to
watch is where the head ends: a frame played twice or skipped there means the
Mimic reads a block's header value differently from eMIMICmkr (the open
question in docs/mci-format.md). Any gain (velocity, faders) is fine; effects,
EQ or a tuned pitch on the slot make the check fail.
"""
from __future__ import annotations

import argparse
import os
import random
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mci  # noqa: E402
import wav2mci  # noqa: E402

NAME = "Playback Check"
SECONDS = 3
CHUNK = 512          # frames per stretch lined up on its own
MAX_SLIP = 4         # frames a stretch may be off before it is called lost
SILENCE = 2205       # quiet frames (50 ms) that end a hit
MATCH = 0.02         # largest residual left, against the hit's own level, to call a stretch matched


def make(out_dir: str) -> str:
    rng = random.Random(1)
    amp = 2 ** 22   # -6 dB
    x = [round(rng.uniform(-amp, amp)) for _ in range(SECONDS * wav2mci.SAMPLE_RATE)]
    with tempfile.TemporaryDirectory() as tmp:
        piece = f"{NAME}_PC1"
        d = os.path.join(tmp, NAME, f"{piece} - KICK1")
        os.makedirs(d)
        with wave.open(os.path.join(d, f"{piece} - KICK1 - 001v127.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(3)
            w.setframerate(wav2mci.SAMPLE_RATE)
            w.writeframes(b"".join(v.to_bytes(3, "little", signed=True) for v in x))
        if wav2mci.main([os.path.join(tmp, NAME), "--out", out_dir, "--name", NAME, "--full-length"]) != 0:
            raise SystemExit("eMIMICmkr's converter failed")
    return os.path.join(out_dir, NAME + ".mci")


def read_take(path: str) -> list[int]:
    with open(path, "rb") as f:
        return wav2mci.read_wav(f.read())


def hits(y: list[int]) -> list[int]:
    """Where each hit starts: the first loud frame after SILENCE quiet ones."""
    peak = max((abs(v) for v in y), default=0)
    level = peak * 0.05
    out, quiet = [], SILENCE
    for i, v in enumerate(y):
        if abs(v) > level:
            if quiet >= SILENCE:
                out.append(i)
            quiet = 0
        else:
            quiet += 1
    return out


def fit(y, x, at, start, n):
    """(gain, residual share) of y[at + k] against gain * x[start + k] over n frames."""
    xy = xx = yy = 0
    for k in range(n):
        a, b = x[start + k], y[at + k] if 0 <= at + k < len(y) else 0
        xy += a * b
        xx += a * a
        yy += b * b
    if xx == 0 or yy == 0:
        return 0.0, 1.0
    g = xy / xx
    return g, max(0.0, yy - g * xy) / yy


def err(y, x, at, gain, j, slip):
    i = at + j + slip
    return (y[i] if 0 <= i < len(y) else 0) - gain * x[j]


def check_hit(y, x, onset):
    """(where the sample's frame 0 is in y, gain, steps, lost).

    steps: [(frame, slip)]: around that frame the sample goes on slip frames later (+1: the
    frame was played twice) or earlier (-1: it was skipped). lost:
    [(first, last)] stretches that match the sample at no slip.
    """
    # The sample starts with noise, so its first frame is near the onset.
    at = onset + min((fit(y, x, onset + s, 0, CHUNK)[1], s) for s in range(-64, 65))[1]
    gain = fit(y, x, at, 0, CHUNK)[0]
    chunks = []    # (first frame, slip or None)
    for start in range(0, len(x) - CHUNK + 1, CHUNK):
        res, slip = min((fit(y, x, at + start + s, start, CHUNK)[1], s) for s in range(-MAX_SLIP, MAX_SLIP + 1))
        chunks.append((start, slip if res < MATCH else None))
    steps, lost, last = [], [], None   # last: (first frame, slip) of the last matched chunk
    for i, (start, slip) in enumerate(chunks):
        if slip is None:
            continue
        if last is not None and slip != last[1]:
            # The frame the slip changes at: the split that leaves the least error either side.
            a, b = last[0], start + CHUNK
            before = [err(y, x, at, gain, j, last[1]) ** 2 for j in range(a, b)]
            after = [err(y, x, at, gain, j, slip) ** 2 for j in range(a, b)]
            total, best, k0 = sum(after), None, a
            for k in range(a, b):
                if best is None or total < best:
                    best, k0 = total, k
                total += before[k - a] - after[k - a]
            steps.append((k0 if slip > last[1] else k0 - 1, slip - last[1]))
        elif last is not None and start > last[0] + CHUNK:
            lost.append((last[0] + CHUNK, start - 1))
        last = (start, slip)
    if last is None:
        lost.append((0, len(x) - 1))
    elif last[0] + CHUNK < len(chunks) * CHUNK:
        lost.append((last[0] + CHUNK, len(x) - 1))
    return at, gain, steps, lost


def first_frames(y, x, at, gain, n=4):
    """The sample's first n frames as the Mimic played them, as a share of what eMIMICmkr wrote."""
    return [round(y[at + k] / (gain * x[k]), 2) if x[k] and 0 <= at + k < len(y) else None for k in range(n)]


def check(mci_path: str, take_path: str) -> int:
    f = mci.read_mci(mci_path)
    x = mci.sample(f, 0)
    head = f.get("INST0pool0desc")[0] - mci.HEAD_PAD   # frames played from the RAM head
    y = read_take(take_path)
    onsets = hits(y)
    if not onsets:
        print("no hits in the recording")
        return 1
    print(f"{len(onsets)} hits; the RAM head holds frames 0-{head - 1}, the blocks stream on from there")
    bad = 0
    for i, onset in enumerate(onsets, 1):
        at, gain, steps, lost = check_hit(y, x, onset)
        line = f"hit {i} at {onset / wav2mci.SAMPLE_RATE:.3f} s"
        if gain <= 0 or lost == [(0, len(x) - 1)]:
            print(line + ": does not match the sample at all (effects, EQ or pitch on the slot?)")
            bad += 1
            continue
        line += f", gain {gain:.2f}, first frames {first_frames(y, x, at, gain)}"
        if not steps and not lost:
            print(line + f": all {len(x)} frames in step")
            continue
        bad += 1
        print(line + ":")
        for frame, slip in steps:
            n = f"{abs(slip)} frame{'s' if abs(slip) > 1 else ''}"
            what = f"{n} played twice" if slip > 0 else f"{n} skipped"
            where = ", where the head ends" if head - 2 <= frame <= head + 1 else ""
            print(f"  near frame {frame}: {what}{where}")
        for a, b in lost:
            print(f"  frames {a}-{b}: not found (cut short, or changed?)")
    print("the Mimic plays it as eMIMICmkr wrote it" if not bad else f"{bad} of {len(onsets)} hits differ")
    return 1 if bad else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make", help=f'write "{NAME}.mci" into DIR')
    m.add_argument("dir")
    c = sub.add_parser("check", help="compare a mimic-rec take with the instrument")
    c.add_argument("mci")
    c.add_argument("take")
    args = ap.parse_args(argv)
    if args.cmd == "make":
        os.makedirs(args.dir, exist_ok=True)
        print("wrote", make(args.dir))
        return 0
    return check(args.mci, args.take)


if __name__ == "__main__":
    sys.exit(main())
