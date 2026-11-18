#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Look.h"
#include "MixView.h"
#include "PluginProcessor.h"

namespace ui
{
// Column browser: Style > Meter/BPM > Section, with the clip list on the right.
class GroovesView : public juce::Component, private juce::Timer
{
public:
    explicit GroovesView(OpenDrumsProcessor&);
    ~GroovesView() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void refresh(); // library changed
    void selectPath(int style, int meter, int section); // used by the editor / tests

    struct Entry
    {
        const od::GrooveClipInfo* clip = nullptr;
        juce::String detail;
    };

private:
    class Column;
    class ClipList;
    void timerCallback() override;
    void rebuildColumns();
    void rebuildClips();
    void chooseFolder();

    OpenDrumsProcessor& proc_;
    int style_ = -1, meter_ = -1, section_ = -1;
    bool favoritesTab_ = false;

    juce::TextButton tabStyles_{juce::String::fromUTF8("Estilos")}, tabFavs_{juce::String::fromUTF8("Favoritos")};
    juce::TextEditor search_;
    juce::TextButton folderButton_{juce::String::fromUTF8("Pasta de grooves…")};
    juce::TextButton play_{juce::String::fromUTF8("\xE2\x96\xB6  Tocar")};
    juce::ToggleButton followHost_{juce::String::fromUTF8("Seguir tempo do host")};
    juce::ToggleButton followTransport_{juce::String::fromUTF8("Seguir play/stop do host")};
    juce::Slider bpm_;
    std::unique_ptr<SliderAtt> bpmAtt_;
    std::unique_ptr<ButtonAtt> hostAtt_, xportAtt_;
    std::unique_ptr<juce::FileChooser> chooser_;

    std::unique_ptr<Column> colStyle_, colMeter_, colSection_;
    std::unique_ptr<ClipList> clips_;
    std::vector<Entry> entries_;
    juce::Rectangle<int> transportBox_;
    juce::String countText_;
};
} // namespace ui
