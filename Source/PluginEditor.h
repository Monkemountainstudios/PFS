#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class PFSAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit PFSAudioProcessorEditor (PFSAudioProcessor&);
    ~PFSAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PFSLookAndFeel;
    class NodeButton;
    class TreeView;
    class TrackStrip;

    void timerCallback() override;
    void configureKnob (juce::Slider&, const juce::String& suffix = {});
    void addSliderAttachment (const juce::String&, juce::Slider&);
    void addButtonAttachment (const juce::String&, juce::Button&);
    void setParameterIndex (const juce::String& id, int index);
    int parameterIndex (const juce::String& id) const;

    PFSAudioProcessor& processor;
    std::unique_ptr<PFSLookAndFeel> lookAndFeel;
    std::array<std::unique_ptr<TreeView>, 2> trees;
    std::array<std::unique_ptr<TrackStrip>, 2> strips;

    juce::Label title, tempoLabel, swingLabel, ratchetLabel, routeLabel, mixLabel, syncReadout;
    juce::Slider tempo, swing, ratchetProbability, ratchetRepeats;
    juce::TextButton clockButton { "CLOCK" }, internalPlay { "PLAY" }, fuap { "FUAP!" }, ratchetFade { "FADE" };
    std::array<juce::TextButton, 2> trackButtons, rateButtons, staticButtons, variationButtons, routeButtons;
    std::array<juce::Label, 2> transposeLabels;
    std::array<std::array<juce::TextButton, 4>, 2> transpose;
    std::array<bool, 2> treeVisible { true, true };
    std::array<int, 2> flashTicks { 0, 0 };
    std::uint64_t lastQuarterPulse = 0;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PFSAudioProcessorEditor)
};
