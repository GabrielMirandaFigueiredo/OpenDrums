#include "PluginEditor.h"

namespace
{
juce::String T(const char* utf8) { return juce::String::fromUTF8(utf8); }
} // namespace

OpenDrumsEditor::OpenDrumsEditor(OpenDrumsProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), mixView_(p), groovesView_(p), matrixView_(p)
{
    setLookAndFeel(&look_);
    setResizable(true, true);
    setResizeLimits(1100, 700, 2200, 1300);
    setSize(1400, 790);

    const char* names[4] = {"KIT", "MIX", "GROOVES", "MATRIX"};
    for (int i = 0; i < 4; ++i)
    {
        tabs_[(size_t) i].setButtonText(names[i]);
        tabs_[(size_t) i].onClick = [this, i] { showView(i); };
        addAndMakeVisible(tabs_[(size_t) i]);
    }

    masterKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterKnob_.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    masterKnob_.setColour(ui::kKnobColourId, ui::kAccent);
    masterKnob_.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(masterKnob_);
    masterAtt_ = std::make_unique<ui::SliderAtt>(proc_.apvts, "master", masterKnob_);

    // "Ambiente": return level of the reverb bus, right where the room is chosen
    ambKnob_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    ambKnob_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    ambKnob_.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    ambKnob_.setColour(ui::kKnobColourId, ui::kBlue);
    ambKnob_.setPopupDisplayEnabled(true, true, nullptr);
    ambKnob_.setTooltip(T("Volume da ambiência (retorno do reverb da sala escolhida)"));
    addAndMakeVisible(ambKnob_);
    ambAtt_ = std::make_unique<ui::SliderAtt>(proc_.apvts, "rv_ret", ambKnob_);
    ambLabel_.setText(T("AMBIENTE"), juce::dontSendNotification);
    ambLabel_.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    ambLabel_.setColour(juce::Label::textColourId, ui::kTextDim);
    ambLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(ambLabel_);

    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, ui::kTextDim);
    addAndMakeVisible(statusLabel_);

    foldersButton_.setButtonText(T("Pasta de kits…"));
    foldersButton_.onClick = [this] { chooseKitsFolder(); };
    addAndMakeVisible(foldersButton_);

    kitBox_.setTextWhenNoChoicesAvailable(T("Nenhum kit encontrado"));
    kitBox_.onChange = [this] {
        if (refreshing_)
            return;
        const int index = kitBox_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(kitIds_.size()))
            proc_.selectKit(kitIds_[static_cast<size_t>(index)]);
    };
    addAndMakeVisible(kitBox_);

    for (int i = 0; i < 8; ++i)
        roomBox_.addItem(T(od::reverbRoom(i).name), i + 1);
    addAndMakeVisible(roomBox_);
    roomAtt_ = std::make_unique<ui::ComboAtt>(proc_.apvts, "rv_room", roomBox_);

    pieceLabel_.setJustificationType(juce::Justification::centredLeft);
    pieceLabel_.setFont(juce::FontOptions(14.0f));
    addAndMakeVisible(pieceLabel_);

    swapButton_.setButtonText(T("Trocar peça…"));
    swapButton_.onClick = [this] {
        if (selectedSlot_.isNotEmpty())
            showPieceMenu(selectedSlot_);
    };
    addAndMakeVisible(swapButton_);

    resetButton_.setButtonText(T("Restaurar peça"));
    resetButton_.onClick = [this] {
        if (selectedSlot_.isNotEmpty())
            proc_.resetSlot(selectedSlot_);
    };
    addAndMakeVisible(resetButton_);

    kitView_.onSlotSelected = [this](const juce::String& slot) {
        selectedSlot_ = slot;
        updateSelectionInfo();
    };
    kitView_.onSlotHit = [this](const juce::String& slot) {
        const int note = proc_.auditionNoteForSlot(slot);
        if (note >= 0)
            proc_.audition(note, 100);
    };
    kitView_.onSlotMenuRequested = [this](const juce::String& slot) { showPieceMenu(slot); };
    addAndMakeVisible(kitView_);
    addChildComponent(mixView_);
    addChildComponent(groovesView_);
    addChildComponent(matrixView_);

    proc_.addChangeListener(this);
    refresh();

    int initial = proc_.getLastView();
    // Developer hook (screenshots/tests): OPENDRUMS_VIEW = 0..3, OPENDRUMS_MATRIX_PAGE = 0..3
    const auto envView = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_VIEW", {});
    if (envView.isNotEmpty())
        initial = envView.getIntValue();
    const auto envPage = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_MATRIX_PAGE", {});
    if (envPage.isNotEmpty())
        matrixView_.showPage(envPage.getIntValue());
    const auto envGroove = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_GROOVE_PATH", {}); // "style,meter,section"
    if (envGroove.isNotEmpty())
    {
        const auto t = juce::StringArray::fromTokens(envGroove, ",", "");
        if (t.size() == 3)
            groovesView_.selectPath(t[0].getIntValue(), t[1].getIntValue(), t[2].getIntValue());
    }
    const auto envRoom = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_ROOM", {});
    if (envRoom.isNotEmpty())
        if (auto* rp = proc_.apvts.getParameter("rv_room"))
            rp->setValueNotifyingHost(rp->convertTo0to1((float) envRoom.getIntValue()));
    const auto envClip = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_GROOVE_CLIP", {});
    if (envClip.isNotEmpty())
        if (auto* info = proc_.getGrooveLibrary().findByRelPath(envClip.toStdString()))
        {
            proc_.selectGrooveClip(*info);
            proc_.grooveStart();
        }
    showView(juce::jlimit(0, 3, initial));

    int note = 0, velocity = 0;
    while (proc_.popHit(note, velocity)) {}
    startTimerHz(60);
}

OpenDrumsEditor::~OpenDrumsEditor()
{
    stopTimer();
    proc_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void OpenDrumsEditor::showView(int view)
{
    view_ = view;
    proc_.setLastView(view);
    for (int i = 0; i < 4; ++i)
        tabs_[(size_t) i].setToggleState(i == view, juce::dontSendNotification);
    const bool kit = view == 0;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&kitBox_, &roomBox_, &ambKnob_, &ambLabel_, &foldersButton_, &swapButton_, &resetButton_, &pieceLabel_, &kitView_})
        c->setVisible(kit);
    mixView_.setVisible(view == 1);
    groovesView_.setVisible(view == 2);
    matrixView_.setVisible(view == 3);
    resized();
    repaint();
}

void OpenDrumsEditor::timerCallback()
{
    const int room = juce::jlimit(0, 7, (int) std::lround(proc_.apvts.getRawParameterValue("rv_room")->load()));
    if (room != lastRoom_)
    {
        lastRoom_ = room;
        kitView_.setRoom(room);
    }

    const auto rig = proc_.getRig();
    int note = 0, velocity = 0;
    while (proc_.popHit(note, velocity)) {
        if (rig == nullptr || note < 0 || note > 127)
            continue;
        const auto& target = rig->notes[static_cast<size_t>(note)];
        if (target.slot == nullptr || target.art == nullptr)
            continue;
        kitView_.triggerHit(juce::String(target.slot->view.slot), juce::String(target.art->role), velocity);
    }
}

void OpenDrumsEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::kBg);
    // top bar
    const auto bar = juce::Rectangle<float>(0.0f, 0.0f, (float) getWidth(), 52.0f);
    g.setColour(ui::kPanel);
    g.fillRect(bar);
    g.setColour(ui::kOutline);
    g.fillRect(0.0f, 51.0f, (float) getWidth(), 2.0f);

    // logo: a tiny cartoon snare + wordmark
    g.setColour(ui::kOutline);
    g.fillEllipse(14.0f, 13.0f, 30.0f, 28.0f);
    g.setColour(juce::Colour(0xff7a1f1f));
    g.fillEllipse(15.5f, 14.5f, 27.0f, 25.0f);
    g.setColour(juce::Colour(0xfff0e6d6));
    g.fillEllipse(15.5f, 13.0f, 27.0f, 14.0f);
    g.setColour(ui::kAccent);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText("OpenDrums", 52, 8, 170, 36, juce::Justification::centredLeft);
    g.setColour(ui::kTextDim);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("MASTER", getWidth() - 100, 16, 52, 20, juce::Justification::centredRight);
}

void OpenDrumsEditor::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(52);
    top.removeFromLeft(230);
    for (auto& t : tabs_) { t.setBounds(top.removeFromLeft(118).reduced(2, 8)); }
    masterKnob_.setBounds(top.removeFromRight(40).reduced(2, 6));
    top.removeFromRight(4);
    statusLabel_.setBounds(top.removeFromRight(juce::jmin(400, top.getWidth())));
    area.removeFromTop(2);

    const auto page = area;
    mixView_.setBounds(page);
    groovesView_.setBounds(page);
    matrixView_.setBounds(page);

    // kit page
    auto kit = page.reduced(12, 8);
    auto bar = kit.removeFromTop(34);
    kitBox_.setBounds(bar.removeFromLeft(280).reduced(0, 3));
    bar.removeFromLeft(8);
    foldersButton_.setBounds(bar.removeFromLeft(130).reduced(0, 3));
    roomBox_.setBounds(bar.removeFromRight(200).reduced(0, 3));
    bar.removeFromRight(8);
    ambKnob_.setBounds(bar.removeFromRight(34).reduced(1, 0));
    ambLabel_.setBounds(bar.removeFromRight(70));
    bar.removeFromRight(8);
    bar.removeFromRight(0);
    auto bottom = kit.removeFromBottom(44);
    resetButton_.setBounds(bottom.removeFromRight(130).reduced(0, 7));
    bottom.removeFromRight(8);
    swapButton_.setBounds(bottom.removeFromRight(130).reduced(0, 7));
    pieceLabel_.setBounds(bottom);
    kit.removeFromTop(6);
    kit.removeFromBottom(6);
    kitView_.setBounds(kit);
}

void OpenDrumsEditor::refresh()
{
    refreshing_ = true;

    kitBox_.clear(juce::dontSendNotification);
    kitIds_.clear();
    int selectedId = 0;
    for (const auto& kit : proc_.getLibrary().kits()) {
        kitIds_.push_back(juce::String(kit->id));
        kitBox_.addItem(juce::String(kit->name), static_cast<int>(kitIds_.size()));
        if (juce::String(kit->id) == proc_.getCurrentKitId())
            selectedId = static_cast<int>(kitIds_.size());
    }
    if (selectedId > 0)
        kitBox_.setSelectedId(selectedId, juce::dontSendNotification);

    auto rig = proc_.getRig();
    juce::StringArray swapped;
    if (rig != nullptr)
        for (const auto& slot : rig->slots)
            if (proc_.isSlotSwapped(juce::String(slot.view.slot)))
                swapped.add(juce::String(slot.view.slot));
    kitView_.setSwappedSlots(swapped);
    kitView_.setRig(rig);
    mixView_.setRig(rig);
    matrixView_.setRig(rig);
    groovesView_.refresh();

    if (proc_.getLibrary().kits().empty())
        statusLabel_.setText(T("Nenhum kit em ") + proc_.getKitsFolder().getFullPathName(), juce::dontSendNotification);
    else if (proc_.isLoading())
        statusLabel_.setText(T("Carregando samples…"), juce::dontSendNotification);
    else if (rig != nullptr && !rig->warnings.empty())
        statusLabel_.setText(juce::String(static_cast<int>(rig->warnings.size())) + T(" aviso(s) ao carregar o kit"),
                             juce::dontSendNotification);
    else
        statusLabel_.setText({}, juce::dontSendNotification);

    refreshing_ = false;
    updateSelectionInfo();
}

void OpenDrumsEditor::updateSelectionInfo()
{
    const od::SlotView* view = nullptr;
    auto rig = proc_.getRig();
    if (rig != nullptr)
        for (const auto& slot : rig->slots)
            if (juce::String(slot.view.slot) == selectedSlot_)
                view = &slot.view;

    const bool hasSlot = view != nullptr && !view->pieceRef.empty();
    swapButton_.setEnabled(hasSlot);
    resetButton_.setEnabled(hasSlot && proc_.isSlotSwapped(selectedSlot_));
    kitView_.setSelectedSlot(hasSlot ? selectedSlot_ : juce::String());

    if (!hasSlot) {
        pieceLabel_.setText(T("Clique numa peça para tocar. Use o ícone ⇄ no canto de cada peça (ou o botão “Trocar peça…”) para trocá-la por outra de qualquer kit."),
                            juce::dontSendNotification);
        return;
    }
    const juce::String kitOfPiece = juce::String(view->pieceRef).upToFirstOccurrenceOf("/", false, false);
    const od::KitDef* kit = proc_.getLibrary().findKit(kitOfPiece.toStdString());
    pieceLabel_.setText(KitView::displayNameForSlot(selectedSlot_) + ": " + juce::String(view->label) +
                            (kit != nullptr ? "   [" + juce::String(kit->name) + "]" : juce::String()),
                        juce::dontSendNotification);
}

void OpenDrumsEditor::showPieceMenu(const juce::String& slot)
{
    auto rig = proc_.getRig();
    if (rig == nullptr)
        return;
    const od::SlotView* view = nullptr;
    for (const auto& s : rig->slots)
        if (juce::String(s.view.slot) == slot)
            view = &s.view;
    if (view == nullptr)
        return;

    // Pieces of the same family from every kit, grouped by kit in a sub menu.
    const auto hits = proc_.getLibrary().piecesOfFamily(view->family);
    std::vector<std::string> refs;
    juce::PopupMenu menu;
    menu.addSectionHeader(T("Trocar: ") + KitView::displayNameForSlot(slot));

    for (const auto& kit : proc_.getLibrary().kits()) {
        juce::PopupMenu sub;
        bool containsCurrent = false;
        bool any = false;
        for (const auto& hit : hits) {
            if (hit.kit != kit.get())
                continue;
            refs.push_back(hit.ref());
            const bool current = hit.ref() == view->pieceRef;
            containsCurrent = containsCurrent || current;
            sub.addItem(static_cast<int>(refs.size()), juce::String(hit.piece->label), true, current);
            any = true;
        }
        if (any)
            menu.addSubMenu(juce::String(kit->name), sub, true, nullptr, containsCurrent);
    }
    if (refs.empty())
        return;

    juce::Component::SafePointer<OpenDrumsEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&kitView_),
                       [safe, slot, refs](int result) {
                           if (safe == nullptr || result <= 0 || result > static_cast<int>(refs.size()))
                               return;
                           safe->proc_.setSlotPiece(slot, juce::String(refs[static_cast<size_t>(result - 1)]));
                       });
}

void OpenDrumsEditor::chooseKitsFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(T("Escolha a pasta que contém os kits"), proc_.getKitsFolder());
    juce::Component::SafePointer<OpenDrumsEditor> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe](const juce::FileChooser& fc) {
                              const auto result = fc.getResult();
                              if (safe != nullptr && result.isDirectory())
                                  safe->proc_.setKitsFolder(result);
                          });
}
