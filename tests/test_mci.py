"""Checks the .mci reader/writer against real Mimic Instrument Editor files.

Point MCI_SAMPLES at a folder of .mci files:
    MCI_SAMPLES=/path/to/mci python3 -m pytest tests
"""
import os
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "mci"))
import mci  # noqa: E402

SAMPLES = sorted(Path(os.environ.get("MCI_SAMPLES", "")).glob("*.mci")) if os.environ.get("MCI_SAMPLES") else []
needs_samples = pytest.mark.skipif(not SAMPLES, reason="set MCI_SAMPLES to a folder of .mci files")


@needs_samples
@pytest.mark.parametrize("path", SAMPLES, ids=lambda p: p.name)
def test_container_round_trips_byte_for_byte(path):
    original = path.read_bytes()
    assert mci.write_mci(mci.read_mci(original)) == original


@needs_samples
@pytest.mark.parametrize("path", SAMPLES, ids=lambda p: p.name)
def test_heads_match_decoded_audio(path):
    f = mci.read_mci(str(path))
    desc, dedo = f.get("INST0pool0desc"), f.get("INST0pool0dedo")
    for i in range(0, f.get("INST0pool0psz"), 7):
        s = mci.sample(f, i)
        assert len(s) == f.get("INST0pool0fcnt")[i]
        head = struct.unpack_from(f"<{desc[i]}f", f.heads, dedo[i])
        assert all(h == 0 for h in head[:mci.HEAD_PAD])
        assert [round(h * 2 ** 23) for h in head[mci.HEAD_PAD:]] == s[:desc[i] - mci.HEAD_PAD]


@needs_samples
def test_rebuilt_instrument_keeps_audio_and_structure():
    path = next(p for p in SAMPLES if mci.read_mci(str(p)).get("INST0numart") == 1)
    f = mci.read_mci(str(path))
    layers = []
    for l in range(f.get("INST0artic0numlay")):
        idx = [i for i in f.get(f"INST0artic0veloLay{l}smpidx") if i >= 0]
        layers.append([mci.sample(f, i) for i in idx])
    art = mci.Articulation(f.get("INST0artic0artn"), f.get("INST0zoneType0"), layers)
    built = mci.build_instrument(f.get("INST0insnam"), f.get("INST0instyp"), [art], f.image,
                                 library=f.get("INST0libnam"), mic=f.get("INST0micinf0micn"))
    again = mci.read_mci(mci.write_mci(built))

    assert set(again.meta) == set(f.meta)
    assert all(again.meta[k][0] == f.meta[k][0] for k in f.meta), "section types differ"
    for i in range(f.get("INST0pool0psz")):
        assert mci.sample(again, i) == mci.sample(f, i)
    assert again.get("INST0pool0fcnt") == f.get("INST0pool0fcnt")
    desc, dedo = again.get("INST0pool0desc"), again.get("INST0pool0dedo")
    for i in range(0, again.get("INST0pool0psz"), 9):
        head = struct.unpack_from(f"<{desc[i]}f", again.heads, dedo[i])
        assert [round(h * 2 ** 23) for h in head[mci.HEAD_PAD:]] == mci.sample(again, i)[:desc[i] - mci.HEAD_PAD]


@needs_samples
def test_ride_layout_matches_editor_3_zone_crash():
    path = next((p for p in SAMPLES if "3 zone" in p.name), None)
    if path is None:
        pytest.skip("no 3-zone crash sample")
    f = mci.read_mci(str(path))

    def zone(a, n=2):   # first n layers, one round robin each, keeps the test fast
        return [[mci.sample(f, f.get(f"INST0artic{a}veloLay{l}smpidx")[0])] for l in range(n)]

    arts = mci.ride_articulations(zone(0), zone(1), zone(2), choke=zone(3, 1))
    built = mci.build_instrument(f.get("INST0insnam"), "Ride", arts, f.image, mic="Ride")
    for a in range(4):
        for k in ["artn", "chk", "fak", "mut", "minrtb", "minrtt", "maxrtb", "maxrtt"]:
            assert built.get(f"INST0artic{a}{k}") == f.get(f"INST0artic{a}{k}"), (a, k)
        assert built.get(f"INST0zoneType{a}") == f.get(f"INST0zoneType{a}")
    for k in ["minrtl", "maxrtl", "minvol", "maxvol", "minvel"]:
        assert built.get(f"INST0artic3veloLay0{k}") == f.get(f"INST0artic3veloLay0{k}"), k
    for k in ["INST0micinf0micpos", "INST0micinf0mict", "INST0instyp"]:
        assert built.get(k) == f.get(k)


@needs_samples
def test_cymbal_layout_matches_2_zone_files():
    path = next((p for p in SAMPLES if mci.read_mci(str(p)).get("INST0instyp") == "Cymbal"), None)
    if path is None:
        pytest.skip("no 2-zone cymbal sample")
    f = mci.read_mci(str(path))

    def zone(a, n=2):
        return [[mci.sample(f, f.get(f"INST0artic{a}veloLay{l}smpidx")[0])] for l in range(n)]

    built = mci.build_instrument("t", "Cymbal", mci.cymbal_articulations(zone(0), zone(1)),
                                 f.image, mic="Cymbal")
    assert built.get("INST0numart") == f.get("INST0numart") == 3
    for a in range(3):
        for k in ["artn", "chk", "fak", "mut", "minrtb", "minrtt", "maxrtb", "maxrtt"]:
            assert built.get(f"INST0artic{a}{k}") == f.get(f"INST0artic{a}{k}"), (a, k)
        assert built.get(f"INST0zoneType{a}") == f.get(f"INST0zoneType{a}")
    for k in ["INST0micinf0micpos", "INST0micinf0mict", "INST0micinf0micn"]:
        assert built.get(k) == f.get(k)


def test_make_choke_follows_the_template():
    hit = [1.0] * 48000
    choke = mci.make_choke(hit, rate=48000)
    assert len(choke) == round(0.5187 * 48000)
    assert choke[0] == pytest.approx(1.3862, rel=1e-4)
    assert choke[-1] == pytest.approx(0.2520, rel=1e-4)
    mid = int((0.0446 + 0.4685) / 2 * 48000)
    assert choke[mid] == pytest.approx((1.3862 * 0.2520) ** 0.5, rel=1e-2)   # fades in dB


SPLIT = sorted(p for p in Path(os.environ.get("MCI_SAMPLES", ".")).glob("*.bin")
               if p.with_suffix(".drd").exists()) if os.environ.get("MCI_SAMPLES") else []


@pytest.mark.skipif(not SPLIT, reason="needs a factory .bin/.drd pair in MCI_SAMPLES")
def test_factory_split_instrument_decodes_24_bit_audio():
    f = mci.read_split(str(SPLIT[0]), str(SPLIT[0].with_suffix(".drd")))
    desc, dedo = f.get("INST0pool0desc"), f.get("INST0pool0dedo")
    for i in range(0, f.get("INST0pool0psz"), 50):
        x = mci.sample(f, i)
        assert len(x) == f.get("INST0pool0fcnt")[i]
        head = struct.unpack_from(f"<{desc[i]}f", f.heads, dedo[i])
        assert [round(h * 2 ** 23) for h in head[mci.HEAD_PAD:]] == x[:desc[i] - mci.HEAD_PAD]
        assert len(mci.channels(f, i)) == f.get("INST0pool0nchn")[i]
    x = mci.sample(f, 0)
    assert any(s & 0xFF for s in x), "expected true 24-bit audio"
    blob, _ = mci.encode_blocks(x[:20000])
    assert mci.decode_blocks(blob) == x[:20000]


@pytest.mark.skipif(not SPLIT, reason="needs a factory .bin/.drd pair in MCI_SAMPLES")
def test_hat_layout_matches_factory_hi_hat():
    f = mci.read_split(str(SPLIT[0]), str(SPLIT[0].with_suffix(".drd")))
    hit = [[[0, 256, -512, 0] * 600]]           # one layer, one tiny sample per zone
    arts, ctrl = mci.hat_articulations([hit] * 5, [hit] * 5, [hit] * 5, hit, hit, transition=hit)
    built = mci.read_mci(mci.write_mci(
        mci.build_instrument("t", "Hat", arts, f.image, mic="Hi-Hat", controller_zones=ctrl)))

    def by_artid(m):
        return {m.get(f"INST0artic{a}artid"): a for a in range(m.get("INST0numart"))}
    ours, theirs = by_artid(built), by_artid(f)
    assert set(ours) == set(theirs)
    for artid in ours:
        for k in ["artn", "noteon", "chk", "fak", "mut"]:
            assert built.get(f"INST0artic{ours[artid]}{k}") == f.get(f"INST0artic{theirs[artid]}{k}"), (artid, k)
        for k in ["minrtb", "minrtt", "maxrtb", "maxrtt"]:
            assert built.get(f"INST0artic{ours[artid]}{k}")[0] == f.get(f"INST0artic{theirs[artid]}{k}")[0], (artid, k)
    for k in ["INST0zonecnt", "INST0zoneType0", "INST0zoneType1", "INST0zoneArtId0",
              "INST0zoneArtId1", "INST0nznctrl", "INST0instyp", "INST0micinf0micpos", "INST0micinf0mict"]:
        assert built.get(k) == f.get(k), k
    for c in range(3):
        for k in ["ctrzone", "stznsz", "trzone"]:
            assert built.get(f"INST0hctrlzn{c}{k}") == f.get(f"INST0hctrlzn{c}{k}"), (c, k)
