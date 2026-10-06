# Pearl Mimic instrument format (.mci)

This describes container version 3, as saved by the Mimic Instrument Editor. It was
reverse-engineered from five Editor files: a kick, a tom, a rototom, a snare and a ride.

The reference implementation in `tools/mci/mci.py` reads all five files and writes them
back byte for byte. It also decodes every sample bit-exactly. Anything below marked
**unknown** has not been confirmed on a Mimic yet.

All integers are little-endian.

## File layout

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 14 | Magic `mimic_storage\0` |
| 14 | 4 | u32 container version, always 3 |
| 18 | 4 | u32 metadata length, uncompressed |
| 22 | varies | Metadata, zlib stream (level 1) |
| base | sum(desc) × 4 | RAM heads, float32 |
| base + sum(desc) × 4 | sum(dlen) | Compressed sample data |
| end − 23 | 7 | `MMKCSM\0` |
| end − 16 | 16 | MD5 of every byte before `MMKCSM` |

`base` is where the zlib stream ends. The Mimic checks the MD5, so a writer must
recompute it.

## Metadata

The decompressed metadata is a flat key/value store, followed by a thumbnail.
Keys are ASCII and null-terminated, and each is stored as `u32 length (including the
null)` + bytes. The store has seven sections, in this order, each starting with a
`u32 count`. Within a section, keys are sorted by byte value, so `Lay10` comes before
`Lay2`.

| # | Value type | Encoding |
| --- | --- | --- |
| 1 | int32 | 4 bytes |
| 2 | int32 array | u32 n, then n × int32 |
| 3 | float32 | 4 bytes |
| 4 | float32 array | u32 n, then n × float32 |
| 5 | uint8 | 1 byte |
| 6 | **unknown** | always empty so far |
| 7 | string | u32 length including null, then bytes |

After section 7 comes `u32 length` + a PNG thumbnail (76×63 for the kick, 94×63 for
the ride). No key precedes it.

### Key naming

`INST0` is the instrument, `artic<a>` an articulation (zone), `veloLay<l>` a velocity
layer (0 = softest), `pool0` the sample pool, `micinf0` the mic, and `rmsenv<i>` the
envelope of pool sample i.

### Instrument keys

| Key | Type | Meaning |
| --- | --- | --- |
| `INST0insnam` | string | Instrument name, e.g. `22 ZILDJIAN K Dark Medium Ride Ride` |
| `INST0instyp` | string | Type: `Kick`, `Snare`, `Tom`, `Ride`, `Perc` |
| `INST0libnam` | string | Library name, e.g. `SL - Fields of Rock` |
| `INST0insdat`, `INST0insimj` | string | Always empty |
| `INST0numart`, `INST0zonecnt` | int | Number of articulations / zones |
| `INST0zoneArtId<a>` | int | Articulation for zone a, always a |
| `INST0zoneType<a>` | int | Zone type id (table below) |
| `INST0micnt` | int | Number of mics, always 1 |
| `INST0micinf0micn` | string | Mic name, e.g. `Kick In`, `Snare Top` |
| `INST0micinf0micpos`, `INST0micinf0mict` | int | Mic position / type (kick 2/0, ride 5/1, others 0/0) |
| `INST0micinf0*` floats and bytes | | Mic and envelope settings, identical in all five files |
| `INST0chkn`, `INST0ecov`, `INST0nznctrl` | int | Always 0 |
| `INST0insv` | int | Always 1 |
| `INST0dav2`, `INST0tmr`, `INSTedtr`, `INSTmnlth` | uint8 | Always 1 |

### Articulation keys

| Key | Type | Meaning |
| --- | --- | --- |
| `artic<a>artn` | string | Name: `Bow`, `Bell`, `Edge`, `Choke Ring`, `Snare Rimshot`… |
| `artic<a>artid` | int | = a |
| `artic<a>noteon` | int | Always 0 |
| `artic<a>numlay` | int | Number of velocity layers (0 for a choke zone) |
| `artic<a>vol` | float | Volume, 1.0 |
| `artic<a>chk`, `mut` | uint8 | 1 on a choke zone |
| `artic<a>fak` | uint8 | 1 on some secondary zones (snare rimshot side, perc additional) |
| `artic<a>minrtb`, `minrtt`, `maxrtb`, `maxrtt` | float[8] | Release-time range: bottom/top layer. Only element 0 is used, the rest are 1.0 |
| `veloLay<l>minvel` | int | Lowest MIDI velocity that plays this layer. Layer 0 is 0 |
| `veloLay<l>minvol`, `maxvol` | float | Volume range inside the layer. maxvol is 1.0, minvol is 0.0 for layer 0 and about 0.9–1.0 otherwise |
| `veloLay<l>minrtl`, `maxrtl` | float[8] | Release time of the layer, linearly interpolated from the articulation's bottom to top values |
| `veloLay<l>nums` | int | Round-robin count, 1–16 |
| `veloLay<l>smpidx` | int[16] | Pool indices of the round robins, padded with −1 |

Zone types seen so far:

| Instrument | Zones (type id) |
| --- | --- |
| Kick | Kick (1) |
| Tom | Center (1), Rimclick (2), Rimshot (3) |
| Snare | Center (1), Rimclick (2), Rimshot (3), Sidestick (4), Rimshot Side (5), Side (6) |
| Ride | Bow (40), Choke Ring (41), Bell (42), Edge (43) |
| Perc | Main (200), Secondary (201), Additional 1 (202), Additional 2 (203) |

Hi-hats, crashes and other cymbals have not been sampled yet.

### Pool keys (one entry per sample)

| Key | Type | Meaning |
| --- | --- | --- |
| `pool0psz` | int | Number of samples |
| `pool0fcnt` | int[] | Length in frames |
| `pool0nchn` | int[] | Channels, always 1 |
| `pool0dofs` | int[] | Offset of the compressed data, counted from `base` |
| `pool0dlen` | int[] | Compressed length, a multiple of 1024 |
| `pool0dedo` | int[] | Offset of the RAM head, counted from `base` |
| `pool0desc` | int[] | RAM head length in frames |
| `pool0nobu` | int[] | Compressed blocks covered by the head |
| `pool0svol` | float[] | Sample volume (0.52, or 0.599 on the snare) |
| `rmsenv<i>rmse` | int[] | Loudness envelope, one value per 1024 frames, on the 24-bit scale |
| `rmsenv<i>rmsl` | int | Envelope length = floor(fcnt / 1024) |
| `rmsenv<i>mrms` | float | Envelope maximum |

Pool samples are numbered articulation by articulation, layer by layer, then round
robin by round robin.

The exact window behind `rmse` is **unknown**. A plain 1024-frame RMS matches its
length and scale but differs by up to about 25%. It probably feeds the Mimic's dynamics
or level display rather than playback, so this needs a test on the device.

## Audio

Each sample is mono, on a 24-bit scale (full scale = 2²³). In every Editor file the
low 8 bits are zero, so the audio is effectively 16-bit. The sample rate is not stored
and is assumed to be 48 kHz (**unknown**).

### Compressed data

Each sample is a run of 1024-byte blocks:

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 2 | u16 `bits`, the delta width (10–25 seen) |
| 2 | 2 | i16 first frame >> 8 |
| 4 | 4 | u32 `n`, frames in the block |
| 8 | 1016 | n deltas of `bits` bits each, signed |

Decoding:

```
x = first << 8
deltas are read most-significant bit first from the payload, seen as little-endian u32 words
frame 0 = x; frame k+1 = frame k + delta k
```

The block stores n deltas, not n − 1. The last delta bridges to the first frame of the
next block, so n × bits ≤ 8128. Unused bits are zero.

The Editor sizes a delta as |d| < 2^(bits−1). It picks `bits` from about the next
580 frames and cuts a block short when a later delta would not fit. Our encoder
instead picks the smallest `bits` that fits a full block. That gives a different but
equally valid stream: the decoder only reads the headers.

### RAM heads

Every sample also has a float32 head, preloaded into RAM so it can play while the rest
streams from storage. A head holds 63 zero frames, then the sample's first frames. It
covers `nobu` whole blocks plus the first frame of the next one:

```
nobu = the most blocks k, up to 64, with 64 + frames(k blocks) ≤ 32768
desc = 63 + frames(nobu blocks) + 1      (no +1 when the head covers the whole sample)
```

The heads are stored in round-robin order: the first round robin of every layer of
every articulation comes first, then the second, and so on. `dedo` records where each
head landed.

## Open questions for a device test

- Does the Mimic load files written by `tools/mci/mci.py`? `eMIMICmkr TEST Kick.mci`
  is a rebuilt kick for this test.
- Is audio with non-zero low bytes (true 24-bit) allowed?
- What exactly do `rmse` and `mrms` drive, and how far off may they be?
- The sample rate.
- How does the Editor compute `minvel` and `minvol` from loudness? Ours is a
  square-root curve on peak RMS.
- What are the zone types for hi-hat, crash, china and splash?
