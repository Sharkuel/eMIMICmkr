"""playcheck on made-up recordings of the Mimic playing its instrument."""
import os
import sys
import wave

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "mci"))
import mci  # noqa: E402
import playcheck  # noqa: E402


def take(path, hits):
    """A mimic-rec style take: each hit preceded by a second of silence."""
    y = []
    for h in hits:
        y += [0] * 44100 + h
    y += [0] * 4410
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(44100)
        w.writeframes(b"".join(max(-2 ** 23, min(2 ** 23 - 1, v)).to_bytes(3, "little", signed=True) for v in y))
    return str(path)


def test_playback_check(tmp_path, capsys):
    path = playcheck.make(str(tmp_path))
    f = mci.read_mci(path)
    x = mci.sample(f, 0)
    head = f.get("INST0pool0desc")[0] - mci.HEAD_PAD
    assert len(x) == 3 * 44100 and head < len(x)   # the sample is longer than its RAM head

    def scaled(s, g):
        return [round(v * g) for v in s]

    assert playcheck.check(path, take(tmp_path / "ok.wav", [scaled(x, 0.8), scaled(x, 0.3)])) == 0
    assert "the Mimic plays it as eMIMICmkr wrote it" in capsys.readouterr().out

    twice = x[:head] + x[head - 1:-1]          # the head's last frame played again by the stream
    assert playcheck.check(path, take(tmp_path / "twice.wav", [scaled(twice, 0.5)])) == 1
    out = capsys.readouterr().out
    assert "1 frame played twice, where the head ends" in out, out

    skipped = x[:head] + x[head + 1:]
    assert playcheck.check(path, take(tmp_path / "skip.wav", [scaled(skipped, 0.5)])) == 1
    assert "1 frame skipped, where the head ends" in capsys.readouterr().out
