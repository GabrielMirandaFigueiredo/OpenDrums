#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.hpp"
#include "Grooves.hpp"
#include "Library.hpp"
#include "Mixer.hpp"
#include "Rig.hpp"

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>

// Display names (pt-BR) of the 14 mixer strips, in core order (od::stripId).
inline const char* stripLabel(int i)
{
    static const char* n[od::kNumStrips] = {"Bumbo", "Caixa", "Tom 1", "Tom 2", "Tom 3", "Surdo", "Chimbal",
                                            "Ataque 1", "Ataque 2", "Condução", "Splash", "China", "Cowbell", "Pandeiro"};
    return n[juce::jlimit(0, od::kNumStrips - 1, i)];
}

// Parameter ids. Strip parameters are "s<N>_<name>", e.g. "s0_fader".
namespace pid
{
inline juce::String strip(int i, const char* name) { return "s" + juce::String(i) + "_" + name; }
}

// OpenDrums: sampled drum instrument. The heavy lifting lives in the JUCE-free core (core/);
// this class is the glue between the host, the core and the editor.
class OpenDrumsProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster, // editor refreshes when the rig / library changes
                           private juce::Timer
{
public:
    static constexpr int kExtraBuses = 7; // stereo outputs 2..8 (the main bus is stereo output 1)

    OpenDrumsProcessor();
    ~OpenDrumsProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OpenDrums"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- kits (message thread) ----
    juce::File getKitsFolder() const { return kitsFolder_; }
    void setKitsFolder(const juce::File& folder);
    void rescanKits();
    const od::Library& getLibrary() const { return library_; }
    juce::String getCurrentKitId() const { return kitId_; }
    void selectKit(const juce::String& kitId);
    void setSlotPiece(const juce::String& slot, const juce::String& pieceRef);
    void resetSlot(const juce::String& slot);
    bool isSlotSwapped(const juce::String& slot) const { return selection_.count(slot.toStdString()) > 0; }
    std::shared_ptr<const od::Rig> getRig() const { return rig_; }
    bool isLoading() const { return loading_.load(); }

    int auditionNoteForSlot(const juce::String& slot) const;
    void audition(int note, int velocity) { engine_.postNote(note, velocity); }
    bool popHit(int& note, int& velocity) { return engine_.popHit(note, velocity); }

    // ---- mixer: routing, meters ----
    uint32_t getRouteMask(int strip) const { return routeMask_[(size_t) strip].load(); }
    void setRouteMask(int strip, uint32_t mask) { routeMask_[(size_t) strip].store(mask ? mask : 0u); }
    float takeStripPeak(int strip) { return mixer_.takeStripPeak(strip); }
    float takeMasterPeak(int ch) { return mixer_.takeMasterPeak(ch); }
    float stripGainReductionDb(int strip) const { return mixer_.stripGainReductionDb(strip); }
    float parallelGainReductionDb() const { return mixer_.parallelGainReductionDb(); }

    // ---- matrix: MIDI channels, CC, notes ----
    uint16_t getChannelMask(int strip) const { return chMask_[(size_t) strip]; }
    void setChannelMask(int strip, uint16_t mask);
    int numCcTargets() const { return mappable_.size(); }
    juce::String ccTargetName(int i) const { return mappableNames_[i]; }
    int getCcTarget(int cc) const { return ccTarget_[(size_t) cc].load(); } // -1 = none
    void setCcTarget(int cc, int target) { ccTarget_[(size_t) cc].store(target); }
    const od::NoteOverrides& getNoteOverrides() const { return noteOverrides_; }
    void setNoteOverride(const juce::String& slot, const juce::String& role, const std::vector<int>& notes);
    void clearNoteOverrides();

    // ---- grooves (message thread) ----
    juce::File getGroovesFolder() const { return groovesFolder_; }
    void setGroovesFolder(const juce::File& folder);
    void rescanGrooves();
    const od::GrooveLibrary& getGrooveLibrary() const { return grooves_; }
    std::shared_ptr<const od::MidiClip> clipFor(const od::GrooveClipInfo& info); // cached
    void selectGrooveClip(const od::GrooveClipInfo& info);                       // loads it into the player
    juce::String getCurrentGrooveRel() const { return grooveRel_; }
    void grooveStart() { grooveCmd_ = 1; }
    void grooveStop() { grooveCmd_ = 2; }
    bool isGroovePlaying() const { return groovePlaying_.load(); }
    float groovePosition() const { return groovePos_.load(); }
    bool isFavorite(const juce::String& rel) const { return favorites_.count(rel.toStdString()) > 0; }
    void toggleFavorite(const juce::String& rel);
    const std::set<std::string>& getFavorites() const { return favorites_; }

    // Developer/test hook: which view the editor shows first (also saved in the state).
    int getLastView() const { return lastView_; }
    void setLastView(int v) { lastView_ = v; }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void reloadRig();
    void buildMixParams(od::MixParams& mp) const;
    void pushCc(int target, int value);
    static BusesProperties makeBuses();
    static juce::File defaultKitsFolder();
    static juce::File defaultGroovesFolder();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void cacheParameters();

    od::DrumEngine engine_;
    od::Mixer mixer_;
    od::Library library_;
    od::GrooveLibrary grooves_;

    juce::File kitsFolder_, groovesFolder_;
    juce::String kitId_;
    od::Selection selection_;
    od::NoteOverrides noteOverrides_;
    std::shared_ptr<const od::Rig> rig_;

    juce::ThreadPool loader_{1};
    std::atomic<int> generation_{0};
    std::atomic<bool> loading_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    // routing / matrix state (read by the audio thread)
    std::array<std::atomic<uint32_t>, od::kNumStrips> routeMask_{};
    std::array<uint16_t, od::kNumStrips> chMask_{};
    std::array<std::atomic<int>, 128> ccTarget_{};
    juce::Array<juce::RangedAudioParameter*> mappable_;
    juce::StringArray mappableNames_;

    // CC -> parameter hand-over (audio -> message thread)
    struct CcEvent { int target; float value; };
    juce::AbstractFifo ccFifo_{256};
    std::array<CcEvent, 256> ccBuf_{};

    // grooves
    juce::SpinLock clipLock_;
    std::shared_ptr<const od::MidiClip> pendingClip_;
    std::atomic<bool> clipDirty_{false};
    std::atomic<int> grooveCmd_{0}; // 1 start, 2 stop
    std::atomic<bool> groovePlaying_{false};
    std::atomic<float> groovePos_{0.0f};
    od::GroovePlayer groovePlayer_; // audio thread only
    bool lastHostPlaying_ = false;
    juce::String grooveRel_;
    std::map<std::string, std::shared_ptr<const od::MidiClip>> clipCache_;
    std::set<std::string> favorites_;
    int lastView_ = 0;
    int timerTicks_ = 0;

    // cached parameter pointers
    struct StripPtrs
    {
        std::atomic<float> *fader, *pan, *mute, *solo, *phase, *sendRv, *sendPar;
        std::atomic<float> *cOn, *cThr, *cRatio, *cMakeup, *cRel, *cFast;
        std::atomic<float> *eOn, *hfF, *hfG, *hfBell, *hmfF, *hmfG, *hmfQ, *lmfF, *lmfG, *lmfQ, *lfF, *lfG, *lfBell;
    };
    std::array<StripPtrs, od::kNumStrips> sp_{};
    std::atomic<float> *pThr_, *pRatio_, *pMakeup_, *pRel_, *pFast_, *pRet_;
    std::atomic<float> *rRoom_, *rDecay_, *rPre_, *rTone_, *rLow_, *rHigh_, *rWidth_, *rRet_;
    std::atomic<float>* master_;
    std::atomic<float>* grooveBpm_;
    std::atomic<float>* grooveHost_;   // follow host tempo
    std::atomic<float>* grooveXport_;  // follow host transport

    static constexpr int kSeg = od::Mixer::kMaxBlock;
    std::array<std::array<float, kSeg>, od::kNumStrips + 1> bufL_{}, bufR_{};
    std::array<std::array<float, kSeg>, od::kNumOutChannels> outBuf_{};
    struct Ev { int pos; int kind; int a; int b; int ch; }; // kind: 0 note-on, 1 all sound off
    static constexpr int kMaxEv = 1024;
    std::array<Ev, kMaxEv> evHost_{}, evGroove_{}, evAll_{};
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenDrumsProcessor)
};
