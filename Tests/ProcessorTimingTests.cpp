#include "PluginProcessor.h"

#include <cstdint>
#include <iostream>
#include <vector>

#define REQUIRE(condition) do { if (! (condition)) { \
    std::cerr << "FAILED: " #condition " at line " << __LINE__ << '\n'; return 1; \
} } while (false)

namespace
{
bool setParameter (PFSAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    if (parameter == nullptr)
        return false;
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    return true;
}

int levelForFlatIndex (int flatIndex)
{
    return pfs::SequencerEngine::levelForFlatIndex (flatIndex);
}
}

int main()
{
    constexpr double sampleRate = 44100.0;
    // The UMC ASIO driver used during testing can report an eight-sample block.
    // Exercise the timing at that worst-case callback size as well.
    constexpr int blockSize = 8;

    // Standalone launches must ignore JUCE's persisted filterState, while
    // plugin instances must continue to restore project state.
    PFSAudioProcessor savedStateSource;
    savedStateSource.setNodeActive (0, 0, false);
    savedStateSource.setNodeMidi (0, 0, 59);
    juce::MemoryBlock savedState;
    savedStateSource.getStateInformation (savedState);

    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PFSAudioProcessor cleanStandalone;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    REQUIRE (cleanStandalone.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
    cleanStandalone.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));
    REQUIRE (cleanStandalone.getNode (0, 0).active);
    REQUIRE (cleanStandalone.getNode (0, 0).midi == pfs::rootMidi);
    REQUIRE (cleanStandalone.parameters.getRawParameterValue ("midiOut")->load() < 0.5f);

    PFSAudioProcessor restoredPlugin;
    restoredPlugin.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));
    REQUIRE (! restoredPlugin.getNode (0, 0).active);
    REQUIRE (restoredPlugin.getNode (0, 0).midi == 59);

    // Internal audio is always available, but a remembered standalone MIDI
    // destination must receive absolutely nothing until CLOCK is enabled.
    PFSAudioProcessor audioOnlyProcessor;
    audioOnlyProcessor.prepareToPlay (sampleRate, blockSize);
    REQUIRE (setParameter (audioOnlyProcessor, "internalPlay", 1.0f));
    juce::AudioBuffer<float> audioOnlyBuffer (2, blockSize);
    bool heardAudioOnlyVoice = false;
    for (int processed = 0; processed < 8000; processed += blockSize)
    {
        juce::MidiBuffer noOutputMidi;
        noOutputMidi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
        audioOnlyProcessor.processBlock (audioOnlyBuffer, noOutputMidi);
        REQUIRE (noOutputMidi.isEmpty());
        heardAudioOnlyVoice = heardAudioOnlyVoice
            || audioOnlyBuffer.getMagnitude (0, 0, blockSize) > 0.000001f
            || audioOnlyBuffer.getMagnitude (1, 0, blockSize) > 0.000001f;
    }
    REQUIRE (heardAudioOnlyVoice);
    REQUIRE (audioOnlyProcessor.getDiagnosticSnapshot().voicesCreated[0] == 1);
    REQUIRE (audioOnlyProcessor.getDiagnosticSnapshot().voicesCreated[1] == 1);

    // Diagnostic isolation build: even with routing enabled and a configured
    // depth of four, probability zero must produce one and only one generated
    // subtrigger for the active origin node. Traversal is unchanged.
    PFSAudioProcessor ratchetBypassProcessor;
    ratchetBypassProcessor.prepareToPlay (sampleRate, blockSize);
    REQUIRE (setParameter (ratchetBypassProcessor, "track1Ratchet", 1.0f));
    REQUIRE (setParameter (ratchetBypassProcessor, "track2Mute", 1.0f));
    REQUIRE (setParameter (ratchetBypassProcessor, "ratchetProb", 0.0f));
    REQUIRE (setParameter (ratchetBypassProcessor, "ratchetRepeats", 4.0f));
    REQUIRE (setParameter (ratchetBypassProcessor, "internalPlay", 1.0f));
    const auto ratchetDiagnosticsBefore = ratchetBypassProcessor.getDiagnosticSnapshot();
    for (int processed = 0; processed < 1000; processed += blockSize)
    {
        juce::MidiBuffer midi;
        ratchetBypassProcessor.processBlock (audioOnlyBuffer, midi);
    }
    PFSAudioProcessor::SchedulerTrace ratchetTrace;
    REQUIRE (ratchetBypassProcessor.popSchedulerTrace (ratchetTrace));
    REQUIRE (ratchetTrace.track == 0);
    REQUIRE (ratchetTrace.level == 0);
    REQUIRE (ratchetTrace.index == 0);
    REQUIRE (ratchetTrace.flatIndex == 0);
    REQUIRE (ratchetTrace.active);
    REQUIRE (ratchetTrace.visible);
    REQUIRE (! ratchetTrace.muted);
    REQUIRE (ratchetTrace.triggerRequested);
    REQUIRE (ratchetTrace.ratchetProbability == 0.0f);
    REQUIRE (ratchetTrace.configuredDepth == 4);
    REQUIRE (ratchetTrace.routed);
    REQUIRE (ratchetTrace.ratchetBypassed);
    REQUIRE (ratchetTrace.subtriggerCount == 1);
    REQUIRE (ratchetTrace.sampleOffsets[0] == 0);
    std::cout << "VISIT id=" << ratchetTrace.nodeEvent
              << " tree=" << (ratchetTrace.track + 1)
              << " level=" << ratchetTrace.level
              << " index=" << ratchetTrace.index
              << " node=" << ratchetTrace.flatIndex
              << " active=" << ratchetTrace.active
              << " visible=" << ratchetTrace.visible
              << " muted=" << ratchetTrace.muted
              << " trigger=" << ratchetTrace.triggerRequested
              << " ratchetProbability=" << ratchetTrace.ratchetProbability
              << " configuredDepth=" << ratchetTrace.configuredDepth
              << " routed=" << ratchetTrace.routed
              << " bypass=" << ratchetTrace.ratchetBypassed
              << " generatedSubtriggers=" << ratchetTrace.subtriggerCount << '\n'
              << "SUBTRIGGER nodeEvent=" << ratchetTrace.nodeEvent
              << " track=" << (ratchetTrace.track + 1)
              << " index=1 sampleOffset=" << ratchetTrace.sampleOffsets[0] << '\n';

    // The other tree consumed the same origin visit but was explicitly muted.
    REQUIRE (ratchetBypassProcessor.popSchedulerTrace (ratchetTrace));
    REQUIRE (ratchetTrace.track == 1);
    REQUIRE (ratchetTrace.level == 0);
    REQUIRE (ratchetTrace.index == 0);
    REQUIRE (ratchetTrace.flatIndex == 0);
    REQUIRE (ratchetTrace.active);
    REQUIRE (ratchetTrace.visible);
    REQUIRE (ratchetTrace.muted);
    REQUIRE (! ratchetTrace.triggerRequested);
    REQUIRE (ratchetTrace.subtriggerCount == 0);
    REQUIRE (! ratchetBypassProcessor.popSchedulerTrace (ratchetTrace));
    const auto ratchetDiagnosticsAfter = ratchetBypassProcessor.getDiagnosticSnapshot();
    REQUIRE (ratchetDiagnosticsAfter.triggerRequests[0] - ratchetDiagnosticsBefore.triggerRequests[0] == 1);
    REQUIRE (ratchetDiagnosticsAfter.voicesCreated[0] - ratchetDiagnosticsBefore.voicesCreated[0] == 1);

    PFSAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    for (int track = 0; track < 2; ++track)
        for (int node = 0; node < pfs::nodeCount; ++node)
        {
            const auto state = processor.getNode (track, node);
            REQUIRE (state.active == (node == 0));
            REQUIRE (state.midi == pfs::rootMidi);
        }

    processor.setNodeActive (0, 0, true);
    processor.setNodeActive (1, 0, true);
    REQUIRE (setParameter (processor, "track1Variation", 1.0f));
    REQUIRE (setParameter (processor, "track2Variation", 1.0f));
    REQUIRE (setParameter (processor, "track1Rate", 0.0f));
    REQUIRE (setParameter (processor, "track2Rate", 0.0f));
    REQUIRE (setParameter (processor, "track1Pan", -100.0f));
    REQUIRE (setParameter (processor, "track2Pan", 100.0f));
    REQUIRE (setParameter (processor, "track1Reverb", 0.0f));
    REQUIRE (setParameter (processor, "track2Reverb", 0.0f));
    REQUIRE (setParameter (processor, "midiOut", 1.0f));
    REQUIRE (setParameter (processor, "internalPlay", 1.0f));

    std::array<std::vector<std::int64_t>, 2> noteOns;
    std::array<std::vector<PFSAudioProcessor::SchedulerTrace>, 2> visitTraces;
    juce::AudioBuffer<float> audio (2, blockSize);
    std::int64_t blockStart = 0;
    bool sawAudibleRootBlock = false;
    std::uint64_t lastVisualSerial = 0;

    while (blockStart < 120000)
    {
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        PFSAudioProcessor::SchedulerTrace visitTrace;
        while (processor.popSchedulerTrace (visitTrace))
        {
            REQUIRE (juce::isPositiveAndBelow (visitTrace.track, 2));
            REQUIRE (visitTrace.visible);
            REQUIRE (pfs::SequencerEngine::hasVisibleNode (
                visitTrace.level, visitTrace.index, visitTrace.flatIndex));
            visitTraces[static_cast<std::size_t> (visitTrace.track)].push_back (visitTrace);
        }
        const auto visual = processor.getVisualStepSnapshot();
        if (visual.serial != lastVisualSerial)
        {
            lastVisualSerial = visual.serial;
            REQUIRE (visual.playheads[0] >= 0);
            REQUIRE (visual.playheads[1] >= 0);
            REQUIRE (levelForFlatIndex (visual.playheads[0]) == levelForFlatIndex (visual.playheads[1]));
            REQUIRE ((visual.branchTo[0] < 0) == (visual.branchTo[1] < 0));
        }
        const auto leftPeak = audio.getMagnitude (0, 0, blockSize);
        const auto rightPeak = audio.getMagnitude (1, 0, blockSize);
        if (juce::jmax (leftPeak, rightPeak) > 0.000001f)
        {
            sawAudibleRootBlock = true;
            // Sample 1 is stereo, not dual-mono, so its channel magnitudes do
            // not have to match.  They must both be present in every audible
            // block: a zero side here means one track failed to render.
            REQUIRE (juce::jmin (leftPeak, rightPeak) > 0.000000001f);
        }
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn() && juce::isPositiveAndBelow (message.getChannel() - 1, 2))
                noteOns[static_cast<std::size_t> (message.getChannel() - 1)].push_back (
                    blockStart + metadata.samplePosition);
        }
        blockStart += blockSize;
    }

    REQUIRE (noteOns[0].size() >= 4);
    REQUIRE (noteOns[0] == noteOns[1]);
    REQUIRE (sawAudibleRootBlock);
    const auto rootDiagnostics = processor.getDiagnosticSnapshot();
    REQUIRE (rootDiagnostics.visits[0] == rootDiagnostics.visits[1]);
    REQUIRE (rootDiagnostics.originVisits[0] == rootDiagnostics.originVisits[1]);
    REQUIRE (rootDiagnostics.triggerRequests[0] == rootDiagnostics.triggerRequests[1]);
    REQUIRE (rootDiagnostics.voicesCreated[0] == rootDiagnostics.voicesCreated[1]);
    REQUIRE (visitTraces[0].size() == visitTraces[1].size());
    REQUIRE (visitTraces[0].size() >= static_cast<std::size_t> (pfs::levels * 2));
    for (int track = 0; track < 2; ++track)
        for (std::size_t visit = 0; visit < visitTraces[static_cast<std::size_t> (track)].size(); ++visit)
        {
            const auto& trace = visitTraces[static_cast<std::size_t> (track)][visit];
            REQUIRE (trace.level == static_cast<int> (visit % pfs::levels));
            REQUIRE (trace.active == (trace.flatIndex == 0));
            REQUIRE (trace.triggerRequested == trace.active);
            REQUIRE (trace.subtriggerCount == (trace.active ? 1 : 0));
            if (trace.level == 0)
            {
                REQUIRE (trace.index == 0);
                REQUIRE (trace.flatIndex == 0);
            }
        }

    // The browser tree has five timed rows: origin plus four branch levels.
    // The leaf resets the traversal immediately; no extra pause tick is added.
    const auto expectedCycle = static_cast<std::int64_t> (sampleRate * 60.0 / 90.0 * 5.0 / 4.0);
    for (std::size_t i = 1; i < noteOns[0].size(); ++i)
        REQUIRE (std::abs ((noteOns[0][i] - noteOns[0][i - 1]) - expectedCycle) <= 1);

    // With every node populated, Random has no rests to expose: both tracks
    // must emit on every shared clock tick even though their left/right paths
    // are chosen independently.
    PFSAudioProcessor allActiveProcessor;
    allActiveProcessor.prepareToPlay (sampleRate, blockSize);
    for (int track = 0; track < 2; ++track)
        for (int node = 0; node < pfs::nodeCount; ++node)
            allActiveProcessor.setNodeActive (track, node, true);

    REQUIRE (setParameter (allActiveProcessor, "track1Variation", 1.0f));
    REQUIRE (setParameter (allActiveProcessor, "track2Variation", 1.0f));
    REQUIRE (setParameter (allActiveProcessor, "track1Rate", 0.0f));
    REQUIRE (setParameter (allActiveProcessor, "track2Rate", 0.0f));
    REQUIRE (setParameter (allActiveProcessor, "midiOut", 1.0f));
    REQUIRE (setParameter (allActiveProcessor, "internalPlay", 1.0f));

    std::array<std::vector<std::int64_t>, 2> allActiveNoteOns;
    blockStart = 0;
    while (blockStart < 120000)
    {
        juce::MidiBuffer midi;
        allActiveProcessor.processBlock (audio, midi);
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn() && juce::isPositiveAndBelow (message.getChannel() - 1, 2))
                allActiveNoteOns[static_cast<std::size_t> (message.getChannel() - 1)].push_back (
                    blockStart + metadata.samplePosition);
        }
        blockStart += blockSize;
    }

    REQUIRE (allActiveNoteOns[0].size() >= 12);
    REQUIRE (allActiveNoteOns[0] == allActiveNoteOns[1]);
    const auto allActiveDiagnostics = allActiveProcessor.getDiagnosticSnapshot();
    REQUIRE (allActiveDiagnostics.visits[0] == allActiveDiagnostics.visits[1]);
    REQUIRE (allActiveDiagnostics.triggerRequests[0] == allActiveDiagnostics.triggerRequests[1]);
    REQUIRE (allActiveDiagnostics.voicesCreated[0] == allActiveDiagnostics.voicesCreated[1]);
    const auto expectedStep = static_cast<std::int64_t> (sampleRate * 60.0 / 90.0 / 4.0);
    for (std::size_t i = 1; i < allActiveNoteOns[0].size(); ++i)
        REQUIRE (std::abs ((allActiveNoteOns[0][i] - allActiveNoteOns[0][i - 1]) - expectedStep) <= 1);

    // Rate labels retain the browser meanings: 1 = sixteenth notes,
    // 1/2 = eighth notes, and 1/4 = quarter notes.
    const int rateChoices[] { 0, 1, 2 };
    const int rateDivisors[] { 1, 2, 4 };
    for (int rateTest = 0; rateTest < 3; ++rateTest)
    {
        PFSAudioProcessor rateProcessor;
        rateProcessor.prepareToPlay (sampleRate, blockSize);
        for (int node = 0; node < pfs::nodeCount; ++node)
            rateProcessor.setNodeActive (0, node, true);
        REQUIRE (setParameter (rateProcessor, "track1Rate", static_cast<float> (rateChoices[rateTest])));
        REQUIRE (setParameter (rateProcessor, "track2Mute", 1.0f));
        REQUIRE (setParameter (rateProcessor, "midiOut", 1.0f));
        REQUIRE (setParameter (rateProcessor, "internalPlay", 1.0f));

        std::vector<std::int64_t> rateNoteOns;
        const auto expectedRateStep = expectedStep * rateDivisors[rateTest];
        std::int64_t rateBlockStart = 0;
        while (rateBlockStart < expectedRateStep * 5 + blockSize)
        {
            juce::MidiBuffer midi;
            rateProcessor.processBlock (audio, midi);
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn() && metadata.getMessage().getChannel() == 1)
                    rateNoteOns.push_back (rateBlockStart + metadata.samplePosition);
            rateBlockStart += blockSize;
        }

        REQUIRE (rateNoteOns.size() >= 5);
        for (std::size_t note = 1; note < rateNoteOns.size(); ++note)
            REQUIRE (std::abs ((rateNoteOns[note] - rateNoteOns[note - 1]) - expectedRateStep) <= 1);
    }

    std::cout << "PFS processor Random timing tests passed\n";
    return 0;
}
