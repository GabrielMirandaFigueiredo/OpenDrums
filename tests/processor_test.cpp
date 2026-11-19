// Headless test of the OpenDrums processor: parameters, multi-bus routing, groove player, state round trip.
#include "PluginProcessor.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static float peakOf(const juce::AudioBuffer<float>& b, int ch)
{
    return b.getNumChannels() > ch ? b.getMagnitude(ch, 0, b.getNumSamples()) : 0.0f;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    OpenDrumsProcessor p;

    // wait for the kit to load (the loader thread hands the rig over through the message loop)
    for (int i = 0; i < 400 && (p.getRig() == nullptr || p.isLoading()); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(25);
    CHECK(p.getRig() != nullptr);
    std::printf("kit %s, grooves %d clips\n", p.getCurrentKitId().toRawUTF8(), p.getGrooveLibrary().totalClips());

    // 8 stereo buses enabled
    juce::AudioProcessor::BusesLayout layout = p.getBusesLayout();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference(i) = juce::AudioChannelSet::stereo();
    CHECK(p.setBusesLayout(layout));
    CHECK(p.getTotalNumOutputChannels() == 16);
    p.setRateAndBufferSizeDetails(48000, 512);
    p.prepareToPlay(48000, 512);

    auto run = [&](int blocks, juce::MidiBuffer first) {
        juce::AudioBuffer<float> acc(16, 512);
        acc.clear();
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf(16, 512);
            juce::MidiBuffer m;
            if (b == 0) m = first;
            p.processBlock(buf, m);
            for (int c = 0; c < 16; ++c)
                for (int s = 0; s < 512; ++s)
                    acc.setSample(c, s, std::max(acc.getSample(c, s), std::abs(buf.getSample(c, s))));
        }
        return acc;
    };

    // kick on note 36 -> master (outputs 1/2)
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), 0);
    auto out = run(20, m);
    std::printf("default route: out0 %.3f out2 %.3f\n", peakOf(out, 0), peakOf(out, 2));
    CHECK(peakOf(out, 0) > 0.01f && peakOf(out, 1) > 0.01f);
    CHECK(peakOf(out, 2) < 1e-6f);

    // route the kick (strip 0) to stereo 3/4 = route bit 2 -> host channels 2/3
    p.setRouteMask(0, 1u << 2);
    run(40, {}); // let the tail die
    m.clear();
    m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), 0);
    out = run(20, m);
    std::printf("kick -> 3/4: out0 %.4f out2 %.3f out3 %.3f\n", peakOf(out, 0), peakOf(out, 2), peakOf(out, 3));
    CHECK(peakOf(out, 2) > 0.01f && peakOf(out, 3) > 0.01f);
    CHECK(peakOf(out, 0) < 0.01f);

    // mono route: mono 3 = bit 9 + 2 -> out channel index 2 (mono n -> channel n-1)
    p.setRouteMask(0, 1u << (od::kRouteMonoFirst + 4)); // mono 5 -> channel 4
    run(40, {});
    m.clear();
    m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), 0);
    out = run(20, m);
    std::printf("kick -> mono 5: ch4 %.3f ch0 %.4f\n", peakOf(out, 4), peakOf(out, 0));
    CHECK(peakOf(out, 4) > 0.01f && peakOf(out, 0) < 0.01f);
    p.setRouteMask(0, od::kDefaultRoute);

    // mute through the parameter
    run(40, {});
    if (auto* mute = p.apvts.getParameter(pid::strip(0, "mute"))) mute->setValueNotifyingHost(1.0f);
    m.clear();
    m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), 0);
    out = run(20, m);
    CHECK(peakOf(out, 0) < 0.05f); // only the reverb return reaches the master
    p.apvts.getParameter(pid::strip(0, "mute"))->setValueNotifyingHost(0.0f);

    // MIDI channel filter: kick only on channel 3 -> a note on channel 10 is ignored
    m.clear();
    m.addEvent(juce::MidiMessage::allSoundOff(10), 0);
    run(60, m); // silence the muted voice that is still ringing
    p.setChannelMask(0, 1u << 2);
    m.clear();
    m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 110), 0);
    out = run(20, m);
    CHECK(peakOf(out, 0) < 1e-4f);
    m.clear();
    m.addEvent(juce::MidiMessage::noteOn(3, 36, (juce::uint8) 110), 0);
    out = run(20, m);
    CHECK(peakOf(out, 0) > 0.01f);
    p.setChannelMask(0, 0xFFFF);

    // CC -> fader through the FIFO (message thread timer drains it)
    run(40, {});
    p.setCcTarget(20, 0); // target 0 = Bumbo fader
    m.clear();
    m.addEvent(juce::MidiMessage::controllerEvent(1, 20, 0), 0);
    run(1, m);
    for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
    const float fader = p.apvts.getRawParameterValue(pid::strip(0, "fader"))->load();
    std::printf("CC20=0 -> kick fader %.1f dB\n", fader);
    CHECK(fader < -50.0f);
    p.apvts.getParameter(pid::strip(0, "fader"))->setValueNotifyingHost(p.apvts.getParameter(pid::strip(0, "fader"))->convertTo0to1(0.0f));
    p.setCcTarget(20, -1);

    // groove player: pick the first clip, start, expect hits
    if (p.getGrooveLibrary().totalClips() > 0)
    {
        const auto& clip = p.getGrooveLibrary().styles().front().meters.front().sections.front().clips.front();
        p.selectGrooveClip(clip);
        p.grooveStart();
        auto g = run(60, {});
        std::printf("groove playing %d, out0 %.3f\n", (int) p.isGroovePlaying(), peakOf(g, 0));
        CHECK(p.isGroovePlaying() && peakOf(g, 0) > 0.01f);
        p.grooveStop();
        run(2, {});
        CHECK(!p.isGroovePlaying());
    }

    // state round trip
    p.setRouteMask(1, 1u << 3);
    p.setChannelMask(2, 0x0200);
    p.setCcTarget(33, 5);
    p.toggleFavorite("Funk/4-4 90bpm/02 Verso/Verso 01.mid");
    p.setNoteOverride("kick", "hit", {36, 35, 60});
    juce::MemoryBlock state;
    p.getStateInformation(state);
    OpenDrumsProcessor q;
    q.setStateInformation(state.getData(), (int) state.getSize());
    for (int i = 0; i < 200 && q.isLoading(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(25);
    CHECK(q.getRouteMask(1) == (1u << 3));
    CHECK(q.getChannelMask(2) == 0x0200);
    CHECK(q.getCcTarget(33) == 5);
    CHECK(q.isFavorite("Funk/4-4 90bpm/02 Verso/Verso 01.mid"));
    CHECK(q.getNoteOverrides().count("kick") == 1);
    if (q.getRig()) std::printf("note 60 mapped after override: %d\n", q.getRig()->notes[60].slot != nullptr);

    // defaults: reverb sends open on every strip (the room must be audible out of the box), compressor attack slow
    for (int i = 0; i < od::kNumStrips; ++i)
    {
        CHECK(p.apvts.getParameter(pid::strip(i, "sendrv"))->getDefaultValue() > 0.2f);
        CHECK(p.apvts.getRawParameterValue(pid::strip(i, "c_fast"))->load() < 0.5f);
    }

    std::printf(failures == 0 ? "PROCESSOR TEST: ALL PASSED\n" : "PROCESSOR TEST: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
