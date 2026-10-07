"""The C++ converter (Source/Core/WavToMci) against the Python reference (tools/mci/wav2mci.py).

Both convert the same input; the results must have identical metadata, heads and
compressed audio. Only the zlib stream holding the metadata may differ (and so the MD5),
should JUCE's bundled zlib ever compress differently from Python's.

Needs eMIMICmkr_mci_tests (cmake --build build --target eMIMICmkr_mci_tests); set
EMIMICMKR_MCI_TESTS to its path when the build folder is not ./build. The stand-in kit test
also needs eMIMICmkr_tests (EMIMICMKR_TESTS). The real SDSE zips are slow in Python: they
run with MCI_REAL=1, read from MCI_SAMPLES (default /mnt/project-files).
"""
import glob
import math
import os
import random
import struct
import subprocess
import sys
import wave
import zipfile
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "mci"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import mci  # noqa: E402
import wav2mci  # noqa: E402
from test_wav2mci import HAT_FOLDERS, render  # noqa: E402


def _find(env, target):
    return os.environ.get(env) or next(iter(glob.glob(
        str(ROOT / "build" / f"{target}_artefacts" / "*" / f"{target}*"))), "")


EXE = _find("EMIMICMKR_MCI_TESTS", "eMIMICmkr_mci_tests")
KIT_EXE = _find("EMIMICMKR_TESTS", "eMIMICmkr_tests")
pytestmark = pytest.mark.skipif(not os.path.isfile(EXE), reason="eMIMICmkr_mci_tests is not built")

REAL_DIR = Path(os.environ.get("MCI_SAMPLES") or "/mnt/project-files")
REAL_ZIPS = sorted(REAL_DIR.glob("*.zip")) if os.environ.get("MCI_REAL") else []


# ---------------------------------------------------------------------------
# Running both converters and comparing

def sources(src):
    """One input or a list of them, as command-line arguments."""
    return [str(s) for s in src] if isinstance(src, (list, tuple)) else [str(src)]


def run_cpp(src, out, *flags):
    run = subprocess.run([EXE, "--convert", *sources(src), "--out", str(out), *flags],
                         capture_output=True, text=True)
    return run.returncode, run.stdout + run.stderr


def zlib_end(raw: bytes) -> int:
    z = zlib.decompressobj()
    z.decompress(raw[22:-23])
    return len(raw) - 23 - len(z.unused_data)


def compare_files(py_path, cpp_path):
    """Asserts the two files hold the same instrument; returns True when they are byte-identical."""
    a, b = mci.read_mci(str(py_path)), mci.read_mci(str(cpp_path))
    assert sorted(a.meta) == sorted(b.meta), f"{cpp_path.name}: different keys"
    for k in a.meta:
        assert a.meta[k] == b.meta[k], f"{cpp_path.name}: {k} differs"
    assert a.image == b.image
    assert a.heads == b.heads, f"{cpp_path.name}: RAM heads differ"
    if a.data != b.data:
        for i in range(a.get("INST0pool0psz")):
            assert mci.sample(a, i) == mci.sample(b, i), f"{cpp_path.name}: sample {i} differs"
        pytest.fail(f"{cpp_path.name}: same audio, different block layout")

    ra, rb = py_path.read_bytes(), cpp_path.read_bytes()
    assert ra[:22] == rb[:22], "header differs"
    assert ra[zlib_end(ra):-23] == rb[zlib_end(rb):-23]
    return ra == rb


def convert_both(tmp_path, src, *flags, expect=None):
    py_out, cpp_out = tmp_path / "py", tmp_path / "cpp"
    assert wav2mci.main([*sources(src), "--out", str(py_out), *flags]) == 0
    code, log = run_cpp(src, cpp_out, *flags)
    assert code == 0, log
    py_files = sorted(p.name for p in py_out.glob("*.mci"))
    assert py_files == sorted(p.name for p in cpp_out.glob("*.mci")), log
    assert py_files, "nothing converted"
    if expect is not None:
        assert py_files == sorted(expect)
    identical = [compare_files(py_out / n, cpp_out / n) for n in py_files]
    # JUCE bundles zlib 1.3.1; so far it compresses byte for byte like Python's zlib.
    assert all(identical), "metadata matches but the zlib stream differs"
    return py_out, cpp_out, log


# ---------------------------------------------------------------------------
# Synthetic inputs

def write_wav(path, frames, width=3, channels=1, rate=44100):
    """frames: a list of per-channel tuples (or ints for mono), already on the target scale."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(width)
        w.setframerate(rate)
        data = bytearray()
        for f in frames:
            for v in (f if channels > 1 else (f,)):
                data += int(v).to_bytes(width, "little", signed=True)
        w.writeframes(bytes(data))


def noisy_hit(velocity, n, width, channels, seed):
    """A decaying noise burst, louder with velocity, different on each channel."""
    rnd = random.Random(seed)
    top = 2 ** (8 * width - 1) - 1
    amp = velocity / 127 * 0.9
    out = []
    for i in range(n):
        env = amp * math.exp(-i / (n / 5))
        vals = [max(-top - 1, min(top, round(top * env * (rnd.random() * 2 - 1)))) for _ in range(channels)]
        out.append(tuple(vals) if channels > 1 else vals[0])
    return out


def mixed_kit(root):
    """Kick, snare, tom and a crash with a recorded choke, in several WAV formats, with
    repeated velocities (round robins), long hits, and files the scan must skip."""
    kit = root / "Mixed Kit"
    zones = [  # (piece, zone label, velocities, width, channels, length)
        ("22 Kick_K1", "KICK1", [127, 127, 100, 100, 64, 30, 30, 5], 2, 1, 3000),
        ("14 Snare_S1", "SNARE1 CENTER", [127, 110, 90, 90, 90, 60, 20], 3, 2, 50000),
        ("14 Snare_S1", "SNARE1 RIMSHOT", [127, 64, 1], 4, 1, 2000),
        ("14 Snare_S1", "SNARE1 XSTICK", [100, 50], 3, 1, 1500),
        ("12 Tom_T1", "TOM1 CENTER", [127, 64], 4, 2, 70000),
        ("12 Tom_T1", "TOM1 RIM", [120], 3, 1, 1200),
        ("12 Tom_T1", "TOM1 SIDESTICK", [90], 3, 1, 1200),       # toms have no sidestick
        ("16 Crash_C1", "CRASH1 EDGE", list(range(127, 0, -9)), 3, 2, 60000),
        ("16 Crash_C1", "CRASH1 CHOKE", [80, 127, 127], 2, 1, 9000),
    ]
    seed = 0
    for piece, label, vels, width, ch, n in zones:
        for i, v in enumerate(vels, 1):
            seed += 1
            write_wav(kit / f"{piece} - {label}" / f"{piece} - {label} - {i:03d}v{v}.wav",
                      noisy_hit(v, n, width, ch, seed), width, ch)
    (kit / "notes.txt").write_text("not audio")
    write_wav(kit / "loose" / "untitled.wav", [0, 1, 2])
    write_wav(kit / "loose" / "Thing - GONG1 - 001v127.wav", [0, 1, 2])
    return kit


def hat_folders(root, folders, velocities, piece="14 Test Hat_HH1"):
    for i, folder in enumerate(folders):
        d = root / "Hat" / f"{piece} - HATS {folder}"
        for k, v in enumerate(velocities, 1):
            write_wav(d / f"{piece} - HATS {folder} - {k:03d}v{v}.wav",
                      noisy_hit(v, 3000 + 100 * i, 3, 1, 1000 * i + k))
    return root / "Hat"


# ---------------------------------------------------------------------------
# Tests

def test_cpp_self_checks():
    run = subprocess.run([EXE], capture_output=True, text=True)
    assert run.returncode == 0 and run.stdout.strip().endswith("OK"), run.stdout


def test_two_zone_crash_with_made_choke(tmp_path):
    render(tmp_path / "in", ["BOW", "EDGE"], range(127, 0, -3))
    convert_both(tmp_path, tmp_path / "in" / "14 Test Crash", expect=["14 Test Crash.mci"])


@pytest.mark.parametrize("flags", [[], ["--no-choke"]])
def test_three_zone_ride(tmp_path, flags):
    render(tmp_path / "in", ["BOW", "EDGE", "BELL"], [20, 60, 100, 127, 127])
    convert_both(tmp_path, tmp_path / "in" / "14 Test Crash", *flags)


def test_cell_limits_thin_out_big_zones(tmp_path):
    render(tmp_path / "in", ["BOW", "EDGE"], list(range(127, 0, -1)) + list(range(127, 50, -1)))
    py_out, _, _ = convert_both(tmp_path, tmp_path / "in" / "14 Test Crash")
    f = mci.read_mci(str(py_out / "14 Test Crash.mci"))
    assert f.get("INST0pool0psz") == 200


def test_mixed_formats_kit_folder(tmp_path):
    kit = mixed_kit(tmp_path / "in")
    _, _, log = convert_both(tmp_path, kit, expect=["22 Kick.mci", "14 Snare.mci", "12 Tom.mci", "16 Crash.mci"])
    assert "untitled.wav" in log and "GONG1" in log     # reported as skipped


def test_mixed_formats_kit_zip_with_name(tmp_path):
    kit = mixed_kit(tmp_path / "in")
    z = tmp_path / "Mixed Kit.zip"
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(kit.rglob("*")):
            zf.write(p, str(p.relative_to(kit.parent)))
    convert_both(tmp_path, z)
    # one piece in a zip: its top folder names the instrument, or --name does
    single = tmp_path / "Crash Only.zip"
    with zipfile.ZipFile(single, "w") as zf:
        for p in sorted((kit / "16 Crash_C1 - CRASH1 EDGE").iterdir()):
            zf.write(p, f"16 Crash Only/{p.name}")
    convert_both(tmp_path / "a", single, expect=["16 Crash Only.mci"])
    convert_both(tmp_path / "b", single, "--name", "My Crash", expect=["My Crash.mci"])


@pytest.mark.parametrize("flags", [[], ["--hat-as-ride"]])
def test_hi_hat_folders(tmp_path, flags):
    folders = (["BOW CLOSED", "BOW OPEN1", "BOW OPEN2", "BOW OPEN3", "BOW OPEN4"]
               + ["EDGE CLOSED", "EDGE OPEN1", "EDGE OPEN2", "EDGE OPEN3", "EDGE OPEN4"]
               + ["PEDAL CLOSED"])
    src = hat_folders(tmp_path / "in", folders, list(range(127, 0, -3)))   # 43 hits: more than 32
    expect = ["14 Test Hat.mci"] + (["14 Test Hat 3-zone.mci"] if flags else [])
    py_out, _, _ = convert_both(tmp_path, src, *flags, expect=expect)
    f = mci.read_mci(str(py_out / "14 Test Hat.mci"))
    assert f.get("INST0instyp") == "Hat" and f.get("INST0numart") == 17


@pytest.mark.parametrize("flags", [[], ["--hat-as-ride"]])
def test_sdse_hi_hat_with_bell_tight_and_foot_splash(tmp_path, flags):
    render(tmp_path / "in", HAT_FOLDERS, [30, 90, 127], piece="14 Z K Cust Session_XY1", family="HATS")
    convert_both(tmp_path, tmp_path / "in" / "14 Test Crash", *flags)


def ride_folders(root, velocities=range(127, 0, -2)):
    """A ride laid out as BatchRecorder writes it: one plain folder per zone."""
    ride = root / "My Kit Ride"
    for z, zone in enumerate(["BOW", "BELL", "EDGE"]):
        for i, v in enumerate(velocities, 1):
            write_wav(ride / zone / f"My Kit Ride - RIDE1 {zone} - {i:03d}v{v}.wav",
                      noisy_hit(v, 2000 + 50 * z, 3, 1, 100 * z + i))
    return ride


def test_several_zone_folders_convert_like_their_parent(tmp_path):
    ride = ride_folders(tmp_path / "in")
    whole, _, _ = convert_both(tmp_path / "whole", ride, expect=["My Kit Ride.mci"])
    zones = [ride / "EDGE", ride / "BOW", ride / "BELL"]
    picked, _, log = convert_both(tmp_path / "picked", zones, expect=["My Kit Ride.mci"])
    assert (whole / "My Kit Ride.mci").read_bytes() == (picked / "My Kit Ride.mci").read_bytes()
    f = mci.read_mci(str(picked / "My Kit Ride.mci"))
    assert f.get("INST0instyp") == "Ride" and f.get("INST0artic0numlay") == 16   # 64 hits, 4 round robins

    # a folder given inside another one adds nothing; a zip among several inputs is skipped
    z = tmp_path / "bell.zip"
    with zipfile.ZipFile(z, "w") as zf:
        for p in sorted((ride / "BELL").iterdir()):
            zf.write(p, p.name)
    again, _, log = convert_both(tmp_path / "again", [ride, ride / "BELL", z], expect=["My Kit Ride.mci"])
    assert (again / "My Kit Ride.mci").read_bytes() == (whole / "My Kit Ride.mci").read_bytes()
    assert "bell.zip: a .zip is converted on its own" in log


def test_several_inputs_default_to_the_folder_holding_them(tmp_path):
    ride = ride_folders(tmp_path / "in", [127, 64, 1])
    zones = [ride / "BOW", ride / "EDGE"]
    assert wav2mci.main([str(z) for z in zones]) == 0
    assert (ride / "My Kit Ride.mci").is_file()
    (ride / "My Kit Ride.mci").unlink()
    run = subprocess.run([EXE, "--convert", *map(str, zones)], capture_output=True, text=True)
    assert run.returncode == 0 and (ride / "My Kit Ride.mci").is_file(), run.stdout


def test_a_zone_folder_converts_on_its_own(tmp_path):
    ride = ride_folders(tmp_path / "in", [127, 90, 64, 30, 1])
    _, cpp_out, _ = convert_both(tmp_path, ride / "BELL", expect=["My Kit Ride.mci"])
    f = mci.read_mci(str(cpp_out / "My Kit Ride.mci"))
    assert f.get("INST0instyp") == "Cymbal"     # a bell alone: no edge or bow to make a ride of


def test_kit_pieces_selected_together(tmp_path):
    kit = mixed_kit(tmp_path / "in")
    pieces = [kit / "22 Kick_K1 - KICK1", kit / "14 Snare_S1 - SNARE1 CENTER", kit / "14 Snare_S1 - SNARE1 RIMSHOT"]
    convert_both(tmp_path, pieces, expect=["22 Kick.mci", "14 Snare.mci"])


def test_rejects_other_sample_rates(tmp_path):
    d = tmp_path / "in" / "K" / "22 Kick_K1 - KICK1"
    write_wav(d / "22 Kick_K1 - KICK1 - 001v127.wav", [0, 1000, -1000] * 100, rate=48000)
    code, log = run_cpp(tmp_path / "in" / "K", tmp_path / "out")
    assert code != 0 and "48000 Hz" in log
    assert not list((tmp_path / "out").glob("*.mci"))


@pytest.mark.skipif(not os.path.isfile(KIT_EXE), reason="eMIMICmkr_tests is not built")
def test_recorded_stand_in_kit(tmp_path):
    run = subprocess.run([KIT_EXE, str(tmp_path / "rec")], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    _, cpp_out, _ = convert_both(tmp_path, tmp_path / "rec" / "Test Kit")
    assert len(list(cpp_out.glob("*.mci"))) == 10


@pytest.mark.skipif(not REAL_ZIPS, reason="set MCI_REAL=1 (and MCI_SAMPLES) to convert the real SDSE zips")
@pytest.mark.parametrize("path", REAL_ZIPS, ids=lambda p: p.name)
def test_real_sdse_zip(tmp_path, path):
    convert_both(tmp_path, path)


SAMPLE_MCI = sorted(Path(os.environ["MCI_SAMPLES"]).glob("*.mci")) if os.environ.get("MCI_SAMPLES") else []


@pytest.mark.skipif(not SAMPLE_MCI, reason="set MCI_SAMPLES to a folder of .mci files")
@pytest.mark.parametrize("path", SAMPLE_MCI, ids=lambda p: p.name)
def test_cpp_reader_and_writer_round_trip_editor_files(tmp_path, path):
    out = tmp_path / "again.mci"
    run = subprocess.run([EXE, "--rewrite", str(path), str(out)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    assert out.read_bytes() == path.read_bytes()
