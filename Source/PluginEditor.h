#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformDisplay.h"
#include "CurveKnob.h"

//==============================================================================
class BPMSamplerEditor  : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit BPMSamplerEditor (BPMSamplerProcessor&);
    ~BPMSamplerEditor() override;

    void paint            (juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized          () override;

private:
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void openFile();
    void selectSlot (int idx);
    void rebindSlot();

    BPMSamplerProcessor& processor;
    int currentSlot = 0;

    //==========================================================================
    // Waveform
    WaveformDisplay waveformDisplay;

    // File
    juce::TextButton loadButton             { "Load File" };
    juce::TextButton detectTransientsButton { "Detect Transients" };
    juce::Label      fileLabel;

    //==========================================================================
    // CurveKnobs — per-slot, rebuilt by rebindSlot()
    std::unique_ptr<CurveKnob> pitchCurveKnob;
    std::unique_ptr<CurveKnob> speedCurveKnob;
    std::unique_ptr<CurveKnob> startPosCurveKnob;
    std::unique_ptr<CurveKnob> granPosCurveKnob;

    // Regular sliders (no curve editor)
    juce::Slider endPosSlider, numBeatsSlider, grainJitterSlider, speedFineSlider;
    juce::Label  endPosLabel,  numBeatsLabel,  grainJitterLabel,  speedFineLabel;

    // Slice index knob (replaces start knob in slice mode)
    juce::Slider sliceIndexSlider;
    juce::Label  sliceIndexLabel;

    // Toggles
    juce::ToggleButton loopButton         { "Loop" };
    juce::ToggleButton gateButton         { "Gate" };
    juce::ToggleButton bpmSyncButton      { "BPM Sync" };
    juce::ToggleButton speedSyncButton    { "Speed Sync" };
    juce::ToggleButton sliceModeButton    { "Slice Mode" };
    juce::ToggleButton midiNoteModeButton { "MIDI Note" };

    // Timestretch mode combo
    juce::ComboBox tsModeCombo;
    juce::Label    tsModeLabel;

    //==========================================================================
    // Resonator section (global params)
    juce::ToggleButton resoEnabledButton { "Resonator" };

    juce::Slider resoRootSlider, resoQSlider, resoTaperSlider;
    juce::Slider resoTimeSlider, resoFeedbackSlider, resoMixSlider;
    juce::Slider resoInharmSlider, resoQTaperSlider, resoDecaySlider;
    juce::Slider resoDriveSlider, resoScatterSlider;
    juce::Label  resoRootLabel, resoQLabel, resoTaperLabel;
    juce::Label  resoTimeLabel, resoFeedbackLabel, resoMixLabel;
    juce::Label  resoInharmLabel, resoQTaperLabel, resoDecayLabel;
    juce::Label  resoDriveLabel, resoScatterLabel;

    juce::ComboBox resoHarmonicsCombo;
    juce::Label    resoHarmonicsLabel;
    juce::ComboBox resoSeriesCombo;
    juce::Label    resoSeriesLabel;

    //==========================================================================
    // Character section (global params)
    juce::ToggleButton hissEnabledButton { "Hiss" };
    juce::Slider       outputSatSlider;
    juce::Label        outputSatLabel;

    //==========================================================================
    // Envelope section (per-slot)
    juce::ToggleButton envEnabledButton { "Envelope" };
    juce::Slider envAttackSlider, envDecaySlider, envSustainSlider, envReleaseSlider;
    juce::Label  envAttackLabel,  envDecayLabel,  envSustainLabel,  envReleaseLabel;

    //==========================================================================
    // Granular section (per-slot)
    juce::ToggleButton granEnabledButton { "Granular" };

    juce::Slider granPosJitterSlider, granDensitySlider, granSizeSlider;
    juce::Label  granPosJitterLabel,  granDensityLabel,  granSizeLabel;

    juce::Slider granScanLenSlider, granScanSpdSlider, granScanDepSlider, granProbSlider;
    juce::Label  granScanLenLabel,  granScanSpdLabel,  granScanDepLabel,  granProbLabel;

    //==========================================================================
    // Multi-sample section (global params)
    juce::ComboBox sampleModeCombo;
    juce::Label    sampleModeLabel;
    juce::ComboBox polyVoicesCombo;
    juce::Label    polyVoicesLabel;
    juce::Slider     morphPosSlider;
    juce::Label      morphPosLabel;
    juce::TextButton lpcMorphBtn { "LPC" };

    //==========================================================================
    // Slot panel — always visible on right side
    std::array<juce::TextButton, 8> slotFileButtons;
    std::array<juce::Label,      8> slotLoLabels;
    std::array<juce::Label,      8> slotHiLabels;
    std::array<juce::Slider,     8> slotVolSliders;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, 8> slotVolAttachments;
    std::array<std::shared_ptr<juce::FileChooser>, 8> slotFileChoosers;

    // Forwards mouseDown on Lo/Hi labels and vol sliders to selectSlot(i)
    struct RowSelectListener : juce::MouseListener {
        std::function<void()> callback;
        void mouseDown (const juce::MouseEvent&) override { if (callback) callback(); }
    };
    std::array<RowSelectListener, 8> rowListeners;

    //==========================================================================
    // Per-slot APVTS attachments — destroyed + rebuilt in rebindSlot()
    std::unique_ptr<juce::SliderParameterAttachment>   endPosAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   numBeatsAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   grainJitterAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   speedFineAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   loopAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   gateAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   bpmSyncAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   speedSyncAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   sliceModeAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   midiNoteModeAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> tsModeAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   granEnabledAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granPosJitterAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granDensityAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granSizeAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granScanLenAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granScanSpdAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granScanDepAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   granProbAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   envEnabledAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   envAttackAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   envDecayAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   envSustainAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   envReleaseAttach;

    // Global APVTS attachments — created once in constructor
    std::unique_ptr<juce::ButtonParameterAttachment>   resoEnabledAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoRootAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoQAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoTaperAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> resoHarmonicsAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoTimeAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoFeedbackAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoMixAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoInharmAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoQTaperAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> resoSeriesAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoDecayAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoDriveAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   resoScatterAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   hissEnabledAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   outputSatAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> sampleModeAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> polyVoicesAttach;
    std::unique_ptr<juce::SliderParameterAttachment>   morphPosAttach;
    std::unique_ptr<juce::ButtonParameterAttachment>   lpcMorphAttach;

    std::shared_ptr<juce::FileChooser> fileChooser;

    //==========================================================================
    // Audio-rate mod assignment UI
    void showModAssignPanel (AudioModAssign& assign, const juce::String& name,
                             juce::Component& anchor);
    void updateModIndicators();

    // One listener per modulatable regular slider — kept alive here
    struct SliderModListener : juce::MouseListener {
        BPMSamplerEditor& editor;
        bool              isGlobal;
        int               target;
        const char*       name;
        juce::Slider&     slider;
        juce::Label&      label;

        SliderModListener (BPMSamplerEditor& e, bool global, int t,
                           const char* n, juce::Slider& s, juce::Label& l)
            : editor(e), isGlobal(global), target(t), name(n), slider(s), label(l)
        { s.addMouseListener (this, false); }

        ~SliderModListener() { slider.removeMouseListener (this); }

        void mouseDown (const juce::MouseEvent& ev) override
        {
            if (!ev.mods.isRightButtonDown()) return;
            if (isGlobal)
                editor.showModAssignPanel (editor.processor.globalMod[target], name, slider);
            else
                editor.showModAssignPanel (editor.processor.slotMod[editor.currentSlot][target], name, slider);
        }
    };
    std::vector<std::unique_ptr<SliderModListener>> sliderModListeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BPMSamplerEditor)
};
