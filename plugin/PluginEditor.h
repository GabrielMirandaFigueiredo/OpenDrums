#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GroovesView.h"
#include "KitView.h"
#include "Look.h"
#include "MatrixView.h"
#include "MixView.h"
#include "PluginProcessor.h"

class OpenDrumsEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer, // lets the groove list drag .mid files out to the DAW
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit OpenDrumsEditor(OpenDrumsProcessor&);
    ~OpenDrumsEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void showView(int view); // 0 kit, 1 mix, 2 grooves, 3 matrix

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void updateSelectionInfo();
    void showPieceMenu(const juce::String& slot);
    void chooseKitsFolder();

    OpenDrumsProcessor& proc_;
    ui::CartoonLookAndFeel look_;
    int view_ = 0;
    int lastRoom_ = -2;

    std::array<juce::TextButton, 4> tabs_;
    juce::Label statusLabel_;
    juce::Slider masterKnob_;
    std::unique_ptr<ui::SliderAtt> masterAtt_;

    // kit page
    juce::ComboBox kitBox_, roomBox_;
    juce::TextButton foldersButton_;
    juce::TextButton swapButton_;
    juce::TextButton resetButton_;
    juce::Label pieceLabel_;
    KitView kitView_;
    std::unique_ptr<ui::ComboAtt> roomAtt_;

    ui::MixView mixView_;
    ui::GroovesView groovesView_;
    ui::MatrixView matrixView_;

    std::unique_ptr<juce::FileChooser> chooser_;
    std::vector<juce::String> kitIds_;
    juce::String selectedSlot_;
    bool refreshing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenDrumsEditor)
};
