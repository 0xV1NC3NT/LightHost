//
//  ControlPanelWindow.cpp
//  Light Host
//

#include "ControlPanelWindow.h"
#include "IconMenu.h"

namespace
{
    constexpr int sidebarWidth = 140;
    constexpr int tabButtonHeight = 34;
}

//==============================================================================
class PluginsPanel final : public Component
{
public:
    explicit PluginsPanel(IconMenu& ownerToUse) : owner(ownerToUse), pluginList(ownerToUse)
    {
        editPluginsButton.setButtonText(String::fromUTF8("\xF0\x9F\x93\x8B Edit Plugin List..."));
        editPluginsButton.onClick = [this] { owner.openPluginScanner(); };
        addAndMakeVisible(editPluginsButton);

        addPluginButton.setButtonText(String::fromUTF8("\xE2\x9E\x95 Add Plugin..."));
        addPluginButton.onClick = [this] { showAddPluginMenu(); };
        addAndMakeVisible(addPluginButton);

        deleteStatesButton.setButtonText(String::fromUTF8("\xF0\x9F\x97\x91 Delete Plugin States"));
        deleteStatesButton.onClick = [this] { owner.deleteAllPluginStates(); };
        addAndMakeVisible(deleteStatesButton);

        activeLabel.setText("Active Plugins (drag to reorder)", dontSendNotification);
        activeLabel.setColour(Label::textColourId, Colours::black);
        addAndMakeVisible(activeLabel);

        viewport.setViewedComponent(&pluginList, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    void refresh()
    {
        pluginList.setPlugins(owner.buildChainEntries());
        updateViewportContentSize();
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto row1 = area.removeFromTop(28);
        editPluginsButton.setBounds(row1.removeFromLeft(140));
        row1.removeFromLeft(6);
        addPluginButton.setBounds(row1.removeFromLeft(110));
        row1.removeFromLeft(6);
        deleteStatesButton.setBounds(row1.removeFromLeft(150));

        area.removeFromTop(10);
        activeLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(4);

        viewport.setBounds(area);
        updateViewportContentSize();
    }

private:
    void updateViewportContentSize()
    {
        pluginList.setSize(viewport.getMaximumVisibleWidth(),
            jmax(pluginList.getPreferredHeight(), viewport.getMaximumVisibleHeight()));
    }

    void showAddPluginMenu()
    {
        PopupMenu m;
        owner.getKnownPluginList().addToMenu(m, KnownPluginList::sortByManufacturer);
        m.showMenuAsync(PopupMenu::Options().withTargetComponent(&addPluginButton),
            [this](int result)
            {
                if (result <= 0)
                    return;
                const int idx = owner.getKnownPluginList().getIndexChosenByMenu(result);
                if (idx > -1)
                    owner.addPlugin(*owner.getKnownPluginList().getType(idx));
            });
    }

    IconMenu& owner;
    TextButton editPluginsButton, addPluginButton, deleteStatesButton;
    Label activeLabel;
    Viewport viewport;
    PluginChainListBox pluginList;
};

//==============================================================================
class ConfigPanel final : public Component
{
public:
    explicit ConfigPanel(IconMenu& ownerToUse) : owner(ownerToUse)
    {
        configLabel.setText("Config:", dontSendNotification);
        configLabel.setColour(Label::textColourId, Colours::black);
        addAndMakeVisible(configLabel);

        configCombo.setTextWhenNothingSelected("(none saved)");
        addAndMakeVisible(configCombo);

        loadConfigButton.setButtonText(String::fromUTF8("\xF0\x9F\x93\x82 Load"));
        loadConfigButton.onClick = [this]
        {
            const String name = configCombo.getText();
            if (name.isNotEmpty())
            {
                owner.loadConfig(name);
                refreshConfigList(name);
            }
        };
        addAndMakeVisible(loadConfigButton);

        saveConfigButton.setButtonText(String::fromUTF8("\xF0\x9F\x92\xBE Save As..."));
        saveConfigButton.onClick = [this] { showSaveConfigDialog(); };
        addAndMakeVisible(saveConfigButton);

        deleteConfigButton.setButtonText(String::fromUTF8("\xF0\x9F\x97\x91 Delete"));
        deleteConfigButton.onClick = [this]
        {
            const String name = configCombo.getText();
            if (name.isNotEmpty())
            {
                owner.deleteConfig(name);
                refreshConfigList();
            }
        };
        addAndMakeVisible(deleteConfigButton);

        exportConfigButton.setButtonText(String::fromUTF8("\xF0\x9F\x93\xA4 Export..."));
        exportConfigButton.onClick = [this] { showExportConfigDialog(); };
        addAndMakeVisible(exportConfigButton);

        importConfigButton.setButtonText(String::fromUTF8("\xF0\x9F\x93\xA5 Import..."));
        importConfigButton.onClick = [this] { showImportConfigDialog(); };
        addAndMakeVisible(importConfigButton);

        refreshConfigList();
    }

    void refresh() { refreshConfigList(); }

    void resized() override
    {
        auto area = getLocalBounds();

        auto row1 = area.removeFromTop(26);
        configLabel.setBounds(row1.removeFromLeft(48));
        row1.removeFromLeft(6);
        configCombo.setBounds(row1);

        area.removeFromTop(8);
        auto row2 = area.removeFromTop(26);
        loadConfigButton.setBounds(row2.removeFromLeft(70));
        row2.removeFromLeft(6);
        saveConfigButton.setBounds(row2.removeFromLeft(90));
        row2.removeFromLeft(6);
        deleteConfigButton.setBounds(row2.removeFromLeft(70));

        area.removeFromTop(6);
        auto row3 = area.removeFromTop(26);
        exportConfigButton.setBounds(row3.removeFromLeft(80));
        row3.removeFromLeft(6);
        importConfigButton.setBounds(row3.removeFromLeft(80));
    }

private:
    void refreshConfigList(const String& nameToSelect = {})
    {
        String target = nameToSelect;
        if (target.isEmpty())
            target = configCombo.getText();
        if (target.isEmpty())
            target = owner.getCurrentConfigName();

        configCombo.clear();
        auto names = owner.listConfigNames();
        for (int i = 0; i < names.size(); ++i)
            configCombo.addItem(names[i], i + 1);

        const int targetId = names.indexOf(target) + 1;
        if (targetId > 0)
            configCombo.setSelectedId(targetId, dontSendNotification);
    }

    void showSaveConfigDialog()
    {
        auto* aw = new AlertWindow("Save Config", "Enter a name for this configuration:", MessageBoxIconType::NoIcon);
        aw->addTextEditor("name", configCombo.getText(), "Name:");
        aw->addButton("Save", 1, KeyPress(KeyPress::returnKey));
        aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));

        aw->enterModalState(true, ModalCallbackFunction::create([this, aw](int result)
        {
            if (result == 1)
            {
                const String name = aw->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                {
                    owner.saveConfigAs(name);
                    refreshConfigList(name);
                }
            }
        }), true);
    }

    void showExportConfigDialog()
    {
        const String name = configCombo.getText();
        if (name.isEmpty())
            return;

        const File source = owner.getConfigFile(name);
        fileChooser = std::make_unique<FileChooser>("Export Config",
            File::getSpecialLocation(File::userDocumentsDirectory).getChildFile(source.getFileName()),
            "*.lhconfig");

        fileChooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
            [source](const FileChooser& fc)
            {
                const File destination = fc.getResult();
                if (destination != File())
                    source.copyFileTo(destination);
            });
    }

    void showImportConfigDialog()
    {
        fileChooser = std::make_unique<FileChooser>("Import Config", File(), "*.lhconfig");
        fileChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [this](const FileChooser& fc)
            {
                const File source = fc.getResult();
                if (!source.existsAsFile())
                    return;

                const File destination = owner.getConfigsDirectory().getChildFile(source.getFileName());
                source.copyFileTo(destination);
                refreshConfigList(destination.getFileNameWithoutExtension());
            });
    }

    IconMenu& owner;
    Label configLabel;
    ComboBox configCombo;
    TextButton loadConfigButton, saveConfigButton, deleteConfigButton, exportConfigButton, importConfigButton;
    std::unique_ptr<FileChooser> fileChooser;
};

//==============================================================================
class AudioPanel final : public Component
{
public:
    explicit AudioPanel(IconMenu& ownerToUse)
        : selector(ownerToUse.getDeviceManager(), 0, 256, 0, 256, false, false, true, true)
    {
        addAndMakeVisible(selector);
    }

    void resized() override
    {
        selector.setBounds(getLocalBounds());
    }

private:
    AudioDeviceSelectorComponent selector;
};

//==============================================================================
class GeneralPanel final : public Component
{
public:
    explicit GeneralPanel(IconMenu& ownerToUse) : owner(ownerToUse)
    {
        #if JUCE_WINDOWS
        autoStartButton.setButtonText("Launch at Windows startup");
        autoStartButton.setToggleState(owner.isAutoStartEnabled(), dontSendNotification);
        autoStartButton.onClick = [this] { owner.setAutoStartEnabled(autoStartButton.getToggleState()); };
        addAndMakeVisible(autoStartButton);
        #endif

        #if !JUCE_MAC
        invertIconButton.setButtonText(String::fromUTF8("\xF0\x9F\x94\x84 Invert Icon Color"));
        invertIconButton.onClick = [this] { owner.toggleIconColor(); };
        addAndMakeVisible(invertIconButton);
        #endif

        quitButton.setButtonText(String::fromUTF8("\xE2\x9C\x96 Quit"));
        quitButton.onClick = [this] { owner.quit(); };
        addAndMakeVisible(quitButton);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        #if JUCE_WINDOWS
        autoStartButton.setBounds(area.removeFromTop(24));
        area.removeFromTop(10);
        #endif

        #if !JUCE_MAC
        invertIconButton.setBounds(area.removeFromTop(28).removeFromLeft(180));
        area.removeFromTop(10);
        #endif

        quitButton.setBounds(area.removeFromTop(28).removeFromLeft(100));
    }

private:
    IconMenu& owner;
    #if JUCE_WINDOWS
    ToggleButton autoStartButton;
    #endif
    TextButton invertIconButton, quitButton;
};

//==============================================================================
class ControlPanelWindow::Content final : public Component
{
public:
    explicit Content(IconMenu& ownerToUse)
        : owner(ownerToUse),
          pluginsPanel(ownerToUse),
          configPanel(ownerToUse),
          audioPanel(ownerToUse),
          generalPanel(ownerToUse)
    {
        pluginsTabButton.setButtonText(String::fromUTF8("\xF0\x9F\x94\x8C  Plugins"));
        pluginsTabButton.setClickingTogglesState(true);
        pluginsTabButton.setRadioGroupId(1);
        pluginsTabButton.onClick = [this] { showTab(Tab::Plugins); };
        addAndMakeVisible(pluginsTabButton);

        configTabButton.setButtonText(String::fromUTF8("\xF0\x9F\x92\xBE  Config"));
        configTabButton.setClickingTogglesState(true);
        configTabButton.setRadioGroupId(1);
        configTabButton.onClick = [this] { showTab(Tab::Config); };
        addAndMakeVisible(configTabButton);

        audioTabButton.setButtonText(String::fromUTF8("\xF0\x9F\x94\x8A  Audio Settings"));
        audioTabButton.setClickingTogglesState(true);
        audioTabButton.setRadioGroupId(1);
        audioTabButton.onClick = [this] { showTab(Tab::Audio); };
        addAndMakeVisible(audioTabButton);

        generalTabButton.setButtonText(String::fromUTF8("\xE2\x9A\x99  General"));
        generalTabButton.setClickingTogglesState(true);
        generalTabButton.setRadioGroupId(1);
        generalTabButton.onClick = [this] { showTab(Tab::General); };
        addAndMakeVisible(generalTabButton);

        addChildComponent(pluginsPanel);
        addChildComponent(configPanel);
        addChildComponent(audioPanel);
        addChildComponent(generalPanel);

        pluginsTabButton.setToggleState(true, dontSendNotification);
        showTab(Tab::Plugins);

        refresh();
        setSize(640, 560);
    }

    void refresh()
    {
        pluginsPanel.refresh();
        configPanel.refresh();
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::white);
        g.setColour(Colour(0xfff2f3f5));
        g.fillRect(0, 0, sidebarWidth, getHeight());
        g.setColour(Colour(0xffd7d9dc));
        g.drawVerticalLine(sidebarWidth, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto sidebar = area.removeFromLeft(sidebarWidth).reduced(8);

        pluginsTabButton.setBounds(sidebar.removeFromTop(tabButtonHeight));
        sidebar.removeFromTop(4);
        configTabButton.setBounds(sidebar.removeFromTop(tabButtonHeight));
        sidebar.removeFromTop(4);
        audioTabButton.setBounds(sidebar.removeFromTop(tabButtonHeight));
        sidebar.removeFromTop(4);
        generalTabButton.setBounds(sidebar.removeFromTop(tabButtonHeight));

        auto contentArea = area.reduced(12);
        pluginsPanel.setBounds(contentArea);
        configPanel.setBounds(contentArea);
        audioPanel.setBounds(contentArea);
        generalPanel.setBounds(contentArea);
    }

private:
    enum class Tab { Plugins, Config, Audio, General };

    void showTab(Tab tab)
    {
        pluginsPanel.setVisible(tab == Tab::Plugins);
        configPanel.setVisible(tab == Tab::Config);
        audioPanel.setVisible(tab == Tab::Audio);
        generalPanel.setVisible(tab == Tab::General);

        if (tab == Tab::Plugins)
            pluginsPanel.refresh();
        else if (tab == Tab::Config)
            configPanel.refresh();
    }

    IconMenu& owner;
    TextButton pluginsTabButton, configTabButton, audioTabButton, generalTabButton;
    PluginsPanel pluginsPanel;
    ConfigPanel configPanel;
    AudioPanel audioPanel;
    GeneralPanel generalPanel;
};

//==============================================================================
ControlPanelWindow::ControlPanelWindow(IconMenu& ownerToUse)
    : DocumentWindow("Light Host - Control Panel", Colours::white,
          DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      owner(ownerToUse)
{
    auto* c = new Content(owner);
    content = c;
    setContentOwned(c, true);
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(520, 400, 1000, 1600);

    restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("controlPanelWindowPos"));
    setVisible(true);
}

ControlPanelWindow::~ControlPanelWindow()
{
    getAppProperties().getUserSettings()->setValue("controlPanelWindowPos", getWindowStateAsString());
    clearContentComponent();
}

void ControlPanelWindow::refresh()
{
    if (content != nullptr)
        content->refresh();
}

void ControlPanelWindow::closeButtonPressed()
{
    owner.controlPanelWindow = nullptr;
}
