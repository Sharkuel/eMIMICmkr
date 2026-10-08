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
| .mci export | In the app (`Source/Core/Mci`, `WavToMci`): every instrument type, plus the WAV-folder converter. `tools/mci` holds the Python reference implementation |

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
3. Select a row and **Listen to zone** to check the note plays the right sound. Other ways to
   build the map, since each library maps notes its own way:
   - **Learn notes**: select a row, hit the pad or key on your e-kit or MIDI keyboard (or click
     the drum in the plugin's window: the app first plays every note once and matches the click
     by sound), and the
     note is filled in and the next row selected.
   - **Find notes**: plays every note once, adds any the map lacks as "?" rows to listen to and
     name, and shows mapped rows that play nothing in red.
   - **Read plugin**: with Superior Drummer 3, reads the loaded kit straight from the plugin:
     every instrument slot, its articulations and their notes (main zones ticked). Other VST3
     plugins fill the "Name in plugin" column from their note names. **Open map...** also opens
     an SD3 preset (.sd3p) the same way.
     SD3 doesn't share its drums' brand and model names, so type them once in the Piece column:
     the app remembers each name by SD3's ID for that drum and fills it in for every kit that uses it.
   - **Save map...** / **Open map...**: keep a map per library as a .csv file
     (`piece,zone,note,cc4,record`), which a spreadsheet can edit too.
   Tom rimclicks and the snare rimclick have no Roland note, so their rows start unticked.
4. Set **Most hits per zone** (default 127, spread from velocity 127 down to 1) and **Record kit**.
   With **Fit Mimic limits** on, each piece records only as many hits as the Mimic can hold
   (kick 64, snare 300, toms 200, 2-zone cymbals 200, rides 300, shared across the piece's
   zones); the **Hits** column shows the count per row. Double-click a Hits cell to set a zone's
   count yourself (marked *); the piece's other ticked zones share what is left.
5. When the recording ends, the app builds the Mimic instruments straight away, into
   `<Save to folder>/<kit name>/Mimic/`.

**Convert WAV folder...** builds Mimic instruments from one or several folders (or a .zip) of WAV hits in
the layout below, such as a kit you recorded yourself or a WAV library, into a `Mimic`
folder inside it. **Also make hi-hats as 3-zone rides** adds a ride-type copy of each hi-hat.

Hits are recorded offline at 44.1 kHz, mixed to centred mono from the plugin's main output,
and saved as 24-bit WAVs under `<Save to folder>/<kit name>/` (Documents/eMIMICmkr unless you pick another with **Change...**), one folder per zone, in
the layout the converter reads (below). The hi-hat is recorded at five pedal positions
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

The app turns hits rendered by SDSE, a similar tool or its own batch recorder into Mimic
instruments, with no Python needed (`emk::WavToMci` in `Source/Core`). It reads a zip or a
folder laid out one folder per zone, with files named
`<piece> - <ZONE> - <hit>v<velocity>.wav`, and writes one `.mci` per kit piece. Several
folders given together (say a ride's `BOW`, `BELL` and `EDGE` folders, or several kit
pieces) are read as one input, their files named relative to the folder holding them all,
so they convert exactly like that folder would; a single zone folder converts on its own too.
A .zip among several inputs is skipped: convert it on its own.

Each zone's hits are sorted by velocity and split into velocity layers of about 4 to 6
round robins: 6 on snare heads and rims, 5 on tom heads and rims, 4 on kicks, sidesticks,
rimclicks and cymbals. That makes round(hits / round robins) layers, from 1 to 20; when a
zone has more than 6 hits per layer to fill, it keeps an even spread of velocities. Hi-hats
keep 8 layers of up to 32 hits.

Cymbals with a bell become 3-zone ride-type instruments, the rest 2-zone cymbals, and a
choke sound is made from the hardest edge hit when none was recorded. Kicks, snares and
toms are recognised too, and SDSE hi-hat folders become a Mimic hi-hat (add `--hat-as-ride` for
a 3-zone ride-type version as well). WAVs must be 44.1 kHz.

The converter also runs headless, from the `eMIMICmkr_mci_tests` console tool:

```
cmake --build build --target eMIMICmkr_mci_tests
build/eMIMICmkr_mci_tests_artefacts/Release/eMIMICmkr_mci_tests --convert "22 ZILDJIAN K Dark Medium Ride.zip" --out ~/Mimic
build/eMIMICmkr_mci_tests_artefacts/Release/eMIMICmkr_mci_tests --convert "My Kit Ride/BOW" "My Kit Ride/BELL" "My Kit Ride/EDGE"
```

Options: `--out DIR` (default: next to the input, or for several, the folder holding them all), `--name NAME` (only when the input holds
one piece), `--no-choke` (leave the Choke Ring empty) and `--hat-as-ride`. Run with no
arguments, the tool checks the codec and builders instead.

`tools/mci/wav2mci.py` stays as the reference implementation and takes the same options:

```
python3 tools/mci/wav2mci.py "22 ZILDJIAN K Dark Medium Ride.zip" --out ~/Mimic
```

`tests/test_cpp_converter.py` converts the same inputs with both and checks the files are
identical: metadata, RAM heads and compressed audio (so far byte for byte). Set
`EMIMICMKR_MCI_TESTS` to the tool's path when the build folder is not `./build`, and
`MCI_REAL=1` to include the real SDSE zips in `MCI_SAMPLES` (slow: the Python side takes
minutes per zip).

## Licensing

eMIMICmkr is GPLv3. JUCE 8's open-source licence is AGPLv3, so the app as a whole is
distributed under AGPLv3 terms unless a JUCE commercial licence is used.

Converted kits contain samples owned by the plugin maker. Check your plugin's licence
before using them anywhere but your own Mimic, and never share or sell them.
