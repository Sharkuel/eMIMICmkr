# Interface mockups

Editable copies of the main window, to redesign eMIMICmkr's interface in a vector editor
(Affinity Designer/Studio, Inkscape, Illustrator, Figma).

| File | What it is |
| --- | --- |
| `eMIMICmkr-main-window.svg` | The window as vectors. Every control is its own named group ("Button: Record kit", "Tick box: Fit Editor limits", "Drop-down: Note map", "Row" > "Cell: KICK1"...), nested as on screen. Shapes are rectangles and paths, words are live text (Liberation Sans), the logo is an embedded image. Open this one. |
| `eMIMICmkr-main-window.pdf` | The same drawing as PDF. Still vectors and text, but without the layer names. |
| `eMIMICmkr-main-window.png` | A screenshot of the real window at 2x, for reference. |

Make them again after the interface changes (the window is drawn by the app itself, so the files always match it):

```
eMIMICmkr --export-mockup design
```

The table shows the map saved in your settings (or the e-drum map on a fresh install).
