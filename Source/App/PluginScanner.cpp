#include "PluginScanner.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
const juce::String scanFlag = "--scan-plugin";

/** A small always-on-top window, shown while one plugin is slow to open, with a Skip button. */
class SkipWindow final : public juce::DocumentWindow
{
public:
    SkipWindow (const juce::String& plugin, std::shared_ptr<std::atomic<bool>> skip)
        : DocumentWindow ("Slow plugin", juce::Colours::darkgrey, 0)
    {
        auto* content = new juce::Component();
        message.setText ("Still opening " + plugin + "...\nIt is skipped automatically after "
                             + juce::String (OutOfProcessScanner::timeoutSeconds) + " seconds.",
                         juce::dontSendNotification);
        message.setJustificationType (juce::Justification::centred);
        button.onClick = [skip] { skip->store (true); };
        content->addAndMakeVisible (message);
        content->addAndMakeVisible (button);
        content->setSize (380, 110);
        message.setBounds (10, 8, 360, 56);
        button.setBounds (140, 70, 100, 28);
        setContentOwned (content, true);
        setUsingNativeTitleBar (true);
        setAlwaysOnTop (true);
        centreWithSize (380, 110);
        setVisible (true);
    }

private:
    juce::Label message;
    juce::TextButton button { "Skip plugin" };
};
} // namespace

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                              juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    const auto output = juce::File::createTempFile (".xml");
    juce::ChildProcess child;
    const juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                      scanFlag, format.getName(), fileOrIdentifier, output.getFullPathName() };
    if (! child.start (command, 0))
        return false;

    const auto start = juce::Time::getMillisecondCounter();
    auto skip = std::make_shared<std::atomic<bool>> (false);
    auto window = std::make_shared<std::unique_ptr<SkipWindow>>();
    bool offered = false;
    const auto name = juce::File::createFileWithoutCheckingPath (fileOrIdentifier).getFileNameWithoutExtension();
    auto closeWindow = [window] { juce::MessageManager::callAsync ([window] { window->reset(); }); };

    while (child.isRunning())
    {
        const auto elapsed = juce::Time::getMillisecondCounter() - start;
        if (! offered && elapsed > (juce::uint32) skipOfferSeconds * 1000)
        {
            offered = true;
            juce::MessageManager::callAsync ([window, name, skip] { *window = std::make_unique<SkipWindow> (name, skip); });
        }
        if (shouldExit() || skip->load() || elapsed > (juce::uint32) timeoutSeconds * 1000)
        {
            child.kill();
            output.deleteFile();
            closeWindow();
            return shouldExit();   // skipped or timed out: on the skip list; a cancelled scan: not
        }
        juce::Thread::sleep (50);
    }
    if (offered)
        closeWindow();

    auto xml = juce::parseXML (output);
    output.deleteFile();
    if (xml == nullptr || ! xml->hasTagName ("SCAN"))
        return false;   // the child crashed before writing its answer
    for (auto* e : xml->getChildIterator())
    {
        auto desc = std::make_unique<juce::PluginDescription>();
        if (desc->loadFromXml (*e))
            result.add (desc.release());
    }
    return true;
}

bool OutOfProcessScanner::runChildScanIfRequested (const juce::String& commandLine)
{
    auto args = juce::StringArray::fromTokens (commandLine, true);
    args.trim();
    for (auto& a : args)
        a = a.unquoted();
    const int at = args.indexOf (scanFlag);
    if (at < 0)
        return false;

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();
    juce::XmlElement answer ("SCAN");
    for (auto* format : formats.getFormats())
        if (format->getName() == args[at + 1])
        {
            juce::OwnedArray<juce::PluginDescription> types;
            format->findAllTypesForFile (types, args[at + 2]);
            for (auto* t : types)
                answer.addChildElement (t->createXml().release());
        }
    juce::File (args[at + 3]).replaceWithText (answer.toString());
    std::_Exit (0);   // some plugins crash while unloading; the answer is already written
}
