#pragma once

#include <JuceHeader.h>
#include "SequencerEngine.h"

class PFSAudioProcessor final : public juce::AudioProcessor
{
public:
    struct DiagnosticSnapshot
    {
        std::uint64_t audioBlocks = 0;
        std::uint64_t masterSteps = 0;
        std::array<std::uint64_t, 2> visits {};
        std::array<std::uint64_t, 2> originVisits {};
        std::array<std::uint64_t, 2> triggerRequests {};
        std::array<std::uint64_t, 2> voicesCreated {};
        std::array<std::uint64_t, 2> nonSilentBlocks {};
        std::array<int, 2> playheads { -1, -1 };
        std::array<int, 2> rates { 1, 1 };
        std::array<bool, 2> random {};
        std::array<bool, 2> muted {};
        std::array<bool, 2> rootActive {};
        int clock = 0;
        bool internalPlay = false;
        bool midiOut = false;
    };

    struct VisualStepSnapshot
    {
        std::uint64_t serial = 0;
        std::array<int, 2> branchFrom { -1, -1 };
        std::array<int, 2> branchTo { -1, -1 };
        std::array<int, 2> playheads { -1, -1 };
    };

    struct SchedulerTrace
    {
        std::uint64_t nodeEvent = 0;
        int track = 0;
        int level = -1;
        int index = -1;
        int flatIndex = -1;
        bool active = false;
        bool visible = false;
        bool muted = false;
        bool triggerRequested = false;
        float ratchetProbability = 0.0f;
        int configuredDepth = 1;
        bool routed = false;
        bool ratchetBypassed = true;
        int subtriggerCount = 0;
        std::array<int, 4> sampleOffsets { -1, -1, -1, -1 };
    };

    PFSAudioProcessor();
    ~PFSAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;

    pfs::Node getNode (int track, int flatIndex) const;
    void setNodeActive (int track, int flatIndex, bool active);
    void setNodeMidi (int track, int flatIndex, int midi);
    void transposeTrack (int track, int semitones);
    int getPlayheadNode (int track) const noexcept { return playheadNodes[track].load(); }
    int getClockStatus() const noexcept { return clockStatus.load(); }
    std::uint64_t getQuarterPulse() const noexcept { return quarterPulse.load(); }
    std::uint64_t getTreeStepSerial() const noexcept { return treeStepSerial.load(); }
    std::uint64_t getBranchSerial (int track) const noexcept { return branchSerial[track].load(); }
    std::pair<int, int> getLastBranch (int track) const noexcept { return { branchFrom[track].load(), branchTo[track].load() }; }
    VisualStepSnapshot getVisualStepSnapshot() const noexcept;
    bool trackHasActiveNodes (int track) const;
    DiagnosticSnapshot getDiagnosticSnapshot() const noexcept;
    bool popSchedulerTrace (SchedulerTrace&) noexcept;
    void resetSequencer();

private:
    struct RuntimeDiagnostics
    {
        std::atomic<std::uint64_t> audioBlocks { 0 };
        std::atomic<std::uint64_t> masterSteps { 0 };
        std::array<std::atomic<std::uint64_t>, 2> visits { 0u, 0u };
        std::array<std::atomic<std::uint64_t>, 2> originVisits { 0u, 0u };
        std::array<std::atomic<std::uint64_t>, 2> triggerRequests { 0u, 0u };
        std::array<std::atomic<std::uint64_t>, 2> voicesCreated { 0u, 0u };
        std::array<std::atomic<std::uint64_t>, 2> nonSilentBlocks { 0u, 0u };
    } runtimeDiagnostics;

    struct SampleData { juce::AudioBuffer<float> audio; double sourceRate = 44100.0; };
    struct Voice
    {
        const SampleData* sample = nullptr;
        int track = 0;
        double position = 0.0;
        double increment = 1.0;
        int age = 0;
        int duration = 0;
        float velocity = 1.0f;
        std::uint32_t generation = 0;
    };
    struct PendingMidi
    {
        juce::MidiMessage message;
        int samplesUntilEvent = 0;
        int track = -1;
        std::uint32_t generation = 0;
    };

    void loadEmbeddedSamples();
    void scheduleBaseStep (int sampleOffset, double samplesPerStep, juce::MidiBuffer& midi);
    void trigger (int track, const pfs::StepResult&, int sampleOffset, int duration,
                  float velocity, juce::MidiBuffer& midi);
    void renderVoices (juce::AudioBuffer<float>& output);
    void pushSchedulerTrace (const SchedulerTrace&) noexcept;
    void copyNodesToState();
    void restoreNodesFromState();
    static juce::String trackParameter (int track, const char* suffix);

    struct TrackParameterRefs
    {
        std::atomic<float>* sample = nullptr; std::atomic<float>* filter = nullptr;
        std::atomic<float>* gate = nullptr; std::atomic<float>* volume = nullptr;
        std::atomic<float>* pan = nullptr; std::atomic<float>* reverb = nullptr;
        std::atomic<float>* mute = nullptr; std::atomic<float>* rate = nullptr;
        std::atomic<float>* variation = nullptr; std::atomic<float>* ratchet = nullptr;
    };

    std::array<pfs::TrackState, 2> tracks;
    pfs::SequencerEngine engine;
    mutable juce::SpinLock stateLock;
    std::array<std::array<std::atomic<bool>, pfs::nodeCount>, 2> nodeActive;
    std::array<std::array<std::atomic<int>, pfs::nodeCount>, 2> nodeMidi;
    std::array<std::atomic<std::uint32_t>, 2> trackGeneration { 0u, 0u };
    std::array<std::atomic<int>, 2> playheadNodes { -1, -1 };
    std::array<std::atomic<int>, 2> branchFrom { -1, -1 };
    std::array<std::atomic<int>, 2> branchTo { -1, -1 };
    std::array<std::atomic<std::uint64_t>, 2> branchSerial { 0u, 0u };
    std::atomic<std::uint64_t> quarterPulse { 0 };
    std::atomic<std::uint64_t> treeStepSerial { 0 };
    std::atomic<std::uint64_t> visualWriteSequence { 0 };
    std::array<SampleData, 6> samples;
    std::vector<Voice> voices;
    std::vector<PendingMidi> pendingMidi;
    std::atomic<float>* tempoParam = nullptr; std::atomic<float>* swingParam = nullptr;
    std::atomic<float>* ratchetProbabilityParam = nullptr; std::atomic<float>* ratchetRepeatsParam = nullptr;
    std::atomic<float>* ratchetFadeParam = nullptr; std::atomic<float>* fuapParam = nullptr;
    std::atomic<float>* internalPlayParam = nullptr; std::atomic<float>* midiOutParam = nullptr;
    std::array<TrackParameterRefs, 2> trackParams;

    std::array<std::array<juce::dsp::StateVariableTPTFilter<float>, 2>, 2> filters;
    juce::Reverb reverb;
    juce::AudioBuffer<float> trackBuffer;
    juce::AudioBuffer<float> reverbBuffer;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 0;
    double internalSamplesUntilStep = 0.0;
    std::int64_t internalStep = 0;
    std::int64_t baseStepCounter = 0;
    std::int64_t lastHostStep = -1;
    bool wasHostPlaying = false;
    bool wasSendingInternalClock = false;
    bool wasMidiOutEnabled = false;
    double midiClockSamplesUntilPulse = 0.0;
    std::int64_t processedSamples = 0;
    std::int64_t lastMidiClockSample = -1;
    double externalSamplesPerPulse = 0.0;
    int externalClockPulse = 0;
    bool externalClockRunning = false;
    bool externalClockPresent = false;
    std::atomic<int> clockStatus { 0 }; // 0 internal, 1 DAW, 2 MIDI stopped, 3 MIDI running
    juce::Random random;

    static constexpr std::uint32_t schedulerTraceCapacity = 256;
    std::array<SchedulerTrace, schedulerTraceCapacity> schedulerTraceRing {};
    std::atomic<std::uint32_t> schedulerTraceWrite { 0 };
    std::atomic<std::uint32_t> schedulerTraceRead { 0 };
    std::atomic<std::uint64_t> schedulerTraceSerial { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFSAudioProcessor)
};
