#include "MainComponent.h"
#include <BinaryData.h>
#include <iostream>
#include "MockupExport.h"
#include "PluginScanner.h"
#include "SquareLook.h"

class EMimicApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    bool moreThanOneInstanceAllowed() override { return true; }   // plugin scans run in a child copy

    void initialise (const juce::String& commandLine) override
    {
        if (OutOfProcessScanner::runChildScanIfRequested (commandLine))
            return;
        juce::LookAndFeel::setDefaultLookAndFeel (&look);
        if (commandLine.contains ("--export-mockup"))
            return exportMockup (commandLine.fromFirstOccurrenceOf ("--export-mockup", false, false).trim().unquoted());
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // The banner, for a moment at start-up (a click closes it sooner). It deletes itself.
        const auto banner = juce::ImageCache::getFromMemory (BinaryData::banner_png, BinaryData::banner_pngSize);
        auto* splash = new juce::SplashScreen (getApplicationName(),
                                               banner.rescaled (banner.getWidth() / 2, banner.getHeight() / 2,
                                                                juce::Graphics::highResamplingQuality),
                                               true);
        splash->deleteAfterDelay (juce::RelativeTime::seconds (2.5), true);
    }
    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    void systemRequestedQuit() override { quit(); }

private:
    /** eMIMICmkr --export-mockup <folder>: saves the main window as a layered SVG and a PNG, to redesign it. */
    void exportMockup (const juce::String& folderName)
    {
        const auto folder = folderName.isEmpty() ? juce::File::getCurrentWorkingDirectory()
                                                 : juce::File::getCurrentWorkingDirectory().getChildFile (folderName);
        folder.createDirectory();
        mockup = std::make_unique<MainComponent>();
        // Let the table lay out its rows first.
        juce::Timer::callAfterDelay (800, [this, folder]
        {
            const auto svg = folder.getChildFile ("eMIMICmkr-main-window.svg");
            const auto png = folder.getChildFile ("eMIMICmkr-main-window.png");
            const bool ok = MockupExport::writeSvg (*mockup, svg) && MockupExport::writePng (*mockup, png);
            std::cout << (ok ? "Saved " + svg.getFullPathName() + " and " + png.getFileName() : juce::String ("Couldn't save the mockup"))
                      << std::endl;
            mockup = nullptr;
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
        });
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                                        .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setIcon (juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize));
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    SquareLook look;   // before mainWindow, so it outlives every window
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::Component> mockup;
};

START_JUCE_APPLICATION (EMimicApplication)
