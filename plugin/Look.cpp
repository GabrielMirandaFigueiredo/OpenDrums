#include "Look.h"

#include <map>

namespace ui
{
juce::Colour groupColour(int strip)
{
    if (strip == 0) return juce::Colour(0xffc4524a);               // kick
    if (strip == 1) return juce::Colour(0xffe8a33d);               // snare
    if (strip >= 2 && strip <= 5) return juce::Colour(0xff6aa84f); // toms
    if (strip == 6) return juce::Colour(0xff4aa3df);               // hi-hat
    if (strip >= 7 && strip <= 11) return juce::Colour(0xffc8a24a); // cymbals
    return juce::Colour(0xff9b72cf);                               // percussion
}

void drawPrims(juce::Graphics& g, const std::vector<od::geo::Prim>& prims)
{
    using Kind = od::geo::Prim::Kind;
    for (const auto& p : prims)
    {
        g.setColour(juce::Colour(p.color));
        switch (p.kind)
        {
            case Kind::Line:
            {
                juce::Path path;
                path.startNewSubPath(p.x1, p.y1);
                path.lineTo(p.x2, p.y2);
                g.strokePath(path, juce::PathStrokeType(p.width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
            }
            case Kind::Ellipse: g.fillEllipse(p.x1 - p.x2, p.y1 - p.y2, p.x2 * 2.0f, p.y2 * 2.0f); break;
            case Kind::RoundRect: g.fillRoundedRectangle(p.x1, p.y1, p.x2, p.y2, p.width); break;
            case Kind::Poly:
            {
                if (p.pts.size() < 6) break;
                juce::Path path;
                path.startNewSubPath(p.pts[0], p.pts[1]);
                for (size_t i = 2; i + 1 < p.pts.size(); i += 2) path.lineTo(p.pts[i], p.pts[i + 1]);
                path.closeSubPath();
                g.fillPath(path);
                break;
            }
        }
    }
}

std::shared_ptr<juce::Drawable> pieceDrawable(const std::filesystem::path& file)
{
    static std::map<juce::String, std::shared_ptr<juce::Drawable>> cache;
    if (file.empty()) return nullptr;
    const juce::String key(file.string());
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::shared_ptr<juce::Drawable> d;
    const juce::File f(key);
    if (f.existsAsFile()) d = std::shared_ptr<juce::Drawable>(juce::Drawable::createFromImageFile(f).release());
    cache[key] = d;
    return d;
}

void drawTag(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour fill, float fontSize)
{
    g.setColour(fill);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(kOutline);
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.5f);
    g.setColour(kOutline.brighter(0.0f));
    g.setFont(juce::FontOptions(fontSize, juce::Font::bold));
    g.drawText(text, r.toNearestInt(), juce::Justification::centred);
}

CartoonLookAndFeel::CartoonLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBg);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::TextButton::buttonColourId, kPanelHi);
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, kOutline);
    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::ComboBox::backgroundColourId, kPanelHi);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::outlineColourId, kOutline);
    setColour(juce::ComboBox::arrowColourId, kAccent);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour(juce::PopupMenu::highlightedTextColourId, kOutline);
    setColour(juce::PopupMenu::headerTextColourId, kAccent);
    setColour(juce::TextEditor::backgroundColourId, kBg);
    setColour(juce::TextEditor::textColourId, kText);
    setColour(juce::TextEditor::outlineColourId, kOutline);
    setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
    setColour(juce::ScrollBar::thumbColourId, kPanelHi.brighter(0.3f));
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxBackgroundColourId, kBg);
    setColour(juce::Slider::textBoxOutlineColourId, kOutline);
    setColour(juce::TooltipWindow::backgroundColourId, kPanel);
    setColour(juce::TooltipWindow::textColourId, kText);
}

void CartoonLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                          juce::Slider& s)
{
    const auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2.0f);
    const float d = juce::jmin(b.getWidth(), b.getHeight());
    const auto r = juce::Rectangle<float>(d, d).withCentre(b.getCentre());
    const auto cap = s.findColour(kKnobColourId);
    const float angle = start + pos * (end - start);

    // travel ring
    g.setColour(kOutline);
    g.fillEllipse(r.expanded(1.5f));
    juce::Path arc;
    arc.addCentredArc(r.getCentreX(), r.getCentreY(), d * 0.5f + 0.5f, d * 0.5f + 0.5f, 0.0f, start, angle, true);
    g.setColour(cap.withAlpha(0.55f));
    g.strokePath(arc, juce::PathStrokeType(2.0f));

    // body: outlined cap with a cartoon highlight
    const auto body = r.reduced(d * 0.09f);
    g.setColour(kOutline);
    g.fillEllipse(body.expanded(1.5f));
    g.setColour(cap);
    g.fillEllipse(body);
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    g.fillEllipse(body.reduced(body.getWidth() * 0.18f).translated(-body.getWidth() * 0.06f, -body.getHeight() * 0.10f));

    // pointer
    const float len = body.getWidth() * 0.46f;
    const auto c = body.getCentre();
    juce::Path p;
    p.startNewSubPath(c);
    p.lineTo(c.x + std::sin(angle) * len, c.y - std::cos(angle) * len);
    g.setColour(kOutline);
    g.strokePath(p, juce::PathStrokeType(juce::jmax(2.0f, d * 0.1f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CartoonLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cx = (float) x + (float) w * 0.5f;
    const float top = (float) y + 6.0f, bot = (float) y + (float) h - 6.0f;

    // groove
    const auto groove = juce::Rectangle<float>(7.0f, bot - top + 4.0f).withCentre({cx, (top + bot) * 0.5f});
    g.setColour(kOutline);
    g.fillRoundedRectangle(groove.expanded(1.5f), 4.0f);
    g.setColour(juce::Colour(0xff0a0b0d));
    g.fillRoundedRectangle(groove, 3.0f);

    // scale ticks (0 dB is at the point where the fader is at unity)
    g.setColour(kTextDim.withAlpha(0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = top + (bot - top) * (float) i / 10.0f;
        g.drawHorizontalLine((int) ty, cx - 16.0f, cx - 10.0f);
        g.drawHorizontalLine((int) ty, cx + 10.0f, cx + 16.0f);
    }

    // cap
    const auto cap = juce::Rectangle<float>(34.0f, 22.0f).withCentre({cx, pos});
    g.setColour(kOutline);
    g.fillRoundedRectangle(cap.expanded(1.5f), 6.0f);
    g.setColour(s.findColour(kKnobColourId));
    g.fillRoundedRectangle(cap, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.30f));
    g.fillRoundedRectangle(cap.reduced(3.0f).removeFromTop(6.0f), 3.0f);
    g.setColour(kOutline);
    g.fillRect(cap.withSizeKeepingCentre(cap.getWidth() - 8.0f, 2.5f));
}

void CartoonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour(juce::TextButton::buttonOnColourId) : b.findColour(juce::TextButton::buttonColourId);
    if (hover) fill = fill.brighter(0.12f);
    if (down) fill = fill.darker(0.15f);
    const float rad = juce::jmin(8.0f, r.getHeight() * 0.35f);
    g.setColour(kOutline);
    g.fillRoundedRectangle(r.translated(0.0f, down ? 0.0f : 1.5f), rad);
    g.setColour(fill);
    g.fillRoundedRectangle(r.translated(0.0f, down ? 1.0f : 0.0f).withTrimmedBottom(1.5f), rad);
    g.setColour(juce::Colours::white.withAlpha(on ? 0.25f : 0.08f));
    g.fillRoundedRectangle(r.reduced(3.0f).removeFromTop(r.getHeight() * 0.3f), rad * 0.6f);
}

void CartoonLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                    .withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
    g.drawText(b.getButtonText(), b.getLocalBounds().reduced(2), juce::Justification::centred);
}

void CartoonLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    const auto led = juce::Rectangle<float>(r.getHeight() * 0.55f, r.getHeight() * 0.55f).withCentre({r.getX() + r.getHeight() * 0.45f, r.getCentreY()});
    g.setColour(kOutline);
    g.fillEllipse(led.expanded(1.5f));
    g.setColour(on ? kAccent : kPanelHi);
    g.fillEllipse(led);
    if (on) { g.setColour(juce::Colours::white.withAlpha(0.5f)); g.fillEllipse(led.reduced(led.getWidth() * 0.3f).translated(-1.0f, -1.0f)); }
    g.setColour(hover ? kText : kText.withAlpha(0.85f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(r.getHeight() * 0.95f).toNearestInt(), juce::Justification::centredLeft);
}

void CartoonLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(1.0f);
    g.setColour(kOutline);
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 7.0f);
    g.setColour(down ? kPanelHi.brighter(0.1f) : box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r.withTrimmedBottom(1.5f), 7.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(kAccent);
    g.fillPath(arrow);
}

void CartoonLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}
} // namespace ui
