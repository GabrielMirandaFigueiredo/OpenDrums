#include "MatrixView.h"

#include "KitView.h"

namespace ui
{
namespace
{
// Shared: draws a rounded LED cell
void drawLed(juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour c)
{
    const auto led = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()) * 0.62f, juce::jmin(r.getWidth(), r.getHeight()) * 0.62f);
    g.setColour(kOutline);
    g.fillEllipse(led.expanded(2.0f));
    g.setColour(on ? c : kPanelHi.darker(0.2f));
    g.fillEllipse(led);
    if (on)
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.fillEllipse(led.reduced(led.getWidth() * 0.3f).translated(-1.5f, -1.5f));
    }
}
} // namespace

// ------------------------------------------------------------ audio outputs grid

class MatrixView::AudioGrid : public juce::Component
{
public:
    explicit AudioGrid(OpenDrumsProcessor& p) : proc_(p) {}
    void paint(juce::Graphics& g) override
    {
        layout();
        // group tags
        drawTag(g, juce::Rectangle<float>((float) col0_, 0.0f, (float) cell_, 18.0f), "MASTER", kText, 9.5f);
        drawTag(g, juce::Rectangle<float>((float) (col0_ + cell_ * 1 + 4), 0.0f, (float) (cell_ * 8 - 8), 18.0f), T("SAÍDAS ESTÉREO"), kBlue, 10.0f);
        drawTag(g, juce::Rectangle<float>((float) (col0_ + cell_ * 9 + 4), 0.0f, (float) (cell_ * 16 - 8), 18.0f), T("SAÍDAS MONO"), kAccent, 10.0f);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        for (int b = 0; b < od::kNumRoutes; ++b)
        {
            g.setColour(kTextDim);
            juce::String name = b == 0 ? "" : b < 9 ? juce::String((b - 1) * 2 + 1) + "/" + juce::String((b - 1) * 2 + 2) : juce::String(b - 8);
            g.drawText(name, col0_ + b * cell_, 20, cell_, 16, juce::Justification::centred);
        }
        for (int s = 0; s < od::kNumStrips; ++s)
        {
            const int y = rowY(s);
            g.setColour(s % 2 ? kPanel : kPanel.brighter(0.03f));
            g.fillRoundedRectangle(0.0f, (float) y, (float) getWidth(), (float) row_ - 2.0f, 6.0f);
            drawTag(g, juce::Rectangle<float>(6.0f, (float) y + 4.0f, 110.0f, (float) row_ - 10.0f), juce::String::fromUTF8(stripLabel(s)), groupColour(s), 11.0f);
            const auto mask = proc_.getRouteMask(s);
            for (int b = 0; b < od::kNumRoutes; ++b)
                drawLed(g, juce::Rectangle<float>((float) (col0_ + b * cell_), (float) y, (float) cell_, (float) row_ - 2.0f), (mask >> b) & 1,
                        b == 0 ? kText : b < 9 ? kBlue : kAccent);
        }
        // reverb / parallel buses always follow the master, shown as a reminder
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(T("Os buses de Reverb e Compressão Paralela entram no Master. Para usar as saídas 3/4…15/16, ative-as no seu host (cada saída aparece como uma entrada extra do plugin)."),
                         juce::Rectangle<int>(0, rowY(od::kNumStrips) + 4, getWidth(), 34), juce::Justification::centredLeft, 2);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        layout();
        const int s = (e.y - 40) / row_;
        const int b = (e.x - col0_) / cell_;
        if (s < 0 || s >= od::kNumStrips || b < 0 || b >= od::kNumRoutes || e.x < col0_) return;
        proc_.setRouteMask(s, proc_.getRouteMask(s) ^ (1u << b));
        repaint();
    }
    void resetAll() { for (int s = 0; s < od::kNumStrips; ++s) proc_.setRouteMask(s, od::kDefaultRoute); repaint(); }

private:
    void layout() { col0_ = 124; cell_ = juce::jmax(22, (getWidth() - col0_ - 4) / od::kNumRoutes); row_ = juce::jlimit(26, 36, (getHeight() - 100) / od::kNumStrips); }
    int rowY(int s) const { return 40 + s * row_; }
    OpenDrumsProcessor& proc_;
    int col0_ = 124, cell_ = 40, row_ = 30;
};

// ------------------------------------------------------------ MIDI channel grid

class MatrixView::ChannelGrid : public juce::Component
{
public:
    explicit ChannelGrid(OpenDrumsProcessor& p) : proc_(p) {}
    void paint(juce::Graphics& g) override
    {
        layout();
        drawTag(g, juce::Rectangle<float>((float) col0_, 0.0f, (float) (cell_ * 16), 18.0f), T("CANAL MIDI DE ENTRADA"), kBlue, 10.0f);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        for (int c = 0; c < 16; ++c)
        {
            g.setColour(c == 9 ? kAccent : kTextDim);
            g.drawText(juce::String(c + 1), col0_ + c * cell_, 20, cell_, 16, juce::Justification::centred);
        }
        for (int s = 0; s < od::kNumStrips; ++s)
        {
            const int y = 40 + s * row_;
            g.setColour(s % 2 ? kPanel : kPanel.brighter(0.03f));
            g.fillRoundedRectangle(0.0f, (float) y, (float) getWidth(), (float) row_ - 2.0f, 6.0f);
            drawTag(g, juce::Rectangle<float>(6.0f, (float) y + 4.0f, 110.0f, (float) row_ - 10.0f), juce::String::fromUTF8(stripLabel(s)), groupColour(s), 11.0f);
            const auto mask = proc_.getChannelMask(s);
            for (int c = 0; c < 16; ++c)
                drawLed(g, juce::Rectangle<float>((float) (col0_ + c * cell_), (float) y, (float) cell_, (float) row_ - 2.0f), (mask >> c) & 1, c == 9 ? kAccent : kGreen);
        }
        g.setColour(kTextDim);
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(T("Cada peça só toca com notas que chegam nos canais marcados. Padrão: todos os canais. Use isto para tocar várias peças de canais diferentes (por exemplo, uma trilha MIDI por peça). Os grooves internos e cliques no kit ignoram o filtro."),
                         juce::Rectangle<int>(0, 40 + od::kNumStrips * row_ + 4, getWidth(), 34), juce::Justification::centredLeft, 2);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        layout();
        const int s = (e.y - 40) / row_;
        const int c = (e.x - col0_) / cell_;
        if (s < 0 || s >= od::kNumStrips || c < 0 || c >= 16 || e.x < col0_) return;
        proc_.setChannelMask(s, (uint16_t) (proc_.getChannelMask(s) ^ (1u << c)));
        repaint();
    }
    void setAll(bool ch10only)
    {
        for (int s = 0; s < od::kNumStrips; ++s) proc_.setChannelMask(s, ch10only ? (uint16_t) (1u << 9) : (uint16_t) 0xFFFF);
        repaint();
    }

private:
    void layout() { col0_ = 124; cell_ = juce::jmax(24, (getWidth() - col0_ - 4) / 16); row_ = juce::jlimit(26, 36, (getHeight() - 100) / od::kNumStrips); }
    OpenDrumsProcessor& proc_;
    int col0_ = 124, cell_ = 40, row_ = 30;
};

// ------------------------------------------------------------ CC page

class MatrixView::CcPage : public juce::Component
{
public:
    explicit CcPage(OpenDrumsProcessor& p) : proc_(p)
    {
        add_.setButtonText(T("+  Adicionar mapeamento"));
        add_.onClick = [this] {
            for (int cc = 20; cc < 128; ++cc)
                if (proc_.getCcTarget(cc) < 0) { proc_.setCcTarget(cc, 0); break; }
            rebuild();
        };
        addAndMakeVisible(add_);
        viewport_.setViewedComponent(&rows_, false);
        addAndMakeVisible(viewport_);
        rebuild();
    }
    void rebuild()
    {
        rows_.clear();
        for (int cc = 0; cc < 128; ++cc)
            if (proc_.getCcTarget(cc) >= 0) rows_.add(proc_, cc, [this] { rebuild(); });
        resized();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        add_.setBounds(r.removeFromTop(30).removeFromLeft(220));
        r.removeFromTop(8);
        viewport_.setBounds(r);
        rows_.setSize(juce::jmax(300, r.getWidth() - 14), juce::jmax(r.getHeight(), rows_.rowCount() * 36 + 8));
        rows_.resized();
    }
    void paint(juce::Graphics& g) override
    {
        if (rows_.rowCount() == 0)
        {
            g.setColour(kTextDim);
            g.setFont(juce::FontOptions(13.0f));
            g.drawFittedText(T("Nenhum CC mapeado. Use \"Adicionar\" e escolha qual controle de MIDI (CC) move qual fader, pan, mute ou send."),
                             getLocalBounds().withTrimmedTop(60).reduced(40), juce::Justification::centredTop, 3);
        }
    }

private:
    class Rows : public juce::Component
    {
    public:
        struct Row : juce::Component
        {
            Row(OpenDrumsProcessor& p, int cc, std::function<void()> changed) : proc_(p), cc_(cc), changed_(std::move(changed))
            {
                for (int i = 0; i < 128; ++i)
                {
                    juce::String n = "CC " + juce::String(i);
                    if (i == 1) n += " (Mod)"; else if (i == 7) n += " (Volume)"; else if (i == 10) n += " (Pan)";
                    else if (i == 11) n += T(" (Expressão)"); else if (i == 64) n += " (Sustain)";
                    ccBox_.addItem(juce::String::fromUTF8(n.toRawUTF8()), i + 1);
                }
                ccBox_.setSelectedId(cc + 1, juce::dontSendNotification);
                for (int t = 0; t < p.numCcTargets(); ++t) targetBox_.addItem(p.ccTargetName(t), t + 1);
                targetBox_.setSelectedId(p.getCcTarget(cc) + 1, juce::dontSendNotification);
                ccBox_.onChange = [this] {
                    const int newCc = ccBox_.getSelectedId() - 1;
                    if (newCc == cc_) return;
                    const int t = proc_.getCcTarget(cc_);
                    proc_.setCcTarget(cc_, -1);
                    proc_.setCcTarget(newCc, t);
                    cc_ = newCc;
                    changed_();
                };
                targetBox_.onChange = [this] { proc_.setCcTarget(cc_, targetBox_.getSelectedId() - 1); };
                remove_.setButtonText("X");
                remove_.onClick = [this] { proc_.setCcTarget(cc_, -1); changed_(); };
                addAndMakeVisible(ccBox_);
                addAndMakeVisible(targetBox_);
                addAndMakeVisible(remove_);
            }
            void paint(juce::Graphics& g) override
            {
                g.setColour(kPanel);
                g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f, 2.0f), 7.0f);
                g.setColour(kAccent);
                g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
                g.drawText(juce::String::fromUTF8("\xE2\x86\x92"), 200, 0, 30, getHeight(), juce::Justification::centred);
            }
            void resized() override
            {
                auto r = getLocalBounds().reduced(8, 5);
                ccBox_.setBounds(r.removeFromLeft(190));
                r.removeFromLeft(40);
                remove_.setBounds(r.removeFromRight(30));
                r.removeFromRight(8);
                targetBox_.setBounds(r.removeFromLeft(juce::jmin(r.getWidth(), 320)));
            }
            OpenDrumsProcessor& proc_;
            int cc_;
            std::function<void()> changed_;
            juce::ComboBox ccBox_, targetBox_;
            juce::TextButton remove_;
        };
        void clear() { rows.clear(); }
        void add(OpenDrumsProcessor& p, int cc, std::function<void()> ch)
        {
            rows.push_back(std::make_unique<Row>(p, cc, std::move(ch)));
            addAndMakeVisible(*rows.back());
        }
        int rowCount() const { return (int) rows.size(); }
        void resized() override { for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, 4 + (int) i * 36, getWidth(), 36); }
        std::vector<std::unique_ptr<Row>> rows;
    };
    OpenDrumsProcessor& proc_;
    juce::TextButton add_;
    juce::Viewport viewport_;
    Rows rows_;
};

// ------------------------------------------------------------ notes page

class MatrixView::NotesPage : public juce::Component
{
public:
    explicit NotesPage(OpenDrumsProcessor& p) : proc_(p)
    {
        viewport_.setViewedComponent(&list_, false);
        addAndMakeVisible(viewport_);
        reset_.setButtonText(T("Restaurar mapa de notas do kit"));
        reset_.onClick = [this] { proc_.clearNoteOverrides(); };
        addAndMakeVisible(reset_);
    }
    void setRig(std::shared_ptr<const od::Rig> rig)
    {
        list_.rows.clear();
        if (rig)
        {
            std::map<std::string, std::map<std::string, std::vector<int>>> map;
            std::vector<std::string> order;
            for (int n = 0; n < 128; ++n)
            {
                const auto& t = rig->notes[(size_t) n];
                if (!t.slot || !t.art) continue;
                if (!map.count(t.slot->view.slot)) order.push_back(t.slot->view.slot);
                map[t.slot->view.slot][t.art->role].push_back(n);
            }
            for (const auto& slot : order)
                for (const auto& [role, notes] : map[slot])
                {
                    const bool over = proc_.getNoteOverrides().count(slot) && proc_.getNoteOverrides().at(slot).count(role);
                    list_.add(proc_, slot, role, notes, over);
                }
        }
        resized();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        reset_.setBounds(r.removeFromTop(30).removeFromLeft(260));
        r.removeFromTop(8);
        viewport_.setBounds(r);
        list_.setSize(juce::jmax(300, r.getWidth() - 14), juce::jmax(r.getHeight(), (int) list_.rows.size() * 32 + 8));
        list_.resized(); // rows must be laid out even when the size did not change
    }

private:
    struct Row : juce::Component
    {
        Row(OpenDrumsProcessor& p, std::string slot, std::string role, const std::vector<int>& notes, bool over)
            : proc_(p), slot_(std::move(slot)), role_(std::move(role)), over_(over)
        {
            juce::StringArray s;
            for (int n : notes) s.add(juce::String(n));
            edit_.setText(s.joinIntoString(", "), false);
            edit_.setJustification(juce::Justification::centredLeft);
            edit_.onReturnKey = [this] { commit(); };
            edit_.onFocusLost = [this] { commit(); };
            addAndMakeVisible(edit_);
        }
        void commit()
        {
            std::vector<int> notes;
            for (const auto& t : juce::StringArray::fromTokens(edit_.getText(), ", ;", ""))
                if (t.containsOnly("0123456789")) notes.push_back(juce::jlimit(0, 127, t.getIntValue()));
            if (notes.empty()) return;
            proc_.setNoteOverride(juce::String(slot_), juce::String(role_), notes);
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(kPanel);
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f, 2.0f), 7.0f);
            g.setColour(over_ ? kAccent : kText);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(KitView::displayNameForSlot(juce::String(slot_)), 12, 0, 120, getHeight(), juce::Justification::centredLeft);
            g.setColour(kTextDim);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String(role_), 138, 0, 130, getHeight(), juce::Justification::centredLeft);
        }
        void resized() override { edit_.setBounds(280, 4, juce::jmin(260, getWidth() - 290), getHeight() - 8); }
        OpenDrumsProcessor& proc_;
        std::string slot_, role_;
        bool over_;
        juce::TextEditor edit_;
    };
    struct List : juce::Component
    {
        void add(OpenDrumsProcessor& p, const std::string& s, const std::string& r, const std::vector<int>& n, bool o)
        {
            rows.push_back(std::make_unique<Row>(p, s, r, n, o));
            addAndMakeVisible(*rows.back());
        }
        void resized() override { for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, 4 + (int) i * 32, getWidth(), 32); }
        std::vector<std::unique_ptr<Row>> rows;
    };
    OpenDrumsProcessor& proc_;
    juce::TextButton reset_;
    juce::Viewport viewport_;
    List list_;
};

// ------------------------------------------------------------ MatrixView

MatrixView::MatrixView(OpenDrumsProcessor& p) : proc_(p)
{
    audio_ = std::make_unique<AudioGrid>(p);
    channels_ = std::make_unique<ChannelGrid>(p);
    cc_ = std::make_unique<CcPage>(p);
    notes_ = std::make_unique<NotesPage>(p);
    for (auto* c : std::initializer_list<juce::Component*>{audio_.get(), channels_.get(), cc_.get(), notes_.get()}) addChildComponent(*c);
    const char* names[4] = {"Saídas de áudio", "Canais MIDI", "MIDI CC", "Notas"};
    for (int i = 0; i < 4; ++i)
    {
        tabs_[(size_t) i].setButtonText(juce::String::fromUTF8(names[i]));
        tabs_[(size_t) i].onClick = [this, i] { showPage(i); };
        addAndMakeVisible(tabs_[(size_t) i]);
    }
    resetButton_.setButtonText(T("Restaurar padrão"));
    resetButton_.onClick = [this] {
        if (page_ == 0) audio_->resetAll();
        else if (page_ == 1) channels_->setAll(false);
    };
    addAndMakeVisible(resetButton_);
    showPage(0);
    startTimerHz(4);
}

MatrixView::~MatrixView() { stopTimer(); }

void MatrixView::setRig(std::shared_ptr<const od::Rig> rig) { notes_->setRig(std::move(rig)); }

void MatrixView::timerCallback()
{
    if (isShowing() && page_ <= 1) (page_ == 0 ? static_cast<juce::Component*>(audio_.get()) : channels_.get())->repaint(); // picks up Mix-view changes
}

void MatrixView::showPage(int page)
{
    page_ = page;
    audio_->setVisible(page == 0);
    channels_->setVisible(page == 1);
    cc_->setVisible(page == 2);
    notes_->setVisible(page == 3);
    for (int i = 0; i < 4; ++i) tabs_[(size_t) i].setToggleState(i == page, juce::dontSendNotification);
    resetButton_.setVisible(page <= 1);
    const char* h[4] = {"Para onde cada peça é enviada: marque uma ou mais saídas do plugin.",
                        "De quais canais MIDI cada peça aceita notas.",
                        "Qual controle MIDI (CC) mexe em qual parâmetro do mixer.",
                        "Quais notas MIDI disparam cada articulação de cada peça."};
    help_ = juce::String::fromUTF8(h[page]);
    if (page == 2) cc_->rebuild();
    repaint();
}

void MatrixView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171b));
    g.setColour(kTextDim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(help_, getLocalBounds().reduced(10, 8).removeFromTop(30).withTrimmedLeft(480).withTrimmedRight(160), juce::Justification::centredLeft, true);
}

void MatrixView::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto top = r.removeFromTop(30);
    for (auto& t : tabs_) { t.setBounds(top.removeFromLeft(116).reduced(0, 2)); top.removeFromLeft(4); }
    resetButton_.setBounds(top.removeFromRight(140).reduced(0, 2));
    r.removeFromTop(10);
    for (auto* c : std::initializer_list<juce::Component*>{audio_.get(), channels_.get(), cc_.get(), notes_.get()}) c->setBounds(r);
}
} // namespace ui
