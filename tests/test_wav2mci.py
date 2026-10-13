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
    assert f.get("INST0artic0numlay") == 11          # 43 hits, about 4 round robins each
    vel = [f.get(f"INST0artic0veloLay{l}minvel") for l in range(11)]
    assert vel[0] == 0 and vel == sorted(vel) and len(set(vel)) == 11
    assert f.get("INST0artic2numlay") == 1          # generated choke


def test_bell_makes_a_three_zone_ride_and_keeps_24_bit(tmp_path):
    render(tmp_path, ["BOW", "EDGE", "BELL"], [20, 60, 100, 127])
    wav2mci.main([str(tmp_path / "14 Test Crash"), "--out", str(tmp_path), "--no-choke"])
    f = mci.read_mci(str(tmp_path / "14 Test Crash.mci"))
    assert f.get("INST0instyp") == "Ride"
    assert [f.get(f"INST0zoneType{a}") for a in range(4)] == [40, 42, 43, 41]
    assert f.get("INST0artic3numlay") == 0
    assert f.get("INST0artic0numlay") == 1           # 4 hits: one layer of 4 round robins
    want = write_hit(tmp_path / "check.wav", 127)
    idx = f.get("INST0artic0veloLay0smpidx")[3]
    left, right = mci.channels(f, idx)              # rides are stored in stereo
    assert left == right == want


def test_split_layers_spreads_hits():
    hits = [wav2mci.Hit(v, "") for v in range(1, 101)]
    layers = wav2mci.split_layers(hits)
    assert len(layers) == 20 and all(len(layer) == 5 for layer in layers)
    assert wav2mci.min_velocities(layers)[:3] == [0, 6, 11]


def test_round_robins_per_layer():
    def layers(n, family, zone):
        hits = [wav2mci.Hit(1 + i * 126 // max(1, n - 1), str(i)) for i in range(n)]
        return [len(layer) for layer in wav2mci.round_robin_layers(hits, wav2mci.round_robins(family, zone))]
    assert layers(64, "kick", "kick") == [4] * 16
    assert len(layers(100, "snare", "center")) == 17 and max(layers(100, "snare", "center")) == 6
    assert layers(300, "cymbal", "edge") == [6] * 20          # 120 of the 300 kept
    assert layers(3, "snare", "sidestick") == [3]
    assert layers(1, "kick", "kick") == [1]
    assert [wav2mci.round_robins(f, z) for f, z in [("snare", "rimshot"), ("tom", "center"), ("tom", "rimshot"),
                                                      ("tom", "rimclick"), ("snare", "rimclick"), ("cymbal", "bell")]] \
        == [6, 5, 5, 4, 4, 4]
    # the thinning keeps the softest and the hardest hit
    hits = [wav2mci.Hit(v, "") for v in range(1, 128)] * 3
    kept = [h for layer in wav2mci.round_robin_layers(hits, 4) for h in layer]
    assert len(kept) == 120 and kept[0].velocity == 1 and kept[-1].velocity == 127


HAT_FOLDERS = ["BELL CLOSED", "BELL OPEN1", "BELL OPEN2", "BELL OPEN4", "BELL OPEN5", "BELL OPEN6",
               "BOW CLOSED2", "BOW OPEN1", "BOW OPEN2", "BOW OPEN3", "BOW OPEN4", "BOW OPEN5",
               "BOW OPEN6", "BOW TIGHT", "EDGE CLOSED", "EDGE OPEN0", "EDGE OPEN1", "EDGE OPEN2",
               "EDGE OPEN3", "EDGE OPEN4", "EDGE TIGHT", "FOOT SPLASH", "PEDAL CLOSED"]


def test_sdse_hi_hat_folders_become_a_hat_and_a_3_zone_ride(tmp_path):
    render(tmp_path, HAT_FOLDERS, [30, 90, 127], piece="14 Z K Cust Session_XY1", family="HATS")
    assert wav2mci.main([str(tmp_path / "14 Test Crash"), "--out", str(tmp_path),
                         "--name", "14 Test Hat", "--hat-as-ride"]) == 0
    f = mci.read_mci(str(tmp_path / "14 Test Hat.mci"))
    assert f.get("INST0instyp") == "Hat"
    assert f.get("INST0numart") == 17 and f.get("INST0nznctrl") == 3
    assert [f.get(f"INST0zoneType{z}") for z in range(2)] == [83, 84]
    r = mci.read_mci(str(tmp_path / "14 Test Hat 3-zone.mci"))
    assert r.get("INST0instyp") == "Ride"
    assert [r.get(f"INST0artic{a}artn") for a in range(3)] == ["Bow", "Bell", "Edge"]


def test_openness_steps_run_closed_to_open():
    assert wav2mci.pick_openness(["tight", "closed2", "open1", "open2", "open3", "open4", "open5", "open6"]) \
        == ["closed2", "open2", "open3", "open4", "open6"]
    assert wav2mci.pick_openness(["closed", "open1"]) == ["closed", "open1", "open1", "open1", "open1"]


def test_share_cells():
    assert wav2mci.share_cells(199, {"edge": 127, "bow": 127}) == {"edge": 100, "bow": 99}
    assert wav2mci.share_cells(300, {"center": 127, "rimshot": 127, "sidestick": 40}) == \
        {"sidestick": 40, "center": 127, "rimshot": 127}
    assert wav2mci.share_cells(300, {"center": 200, "rimshot": 200, "sidestick": 40}) == \
        {"sidestick": 40, "center": 130, "rimshot": 130}
    assert wav2mci.share_cells(64, {"kick": 20}) == {"kick": 20}


def test_shallow_rimshots_go_to_the_rimshot_side_zone():
    assert wav2mci.classify("SNARE1 RIMSHOT SHALLOW") == ("snare", "rimshotside")
    assert wav2mci.classify("SNARE1 RIMSHOT SIDE") == ("snare", "rimshotside")
    assert wav2mci.classify("SNARE1 RIMSHOT") == ("snare", "rimshot")
    assert wav2mci.classify("TOM1 RIMSHOT SHALLOW") == ("tom", "rimshot")


def test_missing_side_rimshot_is_blended(tmp_path):
    render(tmp_path, ["CENTER", "RIMSHOT", "RIMCLICK"], [127, 64, 20], piece="14 Snare_S1", family="SNARE1")
    assert wav2mci.main([str(tmp_path / "14 Test Crash"), "--out", str(tmp_path), "--name", "Snare"]) == 0
    f = mci.read_mci(str(tmp_path / "Snare.mci"))
    names = [f.get(f"INST0artic{a}artn") for a in range(f.get("INST0numart"))]
    assert names == ["Snare Center", "Snare Rimclick", "Snare Rimshot", "Snare Rimshot Side"]
    assert 5 in [f.get(f"INST0zoneType{z}") for z in range(f.get("INST0zonecnt"))]   # Rim Shot Side


def test_fit_length_cuts_and_fades():
    assert wav2mci.fit_length([5, 6], 4) == [5, 6]
    assert wav2mci.fit_length([100] * 10, 6, fade_frames=4) == [100, 100, 56, 25, 6, 0]
    assert wav2mci.fit_length([-100] * 3, 2, fade_frames=4) == [-25, 0]


def test_max_seconds_per_piece():
    ride = wav2mci.Piece("R", "cymbal", {"bell": [], "bow": []})
    crash = wav2mci.Piece("C", "cymbal", {"edge": []})
    assert wav2mci.max_seconds(ride) == 8.0 and wav2mci.max_seconds(crash) == 6.0
    assert wav2mci.max_seconds(wav2mci.Piece("K", "kick")) == 2.0
    assert wav2mci.fade_seconds(ride) == wav2mci.fade_seconds(crash) == 2.0
    assert wav2mci.fade_seconds(wav2mci.Piece("K", "kick")) == 0.5
