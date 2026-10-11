"""Runs the C++ batch recorder against its stand-in plugin, then converts the result.

Needs the eMIMICmkr_tests console app (cmake --build build --target eMIMICmkr_tests);
set EMIMICMKR_TESTS to its path when the build folder is not ./build.
"""
import glob
import os
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "mci"))
import wav2mci  # noqa: E402

EXE = os.environ.get("EMIMICMKR_TESTS") or next(iter(glob.glob(
    os.path.join(ROOT, "build", "eMIMICmkr_tests_artefacts", "*", "eMIMICmkr_tests*"))), "")


@pytest.mark.skipif(not os.path.isfile(EXE), reason="eMIMICmkr_tests is not built")
def test_recorded_kit_converts(tmp_path):
    run = subprocess.run([EXE, str(tmp_path / "rec")], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr

    kit = tmp_path / "rec" / "Test Kit"
    bell = sorted((kit / "Test Kit Ride" / "RIDE1 BELL").iterdir())
    assert bell[0].name == "Test Kit Ride - RIDE1 BELL - 001v127.wav"
    # 32-bit float, so a hit past full scale isn't clipped
    channels, rate, _ = wav2mci._float_wav(bell[0].read_bytes())
    assert (channels, rate) == (1, 44100)

    out = tmp_path / "out"
    assert wav2mci.main([str(kit), "--out", str(out)]) == 0
    names = sorted(p.name for p in out.glob("*.mci"))
    assert "Test Kit Hi-Hat.mci" in names and "Test Kit Ride.mci" in names and len(names) == 10
