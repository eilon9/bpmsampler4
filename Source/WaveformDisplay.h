#pragma once
#include <JuceHeader.h>

class WaveformDisplay : public juce::Component
{
public:
    std::function<void(float)> onStartChanged;
    std::function<void(float)> onEndChanged;

    std::function<void(float)>        onSliceAdded;
    std::function<void(int)>          onSliceRemoved;
    std::function<void(float, float)> onSliceMoved;

    explicit WaveformDisplay (juce::AudioThumbnail* thumb = nullptr);
    ~WaveformDisplay() override = default;

    void setThumbnail (juce::AudioThumbnail* thumb);

    void paint (juce::Graphics& g) override;
    void resized() override {}

    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    void setStartPos    (float normPos);
    void setEndPos      (float normPos);
    void setPlaybackPos (float normPos);
    void setSliceMode   (bool enabled);
    void setSlices      (const std::vector<float>& positions);

    void setMorphDisplay (juce::AudioThumbnail* thumbA, float posA,
                          juce::AudioThumbnail* thumbB, float posB,
                          float frac);

private:
    juce::AudioThumbnail* thumbnail = nullptr;

    juce::AudioThumbnail* morphThumbA = nullptr;
    juce::AudioThumbnail* morphThumbB = nullptr;
    float morphPosA = 0.0f;
    float morphPosB = 0.0f;
    float morphFrac = 0.0f;

    float startPos    = 0.0f;
    float endPos      = 1.0f;
    float playbackPos = 0.0f;

    bool               sliceMode = false;
    std::vector<float> slices;

    enum class DragTarget { None, Start, End, Slice };
    DragTarget dragging     = DragTarget::None;
    float      dragSlicePos = -1.0f;

    static constexpr float MARKER_GRAB_PX = 8.0f;

    float normFromX (float x) const;
    float xFromNorm (float n) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
