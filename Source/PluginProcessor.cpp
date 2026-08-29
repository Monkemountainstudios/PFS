#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
constexpr auto stateType = "PFSState";

std::unique_ptr<juce::RangedAudioParameter> makeFloat (
    const char* id, const char* name, float min, float max, float step, float initial)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> { min, max, step }, initial);
}
}

PFSAudioProcessor::PFSAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, createParameterLayout())
{
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < pfs::nodeCount; ++i)
        {
            tracks[t].nodes[i].midi = pfs::rootMidi;
            // The origin is the guaranteed downbeat of each traversal.
            const auto active = i == 0;
            tracks[t].nodes[i].active = active;
            nodeActive[t][i].store (active);
            nodeMidi[t][i].store (pfs::rootMidi);
        }
    tempoParam = parameters.getRawParameterValue ("tempo"); swingParam = parameters.getRawParameterValue ("swing");
    ratchetProbabilityParam = parameters.getRawParameterValue ("ratchetProb"); ratchetRepeatsParam = parameters.getRawParameterValue ("ratchetRepeats");
    ratchetFadeParam = parameters.getRawParameterValue ("ratchetFade"); fuapParam = parameters.getRawParameterValue ("fuap");
    internalPlayParam = parameters.getRawParameterValue ("internalPlay");
    midiOutParam = parameters.getRawParameterValue ("midiOut");
    for (int t = 0; t < 2; ++t)
    {
        auto& r = trackParams[t];
        r.sample = parameters.getRawParameterValue (trackParameter (t, "Sample")); r.filter = parameters.getRawParameterValue (trackParameter (t, "Filter"));
        r.gate = parameters.getRawParameterValue (trackParameter (t, "Gate")); r.volume = parameters.getRawParameterValue (trackParameter (t, "Volume"));
        r.pan = parameters.getRawParameterValue (trackParameter (t, "Pan")); r.reverb = parameters.getRawParameterValue (trackParameter (t, "Reverb"));
        r.mute = parameters.getRawParameterValue (trackParameter (t, "Mute")); r.rate = parameters.getRawParameterValue (trackParameter (t, "Rate"));
        r.variation = parameters.getRawParameterValue (trackParameter (t, "Variation")); r.ratchet = parameters.getRawParameterValue (trackParameter (t, "Ratchet"));
    }
    loadEmbeddedSamples();
}

juce::AudioProcessorValueTreeState::ParameterLayout PFSAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeFloat ("tempo", "Internal Tempo", 20.0f, 240.0f, 1.0f, 90.0f));
    p.push_back (makeFloat ("swing", "Swing", 50.0f, 75.0f, 0.1f, 50.0f));
    p.push_back (makeFloat ("ratchetProb", "Ratchet Probability", 0.0f, 100.0f, 1.0f, 12.0f));
    p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "ratchetRepeats", 1 }, "Ratchet Repeats", 2, 4, 3));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ratchetFade", 1 }, "Ratchet Fade", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "fuap", 1 }, "FUAP", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "internalPlay", 1 }, "Internal Play", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "midiOut", 1 }, "MIDI Output", false));

    for (int t = 0; t < 2; ++t)
    {
        const auto prefix = "track" + juce::String (t + 1);
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { prefix + "Sample", 1 }, "Track Sample", juce::StringArray { "1", "2", "3", "4", "5" }, 0));
        p.push_back (makeFloat ((prefix + "Filter").toRawUTF8(), "Track Filter", 200.0f, 20000.0f, 1.0f, 20000.0f));
        p.push_back (makeFloat ((prefix + "Gate").toRawUTF8(), "Track Gate", 5.0f, 100.0f, 1.0f, 75.0f));
        p.push_back (makeFloat ((prefix + "Volume").toRawUTF8(), "Track Volume", 0.0f, 120.0f, 0.1f, 100.0f));
        p.push_back (makeFloat ((prefix + "Pan").toRawUTF8(), "Track Pan", -100.0f, 100.0f, 0.1f, 0.0f));
        p.push_back (makeFloat ((prefix + "Reverb").toRawUTF8(), "Track Reverb", 0.0f, 100.0f, 0.1f, 8.0f));
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { prefix + "Mute", 1 }, "Track Mute", false));
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { prefix + "Rate", 1 }, "Track Rate", juce::StringArray { "1", "1/2", "1/4" }, 0));
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { prefix + "Variation", 1 }, "Track Variation", true));
        p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { prefix + "Ratchet", 1 }, "Track Ratchet Route", false));
    }
    return { p.begin(), p.end() };
}

void PFSAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    voices.reserve (128);
    pendingMidi.reserve (128);
    trackBuffer.setSize (2, maximumExpectedSamplesPerBlock, false, false, true);
    reverbBuffer.setSize (2, maximumExpectedSamplesPerBlock, false, false, true);
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maximumExpectedSamplesPerBlock), 1 };
    for (auto& pair : filters)
        for (auto& filter : pair)
        {
            filter.reset();
            filter.prepare (spec);
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.45f; rp.damping = 0.55f; rp.wetLevel = 0.38f; rp.dryLevel = 0.0f; rp.width = 1.0f;
    reverb.setParameters (rp);
    reverb.reset();
    processedSamples = 0;
    lastMidiClockSample = -1;
    externalSamplesPerPulse = 0.0;
    externalClockPulse = 0;
    externalClockRunning = false;
    externalClockPresent = false;
    wasSendingInternalClock = false;
    midiClockSamplesUntilPulse = 0.0;
    resetSequencer();
}

void PFSAudioProcessor::releaseResources() { voices.clear(); }

bool PFSAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PFSAudioProcessor::processBlock (juce::AudioBuffer<float>& output, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    runtimeDiagnostics.audioBlocks.fetch_add (1, std::memory_order_relaxed);
    output.clear();
    const auto numSamples = output.getNumSamples();
    if (numSamples == 0) return;
    currentBlockSize = numSamples;
    const auto midiOutEnabled = midiOutParam->load() >= 0.5f;

    bool hostPlaying = false;
    double bpm = tempoParam->load();
    double ppq = 0.0;
    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
        {
            hostPlaying = position->getIsPlaying();
            if (const auto hostBpm = position->getBpm()) bpm = *hostBpm;
            if (const auto hostPpq = position->getPpqPosition()) ppq = *hostPpq;
        }

    std::array<int, 128> clockOffsets {};
    int numClockOffsets = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isMidiStart())
        {
            externalClockRunning = true;
            externalClockPresent = true;
            externalClockPulse = 0;
            resetSequencer();
        }
        else if (message.isMidiContinue())
        {
            externalClockRunning = true;
            externalClockPresent = true;
        }
        else if (message.isMidiStop())
        {
            externalClockRunning = false;
            externalClockPresent = true;
        }
        else if (message.isMidiClock())
        {
            const auto absoluteSample = processedSamples + metadata.samplePosition;
            if (lastMidiClockSample >= 0)
            {
                const auto interval = static_cast<double> (absoluteSample - lastMidiClockSample);
                if (interval > 0.0 && interval < currentSampleRate)
                    externalSamplesPerPulse = externalSamplesPerPulse <= 0.0 ? interval
                                            : externalSamplesPerPulse * 0.85 + interval * 0.15;
            }
            lastMidiClockSample = absoluteSample;
            externalClockPresent = true;
            if (numClockOffsets < static_cast<int> (clockOffsets.size()))
                clockOffsets[static_cast<std::size_t> (numClockOffsets++)] = metadata.samplePosition;
        }
    }

    if (lastMidiClockSample >= 0 && processedSamples - lastMidiClockSample > static_cast<std::int64_t> (currentSampleRate))
        externalClockPresent = false;

    for (auto& event : pendingMidi)
    {
        // A changed node/pitch invalidates note-ons which have not sounded yet.  Keep
        // note-offs: dropping one after a transpose can leave external MIDI gear with
        // a hanging note.
        if (event.message.isNoteOn()
            && event.track >= 0
            && event.generation != trackGeneration[static_cast<std::size_t> (event.track)].load())
            event.samplesUntilEvent = -1;
        else if (event.samplesUntilEvent < numSamples)
        {
            midi.addEvent (event.message, juce::jmax (0, event.samplesUntilEvent));
            event.samplesUntilEvent = -1;
        }
        else
            event.samplesUntilEvent -= numSamples;
    }
    pendingMidi.erase (std::remove_if (pendingMidi.begin(), pendingMidi.end(),
                                      [] (const PendingMidi& event) { return event.samplesUntilEvent < 0; }),
                       pendingMidi.end());

    bpm = juce::jlimit (20.0, 300.0, bpm);
    const auto samplesPerStep = currentSampleRate * 60.0 / bpm / 4.0;

    if (hostPlaying)
    {
        clockStatus.store (1);
        if (wasSendingInternalClock) midi.addEvent (juce::MidiMessage::midiStop(), 0);
        const auto ppqPerSample = bpm / (60.0 * currentSampleRate);
        const auto endPpq = ppq + static_cast<double> (numSamples) * ppqPerSample;
        const auto unswungAtStart = static_cast<std::int64_t> (std::floor (ppq * 4.0)) - 1;
        if (! wasHostPlaying || unswungAtStart < lastHostStep - 2)
            resetSequencer();

        const auto swingAmount = (swingParam->load() - 50.0) / 25.0;
        const auto swingPpq = 0.25 * 0.45 * swingAmount;
        const auto finalCandidate = static_cast<std::int64_t> (std::ceil (endPpq * 4.0)) + 1;
        for (auto step = std::max<std::int64_t> (0, unswungAtStart); step <= finalCandidate; ++step)
        {
            const auto eventPpq = step * 0.25 + ((step & 1) != 0 ? swingPpq : 0.0);
            if (eventPpq >= ppq && eventPpq < endPpq && step > lastHostStep)
            {
                const auto offset = juce::jlimit (0, numSamples - 1, static_cast<int> (std::llround ((eventPpq - ppq) / ppqPerSample)));
                scheduleBaseStep (offset, samplesPerStep, midi);
                lastHostStep = step;
            }
        }
    }
    else if (externalClockPresent)
    {
        clockStatus.store (externalClockRunning ? 3 : 2);
        if (externalClockRunning)
            for (int i = 0; i < numClockOffsets; ++i)
            {
                if ((externalClockPulse % 6) == 0)
                {
                    const auto externalStep = externalSamplesPerPulse > 0.0 ? externalSamplesPerPulse * 6.0 : samplesPerStep;
                    scheduleBaseStep (clockOffsets[static_cast<std::size_t> (i)], externalStep, midi);
                }
                ++externalClockPulse;
            }
    }
    else if (internalPlayParam->load() >= 0.5f)
    {
        clockStatus.store (0);
        if (wasHostPlaying) resetSequencer();
        while (internalSamplesUntilStep < numSamples)
        {
            auto offset = juce::jmax (0, static_cast<int> (std::llround (internalSamplesUntilStep)));
            if ((internalStep & 1) != 0)
                offset += static_cast<int> (samplesPerStep * 0.45 * ((swingParam->load() - 50.0f) / 25.0f));
            scheduleBaseStep (offset, samplesPerStep, midi);
            internalSamplesUntilStep += samplesPerStep;
            ++internalStep;
        }
        internalSamplesUntilStep -= numSamples;

        if (midiOutEnabled && ! wasSendingInternalClock)
        {
            midi.addEvent (juce::MidiMessage::midiStart(), 0);
            midiClockSamplesUntilPulse = 0.0;
        }
        if (midiOutEnabled)
        {
            const auto samplesPerPulse = samplesPerStep / 6.0;
            while (midiClockSamplesUntilPulse < numSamples)
            {
                midi.addEvent (juce::MidiMessage::midiClock(), juce::jmax (0, static_cast<int> (std::llround (midiClockSamplesUntilPulse))));
                midiClockSamplesUntilPulse += samplesPerPulse;
            }
            midiClockSamplesUntilPulse -= numSamples;
        }
        wasSendingInternalClock = midiOutEnabled;
    }
    else
    {
        clockStatus.store (0);
        if (wasHostPlaying) resetSequencer();
        if (wasSendingInternalClock) midi.addEvent (juce::MidiMessage::midiStop(), 0);
        wasSendingInternalClock = false;
    }
    if (hostPlaying || externalClockPresent) wasSendingInternalClock = false;
    wasHostPlaying = hostPlaying;
    processedSamples += numSamples;

    renderVoices (output);

    // A saved JUCE standalone device may silently reopen a MIDI output from a
    // previous session.  PFS owns an explicit output switch so internal audio
    // can never be echoed externally unless the user asks for it.
    if (! midiOutEnabled)
    {
        midi.clear();
        pendingMidi.clear();
        if (wasMidiOutEnabled)
        {
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            midi.addEvent (juce::MidiMessage::allNotesOff (2), 0);
            midi.addEvent (juce::MidiMessage::midiStop(), 0);
        }
        wasSendingInternalClock = false;
    }
    wasMidiOutEnabled = midiOutEnabled;
}

void PFSAudioProcessor::scheduleBaseStep (int sampleOffset, double samplesPerStep, juce::MidiBuffer& midi)
{
    const auto baseStep = baseStepCounter++;
    runtimeDiagnostics.masterSteps.fetch_add (1, std::memory_order_relaxed);
    if ((baseStep % 4) == 0)
        quarterPulse.fetch_add (1);

    std::array<pfs::StepResult, 2> branchResults;
    std::array<bool, 2> stepDue { false, false };
    std::array<int, 2> divisors { 1, 1 };

    // Odd while the two-track visual state is being written, even when the
    // complete pair is ready.  The editor can therefore never combine a
    // Track 1 branch from one tick with a Track 2 branch from another tick.
    visualWriteSequence.fetch_add (1, std::memory_order_acq_rel);

    {
        // The lock protects traversal state only.  Playback, MIDI insertion and
        // voice allocation happen after it is released, so neither Static nor
        // Random can hold a shared lock in the timing-critical render work.
        const juce::SpinLock::ScopedLockType lock (stateLock);
        for (int t = 0; t < 2; ++t)
        {
            auto& track = tracks[static_cast<std::size_t> (t)];
            const auto& parameter = trackParams[t];
            const auto rateChoice = static_cast<int> (parameter.rate->load());
            track.rateDivisor = rateChoice == 0 ? 1 : rateChoice == 1 ? 2 : 4;
            divisors[static_cast<std::size_t> (t)] = track.rateDivisor;
            track.variation = parameter.variation->load() >= 0.5f;

            // One global tick services both tracks.  Rate can divide that tick,
            // but no track owns or reschedules a clock of its own.
            if ((baseStep % track.rateDivisor) != 0)
            {
                branchFrom[t].store (-1);
                branchTo[t].store (-1);
                continue;
            }

            // Phase is owned by the shared master clock, never by a track.
            // Deriving the row from the global step makes it impossible for
            // two tracks at the same rate to drift or take turns after any
            // sequence of Static/Random changes.  nodeIndex remains per-track
            // because each track still makes its own left/right choices.
            const auto trackStep = baseStep / track.rateDivisor;
            const auto clockLevel = static_cast<int> (trackStep % pfs::levels);
            track.level = clockLevel;
            if (clockLevel == 0)
                track.nodeIndex = 0;

            auto result = engine.step (track);

            // Traversal always consumes exactly one node.  Active controls only
            // whether that already-consumed timing event produces sound.
            result.active = nodeActive[t][result.flatIndex].load();
            result.midi = nodeMidi[t][result.flatIndex].load();
            playheadNodes[static_cast<std::size_t> (t)].store (result.flatIndex);
            branchFrom[t].store (result.branchFrom);
            branchTo[t].store (result.branchTo);
            branchSerial[t].fetch_add (1);
            branchResults[static_cast<std::size_t> (t)] = result;
            stepDue[static_cast<std::size_t> (t)] = true;
            runtimeDiagnostics.visits[static_cast<std::size_t> (t)].fetch_add (1, std::memory_order_relaxed);
            if (result.flatIndex == 0)
                runtimeDiagnostics.originVisits[static_cast<std::size_t> (t)].fetch_add (1, std::memory_order_relaxed);
        }
    }

    // Publish the visual step only after both track snapshots are complete.
    // The UI will therefore always see a coherent pair of branches.
    treeStepSerial.fetch_add (1, std::memory_order_release);
    visualWriteSequence.fetch_add (1, std::memory_order_release);

    // Diagnostic isolation build: traversal still produces exactly the same
    // node events, but ratcheting is bypassed completely. Each audible node is
    // forced to one subtrigger and reported through a lock-free trace queue.
    constexpr bool ratchetBypassed = true;
    const auto ratchetProbability = ratchetProbabilityParam->load();
    const auto configuredDepth = juce::jlimit (2, 4, static_cast<int> (std::lround (ratchetRepeatsParam->load())));

    const auto emit = [this, sampleOffset, samplesPerStep, &midi, &divisors,
                       ratchetProbability, configuredDepth, ratchetBypassed]
                      (int t, const pfs::StepResult& note)
    {
        SchedulerTrace trace;
        trace.nodeEvent = schedulerTraceSerial.fetch_add (1, std::memory_order_relaxed) + 1;
        trace.track = t;
        trace.level = note.level;
        trace.index = note.index;
        trace.flatIndex = note.flatIndex;
        trace.active = note.active;
        // "visible" means this visit maps to one of the node buttons owned by
        // the shared five-level/31-node UI topology. A deliberately hidden
        // whole track still has the corresponding node and is therefore true.
        trace.visible = pfs::SequencerEngine::hasVisibleNode (note.level, note.index, note.flatIndex);
        trace.muted = trackParams[t].mute->load() >= 0.5f;
        trace.triggerRequested = note.active && ! trace.muted;
        trace.ratchetProbability = ratchetProbability;
        trace.configuredDepth = configuredDepth;
        trace.routed = trackParams[t].ratchet->load() >= 0.5f;
        trace.ratchetBypassed = ratchetBypassed;

        if (trace.triggerRequested)
        {
            runtimeDiagnostics.triggerRequests[static_cast<std::size_t> (t)].fetch_add (1, std::memory_order_relaxed);

            constexpr int count = 1;
            const auto rateSamples = samplesPerStep * divisors[static_cast<std::size_t> (t)];
            const auto gate = trackParams[t].gate->load() / 100.0;
            trace.subtriggerCount = count;
            for (int r = 0; r < count; ++r)
            {
                const auto offset = sampleOffset + static_cast<int> (rateSamples * r / count);
                trace.sampleOffsets[static_cast<std::size_t> (r)] = offset;
                const auto duration = std::max (16, static_cast<int> (rateSamples * gate / count));
                const auto velocity = ratchetFadeParam->load() >= 0.5f ? std::pow (0.8f, static_cast<float> (r)) : 1.0f;
                trigger (t, note, offset, duration, velocity, midi);
            }
        }

        pushSchedulerTrace (trace);
    };

    for (int t = 0; t < 2; ++t)
        if (stepDue[static_cast<std::size_t> (t)])
            emit (t, branchResults[static_cast<std::size_t> (t)]);
}

void PFSAudioProcessor::trigger (int track, const pfs::StepResult& step, int sampleOffset, int duration,
                                 float velocity, juce::MidiBuffer& midi)
{
    const auto fuap = fuapParam->load() >= 0.5f;
    const auto selected = juce::jlimit (0, 4, static_cast<int> (trackParams[track].sample->load()));
    const auto& sample = samples[static_cast<std::size_t> (fuap ? 5 : selected)];
    if (sample.audio.getNumSamples() == 0) return;
    runtimeDiagnostics.voicesCreated[static_cast<std::size_t> (track)].fetch_add (1, std::memory_order_relaxed);
    const auto pitch = std::pow (2.0, (step.midi - pfs::rootMidi) / 12.0);
    const auto increment = pitch * sample.sourceRate / currentSampleRate;
    const auto naturalDuration = static_cast<int> (sample.audio.getNumSamples() / increment);
    const auto generation = trackGeneration[static_cast<std::size_t> (track)].load();
    Voice voice { &sample, track, 0.0, increment, 0,
                  fuap ? naturalDuration : std::min (duration, naturalDuration), velocity, generation };
    if (voices.size() >= 128)
        voices.erase (voices.begin());
    voices.push_back (voice);

    const auto midiOffset = juce::jmax (0, sampleOffset);
    const auto addOrDefer = [this, &midi, track, generation] (juce::MidiMessage message, int offset)
    {
        if (offset < currentBlockSize)
            midi.addEvent (message, offset);
        else
            pendingMidi.push_back ({ message, offset - currentBlockSize, track, generation });
    };
    if (midiOutParam->load() >= 0.5f)
    {
        addOrDefer (juce::MidiMessage::noteOn (track + 1, step.midi, velocity), midiOffset);
        addOrDefer (juce::MidiMessage::noteOff (track + 1, step.midi), midiOffset + juce::jmax (1, voice.duration));
    }
    voices.back().age = -sampleOffset;
}

void PFSAudioProcessor::renderVoices (juce::AudioBuffer<float>& output)
{
    const auto n = output.getNumSamples();
    reverbBuffer.setSize (2, n, false, false, true);
    reverbBuffer.clear();

    for (int track = 0; track < 2; ++track)
    {
        trackBuffer.setSize (2, n, false, false, true);
        trackBuffer.clear();
        for (auto& voice : voices)
        {
            if (voice.track != track || voice.sample == nullptr) continue;
            if (voice.generation != trackGeneration[static_cast<std::size_t> (track)].load())
            {
                if (voice.age < 0)
                {
                    voice.age = voice.duration;
                    continue;
                }

                // Do not let a long old-pitch sample obscure the edit.  Give an
                // already audible voice a tiny ramp to silence to avoid a click.
                const auto editFade = juce::jmax (16, static_cast<int> (0.005 * currentSampleRate));
                voice.duration = juce::jmin (voice.duration, voice.age + editFade);
            }
            const auto& source = voice.sample->audio;
            for (int i = 0; i < n; ++i)
            {
                const auto age = voice.age + i;
                if (age < 0 || age >= voice.duration) continue;
                const auto pos = voice.position + age * voice.increment;
                const auto i0 = static_cast<int> (pos);
                if (i0 >= source.getNumSamples() - 1) continue;
                const auto frac = static_cast<float> (pos - i0);
                const auto fadeSamples = juce::jmax (16, static_cast<int> (0.012 * currentSampleRate));
                const auto envelope = age > voice.duration - fadeSamples
                                    ? juce::jlimit (0.0f, 1.0f, static_cast<float> (voice.duration - age) / fadeSamples) : 1.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto sourceCh = juce::jmin (ch, source.getNumChannels() - 1);
                    const auto* data = source.getReadPointer (sourceCh);
                    const auto sample = data[i0] + frac * (data[i0 + 1] - data[i0]);
                    trackBuffer.addSample (ch, i, sample * voice.velocity * envelope);
                }
            }
            voice.age += n;
        }

        const auto& parameter = trackParams[track];
        const auto cutoff = parameter.filter->load();
        float filteredPeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            filters[static_cast<std::size_t> (track)][static_cast<std::size_t> (ch)].setCutoffFrequency (cutoff);
            auto* data = trackBuffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                data[i] = filters[track][ch].processSample (0, data[i]);
                filteredPeak = juce::jmax (filteredPeak, std::abs (data[i]));
            }
        }
        if (filteredPeak > 0.000001f)
            runtimeDiagnostics.nonSilentBlocks[static_cast<std::size_t> (track)].fetch_add (1, std::memory_order_relaxed);

        const auto volume = parameter.volume->load() / 100.0f;
        const auto pan = parameter.pan->load() / 100.0f;
        const auto left = volume * std::sqrt (0.5f * (1.0f - pan));
        const auto right = volume * std::sqrt (0.5f * (1.0f + pan));
        const auto send = parameter.reverb->load() / 100.0f;
        if (output.getNumChannels() == 1)
        {
            output.addFrom (0, 0, trackBuffer, 0, 0, n, left * 0.5f);
            output.addFrom (0, 0, trackBuffer, 1, 0, n, right * 0.5f);
        }
        else
        {
            output.addFrom (0, 0, trackBuffer, 0, 0, n, left);
            output.addFrom (1, 0, trackBuffer, 1, 0, n, right);
        }
        reverbBuffer.addFrom (0, 0, trackBuffer, 0, 0, n, left * send);
        reverbBuffer.addFrom (1, 0, trackBuffer, 1, 0, n, right * send);
    }

    voices.erase (std::remove_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.age >= v.duration; }), voices.end());
    reverb.processStereo (reverbBuffer.getWritePointer (0), reverbBuffer.getWritePointer (1), n);
    if (output.getNumChannels() == 1)
    {
        output.addFrom (0, 0, reverbBuffer, 0, 0, n, 0.5f);
        output.addFrom (0, 0, reverbBuffer, 1, 0, n, 0.5f);
    }
    else
    {
        output.addFrom (0, 0, reverbBuffer, 0, 0, n);
        output.addFrom (1, 0, reverbBuffer, 1, 0, n);
    }
    output.applyGain (0.72f);
}

void PFSAudioProcessor::loadEmbeddedSamples()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::array<const void*, 6> data { BinaryData::sound1_ogg, BinaryData::sound2_ogg, BinaryData::sound3_ogg,
                                            BinaryData::sound4_ogg, BinaryData::sound5_ogg, BinaryData::fuap_ogg };
    const std::array<int, 6> sizes { BinaryData::sound1_oggSize, BinaryData::sound2_oggSize, BinaryData::sound3_oggSize,
                                     BinaryData::sound4_oggSize, BinaryData::sound5_oggSize, BinaryData::fuap_oggSize };
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        auto stream = std::make_unique<juce::MemoryInputStream> (data[i], static_cast<std::size_t> (sizes[i]), false);
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (std::move (stream))))
        {
            samples[i].audio.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
            reader->read (&samples[i].audio, 0, samples[i].audio.getNumSamples(), 0, true, true);
            samples[i].sourceRate = reader->sampleRate;
        }
    }
}

juce::String PFSAudioProcessor::trackParameter (int track, const char* suffix)
{
    return "track" + juce::String (track + 1) + suffix;
}

pfs::Node PFSAudioProcessor::getNode (int track, int flatIndex) const
{
    track = juce::jlimit (0, 1, track); flatIndex = juce::jlimit (0, pfs::nodeCount - 1, flatIndex);
    return { nodeActive[track][flatIndex].load(), nodeMidi[track][flatIndex].load() };
}

void PFSAudioProcessor::setNodeActive (int track, int flatIndex, bool active)
{
    if (! juce::isPositiveAndBelow (track, 2) || ! juce::isPositiveAndBelow (flatIndex, pfs::nodeCount)) return;
    if (nodeActive[track][flatIndex].exchange (active) != active) trackGeneration[track].fetch_add (1);
}

void PFSAudioProcessor::setNodeMidi (int track, int flatIndex, int midi)
{
    if (! juce::isPositiveAndBelow (track, 2) || ! juce::isPositiveAndBelow (flatIndex, pfs::nodeCount)) return;
    const auto pitch = juce::jlimit (36, 84, midi);
    if (nodeMidi[track][flatIndex].exchange (pitch) != pitch) trackGeneration[track].fetch_add (1);
}

void PFSAudioProcessor::transposeTrack (int trackIndex, int semitones)
{
    if (! juce::isPositiveAndBelow (trackIndex, 2)) return;
    for (auto& midi : nodeMidi[static_cast<std::size_t> (trackIndex)])
        midi.store (juce::jlimit (36, 84, midi.load() + semitones));
    trackGeneration[trackIndex].fetch_add (1);
}

bool PFSAudioProcessor::trackHasActiveNodes (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, 2)) return false;
    const auto& nodes = nodeActive[static_cast<std::size_t> (trackIndex)];
    return std::any_of (nodes.begin(), nodes.end(), [] (const auto& node) { return node.load(); });
}

PFSAudioProcessor::DiagnosticSnapshot PFSAudioProcessor::getDiagnosticSnapshot() const noexcept
{
    DiagnosticSnapshot result;
    result.audioBlocks = runtimeDiagnostics.audioBlocks.load (std::memory_order_relaxed);
    result.masterSteps = runtimeDiagnostics.masterSteps.load (std::memory_order_relaxed);
    result.clock = clockStatus.load (std::memory_order_relaxed);
    result.internalPlay = internalPlayParam->load() >= 0.5f;
    result.midiOut = midiOutParam->load() >= 0.5f;
    for (int t = 0; t < 2; ++t)
    {
        const auto index = static_cast<std::size_t> (t);
        result.visits[index] = runtimeDiagnostics.visits[index].load (std::memory_order_relaxed);
        result.originVisits[index] = runtimeDiagnostics.originVisits[index].load (std::memory_order_relaxed);
        result.triggerRequests[index] = runtimeDiagnostics.triggerRequests[index].load (std::memory_order_relaxed);
        result.voicesCreated[index] = runtimeDiagnostics.voicesCreated[index].load (std::memory_order_relaxed);
        result.nonSilentBlocks[index] = runtimeDiagnostics.nonSilentBlocks[index].load (std::memory_order_relaxed);
        result.playheads[index] = playheadNodes[index].load (std::memory_order_relaxed);
        const auto rateChoice = static_cast<int> (trackParams[t].rate->load());
        result.rates[index] = rateChoice == 0 ? 1 : rateChoice == 1 ? 2 : 4;
        result.random[index] = trackParams[t].variation->load() >= 0.5f;
        result.muted[index] = trackParams[t].mute->load() >= 0.5f;
        result.rootActive[index] = nodeActive[index][0].load (std::memory_order_relaxed);
    }
    return result;
}

void PFSAudioProcessor::pushSchedulerTrace (const SchedulerTrace& trace) noexcept
{
    const auto write = schedulerTraceWrite.load (std::memory_order_relaxed);
    const auto next = (write + 1u) % schedulerTraceCapacity;
    if (next == schedulerTraceRead.load (std::memory_order_acquire))
        return;

    schedulerTraceRing[write] = trace;
    schedulerTraceWrite.store (next, std::memory_order_release);
}

bool PFSAudioProcessor::popSchedulerTrace (SchedulerTrace& trace) noexcept
{
    const auto read = schedulerTraceRead.load (std::memory_order_relaxed);
    if (read == schedulerTraceWrite.load (std::memory_order_acquire))
        return false;

    trace = schedulerTraceRing[read];
    schedulerTraceRead.store ((read + 1u) % schedulerTraceCapacity, std::memory_order_release);
    return true;
}

PFSAudioProcessor::VisualStepSnapshot PFSAudioProcessor::getVisualStepSnapshot() const noexcept
{
    VisualStepSnapshot result;
    for (;;)
    {
        const auto before = visualWriteSequence.load (std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;

        result.serial = treeStepSerial.load (std::memory_order_acquire);
        for (int t = 0; t < 2; ++t)
        {
            const auto index = static_cast<std::size_t> (t);
            result.branchFrom[index] = branchFrom[index].load (std::memory_order_relaxed);
            result.branchTo[index] = branchTo[index].load (std::memory_order_relaxed);
            result.playheads[index] = playheadNodes[index].load (std::memory_order_relaxed);
        }

        const auto after = visualWriteSequence.load (std::memory_order_acquire);
        if (before == after)
            return result;
    }
}

void PFSAudioProcessor::resetSequencer()
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    for (auto& track : tracks) engine.reset (track);
    for (auto& node : playheadNodes) node.store (-1);
    lastHostStep = -1;
    internalSamplesUntilStep = 0.0;
    internalStep = 0;
    baseStepCounter = 0;
}

void PFSAudioProcessor::copyNodesToState()
{
    if (const auto existing = parameters.state.getChildWithName ("Nodes"); existing.isValid())
        parameters.state.removeChild (existing, nullptr);
    juce::ValueTree nodeState ("Nodes");
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < pfs::nodeCount; ++i)
        {
            juce::ValueTree item ("Node");
            item.setProperty ("track", t, nullptr); item.setProperty ("index", i, nullptr);
            item.setProperty ("active", nodeActive[t][i].load(), nullptr); item.setProperty ("midi", nodeMidi[t][i].load(), nullptr);
            nodeState.addChild (item, -1, nullptr);
        }
    parameters.state.addChild (nodeState, -1, nullptr);
}

void PFSAudioProcessor::restoreNodesFromState()
{
    const auto nodeState = parameters.state.getChildWithName ("Nodes");
    if (! nodeState.isValid()) return;
    for (const auto item : nodeState)
    {
        const int t = item["track"], i = item["index"];
        if (juce::isPositiveAndBelow (t, 2) && juce::isPositiveAndBelow (i, pfs::nodeCount))
        {
            nodeActive[t][i].store (static_cast<bool> (item["active"]));
            nodeMidi[t][i].store (juce::jlimit (36, 84, static_cast<int> (item["midi"])));
        }
    }
}

void PFSAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    copyNodesToState();
    if (const auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, destination);
}

void PFSAudioProcessor::setStateInformation (const void* data, int size)
{
    // The standalone is an instrument, not a DAW project.  JUCE stores its
    // filterState beside the audio-device choice, which caused every test run
    // to resurrect old transpositions, rates and patterns.  Keep the audio
    // setup (owned by the standalone wrapper), but always use the constructor's
    // clean C4/default state for the instrument itself.  Plugin wrappers still
    // restore their project state normally.
    if (wrapperType == wrapperType_Standalone)
    {
        resetSequencer();
        return;
    }

    if (const auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            restoreNodesFromState();
            resetSequencer();
        }
}

juce::AudioProcessorEditor* PFSAudioProcessor::createEditor() { return new PFSAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PFSAudioProcessor(); }
