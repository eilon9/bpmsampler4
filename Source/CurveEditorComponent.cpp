#include "CurveEditorComponent.h"

namespace CEdTheme
{
    const juce::Colour bg       { 0xff12121e };
    const juce::Colour surface  { 0xff1e1e30 };
    const juce::Colour accent   { 0xff00d4ff };
    const juce::Colour grid     { 0xff2a2a40 };
    const juce::Colour refLine  { 0xff333355 };
    const juce::Colour curve    { 0xffffffff };
    const juce::Colour ptFill   { 0xff00d4ff };
    const juce::Colour ptEdge   { 0xffffffff };
    const juce::Colour textDim  { 0xff8888aa };
    const juce::Colour textMain { 0xfff0f0ff };
}

CurveEditorComponent::CurveEditorComponent (BPMCurveData& d,
                                             const juce::String& paramName,
                                             const juce::String& sfx)
    : data (d), suffix (sfx)
{
    setSize (260, 300);

    titleLabel.setText (paramName + "  — Curve Editor", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (12.f).boldened());
    titleLabel.setColour (juce::Label::textColourId, CEdTheme::textMain);
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    // Range labels (drag horizontally to adjust, double-click to type)
    for (auto* l : { &minLabel, &maxLabel })
    {
        l->setEditable (false, true);
        l->setFont (juce::Font (11.f));
        l->setColour (juce::Label::textColourId, CEdTheme::accent);
        l->setColour (juce::Label::backgroundColourId, CEdTheme::surface);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    minLabel.onTextChange = [this] {
        float v = juce::jlimit (data.absMin, data.displayMax - 0.001f,
                                minLabel.getText().getFloatValue());
        data.displayMin = v;
        updateRangeLabels();
        notifyChange();
        repaint();
    };
    maxLabel.onTextChange = [this] {
        float v = juce::jlimit (data.displayMin + 0.001f, data.absMax,
                                maxLabel.getText().getFloatValue());
        data.displayMax = v;
        updateRangeLabels();
        notifyChange();
        repaint();
    };

    resetCurveBtn.onClick = [this] {
        data.resetCurve();
        notifyChange();
        repaint();
    };
    resetRangeBtn.onClick = [this] {
        data.resetRange();
        updateRangeLabels();
        notifyChange();
        repaint();
    };

    for (auto* b : { &resetCurveBtn, &resetRangeBtn })
    {
        b->setColour (juce::TextButton::buttonColourId,  CEdTheme::surface);
        b->setColour (juce::TextButton::textColourOffId, CEdTheme::accent);
        addAndMakeVisible (b);
    }

    updateRangeLabels();
}

void CurveEditorComponent::resized()
{
    const int w = getWidth();
    titleLabel.setBounds (0, 4, w, 18);
    canvas = juce::Rectangle<float> (20.f, 28.f, (float)(w - 40), 185.f);

    const int rangeY = 222;
    minLabel.setBounds (20,          rangeY, 80, 20);
    maxLabel.setBounds (w - 100,     rangeY, 80, 20);

    const int btnY = rangeY + 26;
    resetCurveBtn.setBounds (20,         btnY, 100, 22);
    resetRangeBtn.setBounds (w - 120,    btnY, 100, 22);
}

void CurveEditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (CEdTheme::bg);

    const auto ci = canvas.toNearestInt();

    // Canvas background
    g.setColour (CEdTheme::surface);
    g.fillRect (canvas);

    // Grid
    g.setColour (CEdTheme::grid);
    for (int i = 1; i < 4; ++i)
    {
        const float nx = i * 0.25f;
        const float ny = i * 0.25f;
        const auto  px = normToCanvas (nx, 0.f);
        const auto  py = normToCanvas (0.f, ny);
        g.drawVerticalLine   ((int)px.x, canvas.getY(), canvas.getBottom());
        g.drawHorizontalLine ((int)py.y, canvas.getX(), canvas.getRight());
    }

    // Diagonal reference (linear)
    {
        const auto p0 = normToCanvas (0.f, 0.f);
        const auto p1 = normToCanvas (1.f, 1.f);
        g.setColour (CEdTheme::refLine);
        g.drawLine (p0.x, p0.y, p1.x, p1.y, 1.f);
    }

    // Curve (draw sorted copy)
    {
        auto sorted = data.points;
        std::sort (sorted.begin(), sorted.end(),
            [] (const BPMCurvePoint& a, const BPMCurvePoint& b) { return a.x < b.x; });

        juce::Path curvePath;
        if (!sorted.empty())
        {
            const auto first = normToCanvas (sorted[0].x, sorted[0].y);
            curvePath.startNewSubPath (first);
            for (int i = 1; i < (int)sorted.size(); ++i)
            {
                const auto pt = normToCanvas (sorted[i].x, sorted[i].y);
                curvePath.lineTo (pt);
            }
        }
        g.setColour (CEdTheme::curve);
        g.strokePath (curvePath, juce::PathStrokeType (1.5f));
    }

    // Breakpoints
    for (int i = 0; i < (int)data.points.size(); ++i)
    {
        const auto cp = normToCanvas (data.points[i].x, data.points[i].y);
        const bool isEnd = (std::abs (data.points[i].x) < 0.01f ||
                            std::abs (data.points[i].x - 1.f) < 0.01f);
        const float r = isEnd ? 5.f : 4.f;
        g.setColour (CEdTheme::ptFill);
        g.fillEllipse (cp.x - r, cp.y - r, r * 2.f, r * 2.f);
        g.setColour (CEdTheme::ptEdge);
        g.drawEllipse (cp.x - r, cp.y - r, r * 2.f, r * 2.f, 1.f);
    }

    // Canvas border
    g.setColour (CEdTheme::accent.withAlpha (0.4f));
    g.drawRect (canvas, 1.f);

    // Axis tick labels
    g.setColour (CEdTheme::textDim);
    g.setFont (9.f);
    const auto fmtVal = [&] (float v) -> juce::String {
        if (std::abs (v) >= 10.f) return juce::String ((int)std::round (v)) + suffix;
        return juce::String (v, 1) + suffix;
    };

    // X axis: min and max
    g.drawText (fmtVal (data.displayMin), ci.getX(), ci.getBottom() + 2, 40, 12,
                juce::Justification::left);
    g.drawText (fmtVal (data.displayMax), ci.getRight() - 40, ci.getBottom() + 2, 40, 12,
                juce::Justification::right);

    // Y axis: min and max
    g.drawText (fmtVal (data.displayMin), ci.getX() - 18, ci.getBottom() - 10, 16, 10,
                juce::Justification::right);
    g.drawText (fmtVal (data.displayMax), ci.getX() - 18, ci.getY(), 16, 10,
                juce::Justification::right);

    // Section labels
    g.setColour (CEdTheme::textDim);
    g.setFont (10.f);
    g.drawText ("Min", 20, 210, 80, 12, juce::Justification::left);
    g.drawText ("Max", getWidth() - 100, 210, 80, 12, juce::Justification::left);
}

//==============================================================================
CurveEditorComponent::~CurveEditorComponent()
{
    if (dragIdx >= 0)
    {
        data.sortAndFixEndpoints();
        notifyChange();
    }
}

void CurveEditorComponent::mouseDown (const juce::MouseEvent& e)
{
    if (!canvas.contains (e.position)) return;

    if (e.mods.isRightButtonDown())
    {
        const int idx = nearestPoint (e.position, 10.f);
        if (idx >= 0 && !(std::abs (data.points[idx].x) < 0.01f ||
                          std::abs (data.points[idx].x - 1.f) < 0.01f))
        {
            data.points.erase (data.points.begin() + idx);
            notifyChange();
            repaint();
        }
        return;
    }

    const int idx = nearestPoint (e.position, 10.f);
    if (idx >= 0)
    {
        dragIdx        = idx;
        dragIsEndpoint = (std::abs (data.points[idx].x) < 0.01f ||
                          std::abs (data.points[idx].x - 1.f) < 0.01f);
    }
    else
    {
        // Add new interior point
        const auto norm = canvasToNorm (e.position.x, e.position.y);
        const float nx  = juce::jlimit (0.01f, 0.99f, norm.x);
        const float ny  = juce::jlimit (0.f,   1.f,   norm.y);
        data.points.push_back ({ nx, ny });
        dragIdx        = (int)data.points.size() - 1;
        dragIsEndpoint = false;
        notifyChange();
        repaint();
    }
}

void CurveEditorComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0 || dragIdx >= (int)data.points.size()) return;

    const auto norm = canvasToNorm (e.position.x, e.position.y);

    if (dragIsEndpoint)
        data.points[dragIdx].y = juce::jlimit (0.f, 1.f, norm.y);
    else
    {
        data.points[dragIdx].x = juce::jlimit (0.01f, 0.99f, norm.x);
        data.points[dragIdx].y = juce::jlimit (0.f,   1.f,   norm.y);
    }
    repaint();
}

void CurveEditorComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragIdx >= 0)
    {
        data.sortAndFixEndpoints();
        dragIdx = -1;
        notifyChange();
        repaint();
    }
}

//==============================================================================
juce::Point<float> CurveEditorComponent::normToCanvas (float nx, float ny) const noexcept
{
    return { canvas.getX() + nx * canvas.getWidth(),
             canvas.getY() + (1.f - ny) * canvas.getHeight() };
}

juce::Point<float> CurveEditorComponent::canvasToNorm (float cx, float cy) const noexcept
{
    return { (cx - canvas.getX()) / canvas.getWidth(),
             1.f - (cy - canvas.getY()) / canvas.getHeight() };
}

int CurveEditorComponent::nearestPoint (juce::Point<float> cp, float distPx) const noexcept
{
    int   bestIdx  = -1;
    float bestDist = distPx * distPx;
    for (int i = 0; i < (int)data.points.size(); ++i)
    {
        const auto sp = normToCanvas (data.points[i].x, data.points[i].y);
        const float d = sp.getDistanceSquaredFrom (cp);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    return bestIdx;
}

void CurveEditorComponent::updateRangeLabels()
{
    const auto fmt = [] (float v) {
        if (std::abs (v) >= 10.f) return juce::String ((int)std::round (v));
        return juce::String (v, 2);
    };
    minLabel.setText (fmt (data.displayMin), juce::dontSendNotification);
    maxLabel.setText (fmt (data.displayMax), juce::dontSendNotification);
}

void CurveEditorComponent::notifyChange()
{
    if (onChange) onChange();
}
