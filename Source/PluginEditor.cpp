#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff101112 };
const juce::Colour panel { 0xff181918 };
const juce::Colour edge { 0xff41413d };
const juce::Colour text { 0xffd8d0b4 };
const juce::Colour machineText { 0xff96958d };
const juce::Colour blue { 0xff5050d7 };
const juce::Colour amber { 0xffd2a315 };

std::uint64_t lastDiagnosticStep = 0;
juce::File diagnosticFile;
juce::String diagnosticLines;
double lastDiagnosticFlushMs = 0.0;

juce::String noteName (int midi)
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[midi % 12] + juce::String (midi / 12 - 1);
}

void setDiscreteParameter (juce::AudioProcessorValueTreeState& state, const juce::String& id, int value)
{
    if (auto* parameter = state.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (value)));
        parameter->endChangeGesture();
    }
}
}

class PFSAudioProcessorEditor::PFSLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PFSLookAndFeel()
    {
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::rotarySliderFillColourId, text.withAlpha (0.72f));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::black);
        setColour (juce::Slider::trackColourId, text.withAlpha (0.62f));
        setColour (juce::Slider::backgroundColourId, juce::Colours::black);
        setColour (juce::Slider::textBoxTextColourId, machineText);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff111213));
        setColour (juce::Slider::textBoxOutlineColourId, edge.withAlpha (0.85f));
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff24231d));
        setColour (juce::TextButton::buttonOnColourId, blue);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                               static_cast<float> (width), static_cast<float> (height)).reduced (8.0f);
        const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre (diameter, diameter);
        g.setColour (juce::Colours::black.withAlpha (0.86f));
        g.fillEllipse (bounds.translated (3.0f, 4.0f).expanded (2.0f));
        juce::ColourGradient face (juce::Colour (0xff3a3b3b), bounds.getX(), bounds.getY(),
                                   juce::Colour (0xff080909), bounds.getRight(), bounds.getBottom(), false);
        face.addColour (0.52, juce::Colour (0xff1c1d1d));
        g.setGradientFill (face); g.fillEllipse (bounds);
        g.setColour (juce::Colours::black); g.drawEllipse (bounds, 2.0f);
        g.setColour (edge); g.drawEllipse (bounds.reduced (3.0f), 1.0f);
        const auto angle = startAngle + position * (endAngle - startAngle);
        juce::Path marker;
        marker.addRoundedRectangle (-1.1f, -diameter * 0.35f, 2.2f, diameter * 0.25f, 1.0f);
        g.setColour (text.brighter (0.1f));
        g.fillPath (marker, juce::AffineTransform::rotation (angle).translated (bounds.getCentreX(), bounds.getCentreY()));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float min, float max, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, min, max, style, slider);
            return;
        }
        const auto centre = x + width * 0.5f;
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (centre - 3.0f, static_cast<float> (y + 3), 6.0f, static_cast<float> (height - 6), 2.0f);
        g.setColour (edge); g.drawVerticalLine (juce::roundToInt (centre), static_cast<float> (y + 5), static_cast<float> (y + height - 5));
        auto thumb = juce::Rectangle<float> (centre - 8.0f, sliderPos - 5.0f, 16.0f, 10.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f)); g.fillRect (thumb.translated (2.0f, 2.0f));
        juce::ColourGradient cap (juce::Colour (0xff555657), thumb.getX(), thumb.getY(),
                                  juce::Colour (0xff171819), thumb.getX(), thumb.getBottom(), false);
        g.setGradientFill (cap); g.fillRect (thumb);
        g.setColour (edge.brighter (0.25f)); g.drawRect (thumb, 1.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        auto fill = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId)
                                            : button.findColour (juce::TextButton::buttonColourId);
        if (button.getName() == "fuap") fill = button.getToggleState() ? juce::Colour (0xff7b151a) : juce::Colour (0xff391013);
        if (highlighted) fill = fill.brighter (0.12f);
        if (down) fill = fill.darker (0.18f);
        g.setColour (juce::Colours::black.withAlpha (0.82f)); g.fillRoundedRectangle (bounds.translated (2.0f, 3.0f).expanded (1.0f), 2.5f);
        if (button.getToggleState())
            for (int glow = 4; glow >= 1; --glow)
            {
                g.setColour (fill.withAlpha (0.035f * static_cast<float> (5 - glow)));
                g.drawRoundedRectangle (bounds.expanded (static_cast<float> (glow)), 3.0f, 2.0f);
            }
        juce::ColourGradient rubber (fill.brighter (0.26f), bounds.getX(), bounds.getY(),
                                     fill.darker (0.28f), bounds.getX(), bounds.getBottom(), false);
        rubber.addColour (0.48, fill);
        g.setGradientFill (rubber); g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (button.getToggleState() ? fill.brighter (0.65f) : edge); g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setColour (text);
        auto font = juce::Font (juce::FontOptions (button.getHeight() > 35 ? 13.0f : 10.0f, juce::Font::bold));
        font.setExtraKerningFactor (0.12f);
        g.setFont (font);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2), juce::Justification::centred, 1);
    }

    class ReadoutLabel final : public juce::Label
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff111213));
            g.setColour (edge.withAlpha (0.85f));
            g.drawRect (getLocalBounds(), 1);
            g.setColour (machineText);
            g.setFont (juce::Font (juce::FontOptions (6.25f)));
            g.drawFittedText (getText(), getLocalBounds().reduced (2, 1), juce::Justification::centred, 1, 0.72f);
        }
    };

    juce::Label* createSliderTextBox (juce::Slider&) override
    {
        auto* label = new ReadoutLabel();
        label->setJustificationType (juce::Justification::centred);
        label->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
        label->setColour (juce::TextEditor::textColourId, machineText);
        label->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111213));
        label->setColour (juce::TextEditor::outlineColourId, edge.withAlpha (0.85f));
        return label;
    }
};

class PFSAudioProcessorEditor::NodeButton final : public juce::TextButton
{
public:
    std::function<void (int)> onWheel;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (onWheel) onWheel (wheel.deltaY > 0.0f ? 1 : -1);
    }
};

class PFSAudioProcessorEditor::TreeView final : public juce::Component
{
public:
    TreeView (PFSAudioProcessor& p, int trackIndex, juce::Rectangle<int> initialBounds)
        : processor (p), track (trackIndex)
    {
        setAccessible (false);
        setBounds (initialBounds);
        for (int i = 0; i < pfs::nodeCount; ++i)
        {
            auto button = std::make_unique<NodeButton>();
            button->setAccessible (false);
            button->onClick = [this, i]
            {
                const auto node = processor.getNode (track, i);
                processor.setNodeActive (track, i, ! node.active);
                refresh();
            };
            button->onWheel = [this, i] (int delta)
            {
                const auto node = processor.getNode (track, i);
                processor.setNodeMidi (track, i, node.midi + delta);
                refresh();
            };

            // The standalone/editor panel has a fixed size. Give each node its
            // final bounds before attaching it to the visible component tree.
            // This avoids JUCE invalidating a partially-created button cache
            // during the first editor layout pass on Windows.
            int level = 0;
            while (level + 1 < pfs::levels
                   && i >= pfs::SequencerEngine::flatIndexFor (level + 1, 0))
                ++level;
            const auto index = i - pfs::SequencerEngine::flatIndexFor (level, 0);
            const auto count = 1 << level;
            const auto width = static_cast<float> (initialBounds.getWidth());
            const auto height = static_cast<float> (initialBounds.getHeight());
            const auto x = (index + 0.5f) * width / count;
            const auto y = height - 16.0f - level * ((height - 36.0f) / 4.0f);
            button->setBounds (juce::roundToInt (x - 14.0f),
                               juce::roundToInt (y - 10.0f), 28, 20);

            addAndMakeVisible (*button);
            nodes[static_cast<std::size_t> (i)] = std::move (button);
        }
        refresh();
    }

    void resized() override
    {
        // Node bounds are established before attachment in the constructor.
        // The editor is deliberately fixed-size, so no runtime relayout is needed.
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (edge.withAlpha (0.68f));
        for (int level = 0; level < pfs::levels - 1; ++level)
            for (int parent = 0; parent < (1 << level); ++parent)
            {
                const auto p = nodes[pfs::SequencerEngine::flatIndexFor (level, parent)]->getBounds().getCentre().toFloat();
                for (int branch = 0; branch < 2; ++branch)
                {
                    const auto c = nodes[pfs::SequencerEngine::flatIndexFor (level + 1, parent * 2 + branch)]->getBounds().getCentre().toFloat();
                    g.drawLine ({ p, c }, 0.85f);
                }
            }

        if (glowFrom >= 0 && glowTo >= 0 && glowProgress < 1.0f)
        {
            const auto from = nodes[glowFrom]->getBounds().getCentre().toFloat();
            const auto to = nodes[glowTo]->getBounds().getCentre().toFloat();
            const auto startT = juce::jlimit (0.0f, 1.0f, glowProgress - 0.16f);
            const auto endT = juce::jlimit (0.0f, 1.0f, glowProgress + 0.04f);
            const auto start = from + (to - from) * startT;
            const auto end = from + (to - from) * endT;
            g.setColour (amber.withAlpha (0.20f)); g.drawLine ({ start, end }, 7.0f);
            g.setColour (amber.withAlpha (0.50f)); g.drawLine ({ start, end }, 3.5f);
            g.setColour (juce::Colour (0xffffd94a)); g.drawLine ({ start, end }, 1.6f);
        }
    }

    void refresh (int coherentPlayhead = -2)
    {
        const auto playhead = coherentPlayhead == -2 ? processor.getPlayheadNode (track) : coherentPlayhead;
        for (int i = 0; i < pfs::nodeCount; ++i)
        {
            const auto state = processor.getNode (track, i);
            nodes[i]->setButtonText (noteName (state.midi));
            nodes[i]->setToggleState (state.active, juce::dontSendNotification);
            nodes[i]->setColour (juce::TextButton::buttonColourId, i == playhead ? amber : panel);
            nodes[i]->setColour (juce::TextButton::buttonOnColourId, i == playhead ? amber : blue);
        }
        repaint();
    }

    void tickAnimation (std::uint64_t serial, std::pair<int, int> branch)
    {
        if (serial != lastBranchSerial)
        {
            lastBranchSerial = serial;
            glowFrom = branch.first; glowTo = branch.second; glowProgress = 0.0f;
        }
        else if (glowProgress < 1.0f)
            glowProgress = juce::jmin (1.0f, glowProgress + 0.13f);
        repaint();
    }

private:
    PFSAudioProcessor& processor;
    int track;
    // Rendering and hit-testing own exactly the same 31-node topology as the
    // sequencer and node-state arrays. No extra or unreachable UI node can exist.
    std::array<std::unique_ptr<NodeButton>, pfs::nodeCount> nodes;
    std::uint64_t lastBranchSerial = 0;
    int glowFrom = -1, glowTo = -1;
    float glowProgress = 2.0f;
};

class PFSAudioProcessorEditor::TrackStrip final : public juce::Component
{
public:
    TrackStrip (PFSAudioProcessor& p, int trackIndex) : processor (p), track (trackIndex)
    {
        heading.setText (juce::String (track + 1), juce::dontSendNotification);
        heading.setJustificationType (juce::Justification::centred);
        sampleLabel.setText ("SAMPLE", juce::dontSendNotification);
        sampleLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (heading); addAndMakeVisible (sampleLabel);
        const auto prefix = "track" + juce::String (track + 1);
        for (int i = 0; i < 5; ++i)
        {
            sampleButtons[i].setButtonText (juce::String (i + 1));
            sampleButtons[i].onClick = [this, prefix, i] { setDiscreteParameter (processor.parameters, prefix + "Sample", i); refresh(); };
            addAndMakeVisible (sampleButtons[i]);
        }

        filter.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        filter.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        filter.setNumDecimalPlacesToDisplay (0);
        filter.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value)); };
        addAndMakeVisible (filter);
        for (auto* slider : { &gate, &volume, &pan, &reverb })
        {
            slider->setSliderStyle (juce::Slider::LinearVertical);
            slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider->setNumDecimalPlacesToDisplay (0);
            slider->textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value)); };
            addAndMakeVisible (*slider);
        }
        filter.setName ("FILTER"); gate.setName ("GATE"); volume.setName ("VOL"); pan.setName ("PAN"); reverb.setName ("REV");
        mute.setButtonText (juce::String (track + 1)); mute.setClickingTogglesState (true); addAndMakeVisible (mute);

        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, prefix + "Filter", filter));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, prefix + "Gate", gate));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, prefix + "Volume", volume));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, prefix + "Pan", pan));
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, prefix + "Reverb", reverb));
        muteAttachment = std::make_unique<ButtonAttachment> (processor.parameters, prefix + "Mute", mute);
        refresh();
    }

    void refresh()
    {
        const auto id = "track" + juce::String (track + 1) + "Sample";
        const auto selected = static_cast<int> (processor.parameters.getRawParameterValue (id)->load());
        for (int i = 0; i < 5; ++i) sampleButtons[i].setToggleState (i == selected, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (edge); g.drawRect (getLocalBounds(), 1);
        g.setColour (text.withAlpha (0.72f));
        g.setFont (9.5f);
        g.drawText ("FILTER", 4, 154, getWidth() - 8, 16, juce::Justification::centred);
        g.setFont (8.0f);
        const char* labels[] { "GATE", "VOL", "PAN", "REV" };
        for (int i = 0; i < 4; ++i) g.drawText (labels[i], 3 + i * 25, 250, 25, 12, juce::Justification::centred);
        g.drawText ("MUTE", 4, getHeight() - 48, getWidth() - 8, 12, juce::Justification::centred);
    }

    void resized() override
    {
        heading.setBounds (0, 7, getWidth(), 18);
        sampleLabel.setBounds (0, 27, getWidth(), 14);
        for (int i = 0; i < 5; ++i) sampleButtons[i].setBounds (getWidth() / 2 - 12, 43 + i * 20, 24, 18);
        filter.setBounds (getWidth() / 2 - 34, 170, 68, 64);
        const auto faderTop = 265;
        const auto faderHeight = juce::jmax (48, getHeight() - faderTop - 75);
        for (int i = 0; i < 4; ++i)
            std::array<juce::Slider*, 4> { &gate, &volume, &pan, &reverb }[i]->setBounds (3 + i * 25, faderTop, 25, faderHeight);
        mute.setBounds (getWidth() / 2 - 14, getHeight() - 34, 28, 22);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    PFSAudioProcessor& processor;
    int track;
    juce::Label heading, sampleLabel;
    std::array<juce::TextButton, 5> sampleButtons;
    juce::Slider filter, gate, volume, pan, reverb;
    juce::TextButton mute;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::unique_ptr<ButtonAttachment> muteAttachment;
};

PFSAudioProcessorEditor::PFSAudioProcessorEditor (PFSAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), lookAndFeel (std::make_unique<PFSLookAndFeel>())
{
    // This interface is a fully custom instrument panel.  Keeping its component
    // tree out of Windows UI Automation avoids a repeatable JUCE accessibility
    // notification fault while the initial bounds are being assigned.
    setAccessible (false);
    setLookAndFeel (lookAndFeel.get());
    title.setText ("PFS - PROBABILISTIC FRACTAL SEQUENCER", juce::dontSendNotification);
    auto titleFont = juce::Font (juce::FontOptions (13.0f)); titleFont.setExtraKerningFactor (0.22f); title.setFont (titleFont);
    title.setColour (juce::Label::textColourId, machineText);
    tempoLabel.setText ("TEMPO", juce::dontSendNotification); swingLabel.setText ("SWING", juce::dontSendNotification);
    ratchetLabel.setText ("RATCHET", juce::dontSendNotification); routeLabel.setText ("ROUTE", juce::dontSendNotification);
    mixLabel.setText ("MIX", juce::dontSendNotification); syncReadout.setText ("INTERNAL", juce::dontSendNotification);
    for (auto* label : { &title, &tempoLabel, &swingLabel, &ratchetLabel, &routeLabel, &mixLabel, &syncReadout })
    { label->setJustificationType (juce::Justification::centred); label->setColour (juce::Label::textColourId, machineText); addAndMakeVisible (*label); }

    configureKnob (tempo, " BPM"); tempo.setNumDecimalPlacesToDisplay (0);
    swing.setSliderStyle (juce::Slider::LinearHorizontal); swing.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); swing.setTextValueSuffix ("%"); swing.setNumDecimalPlacesToDisplay (0); addAndMakeVisible (swing);
    configureKnob (ratchetProbability, "%"); configureKnob (ratchetRepeats);
    ratchetProbability.setNumDecimalPlacesToDisplay (0); ratchetRepeats.setNumDecimalPlacesToDisplay (0);
    addSliderAttachment ("tempo", tempo); addSliderAttachment ("swing", swing);
    addSliderAttachment ("ratchetProb", ratchetProbability); addSliderAttachment ("ratchetRepeats", ratchetRepeats);

    for (auto* button : { &clockButton, &internalPlay, &fuap, &ratchetFade }) addAndMakeVisible (*button);
    internalPlay.setClickingTogglesState (true); fuap.setClickingTogglesState (true); ratchetFade.setClickingTogglesState (true);
    clockButton.setClickingTogglesState (true); clockButton.setTooltip ("Enable MIDI note and clock output");
    clockButton.setColour (juce::TextButton::buttonOnColourId, amber);
    fuap.setName ("fuap");
    addButtonAttachment ("internalPlay", internalPlay); addButtonAttachment ("fuap", fuap); addButtonAttachment ("ratchetFade", ratchetFade);
    addButtonAttachment ("midiOut", clockButton);

    const int shifts[] { -12, -1, 1, 12 };
    const juce::String shiftText[] { "-12", "-1", "+1", "+12" };
    for (int t = 0; t < 2; ++t)
    {
        const auto prefix = "track" + juce::String (t + 1);
        trackButtons[t].setButtonText (juce::String (t + 1)); trackButtons[t].setToggleState (true, juce::dontSendNotification);
        trackButtons[t].onClick = [this, t]
        {
            treeVisible[t] = ! treeVisible[t]; trees[t]->setVisible (treeVisible[t]);
            trackButtons[t].setToggleState (treeVisible[t], juce::dontSendNotification);
        };
        rateButtons[t].onClick = [this, prefix] { setParameterIndex (prefix + "Rate", (parameterIndex (prefix + "Rate") + 1) % 3); };
        staticButtons[t].setButtonText ("STATIC"); staticButtons[t].onClick = [this, prefix] { setParameterIndex (prefix + "Variation", 0); };
        variationButtons[t].setButtonText ("RANDOM"); variationButtons[t].onClick = [this, prefix] { setParameterIndex (prefix + "Variation", 1); };
        staticButtons[t].setColour (juce::TextButton::buttonOnColourId, amber);
        variationButtons[t].setColour (juce::TextButton::buttonOnColourId, amber);
        routeButtons[t].setButtonText (juce::String (t + 1)); routeButtons[t].onClick = [this, prefix] { setParameterIndex (prefix + "Ratchet", 1 - parameterIndex (prefix + "Ratchet")); };
        routeButtons[t].setColour (juce::TextButton::buttonOnColourId, amber);
        for (auto* button : { &trackButtons[t], &rateButtons[t], &staticButtons[t], &variationButtons[t], &routeButtons[t] }) addAndMakeVisible (*button);
        transposeLabels[t].setText ("TRANSPOSE " + juce::String (t + 1), juce::dontSendNotification); transposeLabels[t].setJustificationType (juce::Justification::centred); addAndMakeVisible (transposeLabels[t]);
        for (int i = 0; i < 4; ++i)
        {
            transpose[t][i].setButtonText (shiftText[i]); addAndMakeVisible (transpose[t][i]);
            transpose[t][i].onClick = [this, t, shift = shifts[i]] { processor.transposeTrack (t, shift); trees[t]->refresh(); };
        }
        // Keep the composite tree hidden until its children have their first
        // bounds. This avoids a Windows UI Automation notification racing the
        // initial NodeButton layout during standalone startup.
        // The standalone window is fixed at 1500 x 760. Lay the composite tree
        // out before attaching it, so the first parent resize sees unchanged
        // bounds and never sends a Windows UI Automation move notification.
        constexpr int initialUtilityX = 1500 - 258;
        constexpr int initialTreeTop = 142;
        constexpr int initialTreeHeight = 760 - 310;
        constexpr int initialTreeWidth = (initialUtilityX - 34) / 2;
        trees[t] = std::make_unique<TreeView> (
            processor, t,
            juce::Rectangle<int> { 14 + t * initialTreeWidth, initialTreeTop,
                                   initialTreeWidth - 4, initialTreeHeight });
        addChildComponent (*trees[t]);
        strips[t] = std::make_unique<TrackStrip> (processor, t); addAndMakeVisible (*strips[t]);
    }

    // Keep startup to one deterministic layout pass.  Resizing can be restored
    // after the Windows launch path has been proven stable on the target system.
    setResizable (false, false);
    setSize (1500, 760);
    for (auto& tree : trees)
        tree->setVisible (true);
    lastQuarterPulse = processor.getQuarterPulse();
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        diagnosticFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("PFS-diagnostics-v0.6.11.log");
        diagnosticLines = "PFS v0.6.11 standalone timing diagnostics\n";
        diagnosticFile.replaceWithText (diagnosticLines);
        lastDiagnosticFlushMs = juce::Time::getMillisecondCounterHiRes();
    }
    // Do not allow a timer callback to enter the partially-constructed editor.
    // Some standalone/message-loop combinations can dispatch an immediate
    // timer while the first component bounds are still being assigned.
    juce::Timer::callAfterDelay (100, [safeThis = juce::Component::SafePointer<PFSAudioProcessorEditor> (this)]
    {
        if (safeThis != nullptr)
            safeThis->startTimerHz (20);
    });
}

PFSAudioProcessorEditor::~PFSAudioProcessorEditor()
{
    stopTimer();
    if (diagnosticFile != juce::File{} && diagnosticLines.isNotEmpty())
        diagnosticFile.replaceWithText (diagnosticLines);
    setLookAndFeel (nullptr);
}

void PFSAudioProcessorEditor::configureKnob (juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setTextValueSuffix (suffix);
    slider.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value)); };
    addAndMakeVisible (slider);
}

void PFSAudioProcessorEditor::addSliderAttachment (const juce::String& id, juce::Slider& slider)
{ sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, id, slider)); }

void PFSAudioProcessorEditor::addButtonAttachment (const juce::String& id, juce::Button& button)
{ buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.parameters, id, button)); }

void PFSAudioProcessorEditor::setParameterIndex (const juce::String& id, int index)
{ setDiscreteParameter (processor.parameters, id, index); }

int PFSAudioProcessorEditor::parameterIndex (const juce::String& id) const
{ return static_cast<int> (std::lround (processor.parameters.getRawParameterValue (id)->load())); }

void PFSAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient surface (juce::Colour (0xff1b1c1c), 0.0f, 0.0f, background, 0.0f, static_cast<float> (getHeight()), false);
    g.setGradientFill (surface); g.fillAll();
    auto frame = getLocalBounds().toFloat().reduced (5.0f);
    g.setColour (juce::Colours::black); g.drawRoundedRectangle (frame, 5.0f, 6.0f);
    g.setColour (edge); g.drawRoundedRectangle (frame.reduced (3.0f), 4.0f, 2.0f);
    const auto utilityX = getWidth() - 258;
    g.setColour (edge.withAlpha (0.45f)); g.drawVerticalLine (utilityX, 38.0f, static_cast<float> (getHeight() - 18));
}

void PFSAudioProcessorEditor::resized()
{
    if (trees[0] == nullptr || trees[1] == nullptr || strips[0] == nullptr || strips[1] == nullptr) return;
    const auto utilityX = getWidth() - 258;
    title.setBounds (20, 20, 385, 24);
    tempoLabel.setBounds (utilityX / 2 - 100, 26, 80, 16); tempo.setBounds (utilityX / 2 - 100, 42, 80, 82);
    swingLabel.setBounds (utilityX / 2 + 10, 28, 110, 16); swing.setBounds (utilityX / 2 + 10, 48, 110, 55);
    clockButton.setBounds (utilityX - 220, 44, 82, 48); syncReadout.setBounds (utilityX - 225, 94, 92, 14);
    fuap.setBounds (utilityX - 122, 44, 96, 48);

    const auto logicCentre = utilityX + 125;
    ratchetLabel.setBounds (logicCentre - 108, 23, 216, 15);
    ratchetProbability.setBounds (logicCentre - 68, 40, 72, 78); ratchetRepeats.setBounds (logicCentre + 4, 40, 72, 78);
    routeLabel.setBounds (logicCentre - 55, 121, 110, 14);
    routeButtons[0].setBounds (logicCentre - 39, 138, 26, 22); routeButtons[1].setBounds (logicCentre + 13, 138, 26, 22);
    ratchetFade.setBounds (logicCentre - 27, 169, 54, 22);
    mixLabel.setBounds (utilityX + 18, 205, 220, 18);
    strips[0]->setBounds (utilityX + 14, 229, 106, getHeight() - 309);
    strips[1]->setBounds (utilityX + 130, 229, 106, getHeight() - 309);
    internalPlay.setBounds (utilityX + 78, getHeight() - 64, 94, 42);

    const auto treeTop = 142;
    const auto treeHeight = getHeight() - 310;
    const auto treeWidth = (utilityX - 34) / 2;
    for (int t = 0; t < 2; ++t)
    {
        const auto x = 14 + t * treeWidth;
        const auto centre = x + treeWidth / 2;
        trees[t]->setBounds (x, treeTop, treeWidth - 4, treeHeight);
        rateButtons[t].setBounds (centre - 16, 112, 32, 22);
        staticButtons[t].setBounds (centre - 52, treeTop + treeHeight + 7, 48, 18);
        variationButtons[t].setBounds (centre + 4, treeTop + treeHeight + 7, 48, 18);
        trackButtons[t].setBounds (centre - 36, treeTop + treeHeight + 38, 72, 50);
        transposeLabels[t].setBounds (centre + 72, treeTop + treeHeight + 39, 140, 15);
        for (int i = 0; i < 4; ++i) transpose[t][i].setBounds (centre + 72 + i * 36, treeTop + treeHeight + 58, 32, 22);
    }
}

void PFSAudioProcessorEditor::timerCallback()
{
    bool schedulerTraceAdded = false;
    PFSAudioProcessor::SchedulerTrace schedulerTrace;
    while (processor.popSchedulerTrace (schedulerTrace))
    {
        schedulerTraceAdded = true;
        diagnosticLines << "VISIT id=" << juce::String (schedulerTrace.nodeEvent)
                        << " tree=" << (schedulerTrace.track + 1)
                        << " level=" << schedulerTrace.level
                        << " index=" << schedulerTrace.index
                        << " node=" << schedulerTrace.flatIndex
                        << " active=" << (schedulerTrace.active ? 1 : 0)
                        << " visible=" << (schedulerTrace.visible ? 1 : 0)
                        << " muted=" << (schedulerTrace.muted ? 1 : 0)
                        << " trigger=" << (schedulerTrace.triggerRequested ? 1 : 0)
                        << " ratchetProbability=" << juce::roundToInt (schedulerTrace.ratchetProbability)
                        << " configuredDepth=" << schedulerTrace.configuredDepth
                        << " routed=" << (schedulerTrace.routed ? 1 : 0)
                        << " bypass=" << (schedulerTrace.ratchetBypassed ? 1 : 0)
                        << " generatedSubtriggers=" << schedulerTrace.subtriggerCount << "\n";
        for (int subtrigger = 0; subtrigger < schedulerTrace.subtriggerCount; ++subtrigger)
            diagnosticLines << "SUBTRIGGER nodeEvent=" << juce::String (schedulerTrace.nodeEvent)
                            << " track=" << (schedulerTrace.track + 1)
                            << " index=" << (subtrigger + 1)
                            << " sampleOffset=" << schedulerTrace.sampleOffsets[static_cast<std::size_t> (subtrigger)]
                            << "\n";
    }

    const auto diagnostic = processor.getDiagnosticSnapshot();
    if (diagnosticFile != juce::File{} && diagnostic.masterSteps != lastDiagnosticStep)
    {
        lastDiagnosticStep = diagnostic.masterSteps;
        juce::String line;
        line << juce::String (juce::Time::getMillisecondCounterHiRes(), 1)
             << " blocks=" << diagnostic.audioBlocks
             << " steps=" << diagnostic.masterSteps
             << " clock=" << diagnostic.clock
             << " play=" << (diagnostic.internalPlay ? 1 : 0)
             << " midiOut=" << (diagnostic.midiOut ? 1 : 0);
        for (int t = 0; t < 2; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            line << " | T" << (t + 1)
                 << " rate=" << diagnostic.rates[i]
                 << " random=" << (diagnostic.random[i] ? 1 : 0)
                 << " mute=" << (diagnostic.muted[i] ? 1 : 0)
                 << " root=" << (diagnostic.rootActive[i] ? 1 : 0)
                 << " node=" << diagnostic.playheads[i]
                 << " visit=" << diagnostic.visits[i]
                 << " origin=" << diagnostic.originVisits[i]
                 << " request=" << diagnostic.triggerRequests[i]
                 << " voice=" << diagnostic.voicesCreated[i]
                 << " soundBlocks=" << diagnostic.nonSilentBlocks[i];
        }
        diagnosticLines << line << "\n";
    }

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (diagnosticFile != juce::File{} && schedulerTraceAdded && nowMs - lastDiagnosticFlushMs >= 1000.0)
    {
        diagnosticFile.replaceWithText (diagnosticLines);
        lastDiagnosticFlushMs = nowMs;
    }

    const auto pulse = processor.getQuarterPulse();
    if (pulse != lastQuarterPulse)
    {
        lastQuarterPulse = pulse;
        for (int t = 0; t < 2; ++t)
            if (! treeVisible[t] && processor.trackHasActiveNodes (t)) flashTicks[t] = 2;
    }

    const juce::String rateText[] { "1", "1/2", "1/4" };
    // Read one published two-track visual event and give that same event to
    // both trees.  This prevents the display from making synchronized tracks
    // appear to take turns if an audio tick arrives during this callback.
    const auto visual = processor.getVisualStepSnapshot();
    for (int t = 0; t < 2; ++t)
    {
        const auto prefix = "track" + juce::String (t + 1);
        const auto variation = parameterIndex (prefix + "Variation") != 0;
        const auto routed = parameterIndex (prefix + "Ratchet") != 0;
        rateButtons[t].setButtonText (rateText[juce::jlimit (0, 2, parameterIndex (prefix + "Rate"))]);
        staticButtons[t].setToggleState (! variation, juce::dontSendNotification);
        variationButtons[t].setToggleState (variation, juce::dontSendNotification);
        routeButtons[t].setToggleState (routed, juce::dontSendNotification);
        const auto index = static_cast<std::size_t> (t);
        trees[t]->refresh (visual.playheads[index]);
        trees[t]->tickAnimation (visual.serial, { visual.branchFrom[index], visual.branchTo[index] });
        strips[t]->refresh();
        trackButtons[t].setToggleState (treeVisible[t], juce::dontSendNotification);
        trackButtons[t].setColour (juce::TextButton::buttonColourId, flashTicks[t] > 0 ? amber : juce::Colour (0xff24231d));
        if (flashTicks[t] > 0) --flashTicks[t];
    }

    const juce::String status[] { "INTERNAL", "DAW SYNC", "MIDI STOP", "MIDI IN" };
    auto statusText = status[juce::jlimit (0, 3, processor.getClockStatus())];
    if (parameterIndex ("midiOut") != 0) statusText += " + OUT";
    syncReadout.setText (statusText, juce::dontSendNotification);
    internalPlay.setButtonText (parameterIndex ("internalPlay") != 0 ? "STOP" : "PLAY");
}
