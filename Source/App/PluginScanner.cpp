#include "PluginScanner.h"

namespace
{
const juce::String scanFlag = "--scan-plugin";
constexpr int timeoutMs = 300000;   // a big library loading through a bridge can take minutes
}

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
    while (child.isRunning())
    {
        if (shouldExit() || juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs)
        {
            child.kill();
            output.deleteFile();
            return shouldExit();   // a timeout counts as a crash (skipped); a cancel does not
        }
        juce::Thread::sleep (50);
    }

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
