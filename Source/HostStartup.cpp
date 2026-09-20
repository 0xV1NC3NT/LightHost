#include <JuceHeader.h>
#include "IconMenu.h"

#if !(JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST3 and/or AU support"
#endif

class PluginHostApp final : public JUCEApplication
{
public:
    PluginHostApp() = default;

    void initialise(const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(options);

        appProperties = std::make_unique<ApplicationProperties>();
        appProperties->setStorageParameters(options);

        LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

        mainWindow = std::make_unique<IconMenu>();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override { return "Light Host"; }
    const String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override
    {
        return getParameter("-multi-instance").size() == 2;
    }

    ApplicationCommandManager commandManager;
    std::unique_ptr<ApplicationProperties> appProperties;
    LookAndFeel_V4 lookAndFeel { LookAndFeel_V4::getLightColourScheme() };

private:
    std::unique_ptr<IconMenu> mainWindow;

    StringArray getParameter(const String& lookFor) const
    {
        StringArray found;
        for (auto& param : getCommandLineParameterArray())
        {
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                const int delimiter = param.indexOfChar('=') + 1;
                found.add(param.substring(delimiter));
                return found;
            }
        }
        return found;
    }

    void checkArguments(PropertiesFile::Options& options) const
    {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options.filenameSuffix = multiInstance[1] + "." + options.filenameSuffix;
    }
};

static PluginHostApp& getApp() { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationCommandManager& getCommandManager() { return getApp().commandManager; }
ApplicationProperties& getAppProperties() { return *getApp().appProperties; }

START_JUCE_APPLICATION(PluginHostApp)
