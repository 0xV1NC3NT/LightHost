//
//  IconMenu.h
//  Light Host
//

#pragma once

#include <JuceHeader.h>
#include "PluginChainListBox.h"

ApplicationProperties& getAppProperties();

struct ActivePlugin
{
    PluginDescription description;
    String instanceId;
    bool bypassed = false;
    AudioProcessorGraph::NodeID nodeId;
};

// Hosted plugins that don't guard against denormals themselves (e.g. long reverb/delay
// tails decaying towards zero) can otherwise stall the FPU. Flushing denormals to zero
// around the whole graph traversal covers every node without relying on each plugin
// doing this on its own.
class DenormalSafeAudioProcessorGraph final : public AudioProcessorGraph
{
public:
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        const ScopedNoDenormals noDenormals;
        AudioProcessorGraph::processBlock(buffer, midiMessages);
    }

    void processBlock(AudioBuffer<double>& buffer, MidiBuffer& midiMessages) override
    {
        const ScopedNoDenormals noDenormals;
        AudioProcessorGraph::processBlock(buffer, midiMessages);
    }
};

class IconMenu final : public SystemTrayIconComponent,
                        private Timer,
                        public ChangeListener,
                        public PluginChainListBox::Listener
{
public:
    IconMenu();
    ~IconMenu() override;

    void mouseDown(const MouseEvent&) override;
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed) override;

    // Shared actions - used by both the tray menu and the Control Panel window
    void showAudioSettings();
    void openPluginScanner();
    void openControlPanel();
    void toggleIconColor();
    void deleteAllPluginStates();
    void quit();

    void addPlugin(const PluginDescription& plugin);
    Array<ActivePlugin> getActivePlugins() const { return activePlugins; }
    Array<PluginChainEntry> buildChainEntries() const;
    KnownPluginList& getKnownPluginList() { return knownPluginList; }
    AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // Config save/load/import/export
    File getConfigsDirectory() const;
    File getConfigFile(const String& name) const;
    StringArray listConfigNames() const;
    void saveConfigAs(const String& name);
    void loadConfig(const String& name);
    void deleteConfig(const String& name);
    String getCurrentConfigName() const;

    #if JUCE_WINDOWS
    bool isAutoStartEnabled() const;
    void setAutoStartEnabled(bool shouldBeEnabled);
    #endif

    // PluginChainListBox::Listener
    void editPluginRequested(int index) override;
    void bypassPluginRequested(int index) override;
    void deletePluginRequested(int index) override;
    void movePluginRequested(int fromIndex, int toIndex) override;

private:
    void timerCallback() override;
    void migrateLegacySettings();
    void loadActivePlugins();
    void resetGraphAndLoadFromXml(const XmlElement* pluginsXml, bool assignFreshInstanceIds);
    void rebuildConnections();
    void savePluginStates();
    void persistActivePluginList();
    void removePluginsLackingInputOutput();
    void setIcon();
    void removePluginAt(int index);
    void movePlugin(int fromIndex, int toIndex);
    void notifyChainChanged();

    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    bool menuIconLeftClicked = false;
    DenormalSafeAudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::NodeID inputNodeId, outputNodeId;
    uint32 nextNodeUid = 3;
    Array<ActivePlugin> activePlugins;

    #if JUCE_WINDOWS
    int trayMenuX = 0, trayMenuY = 0;
    #endif

    class PluginListWindow;
    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<class ControlPanelWindow> controlPanelWindow;

    static constexpr int INDEX_EDIT = 1000000;
    static constexpr int INDEX_BYPASS = 2000000;
    static constexpr int INDEX_DELETE = 3000000;
    static constexpr int INDEX_MOVE_UP = 4000000;
    static constexpr int INDEX_MOVE_DOWN = 5000000;

    static constexpr int CMD_BASE = 500000;
    static constexpr int CMD_AUDIO_SETTINGS = CMD_BASE + 1;
    static constexpr int CMD_EDIT_PLUGINS = CMD_BASE + 2;
    static constexpr int CMD_CONTROL_PANEL = CMD_BASE + 3;
    static constexpr int CMD_QUIT = CMD_BASE + 4;
    static constexpr int CMD_DELETE_STATES = CMD_BASE + 5;
    static constexpr int CMD_INVERT_ICON = CMD_BASE + 6;

    friend class ControlPanelWindow;
};
