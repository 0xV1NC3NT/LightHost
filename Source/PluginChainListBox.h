//
//  PluginChainListBox.h
//  Light Host
//
//  A vertical list of active plugins that supports reordering the
//  processing chain by dragging rows up and down.
//

#pragma once

#include <JuceHeader.h>

struct PluginChainEntry
{
    String name;
    bool bypassed = false;
};

class PluginChainListBox : public Component
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editPluginRequested(int index) = 0;
        virtual void bypassPluginRequested(int index) = 0;
        virtual void deletePluginRequested(int index) = 0;
        virtual void movePluginRequested(int fromIndex, int toIndex) = 0;
    };

    explicit PluginChainListBox(Listener& listenerToUse);
    ~PluginChainListBox() override;

    void setPlugins(const Array<PluginChainEntry>& plugins);
    int getNumRows() const { return rows.size(); }
    int getPreferredHeight() const { return rows.size() * rowHeight; }

    void paint(Graphics& g) override;
    void resized() override;

    static constexpr int rowHeight = 32;

private:
    class Row;
    friend class Row;

    void layoutRows(Row* exceptRow = nullptr);
    void reorderDuringDrag(Row* draggedRow, int newSlot);

    Listener& listener;
    OwnedArray<Row> rows;
};
