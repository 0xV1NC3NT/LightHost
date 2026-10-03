//
//  IconMenu.cpp
//  Light Host
//

#include "IconMenu.h"
#include "PluginWindow.h"
#include "ControlPanelWindow.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace
{
    constexpr int INPUT_CHANNEL_ONE = 0;
    constexpr int INPUT_CHANNEL_TWO = 1;

    String makeStateKey(const String& instanceId)
    {
        return "plugin-state-" + instanceId;
    }

    #if JUCE_WINDOWS
    // Windows blocks a plain SetForegroundWindow call from most background
    // processes (the "foreground lock"). Briefly attaching our input queue to
    // the currently-focused thread's is the standard, well-established way
    // around that for tray-style apps bringing their own window forward.
    void forceWindowToForeground(Component& comp)
    {
        auto* peer = comp.getPeer();
        if (peer == nullptr)
            return;

        auto hwnd = (HWND) peer->getNativeHandle();
        if (hwnd == nullptr)
            return;

        const DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD currentThreadId = GetCurrentThreadId();
        const bool needsAttach = foregroundThreadId != 0 && foregroundThreadId != currentThreadId;

        if (needsAttach)
            AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);

        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);

        if (needsAttach)
            AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
    }
    #endif

    // Key scheme used by Light Host 1.x (name+version+format based - can't
    // tell two instances of the same plugin apart, which is exactly why the
    // current format keys state by a per-instance instanceId instead).
    String legacyKey(const String& type, const PluginDescription& d)
    {
        return "plugin-" + type.toLowerCase() + "-" + d.name + d.version + d.pluginFormatName;
    }
}

class IconMenu::PluginListWindow final : public DocumentWindow
{
public:
    PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
        : DocumentWindow("Available Plugins", Colours::white,
              DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          owner(owner_)
    {
        const File deadMansPedalFile(getAppProperties().getUserSettings()
            ->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

        setContentOwned(new PluginListComponent(pluginFormatManager,
            owner.knownPluginList,
            deadMansPedalFile,
            getAppProperties().getUserSettings()), true);

        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(300, 400, 800, 1500);
        setTopLeftPosition(60, 60);

        restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
        setVisible(true);
    }

    ~PluginListWindow() override
    {
        getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
        owner.pluginListWindow = nullptr;
    }

private:
    IconMenu& owner;
};

IconMenu::IconMenu()
{
    addDefaultFormatsToManager (formatManager);

    std::unique_ptr<XmlElement> savedAudioState(getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState.get(), true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    deviceManager.addChangeListener(this);

    std::unique_ptr<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);

    migrateLegacySettings();

    retryTimer.onTick = [this] { retryUnavailablePlugins(false); };

    setIcon();
    setIconTooltip(JUCEApplication::getInstance()->getApplicationName());

    // Plugins with online license checks can take a long time (or block) while
    // loading, so the tray icon is shown first and the chain loads afterwards.
    MessageManager::callAsync([safeThis = Component::SafePointer<IconMenu>(this)]
    {
        if (safeThis != nullptr)
            safeThis->loadActivePlugins();
    });
}

IconMenu::~IconMenu()
{
    controlPanelWindow = nullptr;
    pluginListWindow = nullptr;
    savePluginStates();
    persistActivePluginList();
    deviceManager.removeAudioCallback(&player);
}

void IconMenu::setIcon()
{
    String defaultColor;
    #if JUCE_WINDOWS
    defaultColor = "white";
    #elif JUCE_LINUX
    defaultColor = "black";
    #else
    defaultColor = "black";
    #endif

    if (!getAppProperties().getUserSettings()->containsKey("icon"))
        getAppProperties().getUserSettings()->setValue("icon", defaultColor);

    const String color = getAppProperties().getUserSettings()->getValue("icon");
    Image icon = color.equalsIgnoreCase("white")
        ? ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize)
        : ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);

    // The template image (always a dark, mostly-opaque silhouette) lets macOS
    // automatically invert the menu bar icon for light/dark appearance, so no
    // manual "defaults read AppleInterfaceStyle" polling is needed any more.
    Image templateIcon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
    setIconImage(icon, templateIcon);
}

void IconMenu::migrateLegacySettings()
{
    auto* settings = getAppProperties().getUserSettings();
    if (settings->containsKey("activePluginChain"))
        return;

    std::unique_ptr<XmlElement> legacyActive(settings->getXmlValue("pluginListActive"));
    if (legacyActive == nullptr)
        return;

    struct LegacyEntry { PluginDescription desc; int order; };
    std::vector<LegacyEntry> entries;

    for (auto* pluginXml : legacyActive->getChildIterator())
    {
        PluginDescription desc;
        if (!desc.loadFromXml(*pluginXml))
            continue;

        const int order = settings->getValue(legacyKey("order", desc)).getIntValue();
        entries.push_back({ desc, order });
    }

    std::sort(entries.begin(), entries.end(), [](const LegacyEntry& a, const LegacyEntry& b) { return a.order < b.order; });

    XmlElement chainXml("ACTIVEPLUGINS");
    for (auto& entry : entries)
    {
        const String newId = Uuid().toString();
        const bool bypassed = settings->getBoolValue(legacyKey("bypass", entry.desc), false);
        const String legacyState = settings->getValue(legacyKey("state", entry.desc));
        if (legacyState.isNotEmpty())
            settings->setValue(makeStateKey(newId), legacyState);

        auto pluginXml = entry.desc.createXml();
        pluginXml->setAttribute("instanceId", newId);
        pluginXml->setAttribute("bypassed", bypassed);
        chainXml.addChildElement(pluginXml.release());

        settings->removeValue(legacyKey("order", entry.desc));
        settings->removeValue(legacyKey("bypass", entry.desc));
        settings->removeValue(legacyKey("state", entry.desc));
    }

    settings->setValue("activePluginChain", &chainXml);
    settings->removeValue("pluginListActive");
    getAppProperties().saveIfNeeded();

    Logger::writeToLog("Light Host: migrated " + String((int) entries.size()) + " active plugin(s) from the legacy (1.x) settings format.");
}

void IconMenu::loadActivePlugins()
{
    std::unique_ptr<XmlElement> savedChain(getAppProperties().getUserSettings()->getXmlValue("activePluginChain"));
    resetGraphAndLoadFromXml(savedChain.get(), false);
}

void IconMenu::resetGraphAndLoadFromXml(const XmlElement* pluginsXml, bool assignFreshInstanceIds)
{
    retryTimer.stopTimer();
    PluginWindow::closeAllCurrentlyOpenWindows();

    graph.clear();
    activePlugins.clear();

    inputNodeId = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), AudioProcessorGraph::NodeID(1))->nodeID;
    outputNodeId = graph.addNode(std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), AudioProcessorGraph::NodeID(2))->nodeID;
    nextNodeUid = 3;

    bool anyUnavailable = false;

    if (pluginsXml != nullptr)
    {
        auto* settings = getAppProperties().getUserSettings();

        for (auto* pluginXml : pluginsXml->getChildIterator())
        {
            PluginDescription desc;
            if (!desc.loadFromXml(*pluginXml))
                continue;

            ActivePlugin ap;
            ap.description = desc;
            ap.bypassed = pluginXml->getBoolAttribute("bypassed", false);

            if (assignFreshInstanceIds)
            {
                ap.instanceId = Uuid().toString();

                // State goes through the same settings key as a regular session so a
                // plugin that fails to load keeps it for a later retry.
                if (auto* stateXml = pluginXml->getChildByName("STATE"))
                {
                    const String state = stateXml->getAllSubText();
                    if (state.isNotEmpty())
                        settings->setValue(makeStateKey(ap.instanceId), state);
                }
            }
            else
            {
                ap.instanceId = pluginXml->getStringAttribute("instanceId");
                if (ap.instanceId.isEmpty())
                    ap.instanceId = Uuid().toString();
            }

            if (auto instance = createPluginInstanceFor(desc, ap.instanceId))
            {
                ap.nodeId = graph.addNode(std::move(instance), AudioProcessorGraph::NodeID(nextNodeUid++))->nodeID;
            }
            else
            {
                ap.unavailable = true;
                anyUnavailable = true;
            }

            activePlugins.add(ap);
        }
    }

    chainLoaded = true;
    rebuildConnections();

    retryAttempt = 0;
    if (anyUnavailable)
        scheduleRetry();
}

std::unique_ptr<AudioPluginInstance> IconMenu::createPluginInstanceFor(const PluginDescription& description, const String& instanceId)
{
    String errorMessage;
    std::unique_ptr<AudioPluginInstance> instance;

    // Plugin code is outside our control (license checks, network calls...), so a
    // failure here must never take the whole host down with it.
    try
    {
        instance = formatManager.createPluginInstance(description, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
    }
    catch (...)
    {
        errorMessage = "the plugin threw an exception while loading";
    }

    if (instance == nullptr)
    {
        Logger::writeToLog("Light Host: failed to load plugin \"" + description.name + "\": " + errorMessage);
        return nullptr;
    }

    // Some plugins (VST3 in particular) only fully build their parameter
    // tree once prepareToPlay has run - restoring state before that can
    // get silently discarded. Prepare first, then restore, then hand it
    // to the graph (which will prepare it again, harmlessly, on its own).
    if (graph.getSampleRate() > 0)
        instance->prepareToPlay(graph.getSampleRate(), graph.getBlockSize());

    const String savedState = getAppProperties().getUserSettings()->getValue(makeStateKey(instanceId));
    if (savedState.isNotEmpty())
    {
        MemoryBlock savedStateBinary;
        savedStateBinary.fromBase64Encoding(savedState);
        instance->setStateInformation(savedStateBinary.getData(), (int) savedStateBinary.getSize());
    }

    return instance;
}

void IconMenu::scheduleRetry()
{
    // Gives the network time to come up after login without hammering plugins
    // that pop up a dialog on every failed license check.
    static constexpr int delaysSeconds[] = { 5, 15, 30, 60, 120 };

    if (retryAttempt >= numElementsInArray(delaysSeconds))
        return;

    retryTimer.startTimer(delaysSeconds[retryAttempt++] * 1000);
}

void IconMenu::retryUnavailablePlugins(bool manual)
{
    retryTimer.stopTimer();

    if (retryInProgress)
        return;

    if (manual)
        retryAttempt = 0;

    StringArray pendingIds;
    for (auto& ap : activePlugins)
        if (ap.unavailable)
            pendingIds.add(ap.instanceId);

    if (pendingIds.isEmpty())
        return;

    const ScopedValueSetter<bool> inProgress(retryInProgress, true);

    // Loading can run a nested message loop (license dialogs), so the chain may be
    // edited in the meantime - entries are looked up by id instead of held by reference.
    auto indexOfInstance = [this](const String& id)
    {
        for (int i = 0; i < activePlugins.size(); ++i)
            if (activePlugins.getReference(i).instanceId == id)
                return i;
        return -1;
    };

    bool anyLoaded = false, anyStillMissing = false;

    for (auto& id : pendingIds)
    {
        const int before = indexOfInstance(id);
        if (before < 0 || !activePlugins.getReference(before).unavailable)
            continue;

        const PluginDescription description = activePlugins.getReference(before).description;
        auto instance = createPluginInstanceFor(description, id);

        const int index = indexOfInstance(id);
        if (index < 0 || !activePlugins.getReference(index).unavailable)
            continue;

        if (instance == nullptr)
        {
            anyStillMissing = true;
            continue;
        }

        auto& ap = activePlugins.getReference(index);
        ap.nodeId = graph.addNode(std::move(instance), AudioProcessorGraph::NodeID(nextNodeUid++))->nodeID;
        ap.unavailable = false;
        anyLoaded = true;
    }

    if (anyLoaded)
    {
        rebuildConnections();
        notifyChainChanged();
    }

    if (anyStillMissing)
        scheduleRetry();
}

void IconMenu::rebuildConnections()
{
    auto existingConnections = graph.getConnections();
    for (auto& connection : existingConnections)
        graph.removeConnection(connection);

    if (activePlugins.isEmpty())
    {
        graph.addConnection({ { inputNodeId, INPUT_CHANNEL_ONE }, { outputNodeId, INPUT_CHANNEL_ONE } });
        graph.addConnection({ { inputNodeId, INPUT_CHANNEL_TWO }, { outputNodeId, INPUT_CHANNEL_TWO } });
        return;
    }

    AudioProcessorGraph::NodeID previousId;
    bool hasPrevious = false;

    for (auto& ap : activePlugins)
    {
        if (ap.bypassed || ap.unavailable)
            continue;

        if (!hasPrevious)
        {
            graph.addConnection({ { inputNodeId, INPUT_CHANNEL_ONE }, { ap.nodeId, INPUT_CHANNEL_ONE } });
            graph.addConnection({ { inputNodeId, INPUT_CHANNEL_TWO }, { ap.nodeId, INPUT_CHANNEL_TWO } });
        }
        else
        {
            graph.addConnection({ { previousId, INPUT_CHANNEL_ONE }, { ap.nodeId, INPUT_CHANNEL_ONE } });
            graph.addConnection({ { previousId, INPUT_CHANNEL_TWO }, { ap.nodeId, INPUT_CHANNEL_TWO } });
        }

        previousId = ap.nodeId;
        hasPrevious = true;
    }

    if (hasPrevious)
    {
        graph.addConnection({ { previousId, INPUT_CHANNEL_ONE }, { outputNodeId, INPUT_CHANNEL_ONE } });
        graph.addConnection({ { previousId, INPUT_CHANNEL_TWO }, { outputNodeId, INPUT_CHANNEL_TWO } });
    }
    else
    {
        graph.addConnection({ { inputNodeId, INPUT_CHANNEL_ONE }, { outputNodeId, INPUT_CHANNEL_ONE } });
        graph.addConnection({ { inputNodeId, INPUT_CHANNEL_TWO }, { outputNodeId, INPUT_CHANNEL_TWO } });
    }
}

void IconMenu::addPlugin(const PluginDescription& plugin)
{
    ActivePlugin ap;
    ap.description = plugin;
    ap.instanceId = Uuid().toString();
    ap.bypassed = false;

    auto instance = createPluginInstanceFor(plugin, ap.instanceId);
    if (instance == nullptr)
        return;

    auto node = graph.addNode(std::move(instance), AudioProcessorGraph::NodeID(nextNodeUid++));
    ap.nodeId = node->nodeID;
    activePlugins.add(ap);

    rebuildConnections();
    notifyChainChanged();
}

void IconMenu::removePluginAt(int index)
{
    if (!isPositiveAndBelow(index, activePlugins.size()))
        return;

    PluginWindow::closeCurrentlyOpenWindowsFor(activePlugins.getReference(index).nodeId);

    getAppProperties().getUserSettings()->removeValue(makeStateKey(activePlugins.getReference(index).instanceId));
    graph.removeNode(activePlugins.getReference(index).nodeId);
    activePlugins.remove(index);

    rebuildConnections();
    notifyChainChanged();
}

void IconMenu::movePlugin(int fromIndex, int toIndex)
{
    if (!isPositiveAndBelow(fromIndex, activePlugins.size()) || !isPositiveAndBelow(toIndex, activePlugins.size()))
        return;
    if (fromIndex == toIndex)
        return;

    activePlugins.move(fromIndex, toIndex);

    rebuildConnections();
    notifyChainChanged();
}

void IconMenu::editPluginRequested(int index)
{
    if (!isPositiveAndBelow(index, activePlugins.size()))
        return;

    if (activePlugins.getReference(index).unavailable)
    {
        // Deferred: reloading refreshes the Control Panel list, which must not
        // happen while one of its own buttons is still handling a click.
        MessageManager::callAsync([safeThis = Component::SafePointer<IconMenu>(this)]
        {
            if (safeThis != nullptr)
                safeThis->retryUnavailablePlugins(true);
        });
        return;
    }

    if (AudioProcessorGraph::Node::Ptr node = graph.getNodeForId(activePlugins.getReference(index).nodeId))
        if (auto* w = PluginWindow::getWindowFor(node, PluginWindow::Normal))
            w->toFront(true);
}

void IconMenu::bypassPluginRequested(int index)
{
    if (!isPositiveAndBelow(index, activePlugins.size()))
        return;

    auto& ap = activePlugins.getReference(index);
    ap.bypassed = !ap.bypassed;

    rebuildConnections();
    notifyChainChanged();
}

void IconMenu::deletePluginRequested(int index)
{
    removePluginAt(index);
}

void IconMenu::movePluginRequested(int fromIndex, int toIndex)
{
    movePlugin(fromIndex, toIndex);
}

Array<PluginChainEntry> IconMenu::buildChainEntries() const
{
    Array<PluginChainEntry> entries;
    for (auto& ap : activePlugins)
    {
        PluginChainEntry entry;
        entry.name = ap.description.name;
        entry.bypassed = ap.bypassed;
        entry.unavailable = ap.unavailable;
        entries.add(entry);
    }
    return entries;
}

void IconMenu::notifyChainChanged()
{
    persistActivePluginList();
    if (controlPanelWindow != nullptr)
        controlPanelWindow->refresh();
}

void IconMenu::persistActivePluginList()
{
    if (!chainLoaded)
        return;

    XmlElement chainXml("ACTIVEPLUGINS");
    for (auto& ap : activePlugins)
    {
        auto pluginXml = ap.description.createXml();
        pluginXml->setAttribute("instanceId", ap.instanceId);
        pluginXml->setAttribute("bypassed", ap.bypassed);
        chainXml.addChildElement(pluginXml.release());
    }
    getAppProperties().getUserSettings()->setValue("activePluginChain", &chainXml);
    getAppProperties().saveIfNeeded();
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        std::unique_ptr<XmlElement> savedPluginList(knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue("pluginList", savedPluginList.get());
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &deviceManager)
    {
        std::unique_ptr<XmlElement> audioState(deviceManager.createStateXml());
        getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState.get());
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());

    if (menuIconLeftClicked)
    {
        menu.addItem(CMD_AUDIO_SETTINGS, "Preferences");
        menu.addItem(CMD_EDIT_PLUGINS, "Edit Plugins");
        menu.addItem(CMD_CONTROL_PANEL, "Control Panel...");
        menu.addSeparator();
        menu.addSectionHeader("Active Plugins");

        for (int i = 0; i < activePlugins.size(); ++i)
        {
            const auto& active = activePlugins.getReference(i);

            PopupMenu options;
            options.addItem(INDEX_EDIT + i, active.unavailable ? "Retry Loading" : "Edit");
            options.addItem(INDEX_BYPASS + i, "Bypass", true, active.bypassed);
            options.addSeparator();
            options.addItem(INDEX_MOVE_UP + i, "Move Up", i > 0);
            options.addItem(INDEX_MOVE_DOWN + i, "Move Down", i < activePlugins.size() - 1);
            options.addSeparator();
            options.addItem(INDEX_DELETE + i, "Delete");
            menu.addSubMenu(active.unavailable ? active.description.name + " (unavailable)" : active.description.name, options);
        }

        menu.addSeparator();
        menu.addSectionHeader("Available Plugins");
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(CMD_CONTROL_PANEL, "Control Panel...");
        menu.addSeparator();
        menu.addItem(CMD_QUIT, "Quit");
        #if !JUCE_MAC
        menu.addItem(CMD_INVERT_ICON, "Invert Icon Color");
        #endif
    }

    #if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #else
    if (trayMenuX == 0 || trayMenuY == 0)
    {
        POINT iconLocation;
        iconLocation.x = 0;
        iconLocation.y = 0;
        GetCursorPos(&iconLocation);
        trayMenuX = iconLocation.x;
        trayMenuY = iconLocation.y;
    }
    juce::Rectangle<int> rect(trayMenuX, trayMenuY, 1, 1);
    menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
    #endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    #if JUCE_MAC
    Process::setDockIconVisible(true);
    #endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    #if JUCE_WINDOWS
    trayMenuX = trayMenuY = 0;
    #endif
    startTimer(50);
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    #if JUCE_MAC
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
    #endif

    if (id == CMD_CONTROL_PANEL)
    {
        im->openControlPanel();
        return;
    }
    if (id == CMD_AUDIO_SETTINGS)
    {
        im->showAudioSettings();
        return;
    }
    if (id == CMD_EDIT_PLUGINS)
    {
        im->openPluginScanner();
        return;
    }
    if (id == CMD_QUIT)
    {
        im->quit();
        return;
    }
    if (id == CMD_INVERT_ICON)
    {
        im->toggleIconColor();
        return;
    }

    if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        im->removePluginAt(id - im->INDEX_DELETE);
    else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
        im->bypassPluginRequested(id - im->INDEX_BYPASS);
    else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        im->editPluginRequested(id - im->INDEX_EDIT);
    else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
        im->movePlugin(id - im->INDEX_MOVE_UP, id - im->INDEX_MOVE_UP - 1);
    else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
        im->movePlugin(id - im->INDEX_MOVE_DOWN, id - im->INDEX_MOVE_DOWN + 1);
    else if (id > 0 && im->knownPluginList.getIndexChosenByMenu(id) > -1)
        im->addPlugin(*im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id)));
    else
        return;

    im->startTimer(50);
}

void IconMenu::savePluginStates()
{
    for (auto& ap : activePlugins)
    {
        if (auto node = graph.getNodeForId(ap.nodeId))
        {
            MemoryBlock savedStateBinary;
            node->getProcessor()->getStateInformation(savedStateBinary);
            getAppProperties().getUserSettings()->setValue(makeStateKey(ap.instanceId), savedStateBinary.toBase64Encoding());
        }
    }
    getAppProperties().saveIfNeeded();
}

void IconMenu::showAudioSettings()
{
    auto* audioSettingsComp = new AudioDeviceSelectorComponent(deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp->setSize(500, 450);

    DialogWindow::LaunchOptions o;
    o.content.setOwned(audioSettingsComp);
    o.dialogTitle = "Audio Settings";
    o.componentToCentreAround = this;
    o.dialogBackgroundColour = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;

    o.launchAsync();
}

void IconMenu::openPluginScanner()
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow>(*this, formatManager);
    pluginListWindow->toFront(true);
    #if JUCE_WINDOWS
    forceWindowToForeground(*pluginListWindow);
    #endif
}

void IconMenu::openControlPanel()
{
    if (controlPanelWindow == nullptr)
        controlPanelWindow = std::make_unique<ControlPanelWindow>(*this);
    controlPanelWindow->toFront(true);
    #if JUCE_WINDOWS
    forceWindowToForeground(*controlPanelWindow);
    #endif
}

void IconMenu::toggleIconColor()
{
    const String color = getAppProperties().getUserSettings()->getValue("icon");
    getAppProperties().getUserSettings()->setValue("icon", color.equalsIgnoreCase("black") ? "white" : "black");
    setIcon();
}

void IconMenu::quit()
{
    JUCEApplication::getInstance()->systemRequestedQuit();
}

void IconMenu::removePluginsLackingInputOutput()
{
    Array<PluginDescription> toRemove;
    for (int i = 0; i < knownPluginList.getNumTypes(); ++i)
    {
        auto* plugin = knownPluginList.getType(i);
        if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
            toRemove.add(*plugin);
    }
    for (auto& desc : toRemove)
        knownPluginList.removeType(desc);
}

File IconMenu::getConfigsDirectory() const
{
    auto dir = getAppProperties().getUserSettings()->getFile().getSiblingFile("Configs");
    if (!dir.isDirectory())
        dir.createDirectory();
    return dir;
}

File IconMenu::getConfigFile(const String& name) const
{
    return getConfigsDirectory().getChildFile(File::createLegalFileName(name) + ".lhconfig");
}

StringArray IconMenu::listConfigNames() const
{
    StringArray names;
    for (const auto& f : getConfigsDirectory().findChildFiles(File::findFiles, false, "*.lhconfig"))
        names.add(f.getFileNameWithoutExtension());
    names.sort(true);
    return names;
}

void IconMenu::saveConfigAs(const String& name)
{
    XmlElement configXml("LIGHTHOSTCONFIG");
    configXml.setAttribute("name", name);

    for (auto& ap : activePlugins)
    {
        auto pluginXml = ap.description.createXml();
        pluginXml->setAttribute("bypassed", ap.bypassed);

        String stateBase64;
        if (auto node = graph.getNodeForId(ap.nodeId))
        {
            MemoryBlock state;
            node->getProcessor()->getStateInformation(state);
            stateBase64 = state.toBase64Encoding();
        }
        else
        {
            // Not loaded right now - keep whatever state was saved for it.
            stateBase64 = getAppProperties().getUserSettings()->getValue(makeStateKey(ap.instanceId));
        }
        pluginXml->createNewChildElement("STATE")->addTextElement(stateBase64);
        configXml.addChildElement(pluginXml.release());
    }

    getConfigFile(name).replaceWithText(configXml.toString());

    getAppProperties().getUserSettings()->setValue("activeConfigName", name);
    getAppProperties().saveIfNeeded();
}

void IconMenu::loadConfig(const String& name)
{
    auto file = getConfigFile(name);
    if (!file.existsAsFile())
        return;

    XmlDocument xmlDoc(file);
    std::unique_ptr<XmlElement> configXml(xmlDoc.getDocumentElement());
    if (configXml == nullptr)
        return;

    resetGraphAndLoadFromXml(configXml.get(), true);

    getAppProperties().getUserSettings()->setValue("activeConfigName", name);
    getAppProperties().saveIfNeeded();

    notifyChainChanged();
}

void IconMenu::deleteConfig(const String& name)
{
    getConfigFile(name).deleteFile();

    if (getCurrentConfigName() == name)
    {
        getAppProperties().getUserSettings()->removeValue("activeConfigName");
        getAppProperties().saveIfNeeded();
    }
}

String IconMenu::getCurrentConfigName() const
{
    return getAppProperties().getUserSettings()->getValue("activeConfigName");
}

#if JUCE_WINDOWS
namespace
{
    const String autoStartRegistryPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Light Host";
}

bool IconMenu::isAutoStartEnabled() const
{
    return WindowsRegistry::valueExists(autoStartRegistryPath);
}

void IconMenu::setAutoStartEnabled(bool shouldBeEnabled)
{
    if (shouldBeEnabled)
    {
        const String exePath = File::getSpecialLocation(File::currentExecutableFile).getFullPathName();
        WindowsRegistry::setValue(autoStartRegistryPath, "\"" + exePath + "\"");
    }
    else
    {
        WindowsRegistry::deleteValue(autoStartRegistryPath);
    }
}
#endif
