#include "PluginEditor.h"

//==============================================================================
// Colours & sizes
//==============================================================================

namespace Theme
{
    const juce::Colour bg          { 0xff1a1a2e };
    const juce::Colour surface     { 0xff16213e };
    const juce::Colour accent      { 0xff00d4ff };
    const juce::Colour accentDim   { 0xff0070a0 };
    const juce::Colour textMain    { 0xfff0f0ff };
    const juce::Colour textDim     { 0xff8888aa };
    const juce::Colour toggleOn    { 0xff00ff88 };
    const juce::Colour toggleOff   { 0xff334455 };
    const juce::Colour slotSelected{ 0xff1a3a5c };

    constexpr int W         = 740;
    constexpr int H         = 820;
    constexpr int PANEL_W   = 190;   // always-visible slot panel on the right

    constexpr int WAVE_H    = 200;
    constexpr int PAD       = 14;
    constexpr int ROW_H     = 80;
    constexpr int KNOB_W    = 80;
    constexpr int BTN_H     = 34;
}

//==============================================================================
// Simple dark LookAndFeel
//==============================================================================

class DarkLAF : public juce::LookAndFeel_V4
{
public:
    DarkLAF()
    {
        setColour (juce::Slider::thumbColourId,              Theme::accent);
        setColour (juce::Slider::rotarySliderFillColourId,   Theme::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, Theme::surface);
        setColour (juce::Slider::textBoxTextColourId,        Theme::textMain);
        setColour (juce::Slider::textBoxBackgroundColourId,  Theme::surface);
        setColour (juce::Slider::textBoxOutlineColourId,     Theme::accentDim);
        setColour (juce::Label::textColourId,                Theme::textMain);
        setColour (juce::TextButton::buttonColourId,         Theme::surface);
        setColour (juce::TextButton::buttonOnColourId,       Theme::accent);
        setColour (juce::TextButton::textColourOffId,        Theme::accent);
        setColour (juce::TextButton::textColourOnId,         Theme::bg);
        setColour (juce::ToggleButton::textColourId,         Theme::textMain);
        setColour (juce::ToggleButton::tickColourId,         Theme::toggleOn);
        setColour (juce::ToggleButton::tickDisabledColourId, Theme::toggleOff);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const float cx = x + w * 0.5f;
        const bool  isCompact = h < 55;
        const float arcH  = isCompact ? h - 12.0f : (float)h;
        const float cy    = y + arcH * 0.5f;
        const float r     = juce::jmin ((float)w, arcH) * 0.38f;
        const float strokeW = juce::jmin (4.0f, r * 0.17f);
        const float dotR    = juce::jmax (1.5f, r * 0.21f);

        juce::Path track;
        track.addCentredArc (cx, cy, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::surface);
        g.strokePath (track, juce::PathStrokeType (strokeW));

        juce::Path filled;
        filled.addCentredArc (cx, cy, r, r, 0.0f, startAngle,
                              startAngle + (endAngle - startAngle) * sliderPos, true);
        g.setColour (Theme::accent);
        g.strokePath (filled, juce::PathStrokeType (strokeW));

        const float angle = startAngle + (endAngle - startAngle) * sliderPos;
        const float tx = cx + r * std::sin (angle);
        const float ty = cy - r * std::cos (angle);
        g.setColour (Theme::accent);
        g.fillEllipse (tx - dotR, ty - dotR, dotR * 2.0f, dotR * 2.0f);

        g.setColour (Theme::textDim);
        g.setFont (10.0f);
        if (isCompact)
            g.drawText (slider.getTextFromValue (slider.getValue()),
                        x, y + (int)arcH, w, 12, juce::Justification::centred);
        else
        {
            const int textY = juce::jmin ((int)(cy + r * 0.55f), y + h - 14);
            g.drawText (slider.getTextFromValue (slider.getValue()),
                        (int)(cx - 22.0f), textY, 44, 14, juce::Justification::centred);
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn,
                           bool, bool) override
    {
        const bool on = btn.getToggleState();
        const auto b  = btn.getLocalBounds().toFloat();

        const float pw = 38.0f, ph = 20.0f;
        const float px = 4.0f, py = (b.getHeight() - ph) * 0.5f;
        juce::Rectangle<float> pill (px, py, pw, ph);

        g.setColour (on ? Theme::toggleOn : Theme::toggleOff);
        g.fillRoundedRectangle (pill, ph * 0.5f);

        const float tx = on ? px + pw - ph + 2.0f : px + 2.0f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (tx, py + 2.0f, ph - 4.0f, ph - 4.0f);

        g.setColour (Theme::textMain);
        g.setFont (13.0f);
        g.drawText (btn.getButtonText(),
                    (int)(px + pw + 6.0f), 0,
                    (int)(b.getWidth() - px - pw - 6.0f), (int)b.getHeight(),
                    juce::Justification::centredLeft);
    }
};

//==============================================================================
// Mod assignment panel (shown in a CallOutBox on right-click)
//==============================================================================

class ModAssignPanel final : public juce::Component, private juce::Timer
{
public:
    ModAssignPanel (AudioModAssign& a, const juce::String& paramName,
                   BPMSamplerProcessor& proc, std::function<void()> onChanged)
        : assign (a), processor (proc), onChangedCb (std::move (onChanged))
    {
        using namespace juce;
        title.setText ("Mod: " + paramName, dontSendNotification);
        title.setFont (Font (12.0f, Font::bold));
        title.setColour (Label::textColourId, Colours::white);
        addAndMakeVisible (title);

        for (int i = 0; i <= NUM_MOD_CHANNELS; ++i)
        {
            chanBtns[i].setButtonText (i == 0 ? "-" : String (i));
            chanBtns[i].setClickingTogglesState (false);
            chanBtns[i].onClick = [this, i] { setChannel (i == 0 ? -1 : i - 1); };
            addAndMakeVisible (chanBtns[i]);
        }

        depthLabel.setText ("Depth", dontSendNotification);
        depthLabel.setColour (Label::textColourId, Colours::white);
        depthLabel.setFont (Font (11.0f));
        addAndMakeVisible (depthLabel);

        depthSlider.setRange (-1.0, 1.0, 0.01);
        depthSlider.setValue (a.depth, dontSendNotification);
        depthSlider.setSliderStyle (Slider::LinearHorizontal);
        depthSlider.setTextBoxStyle (Slider::TextBoxRight, false, 52, 18);
        depthSlider.textFromValueFunction = [] (double v) { return String ((int)std::round (v * 100)) + "%"; };
        depthSlider.valueFromTextFunction = [] (const String& s) { return s.dropLastCharacters (1).getDoubleValue() / 100.0; };
        depthSlider.onValueChange = [this] {
            assign.depth = (float)depthSlider.getValue();
            if (onChangedCb) onChangedCb();
        };
        addAndMakeVisible (depthSlider);

        updateButtons();
        startTimerHz (20);
        setSize (270, 108);
    }

    ~ModAssignPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        // Draw signal level dots above each channel button (ch 1-8)
        const int n = NUM_MOD_CHANNELS;
        if (meterBounds.isEmpty()) return;
        const float cellW = (float)meterBounds.getWidth() / (float)n;
        for (int ch = 0; ch < n; ++ch)
        {
            const float level = processor.getModChannelLevel (ch);
            const bool  hasSignal = level > 0.001f;
            const float cx = meterBounds.getX() + (ch + 0.5f) * cellW;
            const float cy = meterBounds.getCentreY();
            g.setColour (hasSignal ? juce::Colour (0xff00ff88) : juce::Colour (0xff334455));
            g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        title.setBounds (b.removeFromTop (18));
        b.removeFromTop (2);

        // Level dots row (ch 1-8 only, offset by one button width to align with ch buttons)
        const int btnW = b.getWidth() / (NUM_MOD_CHANNELS + 1);
        meterBounds = b.removeFromTop (10).withTrimmedLeft (btnW);
        b.removeFromTop (2);

        auto row = b.removeFromTop (24);
        for (int i = 0; i <= NUM_MOD_CHANNELS; ++i)
            chanBtns[i].setBounds (row.removeFromLeft (btnW).reduced (1));

        b.removeFromTop (4);
        auto depthRow = b.removeFromTop (22);
        depthLabel.setBounds (depthRow.removeFromLeft (42));
        depthSlider.setBounds (depthRow);
    }

private:
    AudioModAssign&       assign;
    BPMSamplerProcessor&  processor;
    std::function<void()> onChangedCb;
    juce::Label           title, depthLabel;
    juce::TextButton      chanBtns[NUM_MOD_CHANNELS + 1];
    juce::Slider          depthSlider;
    juce::Rectangle<int>  meterBounds;

    void timerCallback() override { repaint (meterBounds); }

    void setChannel (int ch)
    {
        assign.channel = ch;
        updateButtons();
        if (onChangedCb) onChangedCb();
    }

    void updateButtons()
    {
        for (int i = 0; i <= NUM_MOD_CHANNELS; ++i)
        {
            const bool active = (i == 0) ? (assign.channel < 0) : (assign.channel == i - 1);
            chanBtns[i].setColour (juce::TextButton::buttonColourId,  active ? juce::Colour (0xff00d4ff) : juce::Colour (0xff16213e));
            chanBtns[i].setColour (juce::TextButton::textColourOffId, active ? juce::Colour (0xff1a1a2e) : juce::Colour (0xff00d4ff));
        }
    }
};

//==============================================================================
// Helper: labelled knob layout
//==============================================================================

static void setupKnob (juce::Slider& s, juce::Label& l,
                       const juce::String& name,
                       double min, double max, double defaultVal,
                       const juce::String& suffix = "")
{
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (min, max);
    s.setValue (defaultVal);
    if (suffix.isNotEmpty()) s.setTextValueSuffix (suffix);

    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (11.0f));
}

//==============================================================================
// Constructor
//==============================================================================

BPMSamplerEditor::BPMSamplerEditor (BPMSamplerProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      waveformDisplay (p.slotThumbnails[0].get())
{
    setSize (Theme::W + Theme::PANEL_W, Theme::H);
    setResizable (true, false);
    setResizeLimits (500, 300, 1600, 1200);

    static DarkLAF laf;
    setLookAndFeel (&laf);

    //==========================================================================
    // Waveform
    addAndMakeVisible (waveformDisplay);

    // Waveform callbacks capture this->currentSlot by reference via this
    waveformDisplay.onStartChanged = [this] (float v) {
        const juce::String id = "s" + juce::String (currentSlot) + "_startPos";
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (v);
    };
    waveformDisplay.onEndChanged = [this] (float v) {
        const juce::String id = "s" + juce::String (currentSlot) + "_endPos";
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (v);
    };
    waveformDisplay.onSliceAdded = [this] (float pos) {
        processor.addSlice (currentSlot, pos);
    };
    waveformDisplay.onSliceRemoved = [this] (int idx) {
        processor.removeSliceAt (currentSlot, idx);
    };
    waveformDisplay.onSliceMoved = [this] (float from, float to) {
        processor.moveSlice (currentSlot, from, to);
    };

    processor.onSlotLoaded = [this] (int slotIdx) {
        const auto& slot = processor.slots[slotIdx];
        const juce::String name = slot.hasFile()
            ? juce::File (slot.filePath).getFileName() : "(empty)";
        slotFileButtons[slotIdx].setButtonText ("Load");

        if (slotIdx == currentSlot)
        {
            fileLabel.setText (slot.hasFile()
                ? juce::File (slot.filePath).getFileNameWithoutExtension()
                : "No file loaded", juce::dontSendNotification);
            waveformDisplay.setThumbnail (processor.slotThumbnails[slotIdx].get());
        }
    };

    //==========================================================================
    // Load button, detect button & label
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { openFile(); };

    addChildComponent (detectTransientsButton);
    detectTransientsButton.onClick = [this] {
        processor.detectTransients (currentSlot, 0.3f);
    };

    fileLabel.setText ("No file loaded", juce::dontSendNotification);
    fileLabel.setJustificationType (juce::Justification::centredLeft);
    fileLabel.setColour (juce::Label::textColourId, Theme::textDim);
    fileLabel.setFont (juce::Font (13.0f));
    addAndMakeVisible (fileLabel);

    //==========================================================================
    // Regular knobs (will be given per-slot attachments in rebindSlot)
    setupKnob (endPosSlider,       endPosLabel,       "End",    0.0,   1.0,  1.0);
    setupKnob (numBeatsSlider,     numBeatsLabel,     "Beats",  1.0,  64.0,  4.0);
    setupKnob (grainJitterSlider,  grainJitterLabel,  "Jitter", 0.0,  12.0,  0.0, " st");
    setupKnob (speedFineSlider,    speedFineLabel,    "Fine",   0.5,   2.0,  1.0, "x");

    for (auto* s : { &endPosSlider, &numBeatsSlider, &grainJitterSlider, &speedFineSlider })
        addAndMakeVisible (s);
    for (auto* l : { &endPosLabel, &numBeatsLabel, &grainJitterLabel, &speedFineLabel })
        addAndMakeVisible (l);

    endPosSlider.onValueChange = [this] {
        waveformDisplay.setEndPos ((float)endPosSlider.getValue());
    };

    //==========================================================================
    // Slice index knob
    sliceIndexSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    sliceIndexSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
    sliceIndexSlider.setRange (0, 0, 1);
    sliceIndexSlider.onValueChange = [this] {
        const juce::String id = "s" + juce::String (currentSlot) + "_sliceIndex";
        const float val = (float)sliceIndexSlider.getValue();
        if (auto* param = processor.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (val));
    };
    sliceIndexLabel.setText ("Slice", juce::dontSendNotification);
    sliceIndexLabel.setJustificationType (juce::Justification::centred);
    sliceIndexLabel.setFont (juce::Font (11.0f));
    addChildComponent (sliceIndexSlider);
    addChildComponent (sliceIndexLabel);

    //==========================================================================
    // Toggles (per-slot; attachments made in rebindSlot)
    for (auto* btn : { &loopButton, &gateButton, &bpmSyncButton, &speedSyncButton,
                       &sliceModeButton, &midiNoteModeButton })
        addAndMakeVisible (btn);

    // TS mode combo
    tsModeCombo.addItem ("Off",           1);
    tsModeCombo.addItem ("OLA",           2);
    tsModeCombo.addItem ("WSOLA",         3);
    tsModeCombo.addItem ("Phase Vocoder", 4);
    tsModeCombo.setSelectedId (1, juce::dontSendNotification);
    tsModeLabel.setText ("Timestretch", juce::dontSendNotification);
    tsModeLabel.setJustificationType (juce::Justification::centred);
    tsModeLabel.setFont (juce::Font (11.0f));
    tsModeLabel.setColour (juce::Label::textColourId, Theme::textMain);
    addAndMakeVisible (tsModeCombo);
    addAndMakeVisible (tsModeLabel);

    //==========================================================================
    // Character controls (global)
    addAndMakeVisible (hissEnabledButton);
    setupKnob (outputSatSlider, outputSatLabel, "Warm",   0.0, 1.0, 0.0);
    addAndMakeVisible (outputSatSlider);
    addAndMakeVisible (outputSatLabel);

    //==========================================================================
    // Resonator controls (global)
    addAndMakeVisible (resoEnabledButton);

    setupKnob (resoRootSlider,     resoRootLabel,     "Root",    20.0, 2000.0, 220.0, " Hz");
    setupKnob (resoQSlider,        resoQLabel,        "Q",       0.5,    20.0,   5.0);
    setupKnob (resoTaperSlider,    resoTaperLabel,    "Taper",   0.0,    2.0,   0.5);
    setupKnob (resoQTaperSlider,   resoQTaperLabel,   "Q Taper",-1.0,    1.0,   0.0);
    setupKnob (resoInharmSlider,   resoInharmLabel,   "Inharm",  0.0,    0.5,   0.0);
    setupKnob (resoDriveSlider,    resoDriveLabel,    "Drive",   0.0,    1.0,   0.0);
    setupKnob (resoScatterSlider,  resoScatterLabel,  "Scatter", 0.0,    2.0,   0.0, "%");
    setupKnob (resoTimeSlider,     resoTimeLabel,     "Time",    0.5,   50.0,  10.0, " ms");
    setupKnob (resoFeedbackSlider, resoFeedbackLabel, "Feedback",0.0,   0.95,   0.3);
    setupKnob (resoMixSlider,      resoMixLabel,      "Mix",     0.0,    1.0,   0.5);
    setupKnob (resoDecaySlider,    resoDecayLabel,    "Decay",   0.01,   5.0,   0.5, " s");

    for (auto* s : { &resoRootSlider, &resoQSlider, &resoTaperSlider,
                     &resoQTaperSlider, &resoInharmSlider,
                     &resoDriveSlider, &resoScatterSlider,
                     &resoTimeSlider, &resoFeedbackSlider, &resoMixSlider,
                     &resoDecaySlider })
        addAndMakeVisible (s);
    for (auto* l : { &resoRootLabel, &resoQLabel, &resoTaperLabel,
                     &resoQTaperLabel, &resoInharmLabel,
                     &resoDriveLabel, &resoScatterLabel,
                     &resoTimeLabel, &resoFeedbackLabel, &resoMixLabel,
                     &resoDecayLabel })
        addAndMakeVisible (l);

    resoHarmonicsCombo.addItem ("8",  1);
    resoHarmonicsCombo.addItem ("16", 2);
    resoHarmonicsCombo.addItem ("32", 3);
    resoHarmonicsCombo.setSelectedId (1, juce::dontSendNotification);
    resoHarmonicsLabel.setText ("Harmonics", juce::dontSendNotification);
    resoHarmonicsLabel.setJustificationType (juce::Justification::centred);
    resoHarmonicsLabel.setFont (juce::Font (11.0f));
    addAndMakeVisible (resoHarmonicsCombo);
    addAndMakeVisible (resoHarmonicsLabel);

    resoSeriesCombo.addItem ("All",  1);
    resoSeriesCombo.addItem ("Odd",  2);
    resoSeriesCombo.addItem ("Even", 3);
    resoSeriesCombo.setSelectedId (1, juce::dontSendNotification);
    resoSeriesLabel.setText ("Series", juce::dontSendNotification);
    resoSeriesLabel.setJustificationType (juce::Justification::centred);
    resoSeriesLabel.setFont (juce::Font (11.0f));
    addAndMakeVisible (resoSeriesCombo);
    addAndMakeVisible (resoSeriesLabel);

    //==========================================================================
    // Granular controls (per-slot; toggle attachment made in rebindSlot)
    addAndMakeVisible (granEnabledButton);

    setupKnob (granPosJitterSlider, granPosJitterLabel, "Pos Jitter", 0.0, 1.0,   0.1);
    setupKnob (granDensitySlider,   granDensityLabel,   "Density",    1.0, 100.0, 10.0, " /s");
    setupKnob (granSizeSlider,      granSizeLabel,      "Size",       10.0, 500.0, 80.0, " ms");
    setupKnob (granScanLenSlider, granScanLenLabel, "Scan Len", 0.0, 1.0,   0.5);
    setupKnob (granScanSpdSlider, granScanSpdLabel, "Scan Spd", 0.0, 4.0,   0.0, " Hz");
    setupKnob (granScanDepSlider, granScanDepLabel, "Scan Dep", 0.0, 1.0,   0.0);
    setupKnob (granProbSlider,    granProbLabel,    "Prob",     0.0, 1.0,   1.0);

    for (auto* s : { &granPosJitterSlider, &granDensitySlider,  &granSizeSlider,
                     &granScanLenSlider,   &granScanSpdSlider,  &granScanDepSlider,
                     &granProbSlider })
    {
        addAndMakeVisible (s);
        s->setVisible (false);
    }
    for (auto* l : { &granPosJitterLabel, &granDensityLabel,  &granSizeLabel,
                     &granScanLenLabel,   &granScanSpdLabel,  &granScanDepLabel,
                     &granProbLabel })
    {
        addAndMakeVisible (l);
        l->setVisible (false);
    }

    //==========================================================================
    // Envelope section (per-slot; attachments made in rebindSlot)
    addAndMakeVisible (envEnabledButton);
    setupKnob (envAttackSlider,  envAttackLabel,  "Attack",  1.0, 5000.0, 10.0,  " ms");
    setupKnob (envDecaySlider,   envDecayLabel,   "Decay",   1.0, 5000.0, 100.0, " ms");
    setupKnob (envSustainSlider, envSustainLabel, "Sustain", 0.0, 1.0,    1.0);
    setupKnob (envReleaseSlider, envReleaseLabel, "Release", 1.0, 5000.0, 200.0, " ms");
    for (auto* s : { &envAttackSlider, &envDecaySlider, &envSustainSlider, &envReleaseSlider })
        addAndMakeVisible (s);
    for (auto* l : { &envAttackLabel, &envDecayLabel, &envSustainLabel, &envReleaseLabel })
        addAndMakeVisible (l);

    //==========================================================================
    // Multi-sample section (global)
    sampleModeCombo.addItem ("Single",   1);
    sampleModeCombo.addItem ("MIDI Map", 2);
    sampleModeCombo.addItem ("Morph",    3);
    sampleModeCombo.setSelectedId (1, juce::dontSendNotification);
    sampleModeLabel.setText ("Mode", juce::dontSendNotification);
    sampleModeLabel.setJustificationType (juce::Justification::centredLeft);
    sampleModeLabel.setFont (juce::Font (11.0f));
    sampleModeLabel.setColour (juce::Label::textColourId, Theme::textMain);
    addAndMakeVisible (sampleModeCombo);
    addAndMakeVisible (sampleModeLabel);

    for (int i = 1; i <= 8; ++i)
        polyVoicesCombo.addItem (i == 1 ? "1 Voice" : juce::String (i) + " Voices", i);
    polyVoicesCombo.setSelectedId (1, juce::dontSendNotification);
    polyVoicesLabel.setText ("Poly", juce::dontSendNotification);
    polyVoicesLabel.setJustificationType (juce::Justification::centredLeft);
    polyVoicesLabel.setFont (juce::Font (11.0f));
    polyVoicesLabel.setColour (juce::Label::textColourId, Theme::textMain);
    addAndMakeVisible (polyVoicesCombo);
    addAndMakeVisible (polyVoicesLabel);

    morphPosSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    morphPosSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 46, 20);
    morphPosSlider.setRange (0.0, 7.0, 0.001);
    morphPosSlider.setValue (0.0);
    morphPosLabel.setText ("Morph", juce::dontSendNotification);
    morphPosLabel.setJustificationType (juce::Justification::centredLeft);
    morphPosLabel.setFont (juce::Font (11.0f));
    morphPosLabel.setColour (juce::Label::textColourId, Theme::textMain);
    addAndMakeVisible (morphPosSlider);
    addAndMakeVisible (morphPosLabel);
    morphPosSlider.setVisible (false);
    morphPosLabel.setVisible  (false);

    lpcMorphBtn.setClickingTogglesState (true);
    lpcMorphBtn.setVisible (false);
    addAndMakeVisible (lpcMorphBtn);

    //==========================================================================
    // Global APVTS attachments
    resoEnabledAttach   = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter ("resoEnabled"),   resoEnabledButton);
    resoRootAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoRoot"),      resoRootSlider);
    resoQAttach         = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoQ"),         resoQSlider);
    resoTaperAttach     = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoTaper"),     resoTaperSlider);
    resoHarmonicsAttach = std::make_unique<juce::ComboBoxParameterAttachment>
                              (*processor.apvts.getParameter ("resoHarmonics"), resoHarmonicsCombo);
    resoTimeAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoTime"),      resoTimeSlider);
    resoFeedbackAttach  = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoFeedback"),  resoFeedbackSlider);
    resoMixAttach       = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoMix"),       resoMixSlider);
    resoInharmAttach    = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoInharm"),    resoInharmSlider);
    resoQTaperAttach    = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoQTaper"),    resoQTaperSlider);
    resoSeriesAttach    = std::make_unique<juce::ComboBoxParameterAttachment>
                              (*processor.apvts.getParameter ("resoSeries"),    resoSeriesCombo);
    resoDecayAttach     = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoDecay"),     resoDecaySlider);
    resoDriveAttach     = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoDrive"),     resoDriveSlider);
    resoScatterAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter ("resoScatter"),   resoScatterSlider);

    hissEnabledAttach = std::make_unique<juce::ButtonParameterAttachment>
                            (*processor.apvts.getParameter ("hissEnabled"), hissEnabledButton);
    outputSatAttach   = std::make_unique<juce::SliderParameterAttachment>
                            (*processor.apvts.getParameter ("outputSat"),     outputSatSlider);

    sampleModeAttach  = std::make_unique<juce::ComboBoxParameterAttachment>
                            (*processor.apvts.getParameter ("sampleMode"), sampleModeCombo);
    polyVoicesAttach  = std::make_unique<juce::ComboBoxParameterAttachment>
                            (*processor.apvts.getParameter ("polyVoices"), polyVoicesCombo);
    morphPosAttach    = std::make_unique<juce::SliderParameterAttachment>
                            (*processor.apvts.getParameter ("morphPos"),   morphPosSlider);
    lpcMorphAttach    = std::make_unique<juce::ButtonParameterAttachment>
                            (*processor.apvts.getParameter ("morphAlgo"), lpcMorphBtn);

    //==========================================================================
    // Slot panel — 8 rows, always visible
    for (int i = 0; i < 8; ++i)
    {
        // Volume knob — replaces old S0/S1 select button; clicking also selects the slot
        slotVolSliders[i].setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slotVolSliders[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slotVolSliders[i].setRange (0.0, 2.0, 0.001);
        slotVolSliders[i].setDoubleClickReturnValue (true, 1.0);
        slotVolSliders[i].setTooltip ("Volume (slot " + juce::String (i) + ")");
        addAndMakeVisible (slotVolSliders[i]);

        slotVolAttachments[i] = std::make_unique<juce::SliderParameterAttachment> (
            *processor.apvts.getParameter ("s" + juce::String (i) + "_vol"),
            slotVolSliders[i]);

        // Mouse listener: clicking vol knob, Lo, or Hi label selects this slot
        rowListeners[i].callback = [this, i] { selectSlot (i); };
        slotVolSliders[i].addMouseListener (&rowListeners[i], false);
        slotLoLabels[i].addMouseListener   (&rowListeners[i], false);
        slotHiLabels[i].addMouseListener   (&rowListeners[i], false);

        slotFileButtons[i].setButtonText ("Load");
        slotFileButtons[i].onClick = [this, i]
        {
            selectSlot (i);
            slotFileChoosers[i] = std::make_shared<juce::FileChooser> (
                "Load sample for slot " + juce::String (i),
                juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                "*.wav;*.aif;*.aiff;*.mp3;*.ogg;*.flac");
            slotFileChoosers[i]->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, i] (const juce::FileChooser& fc)
                {
                    const auto result = fc.getResult();
                    if (result.existsAsFile())
                        processor.loadAudioFileToSlot (result, i);
                });
        };
        addAndMakeVisible (slotFileButtons[i]);

        slotLoLabels[i].setText ("--", juce::dontSendNotification);
        slotLoLabels[i].setFont (juce::Font (11.0f));
        slotLoLabels[i].setJustificationType (juce::Justification::centred);
        slotLoLabels[i].setEditable (false, true, false);
        slotLoLabels[i].setColour (juce::Label::textColourId, Theme::textDim);
        slotLoLabels[i].setColour (juce::Label::backgroundWhenEditingColourId, Theme::surface);
        slotLoLabels[i].onTextChange = [this, i]
        {
            const int v = juce::jlimit (0, 127, slotLoLabels[i].getText().getIntValue());
            processor.setSlotNoteRange (i, v, processor.slots[i].hiNote);
            slotLoLabels[i].setText (juce::String (v), juce::dontSendNotification);
        };
        addAndMakeVisible (slotLoLabels[i]);

        slotHiLabels[i].setText ("--", juce::dontSendNotification);
        slotHiLabels[i].setFont (juce::Font (11.0f));
        slotHiLabels[i].setJustificationType (juce::Justification::centred);
        slotHiLabels[i].setEditable (false, true, false);
        slotHiLabels[i].setColour (juce::Label::textColourId, Theme::textDim);
        slotHiLabels[i].setColour (juce::Label::backgroundWhenEditingColourId, Theme::surface);
        slotHiLabels[i].onTextChange = [this, i]
        {
            const int v = juce::jlimit (0, 127, slotHiLabels[i].getText().getIntValue());
            processor.setSlotNoteRange (i, processor.slots[i].loNote, v);
            slotHiLabels[i].setText (juce::String (v), juce::dontSendNotification);
        };
        addAndMakeVisible (slotHiLabels[i]);
    }

    //==========================================================================
    // Build per-slot attachments and CurveKnobs for slot 0
    rebindSlot();

    // Register right-click mod listeners for regular (non-CurveKnob) sliders
    // Slot-specific (use currentSlot at click time)
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_GrainJitter, "Grain Pitch Jitter", grainJitterSlider,    grainJitterLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_GranPosJit, "Pos Jitter",          granPosJitterSlider,  granPosJitterLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_GranSize,   "Grain Size",          granSizeSlider,        granSizeLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_GranDensity,"Density",             granDensitySlider,     granDensityLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_ScanLen,    "Scan Len",            granScanLenSlider,     granScanLenLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_ScanSpd,    "Scan Spd",            granScanSpdSlider,     granScanSpdLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, false, kMod_ScanDep,    "Scan Dep",            granScanDepSlider,     granScanDepLabel));
    // Global
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, true,  kMod_ResoRoot,   "Reso Root",           resoRootSlider,        resoRootLabel));
    sliderModListeners.push_back (std::make_unique<SliderModListener> (*this, true,  kMod_ResoInharm, "Reso Inharm",         resoInharmSlider,      resoInharmLabel));

    startTimer (40); // ~25 fps
}

BPMSamplerEditor::~BPMSamplerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    processor.onSlotLoaded = nullptr;
}

//==============================================================================
// Slot selection
//==============================================================================

void BPMSamplerEditor::selectSlot (int idx)
{
    if (idx < 0 || idx >= BPMSamplerProcessor::NUM_SLOTS) return;

    currentSlot = idx;
    processor.selectedSlot.store (idx);

    // Switch waveform thumbnail
    waveformDisplay.setThumbnail (processor.slotThumbnails[idx].get());

    // Update file label
    const auto& slot = processor.slots[idx];
    fileLabel.setText (slot.hasFile()
        ? juce::File (slot.filePath).getFileNameWithoutExtension()
        : "No file loaded", juce::dontSendNotification);

    // Rebuild per-slot attachments and CurveKnobs
    rebindSlot();

    repaint();
}

void BPMSamplerEditor::mouseDown (const juce::MouseEvent& e)
{
    const float vs      = (float)getHeight() / (float)Theme::H;
    const int   panelW  = (int)(Theme::PANEL_W * vs);
    const int   panX    = getWidth() - panelW;

    if (e.x < panX) return;

    const int panPad  = (int)(8  * vs);
    const int headerH = (int)(22 * vs);
    const int totalH  = getHeight() - 2 * panPad - headerH;
    const int rowH    = totalH / 8;
    const int relY    = e.y - (panPad + headerH);

    if (relY < 0) return;
    const int idx = relY / rowH;
    if (idx >= 0 && idx < BPMSamplerProcessor::NUM_SLOTS)
        selectSlot (idx);
}

void BPMSamplerEditor::rebindSlot()
{
    const juce::String s = "s" + juce::String (currentSlot) + "_";
    auto& slot = processor.slots[currentSlot];

    //--- Destroy old per-slot attachments (order matters: attachment before CurveKnob) ---
    endPosAttach.reset();
    numBeatsAttach.reset();
    grainJitterAttach.reset();
    speedFineAttach.reset();
    loopAttach.reset();
    gateAttach.reset();
    bpmSyncAttach.reset();
    speedSyncAttach.reset();
    sliceModeAttach.reset();
    midiNoteModeAttach.reset();
    tsModeAttach.reset();
    granEnabledAttach.reset();
    granPosJitterAttach.reset();
    granDensityAttach.reset();
    granSizeAttach.reset();
    granScanLenAttach.reset();
    granScanSpdAttach.reset();
    granScanDepAttach.reset();
    granProbAttach.reset();
    envEnabledAttach.reset();
    envAttackAttach.reset();
    envDecayAttach.reset();
    envSustainAttach.reset();
    envReleaseAttach.reset();

    // CurveKnobs remove themselves from the component hierarchy in their dtor
    pitchCurveKnob.reset();
    speedCurveKnob.reset();
    startPosCurveKnob.reset();
    granPosCurveKnob.reset();

    //--- Recreate CurveKnobs ---
    pitchCurveKnob    = std::make_unique<CurveKnob> (
        *processor.apvts.getParameter (s + "pitch"),    slot.pitchCurve,    "Pitch",    " st");
    speedCurveKnob    = std::make_unique<CurveKnob> (
        *processor.apvts.getParameter (s + "speed"),    slot.speedCurve,    "Speed",    "x");
    startPosCurveKnob = std::make_unique<CurveKnob> (
        *processor.apvts.getParameter (s + "startPos"), slot.startPosCurve, "Start");
    granPosCurveKnob  = std::make_unique<CurveKnob> (
        *processor.apvts.getParameter (s + "granPos"),  slot.granPosCurve,  "Position");

    startPosCurveKnob->onValueChange = [this] (float v) {
        waveformDisplay.setStartPos (v);
    };

    const auto onCurveChange = [this] {
        processor.updateHostDisplay (
            juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true));
    };
    for (auto* k : { pitchCurveKnob.get(), speedCurveKnob.get(),
                     startPosCurveKnob.get(), granPosCurveKnob.get() })
    {
        k->onCurveChanged = onCurveChange;
        addAndMakeVisible (k);
    }

    //--- Recreate per-slot attachments ---
    endPosAttach        = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "endPos"),         endPosSlider);
    numBeatsAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "numBeats"),       numBeatsSlider);
    grainJitterAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "grainJitter"),    grainJitterSlider);
    speedFineAttach     = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "speedFine"),      speedFineSlider);
    loopAttach          = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "loopEnabled"),    loopButton);
    gateAttach          = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "gateEnabled"),    gateButton);
    bpmSyncAttach       = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "bpmSync"),        bpmSyncButton);
    speedSyncAttach     = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "speedSync"),      speedSyncButton);
    sliceModeAttach     = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "sliceMode"),      sliceModeButton);
    midiNoteModeAttach  = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "midiNoteMode"),   midiNoteModeButton);
    tsModeAttach        = std::make_unique<juce::ComboBoxParameterAttachment>
                              (*processor.apvts.getParameter (s + "tsMode"),         tsModeCombo);
    granEnabledAttach   = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "granEnabled"),    granEnabledButton);
    granPosJitterAttach = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granPosJitter"),  granPosJitterSlider);
    granDensityAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granDensity"),    granDensitySlider);
    granSizeAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granSize"),       granSizeSlider);
    granScanLenAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granScanLen"),    granScanLenSlider);
    granScanSpdAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granScanSpd"),    granScanSpdSlider);
    granScanDepAttach   = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granScanDep"),    granScanDepSlider);
    granProbAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "granProb"),       granProbSlider);
    envEnabledAttach    = std::make_unique<juce::ButtonParameterAttachment>
                              (*processor.apvts.getParameter (s + "envEnabled"),     envEnabledButton);
    envAttackAttach     = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "envAttack"),      envAttackSlider);
    envDecayAttach      = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "envDecay"),       envDecaySlider);
    envSustainAttach    = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "envSustain"),     envSustainSlider);
    envReleaseAttach    = std::make_unique<juce::SliderParameterAttachment>
                              (*processor.apvts.getParameter (s + "envRelease"),     envReleaseSlider);

    // Sync waveform display start/end from the newly bound parameters
    waveformDisplay.setStartPos (*processor.apvts.getRawParameterValue (s + "startPos"));
    waveformDisplay.setEndPos   (*processor.apvts.getRawParameterValue (s + "endPos"));

    // Sync knob visibility to current slot's state
    const bool granOn   = granEnabledButton.getToggleState();
    const bool sliceOn  = sliceModeButton.getToggleState();

    granPosCurveKnob->setVisible     (granOn);
    startPosCurveKnob->setVisible    (!granOn && !sliceOn);
    sliceIndexSlider.setVisible      (sliceOn && !granOn);
    sliceIndexLabel.setVisible       (sliceOn && !granOn);
    detectTransientsButton.setVisible (sliceOn);

    for (auto* s2 : { &granPosJitterSlider, &granDensitySlider,  &granSizeSlider,
                      &granScanLenSlider,   &granScanSpdSlider,  &granScanDepSlider,
                      &granProbSlider })
        s2->setVisible (granOn);
    for (auto* l : { &granPosJitterLabel, &granDensityLabel,  &granSizeLabel,
                     &granScanLenLabel,   &granScanSpdLabel,  &granScanDepLabel,
                     &granProbLabel })
        l->setVisible (granOn);
    endPosSlider.setVisible     (!granOn);
    endPosLabel.setVisible      (!granOn);
    speedCurveKnob->setVisible  (!granOn);
    speedFineSlider.setVisible  (!granOn);
    speedFineLabel.setVisible   (!granOn);
    numBeatsSlider.setVisible   (!granOn);
    numBeatsLabel.setVisible    (!granOn);

    // Wire mod right-click for CurveKnobs (always points to currentSlot via lambda capture)
    pitchCurveKnob->onRightClick = [this] {
        showModAssignPanel (processor.slotMod[currentSlot][kMod_Pitch], "Pitch", *pitchCurveKnob);
    };
    speedCurveKnob->onRightClick = [this] {
        showModAssignPanel (processor.slotMod[currentSlot][kMod_Speed], "Speed", *speedCurveKnob);
    };
    granPosCurveKnob->onRightClick = [this] {
        showModAssignPanel (processor.slotMod[currentSlot][kMod_GranPos], "Gran Pos", *granPosCurveKnob);
    };
    startPosCurveKnob->onRightClick = [this] {
        showModAssignPanel (processor.slotMod[currentSlot][kMod_StartPos], "Start Pos", *startPosCurveKnob);
    };

    updateModIndicators();
    resized();
}

//==============================================================================
// Layout
//==============================================================================

void BPMSamplerEditor::resized()
{
    const float vs = (float)getHeight() / (float)Theme::H;

    const int p       = (int)(Theme::PAD    * vs);
    const int wh      = (int)(Theme::WAVE_H * vs);
    const int btnH    = (int)(Theme::BTN_H  * vs);
    const int knobH   = (int)(Theme::ROW_H  * vs);
    const int labelH  = (int)(18 * vs);
    const int comboH  = (int)(24 * vs);
    const int panelW  = (int)(Theme::PANEL_W * vs);

    // Main area is to the left of the slot panel
    const int mainRight = getWidth() - panelW;
    const int w = mainRight - 2 * p;

    // Waveform
    waveformDisplay.setBounds (p, p, w, wh);

    // Load button + detect button + file label
    const int barY    = p + wh + p;
    const bool sliceOn = sliceModeButton.getToggleState();
    loadButton.setBounds (p, barY, (int)(110 * vs), btnH);
    if (sliceOn)
    {
        detectTransientsButton.setBounds (p + (int)(118 * vs), barY, (int)(150 * vs), btnH);
        fileLabel.setBounds (p + (int)(276 * vs), barY, w - (int)(262 * vs), btnH);
    }
    else
    {
        fileLabel.setBounds (p + (int)(118 * vs), barY, w - (int)(118 * vs), btnH);
    }

    // Knob row — 10 columns in gran mode, 7 otherwise
    const bool granMode = granEnabledButton.getToggleState();
    const int knobRowY  = barY + btnH + p;
    const int numCols   = granMode ? 10 : 7;
    const int knobW     = juce::jmin ((int)(Theme::KNOB_W * vs), w / numCols - 4);
    const int spacing   = w / numCols;

    auto placeKnob = [&] (juce::Slider& sl, juce::Label& l, int col) {
        const int x = p + col * spacing + (spacing - knobW) / 2;
        sl.setBounds (x, knobRowY, knobW, knobH - labelH);
        l.setBounds  (x, knobRowY + knobH - labelH, knobW, labelH);
    };
    auto placeCurveKnob = [&] (CurveKnob* k, int col) {
        if (k == nullptr) return;
        const int x = p + col * spacing + (spacing - knobW) / 2;
        k->setBounds (x, knobRowY, knobW, knobH);
    };

    if (sliceOn)
    {
        const int x = p + 0 * spacing + (spacing - knobW) / 2;
        sliceIndexSlider.setBounds (x, knobRowY, knobW, knobH - labelH);
        sliceIndexLabel.setBounds  (x, knobRowY + knobH - labelH, knobW, labelH);
    }
    else
    {
        placeCurveKnob (startPosCurveKnob.get(), 0);
    }

    placeKnob (endPosSlider,       endPosLabel,       1);

    // Column 2: small "Fine" trim knob stacked above the Speed CurveKnob
    {
        const int x2        = p + 2 * spacing + (spacing - knobW) / 2;
        const int fineKnobH = (int)(28 * vs);
        const int fineLblH  = (int)(14 * vs);
        const int fineAreaH = fineKnobH + fineLblH;
        const int fineKnobW = knobW / 2;
        const int fineX     = x2 + (knobW - fineKnobW) / 2;
        speedFineSlider.setBounds (fineX, knobRowY,               fineKnobW, fineKnobH);
        speedFineLabel.setBounds  (x2,    knobRowY + fineKnobH,   knobW,     fineLblH);
        if (speedCurveKnob)
            speedCurveKnob->setBounds (x2, knobRowY + fineAreaH, knobW, knobH - fineAreaH);
    }

    placeCurveKnob (pitchCurveKnob.get(), 3);
    placeKnob (numBeatsSlider,     numBeatsLabel,     4);
    placeKnob (grainJitterSlider,  grainJitterLabel,  5);

    placeCurveKnob (granPosCurveKnob.get(), 0);
    placeKnob (granPosJitterSlider, granPosJitterLabel, 1);
    placeKnob (granDensitySlider,   granDensityLabel,   2);
    placeKnob (granSizeSlider,      granSizeLabel,      4);
    placeKnob (granScanLenSlider, granScanLenLabel, 6);
    placeKnob (granScanSpdSlider, granScanSpdLabel, 7);
    placeKnob (granScanDepSlider, granScanDepLabel, 8);
    placeKnob (granProbSlider,    granProbLabel,    9);

    // Toggle row
    const int toggleRowY = knobRowY + knobH + p;
    const int toggleW    = w / 8;

    loopButton.setBounds         (p + 0 * toggleW, toggleRowY, toggleW - 4, btnH);
    gateButton.setBounds         (p + 1 * toggleW, toggleRowY, toggleW - 4, btnH);
    bpmSyncButton.setBounds      (p + 2 * toggleW, toggleRowY, toggleW - 4, btnH);
    tsModeLabel.setBounds        (p + 3 * toggleW, toggleRowY, toggleW - 4, btnH);
    tsModeCombo.setBounds        (p + 3 * toggleW, toggleRowY + btnH + (int)(4 * vs), toggleW - 4, comboH);
    speedSyncButton.setBounds    (p + 4 * toggleW, toggleRowY, toggleW - 4, btnH);
    sliceModeButton.setBounds    (p + 5 * toggleW, toggleRowY, toggleW - 4, btnH);
    granEnabledButton.setBounds  (p + 6 * toggleW, toggleRowY, toggleW - 4, btnH);
    midiNoteModeButton.setBounds (p + 7 * toggleW, toggleRowY, toggleW - 4, btnH);

    // Resonator section
    const int resoSectionY = toggleRowY + btnH + (int)(4 * vs) + comboH + p * 2;
    const int resoToggleY  = resoSectionY + (int)(16 * vs);

    resoEnabledButton.setBounds     (p,                 resoToggleY, (int)(120 * vs), btnH);
    resoHarmonicsLabel.setBounds    (p + (int)(130*vs), resoToggleY, (int)(70  * vs), btnH);
    resoHarmonicsCombo.setBounds    (p + (int)(200*vs), resoToggleY, (int)(60  * vs), comboH);
    resoSeriesLabel.setBounds       (p + (int)(278*vs), resoToggleY, (int)(50  * vs), btnH);
    resoSeriesCombo.setBounds       (p + (int)(328*vs), resoToggleY, (int)(60  * vs), comboH);

    const int resoKnobY   = resoToggleY + btnH + (int)(6 * vs);
    const int resoSpacing = w / 11;
    const int resoKnobW   = juce::jmin ((int)(60 * vs), resoSpacing - 2);

    auto placeResoKnob = [&] (juce::Slider& sl, juce::Label& l, int col) {
        const int x = p + col * resoSpacing + (resoSpacing - resoKnobW) / 2;
        sl.setBounds (x, resoKnobY, resoKnobW, knobH - labelH);
        l.setBounds  (x, resoKnobY + knobH - labelH, resoKnobW, labelH);
    };
    placeResoKnob (resoRootSlider,     resoRootLabel,     0);
    placeResoKnob (resoQSlider,        resoQLabel,        1);
    placeResoKnob (resoDecaySlider,    resoDecayLabel,    2);
    placeResoKnob (resoTaperSlider,    resoTaperLabel,    3);
    placeResoKnob (resoQTaperSlider,   resoQTaperLabel,   4);
    placeResoKnob (resoInharmSlider,   resoInharmLabel,   5);
    placeResoKnob (resoDriveSlider,    resoDriveLabel,    6);
    placeResoKnob (resoScatterSlider,  resoScatterLabel,  7);
    placeResoKnob (resoTimeSlider,     resoTimeLabel,     8);
    placeResoKnob (resoFeedbackSlider, resoFeedbackLabel, 9);
    placeResoKnob (resoMixSlider,      resoMixLabel,      10);

    // Character section
    const int charSectionY = resoKnobY + knobH + p;
    const int charRowY     = charSectionY + (int)(20 * vs);
    const int charKnobH    = (int)(50 * vs);
    const int charKnobW    = (int)(50 * vs);

    hissEnabledButton.setBounds (p, charRowY + (charKnobH - btnH) / 2, (int)(90 * vs), btnH);
    const int satX = p + (int)(94 * vs);
    outputSatSlider.setBounds (satX, charRowY, charKnobW, charKnobH - labelH);
    outputSatLabel.setBounds  (satX, charRowY + charKnobH - labelH, charKnobW, labelH);
    // Envelope section
    const int envSectionY = charRowY + charKnobH + p;
    const int envRowY     = envSectionY + (int)(16 * vs);
    const int envKnobH    = (int)(50 * vs);
    const int envLabelH   = (int)(14 * vs);
    const int envKnobW    = (int)(50 * vs);
    envEnabledButton.setBounds (p, envRowY + (envKnobH - btnH) / 2, (int)(90 * vs), btnH);
    {
        int ex = p + (int)(94 * vs);
        envAttackSlider.setBounds  (ex, envRowY, envKnobW, envKnobH - envLabelH);
        envAttackLabel.setBounds   (ex, envRowY + envKnobH - envLabelH, envKnobW, envLabelH);
        ex += envKnobW + (int)(4 * vs);
        envDecaySlider.setBounds   (ex, envRowY, envKnobW, envKnobH - envLabelH);
        envDecayLabel.setBounds    (ex, envRowY + envKnobH - envLabelH, envKnobW, envLabelH);
        ex += envKnobW + (int)(4 * vs);
        envSustainSlider.setBounds (ex, envRowY, envKnobW, envKnobH - envLabelH);
        envSustainLabel.setBounds  (ex, envRowY + envKnobH - envLabelH, envKnobW, envLabelH);
        ex += envKnobW + (int)(4 * vs);
        envReleaseSlider.setBounds (ex, envRowY, envKnobW, envKnobH - envLabelH);
        envReleaseLabel.setBounds  (ex, envRowY + envKnobH - envLabelH, envKnobW, envLabelH);
    }

    // Samples section bar
    const int sampleBarY = envRowY + envKnobH + p;
    const int sampleBarH = btnH;

    sampleModeLabel.setBounds (p,                 sampleBarY + (sampleBarH - labelH) / 2, (int)(38 * vs), labelH);
    sampleModeCombo.setBounds (p + (int)(42*vs),  sampleBarY + (sampleBarH - comboH) / 2, (int)(96 * vs), comboH);
    polyVoicesLabel.setBounds (p + (int)(148*vs), sampleBarY + (sampleBarH - labelH) / 2, (int)(28 * vs), labelH);
    polyVoicesCombo.setBounds (p + (int)(178*vs), sampleBarY + (sampleBarH - comboH) / 2, (int)(86 * vs), comboH);
    morphPosLabel.setBounds   (p + (int)(274*vs), sampleBarY + (sampleBarH - labelH) / 2, (int)(44 * vs), labelH);
    {
        const int lpcW = (int)(42 * vs);
        const int gap  = (int)(4  * vs);
        morphPosSlider.setBounds (p + (int)(320*vs), sampleBarY + (sampleBarH - comboH) / 2,
                                   w - (int)(320*vs) - lpcW - gap, comboH);
        lpcMorphBtn.setBounds    (p + w - lpcW, sampleBarY + (sampleBarH - comboH) / 2,
                                   lpcW, comboH);
    }

    //==========================================================================
    // Slot panel (right side, always visible)
    const int panX      = mainRight;
    const int panPad    = (int)(8 * vs);
    const int innerX    = panX + panPad;
    const int innerW    = panelW - 2 * panPad;
    const int headerH   = (int)(22 * vs);
    const int totalH    = getHeight() - 2 * panPad - headerH;
    const int rowH      = totalH / 8;

    const int volW      = (int)(28 * vs);
    const int noteW     = (int)(24 * vs);
    const int fileBtnW  = innerW - volW - noteW - noteW - 8;
    const int ctrlH     = (int)(22 * vs);

    for (int i = 0; i < 8; ++i)
    {
        const int rowY  = panPad + headerH + i * rowH;
        const int ctrlY = rowY + (rowH - ctrlH) / 2;
        const int fileX = innerX + volW + 4;
        const int loX   = fileX + fileBtnW + 4;
        const int hiX   = loX + noteW + 4;

        slotVolSliders[i].setBounds (innerX,  ctrlY, volW,     ctrlH);
        slotFileButtons[i].setBounds (fileX,  ctrlY, fileBtnW, ctrlH);
        slotLoLabels[i].setBounds   (loX,     ctrlY, noteW,    ctrlH);
        slotHiLabels[i].setBounds   (hiX,     ctrlY, noteW,    ctrlH);
    }
}

//==============================================================================
// Paint
//==============================================================================

void BPMSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    const float vs        = (float)getHeight() / (float)Theme::H;
    const int   p         = (int)(Theme::PAD    * vs);
    const int   wh        = (int)(Theme::WAVE_H * vs);
    const int   btnH      = (int)(Theme::BTN_H  * vs);
    const int   knobH     = (int)(Theme::ROW_H  * vs);
    const int   comboH    = (int)(24 * vs);
    const int   barY      = p + wh + p;
    const int   knobRowY  = barY + btnH + p;
    const int   toggleRowY= knobRowY + knobH + p;
    const int   panelW    = (int)(Theme::PANEL_W * vs);
    const int   panX      = getWidth() - panelW;

    g.setColour (Theme::textDim);
    g.setFont (juce::Font (11.0f).italicised());
    g.drawText ("PLAYBACK",  p, toggleRowY - (int)(16 * vs), 100, (int)(14 * vs),
                juce::Justification::centredLeft);

    const int resoSectionY = toggleRowY + btnH + (int)(4 * vs) + comboH + p * 2;
    g.drawText ("RESONATOR", p, resoSectionY, 100, (int)(14 * vs),
                juce::Justification::centredLeft);

    const int resoToggleY2 = resoSectionY + (int)(16 * vs);
    const int resoKnobY2   = resoToggleY2 + btnH + (int)(6 * vs);
    const int charSectionY = resoKnobY2 + knobH + p;
    g.drawText ("CHARACTER", p, charSectionY, 100, (int)(14 * vs),
                juce::Justification::centredLeft);

    const int charRowY2  = charSectionY + (int)(20 * vs);
    const int charKnobH  = (int)(50 * vs);
    const int envSecY2   = charRowY2 + charKnobH + p;
    g.drawText ("ENVELOPE", p, envSecY2, 100, (int)(14 * vs), juce::Justification::centredLeft);
    const int envRowY2   = envSecY2 + (int)(16 * vs);
    const int envKnobH2  = (int)(50 * vs);
    g.drawText ("SAMPLES", p, envRowY2 + envKnobH2 + p - (int)(14 * vs), 100, (int)(14 * vs),
                juce::Justification::centredLeft);

    // Slot panel background
    g.setColour (Theme::surface);
    g.fillRect (panX, 0, panelW, getHeight());

    g.setColour (Theme::accentDim);
    g.drawVerticalLine (panX, 0.0f, (float)getHeight());

    // Panel header
    const int panPad  = (int)(8 * vs);
    const int headerH = (int)(22 * vs);
    const int innerX  = panX + panPad;
    const int innerW  = panelW - 2 * panPad;

    g.setColour (Theme::textDim);
    g.setFont (juce::Font (10.0f).italicised());
    g.drawText ("SLOTS", innerX, panPad, innerW, headerH - 4, juce::Justification::centredLeft);

    const int volW     = (int)(28 * vs);
    const int noteW    = (int)(24 * vs);
    const int fileBtnW = innerW - volW - noteW - noteW - 8;
    const int fileX    = innerX + volW + 4;
    const int loX      = fileX + fileBtnW + 4;
    const int hiX      = loX + noteW + 4;

    g.setFont (juce::Font (9.0f));
    g.drawText ("Vol", innerX, panPad, volW,  headerH - 4, juce::Justification::centred);
    g.drawText ("Lo",  loX,    panPad, noteW, headerH - 4, juce::Justification::centred);
    g.drawText ("Hi",  hiX,    panPad, noteW, headerH - 4, juce::Justification::centred);

    // Row zebra stripes + selection highlight
    const int totalH = getHeight() - 2 * panPad - headerH;
    const int rowH   = totalH / 8;
    for (int i = 0; i < 8; ++i)
    {
        const int rowY = panPad + headerH + i * rowH;
        if (i == currentSlot)
        {
            g.setColour (Theme::slotSelected.withAlpha (0.6f));
            g.fillRect (panX + 1, rowY, panelW - 1, rowH);
        }
        else if (i % 2 == 0)
        {
            g.setColour (juce::Colour (0x08ffffff));
            g.fillRect (panX + 1, rowY, panelW - 1, rowH);
        }
    }
}

//==============================================================================
// Mod dot overlay — drawn over the knob children at the modulated position
//==============================================================================

void BPMSamplerEditor::paintOverChildren (juce::Graphics& g)
{
    const float startAng = juce::MathConstants<float>::pi * 1.2f;
    const float endAng   = juce::MathConstants<float>::pi * 2.8f;

    auto drawModDot = [&] (CurveKnob* knob, const AudioModAssign& assign)
    {
        if (knob == nullptr || !knob->isVisible()) return;
        if (assign.channel < 0) return;

        const float instVal = processor.getModChannelInstValue (assign.channel);
        const float baseNorm = knob->getSliderNorm();
        const float modNorm  = juce::jlimit (0.f, 1.f, baseNorm + instVal * assign.depth);

        // Reproduce DarkLAF rotary geometry in editor-local coordinates.
        // The slider occupies the top (knobHeight - 18) pixels of the knob component.
        const auto kb  = knob->getBounds();  // editor-local
        const int  kx  = kb.getX(),  ky = kb.getY();
        const int  w   = kb.getWidth(), h = kb.getHeight() - 18;

        const float cx   = kx + w * 0.5f;
        const bool  isComp = h < 55;
        const float arcH = isComp ? h - 12.0f : (float)h;
        const float cy   = ky + arcH * 0.5f;
        const float r    = juce::jmin ((float)w, arcH) * 0.38f;
        const float dotR = juce::jmax (2.5f, r * 0.21f);

        const float angle = startAng + (endAng - startAng) * modNorm;
        const float tx = cx + r * std::sin (angle);
        const float ty = cy - r * std::cos (angle);

        g.setColour (juce::Colour (0xffff9900));
        g.fillEllipse (tx - dotR, ty - dotR, dotR * 2.f, dotR * 2.f);
    };

    drawModDot (pitchCurveKnob.get(),     processor.slotMod[currentSlot][kMod_Pitch]);
    drawModDot (speedCurveKnob.get(),     processor.slotMod[currentSlot][kMod_Speed]);
    drawModDot (granPosCurveKnob.get(),   processor.slotMod[currentSlot][kMod_GranPos]);
    drawModDot (startPosCurveKnob.get(),  processor.slotMod[currentSlot][kMod_StartPos]);
}

//==============================================================================
// Timer
//==============================================================================

void BPMSamplerEditor::timerCallback()
{
    const bool morphMode = (sampleModeCombo.getSelectedId() == 3);
    if (morphMode)
    {
        const int   sa   = processor.morphDisplaySa.load();
        const int   sb   = processor.morphDisplaySb.load();
        const float frac = processor.morphDisplayFrac.load();
        const float posA = (float)processor.morphDisplayPosA.load();
        const float posB = (float)processor.morphDisplayPosB.load();
        waveformDisplay.setMorphDisplay (
            processor.slotThumbnails[sa].get(), posA,
            processor.slotThumbnails[sb].get(), posB,
            frac);
    }
    else
    {
        waveformDisplay.setMorphDisplay (nullptr, 0.0f, nullptr, 0.0f, 0.0f);
        waveformDisplay.setPlaybackPos ((float)processor.getPlaybackPositionNorm (currentSlot));
    }

    const bool tsOn   = tsModeCombo.getSelectedId() > 1;
    const bool bpmOn  = bpmSyncButton.getToggleState();
    const bool granOn = granEnabledButton.getToggleState();
    const bool sliceOn= sliceModeButton.getToggleState();

    // Show/hide morph slider and LPC button
    if (morphPosSlider.isVisible() != morphMode)
    {
        morphPosSlider.setVisible (morphMode);
        morphPosLabel.setVisible  (morphMode);
        lpcMorphBtn.setVisible    (morphMode);
    }

    // Update slot panel labels every tick
    for (int i = 0; i < 8; ++i)
    {
        const auto& slot = processor.slots[i];
        const juce::String name = slot.hasFile()
            ? juce::File (slot.filePath).getFileName() : "(empty)";
        const juce::String btnText = "S" + juce::String (i) + ": " + name;
        if (slotFileButtons[i].getButtonText() != btnText)
            slotFileButtons[i].setButtonText (btnText);

        const juce::String loText = slot.loNote >= 0 ? juce::String (slot.loNote) : "--";
        const juce::String hiText = slot.hiNote >= 0 ? juce::String (slot.hiNote) : "--";
        if (!slotLoLabels[i].isBeingEdited() && slotLoLabels[i].getText() != loText)
            slotLoLabels[i].setText (loText, juce::dontSendNotification);
        if (!slotHiLabels[i].isBeingEdited() && slotHiLabels[i].getText() != hiText)
            slotHiLabels[i].setText (hiText, juce::dontSendNotification);
    }

    // Speed knob: disabled when BPM sync handles it
    if (speedCurveKnob) speedCurveKnob->setEnabled (!bpmOn);

    // Beats knob: only relevant when BPM sync is on
    numBeatsSlider.setEnabled (bpmOn);
    numBeatsLabel.setEnabled  (bpmOn);

    // Pitch knob: works in TS and granular modes
    if (pitchCurveKnob) pitchCurveKnob->setEnabled (tsOn || granOn);

    // Granular mode: 8 cols, swap cols 0/1/2/4, show feedback/prob at 6/7, hide analogJitter
    if (granPosCurveKnob && granPosCurveKnob->isVisible() != granOn)
    {
        granPosCurveKnob->setVisible (granOn);
        for (auto* sl : { &granPosJitterSlider, &granDensitySlider,  &granSizeSlider,
                          &granScanLenSlider,   &granScanSpdSlider,  &granScanDepSlider,
                          &granProbSlider })
            sl->setVisible (granOn);
        for (auto* l : { &granPosJitterLabel, &granDensityLabel,  &granSizeLabel,
                         &granScanLenLabel,   &granScanSpdLabel,  &granScanDepLabel,
                         &granProbLabel })
            l->setVisible (granOn);



        if (startPosCurveKnob) startPosCurveKnob->setVisible (!granOn && !sliceOn);
        endPosSlider.setVisible    (!granOn);
        endPosLabel.setVisible     (!granOn);
        if (speedCurveKnob) speedCurveKnob->setVisible (!granOn);
        speedFineSlider.setVisible (!granOn);
        speedFineLabel.setVisible  (!granOn);
        numBeatsSlider.setVisible  (!granOn);
        numBeatsLabel.setVisible   (!granOn);
        resized();
    }

    // Slice mode: swap start knob / detect button
    const bool startVisible = !sliceOn && !granOn;
    if (startPosCurveKnob && startPosCurveKnob->isVisible() != startVisible)
    {
        if (startPosCurveKnob) startPosCurveKnob->setVisible (startVisible);
        sliceIndexSlider.setVisible   (sliceOn && !granOn);
        sliceIndexLabel.setVisible    (sliceOn && !granOn);
        detectTransientsButton.setVisible (sliceOn);
        resized();
    }

    // Sync slice data to waveform display and slice index knob
    {
        const juce::ScopedLock sl (processor.slots[currentSlot].sliceLock);
        waveformDisplay.setSlices (processor.slots[currentSlot].slicePositions);

        const int numSlices = (int)processor.slots[currentSlot].slicePositions.size();
        const int maxIdx    = juce::jmax (0, numSlices - 1);
        if ((int)sliceIndexSlider.getMaximum() != maxIdx)
            sliceIndexSlider.setRange (0, maxIdx, 1);

        const juce::String sliceId = "s" + juce::String (currentSlot) + "_sliceIndex";
        const int paramVal = juce::jlimit (0, maxIdx,
            (int)*processor.apvts.getRawParameterValue (sliceId));
        if ((int)sliceIndexSlider.getValue() != paramVal)
            sliceIndexSlider.setValue (paramVal, juce::dontSendNotification);
    }
    waveformDisplay.setSliceMode (sliceOn);

    // Animate mod dot overlay whenever any mod channel is active
    bool anyModActive = false;
    const auto& sm = processor.slotMod[currentSlot];
    for (int t = 0; t < kNumSlotModTargets && !anyModActive; ++t)
        anyModActive = sm[t].channel >= 0;
    for (int t = 0; t < kNumGlobalModTargets && !anyModActive; ++t)
        anyModActive = processor.globalMod[t].channel >= 0;
    if (anyModActive)
        repaint();
}

//==============================================================================
// File loading
//==============================================================================

void BPMSamplerEditor::openFile()
{
    fileChooser = std::make_shared<juce::FileChooser> (
        "Load Audio File",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff;*.mp3;*.ogg;*.flac");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result.existsAsFile())
            {
                processor.loadAudioFile (result);
                fileLabel.setText (result.getFileNameWithoutExtension(),
                                   juce::dontSendNotification);
            }
        });
}

//==============================================================================
// Mod assignment helpers
//==============================================================================

void BPMSamplerEditor::showModAssignPanel (AudioModAssign& assign, const juce::String& name,
                                            juce::Component& anchor)
{
    juce::Component::SafePointer<BPMSamplerEditor> safeThis (this);
    auto panel = std::make_unique<ModAssignPanel> (assign, name, processor, [safeThis] {
        if (safeThis) safeThis->updateModIndicators();
    });
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

void BPMSamplerEditor::updateModIndicators()
{
    // Slot-specific mod labels � orange when assigned, default white otherwise
    auto col = [] (const AudioModAssign& a) {
        return a.channel >= 0 ? juce::Colour (0xffff9900) : juce::Colour (0xfff0f0ff);
    };

    if (pitchCurveKnob)  pitchCurveKnob->refreshButtonColour();
    if (speedCurveKnob)  speedCurveKnob->refreshButtonColour();
    if (granPosCurveKnob) granPosCurveKnob->refreshButtonColour();

    const int s = currentSlot;
    grainJitterLabel.setColour  (juce::Label::textColourId, col (processor.slotMod[s][kMod_GrainJitter]));
    granPosJitterLabel.setColour(juce::Label::textColourId, col (processor.slotMod[s][kMod_GranPosJit]));
    granSizeLabel.setColour     (juce::Label::textColourId, col (processor.slotMod[s][kMod_GranSize]));
    granDensityLabel.setColour  (juce::Label::textColourId, col (processor.slotMod[s][kMod_GranDensity]));
    granScanLenLabel.setColour  (juce::Label::textColourId, col (processor.slotMod[s][kMod_ScanLen]));
    granScanSpdLabel.setColour  (juce::Label::textColourId, col (processor.slotMod[s][kMod_ScanSpd]));
    granScanDepLabel.setColour  (juce::Label::textColourId, col (processor.slotMod[s][kMod_ScanDep]));
    resoRootLabel.setColour     (juce::Label::textColourId, col (processor.globalMod[kMod_ResoRoot]));
    resoInharmLabel.setColour   (juce::Label::textColourId, col (processor.globalMod[kMod_ResoInharm]));
}
