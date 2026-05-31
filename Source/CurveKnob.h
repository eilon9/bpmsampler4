#pragma once
#include <JuceHeader.h>
#include "CurveData.h"
#include "CurveEditorComponent.h"

class CurveKnob final : public juce::Component,
                        private juce::AudioProcessorParameter::Listener,
                        private juce::AsyncUpdater
{
public:
    CurveKnob (juce::RangedAudioParameter& param,
               BPMCurveData&               curveData,
               const juce::String&         labelText,
               const juce::String&         suffix = "");

    ~CurveKnob() override;

    std::function<void(float)> onValueChange;
    std::function<void()>      onCurveChanged;
    std::function<void()>      onRightClick;

    float getCurrentValue() const { return (float)slider.getValue(); }

    // Returns current slider position normalised to [0, 1] within the display range.
    float getSliderNorm() const noexcept
    {
        const float dr = curveData.displayMax - curveData.displayMin;
        return (dr > 1e-9f) ? juce::jlimit (0.f, 1.f,
            ((float)slider.getValue() - curveData.displayMin) / dr) : 0.f;
    }

    void applyRangeUpdate();
    void refreshButtonColour();

    void resized() override;

private:
    juce::RangedAudioParameter& param;
    BPMCurveData&               curveData;
    juce::String                suffix;

    juce::Slider     slider;
    juce::Label      label;
    juce::TextButton curveBtn { "~" };

    // Custom sync state — replaces SliderParameterAttachment
    std::atomic<float> latestParamNorm { 0.f };
    bool               ignoreSliderCallback = false;

    struct RightClickInterceptor : juce::MouseListener {
        CurveKnob& owner;
        explicit RightClickInterceptor (CurveKnob& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown() && owner.onRightClick)
                owner.onRightClick();
        }
    };
    RightClickInterceptor rightClickInterceptor { *this };

    void syncSliderFromParam (float paramNorm);
    void updateSliderRange();

    // juce::AudioProcessorParameter::Listener
    void parameterValueChanged (int, float newNorm) override;
    void parameterGestureChanged (int, bool) override {}

    // juce::AsyncUpdater — defers slider update to the message thread
    void handleAsyncUpdate() override;

    void openCurveEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveKnob)
};
