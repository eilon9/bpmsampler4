#pragma once
#include <JuceHeader.h>
#include "CurveData.h"

class CurveEditorComponent final : public juce::Component
{
public:
    CurveEditorComponent (BPMCurveData& d, const juce::String& paramName,
                          const juce::String& suffix = "");
    ~CurveEditorComponent() override;

    std::function<void()> onChange;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    BPMCurveData&    data;
    juce::String  suffix;

    juce::Label      titleLabel;
    juce::Label      minLabel, maxLabel;
    juce::TextButton resetCurveBtn { "Reset Curve" };
    juce::TextButton resetRangeBtn { "Reset Range" };

    juce::Rectangle<float> canvas;

    int  dragIdx        = -1;
    bool dragIsEndpoint = false;

    // Range label drag state
    bool  draggingMin    = false;
    bool  draggingMax    = false;
    float dragStartX     = 0.f;
    float dragStartVal   = 0.f;

    juce::Point<float> normToCanvas (float nx, float ny) const noexcept;
    juce::Point<float> canvasToNorm (float cx, float cy) const noexcept;
    int  nearestPoint (juce::Point<float> cp, float distPx) const noexcept;
    void updateRangeLabels();
    void notifyChange();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditorComponent)
};
