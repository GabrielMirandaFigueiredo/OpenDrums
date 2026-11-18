#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "KitGeometry.hpp"

#include <filesystem>
#include <memory>

// Shared look of the plugin: dark "cartoon" panels with thick outlines, chunky knobs and faders.
namespace ui
{
inline juce::String T(const char* utf8) { return juce::String::fromUTF8(utf8); }

const juce::Colour kBg(0xff1a1c21);
const juce::Colour kPanel(0xff262a31);
const juce::Colour kPanelHi(0xff323843);
const juce::Colour kOutline(0xff0d0e11);
const juce::Colour kAccent(0xffe8a33d);
const juce::Colour kText(0xffdfe2e8);
const juce::Colour kTextDim(0xff8b909c);
const juce::Colour kGreen(0xff6fcf6a);
const juce::Colour kRed(0xffe0564f);
const juce::Colour kBlue(0xff4aa3df);
const juce::Colour kBrown(0xffa8754a);

// Colour of a strip's header by instrument group (kick/snare/toms/hats/cymbals/percussion).
juce::Colour groupColour(int strip);

// Draws the primitives produced by core (kit hardware, room scenes).
void drawPrims(juce::Graphics& g, const std::vector<od::geo::Prim>& prims);

// Picture of a drum piece (cached; nullptr when missing).
std::shared_ptr<juce::Drawable> pieceDrawable(const std::filesystem::path& file);

// A pill-shaped caption used for little section titles.
void drawTag(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour fill,
             float fontSize = 10.0f);

class CartoonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CartoonLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool hover, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(juce::FontOptions(12.0f)); }
    juce::Font getTextButtonFont(juce::TextButton& b, int) override { return juce::Font(juce::FontOptions(b.getWidth() < 36 ? 8.5f : b.getWidth() < 44 ? 9.5f : 12.0f, juce::Font::bold)); }
};

// Colour ids used on buttons/sliders by the views.
constexpr int kKnobColourId = juce::Slider::rotarySliderFillColourId;
} // namespace ui
