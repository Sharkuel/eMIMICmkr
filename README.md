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
| Plugin hosting | Skeleton app: find plugins, load one, show its window, render a test hit to WAV |
| Kit auto-mapping | General MIDI fallback map only |
| Multi-velocity recording | Single hits work, batch recording not yet |
| .mci export from the app | Done in Python (`tools/mci`), C++ port not yet |

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

## The .mci tools

`tools/mci/mci.py` reads, writes and builds Mimic instruments, with no dependencies
beyond Python 3. To run its tests against your own Editor files:

```
pip install pytest
MCI_SAMPLES=/path/to/folder/with/mci python3 -m pytest tests
```

## Licensing

eMIMICmkr is GPLv3. JUCE 8's open-source licence is AGPLv3, so the app as a whole is
distributed under AGPLv3 terms unless a JUCE commercial licence is used.

Converted kits contain samples owned by the plugin maker. Check your plugin's licence
before using them anywhere but your own Mimic, and never share or sell them.
