#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "KitGeometry.hpp"
#include "Rig.hpp"

#include <functional>
#include <map>
#include <memory>
#include <vector>

// Draws the kit: every slot shows the picture of the piece that is currently loaded in it
// (so a swapped-in piece looks like the piece that is actually sounding), plus its stand,
// feet and, for the bass drum, the pedal. Pieces animate when they are hit (see triggerHit).
//   left click  : select the slot and play it
//   the "Trocar" badge on every piece (or right click / double click) : open the "swap piece" menu
// All geometry and animation curves live in core/KitGeometry (shared with the preview tool).
class KitView : public juce::Component, private juce::Timer
{
public:
    KitView();

    void setRig(std::shared_ptr<const od::Rig> rig);
    void setSwappedSlots(const juce::StringArray& slots);
    void setSelectedSlot(const juce::String& slot);
    void setRoom(int roomIndex); // cartoon backdrop of the reverb room (0..7, -1 = plain)
    juce::String getSelectedSlot() const { return selected_; }

    // Called when a note starts a sound (from the host's MIDI or from a click). `role` is the
    // articulation ("hit", "closed", "open", "pedal", ...).
    void triggerHit(const juce::String& slot, const juce::String& role, int velocity);

    std::function<void(const juce::String& slot)> onSlotSelected;
    std::function<void(const juce::String& slot)> onSlotHit;
    std::function<void(const juce::String& slot)> onSlotMenuRequested;

    static juce::String displayNameForSlot(const juce::String& slot);

    void paint(juce::Graphics&) override;
    void resized() override { layoutItems(); rebuildScene(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct Item
    {
        od::SlotView view;
        std::shared_ptr<juce::Drawable> drawable;
        od::geo::Rect bounds;
        od::geo::HitAnim anim; // t = "never" until the first hit
    };

    void timerCallback() override;
    void layoutItems();
    void rebuildScene();
    int itemAt(juce::Point<float> p) const;
    std::shared_ptr<juce::Drawable> drawableFor(const std::filesystem::path& file);

    std::vector<Item> items_; // sorted back (small y) to front (large y)
    std::map<juce::String, std::shared_ptr<juce::Drawable>> cache_;
    juce::StringArray swapped_;
    juce::String selected_;
    int hovered_ = -1;
    int hoveredBadge_ = -1;
    juce::Rectangle<float> badgeRect(size_t itemIndex, bool expanded) const;
    int badgeAt(juce::Point<float> p) const;
    int room_ = -1;
    std::vector<od::geo::Prim> scene_;
    double lastTickMs_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitView)
};
