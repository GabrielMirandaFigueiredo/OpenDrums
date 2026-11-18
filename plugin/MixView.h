#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Look.h"
#include "PluginProcessor.h"

#include <array>
#include <memory>

namespace ui
{
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

// Rotary knob with a small caption under it, bound to a parameter.
class KnobCell : public juce::Component
{
public:
    KnobCell(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& caption,
             juce::Colour colour, bool captionBelow = true);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider slider;

private:
    juce::String caption_;
    bool below_;
    std::unique_ptr<SliderAtt> att_;
};

// Level meter (peak, with slow fall), optionally showing gain reduction from the top.
class Meter : public juce::Component
{
public:
    explicit Meter(bool gainReduction = false) : gr_(gainReduction) {}
    void setLevelLinear(float lin);
    void setReductionDb(float db);
    void paint(juce::Graphics&) override;

private:
    bool gr_;
    float value_ = 0.0f; // dB (level) or dB of reduction
};

class ChannelStrip : public juce::Component
{
public:
    ChannelStrip(OpenDrumsProcessor&, int index);
    void setSlot(const juce::String& pieceLabel, std::shared_ptr<juce::Drawable> picture, bool present);
    void updateMeters();
    void refreshRoute();
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void showTab(bool sends);

    OpenDrumsProcessor& proc_;
    int index_;
    juce::String pieceLabel_;
    std::shared_ptr<juce::Drawable> picture_;
    bool present_ = true;
    bool sends_ = false;
    int lastMask_ = -1;

    juce::ComboBox outBox_;
    juce::TextButton tabProc_{"EQ/DIN"}, tabSends_{"SENDS"};
    juce::TextButton cOn_{"ON"}, cSpeed_{"FAST"}, eOn_{"ON"}, hfBell_{"BELL"}, lfBell_{"BELL"};
    juce::TextButton mute_{"M"}, solo_{"S"}, phase_{juce::String::fromUTF8("\xC3\x98")};
    juce::Slider fader_;
    Meter meter_, grMeter_{true};
    std::array<std::unique_ptr<KnobCell>, 11> eq_; // hfF hfG | hmfF hmfG hmfQ | lmfF lmfG lmfQ | lfF lfG
    std::array<std::unique_ptr<KnobCell>, 4> comp_; // thr ratio makeup release
    std::unique_ptr<KnobCell> pan_, sendRv_, sendPar_;
    std::vector<std::unique_ptr<ButtonAtt>> buttonAtts_;
    std::unique_ptr<SliderAtt> faderAtt_;

    juce::Rectangle<int> compBox_, eqBox_, picBox_, sendsBox_;
};

class MixView : public juce::Component, private juce::Timer
{
public:
    explicit MixView(OpenDrumsProcessor&);
    ~MixView() override;
    void setRig(std::shared_ptr<const od::Rig> rig);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    class Content;
    OpenDrumsProcessor& proc_;
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    std::unique_ptr<juce::Component> parallel_, reverb_, master_; // pinned bus strips (types in the .cpp)
    std::function<void()> updateBusMeters_;
};
} // namespace ui
