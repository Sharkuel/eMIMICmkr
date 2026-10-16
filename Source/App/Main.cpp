#include "MainComponent.h"
#include <BinaryData.h>
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
};

START_JUCE_APPLICATION (EMimicApplication)
