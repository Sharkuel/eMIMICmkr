#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Saves a window as an editable design mockup, to redesign the interface in a vector editor
    (Affinity, Inkscape, Illustrator, Figma).

    Every component becomes a named group in the SVG ("Button: Record kit", "Tick box: Fit Editor
    limits"...), nested as on screen, holding its shapes as vectors and its words as text. Run the
    app with --export-mockup <folder> to write eMIMICmkr-main-window.svg and a 2x PNG beside it. */
namespace MockupExport
{
    /** Writes the component and its children as layered SVG. */
    bool writeSvg (juce::Component&, const juce::File& svgFile);

    /** Writes a screenshot of the component, at the given pixel scale. */
    bool writePng (juce::Component&, const juce::File& pngFile, float scale = 2.0f);
}
