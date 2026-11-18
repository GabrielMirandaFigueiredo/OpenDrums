#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Look.h"
#include "PluginProcessor.h"

namespace ui
{
// Routing matrix: audio outputs per piece, MIDI channels per piece, MIDI CC -> controls, MIDI notes per piece.
class MatrixView : public juce::Component, private juce::Timer
{
public:
    explicit MatrixView(OpenDrumsProcessor&);
    ~MatrixView() override;
    void setRig(std::shared_ptr<const od::Rig> rig);
    void showPage(int page); // 0 audio, 1 MIDI channels, 2 CC, 3 notes
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    class AudioGrid;
    class ChannelGrid;
    class CcPage;
    class NotesPage;
    void timerCallback() override;

    OpenDrumsProcessor& proc_;
    int page_ = 0;
    std::array<juce::TextButton, 4> tabs_;
    juce::TextButton resetButton_;
    std::unique_ptr<AudioGrid> audio_;
    std::unique_ptr<ChannelGrid> channels_;
    std::unique_ptr<CcPage> cc_;
    std::unique_ptr<NotesPage> notes_;
    juce::String help_;
};
} // namespace ui
