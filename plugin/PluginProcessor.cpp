#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Smf.hpp"

#include <algorithm>
#include <cmath>

namespace
{
juce::NormalisableRange<float> rangeWithCentre(float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r(lo, hi, step);
    r.setSkewForCentre(centre);
    return r;
}
} // namespace

juce::AudioProcessor::BusesProperties OpenDrumsProcessor::makeBuses()
{
    // Stereo output 1/2 is on by default; 3/4 ... 15/16 are extra outputs the host may enable
    // (each drum piece can be routed to any of them in the Mix and Matrix views).
    auto b = BusesProperties().withOutput("Saída 1/2", juce::AudioChannelSet::stereo(), true);
    for (int i = 1; i <= kExtraBuses; ++i)
        b = b.withOutput("Saída " + juce::String(i * 2 + 1) + "/" + juce::String(i * 2 + 2), juce::AudioChannelSet::stereo(), false);
    return b;
}

OpenDrumsProcessor::OpenDrumsProcessor()
    : AudioProcessor(makeBuses()),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    kitsFolder_ = defaultKitsFolder();
    groovesFolder_ = defaultGroovesFolder();
    for (auto& m : routeMask_) m.store(od::kDefaultRoute);
    chMask_.fill(0xFFFF);
    for (auto& t : ccTarget_) t.store(-1);
    cacheParameters();

    // default MIDI CC map: CC7 -> kick fader like many controllers do for "volume" is not assumed; start empty.
    rescanGrooves();
    rescanKits();
    startTimerHz(30);
}

OpenDrumsProcessor::~OpenDrumsProcessor()
{
    stopTimer();
    alive_->store(false);
    loader_.removeAllJobs(true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout OpenDrumsProcessor::createLayout()
{
    using P = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&](const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                    const juce::String& unit = {}) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(P{id, 1}, name, r, def,
                                                               juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    auto addB = [&](const juce::String& id, const juce::String& name, bool def) {
        layout.add(std::make_unique<juce::AudioParameterBool>(P{id, 1}, name, def));
    };

    for (int i = 0; i < od::kNumStrips; ++i)
    {
        const juce::String n = stripLabel(i);
        auto s = [&](const char* k) { return pid::strip(i, k); };
        addF(s("fader"), n + " Fader", {-60.0f, 6.0f, 0.1f, 2.0f}, 0.0f, "dB");
        addF(s("pan"), n + " Pan", {-1.0f, 1.0f, 0.01f}, 0.0f);
        addB(s("mute"), n + " Mute", false);
        addB(s("solo"), n + " Solo", false);
        addB(s("phase"), n + " Fase", false);
        addF(s("sendrv"), n + " Send Reverb", {0.0f, 1.0f, 0.01f}, 0.0f);
        addF(s("sendpar"), n + " Send Paralelo", {0.0f, 1.0f, 0.01f}, 0.0f);
        addB(s("c_on"), n + " Comp On", false);
        addF(s("c_thr"), n + " Comp Threshold", {-40.0f, 0.0f, 0.1f}, -18.0f, "dB");
        addF(s("c_ratio"), n + " Comp Ratio", rangeWithCentre(1.0f, 20.0f, 4.0f, 0.1f), 4.0f);
        addF(s("c_makeup"), n + " Comp Makeup", {0.0f, 24.0f, 0.1f}, 0.0f, "dB");
        addF(s("c_rel"), n + " Comp Release", rangeWithCentre(20.0f, 1200.0f, 200.0f, 1.0f), 150.0f, "ms");
        addB(s("c_fast"), n + " Comp Ataque Rápido", true);
        addB(s("e_on"), n + " EQ On", false);
        addF(s("hf_f"), n + " HF Freq", rangeWithCentre(1500.0f, 16000.0f, 6000.0f, 10.0f), 8000.0f, "Hz");
        addF(s("hf_g"), n + " HF Ganho", {-15.0f, 15.0f, 0.1f}, 0.0f, "dB");
        addB(s("hf_bell"), n + " HF Bell", false);
        addF(s("hmf_f"), n + " HMF Freq", rangeWithCentre(600.0f, 7000.0f, 2500.0f, 10.0f), 3000.0f, "Hz");
        addF(s("hmf_g"), n + " HMF Ganho", {-15.0f, 15.0f, 0.1f}, 0.0f, "dB");
        addF(s("hmf_q"), n + " HMF Q", rangeWithCentre(0.3f, 4.0f, 1.0f, 0.01f), 1.0f);
        addF(s("lmf_f"), n + " LMF Freq", rangeWithCentre(200.0f, 2500.0f, 800.0f, 10.0f), 800.0f, "Hz");
        addF(s("lmf_g"), n + " LMF Ganho", {-15.0f, 15.0f, 0.1f}, 0.0f, "dB");
        addF(s("lmf_q"), n + " LMF Q", rangeWithCentre(0.3f, 4.0f, 1.0f, 0.01f), 1.0f);
        addF(s("lf_f"), n + " LF Freq", rangeWithCentre(30.0f, 450.0f, 110.0f, 1.0f), 100.0f, "Hz");
        addF(s("lf_g"), n + " LF Ganho", {-15.0f, 15.0f, 0.1f}, 0.0f, "dB");
        addB(s("lf_bell"), n + " LF Bell", false);
    }

    // parallel compression bus
    addF("par_thr", "Paralelo Threshold", {-50.0f, 0.0f, 0.1f}, -30.0f, "dB");
    addF("par_ratio", "Paralelo Ratio", rangeWithCentre(1.0f, 20.0f, 6.0f, 0.1f), 8.0f);
    addF("par_makeup", "Paralelo Makeup", {0.0f, 24.0f, 0.1f}, 6.0f, "dB");
    addF("par_rel", "Paralelo Release", rangeWithCentre(20.0f, 1200.0f, 200.0f, 1.0f), 120.0f, "ms");
    addB("par_fast", "Paralelo Ataque Rápido", true);
    addF("par_ret", "Paralelo Retorno", {-60.0f, 6.0f, 0.1f, 2.0f}, -8.0f, "dB");

    // reverb bus
    juce::StringArray rooms;
    for (int i = 0; i < 8; ++i) rooms.add(od::reverbRoom(i).name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(P{"rv_room", 1}, "Sala", rooms, 2));
    addF("rv_decay", "Reverb Decay", rangeWithCentre(0.5f, 2.0f, 1.0f, 0.01f), 1.0f, "x");
    addF("rv_pre", "Reverb Pré-delay", {0.0f, 150.0f, 0.5f}, 0.0f, "ms");
    addF("rv_tone", "Reverb Tom", {0.0f, 1.0f, 0.01f}, 0.5f);
    addF("rv_low", "Reverb Corte Grave", rangeWithCentre(20.0f, 600.0f, 100.0f, 1.0f), 80.0f, "Hz");
    addF("rv_high", "Reverb Corte Agudo", rangeWithCentre(2000.0f, 20000.0f, 8000.0f, 10.0f), 12000.0f, "Hz");
    addF("rv_width", "Reverb Largura", {0.0f, 1.0f, 0.01f}, 1.0f);
    addF("rv_ret", "Reverb Retorno", {-60.0f, 6.0f, 0.1f, 2.0f}, -8.0f, "dB");

    addF("master", "Master", {-60.0f, 6.0f, 0.1f, 2.0f}, -6.0f, "dB");

    // groove player
    addF("groove_bpm", "Groove BPM", {40.0f, 240.0f, 0.1f}, 100.0f, "BPM");
    addB("groove_host", "Groove segue tempo do host", true);
    addB("groove_xport", "Groove segue transporte do host", false);
    return layout;
}

void OpenDrumsProcessor::cacheParameters()
{
    auto raw = [&](const juce::String& id) {
        auto* p = apvts.getRawParameterValue(id);
        jassert(p != nullptr);
        return p;
    };
    for (int i = 0; i < od::kNumStrips; ++i)
    {
        auto s = [&](const char* k) { return raw(pid::strip(i, k)); };
        auto& p = sp_[(size_t) i];
        p.fader = s("fader"); p.pan = s("pan"); p.mute = s("mute"); p.solo = s("solo"); p.phase = s("phase");
        p.sendRv = s("sendrv"); p.sendPar = s("sendpar");
        p.cOn = s("c_on"); p.cThr = s("c_thr"); p.cRatio = s("c_ratio"); p.cMakeup = s("c_makeup");
        p.cRel = s("c_rel"); p.cFast = s("c_fast");
        p.eOn = s("e_on"); p.hfF = s("hf_f"); p.hfG = s("hf_g"); p.hfBell = s("hf_bell");
        p.hmfF = s("hmf_f"); p.hmfG = s("hmf_g"); p.hmfQ = s("hmf_q");
        p.lmfF = s("lmf_f"); p.lmfG = s("lmf_g"); p.lmfQ = s("lmf_q");
        p.lfF = s("lf_f"); p.lfG = s("lf_g"); p.lfBell = s("lf_bell");
    }
    pThr_ = raw("par_thr"); pRatio_ = raw("par_ratio"); pMakeup_ = raw("par_makeup"); pRel_ = raw("par_rel");
    pFast_ = raw("par_fast"); pRet_ = raw("par_ret");
    rRoom_ = raw("rv_room"); rDecay_ = raw("rv_decay"); rPre_ = raw("rv_pre"); rTone_ = raw("rv_tone");
    rLow_ = raw("rv_low"); rHigh_ = raw("rv_high"); rWidth_ = raw("rv_width"); rRet_ = raw("rv_ret");
    master_ = raw("master");
    grooveBpm_ = raw("groove_bpm"); grooveHost_ = raw("groove_host"); grooveXport_ = raw("groove_xport");

    // parameters a MIDI CC may drive
    for (int i = 0; i < od::kNumStrips; ++i)
        for (const char* k : {"fader", "pan", "sendrv", "sendpar", "mute", "solo"})
        {
            const char* what = std::string(k) == "fader" ? "Fader" : std::string(k) == "pan" ? "Pan"
                             : std::string(k) == "sendrv" ? "Send Reverb" : std::string(k) == "sendpar" ? "Send Paralelo"
                             : std::string(k) == "mute" ? "Mute" : "Solo";
            mappable_.add(apvts.getParameter(pid::strip(i, k)));
            mappableNames_.add(juce::String(stripLabel(i)) + " · " + what);
        }
    for (auto [id, name] : {std::pair<const char*, const char*>{"master", "Master · Volume"},
                            {"rv_ret", "Bus Reverb · Retorno"}, {"par_ret", "Bus Paralelo · Retorno"},
                            {"rv_decay", "Bus Reverb · Decay"}, {"par_thr", "Bus Paralelo · Threshold"}})
    {
        mappable_.add(apvts.getParameter(id));
        mappableNames_.add(name);
    }
}

juce::File OpenDrumsProcessor::defaultKitsFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_KITS", {});
    if (env.isNotEmpty() && juce::File(env).isDirectory())
        return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("OpenDrums").getChildFile("Kits");
}

juce::File OpenDrumsProcessor::defaultGroovesFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("OPENDRUMS_GROOVES", {});
    if (env.isNotEmpty() && juce::File(env).isDirectory())
        return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("OpenDrums").getChildFile("Grooves");
}

// ------------------------------------------------------------------ audio

void OpenDrumsProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    engine_.prepare(sampleRate);
    engine_.setMasterGain(1.0f);
    mixer_.prepare(sampleRate);
}

bool OpenDrumsProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void OpenDrumsProcessor::buildMixParams(od::MixParams& mp) const
{
    for (int i = 0; i < od::kNumStrips; ++i)
    {
        const auto& p = sp_[(size_t) i];
        auto& s = mp.strip[(size_t) i];
        s.faderDb = p.fader->load(); s.pan = p.pan->load();
        s.mute = p.mute->load() > 0.5f; s.solo = p.solo->load() > 0.5f; s.phase = p.phase->load() > 0.5f;
        s.sendReverb = p.sendRv->load(); s.sendParallel = p.sendPar->load();
        s.comp.on = p.cOn->load() > 0.5f; s.comp.thresholdDb = p.cThr->load(); s.comp.ratio = p.cRatio->load();
        s.comp.makeupDb = p.cMakeup->load(); s.comp.releaseMs = p.cRel->load(); s.comp.fastAttack = p.cFast->load() > 0.5f;
        s.eq.on = p.eOn->load() > 0.5f;
        s.eq.hfFreq = p.hfF->load(); s.eq.hfGain = p.hfG->load(); s.eq.hfBell = p.hfBell->load() > 0.5f;
        s.eq.hmfFreq = p.hmfF->load(); s.eq.hmfGain = p.hmfG->load(); s.eq.hmfQ = p.hmfQ->load();
        s.eq.lmfFreq = p.lmfF->load(); s.eq.lmfGain = p.lmfG->load(); s.eq.lmfQ = p.lmfQ->load();
        s.eq.lfFreq = p.lfF->load(); s.eq.lfGain = p.lfG->load(); s.eq.lfBell = p.lfBell->load() > 0.5f;
        s.outMask = routeMask_[(size_t) i].load(std::memory_order_relaxed);
    }
    mp.parallel.comp.thresholdDb = pThr_->load(); mp.parallel.comp.ratio = pRatio_->load();
    mp.parallel.comp.makeupDb = pMakeup_->load(); mp.parallel.comp.releaseMs = pRel_->load();
    mp.parallel.comp.fastAttack = pFast_->load() > 0.5f; mp.parallel.returnDb = pRet_->load();
    mp.reverb.rv.room = juce::jlimit(0, 7, (int) std::lround(rRoom_->load()));
    mp.reverb.rv.decayScale = rDecay_->load(); mp.reverb.rv.extraPredelayMs = rPre_->load();
    mp.reverb.rv.tone = rTone_->load(); mp.reverb.rv.lowCutHz = rLow_->load();
    mp.reverb.rv.highCutHz = rHigh_->load(); mp.reverb.rv.width = rWidth_->load();
    mp.reverb.returnDb = rRet_->load();
    mp.masterDb = master_->load();
}

void OpenDrumsProcessor::pushCc(int target, int value)
{
    int s1, n1, s2, n2;
    ccFifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0)
    {
        auto* p = mappable_[target];
        const bool isBool = dynamic_cast<juce::AudioParameterBool*>(p) != nullptr;
        ccBuf_[(size_t) s1] = {target, isBool ? (value >= 64 ? 1.0f : 0.0f) : (float) value / 127.0f};
        ccFifo_.finishedWrite(1);
    }
}

void OpenDrumsProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // ---- host info
    bool hostPlaying = false;
    double hostBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
        }

    // ---- groove player control
    if (clipDirty_.load())
    {
        const juce::SpinLock::ScopedTryLockType lock(clipLock_);
        if (lock.isLocked())
        {
            groovePlayer_.setClip(pendingClip_);
            clipDirty_ = false;
        }
    }
    if (grooveXport_->load() > 0.5f)
    {
        if (hostPlaying && !lastHostPlaying_) groovePlayer_.start();
        if (!hostPlaying && lastHostPlaying_) groovePlayer_.stop();
    }
    lastHostPlaying_ = hostPlaying;
    switch (grooveCmd_.exchange(0))
    {
        case 1: groovePlayer_.start(); break;
        case 2: groovePlayer_.stop(); break;
        default: break;
    }

    // ---- gather events: host MIDI and groove notes, both sorted by position
    int nHost = 0, nGroove = 0;
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        const int pos = juce::jlimit(0, numSamples - 1, metadata.samplePosition);
        if (m.isNoteOn() && nHost < kMaxEv)
            evHost_[(size_t) nHost++] = {pos, 0, m.getNoteNumber(), m.getVelocity(), m.getChannel() - 1};
        else if ((m.isAllNotesOff() || m.isAllSoundOff()) && nHost < kMaxEv)
            evHost_[(size_t) nHost++] = {pos, 1, 0, 0, 0};
        else if (m.isController())
        {
            const int target = ccTarget_[(size_t) m.getControllerNumber()].load(std::memory_order_relaxed);
            if (target >= 0 && target < mappable_.size()) pushCc(target, m.getControllerValue());
        }
    }
    {
        double bpm = grooveBpm_->load();
        if (grooveHost_->load() > 0.5f && hostBpm > 0.0) bpm = hostBpm;
        groovePlayer_.process(numSamples, sampleRate_, bpm, [&](int off, int note, int vel) {
            if (nGroove < kMaxEv)
                evGroove_[(size_t) nGroove++] = {juce::jlimit(0, numSamples - 1, off), 0, note, vel, -1};
        });
        groovePlaying_ = groovePlayer_.isPlaying();
        groovePos_ = groovePlayer_.position();
    }
    int nAll = 0;
    {
        int a = 0, g = 0;
        while ((a < nHost || g < nGroove) && nAll < kMaxEv)
        {
            if (g >= nGroove || (a < nHost && evHost_[(size_t) a].pos <= evGroove_[(size_t) g].pos))
                evAll_[(size_t) nAll++] = evHost_[(size_t) a++];
            else
                evAll_[(size_t) nAll++] = evGroove_[(size_t) g++];
        }
    }

    // ---- mixer parameters
    od::MixParams mp;
    buildMixParams(mp);
    mixer_.setParams(mp);
    engine_.drainPosted();

    // map the 16 mixer output channels to host buffer channels (-1 = bus disabled)
    int hostCh[od::kNumOutChannels];
    for (int c = 0; c < od::kNumOutChannels; ++c)
    {
        hostCh[c] = -1;
        const int busIndex = c / 2;
        if (busIndex < getBusCount(false))
            if (auto* bus = getBus(false, busIndex))
                if (bus->isEnabled())
                    hostCh[c] = bus->getChannelIndexInProcessBlockBuffer(c % 2);
    }

    float* sl[od::kNumStrips + 1];
    float* sr[od::kNumStrips + 1];
    float* oc[od::kNumOutChannels];

    int pos = 0, ev = 0;
    while (pos < numSamples)
    {
        while (ev < nAll && evAll_[(size_t) ev].pos <= pos)
        {
            const auto& e = evAll_[(size_t) ev++];
            if (e.kind == 0) engine_.noteOn(e.a, e.b, e.ch);
            else engine_.allSoundOff();
        }
        int next = std::min(numSamples, pos + kSeg);
        if (ev < nAll) next = std::min(next, std::max(evAll_[(size_t) ev].pos, pos + 1));
        const int len = next - pos;

        for (int i = 0; i <= od::kNumStrips; ++i)
        {
            std::fill_n(bufL_[(size_t) i].begin(), len, 0.0f);
            std::fill_n(bufR_[(size_t) i].begin(), len, 0.0f);
            sl[i] = bufL_[(size_t) i].data();
            sr[i] = bufR_[(size_t) i].data();
        }
        for (int c = 0; c < od::kNumOutChannels; ++c)
        {
            std::fill_n(outBuf_[(size_t) c].begin(), len, 0.0f);
            oc[c] = outBuf_[(size_t) c].data();
        }
        engine_.renderStrips(sl, sr, len);
        mixer_.process(sl, sr, oc, len);
        for (int c = 0; c < od::kNumOutChannels; ++c)
            if (hostCh[c] >= 0 && hostCh[c] < buffer.getNumChannels())
                juce::FloatVectorOperations::add(buffer.getWritePointer(hostCh[c]) + pos, oc[c], len);
        pos = next;
    }
    midi.clear();
}

void OpenDrumsProcessor::timerCallback()
{
    // MIDI CC -> parameters (from the audio thread FIFO)
    int s1, n1, s2, n2;
    const int ready = ccFifo_.getNumReady();
    if (ready > 0)
    {
        ccFifo_.prepareToRead(ready, s1, n1, s2, n2);
        auto apply = [&](int start, int count) {
            for (int i = 0; i < count; ++i)
            {
                const auto e = ccBuf_[(size_t) (start + i)];
                if (auto* p = mappable_[e.target]) p->setValueNotifyingHost(e.value);
            }
        };
        apply(s1, n1);
        apply(s2, n2);
        ccFifo_.finishedRead(n1 + n2);
    }
    if (++timerTicks_ >= 30)
    {
        timerTicks_ = 0;
        engine_.collectGarbage();
    }
}

juce::AudioProcessorEditor* OpenDrumsProcessor::createEditor()
{
    return new OpenDrumsEditor(*this);
}

// ------------------------------------------------------------------ matrix

void OpenDrumsProcessor::setChannelMask(int strip, uint16_t mask)
{
    chMask_[(size_t) strip] = mask;
    engine_.setChannelMask(strip, mask);
}

void OpenDrumsProcessor::setNoteOverride(const juce::String& slot, const juce::String& role, const std::vector<int>& notes)
{
    noteOverrides_[slot.toStdString()][role.toStdString()] = notes;
    reloadRig();
}

void OpenDrumsProcessor::clearNoteOverrides()
{
    noteOverrides_.clear();
    reloadRig();
}

// ------------------------------------------------------------------ kits

void OpenDrumsProcessor::setKitsFolder(const juce::File& folder)
{
    kitsFolder_ = folder;
    selection_.clear();
    kitId_ = {};
    rescanKits();
}

void OpenDrumsProcessor::rescanKits()
{
    ++generation_;
    loader_.removeAllJobs(true, 10000);
    loading_ = false;

    library_.scan(kitsFolder_.getFullPathName().toStdString());

    if (library_.findKit(kitId_.toStdString()) == nullptr)
    {
        kitId_ = library_.kits().empty() ? juce::String() : juce::String(library_.kits().front()->id);
        selection_.clear();
    }
    reloadRig();
}

void OpenDrumsProcessor::selectKit(const juce::String& kitId)
{
    if (library_.findKit(kitId.toStdString()) == nullptr)
        return;
    kitId_ = kitId;
    selection_.clear();
    reloadRig();
}

void OpenDrumsProcessor::setSlotPiece(const juce::String& slot, const juce::String& pieceRef)
{
    if (!library_.findPiece(pieceRef.toStdString()))
        return;
    selection_[slot.toStdString()] = pieceRef.toStdString();
    reloadRig();
}

void OpenDrumsProcessor::resetSlot(const juce::String& slot)
{
    if (selection_.erase(slot.toStdString()) > 0)
        reloadRig();
}

void OpenDrumsProcessor::reloadRig()
{
    const od::KitDef* kit = library_.findKit(kitId_.toStdString());
    if (kit == nullptr)
    {
        rig_.reset();
        sendChangeMessage();
        return;
    }

    const int generation = ++generation_;
    loading_ = true;
    sendChangeMessage();

    od::Selection selection = selection_;
    od::NoteOverrides overrides = noteOverrides_;
    const std::string kitId = kitId_.toStdString();
    auto alive = alive_;

    loader_.addJob([this, generation, kitId, selection, overrides, alive] {
        const od::KitDef* k = library_.findKit(kitId);
        if (k == nullptr || generation != generation_.load())
            return;
        std::shared_ptr<const od::Rig> rig = od::buildRig(library_, *k, selection, overrides);

        juce::MessageManager::callAsync([this, generation, rig, alive] {
            if (!alive->load() || generation != generation_.load())
                return;
            rig_ = rig;
            engine_.setRig(rig);
            loading_ = false;
            sendChangeMessage();
        });
    });
}

int OpenDrumsProcessor::auditionNoteForSlot(const juce::String& slot) const
{
    if (!rig_)
        return -1;
    const std::string id = slot.toStdString();
    int fallback = -1;
    for (int n = 0; n < 128; ++n)
    {
        const auto& target = rig_->notes[(size_t) n];
        if (target.slot == nullptr || target.slot->view.slot != id)
            continue;
        if (target.art->role == "hit" || target.art->role == "closed")
            return n;
        if (fallback < 0)
            fallback = n;
    }
    return fallback;
}

// ------------------------------------------------------------------ grooves

void OpenDrumsProcessor::setGroovesFolder(const juce::File& folder)
{
    groovesFolder_ = folder;
    rescanGrooves();
}

void OpenDrumsProcessor::rescanGrooves()
{
    clipCache_.clear();
    grooves_.scan(groovesFolder_.getFullPathName().toStdString());
    sendChangeMessage();
}

std::shared_ptr<const od::MidiClip> OpenDrumsProcessor::clipFor(const od::GrooveClipInfo& info)
{
    auto it = clipCache_.find(info.relPath);
    if (it != clipCache_.end())
        return it->second;
    std::string err;
    auto clip = od::loadMidiClip(info.path, err); // nullptr is cached too: do not retry a broken file on every repaint
    clipCache_[info.relPath] = clip;
    return clip;
}

void OpenDrumsProcessor::selectGrooveClip(const od::GrooveClipInfo& info)
{
    auto clip = clipFor(info);
    if (!clip)
        return;
    grooveRel_ = juce::String(info.relPath);
    {
        const juce::SpinLock::ScopedLockType lock(clipLock_);
        pendingClip_ = clip;
    }
    clipDirty_ = true;
    if (auto* p = apvts.getParameter("groove_bpm"))
        p->setValueNotifyingHost(p->convertTo0to1((float) clip->bpm));
}

void OpenDrumsProcessor::toggleFavorite(const juce::String& rel)
{
    const std::string r = rel.toStdString();
    if (!favorites_.erase(r))
        favorites_.insert(r);
}

// ------------------------------------------------------------------ state

void OpenDrumsProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("OpenDrumsState");
    xml.setAttribute("version", 2);
    xml.setAttribute("kit", kitId_);
    xml.setAttribute("kitsFolder", kitsFolder_.getFullPathName());
    xml.setAttribute("groovesFolder", groovesFolder_.getFullPathName());
    xml.setAttribute("groove", grooveRel_);
    xml.setAttribute("view", lastView_);
    for (const auto& [slot, ref] : selection_)
    {
        auto* e = xml.createNewChildElement("Piece");
        e->setAttribute("slot", juce::String(slot));
        e->setAttribute("ref", juce::String(ref));
    }
    for (int i = 0; i < od::kNumStrips; ++i)
    {
        auto* e = xml.createNewChildElement("Strip");
        e->setAttribute("i", i);
        e->setAttribute("route", (int) routeMask_[(size_t) i].load());
        e->setAttribute("channels", (int) chMask_[(size_t) i]);
    }
    for (int cc = 0; cc < 128; ++cc)
        if (ccTarget_[(size_t) cc].load() >= 0)
        {
            auto* e = xml.createNewChildElement("Cc");
            e->setAttribute("cc", cc);
            e->setAttribute("target", mappableNames_[ccTarget_[(size_t) cc].load()]);
        }
    for (const auto& [slot, roles] : noteOverrides_)
        for (const auto& [role, notes] : roles)
        {
            auto* e = xml.createNewChildElement("Note");
            e->setAttribute("slot", juce::String(slot));
            e->setAttribute("role", juce::String(role));
            juce::StringArray list;
            for (int n : notes) list.add(juce::String(n));
            e->setAttribute("notes", list.joinIntoString(","));
        }
    for (const auto& f : favorites_)
        xml.createNewChildElement("Favorite")->setAttribute("rel", juce::String(f));
    if (auto params = apvts.copyState().createXml())
        xml.addChildElement(params.release());
    copyXmlToBinary(xml, destData);
}

void OpenDrumsProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("OpenDrumsState"))
        return;

    if (auto* params = xml->getChildByName(apvts.state.getType().toString()))
        apvts.replaceState(juce::ValueTree::fromXml(*params));

    const juce::File savedKits(xml->getStringAttribute("kitsFolder"));
    if (savedKits.isDirectory()) kitsFolder_ = savedKits;
    const juce::File savedGrooves(xml->getStringAttribute("groovesFolder"));
    if (savedGrooves.isDirectory() && savedGrooves != groovesFolder_)
    {
        groovesFolder_ = savedGrooves;
        rescanGrooves();
    }
    lastView_ = xml->getIntAttribute("view", 0);

    kitId_ = xml->getStringAttribute("kit");
    selection_.clear();
    for (auto* e : xml->getChildWithTagNameIterator("Piece"))
        selection_[e->getStringAttribute("slot").toStdString()] = e->getStringAttribute("ref").toStdString();

    for (auto* e : xml->getChildWithTagNameIterator("Strip"))
    {
        const int i = e->getIntAttribute("i", -1);
        if (i < 0 || i >= od::kNumStrips) continue;
        setRouteMask(i, (uint32_t) e->getIntAttribute("route", (int) od::kDefaultRoute));
        setChannelMask(i, (uint16_t) e->getIntAttribute("channels", 0xFFFF));
    }
    for (auto& t : ccTarget_) t.store(-1);
    for (auto* e : xml->getChildWithTagNameIterator("Cc"))
    {
        const int cc = e->getIntAttribute("cc", -1);
        const int t = mappableNames_.indexOf(e->getStringAttribute("target"));
        if (cc >= 0 && cc < 128 && t >= 0) ccTarget_[(size_t) cc].store(t);
    }
    noteOverrides_.clear();
    for (auto* e : xml->getChildWithTagNameIterator("Note"))
    {
        std::vector<int> notes;
        for (const auto& s : juce::StringArray::fromTokens(e->getStringAttribute("notes"), ",", ""))
            if (s.isNotEmpty()) notes.push_back(s.getIntValue());
        noteOverrides_[e->getStringAttribute("slot").toStdString()][e->getStringAttribute("role").toStdString()] = notes;
    }
    favorites_.clear();
    for (auto* e : xml->getChildWithTagNameIterator("Favorite"))
        favorites_.insert(e->getStringAttribute("rel").toStdString());

    rescanKits();

    const auto rel = xml->getStringAttribute("groove");
    if (rel.isNotEmpty())
        if (auto* info = grooves_.findByRelPath(rel.toStdString()))
            selectGrooveClip(*info);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenDrumsProcessor();
}
