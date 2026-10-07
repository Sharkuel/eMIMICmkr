# eMIMICmkr
Tool to record samples from your VST libraries, and convert them into the Pearl Mimic.

Pronunciation (e-MIMIC-maker)

eMIMICmkr hosts a drum plugin (Superior Drummer 3, BFD3, Addictive Drums and others)
and works out which MIDI note plays which kit piece and zone. It records every zone
at several velocities and builds Pearl Mimic instruments (.mci) with the layers
sorted by how hard each hit was played.

## Status

| Part | State |
| --- | --- |
| .mci format | Decoded. Reader and writer work, tested on five Editor files ([docs/mci-format.md](docs/mci-format.md)) |
| Plugin hosting | Find plugins, load one, show its window, listen to any zone |
| Kit auto-mapping | Editable note map with Roland e-drum and General MIDI presets; reading the plugin's own map not yet |
| Multi-velocity recording | Batch recording of a whole kit, hi-hat openness on CC4; awaiting a Superior Drummer 3 test |
| .mci export | Python (`tools/mci`): every instrument type, plus a WAV-folder converter. C++ port not yet |

## Building the app

You need CMake 3.22+ and a C++17 compiler. JUCE 8 is downloaded automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Linux, install the JUCE dependencies first, for example on Debian/Ubuntu:

```
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libfreetype-dev libfontconfig1-dev
```

On Linux, Windows plugins can run through a bridge such as yabridge, which exposes
them as VST3 plugins the app can load.

## Recording a kit

1. **Find plugins**, pick your drum plugin and **Load plugin**. Load a kit in the plugin window.
2. Pick the note map. **e-drum (Roland TD, hi-hat on CC4)** is the default: set the plugin's
   MIDI input map to its Roland TD preset (Superior Drummer 3: Settings > MIDI In/E-Drums) so
   the notes match and the hi-hat opens and closes with CC4. Every cell of the table can be
   edited: double-click a note to change it, untick a zone to skip it.
3. Select a row and **Listen to zone** to check the note plays the right sound.
4. Set **Hits per zone** (default 64, spread from velocity 127 down to 1) and **Record kit**.

Hits are recorded offline at 44.1 kHz, mixed to centred mono from the plugin's main output,
and saved as 24-bit WAVs under `Documents/eMIMICmkr/<kit name>/`, one folder per zone, in
the layout `wav2mci.py` reads (below). The hi-hat is recorded at five pedal positions
(CC4 127, 96, 64, 32, 0) on both bow and edge, plus the pedal chick.

`cmake --build build --target eMIMICmkr_tests` builds a check of the recorder against a
stand-in plugin; `tests/test_batch_recording.py` runs it and converts the result.

## The .mci tools

`tools/mci/mci.py` reads, writes and builds Mimic instruments, with no dependencies
beyond Python 3. To run its tests against your own Editor files:

```
pip install pytest
MCI_SAMPLES=/path/to/folder/with/mci python3 -m pytest tests
```

### Converting rendered WAVs

`tools/mci/wav2mci.py` turns hits rendered by SDSE or a similar tool into Mimic
instruments. It reads a zip or a folder laid out one folder per zone, with files named
`<piece> - <ZONE> - <hit>v<velocity>.wav`:

```
python3 tools/mci/wav2mci.py "22 ZILDJIAN K Dark Medium Ride.zip" --out ~/Mimic
```

Cymbals with a bell become 3-zone ride-type instruments, the rest 2-zone cymbals, and a
choke sound is made from the hardest edge hit when none was recorded. Kicks, snares and
toms are recognised too, and SDSE hi-hat folders become a Mimic hi-hat (add `--hat-as-ride` for
a 3-zone ride-type version as well). WAVs must be 44.1 kHz.

## Licensing

eMIMICmkr is GPLv3. JUCE 8's open-source licence is AGPLv3, so the app as a whole is
distributed under AGPLv3 terms unless a JUCE commercial licence is used.

Converted kits contain samples owned by the plugin maker. Check your plugin's licence
before using them anywhere but your own Mimic, and never share or sell them.
