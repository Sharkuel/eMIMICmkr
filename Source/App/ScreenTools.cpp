#include "ScreenTools.h"

namespace ScreenTools
{
namespace
{
juce::File findProgram (const juce::String& name)
{
    juce::StringArray folders;
    folders.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}),
                       juce::File::getSeparatorChar() == '\\' ? ";" : ":", "");
    folders.addArray ({ "/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "C:\\Program Files\\Tesseract-OCR",
                        "C:\\Program Files (x86)\\Tesseract-OCR" });
    for (const auto& folder : folders)
        if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder))
            for (const auto& file : { juce::File (folder).getChildFile (name), juce::File (folder).getChildFile (name + ".exe") })
                if (file.existsAsFile())
                    return file;
    return {};
}

/** Runs a program and returns what it wrote out, byte for byte; empty when it failed. */
juce::MemoryBlock runForOutput (juce::StringArray args, int timeoutMs, bool* ok = nullptr)
{
    juce::MemoryBlock out;
    if (ok != nullptr)
        *ok = false;
    const auto program = juce::File::isAbsolutePath (args[0]) ? juce::File (args[0]) : findProgram (args[0]);
    if (program == juce::File())
        return out;
    args.set (0, program.getFullPathName());
    juce::ChildProcess process;
    if (! process.start (args, juce::ChildProcess::wantStdOut))
        return out;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    char buffer[8192];
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        const int n = process.readProcessOutput (buffer, sizeof (buffer));
        if (n > 0)
            out.append (buffer, (size_t) n);
        else if (! process.isRunning())
            break;
        else
            juce::Thread::sleep (5);
    }
    if (process.isRunning())
    {
        process.kill();
        return {};
    }
    if (ok != nullptr)
        *ok = process.getExitCode() == 0;
    return out;
}

juce::Image loadFile (const juce::File& file)
{
    return file.existsAsFile() ? juce::ImageFileFormat::loadFrom (file) : juce::Image();
}

#if JUCE_WINDOWS
/** A PowerShell script, passed encoded so no quoting gets in the way. */
juce::StringArray powerShell (const juce::String& script, bool sta)
{
    juce::MemoryBlock utf16;
    for (auto c : script)
    {
        const auto u = (juce::uint16) c;   // the scripts are plain ASCII
        utf16.append (&u, 2);
    }
    juce::StringArray args { "powershell", "-NoProfile", "-NonInteractive" };
    if (sta)
        args.add ("-STA");
    args.addArray ({ "-EncodedCommand", juce::Base64::toBase64 (utf16.getData(), utf16.getSize()) });
    return args;
}

juce::String quoted (const juce::File& f)
{
    return "'" + f.getFullPathName().replace ("'", "''") + "'";
}
#endif
} // namespace

juce::Image grabScreen (juce::String& error)
{
    const juce::TemporaryFile temp (".png");
    const auto file = temp.getFile();
    const auto path = file.getFullPathName();
    std::vector<juce::StringArray> tries;
   #if JUCE_WINDOWS
    tries.push_back (powerShell ("Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public class Dpi { "
                                 "[DllImport(\"user32.dll\")] public static extern bool SetProcessDPIAware(); }'; "
                                 "[Dpi]::SetProcessDPIAware() | Out-Null; "
                                 "Add-Type -AssemblyName System.Windows.Forms, System.Drawing; "
                                 "$b = [System.Windows.Forms.SystemInformation]::VirtualScreen; "
                                 "$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height; "
                                 "$g = [System.Drawing.Graphics]::FromImage($bmp); "
                                 "$g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size); "
                                 "$bmp.Save(" + quoted (file) + ", [System.Drawing.Imaging.ImageFormat]::Png)", false));
   #elif JUCE_MAC
    tries.push_back ({ "screencapture", "-x", path });
   #else
    tries.push_back ({ "spectacle", "--background", "--nonotify", "--fullscreen", "--output", path });
    tries.push_back ({ "grim", path });
    tries.push_back ({ "gnome-screenshot", "-f", path });
    tries.push_back ({ "scrot", path });
    tries.push_back ({ "maim", path });
    tries.push_back ({ "import", "-window", "root", path });
   #endif
    juce::StringArray tried;
    for (const auto& args : tries)
    {
        if (findProgram (args[0]) == juce::File())
            continue;
        tried.add (args[0]);
        file.deleteFile();
        runForOutput (args, 20000);
        if (auto image = loadFile (file); image.isValid())
            return image;
    }
    error = tried.isEmpty() ? juce::String ("No screenshot tool found. Install Spectacle, grim, gnome-screenshot, scrot, maim "
                                            "or ImageMagick.")
                            : "Taking a screenshot with " + tried.joinIntoString (", ") + " didn't work.";
    return {};
}

juce::Image clipboardImage (juce::String& error)
{
    const juce::TemporaryFile temp (".png");
    const auto file = temp.getFile();
   #if JUCE_WINDOWS
    runForOutput (powerShell ("Add-Type -AssemblyName System.Windows.Forms, System.Drawing; "
                              "$i = [System.Windows.Forms.Clipboard]::GetImage(); "
                              "if ($i) { $i.Save(" + quoted (file) + ", [System.Drawing.Imaging.ImageFormat]::Png) }", true),
                  10000);
    if (auto image = loadFile (file); image.isValid())
        return image;
   #elif JUCE_MAC
    runForOutput ({ "osascript", "-e", "set f to open for access (POSIX file \"" + file.getFullPathName() + "\") with write permission",
                    "-e", juce::String (juce::CharPointer_UTF8 ("write (the clipboard as \xc2\xab" "class PNGf\xc2\xbb) to f")), "-e", "close access f" }, 10000);
    if (auto image = loadFile (file); image.isValid())
        return image;
   #else
    for (const auto& args : { juce::StringArray { "wl-paste", "--no-newline", "--type", "image/png" },
                              juce::StringArray { "xclip", "-selection", "clipboard", "-t", "image/png", "-o" } })
    {
        bool ok = false;
        const auto data = runForOutput (args, 5000, &ok);
        if (ok && data.getSize() > 0)
            if (auto image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize()); image.isValid())
                return image;
    }
   #endif
    // A copied image file: its path, or its file:// URL from a file manager.
    for (auto line : juce::StringArray::fromLines (juce::SystemClipboard::getTextFromClipboard()))
    {
        line = line.trim();
        const auto f = line.startsWith ("file://") ? juce::URL (line).getLocalFile()
                     : juce::File::isAbsolutePath (line) ? juce::File (line) : juce::File();
        if (auto image = loadFile (f); image.isValid())
            return image;
    }
    error = "There's no picture on the clipboard.";
   #if JUCE_LINUX
    if (findProgram ("wl-paste") == juce::File() && findProgram ("xclip") == juce::File())
        error << " Pasting pictures needs wl-clipboard (Wayland) or xclip (X11) installed.";
   #endif
    return {};
}

juce::String readText (const juce::Image& picture, juce::String& error)
{
    const auto tesseract = findProgram ("tesseract");
    if (tesseract == juce::File())
    {
        error = "Reading text needs Tesseract OCR: install the tesseract package (Linux), "
                "\"brew install tesseract\" (macOS) or the UB Mannheim installer (Windows), then try again.";
        return {};
    }
    if (! picture.isValid())
        return {};

    // Tesseract reads dark letters on white, about 30 pixels tall: scale up, grey, and
    // turn light-on-dark plugin text around.
    const int scale = juce::jlimit (1, 4, 120 / juce::jmax (1, picture.getHeight()) + 1);
    const int pad = 12;
    juce::Image big (juce::Image::RGB, picture.getWidth() * scale + 2 * pad, picture.getHeight() * scale + 2 * pad, true);
    double sum = 0.0;
    for (int y = 0; y < picture.getHeight(); ++y)
        for (int x = 0; x < picture.getWidth(); ++x)
            sum += picture.getPixelAt (x, y).getPerceivedBrightness();
    const bool dark = sum / juce::jmax (1, picture.getWidth() * picture.getHeight()) < 0.5;
    {
        juce::Graphics g (big);
        g.fillAll (dark ? juce::Colours::black : juce::Colours::white);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (picture, pad, pad, picture.getWidth() * scale, picture.getHeight() * scale,
                     0, 0, picture.getWidth(), picture.getHeight());
    }
    for (int y = 0; y < big.getHeight(); ++y)
        for (int x = 0; x < big.getWidth(); ++x)
        {
            auto v = big.getPixelAt (x, y).getPerceivedBrightness();
            if (dark)
                v = 1.0f - v;
            big.setPixelAt (x, y, juce::Colour::greyLevel (v));
        }

    const juce::TemporaryFile temp (".png");
    {
        juce::FileOutputStream out (temp.getFile());
        juce::PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (big, out))
        {
            error = "Couldn't write a temporary file for Tesseract.";
            return {};
        }
    }
    bool ok = false;
    const auto output = runForOutput ({ tesseract.getFullPathName(), temp.getFile().getFullPathName(), "stdout", "--psm", "6", "tsv" },
                                      30000, &ok);
    if (! ok)
    {
        error = "Tesseract couldn't read that picture.";
        return {};
    }
    // One word per row with how sure Tesseract is: drop the shaky ones, which are mostly the
    // plugin's arrows and icons read as letters.
    juce::StringArray words;
    for (const auto& row : juce::StringArray::fromLines (juce::String::fromUTF8 ((const char*) output.getData(), (int) output.getSize())))
    {
        const auto cells = juce::StringArray::fromTokens (row, "\t", "");
        if (cells.size() < 12 || cells[0] != "5")
            continue;
        const auto word = cells[11].trim();
        const bool plain = word.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
                           || word == "-" || word == "&" || word == "/";
        if (word.isNotEmpty() && plain && cells[10].getFloatValue() >= 55.0f)
            words.add (word);
    }
    auto text = words.joinIntoString (" ");
    while (text.contains ("  "))
        text = text.replace ("  ", " ");
    return text.replace (juce::CharPointer_UTF8 ("\xe2\x80\x9d"), "\"").replace (juce::CharPointer_UTF8 ("\xe2\x80\x9c"), "\"")
               .replace (juce::CharPointer_UTF8 ("\xe2\x80\x99"), "'").trim();
}

juce::Image displayPart (const juce::Image& shot, const juce::Displays::Display& display)
{
    juce::Rectangle<int> all;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
        all = all.getUnion (d.totalArea);
    if (! shot.isValid() || all.isEmpty())
        return shot;
    const double sx = shot.getWidth() / (double) all.getWidth(), sy = shot.getHeight() / (double) all.getHeight();
    if (std::abs (sx / sy - 1.0) > 0.05)   // not a picture of this layout: show it whole
        return shot;
    const auto area = (display.totalArea - all.getPosition()).toDouble();
    const auto pixels = juce::Rectangle<double> (area.getX() * sx, area.getY() * sy, area.getWidth() * sx, area.getHeight() * sy)
                            .getSmallestIntegerContainer()
                            .getIntersection (shot.getBounds());
    return pixels.isEmpty() ? shot : shot.getClippedImage (pixels).createCopy();
}

//==============================================================================
CaptureView::CaptureView (juce::Image screenshot) : shot (std::move (screenshot))
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    message = "Drag a box around a kit piece's picture or name, then choose what it is. Esc or right-click when done.";
}

void CaptureView::setMessage (const juce::String& text)
{
    message = text;
    repaint();
}

juce::Rectangle<int> CaptureView::imageArea() const
{
    if (! shot.isValid())
        return {};
    return juce::RectanglePlacement (juce::RectanglePlacement::centred)
        .appliedTo (shot.getBounds().toFloat(), getLocalBounds().toFloat())
        .toNearestInt();
}

void CaptureView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const auto area = imageArea();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (shot, area.toFloat());

    // Dim everything but the box.
    juce::RectangleList<int> dim (getLocalBounds());
    dim.subtract (box);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRectList (dim);
    if (! box.isEmpty())
    {
        g.setColour (juce::Colours::deepskyblue);
        g.drawRect (box.expanded (1), 2);
    }

    auto banner = getLocalBounds().removeFromTop (44).reduced (getWidth() / 6, 6);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRoundedRectangle (banner.toFloat(), 6.0f);
    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText (message, banner.reduced (10, 0), juce::Justification::centred, 2);
}

void CaptureView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onClose)
            onClose();
        return;
    }
    start = e.getPosition();
    box = {};
    repaint();
}

void CaptureView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    box = juce::Rectangle<int> (start, e.getPosition()).getIntersection (imageArea());
    repaint();
}

void CaptureView::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || box.getWidth() < 4 || box.getHeight() < 4)
        return;
    const auto area = imageArea();
    const double scale = shot.getWidth() / (double) juce::jmax (1, area.getWidth());
    const auto rel = (box - area.getPosition()).toDouble();
    const auto pixels = juce::Rectangle<double> (rel.getX() * scale, rel.getY() * scale, rel.getWidth() * scale, rel.getHeight() * scale)
                            .getSmallestIntegerContainer()
                            .getIntersection (shot.getBounds());
    if (! pixels.isEmpty() && onBox)
        onBox (shot.getClippedImage (pixels).createCopy(), e.getPosition());
}

bool CaptureView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
} // namespace ScreenTools
