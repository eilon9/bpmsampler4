#include "CurveKnob.h"

namespace CKTheme
{
    const juce::Colour surface  { 0xff16213e };
    const juce::Colour accent   { 0xff00d4ff };
    const juce::Colour accentDim{ 0xff0070a0 };
}

CurveKnob::CurveKnob (juce::RangedAudioParameter& p,
                       BPMCurveData&               cd,
                       const juce::String&         labelText,
                       const juce::String&         sfx)
    : param (p), curveData (cd), suffix (sfx)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    if (sfx.isNotEmpty()) slider.setTextValueSuffix (sfx);
    addAndMakeVisible (slider);

    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (11.f));
    addAndMakeVisible (label);

    curveBtn.setColour (juce::TextButton::buttonColourId,  CKTheme::surface);
    curveBtn.setColour (juce::TextButton::textColourOffId, CKTheme::accentDim);
    curveBtn.onClick = [this] { openCurveEditor(); };
    addAndMakeVisible (curveBtn);

    // Set initial slider range and sync to current parameter value.
    updateSliderRange();

    // When the user drags the knob, write a normalised value into the parameter:
    //   paramNorm = (sliderValue - displayMin) / (displayMax - displayMin)
    // This makes CC 0→1 and the knob full-throw both map to displayMin→displayMax.
    slider.onValueChange = [this]
    {
        const float dr   = curveData.displayMax - curveData.displayMin;
        const float norm = (dr > 1e-9f)
            ? juce::jlimit (0.f, 1.f, ((float)slider.getValue() - curveData.displayMin) / dr)
            : 0.f;
        if (!ignoreSliderCallback)
            param.setValueNotifyingHost (norm);
        if (onValueChange) onValueChange (curveData.applyToNorm (norm));
    };

    slider.addMouseListener (&rightClickInterceptor, false);

    // Listen for parameter changes coming from CC / automation / DAW.
    param.addListener (this);
}

CurveKnob::~CurveKnob()
{
    slider.removeMouseListener (&rightClickInterceptor);
    cancelPendingUpdate();
    param.removeListener (this);
}

//==============================================================================
void CurveKnob::resized()
{
    const auto b    = getLocalBounds();
    const int  lblH = 18;
    const int  btnW = 14, btnH = 10;

    slider.setBounds   (b.getX(), b.getY(), b.getWidth(), b.getHeight() - lblH);
    label.setBounds    (b.getX(), b.getBottom() - lblH, b.getWidth(), lblH);
    curveBtn.setBounds (b.getRight() - btnW - 2, b.getY() + 2, btnW, btnH);
}

//==============================================================================
// Called on any thread when the parameter value changes (CC, automation, etc.)
void CurveKnob::parameterValueChanged (int, float newNorm)
{
    latestParamNorm.store (newNorm);
    triggerAsyncUpdate();
}

// Runs on the message thread — safe to update the slider here.
void CurveKnob::handleAsyncUpdate()
{
    syncSliderFromParam (latestParamNorm.load());
}

// Update the slider to reflect paramNorm without writing back to the parameter.
void CurveKnob::syncSliderFromParam (float paramNorm)
{
    const float displayVal = juce::jlimit (
        curveData.displayMin, curveData.displayMax,
        curveData.displayMin + paramNorm * (curveData.displayMax - curveData.displayMin));

    ignoreSliderCallback = true;
    slider.setValue ((double)displayVal, juce::sendNotificationSync);
    ignoreSliderCallback = false;
}

//==============================================================================
void CurveKnob::updateSliderRange()
{
    ignoreSliderCallback = true;
    slider.setRange ((double)curveData.displayMin, (double)curveData.displayMax, 0.0);
    syncSliderFromParam (param.getValue());
    ignoreSliderCallback = false;
}

void CurveKnob::applyRangeUpdate()
{
    updateSliderRange();
    refreshButtonColour();
    if (onCurveChanged) onCurveChanged();
}

void CurveKnob::refreshButtonColour()
{
    const bool modified = !curveData.isDefault();
    curveBtn.setColour (juce::TextButton::textColourOffId,
                        modified ? CKTheme::accent : CKTheme::accentDim);
    repaint();
}

//==============================================================================
void CurveKnob::openCurveEditor()
{
    auto editor = std::make_unique<CurveEditorComponent> (curveData, label.getText(), suffix);
    juce::Component::SafePointer<CurveKnob> safeThis (this);
    editor->onChange = [safeThis] {
        if (safeThis) safeThis->applyRangeUpdate();
    };
    juce::CallOutBox::launchAsynchronously (std::move (editor),
                                            curveBtn.getScreenBounds(),
                                            nullptr);
}
