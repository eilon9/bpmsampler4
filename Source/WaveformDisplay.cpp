#include "WaveformDisplay.h"

WaveformDisplay::WaveformDisplay (juce::AudioThumbnail* thumb)
    : thumbnail (thumb)
{
    setOpaque (true);
}

void WaveformDisplay::setThumbnail (juce::AudioThumbnail* thumb)
{
    thumbnail = thumb;
    repaint();
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Background
    g.fillAll (juce::Colour (0xff1a1a2e));

    // Active region highlight
    const float x0 = xFromNorm (startPos);
    const float x1 = xFromNorm (endPos);
    g.setColour (juce::Colour (0xff00d4ff).withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, bounds.getHeight()));

    // Waveform
    const bool isMorph = (morphThumbA != nullptr);
    if (isMorph)
    {
        const juce::Rectangle<int> waveRect = getLocalBounds().reduced (0, 4);
        if (morphThumbA->getTotalLength() > 0.0)
        {
            g.setColour (juce::Colour (0xff4a9eff).withAlpha (0.85f * (1.0f - morphFrac)));
            morphThumbA->drawChannels (g, waveRect, 0.0, morphThumbA->getTotalLength(), 1.0f);
        }
        if (morphThumbB != nullptr && morphThumbB->getTotalLength() > 0.0 && morphFrac > 0.001f)
        {
            g.setColour (juce::Colour (0xff4a9eff).withAlpha (0.85f * morphFrac));
            morphThumbB->drawChannels (g, waveRect, 0.0, morphThumbB->getTotalLength(), 1.0f);
        }
        if (morphThumbA->getTotalLength() == 0.0 && (morphThumbB == nullptr || morphThumbB->getTotalLength() == 0.0))
        {
            g.setColour (juce::Colour (0xff444466));
            g.setFont (14.0f);
            g.drawText ("No file loaded – press Load File", getLocalBounds(), juce::Justification::centred);
        }
    }
    else if (thumbnail != nullptr && thumbnail->getTotalLength() > 0.0)
    {
        g.setColour (juce::Colour (0xff4a9eff).withAlpha (0.85f));
        thumbnail->drawChannels (g, getLocalBounds().reduced (0, 4),
                                 0.0, thumbnail->getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff444466));
        g.setFont (14.0f);
        g.drawText ("No file loaded – press Load File", getLocalBounds(),
                    juce::Justification::centred);
    }

    if (!sliceMode)
    {
        // Start marker (green)
        g.setColour (juce::Colour (0xff00ff88));
        g.drawLine (x0, 0.0f, x0, bounds.getHeight(), 2.0f);
        juce::Path tri;
        tri.addTriangle (x0 - 6.0f, 0.0f, x0 + 6.0f, 0.0f, x0, 10.0f);
        g.fillPath (tri);

        // End marker (red)
        g.setColour (juce::Colour (0xffff4466));
        g.drawLine (x1, 0.0f, x1, bounds.getHeight(), 2.0f);
        juce::Path tri2;
        tri2.addTriangle (x1 - 6.0f, 0.0f, x1 + 6.0f, 0.0f, x1, 10.0f);
        g.fillPath (tri2);
    }
    else
    {
        // Slice markers (orange/yellow)
        for (int i = 0; i < (int)slices.size(); ++i)
        {
            const float sx = xFromNorm (slices[i]);
            g.setColour (juce::Colour (0xffffaa00));
            g.drawLine (sx, 0.0f, sx, bounds.getHeight(), 1.5f);
            // small downward triangle handle
            juce::Path tri;
            tri.addTriangle (sx - 5.0f, 0.0f, sx + 5.0f, 0.0f, sx, 9.0f);
            g.fillPath (tri);
            // index label
            g.setColour (juce::Colour (0xffffaa00).withAlpha (0.8f));
            g.setFont (10.0f);
            g.drawText (juce::String (i), (int)sx + 2, 2, 20, 12,
                        juce::Justification::centredLeft);
        }
    }

    // Playback cursor(s)
    if (isMorph)
    {
        // Slot A head fades out as morphFrac increases; slot B fades in
        const float cx = xFromNorm (morphPosA);
        g.setColour (juce::Colours::white.withAlpha (0.7f * (1.0f - morphFrac)));
        g.drawLine (cx, 0.0f, cx, bounds.getHeight(), 1.5f);

        if (morphFrac > 0.001f && morphThumbB != nullptr && morphThumbB->getTotalLength() > 0.0)
        {
            const float cx2 = xFromNorm (morphPosB);
            g.setColour (juce::Colours::white.withAlpha (0.7f * morphFrac));
            g.drawLine (cx2, 0.0f, cx2, bounds.getHeight(), 1.5f);
        }
    }
    else
    {
        const float cx = xFromNorm (playbackPos);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawLine (cx, 0.0f, cx, bounds.getHeight(), 1.5f);
    }

    // Border
    g.setColour (juce::Colour (0xff00d4ff).withAlpha (0.3f));
    g.drawRect (bounds, 1.0f);
}

//==============================================================================

float WaveformDisplay::normFromX (float x) const
{
    return juce::jlimit (0.0f, 1.0f, x / (float)getWidth());
}

float WaveformDisplay::xFromNorm (float n) const
{
    return juce::jlimit (0.0f, (float)getWidth(), n * (float)getWidth());
}

void WaveformDisplay::setStartPos (float normPos)
{
    startPos = juce::jlimit (0.0f, endPos - 0.001f, normPos);
    repaint();
}

void WaveformDisplay::setEndPos (float normPos)
{
    endPos = juce::jlimit (startPos + 0.001f, 1.0f, normPos);
    repaint();
}

void WaveformDisplay::setPlaybackPos (float normPos)
{
    playbackPos = normPos;
    repaint();
}

void WaveformDisplay::setSliceMode (bool enabled)
{
    sliceMode = enabled;
    repaint();
}

void WaveformDisplay::setSlices (const std::vector<float>& positions)
{
    slices = positions;
    repaint();
}

void WaveformDisplay::setMorphDisplay (juce::AudioThumbnail* thumbA, float posA,
                                       juce::AudioThumbnail* thumbB, float posB,
                                       float frac)
{
    morphThumbA = thumbA;
    morphThumbB = thumbB;
    morphPosA   = posA;
    morphPosB   = posB;
    morphFrac   = frac;
    repaint();
}

//==============================================================================

void WaveformDisplay::mouseDown (const juce::MouseEvent& e)
{
    const float mx = (float)e.x;

    if (sliceMode)
    {
        if (e.mods.isRightButtonDown())
        {
            // Right-click near a slice marker → remove it
            int closest = -1;
            float bestDist = MARKER_GRAB_PX;
            for (int i = 0; i < (int)slices.size(); ++i)
            {
                const float d = std::abs (mx - xFromNorm (slices[i]));
                if (d < bestDist) { bestDist = d; closest = i; }
            }
            if (closest >= 0 && onSliceRemoved)
                onSliceRemoved (closest);
            dragging = DragTarget::None;
            return;
        }

        // Left-click near a slice marker → start drag
        for (int i = 0; i < (int)slices.size(); ++i)
        {
            if (std::abs (mx - xFromNorm (slices[i])) <= MARKER_GRAB_PX)
            {
                dragging     = DragTarget::Slice;
                dragSlicePos = slices[i];
                return;
            }
        }

        dragging = DragTarget::None;
        return;
    }

    const float xs = xFromNorm (startPos);
    const float xe = xFromNorm (endPos);

    if (std::abs (mx - xs) <= MARKER_GRAB_PX)
        dragging = DragTarget::Start;
    else if (std::abs (mx - xe) <= MARKER_GRAB_PX)
        dragging = DragTarget::End;
    else
        dragging = DragTarget::None;
}

void WaveformDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const float n = normFromX ((float)e.x);

    if (dragging == DragTarget::Start)
    {
        startPos = juce::jlimit (0.0f, endPos - 0.001f, n);
        if (onStartChanged) onStartChanged (startPos);
        repaint();
    }
    else if (dragging == DragTarget::End)
    {
        endPos = juce::jlimit (startPos + 0.001f, 1.0f, n);
        if (onEndChanged) onEndChanged (endPos);
        repaint();
    }
    else if (dragging == DragTarget::Slice)
    {
        const float newPos = juce::jlimit (0.0f, 1.0f, n);
        if (onSliceMoved)
            onSliceMoved (dragSlicePos, newPos);
        dragSlicePos = newPos;
        repaint();
    }
}

void WaveformDisplay::mouseUp (const juce::MouseEvent&)
{
    dragging = DragTarget::None;
}

void WaveformDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (!sliceMode) return;

    // Don't add a new slice if the double-click landed on an existing marker
    const float mx = (float)e.x;
    for (auto& s : slices)
        if (std::abs (mx - xFromNorm (s)) <= MARKER_GRAB_PX * 2.0f)
            return;

    if (onSliceAdded)
        onSliceAdded (normFromX (mx));
}
