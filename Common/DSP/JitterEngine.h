#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

// Jitter — unstable digital sample-clock / sample-position instability.
//
// A short fractional delay per channel whose read position follows a piecewise-
// linear random walk ("sample-and-ramp"): a new random target offset is picked
// every `updateInterval` samples and the read position ramps linearly to it.
// This produces erratic micro pitch/timing imperfection — the character of an
// unstable early-digital sampler clock — rather than smooth wow/flutter or noise.
//
// Controls:
//   DEPTH      — how far the read position wanders (pitch-deviation amount).
//   RATE       — how often a new random target is picked (slow → fast/erratic).
//   TRANSIENT  — boosts the wander on transients (drums/plucks react harder).
//   BLEND      — parallel mix of the jittered path against a clean delayed tap
//                (both share the centre delay, so the blend never combs).
//
// Left/right use independent seeds (subtle width, mono-safe). Unity gain, no
// clicks (read position always moves continuously).
class JitterEngine
{
public:
    void prepare(double sr)
    {
        sampleRate  = sr;
        centerDelay = static_cast<float>(sr * 0.0015);   // 1.5 ms centre tap
        maxDelay    = static_cast<float>(sr * 0.004);
        lineLen     = static_cast<int>(maxDelay) + 4;
        for (int ch = 0; ch < 2; ++ch)
            line[ch].assign(static_cast<size_t>(lineLen), 0.0f);

        envFastCoef = onePole(0.002f);   // 2 ms
        envSlowCoef = onePole(0.050f);   // 50 ms

        rng[0] = 0x1234567u;
        rng[1] = 0x89ABCDEFu;
        reset();
    }

    void setDepth(float pct)
    {
        const float d = std::clamp(pct * 0.01f, 0.0f, 1.0f);
        maxOffset = d * d * static_cast<float>(sampleRate * 0.0007);  // up to ~0.7 ms
        active    = (maxOffset > 0.0001f);
    }

    void setRate(float pct)
    {
        const float r = std::clamp(pct * 0.01f, 0.0f, 1.0f);
        updateInterval = static_cast<int>(juce::jmap(r, 0.0f, 1.0f, 1600.0f, 120.0f));
        if (updateInterval < 8) updateInterval = 8;
    }

    void setTransient(float pct) { transAmt   = std::clamp(pct * 0.01f, 0.0f, 1.0f); }
    void setBlend(float pct)     { blendTarget = std::clamp(pct * 0.01f, 0.0f, 1.0f); }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            writePos[ch] = 0;
            cur[ch] = target[ch] = step[ch] = 0.0f;
            envFast[ch] = envSlow[ch] = 0.0f;
            counter[ch] = 1 + ch * 50;
        }
        blend = blendTarget;
    }

    int getCenterDelaySamples() const { return static_cast<int>(centerDelay); }

    float processSample(int ch, float input)
    {
        auto& buf = line[ch];
        const int wp = writePos[ch];
        buf[static_cast<size_t>(wp)] = input;
        writePos[ch] = (wp + 1) % lineLen;

        // Transient detection (fast vs slow envelope difference)
        const float a = std::fabs(input);
        envFast[ch] += (a - envFast[ch]) * envFastCoef;
        envSlow[ch] += (a - envSlow[ch]) * envSlowCoef;
        const float trans = std::clamp((envFast[ch] - envSlow[ch]) * 6.0f, 0.0f, 1.0f);

        // Random-walk read offset
        if (active)
        {
            if (--counter[ch] <= 0)
            {
                counter[ch] = updateInterval;
                target[ch]  = rand11(rng[ch]) * maxOffset;
                step[ch]    = (target[ch] - cur[ch]) / static_cast<float>(updateInterval);
            }
            cur[ch] += step[ch];
        }
        else
        {
            cur[ch] += (0.0f - cur[ch]) * 0.002f;
        }

        const float effOffset = cur[ch] * (1.0f + transAmt * trans * 3.0f);

        if (ch == 0) blend += (blendTarget - blend) * 0.001f;   // smooth once per frame

        // Clean delayed tap (centre) + jittered tap — both at centre base → no comb
        const float cleanOut = readAt(buf, static_cast<float>(wp) - centerDelay);
        float jitDelay = centerDelay + effOffset;
        jitDelay = std::clamp(jitDelay, 1.0f, maxDelay);
        const float jitOut = readAt(buf, static_cast<float>(wp) - jitDelay);

        return cleanOut * (1.0f - blend) + jitOut * blend;
    }

private:
    float onePole(float sec) const
    {
        return 1.0f - std::exp(-1.0f / (sec * static_cast<float>(sampleRate)));
    }

    static float rand11(uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
    }

    float readAt(const std::vector<float>& b, float pos) const
    {
        while (pos < 0.0f) pos += static_cast<float>(lineLen);
        const int   i0   = static_cast<int>(pos);
        const int   i1   = (i0 + 1) % lineLen;
        const float frac = pos - static_cast<float>(i0);
        return b[static_cast<size_t>(i0)] * (1.0f - frac) + b[static_cast<size_t>(i1)] * frac;
    }

    double sampleRate     = 44100.0;
    float  centerDelay    = 66.0f, maxDelay = 176.0f;
    int    lineLen        = 0;
    bool   active         = false;
    float  maxOffset      = 0.0f;
    int    updateInterval = 800;
    float  transAmt       = 0.0f;
    float  blend = 1.0f, blendTarget = 1.0f;
    float  envFastCoef = 0.02f, envSlowCoef = 0.001f;

    std::vector<float> line[2];
    int      writePos[2] = { 0, 0 };
    float    cur[2]      = { 0.0f, 0.0f };
    float    target[2]   = { 0.0f, 0.0f };
    float    step[2]     = { 0.0f, 0.0f };
    float    envFast[2]  = { 0.0f, 0.0f };
    float    envSlow[2]  = { 0.0f, 0.0f };
    int      counter[2]  = { 1, 51 };
    uint32_t rng[2]      = { 0x1234567u, 0x89ABCDEFu };
};
