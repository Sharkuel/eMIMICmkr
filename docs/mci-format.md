# Pearl Mimic instrument format (.mci)

This describes container version 3, as saved by the Mimic Instrument Editor. It was
reverse-engineered from Editor files: a kick, a tom, a rototom, a snare, a ride, two
crashes saved in the ride layout, and a kick we built that the Editor opened and resaved.

The reference implementation in `tools/mci/mci.py` reads all of them and writes them
back byte for byte. It also decodes every sample bit-exactly. The app's C++ port
(`Source/Core/Mci.cpp`) writes the same bytes; `tests/test_cpp_converter.py` checks it. Anything below marked
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
| `INST0micinf0micpos`, `INST0micinf0mict` | int | Mic position / type (kick 2/0, ride 5/1, others 0/0). The Editor resets a kick to 2 when it saves |
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

### Cymbals

A 3-zone crash is saved exactly like a ride: `instyp` is `Ride`, the zones are Bow (40),
Bell (42), Edge (43) and Choke Ring (41), and the mic is `Ride` with micpos 5 and mict 1.
The Mimic then lists it in its ride menu. Both PAISTE crash files we have use this
layout, and both use the ride's release times (minrtb 0.5, minrtt 0.5, maxrtb 3.0,
maxrtt 5.0).

The Choke Ring is normally empty (`numlay` 0, release times all 1.0). It can also carry
a sample: one layer with one round robin, the same release times as the other zones,
and `minrtl`/`maxrtl` at the bottom values (0.5 and 3.0). A single layer always takes
the bottom of the release range.

A 2-zone crash or splash (the Mimic's crash menu) has `instyp` `Cymbal` and mic `Cymbal`
(micpos 5, mict 1). It has three zones, edge first: Edge (43), Bow (40) and an empty
Choke Ring (41), with the same release times as a ride. This comes from four Meinl
"X-Hihat" files from mimicpro.com, which are hi-hats packaged as 2-zone cymbals (they
came in a folder named `MeinlXhat.lib`). Those files are stereo (`pool0nchn` 2) and set
`INST0micinf0isst` to 1, where Editor files are mono with 0. On the Mimic a mono Cymbal or
Ride (micpos 5, the overhead mic) plays on the left side only, so eMIMICmkr writes those
two types as two identical channels with `isst` 1, like the Meinl files.

The Editor cannot make real hi-hats (open/closed with a pedal). Factory hi-hats are
described below.

### Choke samples

The Mimic plays one Choke Ring sample and scales it to how hard the pad is hit, so the
sample should hold only the cymbal's ringing (Alexandre, 2026-10-07). `make_choke()` in
`mci.py` makes one from the hardest edge hit: it cuts the attack where a 5 ms envelope,
at least 20 ms after its peak, first falls 6 dB below the peak, then keeps up to 3 s of
ring with a 5 ms fade in and a 1 s fade out.

### Hi-hats (factory instruments)

The Editor cannot make hi-hats, but Pearl's factory library has them. All six factory
hi-hats share one layout:

- `instyp` is `Hat`, with three mics: `Hi-Hat` (micpos 0, mict 0), `OH` (5, 1) and
  `Room` (6, 1). `insv` and `tmr` are 0, unlike Editor files.
- 18 articulations. Here `artid` is **not** the articulation's index, and `noteon` holds
  a MIDI note (the SSD drum map the samples came from):

| artid | Name | noteon | Notes |
| --- | --- | --- | --- |
| 0–4 | Shank Closed, 1/4, 2/4, 3/4 Open, Shank Open | 22, 66–69 | |
| 5–9 | Tip Closed, 1/4, 2/4, 3/4 Open, Tip Open | 42, 61–64 | |
| 10 | Pedal | 44 | zone 83 |
| 11 | Footsplash | 70 | zone 84 |
| 12–16 | Closed, 1/4, 2/4, 3/4 Open, Open | 108–112 | the edge |
| 17 | Transition | 23 | |

- Only two plain zones: Pedal (type 83) and Footsplash (type 84). `zoneArtId` holds the
  `artid`, not the index.
- The struck zones are controller zones, one per playing area, `nznctrl` = 3:

| Key | Meaning |
| --- | --- |
| `INST0hctrlzn<i>ctrzone` | Zone type: 81 tip (bow), 82 shank, 80 edge |
| `INST0hctrlzn<i>trzone` | int[]: the `artid`s from closed to open |
| `INST0hctrlzn<i>stznsz` | Number of openness steps, 5 |

  The pedal position picks one of the five articulations.
- Closed and partly open articulations have `mut` = 1. Release ranges are 0.5/0.3
  (bottom) and 1.0–2.0/1.5–4.0 (top), getting longer as the hat opens, and the first
  three elements of each release array are set rather than one.
- Pool samples are shared between articulations: 486 layer slots point to 338 samples.

`hat_articulations()` in `mci.py` builds this layout with one mic and mono audio, and
`build_instrument(..., controller_zones=...)` writes it. A hi-hat built this way, saved
as a single .mci, loaded on a Mimic and every articulation played as intended
(tested 2026-10-06).

### Factory library files

The factory library (`Mimic_Factory.lib`) stores each instrument as a pair in
`instruments/`: a `.bin` with the header and metadata (no audio, no MMKCSM trailer) and
a `.drd` with the RAM heads then the compressed data, offsets counted from its first
byte. `read_split()` in `mci.py` reads a pair. Kits (`.kit`) are `mimic_storage` files
with the kit's mixer and pad settings. `checksum.dat` starts with
`mimic_checksum_list`, a header, then a zlib stream of (null-terminated name, 16-byte
MD5 of the whole file) for every `.bin`, `.drd`, `.kit` and `libver.mimicinfo`.

### Pool keys (one entry per sample)

| Key | Type | Meaning |
| --- | --- | --- |
| `pool0psz` | int | Number of samples |
| `pool0fcnt` | int[] | Length in frames |
| `pool0nchn` | int[] | Channels: 1 in Editor files, 2 in our Cymbal/Ride, 5 in factory hats |
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

The exact formula behind `rmse` is **unknown**, but it depends only on the audio: the
Editor rewrote identical values when it resaved one of our files. The closest model so
far is a one-pole follower of |x| with a 1024-frame time constant, read at the end of
each window and scaled by 1.04. It lands within about 5–10% on every Editor file (a plain
1024-frame RMS was 12–30% off). It probably feeds the Mimic's dynamics or level display
rather than playback.

## Audio

Samples are on a 24-bit scale (full scale = 2²³). In every Editor file the audio is
mono and the low 8 bits are zero, so it is effectively 16-bit. Pearl's factory
instruments hold true 24-bit audio, and theirs is interleaved over `pool0nchn`
channels (5 in the factory hi-hats: frame 0 of channels 0–4, then frame 1, and so on).
`fcnt` counts every value, so a sample lasts fcnt / nchn frames, and the envelope
length is floor(fcnt / (1024 × nchn)).

The sample rate is not stored. It is almost certainly 44.1 kHz: factory sample lengths
per channel land on whole seconds at 44.1 kHz (220500, 352800, 441000 frames).

### Compressed data

Each sample is a run of 1024-byte blocks:

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 1 | u8 `bits`, the delta width (10–25 seen in Editor files, never below 4 in Pearl's) |
| 1 | 1 | u8 low byte of the first value |
| 2 | 2 | i16 first value >> 8 |
| 4 | 4 | u32 `n`, frames in the block |
| 8 | 1016 | n deltas of `bits` bits each, signed |

Decoding:

```
x = (high << 8) | low
deltas are read most-significant bit first from the payload, seen as little-endian u32 words
frame 0 = x; frame k+1 = frame k + delta k
```

The block stores n deltas, not n − 1. The last delta bridges to the first frame of the
next block, so n × bits ≤ 8128. Unused bits are zero.

The Editor sizes a delta as |d| < 2^(bits−1). It picks `bits` from about the next
580 frames and cuts a block short when a later delta would not fit. Our encoder
instead picks the smallest `bits` that fits a full block, but never less than 4. That
gives a different but equally valid stream: the decoder only reads the headers. The floor
matters: without it, silence packs into 1-bit blocks of 8128 frames, which Pearl's files
never contain (theirs stop at 4 bits, 2032 frames), and are the likely cause of the
clicks heard on the Mimic from instruments full of them.

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

## Kit presets (.kit)

A kit preset is the same container holding only metadata: the 22-byte header
(`mimic_storage\0`, u32 version 3, u32 metadata length), then the metadata as one zlib
stream (level 1) with an empty image, and nothing after it: no audio and no `MMKCSM`
trailer. The kit's name is its file name (`[U] <name>.kit`, inside `<name>.mdp`).

It has 26 fixed slots, named by `insstCS<slot>isltn` (0-25): Kick, Kick Layer, Snare,
Snare Layer, Hi-Hat, Ride 1, Ride 2, Tom 1-4, Crash 1-2, Aux 1-5, Xtra 1-8. Slot names hold
spaces and some are prefixes of others ("Kick", "Kick Layer"), so keys are matched as the
exact slot name followed by a known suffix.

| `insstCS<slot>` key | Meaning |
| --- | --- |
| `isltn` | slot number |
| `nm`, `tnm` | slot name; `tnm` is usually the slot name when loaded and empty when not |
| `itp` | type of the instrument in it (Kick, Snare, Tom, Hat, Ride, Cymbal, Perc, Wave) |
| `artc`, `artn<i>`, `azt` | articulation count, names and zone types, fixed per type (below) |
| `lsin`, `lsln` | the instrument's `INST0insnam` and `INST0libnam`: how the Mimic finds it |
| `isld`, `enb` | loaded, enabled (u8). Ride 2 is disabled in every kit Pearl ships |
| `mdir`, `moh`, `mrm` | output routing (16/22/23; the Kick slot has `mdir` 5) |

| `itp` | `artn` | `azt` |
| --- | --- | --- |
| Kick, Wave | Center | 1 |
| Snare | Center, Side, Rim Click, Side Stick, Rim Shot, Rim Shot Side | 1, 6, 2, 4, 3, 5 |
| Tom | Center, Rim Click, Rim Shot | 1, 2, 3 |
| Hat | Bow Single Zone, Bow Dual Zone, Edge Dual Zone, Chick, Foot Splash | 80-84 |
| Ride | Bow, Bell, Crash, Mute | 40, 42, 43, 41 |
| Cymbal | Bow, Crash, Mute | 40, 43, 41 |
| Perc | Main, Secondary, Additional 1, Additional 2 | 200-203 |

A loaded slot has a mixer channel, `CME<slot>` + `cb cv dov iv mal nm sd sv sva sw tn vr`,
`cmp<0-7>...` (compressors), `eq<0-7>e5v1[en]` (EQs) and one `mi<n>...` group per mic
(`nm` is the mic count), which copies the instrument's `INST0micinf<n>...` with the kit's
own level (`micv`) and pan (`micp`, 0.5 = centre). An empty slot has no `CME<slot>` keys.
Bus channels (`CMEKicks...`, `CMESnares...`, `CMERoom...`, `CMEOH...`, `CMEMaster...`),
the master (`mK...`, `mRev...`, `KitVol`) and pad settings (`TPAD...`) are kit-wide.

`emk::MimicKit` builds a kit from a user's kit as template; `eMIMICmkr_mci_tests
--rewrite-kit IN OUT` rewrites a kit byte for byte, and `--kit TEMPLATE OUT MCI...` builds one.

## Open questions for a device test

- Does the Mimic itself load files written by `tools/mci/mci.py`? The Editor already
  does: it opened `eMIMICmkr TEST Kick.mci`, recomputed the velocities, and resaved it.
- Will the Mimic accept a user library laid out like the factory one (.bin/.drd)?
- What exactly do `rmse` and `mrms` drive, and how far off may they be?
- How does the Editor compute `minvel` and `minvol` from loudness ("Calc Velo")? Ours
  is a square-root curve on peak loudness, close but not identical.
- What does `isst` do on its own (we only ever set it together with 2 channels)?
