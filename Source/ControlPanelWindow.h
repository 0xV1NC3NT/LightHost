//
//  ControlPanelWindow.h
//  Light Host
//
//  A persistent window exposing the same actions as the tray menu
//  (audio settings, plugin scanning, quitting, ...) plus the active
//  plugin chain as a drag-to-reorder list.
//

#pragma once

#include <JuceHeader.h>

class IconMenu;

class ControlPanelWindow final : public DocumentWindow
{
public:
    explicit ControlPanelWindow(IconMenu& ownerToUse);
    ~ControlPanelWindow() override;

    void refresh();
    void closeButtonPressed() override;

private:
    class Content;
    IconMenu& owner;
    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanelWindow)
};
