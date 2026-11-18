#include "MixView.h"

#include <cmath>

namespace ui
{
// ---------------------------------------------------------------- KnobCell

KnobCell::KnobCell(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& caption,
                   juce::Colour colour, bool below)
    : caption_(caption), below_(below)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setColour(kKnobColourId, colour);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(slider);
    att_ = std::make_unique<SliderAtt>(apvts, id, slider);
}

void KnobCell::resized()
{
    auto r = getLocalBounds();
    if (below_) r.removeFromBottom(11);
    slider.setBounds(r);
}

void KnobCell::paint(juce::Graphics& g)
{
    if (!below_) return;
    g.setColour(kTextDim);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(caption_, getLocalBounds().removeFromBottom(11), juce::Justification::centred);
}

// ---------------------------------------------------------------- Meter

void Meter::setLevelLinear(float lin)
{
    const float db = lin > 1.0e-5f ? 20.0f * std::log10(lin) : -90.0f;
    value_ = db > value_ ? db : juce::jmax(db, value_ - 2.2f); // fast attack, slow fall
    repaint();
}

void Meter::setReductionDb(float db)
{
    value_ = db;
    repaint();
}

void Meter::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour(kOutline);
    g.fillRoundedRectangle(r, 3.0f);
    const auto in = r.reduced(1.5f);
    g.setColour(juce::Colour(0xff0a0b0d));
    g.fillRoundedRectangle(in, 2.0f);

    if (gr_)
    {
        const float frac = juce::jlimit(0.0f, 1.0f, value_ / 18.0f);
        g.setColour(kAccent);
        g.fillRoundedRectangle(in.withHeight(in.getHeight() * frac), 2.0f);
        return;
    }
    const float frac = juce::jlimit(0.0f, 1.0f, (value_ + 60.0f) / 66.0f);
    const float h = in.getHeight() * frac;
    const int segs = 22;
    const float segH = in.getHeight() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float yb = in.getBottom() - (float) i * segH;
        if (in.getBottom() - yb + segH > h + 0.5f) break;
        const float t = (float) i / (float) segs;
        g.setColour(t > 0.88f ? kRed : t > 0.72f ? juce::Colour(0xffe8d33d) : kGreen);
        g.fillRect(in.getX() + 1.0f, yb - segH + 1.0f, in.getWidth() - 2.0f, segH - 1.2f);
    }
}

// ---------------------------------------------------------------- ChannelStrip

namespace
{
void styleToggle(juce::TextButton& b, juce::Colour on)
{
    b.setClickingTogglesState(true);
    b.setColour(juce::TextButton::buttonOnColourId, on);
}
} // namespace

ChannelStrip::ChannelStrip(OpenDrumsProcessor& p, int index) : proc_(p), index_(index)
{
    auto& ap = p.apvts;
    auto id = [&](const char* k) { return pid::strip(index, k); };

    // output selector: Master, stereo pairs, mono outputs
    for (int bit = 0; bit < od::kNumRoutes; ++bit)
        outBox_.addItem(juce::String(od::routeName(bit)), bit + 1);
    outBox_.onChange = [this] {
        const int sel = outBox_.getSelectedId();
        if (sel >= 1 && sel <= od::kNumRoutes)
        {
            proc_.setRouteMask(index_, 1u << (sel - 1));
            lastMask_ = (int) proc_.getRouteMask(index_);
        }
    };
    addAndMakeVisible(outBox_);

    tabProc_.setClickingTogglesState(false);
    tabSends_.setClickingTogglesState(false);
    tabProc_.onClick = [this] { showTab(false); };
    tabSends_.onClick = [this] { showTab(true); };
    addAndMakeVisible(tabProc_);
    addAndMakeVisible(tabSends_);

    auto attach = [&](juce::Button& b, const char* key) {
        addAndMakeVisible(b);
        buttonAtts_.push_back(std::make_unique<ButtonAtt>(ap, id(key), b));
    };
    styleToggle(cOn_, kGreen);   attach(cOn_, "c_on");
    styleToggle(cSpeed_, kBlue); attach(cSpeed_, "c_fast");
    cSpeed_.onStateChange = [this] { cSpeed_.setButtonText(cSpeed_.getToggleState() ? "FAST" : "SLOW"); };
    cSpeed_.setButtonText(cSpeed_.getToggleState() ? "FAST" : "SLOW");
    styleToggle(eOn_, kGreen);   attach(eOn_, "e_on");
    styleToggle(hfBell_, kRed);  attach(hfBell_, "hf_bell");
    styleToggle(lfBell_, kBrown); attach(lfBell_, "lf_bell");
    styleToggle(mute_, kRed);    attach(mute_, "mute");
    styleToggle(solo_, juce::Colour(0xffe8d33d)); attach(solo_, "solo");
    styleToggle(phase_, kBlue);  attach(phase_, "phase");

    const juce::Colour cC = kAccent, cHF = kRed, cHM = kGreen, cLM = kBlue, cLF = kBrown;
    const char* ck[4] = {"c_thr", "c_ratio", "c_makeup", "c_rel"};
    const char* cn[4] = {"THR", "RATIO", "MKUP", "REL"};
    for (int i = 0; i < 4; ++i) { comp_[(size_t) i] = std::make_unique<KnobCell>(ap, id(ck[i]), cn[i], cC); addAndMakeVisible(*comp_[(size_t) i]); }
    struct E { const char* k; const char* n; juce::Colour c; };
    const E eqs[11] = {{"hf_f", "HF", cHF}, {"hf_g", "dB", cHF}, {"hmf_f", "HMF", cHM}, {"hmf_g", "dB", cHM}, {"hmf_q", "Q", cHM},
                       {"lmf_f", "LMF", cLM}, {"lmf_g", "dB", cLM}, {"lmf_q", "Q", cLM}, {"lf_f", "LF", cLF}, {"lf_g", "dB", cLF}, {"", "", cLF}};
    for (int i = 0; i < 10; ++i) { eq_[(size_t) i] = std::make_unique<KnobCell>(ap, id(eqs[i].k), eqs[i].n, eqs[i].c); addAndMakeVisible(*eq_[(size_t) i]); }

    pan_ = std::make_unique<KnobCell>(ap, id("pan"), "PAN", kText);
    addAndMakeVisible(*pan_);
    sendRv_ = std::make_unique<KnobCell>(ap, id("sendrv"), "REVERB", kBlue);
    sendPar_ = std::make_unique<KnobCell>(ap, id("sendpar"), "PARALELO", kAccent);
    addChildComponent(*sendRv_);
    addChildComponent(*sendPar_);

    fader_.setSliderStyle(juce::Slider::LinearVertical);
    fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader_.setColour(kKnobColourId, kAccent);
    fader_.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(fader_);
    faderAtt_ = std::make_unique<SliderAtt>(ap, id("fader"), fader_);
    addAndMakeVisible(meter_);
    addAndMakeVisible(grMeter_);

    showTab(false);
    refreshRoute();
}

void ChannelStrip::showTab(bool sends)
{
    sends_ = sends;
    tabProc_.setToggleState(!sends, juce::dontSendNotification);
    tabSends_.setToggleState(sends, juce::dontSendNotification);
    for (auto& k : comp_) k->setVisible(!sends);
    for (auto& k : eq_) if (k) k->setVisible(!sends);
    cOn_.setVisible(!sends); cSpeed_.setVisible(!sends); eOn_.setVisible(!sends);
    hfBell_.setVisible(!sends); lfBell_.setVisible(!sends); grMeter_.setVisible(!sends);
    sendRv_->setVisible(sends);
    sendPar_->setVisible(sends);
    repaint();
}

void ChannelStrip::setSlot(const juce::String& label, std::shared_ptr<juce::Drawable> pic, bool present)
{
    pieceLabel_ = label;
    picture_ = std::move(pic);
    present_ = present;
    repaint();
}

void ChannelStrip::refreshRoute()
{
    const int mask = (int) proc_.getRouteMask(index_);
    if (mask == lastMask_) return;
    lastMask_ = mask;
    int bits = 0, first = -1;
    for (int b = 0; b < od::kNumRoutes; ++b)
        if (mask & (1 << b)) { ++bits; if (first < 0) first = b; }
    if (bits == 1)
        outBox_.setSelectedId(first + 1, juce::dontSendNotification);
    else
        outBox_.setText(bits == 0 ? T("Sem saída") : juce::String(bits) + T(" saídas"), juce::dontSendNotification);
}

void ChannelStrip::updateMeters()
{
    meter_.setLevelLinear(proc_.takeStripPeak(index_));
    grMeter_.setReductionDb(-proc_.stripGainReductionDb(index_));
    refreshRoute();
}

void ChannelStrip::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(kOutline);
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(present_ ? kPanel : kPanel.darker(0.3f));
    g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);

    // header with the piece name
    const auto head = juce::Rectangle<float>(r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 20.0f);
    drawTag(g, head, juce::String::fromUTF8(stripLabel(index_)), groupColour(index_), 11.0f);

    // piece picture
    g.setColour(juce::Colour(0xff15171b));
    g.fillRoundedRectangle(picBox_.toFloat(), 6.0f);
    if (picture_ != nullptr && present_)
    {
        const auto src = picture_->getDrawableBounds();
        const auto fit = juce::RectanglePlacement(juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize)
                             .getTransformToFit(src, picBox_.toFloat().reduced(4.0f));
        picture_->draw(g, present_ ? 1.0f : 0.3f, fit);
    }
    else
    {
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(T("sem peça"), picBox_, juce::Justification::centred);
    }

    auto section = [&](juce::Rectangle<int> b, const juce::String& title) {
        g.setColour(juce::Colour(0xff1d2025));
        g.fillRoundedRectangle(b.toFloat(), 6.0f);
        g.setColour(kOutline);
        g.drawRoundedRectangle(b.toFloat(), 6.0f, 1.2f);
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(title, b.getX() + 6, b.getY() + 2, 60, 12, juce::Justification::centredLeft);
    };
    if (!sends_)
    {
        section(compBox_, "COMP");
        section(eqBox_, "EQ");
    }
    else
    {
        section(sendsBox_, "SENDS");
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(10.0f));
        g.drawFittedText(T("Envios pós-fader para os buses de reverb e de compressão paralela."),
                         sendsBox_.reduced(8).removeFromBottom(46), juce::Justification::centred, 3);
    }
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced(5);
    r.removeFromTop(22); // header tag
    r.removeFromTop(4);
    picBox_ = r.removeFromTop(juce::jlimit(34, 56, r.getWidth() - 20));
    r.removeFromTop(4);
    outBox_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    auto tabs = r.removeFromTop(20);
    tabProc_.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2).reduced(1, 0));
    tabSends_.setBounds(tabs.reduced(1, 0));
    r.removeFromTop(4);

    const int area = 276;
    auto proc = r.removeFromTop(area);
    sendsBox_ = proc;
    {
        compBox_ = proc.removeFromTop(104);
        proc.removeFromTop(4);
        eqBox_ = proc;

        auto c = compBox_.reduced(3);
        c.removeFromTop(11);
        const int cw = c.getWidth() / 3;
        const int ch = (c.getHeight() - 4) / 2;
        auto row1 = c.removeFromTop(ch);
        auto row2 = c.removeFromBottom(ch);
        for (int i = 0; i < 3; ++i) comp_[(size_t) i]->setBounds(row1.removeFromLeft(cw));
        comp_[3]->setBounds(row2.removeFromLeft(cw));
        cSpeed_.setBounds(row2.removeFromLeft(cw).reduced(1, 8));
        cOn_.setBounds(row2.reduced(1, 8));
        grMeter_.setBounds(compBox_.getX() + 40, compBox_.getY() + 3, compBox_.getWidth() - 80, 6);

        auto e = eqBox_.reduced(3);
        e.removeFromTop(13);
        eOn_.setBounds(eqBox_.getRight() - 40, eqBox_.getY() + 2, 36, 13);
        const int rowH = e.getHeight() / 4;
        const int w3 = e.getWidth() / 3;
        auto row = [&](int rIdx) { return juce::Rectangle<int>(e.getX(), e.getY() + rIdx * rowH, e.getWidth(), rowH); };
        auto a = row(0);
        eq_[0]->setBounds(a.removeFromLeft(w3)); eq_[1]->setBounds(a.removeFromLeft(w3));
        hfBell_.setBounds(a.reduced(1, 6));
        a = row(1);
        for (int i = 0; i < 3; ++i) eq_[(size_t) (2 + i)]->setBounds(a.removeFromLeft(w3));
        a = row(2);
        for (int i = 0; i < 3; ++i) eq_[(size_t) (5 + i)]->setBounds(a.removeFromLeft(w3));
        a = row(3);
        eq_[8]->setBounds(a.removeFromLeft(w3)); eq_[9]->setBounds(a.removeFromLeft(w3));
        lfBell_.setBounds(a.reduced(1, 6));

        auto s = sendsBox_.reduced(6);
        s.removeFromTop(16);
        const int kw = s.getWidth();
        sendRv_->setBounds(s.removeFromTop(64).withSizeKeepingCentre(juce::jmin(kw, 60), 64));
        sendPar_->setBounds(s.removeFromTop(64).withSizeKeepingCentre(juce::jmin(kw, 60), 64));
    }
    r.removeFromTop(4);

    auto panRow = r.removeFromTop(44);
    pan_->setBounds(panRow.withSizeKeepingCentre(44, 44));
    r.removeFromTop(2);
    auto msRow = r.removeFromTop(22);
    const int bw = msRow.getWidth() / 3;
    mute_.setBounds(msRow.removeFromLeft(bw).reduced(1, 0));
    solo_.setBounds(msRow.removeFromLeft(bw).reduced(1, 0));
    phase_.setBounds(msRow.reduced(1, 0));
    r.removeFromTop(4);

    auto faderArea = r;
    meter_.setBounds(faderArea.removeFromRight(11).reduced(0, 6));
    fader_.setBounds(faderArea);
}

// ---------------------------------------------------------------- bus strips

namespace
{
// Parallel-compression bus (own compressor) with a return fader.
class ParallelStrip : public juce::Component
{
public:
    explicit ParallelStrip(OpenDrumsProcessor& p) : proc_(p)
    {
        auto& ap = p.apvts;
        const char* k[4] = {"par_thr", "par_ratio", "par_makeup", "par_rel"};
        const char* n[4] = {"THR", "RATIO", "MKUP", "REL"};
        for (int i = 0; i < 4; ++i) { knobs_[(size_t) i] = std::make_unique<KnobCell>(ap, k[i], n[i], kAccent); addAndMakeVisible(*knobs_[(size_t) i]); }
        speed_.setClickingTogglesState(true);
        speed_.setColour(juce::TextButton::buttonOnColourId, kBlue);
        addAndMakeVisible(speed_);
        speedAtt_ = std::make_unique<ButtonAtt>(ap, "par_fast", speed_);
        speed_.onStateChange = [this] { speed_.setButtonText(speed_.getToggleState() ? "FAST" : "SLOW"); };
        speed_.setButtonText(speed_.getToggleState() ? "FAST" : "SLOW");
        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader_.setColour(kKnobColourId, kAccent);
        fader_.setPopupDisplayEnabled(true, true, nullptr);
        addAndMakeVisible(fader_);
        faderAtt_ = std::make_unique<SliderAtt>(ap, "par_ret", fader_);
        addAndMakeVisible(gr_);
    }
    void updateMeters() { gr_.setReductionDb(-proc_.parallelGainReductionDb()); }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(kOutline);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);
        drawTag(g, juce::Rectangle<float>(r.getX() + 4, r.getY() + 4, r.getWidth() - 8, 20), "COMP PARALELO", juce::Colour(0xffd9534f), 10.0f);
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText("GR", gr_.getX() - 18, gr_.getY() - 2, 16, 10, juce::Justification::centredRight);
        g.drawText("RETORNO", faderLabel_, juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(6);
        r.removeFromTop(30);
        const int kh = 56;
        auto a = r.removeFromTop(kh);
        knobs_[0]->setBounds(a.removeFromLeft(a.getWidth() / 2));
        knobs_[1]->setBounds(a);
        a = r.removeFromTop(kh);
        knobs_[2]->setBounds(a.removeFromLeft(a.getWidth() / 2));
        knobs_[3]->setBounds(a);
        r.removeFromTop(6);
        speed_.setBounds(r.removeFromTop(22).reduced(6, 0));
        r.removeFromTop(8);
        gr_.setBounds(r.removeFromTop(8).withTrimmedLeft(20).reduced(0, 0));
        r.removeFromTop(14);
        faderLabel_ = r.removeFromBottom(12);
        fader_.setBounds(r);
    }

private:
    OpenDrumsProcessor& proc_;
    std::array<std::unique_ptr<KnobCell>, 4> knobs_;
    juce::TextButton speed_;
    juce::Slider fader_;
    Meter gr_{true};
    std::unique_ptr<ButtonAtt> speedAtt_;
    std::unique_ptr<SliderAtt> faderAtt_;
    juce::Rectangle<int> faderLabel_;
};

class ReverbStrip : public juce::Component
{
public:
    explicit ReverbStrip(OpenDrumsProcessor& p)
    {
        auto& ap = p.apvts;
        for (int i = 0; i < 8; ++i) room_.addItem(juce::String::fromUTF8(od::reverbRoom(i).name), i + 1);
        addAndMakeVisible(room_);
        roomAtt_ = std::make_unique<ComboAtt>(ap, "rv_room", room_);
        const char* k[6] = {"rv_decay", "rv_pre", "rv_tone", "rv_width", "rv_low", "rv_high"};
        const char* n[6] = {"DECAY", "PRE", "TOM", "LARG.", "LOW", "HIGH"};
        for (int i = 0; i < 6; ++i) { knobs_[(size_t) i] = std::make_unique<KnobCell>(ap, k[i], n[i], kBlue); addAndMakeVisible(*knobs_[(size_t) i]); }
        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader_.setColour(kKnobColourId, kBlue);
        fader_.setPopupDisplayEnabled(true, true, nullptr);
        addAndMakeVisible(fader_);
        faderAtt_ = std::make_unique<SliderAtt>(ap, "rv_ret", fader_);
    }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(kOutline);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);
        drawTag(g, juce::Rectangle<float>(r.getX() + 4, r.getY() + 4, r.getWidth() - 8, 20), "REVERB", kBlue, 11.0f);
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText("RETORNO", faderLabel_, juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(6);
        r.removeFromTop(30);
        room_.setBounds(r.removeFromTop(22));
        r.removeFromTop(6);
        for (int row = 0; row < 3; ++row)
        {
            auto a = r.removeFromTop(54);
            knobs_[(size_t) (row * 2)]->setBounds(a.removeFromLeft(a.getWidth() / 2));
            knobs_[(size_t) (row * 2 + 1)]->setBounds(a);
        }
        r.removeFromTop(6);
        faderLabel_ = r.removeFromBottom(12);
        fader_.setBounds(r);
    }

private:
    juce::ComboBox room_;
    std::array<std::unique_ptr<KnobCell>, 6> knobs_;
    juce::Slider fader_;
    std::unique_ptr<ComboAtt> roomAtt_;
    std::unique_ptr<SliderAtt> faderAtt_;
    juce::Rectangle<int> faderLabel_;
};

class MasterStrip : public juce::Component
{
public:
    explicit MasterStrip(OpenDrumsProcessor& p) : proc_(p)
    {
        fader_.setSliderStyle(juce::Slider::LinearVertical);
        fader_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader_.setColour(kKnobColourId, kText);
        fader_.setPopupDisplayEnabled(true, true, nullptr);
        addAndMakeVisible(fader_);
        att_ = std::make_unique<SliderAtt>(p.apvts, "master", fader_);
        addAndMakeVisible(mL_);
        addAndMakeVisible(mR_);
    }
    void updateMeters()
    {
        mL_.setLevelLinear(proc_.takeMasterPeak(0));
        mR_.setLevelLinear(proc_.takeMasterPeak(1));
    }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(kOutline);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);
        drawTag(g, juce::Rectangle<float>(r.getX() + 4, r.getY() + 4, r.getWidth() - 8, 20), "MASTER", kText, 11.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(6);
        r.removeFromTop(30);
        auto meters = r.removeFromRight(24);
        mL_.setBounds(meters.removeFromLeft(11).reduced(0, 6));
        mR_.setBounds(meters.removeFromRight(11).reduced(0, 6));
        fader_.setBounds(r);
    }

private:
    OpenDrumsProcessor& proc_;
    juce::Slider fader_;
    Meter mL_, mR_;
    std::unique_ptr<SliderAtt> att_;
};
} // namespace

// ---------------------------------------------------------------- MixView

class MixView::Content : public juce::Component // the scrolling strips
{
public:
    explicit Content(OpenDrumsProcessor& p)
    {
        for (int i = 0; i < od::kNumStrips; ++i)
        {
            strips[(size_t) i] = std::make_unique<ChannelStrip>(p, i);
            addAndMakeVisible(*strips[(size_t) i]);
        }
    }
    static constexpr int kStripW = 100, kGap = 3;
    static int widthNeeded() { return od::kNumStrips * (kStripW + kGap) + 8; }
    void resized() override
    {
        int x = 4;
        const int h = getHeight() - 8;
        for (auto& s : strips) { s->setBounds(x, 4, kStripW, h); x += kStripW + kGap; }
    }
    std::array<std::unique_ptr<ChannelStrip>, od::kNumStrips> strips;
};

MixView::MixView(OpenDrumsProcessor& p) : proc_(p)
{
    content_ = std::make_unique<Content>(p);
    auto par = std::make_unique<ParallelStrip>(p);
    auto mas = std::make_unique<MasterStrip>(p);
    updateBusMeters_ = [a = par.get(), b = mas.get()] { a->updateMeters(); b->updateMeters(); };
    parallel_ = std::move(par);
    reverb_ = std::make_unique<ReverbStrip>(p);
    master_ = std::move(mas);
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
    addAndMakeVisible(*parallel_);
    addAndMakeVisible(*reverb_);
    addAndMakeVisible(*master_);
    startTimerHz(30);
}

MixView::~MixView() { stopTimer(); updateBusMeters_ = nullptr; }

void MixView::setRig(std::shared_ptr<const od::Rig> rig)
{
    for (int i = 0; i < od::kNumStrips; ++i)
    {
        const od::SlotView* view = nullptr;
        if (rig)
            for (const auto& s : rig->slots)
                if (s.view.slot == od::stripId(i) && !s.view.pieceRef.empty())
                    view = &s.view;
        content_->strips[(size_t) i]->setSlot(view ? juce::String(view->label) : juce::String(),
                                              view ? pieceDrawable(view->imagePath) : nullptr, view != nullptr);
    }
}

void MixView::resized()
{
    constexpr int busW = 128, masterW = 90, gap = 3;
    auto r = getLocalBounds().reduced(0, 4);
    master_->setBounds(r.removeFromRight(masterW + 4).withTrimmedRight(4));
    reverb_->setBounds(r.removeFromRight(busW + gap).withTrimmedRight(gap));
    parallel_->setBounds(r.removeFromRight(busW + gap).withTrimmedRight(gap + 6));
    viewport_.setBounds(getLocalBounds().withTrimmedRight(getWidth() - r.getRight()));
    const int w = juce::jmax(viewport_.getWidth(), Content::widthNeeded());
    content_->setSize(w, viewport_.getHeight() - (w > viewport_.getWidth() ? 10 : 0));
}

void MixView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171b));
}

void MixView::timerCallback()
{
    if (!isShowing()) { for (int i = 0; i < od::kNumStrips; ++i) (void) proc_.takeStripPeak(i); return; }
    for (auto& s : content_->strips) s->updateMeters();
    if (updateBusMeters_) updateBusMeters_();
}
} // namespace ui
