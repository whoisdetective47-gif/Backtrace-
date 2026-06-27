#pragma once
#include <JuceHeader.h>

// =============================================================================
//  Shared fade-curve shapes — used by BOTH the DSP (applyEdit) and the waveform
//  draw, so what you see is exactly what you hear.
//
//  btFadeGain(curve, t): t in [0,1], returns gain in [0,1]. g(0)=0, g(1)=1 and
//  monotonic for every curve, so a fade always reaches zero at the boundary —
//  click-free by construction.
// =============================================================================
enum BtFadeCurve { FadeLinear = 0, FadeExp = 1, FadeLog = 2, FadeSCurve = 3, FadeEqualPower = 4, kNumFadeCurves = 5 };

inline const char* btFadeCurveName(int c)
{
    static const char* n[] = { "Linear", "Exp", "Log", "S-Curve", "Equal Pwr" };
    return n[juce::jlimit(0, kNumFadeCurves - 1, c)];
}

inline float btFadeGain(int curve, float t) noexcept
{
    t = juce::jlimit(0.0f, 1.0f, t);
    switch (curve)
    {
        case FadeExp:        return t * t;                                      // slow start, concave
        case FadeLog:        return std::sqrt(t);                              // fast start, convex
        case FadeSCurve:     return t * t * (3.0f - 2.0f * t);                 // smoothstep
        case FadeEqualPower: return std::sin(t * juce::MathConstants<float>::halfPi);  // -3 dB midpoint
        case FadeLinear:
        default:             return t;
    }
}
