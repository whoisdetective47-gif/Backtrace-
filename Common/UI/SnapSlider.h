#pragma once
#include <JuceHeader.h>

// Vertical fader that can snap to a predefined set of values.
// Snap is off by default; toggle snapEnabled at runtime.
// Used for PITCH (musical intervals) and SPEED (speed ratios).
class SnapSlider : public juce::Slider
{
public:
    // major == always show a label; minor points only label when active (keeps
    // a dense detent set readable on the fader).
    struct SnapPoint { double value; juce::String label; bool major = true; };

    bool snapEnabled = false;
    std::vector<SnapPoint> snapPoints;

    // Musical interval snap points — used by both the varispeed (authentic, in-tune)
    // fader and the modern pitch fader. Landmarks (root, P4, P5, octaves) are
    // labelled; the in-between intervals are detents that label only when selected.
    static std::vector<SnapPoint> musicalPoints()
    {
        return {
            { -24.0, "2OCT", true  },
            { -12.0, "OCT",  true  },
            { -10.0, "m7",   false },
            {  -7.0, "P5",   true  },
            {  -5.0, "P4",   true  },
            {  -4.0, "M3",   false },
            {  -3.0, "m3",   false },
            {  -2.0, "M2",   false },
            {  -1.0, "m2",   false },
            {   0.0, "ROOT", true  },
            {   1.0, "m2",   false },
            {   2.0, "M2",   false },
            {   3.0, "m3",   false },
            {   4.0, "M3",   false },
            {   5.0, "P4",   true  },
            {   7.0, "P5",   true  },
            {  10.0, "m7",   false },
            {  12.0, "OCT",  true  },
            {  24.0, "2OCT", true  },
        };
    }

    // Speed ratio snap points expressed in semitones (same param range as speed fader).
    // -24 = 1/4x, -12 = 1/2x, -7 ≈ 2/3x, 0 = 1x, +7 ≈ 3/2x, +12 = 2x, +24 = 4x
    static std::vector<SnapPoint> speedRatioPoints()
    {
        return {
            { -24.0, " 1/4" },
            { -12.0, " 1/2" },
            {  -7.0, " 2/3" },
            {   0.0, "  1x" },
            {   7.0, " 3/2" },
            {  12.0, "  2x" },
            {  24.0, "  4x" },
        };
    }

    double snapValue(double v, juce::Slider::DragMode /*dir*/) override
    {
        if (!snapEnabled || snapPoints.empty())
            return v;

        double best = snapPoints[0].value;
        double bestDist = std::abs(v - best);
        for (auto& p : snapPoints)
        {
            double d = std::abs(v - p.value);
            if (d < bestDist) { bestDist = d; best = p.value; }
        }
        return best;
    }
};
