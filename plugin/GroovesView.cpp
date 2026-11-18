#include "GroovesView.h"

namespace ui
{
namespace
{
const juce::Colour kRowColours[od::kThumbRows] = {juce::Colour(0xffc4524a), juce::Colour(0xffe8a33d), juce::Colour(0xff6aa84f),
                                                  juce::Colour(0xff4aa3df), juce::Colour(0xffc8a24a), juce::Colour(0xff9b72cf)};
}

// One folder column. Rows are simple names; the selected one is highlighted and shows a chevron.
class GroovesView::Column : public juce::Component, private juce::ListBoxModel
{
public:
    Column(const juce::String& title) : title_(title)
    {
        list_.setModel(this);
        list_.setRowHeight(30);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list_.setOutlineThickness(0);
        addAndMakeVisible(list_);
    }
    void setItems(juce::StringArray items, int selected)
    {
        items_ = std::move(items);
        selected_ = selected;
        list_.updateContent();
        list_.repaint();
    }
    std::function<void(int)> onSelect;
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(kOutline);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);
        drawTag(g, juce::Rectangle<float>(r.getX() + 6, r.getY() + 6, r.getWidth() - 12, 20), title_, kAccent, 11.0f);
        if (items_.isEmpty())
        {
            g.setColour(kTextDim);
            g.setFont(juce::FontOptions(11.0f));
            g.drawFittedText(T("(escolha à esquerda)"), getLocalBounds().withTrimmedTop(40).reduced(10), juce::Justification::centredTop, 2);
        }
    }
    void resized() override { list_.setBounds(getLocalBounds().withTrimmedTop(32).reduced(6, 4)); }

private:
    int getNumRows() override { return items_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= items_.size()) return;
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1.0f, 1.5f);
        const bool sel = row == selected_;
        g.setColour(sel ? kAccent : kPanelHi);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(sel ? kOutline : kText);
        g.setFont(juce::FontOptions(13.0f, sel ? juce::Font::bold : juce::Font::plain));
        // little folder glyph
        const auto f = juce::Rectangle<float>(10.0f, r.getCentreY() - 6.0f, 15.0f, 11.0f);
        g.setColour(sel ? kOutline : kAccent);
        g.fillRoundedRectangle(f.withTrimmedTop(2.0f), 2.0f);
        g.fillRoundedRectangle(f.withWidth(7.0f).withHeight(4.0f), 1.5f);
        g.setColour(sel ? kOutline : kText);
        g.drawText(items_[row], 32, 0, w - 52, h, juce::Justification::centredLeft, true);
        g.drawText(">", w - 22, 0, 16, h, juce::Justification::centred);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < items_.size() && onSelect) onSelect(row);
    }

    juce::String title_;
    juce::StringArray items_;
    int selected_ = -1;
    juce::ListBox list_;
};

// Clip rows: mini piano roll, name, favourite star. Click selects, drag-out sends the .mid to the DAW.
class GroovesView::ClipList : public juce::Component, private juce::ListBoxModel
{
public:
    ClipList(OpenDrumsProcessor& p, GroovesView& owner) : proc_(p), owner_(owner)
    {
        list_.setModel(this);
        list_.setRowHeight(46);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list_.setOutlineThickness(0);
        addAndMakeVisible(list_);
        list_.addMouseListener(this, true);
    }
    void update() { list_.updateContent(); list_.repaint(); }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(kOutline);
        g.fillRoundedRectangle(r, 9.0f);
        g.setColour(kPanel);
        g.fillRoundedRectangle(r.reduced(1.5f), 8.0f);
        drawTag(g, juce::Rectangle<float>(r.getX() + 6, r.getY() + 6, 90, 20), T("CLIPES MIDI"), kAccent, 11.0f);
        if (owner_.entries_.empty())
        {
            g.setColour(kTextDim);
            g.setFont(juce::FontOptions(13.0f));
            g.drawFittedText(T("Escolha um estilo, um compasso/BPM e uma seção para ver os clipes."),
                             getLocalBounds().reduced(30), juce::Justification::centred, 3);
        }
    }
    void resized() override { list_.setBounds(getLocalBounds().withTrimmedTop(32).reduced(6, 4)); }
    void scrollToTop() { list_.scrollToEnsureRowIsOnscreen(0); }

private:
    int getNumRows() override { return (int) owner_.entries_.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) owner_.entries_.size()) return;
        const auto& e = owner_.entries_[(size_t) row];
        const bool current = juce::String(e.clip->relPath) == proc_.getCurrentGrooveRel();
        const auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1.0f, 2.0f);
        g.setColour(current ? kAccent.withAlpha(0.95f) : kPanelHi);
        g.fillRoundedRectangle(r, 8.0f);
        const auto fg = current ? kOutline : kText;

        // mini piano roll
        const auto thumb = juce::Rectangle<float>(r.getX() + 8.0f, r.getY() + 5.0f, 150.0f, r.getHeight() - 10.0f);
        g.setColour(current ? juce::Colour(0xff2a2218) : juce::Colour(0xff15171b));
        g.fillRoundedRectangle(thumb, 5.0f);
        if (auto clip = proc_.clipFor(*e.clip))
        {
            const float rowH = (thumb.getHeight() - 4.0f) / (float) od::kThumbRows;
            for (const auto& d : od::makeThumbnail(*clip))
            {
                const float x = thumb.getX() + 4.0f + d.x * (thumb.getWidth() - 8.0f);
                const float y = thumb.getY() + 2.0f + ((float) d.row + 0.5f) * rowH;
                g.setColour(kRowColours[d.row].withAlpha(0.45f + 0.55f * d.strength));
                g.fillEllipse(x - 2.0f, y - 2.0f, 4.0f, 4.0f);
            }
            if (current && proc_.isGroovePlaying())
            {
                const float px = thumb.getX() + 4.0f + proc_.groovePosition() * (thumb.getWidth() - 8.0f);
                g.setColour(juce::Colours::white);
                g.drawVerticalLine((int) px, thumb.getY() + 1.0f, thumb.getBottom() - 1.0f);
            }
        }

        g.setColour(fg);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(juce::String(e.clip->name), (int) thumb.getRight() + 12, 4, w - 260, 20, juce::Justification::centredLeft, true);
        g.setColour(current ? fg.withAlpha(0.8f) : kTextDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(e.detail, (int) thumb.getRight() + 12, 23, w - 260, 16, juce::Justification::centredLeft, true);

        // star
        const bool fav = proc_.isFavorite(juce::String(e.clip->relPath));
        juce::Path star;
        star.addStar({(float) w - 24.0f, (float) h * 0.5f}, 5, 5.0f, 11.0f);
        g.setColour(kOutline);
        g.strokePath(star, juce::PathStrokeType(3.0f));
        g.setColour(fav ? juce::Colour(0xffffd24d) : (current ? juce::Colour(0xffd9c9a3) : kPanel));
        g.fillPath(star);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) owner_.entries_.size()) return;
        const auto* clip = owner_.entries_[(size_t) row].clip;
        if (e.x > list_.getWidth() - 44)
        {
            proc_.toggleFavorite(juce::String(clip->relPath));
            if (owner_.favoritesTab_) owner_.rebuildClips();
            list_.repaint();
            return;
        }
        proc_.selectGrooveClip(*clip);
        list_.repaint();
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int) owner_.entries_.size()) return;
        proc_.selectGrooveClip(*owner_.entries_[(size_t) row].clip);
        proc_.grooveStart();
        list_.repaint();
    }
public:
    // Drag the selected clip out as a .mid file (works onto the DAW's tracks).
    void startExternalDrag(int row)
    {
        if (row < 0 || row >= (int) owner_.entries_.size()) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this))
            c->performExternalDragDropOfFiles({juce::String(owner_.entries_[(size_t) row].clip->path.string())}, false);
    }
    juce::ListBox& listBox() { return list_; }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragRow_ = list_.getRowContainingPosition(e.getEventRelativeTo(&list_).x, e.getEventRelativeTo(&list_).y);
        dragged_ = false;
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragged_ && dragRow_ >= 0 && e.getDistanceFromDragStart() > 8 && e.getMouseDownX() < list_.getWidth() - 44)
        {
            dragged_ = true;
            startExternalDrag(dragRow_);
        }
    }

private:
    OpenDrumsProcessor& proc_;
    GroovesView& owner_;
    juce::ListBox list_;
    int dragRow_ = -1;
    bool dragged_ = false;
};

GroovesView::GroovesView(OpenDrumsProcessor& p) : proc_(p)
{
    colStyle_ = std::make_unique<Column>(T("ESTILOS"));
    colMeter_ = std::make_unique<Column>(T("COMPASSO / BPM"));
    colSection_ = std::make_unique<Column>(T("SEÇÃO"));
    clips_ = std::make_unique<ClipList>(p, *this);
    for (auto* c : {colStyle_.get(), colMeter_.get(), colSection_.get()}) addAndMakeVisible(*c);
    addAndMakeVisible(*clips_);

    colStyle_->onSelect = [this](int i) { style_ = i; meter_ = section_ = -1; rebuildColumns(); rebuildClips(); };
    colMeter_->onSelect = [this](int i) { meter_ = i; section_ = -1; rebuildColumns(); rebuildClips(); };
    colSection_->onSelect = [this](int i) { section_ = i; rebuildColumns(); rebuildClips(); };

    tabStyles_.setClickingTogglesState(false);
    tabFavs_.setClickingTogglesState(false);
    tabStyles_.setToggleState(true, juce::dontSendNotification);
    tabStyles_.onClick = [this] { favoritesTab_ = false; tabStyles_.setToggleState(true, juce::dontSendNotification); tabFavs_.setToggleState(false, juce::dontSendNotification); rebuildClips(); resized(); };
    tabFavs_.onClick = [this] { favoritesTab_ = true; tabFavs_.setToggleState(true, juce::dontSendNotification); tabStyles_.setToggleState(false, juce::dontSendNotification); rebuildClips(); resized(); };
    addAndMakeVisible(tabStyles_);
    addAndMakeVisible(tabFavs_);

    search_.setTextToShowWhenEmpty(T("Buscar (ex.: funk verso)"), kTextDim);
    search_.onTextChange = [this] { rebuildClips(); };
    addAndMakeVisible(search_);

    folderButton_.onClick = [this] { chooseFolder(); };
    addAndMakeVisible(folderButton_);

    play_.onClick = [this] {
        if (proc_.isGroovePlaying()) proc_.grooveStop();
        else proc_.grooveStart();
    };
    addAndMakeVisible(play_);
    bpm_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    bpm_.setColour(kKnobColourId, kAccent);
    addAndMakeVisible(bpm_);
    bpmAtt_ = std::make_unique<SliderAtt>(p.apvts, "groove_bpm", bpm_);
    addAndMakeVisible(followHost_);
    addAndMakeVisible(followTransport_);
    hostAtt_ = std::make_unique<ButtonAtt>(p.apvts, "groove_host", followHost_);
    xportAtt_ = std::make_unique<ButtonAtt>(p.apvts, "groove_xport", followTransport_);

    refresh();
    startTimerHz(15);
}

GroovesView::~GroovesView() { stopTimer(); }

void GroovesView::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(T("Escolha a pasta de grooves"), proc_.getGroovesFolder());
    juce::Component::SafePointer<GroovesView> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe](const juce::FileChooser& fc) {
                              if (safe != nullptr && fc.getResult().isDirectory())
                                  safe->proc_.setGroovesFolder(fc.getResult());
                          });
}

void GroovesView::refresh()
{
    const auto& styles = proc_.getGrooveLibrary().styles();
    if (style_ >= (int) styles.size()) style_ = meter_ = section_ = -1;
    countText_ = juce::String(proc_.getGrooveLibrary().totalClips()) + T(" clipes · ") +
                 juce::String((int) styles.size()) + T(" estilos");
    rebuildColumns();
    rebuildClips();
    repaint();
}

void GroovesView::selectPath(int style, int meter, int section)
{
    style_ = style; meter_ = meter; section_ = section;
    rebuildColumns();
    rebuildClips();
}

void GroovesView::rebuildColumns()
{
    const auto& styles = proc_.getGrooveLibrary().styles();
    juce::StringArray s, m, sec;
    for (const auto& st : styles) s.add(juce::String(st.name) + "  (" + juce::String(st.clipCount()) + ")");
    colStyle_->setItems(s, style_);
    if (style_ >= 0 && style_ < (int) styles.size())
    {
        const auto& meters = styles[(size_t) style_].meters;
        for (const auto& me : meters) m.add(juce::String::fromUTF8(me.name.c_str()));
        if (meter_ >= (int) meters.size()) meter_ = -1;
        if (meter_ >= 0)
            for (const auto& se : meters[(size_t) meter_].sections) sec.add(juce::String(se.name) + "  (" + juce::String((int) se.clips.size()) + ")");
    }
    colMeter_->setItems(m, meter_);
    colSection_->setItems(sec, section_);
}

void GroovesView::rebuildClips()
{
    entries_.clear();
    const auto& lib = proc_.getGrooveLibrary();
    const auto q = search_.getText().trim();
    auto detailOf = [](const od::GrooveStyle& st, const od::GrooveMeter& me, const od::GrooveSection& se) {
        return juce::String(st.name) + "  ·  " + juce::String::fromUTF8(me.name.c_str()) + "  ·  " + juce::String(se.name);
    };
    if (favoritesTab_)
    {
        for (const auto& rel : proc_.getFavorites())
            for (const auto& st : lib.styles())
                for (const auto& me : st.meters)
                    for (const auto& se : me.sections)
                        for (const auto& cl : se.clips)
                            if (cl.relPath == rel) entries_.push_back({&cl, detailOf(st, me, se)});
    }
    else if (q.isNotEmpty())
    {
        for (const auto& h : lib.search(q.toStdString(), 300)) entries_.push_back({h.clip, detailOf(*h.style, *h.meter, *h.section)});
    }
    else if (style_ >= 0 && meter_ >= 0 && section_ >= 0)
    {
        const auto& st = lib.styles()[(size_t) style_];
        const auto& me = st.meters[(size_t) meter_];
        if (section_ < (int) me.sections.size())
            for (const auto& cl : me.sections[(size_t) section_].clips) entries_.push_back({&cl, detailOf(st, me, me.sections[(size_t) section_])});
    }
    clips_->update();
    clips_->scrollToTop();
}

void GroovesView::timerCallback()
{
    play_.setButtonText(proc_.isGroovePlaying() ? juce::String::fromUTF8("\xE2\x96\xA0  Parar") : juce::String::fromUTF8("\xE2\x96\xB6  Tocar"));
    play_.setToggleState(proc_.isGroovePlaying(), juce::dontSendNotification);
    if (isShowing() && proc_.isGroovePlaying()) clips_->listBox().repaint();
}

void GroovesView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171b));
    // transport bar
    g.setColour(kOutline);
    g.fillRoundedRectangle(transportBox_.toFloat(), 10.0f);
    g.setColour(kPanel);
    g.fillRoundedRectangle(transportBox_.toFloat().reduced(1.5f), 9.0f);
    g.setColour(kTextDim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(countText_, getLocalBounds().removeFromTop(40).removeFromRight(400).withTrimmedRight(190), juce::Justification::centredRight);
    const juce::String cur = proc_.getCurrentGrooveRel();
    g.setColour(kText);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(cur.isEmpty() ? T("Nenhum clipe selecionado. Duplo clique para tocar; arraste o clipe para uma trilha da DAW.")
                             : cur.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".mid", false, false) + "   —   " +
                                   cur.upToLastOccurrenceOf("/", false, false).replace("/", "  ›  "),
               transportBox_.reduced(14, 4).removeFromTop(20), juce::Justification::centredLeft, true);
}

void GroovesView::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto top = r.removeFromTop(30);
    tabStyles_.setBounds(top.removeFromLeft(100).reduced(0, 2));
    top.removeFromLeft(6);
    tabFavs_.setBounds(top.removeFromLeft(100).reduced(0, 2));
    top.removeFromLeft(14);
    folderButton_.setBounds(top.removeFromRight(150).reduced(0, 2));
    top.removeFromRight(8);
    search_.setBounds(top.removeFromRight(240).reduced(0, 2));
    r.removeFromTop(8);

    transportBox_ = r.removeFromBottom(78);
    r.removeFromBottom(8);
    auto t = transportBox_.reduced(14, 8);
    t.removeFromTop(22);
    play_.setBounds(t.removeFromLeft(120).reduced(0, 2));
    t.removeFromLeft(14);
    bpm_.setBounds(t.removeFromLeft(280));
    t.removeFromLeft(14);
    followHost_.setBounds(t.removeFromLeft(190));
    followTransport_.setBounds(t.removeFromLeft(210));

    const int colW = juce::jlimit(170, 230, r.getWidth() / 5);
    colStyle_->setBounds(r.removeFromLeft(colW));
    r.removeFromLeft(6);
    colMeter_->setBounds(r.removeFromLeft(colW));
    r.removeFromLeft(6);
    colSection_->setBounds(r.removeFromLeft(colW));
    r.removeFromLeft(6);
    clips_->setBounds(r);
    const bool fav = favoritesTab_;
    colStyle_->setVisible(!fav);
    colMeter_->setVisible(!fav);
    colSection_->setVisible(!fav);
    if (fav) clips_->setBounds(getLocalBounds().reduced(10, 8).withTrimmedTop(38).withTrimmedBottom(86));
}
} // namespace ui
