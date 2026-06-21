#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Variable-speed playback pitch engine using a circular delay buffer.
//
// Concept: write audio into a circular buffer at the host rate (1 sample per clock).
// Read it back at a speed of `ratio` samples per clock, where
//   ratio = 2^(semitones/12)
// This mimics a sampler playing a recording faster or slower.
//
// At ratio < 1.0 (pitch down): read is slower than write. Over time the write
// pointer catches up to the read pointer, causing wrap-around repetition — a
// musically acceptable artifact that reinforces the vintage sampler character.
// A large buffer (3 s) keeps this from happening at typical pitch amounts.
//
// Linear interpolation gives the fractional read position a slight smoothness
// while preserving the deliberately un-clean character.
class SamplerPitchEngine
{
public:
    // ~10 ms initial gap between write and read — also the reported plugin latency
    static constexpr int kInitialDelay = 512;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr      = sampleRate;
        bufSize = static_cast<int>(sampleRate * 3.0) + 4;

        for (int ch = 0; ch < 2; ++ch)
        {
            buf[ch].assign(bufSize, 0.0f);
            writePos[ch] = kInitialDelay;
            readPos [ch] = 0.0;
        }

        smoothRatio.reset(sampleRate, 0.040);       // 40 ms default glide
        smoothRatio.setCurrentAndTargetValue(1.0f);
        targetRatio = 1.0f;
    }

    // semitones: -24 to +24,  fineCents: -100 to +100
    void setPitch(float semitones, float fineCents)
    {
        float total = semitones + fineCents * 0.01f;
        targetRatio = std::pow(2.0f, total / 12.0f);
        smoothRatio.setTargetValue(targetRatio);
    }

    // glideMs: how long the pitch ramp takes (sets the SmoothedValue ramp length)
    void setGlideTime(float glideMs)
    {
        float cur = smoothRatio.getCurrentValue();
        smoothRatio.reset(sr, static_cast<double>(glideMs) / 1000.0);
        smoothRatio.setCurrentAndTargetValue(cur);
        smoothRatio.setTargetValue(targetRatio);
    }

    int getLatencySamples() const { return kInitialDelay; }

    // Call once per sample to advance the smoother, returns ratio for this sample
    float advanceAndGetRatio()
    {
        return smoothRatio.getNextValue();
    }

    // Process one sample for a given channel using the pre-computed ratio
    float processSample(int channel, float input, float ratio)
    {
        jassert(channel >= 0 && channel < 2);

        // Write current input into the buffer
        buf[channel][writePos[channel]] = input;
        writePos[channel] = (writePos[channel] + 1) % bufSize;

        // Advance read pointer by ratio (keep state live even when bypassed)
        readPos[channel] += static_cast<double>(ratio);
        if (readPos[channel] >= bufSize) readPos[channel] -= bufSize;
        if (readPos[channel] <  0.0)    readPos[channel] += bufSize;

        // Clean fixed-delay tap, phase-aligned with the varispeed read at unity.
        // At ratio == 1 the read/write gap stays at kInitialDelay, so this tap and
        // the interpolated read coincide — the blend below is seamless.
        const int cleanIdx = (writePos[channel] - kInitialDelay + bufSize) % bufSize;
        const float cleanOut = buf[channel][cleanIdx];

        // Bypass blend: 1.0 = transparent (at unity), 0.0 = full varispeed read.
        const float blend = unityBlend(ratio);
        if (blend > 0.999f)
            return cleanOut;                       // transparent path, no interp dulling

        // Linear interpolation for fractional position
        int   i0   = static_cast<int>(readPos[channel]);
        int   i1   = (i0 + 1) % bufSize;
        float frac = static_cast<float>(readPos[channel] - i0);
        const float vari = buf[channel][i0] * (1.0f - frac) + buf[channel][i1] * frac;

        return cleanOut * blend + vari * (1.0f - blend);
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(buf[ch].begin(), buf[ch].end(), 0.0f);
            writePos[ch] = kInitialDelay;
            readPos [ch] = 0.0;
        }
        smoothRatio.setCurrentAndTargetValue(targetRatio);
    }

private:
    // Returns 1.0 when the ratio is at musical unity, ramping to 0.0 across a
    // ~1/4-semitone zone. Lets the engine fade to a clean tap near 0 st without
    // any hard switch (no click when gliding through unity).
    static float unityBlend(float ratio)
    {
        constexpr float lo = 0.0012f;   // ≈ 0.02 semitone  → still fully clean
        constexpr float hi = 0.0145f;   // ≈ 0.25 semitone  → full effect
        const float dev = std::abs(ratio - 1.0f);
        const float t   = std::clamp((dev - lo) / (hi - lo), 0.0f, 1.0f);
        return 1.0f - t;
    }

    double   sr         = 44100.0;
    int      bufSize    = 0;
    float    targetRatio = 1.0f;

    std::vector<float> buf[2];
    int    writePos[2] = { kInitialDelay, kInitialDelay };
    double readPos [2] = { 0.0, 0.0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothRatio;
};
