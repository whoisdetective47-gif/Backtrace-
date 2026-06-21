#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Two-tap delay-line pitch shifter ("rotating tape-head" / Doppler design).
//
// WHY THIS REPLACES THE OLD ENGINE:
// The previous dual-head OLA reset its read heads to a fixed delay every grain,
// which reintroduced a near-real-time (UNSHIFTED) copy of the signal. That copy
// was the audible "ghost note" on bass and the source of beating/detuning on
// melodic material — the output was always shifted-pitch PLUS original-pitch.
//
// HOW THIS ONE WORKS (and why it's in tune with no ghost):
// A single phase ramp advances the read delay by (1 - ratio) per sample, so the
// buffer is read at EXACTLY `ratio` samples per output sample. Reading a stream
// at rate `ratio` resamples it by `ratio` → the pitch ratio is exactly
// 2^(semitones/12). Two read taps half a grain apart are crossfaded with
// complementary windows (each window is zero exactly at its tap's wrap point),
// so the ramp discontinuity is masked. There is no fixed-delay tap anywhere in
// the shifted path, so there is no unshifted leakage — no ghost, no detuning.
//
// Some granular texture remains on large shifts (inherent to time-domain pitch
// shifting), but small and musical shifts are smooth and accurate. Left and
// right share one phase, so the shift is identical per channel (mono-safe).
class ModernPitchShifter
{
public:
    void prepare(double sr, int /*maxBlock*/)
    {
        sampleRate = sr;
        grainSize  = std::max(512.0f, static_cast<float>(sr * 0.050)); // ~50 ms grain
        baseOffset = 4.0f;
        bufSz      = static_cast<int>(grainSize * 2.0f) + 16;
        latency    = static_cast<int>(grainSize * 0.5f + baseOffset);  // group delay

        for (int ch = 0; ch < 2; ++ch)
        {
            buf[ch].assign(static_cast<size_t>(bufSz), 0.0f);
            writePos[ch] = 0;
        }
        phase       = 0.0f;
        targetRatio = 1.0f;
        smoothRatio.reset(sr, 0.040);
        smoothRatio.setCurrentAndTargetValue(1.0f);
    }

    void setPitch(float semitones)
    {
        targetRatio = std::pow(2.0f, semitones / 12.0f);
        smoothRatio.setTargetValue(targetRatio);
    }

    void setGlideTime(float ms)
    {
        float cur = smoothRatio.getCurrentValue();
        smoothRatio.reset(sampleRate, static_cast<double>(ms) / 1000.0);
        smoothRatio.setCurrentAndTargetValue(cur);
        smoothRatio.setTargetValue(targetRatio);
    }

    int getLatencySamples() const { return latency; }

    // Advance the ratio smoother + phase ramp once per sample (shared by channels).
    float advanceAndGetRatio()
    {
        const float r = smoothRatio.getNextValue();
        // read advances at `r` per sample  ⇔  delay changes by (1 - r) per sample
        phase += (1.0f - r) / grainSize;
        while (phase >= 1.0f) phase -= 1.0f;
        while (phase <  0.0f) phase += 1.0f;
        return r;
    }

    float processSample(int ch, float input, float ratio)
    {
        auto& b = buf[ch];
        const int wp = writePos[ch];
        b[static_cast<size_t>(wp)] = input;
        writePos[ch] = (wp + 1) % bufSz;

        // Clean fixed-delay tap — transparent at unity, phase-aligned with latency
        const int   cleanIdx = (wp - latency + bufSz) % bufSz;
        const float cleanOut = b[static_cast<size_t>(cleanIdx)];

        const float blend = unityBlend(ratio);
        if (blend > 0.999f)
            return cleanOut;                       // exact unity — no processing

        // Two read taps, half a grain apart, BOTH advancing at `ratio`
        const float d0 = baseOffset + phase * grainSize;
        float ph1 = phase + 0.5f;
        if (ph1 >= 1.0f) ph1 -= 1.0f;
        const float d1 = baseOffset + ph1 * grainSize;

        const float r0 = readInterp(b, static_cast<float>(wp) - d0);
        const float r1 = readInterp(b, static_cast<float>(wp) - d1);

        // Complementary windows (sum = 1); each is zero at its tap's wrap point,
        // so the ramp discontinuity is fully masked.
        const float w0      = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * phase));
        const float shifted = r0 * w0 + r1 * (1.0f - w0);

        return cleanOut * blend + shifted * (1.0f - blend);
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(buf[ch].begin(), buf[ch].end(), 0.0f);
            writePos[ch] = 0;
        }
        phase = 0.0f;
        smoothRatio.setCurrentAndTargetValue(targetRatio);
    }

private:
    static float unityBlend(float ratio)
    {
        constexpr float lo = 0.0012f, hi = 0.0145f;  // ~0.02 → ~0.25 semitone
        const float dev = std::abs(ratio - 1.0f);
        const float t   = std::clamp((dev - lo) / (hi - lo), 0.0f, 1.0f);
        return 1.0f - t;
    }

    float readInterp(const std::vector<float>& b, float pos) const
    {
        while (pos <  0.0f)                       pos += static_cast<float>(bufSz);
        while (pos >= static_cast<float>(bufSz))  pos -= static_cast<float>(bufSz);
        const int   i0   = static_cast<int>(pos);
        const int   i1   = (i0 + 1) % bufSz;
        const float frac = pos - static_cast<float>(i0);
        return b[static_cast<size_t>(i0)] * (1.0f - frac)
             + b[static_cast<size_t>(i1)] * frac;
    }

    double sampleRate  = 44100.0;
    float  grainSize   = 2205.0f;
    float  baseOffset  = 4.0f;
    int    bufSz       = 4416;
    int    latency     = 1106;
    float  phase       = 0.0f;
    float  targetRatio = 1.0f;

    std::vector<float> buf[2];
    int    writePos[2] = { 0, 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothRatio;
};
