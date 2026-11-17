#include "MockupExport.h"
#include <map>
#include <unordered_map>
#include <vector>
#if defined(__GNUC__)
 #include <cxxabi.h>
 #include <cstdlib>
#endif

namespace
{
juce::String num (float v)
{
    if (std::abs (v) < 0.005f)
        return "0";
    auto s = juce::String (v, 2);
    if (s.containsChar ('.'))
        s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s;
}

juce::String escape (const juce::String& s)
{
    return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
}

juce::String hex (juce::Colour c)
{
    return "#" + c.toDisplayString (false).toLowerCase();
}

juce::String matrix (const juce::AffineTransform& t)
{
    return "matrix(" + num (t.mat00) + " " + num (t.mat10) + " " + num (t.mat01) + " " + num (t.mat11) + " "
           + num (t.mat02) + " " + num (t.mat12) + ")";
}

bool keepsAxes (const juce::AffineTransform& t)
{
    return std::abs (t.mat01) < 1.0e-6f && std::abs (t.mat10) < 1.0e-6f;
}

float scaleOf (const juce::AffineTransform& t)
{
    return std::sqrt (std::abs (t.getDeterminant()));
}

juce::String pathData (const juce::Path& path, const juce::AffineTransform& t)
{
    juce::String d;
    auto pt = [&t] (float x, float y)
    {
        t.transformPoint (x, y);
        return num (x) + " " + num (y);
    };
    for (juce::Path::Iterator i (path); i.next();)
    {
        using E = juce::Path::Iterator;
        switch (i.elementType)
        {
            case E::startNewSubPath: d << "M" << pt (i.x1, i.y1); break;
            case E::lineTo:          d << "L" << pt (i.x1, i.y1); break;
            case E::quadraticTo:     d << "Q" << pt (i.x1, i.y1) << " " << pt (i.x2, i.y2); break;
            case E::cubicTo:         d << "C" << pt (i.x1, i.y1) << " " << pt (i.x2, i.y2) << " " << pt (i.x3, i.y3); break;
            case E::closePath:       d << "Z"; break;
        }
    }
    return d;
}

/** A JUCE graphics context that writes SVG elements instead of pixels, in window coordinates. */
class SvgContext final : public juce::LowLevelGraphicsContext
{
public:
    explicit SvgContext (juce::Rectangle<int> area)
    {
        states.push_back ({});
        states.back().clip = area.toFloat();
    }

    juce::MemoryOutputStream body;
    juce::MemoryOutputStream defs;
    int indent = 1;

    void openGroup (const juce::String& label)
    {
        auto id = label.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_")
                      .trim().replaceCharacter (' ', '-');
        if (id.isEmpty() || ! juce::CharacterFunctions::isLetter (id[0]))
            id = "g-" + id;
        const int n = ++idCounts[id];
        if (n > 1)
            id << "-" << n;
        line ("<g id=\"" + escape (id) + "\" serif:id=\"" + escape (label) + "\" inkscape:label=\"" + escape (label) + "\">");
        ++indent;
    }

    void closeGroup()
    {
        --indent;
        line ("</g>");
    }

    // LowLevelGraphicsContext
    bool isVectorDevice() const override { return true; }
    void setOrigin (juce::Point<int> o) override { addTransform (juce::AffineTransform::translation (o)); }
    void addTransform (const juce::AffineTransform& t) override { s().transform = t.followedBy (s().transform); }
    float getPhysicalPixelScaleFactor() const override { return 1.0f; }

    bool clipToRectangle (const juce::Rectangle<int>& r) override
    {
        s().clip = s().clip.getIntersection (r.toFloat().transformedBy (s().transform));
        return ! isClipEmpty();
    }
    bool clipToRectangleList (const juce::RectangleList<int>& r) override { return clipToRectangle (r.getBounds()); }
    void excludeClipRectangle (const juce::Rectangle<int>&) override {}
    void clipToPath (const juce::Path& p, const juce::AffineTransform& t) override
    {
        s().clip = s().clip.getIntersection (p.getBoundsTransformed (t.followedBy (s().transform)));
    }
    void clipToImageAlpha (const juce::Image& image, const juce::AffineTransform& t) override
    {
        s().clip = s().clip.getIntersection (image.getBounds().toFloat().transformedBy (t.followedBy (s().transform)));
    }
    bool clipRegionIntersects (const juce::Rectangle<int>& r) override
    {
        return s().clip.intersects (r.toFloat().transformedBy (s().transform));
    }
    juce::Rectangle<int> getClipBounds() const override
    {
        const auto& st = states.back();
        return st.clip.transformedBy (st.transform.inverted()).getSmallestIntegerContainer();
    }
    bool isClipEmpty() const override { return states.back().clip.isEmpty(); }

    void saveState() override { states.push_back (states.back()); }
    void restoreState() override
    {
        if (states.size() > 1)
            states.pop_back();
    }

    void beginTransparencyLayer (float opacity) override
    {
        line ("<g opacity=\"" + num (opacity) + "\">");
        ++indent;
        saveState();
    }
    void endTransparencyLayer() override
    {
        restoreState();
        closeGroup();
    }

    void setFill (const juce::FillType& f) override { s().fill = f; }
    void setOpacity (float o) override { s().fill.setOpacity (o); }
    void setInterpolationQuality (juce::Graphics::ResamplingQuality) override {}

    void fillRect (const juce::Rectangle<int>& r, bool) override { fillRect (r.toFloat()); }
    void fillRect (const juce::Rectangle<float>& r) override
    {
        if (! keepsAxes (s().transform))
        {
            juce::Path p;
            p.addRectangle (r);
            return fillPath (p, {});
        }
        const auto a = r.transformedBy (s().transform);
        shape ("<rect x=\"" + num (a.getX()) + "\" y=\"" + num (a.getY()) + "\" width=\"" + num (a.getWidth())
                   + "\" height=\"" + num (a.getHeight()) + "\"" + paint (true, a) + "/>", a);
    }
    void fillRectList (const juce::RectangleList<float>& list) override
    {
        for (auto& r : list)
            fillRect (r);
    }
    void fillPath (const juce::Path& p, const juce::AffineTransform& t) override
    {
        const auto all = t.followedBy (s().transform);
        const auto a = p.getBoundsTransformed (all);
        shape ("<path d=\"" + pathData (p, all) + "\"" + (p.isUsingNonZeroWinding() ? "" : " fill-rule=\"evenodd\"")
                   + paint (true, a) + "/>", a);
    }

    void strokePath (const juce::Path& p, const juce::PathStrokeType& type, const juce::AffineTransform& t) override
    {
        const auto all = t.followedBy (s().transform);
        const float w = type.getStrokeThickness() * scaleOf (all);
        const auto a = p.getBoundsTransformed (all).expanded (w * 0.5f);
        using P = juce::PathStrokeType;
        const juce::String join = type.getJointStyle() == P::mitered ? "miter" : type.getJointStyle() == P::curved ? "round" : "bevel";
        const juce::String cap = type.getEndStyle() == P::butt ? "butt" : type.getEndStyle() == P::square ? "square" : "round";
        shape ("<path d=\"" + pathData (p, all) + "\" fill=\"none\"" + paint (false, a) + " stroke-width=\"" + num (w)
                   + "\" stroke-linejoin=\"" + join + "\" stroke-linecap=\"" + cap + "\"/>", a);
    }

    void drawRect (const juce::Rectangle<float>& r, float thickness) override
    {
        if (! keepsAxes (s().transform))
            return LowLevelGraphicsContext::drawRect (r, thickness);
        const float w = thickness * scaleOf (s().transform);
        const auto a = r.transformedBy (s().transform).reduced (w * 0.5f);   // JUCE draws inside the rectangle
        shape ("<rect x=\"" + num (a.getX()) + "\" y=\"" + num (a.getY()) + "\" width=\"" + num (a.getWidth())
                   + "\" height=\"" + num (a.getHeight()) + "\" fill=\"none\"" + paint (false, a)
                   + " stroke-width=\"" + num (w) + "\"/>", a.expanded (w));
    }

    void fillRoundedRectangle (const juce::Rectangle<float>& r, float corner) override
    {
        roundedRect (r, corner, 0.0f);
    }
    void drawRoundedRectangle (const juce::Rectangle<float>& r, float corner, float thickness) override
    {
        roundedRect (r, corner, thickness);
    }
    void fillEllipse (const juce::Rectangle<float>& r) override { ellipse (r, 0.0f); }
    void drawEllipse (const juce::Rectangle<float>& r, float thickness) override { ellipse (r, thickness); }

    void drawImage (const juce::Image& image, const juce::AffineTransform& t) override
    {
        const auto all = t.followedBy (s().transform);
        const auto a = image.getBounds().toFloat().transformedBy (all);
        const float alpha = s().fill.getOpacity();
        shape ("<image width=\"" + juce::String (image.getWidth()) + "\" height=\"" + juce::String (image.getHeight())
                   + "\" transform=\"" + matrix (all) + "\"" + (alpha < 1.0f ? " opacity=\"" + num (alpha) + "\"" : juce::String())
                   + " preserveAspectRatio=\"none\" xlink:href=\"data:image/png;base64," + png (image) + "\"/>", a);
    }

    void drawLine (const juce::Line<float>& l) override { drawLineWithThickness (l, 1.0f); }
    void drawLineWithThickness (const juce::Line<float>& l, float thickness) override
    {
        juce::Path p;
        p.startNewSubPath (l.getStart());
        p.lineTo (l.getEnd());
        strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::mitered, juce::PathStrokeType::butt), {});
    }

    void setFont (const juce::Font& f) override { s().font = f; }
    const juce::Font& getFont() override { return s().font; }

    void drawGlyphs (juce::Span<const uint16_t> glyphs, juce::Span<const juce::Point<float>> positions,
                     const juce::AffineTransform& t) override
    {
        const auto& font = s().font;
        auto typeface = font.getTypefacePtr();
        if (typeface == nullptr)
            return;
        const auto& chars = charsOf (*typeface);
        const auto all = t.followedBy (s().transform);

        // One <text> per baseline, so a two-line label stays two editable lines.
        size_t start = 0;
        while (start < glyphs.size())
        {
            size_t end = start;
            juce::String text;
            while (end < glyphs.size() && std::abs (positions[end].y - positions[start].y) < 0.5f)
            {
                const auto found = chars.find (glyphs[end]);
                text << juce::String::charToString (found != chars.end() ? found->second : (juce::juce_wchar) '?');
                ++end;
            }
            textRun (text.trimEnd(), positions[start], positions[end - 1], font, all);
            start = end;
        }
    }

    std::unique_ptr<juce::ImageType> getPreferredImageTypeForTemporaryImages() const override
    {
        return std::make_unique<juce::SoftwareImageType>();
    }
    uint64_t getFrameId() const override { return 0; }

private:
    struct State
    {
        juce::AffineTransform transform;
        juce::Rectangle<float> clip;
        juce::FillType fill { juce::Colours::black };
        juce::Font font { juce::FontOptions() };
    };
    std::vector<State> states;
    std::map<juce::String, int> idCounts;
    std::map<juce::String, juce::String> clipIds;
    std::map<const juce::Typeface*, std::unordered_map<uint32_t, juce::juce_wchar>> glyphChars;
    std::map<juce::String, juce::String> pngCache;
    int gradients = 0;

    State& s() { return states.back(); }

    void line (const juce::String& text)
    {
        body << juce::String::repeatedString ("  ", indent) << text << "\n";
    }

    /** Writes an element, clipped to the current clip region when it reaches outside it. */
    void shape (juce::String element, juce::Rectangle<float> bounds)
    {
        const auto& clip = s().clip;
        if (! clip.intersects (bounds.expanded (0.01f)))
            return;
        if (! clip.expanded (0.5f).contains (bounds))
        {
            const auto key = num (clip.getX()) + " " + num (clip.getY()) + " " + num (clip.getWidth()) + " " + num (clip.getHeight());
            auto& id = clipIds[key];
            if (id.isEmpty())
            {
                id = "clip" + juce::String ((int) clipIds.size());
                defs << "    <clipPath id=\"" << id << "\"><rect x=\"" << num (clip.getX()) << "\" y=\"" << num (clip.getY())
                     << "\" width=\"" << num (clip.getWidth()) << "\" height=\"" << num (clip.getHeight()) << "\"/></clipPath>\n";
            }
            element = element.replaceFirstOccurrenceOf ("/>", " clip-path=\"url(#" + id + ")\"/>");
            element = element.replaceFirstOccurrenceOf ("><tspan", " clip-path=\"url(#" + id + ")\"><tspan");
        }
        line (element);
    }

    /** fill="..." or stroke="..." for the current fill, with its opacity. */
    juce::String paint (bool fill, juce::Rectangle<float> area)
    {
        const auto& f = s().fill;
        const juce::String attr = fill ? "fill" : "stroke";
        if (f.isGradient() && f.gradient != nullptr)
            return " " + attr + "=\"url(#" + gradient (*f.gradient, f.transform.followedBy (s().transform), f.getOpacity()) + ")\"";
        auto colour = f.isColour() ? f.colour : juce::Colours::grey;
        ignoreUnused (area);
        const float alpha = colour.getFloatAlpha() * f.getOpacity();
        return " " + attr + "=\"" + hex (colour) + "\"" + (alpha < 0.999f ? " " + attr + "-opacity=\"" + num (alpha) + "\"" : juce::String());
    }

    juce::String gradient (const juce::ColourGradient& g, const juce::AffineTransform& t, float opacity)
    {
        const auto id = "gradient" + juce::String (++gradients);
        if (g.isRadial)
            defs << "    <radialGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" cx=\"" << num (g.point1.x)
                 << "\" cy=\"" << num (g.point1.y) << "\" r=\"" << num (g.point1.getDistanceFrom (g.point2)) << "\"";
        else
            defs << "    <linearGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" x1=\"" << num (g.point1.x)
                 << "\" y1=\"" << num (g.point1.y) << "\" x2=\"" << num (g.point2.x) << "\" y2=\"" << num (g.point2.y) << "\"";
        defs << " gradientTransform=\"" << matrix (t) << "\">";
        for (int i = 0; i < g.getNumColours(); ++i)
        {
            const auto c = g.getColour (i);
            defs << "<stop offset=\"" << num ((float) g.getColourPosition (i)) << "\" stop-color=\"" << hex (c)
                 << "\" stop-opacity=\"" << num (c.getFloatAlpha() * opacity) << "\"/>";
        }
        defs << (g.isRadial ? "</radialGradient>\n" : "</linearGradient>\n");
        return id;
    }

    void roundedRect (juce::Rectangle<float> r, float corner, float thickness)
    {
        if (! keepsAxes (s().transform))
        {
            juce::Path p;
            p.addRoundedRectangle (r, corner);
            return thickness > 0.0f ? strokePath (p, juce::PathStrokeType (thickness), {}) : fillPath (p, {});
        }
        const float k = scaleOf (s().transform);
        const auto a = r.transformedBy (s().transform);
        const float rx = juce::jmin (corner * k, a.getWidth() * 0.5f, a.getHeight() * 0.5f);
        shape ("<rect x=\"" + num (a.getX()) + "\" y=\"" + num (a.getY()) + "\" width=\"" + num (a.getWidth())
                   + "\" height=\"" + num (a.getHeight()) + "\" rx=\"" + num (rx) + "\""
                   + (thickness > 0.0f ? " fill=\"none\"" + paint (false, a) + " stroke-width=\"" + num (thickness * k) + "\""
                                       : paint (true, a)) + "/>",
               a.expanded (thickness * k));
    }

    void ellipse (juce::Rectangle<float> r, float thickness)
    {
        if (! keepsAxes (s().transform))
        {
            juce::Path p;
            p.addEllipse (r);
            return thickness > 0.0f ? strokePath (p, juce::PathStrokeType (thickness), {}) : fillPath (p, {});
        }
        const float k = scaleOf (s().transform);
        const auto a = r.transformedBy (s().transform);
        shape ("<ellipse cx=\"" + num (a.getCentreX()) + "\" cy=\"" + num (a.getCentreY()) + "\" rx=\"" + num (a.getWidth() * 0.5f)
                   + "\" ry=\"" + num (a.getHeight() * 0.5f) + "\""
                   + (thickness > 0.0f ? " fill=\"none\"" + paint (false, a) + " stroke-width=\"" + num (thickness * k) + "\""
                                       : paint (true, a)) + "/>",
               a.expanded (thickness * k));
    }

    /** Which character each glyph of a typeface draws, for the letters a window shows. */
    const std::unordered_map<uint32_t, juce::juce_wchar>& charsOf (const juce::Typeface& typeface)
    {
        auto& map = glyphChars[&typeface];
        if (map.empty())
        {
            auto add = [&] (juce::juce_wchar from, juce::juce_wchar to)
            {
                for (auto c = from; c <= to; ++c)
                    if (auto g = typeface.getNominalGlyphForCodepoint (c))
                        map.emplace (*g, c);   // the first (plainest) character wins
            };
            add (0x20, 0x7e);
            add (0xa0, 0x17f);
            add (0x2010, 0x2027);
            add (0x2190, 0x21ff);
            add (0x2500, 0x27bf);
        }
        return map;
    }

    void textRun (const juce::String& text, juce::Point<float> first, juce::Point<float> last,
                  const juce::Font& font, const juce::AffineTransform& t)
    {
        if (text.trim().isEmpty())
            return;
        const float size = font.getHeight() * font.getTypefacePtr()->getMetrics (font.getMetricsKind()).heightToPoints;
        const float hScale = font.getHorizontalScale();
        auto pos = first.transformedBy (t);
        const auto end = last.transformedBy (t);
        const float k = scaleOf (t);
        const juce::Rectangle<float> bounds (pos.x, pos.y - font.getAscent() * k,
                                             juce::jmax (1.0f, end.x - pos.x + size * k * 0.6f), font.getHeight() * k);
        auto typeface = font.getTypefacePtr();
        juce::String attrs = " font-family=\"'" + escape (typeface->getName()) + "', sans-serif\" font-size=\"" + num (size)
                             + "\"" + (font.isBold() ? " font-weight=\"bold\"" : "") + (font.isItalic() ? " font-style=\"italic\"" : "")
                             + paint (true, bounds);
        juce::String place;
        auto local = t;
        if (keepsAxes (t) && std::abs (hScale - 1.0f) < 0.01f)
        {
            place = " x=\"" + num (pos.x) + "\" y=\"" + num (pos.y) + "\"";
            attrs = attrs.replace ("font-size=\"" + num (size) + "\"", "font-size=\"" + num (size * k) + "\"");
        }
        else
        {
            local = juce::AffineTransform::scale (hScale, 1.0f).translated (first).followedBy (t);
            place = " transform=\"" + matrix (local) + "\"";
        }
        shape ("<text xml:space=\"preserve\"" + place + attrs + "><tspan>" + escape (text) + "</tspan></text>", bounds);
    }

    juce::String png (const juce::Image& image)
    {
        juce::MemoryOutputStream data;
        juce::PNGImageFormat().writeImageToStream (image, data);
        const auto key = juce::String::toHexString (data.getData(), (int) juce::jmin ((size_t) 64, data.getDataSize()))
                         + juce::String ((juce::int64) data.getDataSize());
        auto& encoded = pngCache[key];
        if (encoded.isEmpty())
            encoded = juce::Base64::toBase64 (data.getData(), data.getDataSize());
        return encoded;
    }
};

juce::String typeName (const juce::Component& c)
{
    juce::String name = typeid (c).name();
   #if defined(__GNUC__)
    int status = 0;
    if (char* plain = abi::__cxa_demangle (name.toRawUTF8(), nullptr, nullptr, &status))
    {
        name = plain;
        std::free (plain);
    }
   #endif
    return name.fromLastOccurrenceOf ("::", false, false).fromLastOccurrenceOf (" ", false, false);
}

/** The layer name a designer sees: what the control is, then its words. */
juce::String describe (const juce::Component& c)
{
    auto with = [] (const juce::String& kind, const juce::String& words)
    {
        const auto w = words.replaceCharacters ("\r\n", "  ").trim();
        return w.isEmpty() ? kind : kind + ": " + w.substring (0, 40);
    };
    const auto own = c.getName();
    if (auto* t = dynamic_cast<const juce::ToggleButton*> (&c))     return with ("Tick box", t->getButtonText().isNotEmpty() ? t->getButtonText() : own);
    if (auto* b = dynamic_cast<const juce::Button*> (&c))           return with ("Button", b->getButtonText().isNotEmpty() ? b->getButtonText() : own);
    if (auto* x = dynamic_cast<const juce::ComboBox*> (&c))         return with ("Drop-down", own.isNotEmpty() ? own : x->getText());
    if (auto* e = dynamic_cast<const juce::TextEditor*> (&c))       return with ("Text box", own.isNotEmpty() ? own : e->getText());
    if (auto* l = dynamic_cast<const juce::Label*> (&c))            return with ("Label", l->getText().isNotEmpty() ? l->getText() : own);
    if (dynamic_cast<const juce::TableHeaderComponent*> (&c) != nullptr) return "Table header";
    if (dynamic_cast<const juce::ScrollBar*> (&c) != nullptr)       return "Scroll bar";
    if (dynamic_cast<const juce::ListBox*> (&c) != nullptr)         return with ("Table", own);
    return own.isNotEmpty() ? own : typeName (c);
}

/** JUCE's inner helper components, which would only be clutter in a layers panel. */
bool isPlumbing (const juce::Component& c)
{
    static const juce::StringArray names { "TextEditorViewport", "AccessibilityIgnoredComponent", "TextHolderComponent",
                                           "IgnoredComponent", "ListViewport", "RowComponent" };
    return c.getName().isEmpty() && names.contains (typeName (c));
}

void paintTree (juce::Graphics& g, SvgContext& svg, juce::Component& c)
{
    const bool group = ! isPlumbing (c);
    if (group)
    {
        auto label = describe (c);
        const auto type = typeName (c);
        if (type == "RowComp")
            label = "Row";
        if (type == "EditableCell")
            label = label.replace ("Label", "Cell");
        if (type == "ImageCell")
            label = "Image cell";
        svg.openGroup (label);
    }
    const bool faded = c.getAlpha() < 1.0f;
    if (faded)
        g.beginTransparencyLayer (c.getAlpha());

    c.paint (g);
    for (auto* child : c.getChildren())
    {
        if (! child->isVisible())
            continue;
        juce::Graphics::ScopedSaveState keep (g);
        if (g.reduceClipRegion (child->getBounds()))
        {
            g.setOrigin (child->getPosition());
            paintTree (g, svg, *child);
        }
    }
    c.paintOverChildren (g);

    if (faded)
        g.endTransparencyLayer();
    if (group)
        svg.closeGroup();
}
} // namespace

bool MockupExport::writeSvg (juce::Component& root, const juce::File& file)
{
    SvgContext svg (root.getLocalBounds());
    {
        juce::Graphics g (svg);
        paintTree (g, svg, root);
    }
    const auto w = juce::String (root.getWidth()), h = juce::String (root.getHeight());
    juce::MemoryOutputStream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!-- " << escape (describe (root)) << ", exported by eMIMICmkr's mockup export. Every control is a named group. -->\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\""
        << " xmlns:serif=\"http://www.serif.com/\" xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\""
        << " width=\"" << w << "\" height=\"" << h << "\" viewBox=\"0 0 " << w << " " << h << "\">\n";
    if (svg.defs.getDataSize() > 0)
        out << "  <defs>\n" << svg.defs.toString() << "  </defs>\n";
    out << svg.body.toString() << "</svg>\n";
    return file.replaceWithText (out.toString(), false, false, "\n");
}

bool MockupExport::writePng (juce::Component& root, const juce::File& file, float scale)
{
    const auto image = root.createComponentSnapshot (root.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
}
