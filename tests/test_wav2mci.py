"""wav2mci on a tiny synthetic render, laid out like SDSE's output."""
import math
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "mci"))
import mci  # noqa: E402
import wav2mci  # noqa: E402


def write_hit(path, velocity, width=3):
    n = 4096
    amp = velocity / 127 * 0.5
    vals = [round(amp * math.sin(i / 7) * math.exp(-i / 900) * 2 ** (8 * width - 1)) for i in range(n)]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(width)
        w.setframerate(44100)
        w.writeframes(b"".join(v.to_bytes(width, "little", signed=True) for v in vals))
    return vals


def render(tmp_path, zones, velocities, piece="14 Test Crash_AB12", family="CRASH1"):
    for zone in zones:
        d = tmp_path / "14 Test Crash" / f"{piece} - {family} {zone}"
        d.mkdir(parents=True)
        for i, v in enumerate(velocities, 1):
            write_hit(d / f"{piece} - {family} {zone} - {i:03d}v{v}.wav", v)


def test_two_zone_crash_becomes_a_cymbal(tmp_path):
    render(tmp_path, ["BOW", "EDGE"], range(127, 0, -3))
    assert wav2mci.main([str(tmp_path / "14 Test Crash"), "--out", str(tmp_path)]) == 0
    f = mci.read_mci(str(tmp_path / "14 Test Crash.mci"))
    assert f.get("INST0instyp") == "Cymbal"
    assert [f.get(f"INST0artic{a}artn") for a in range(3)] == ["Edge", "Bow", "Choke Ring"]
    assert f.get("INST0artic0numlay") == 20
    vel = [f.get(f"INST0artic0veloLay{l}minvel") for l in range(20)]
    assert vel[0] == 0 and vel == sorted(vel) and len(set(vel)) == 20
    assert f.get("INST0artic2numlay") == 1          # generated choke


def test_bell_makes_a_three_zone_ride_and_keeps_24_bit(tmp_path):
    render(tmp_path, ["BOW", "EDGE", "BELL"], [20, 60, 100, 127])
    wav2mci.main([str(tmp_path / "14 Test Crash"), "--out", str(tmp_path), "--no-choke"])
    f = mci.read_mci(str(tmp_path / "14 Test Crash.mci"))
    assert f.get("INST0instyp") == "Ride"
    assert [f.get(f"INST0zoneType{a}") for a in range(4)] == [40, 42, 43, 41]
    assert f.get("INST0artic3numlay") == 0
    want = write_hit(tmp_path / "check.wav", 127)
    idx = f.get("INST0artic0veloLay3smpidx")[0]
    assert mci.sample(f, idx) == want


def test_split_layers_spreads_hits():
    hits = [wav2mci.Hit(v, "") for v in range(1, 101)]
    layers = wav2mci.split_layers(hits)
    assert len(layers) == 20 and all(len(layer) == 5 for layer in layers)
    assert wav2mci.min_velocities(layers)[:3] == [0, 6, 11]
