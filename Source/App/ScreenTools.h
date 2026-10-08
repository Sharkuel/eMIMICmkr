#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Pictures and text from the screen and the clipboard, for naming kit pieces from what the
    plugin shows. Uses the system's own tools: Spectacle, grim, gnome-screenshot, scrot, maim or
    ImageMagick on Linux, screencapture and osascript on macOS, PowerShell on Windows, and
    Tesseract to read text. */
namespace ScreenTools
{
/** Every screen, as one picture. Blocks while the tool runs. */
juce::Image grabScreen (juce::String& error);

/** A picture copied to the clipboard, or an image file whose path or URL was copied (then
    source, when given, gets that file). */
juce::Image clipboardImage (juce::String& error, juce::File* source = nullptr);

/** The text in a picture, on one line. error says what's missing when Tesseract isn't installed. */
juce::String readText (const juce::Image&, juce::String& error);

/** The part of a picture of all screens that shows display, judging by the desktop layout. */
juce::Image displayPart (const juce::Image& allScreens, const juce::Displays::Display& display);

/** A full-screen view of a screenshot: drag a box around a picture or a name, then choose what
    it is for. Esc or a right-click closes it. */
class CaptureView final : public juce::Component
{
public:
    explicit CaptureView (juce::Image screenshot);

    /** A box was drawn: crop is the part of the screenshot in it, at is where the mouse let go. */
    std::function<void (const juce::Image& crop, juce::Point<int> at)> onBox;
    std::function<void()> onClose;
    void setMessage (const juce::String&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<int> imageArea() const;
    juce::Image shot;
    juce::Rectangle<int> box;
    juce::Point<int> start;
    juce::String message;
};
} // namespace ScreenTools
