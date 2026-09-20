//
//  PluginChainListBox.cpp
//  Light Host
//

#include "PluginChainListBox.h"
#include <cmath>

class PluginChainListBox::Row final : public Component
{
public:
    Row(PluginChainListBox& ownerBox, int startIndex, String nameToShow, bool bypassedToShow)
        : owner(ownerBox), logicalIndex(startIndex), pluginName(std::move(nameToShow)), bypassed(bypassedToShow)
    {
        bypassButton.setButtonText("Bypass");
        bypassButton.setClickingTogglesState(true);
        bypassButton.setToggleState(bypassed, dontSendNotification);
        bypassButton.onClick = [this]
        {
            Listener* l = &owner.listener;
            const int idx = logicalIndex;
            MessageManager::callAsync([l, idx] { l->bypassPluginRequested(idx); });
        };
        addAndMakeVisible(bypassButton);

        editButton.setButtonText("Edit");
        editButton.onClick = [this] { owner.listener.editPluginRequested(logicalIndex); };
        addAndMakeVisible(editButton);

        deleteButton.setButtonText(String::fromUTF8("\xc3\x97"));
        deleteButton.onClick = [this]
        {
            Listener* l = &owner.listener;
            const int idx = logicalIndex;
            MessageManager::callAsync([l, idx] { l->deletePluginRequested(idx); });
        };
        addAndMakeVisible(deleteButton);
    }

    void setLogicalIndex(int newIndex) { logicalIndex = newIndex; }
    int getLogicalIndex() const { return logicalIndex; }

    void paint(Graphics& g) override
    {
        const Colour rowBackground = isDragging ? Colour(0xffdce8f7)
            : (logicalIndex % 2 == 0 ? Colours::white : Colour(0xfff2f3f5));
        g.fillAll(rowBackground);

        g.setColour(Colour(0xffd7d9dc));
        g.drawHorizontalLine(getHeight() - 1, 0.0f, (float) getWidth());

        auto area = getLocalBounds().reduced(2);
        area.removeFromRight(146); // matches the total width removed for buttons in resized()
        auto handle = area.removeFromLeft(18).reduced(4, 8);

        g.setColour(Colour(0xff9aa0a6));
        for (int i = 0; i < 3; ++i)
            g.drawHorizontalLine(handle.getY() + i * (handle.getHeight() / 2), (float) handle.getX(), (float) handle.getRight());

        g.setColour(bypassed ? Colour(0xff9aa0a6) : Colour(0xff1a1a1a));
        g.drawText(pluginName, area.reduced(4, 0), Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2);
        deleteButton.setBounds(area.removeFromRight(24));
        area.removeFromRight(4);
        editButton.setBounds(area.removeFromRight(50));
        area.removeFromRight(4);
        bypassButton.setBounds(area.removeFromRight(64));
    }

    void mouseDown(const MouseEvent& e) override
    {
        juce::ignoreUnused(e);
        dragStartY = getY();
        mouseDownYInParent = (float) e.getEventRelativeTo(getParentComponent()).position.y;
        dragOriginIndex = logicalIndex;
        isDragging = false;
        toFront(false);
    }

    void mouseDrag(const MouseEvent& e) override
    {
        auto yInParent = (float) e.getEventRelativeTo(getParentComponent()).position.y;
        auto deltaY = yInParent - mouseDownYInParent;

        if (!isDragging && std::abs(deltaY) > 3.0f)
            isDragging = true;

        if (!isDragging)
            return;

        const int maxY = jmax(0, owner.getHeight() - getHeight());
        const int newY = jlimit(0, maxY, roundToInt((float) dragStartY + deltaY));
        setTopLeftPosition(getX(), newY);
        repaint();

        const int newSlot = jlimit(0, owner.rows.size() - 1, (newY + getHeight() / 2) / rowHeight);
        owner.reorderDuringDrag(this, newSlot);
    }

    void mouseUp(const MouseEvent&) override
    {
        if (!isDragging)
            return;

        isDragging = false;
        const int finalSlot = owner.rows.indexOf(this);
        owner.layoutRows();

        if (finalSlot != dragOriginIndex)
        {
            Listener* l = &owner.listener;
            const int from = dragOriginIndex;
            MessageManager::callAsync([l, from, finalSlot] { l->movePluginRequested(from, finalSlot); });
        }
    }

private:
    PluginChainListBox& owner;
    int logicalIndex;
    String pluginName;
    bool bypassed;
    TextButton bypassButton, editButton, deleteButton;
    int dragStartY = 0;
    float mouseDownYInParent = 0.0f;
    int dragOriginIndex = 0;
    bool isDragging = false;
};

PluginChainListBox::PluginChainListBox(Listener& listenerToUse) : listener(listenerToUse)
{
}

PluginChainListBox::~PluginChainListBox() = default;

void PluginChainListBox::setPlugins(const Array<PluginChainEntry>& plugins)
{
    rows.clear();
    for (int i = 0; i < plugins.size(); ++i)
        rows.add(new Row(*this, i, plugins.getReference(i).name, plugins.getReference(i).bypassed));

    for (auto* row : rows)
        addAndMakeVisible(row);

    layoutRows();
}

void PluginChainListBox::paint(Graphics& g)
{
    // Covers the full viewport even when the row list is shorter than it,
    // so no LookAndFeel-dependent background ever shows through underneath.
    g.fillAll(Colours::white);
}

void PluginChainListBox::resized()
{
    layoutRows();
}

void PluginChainListBox::layoutRows(Row* exceptRow)
{
    for (int i = 0; i < rows.size(); ++i)
    {
        auto* row = rows.getUnchecked(i);
        row->setLogicalIndex(i);
        if (row != exceptRow)
            row->setBounds(0, i * rowHeight, getWidth(), rowHeight);
    }
}

void PluginChainListBox::reorderDuringDrag(Row* draggedRow, int newSlot)
{
    const int oldSlot = rows.indexOf(draggedRow);
    if (oldSlot < 0 || oldSlot == newSlot)
        return;

    rows.move(oldSlot, newSlot);
    layoutRows(draggedRow);
}
