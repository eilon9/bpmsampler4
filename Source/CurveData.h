#pragma once
#include <JuceHeader.h>
#include <vector>
#include <algorithm>

struct BPMCurvePoint { float x = 0.f, y = 0.f; };

struct BPMCurveData
{
    std::vector<BPMCurvePoint> points { {0.f, 0.f}, {1.f, 1.f} };
    float displayMin = 0.f, displayMax = 1.f;
    float absMin     = 0.f, absMax     = 1.f;

    BPMCurveData()
    {
        // Pre-reserve so push_back never reallocates — the audio thread reads
        // points directly, and a reallocation would invalidate the internal pointer.
        points.reserve (32);
    }

    static BPMCurveData create (float dMin, float dMax, float aMin, float aMax)
    {
        BPMCurveData d;
        d.displayMin = dMin; d.displayMax = dMax;
        d.absMin     = aMin; d.absMax     = aMax;
        return d;
    }

    // Piecewise-linear interpolation through the breakpoints.
    // Called from the audio thread — safe as long as points.data() is stable
    // (guaranteed by reserve(32) above, provided the user adds < 32 points).
    float evaluate (float nx) const noexcept
    {
        nx = juce::jlimit (0.f, 1.f, nx);
        const int n = (int)points.size();
        for (int i = 0; i < n - 1; ++i)
        {
            if (nx <= points[i + 1].x)
            {
                const float span = points[i + 1].x - points[i].x;
                if (span < 1e-7f) return points[i + 1].y;
                const float t = (nx - points[i].x) / span;
                return juce::jlimit (0.f, 1.f,
                    points[i].y + t * (points[i + 1].y - points[i].y));
            }
        }
        return points.empty() ? nx : points.back().y;
    }

    float inverseEvaluate (float ny) const noexcept
    {
        ny = juce::jlimit (0.f, 1.f, ny);
        float lo = 0.f, hi = 1.f;
        for (int i = 0; i < 32; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            (evaluate (mid) < ny ? lo : hi) = mid;
        }
        return (lo + hi) * 0.5f;
    }

    // Convert a normalized (0-1) parameter value to a curved display-range value.
    // paramNorm 0→1 maps to displayMin→displayMax, shaped by the curve.
    // Safe to call from the audio thread (reads points directly via evaluate()).
    float applyToNorm (float paramNorm) const noexcept
    {
        return displayMin
             + evaluate (juce::jlimit (0.f, 1.f, paramNorm))
               * (displayMax - displayMin);
    }

    bool isDefault() const noexcept
    {
        return points.size() == 2
            && points[0].x == 0.f && points[0].y == 0.f
            && points[1].x == 1.f && points[1].y == 1.f
            && displayMin == absMin && displayMax == absMax;
    }

    void resetCurve()
    {
        points = { {0.f, 0.f}, {1.f, 1.f} };
        points.reserve (32);
    }

    void resetRange() { displayMin = absMin; displayMax = absMax; }

    void sortAndFixEndpoints()
    {
        std::sort (points.begin(), points.end(),
            [] (const BPMCurvePoint& a, const BPMCurvePoint& b) { return a.x < b.x; });
        if (points.size() < 2) points = { {0.f, 0.f}, {1.f, 1.f} };
        points.front().x = 0.f;
        points.back().x  = 1.f;
    }

    void saveToXml (juce::XmlElement& xml) const
    {
        xml.setAttribute ("dMin", (double)displayMin);
        xml.setAttribute ("dMax", (double)displayMax);
        xml.setAttribute ("aMin", (double)absMin);
        xml.setAttribute ("aMax", (double)absMax);
        for (auto& p : points)
        {
            auto* pt = xml.createNewChildElement ("Pt");
            pt->setAttribute ("x", (double)p.x);
            pt->setAttribute ("y", (double)p.y);
        }
    }

    void loadFromXml (const juce::XmlElement& xml)
    {
        displayMin = (float)xml.getDoubleAttribute ("dMin", (double)displayMin);
        displayMax = (float)xml.getDoubleAttribute ("dMax", (double)displayMax);
        absMin     = (float)xml.getDoubleAttribute ("aMin", (double)absMin);
        absMax     = (float)xml.getDoubleAttribute ("aMax", (double)absMax);
        points.clear();
        for (auto* pt = xml.getFirstChildElement(); pt; pt = pt->getNextElement())
            if (pt->hasTagName ("Pt"))
                points.push_back ({ (float)pt->getDoubleAttribute ("x"),
                                    (float)pt->getDoubleAttribute ("y") });
        sortAndFixEndpoints();
        points.reserve (32);
    }
};
