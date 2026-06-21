#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Two-stage stereo widener.
//
//   Stage A — Mid/Side width scaling for material that ALREADY has stereo info.
//   Stage B — Decorrelated artificial Side generated from Mid (all-pass
//             diffusion), so a MONO source actually gains width instead of just
//             having a (non-existent) Side boosted.
//
// Width control (0..100):  0 = mono,  50 = neutral/original,  100 = wide.
//
// All added width lives in the Side channel, so a mono fold-down (L + R) always
// collapses to 2*Mid → perfect mono compatibility, no comb, no hollowing. The
// artificial Side is high-passed at ~150 Hz so the low end stays centred and
// focused (bass doesn't smear). Mid passes through untouched, so the widener
// adds no latency to the main signal. Width is smoothed to avoid zipper.
class StereoWidener
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;

        // All-pass diffuser delays (samples) — spread, mutually prime-ish.
        const int   delays[kStages] = { 142, 277, 379 };
        const float gains [kStages] = { 0.55f, 0.62f, 0.5f };
        for (int s = 0; s < kStages; ++s)
            ap[s].prepare(delays[s], gains[s]);

        juce::dsp::ProcessSpec spec { sr, 512u, 1u };
        sideHP.prepare(spec);
        sideHP.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        sideHP.setCutoffFrequency(150.0f);   // keep lows mono / focused
        sideHP.reset();

        width.reset(sr, 0.02);
        width.setCurrentAndTargetValue(0.5f);  // neutral
    }

    void setWidth(float pct) { width.setTargetValue(std::clamp(pct * 0.01f, 0.0f, 1.0f)); }

    void reset()
    {
        for (auto& a : ap) a.reset();
        sideHP.reset();
    }

    // Process the final stereo output in place.
    void process(float* L, float* R, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float w    = width.getNextValue();
            const float mid  = (L[i] + R[i]) * 0.5f;
            const float side = (L[i] - R[i]) * 0.5f;

            // Decorrelated artificial side from mid (always run → no stale state)
            float dec = mid;
            for (int s = 0; s < kStages; ++s) dec = ap[s].process(dec);
            dec = sideHP.processSample(0, dec);

            // Stage A: scale existing side  (w=0 → mono, 0.5 → unity, 1 → 2×)
            // Stage B: add artificial side only above neutral
            const float wide    = std::max(0.0f, w - 0.5f) * 2.0f;   // 0..1
            const float outSide = side * (w * 2.0f) + dec * wide * kArtGain;

            // Mild level compensation so widening doesn't read as "louder"
            const float comp = 1.0f - 0.08f * wide;

            L[i] = (mid + outSide) * comp;
            R[i] = (mid - outSide) * comp;
        }
    }

private:
    static constexpr int   kStages  = 3;
    static constexpr float kArtGain = 0.7f;

    struct Allpass
    {
        std::vector<float> buf;
        int   idx = 0;
        float g   = 0.5f;
        void prepare(int d, float gain) { buf.assign((size_t)std::max(1, d), 0.0f); idx = 0; g = gain; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process(float x)
        {
            const float d = buf[(size_t)idx];
            const float y = -g * x + d;
            buf[(size_t)idx] = x + g * y;
            idx = (idx + 1) % (int)buf.size();
            return y;
        }
    };

    double  sampleRate = 44100.0;
    Allpass ap[kStages];
    juce::dsp::StateVariableTPTFilter<float> sideHP;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> width;
};
