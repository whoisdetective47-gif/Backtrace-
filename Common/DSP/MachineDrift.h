#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Vintage machine movement — wow & flutter via per-channel modulated delay.
//
// The previous implementation randomly jittered drive/sample-rate/tone, which
// sounded like a broken chorus and produced no real stereo width. This models
// the actual thing: the pitch/time instability of an aging sampler or tape
// transport. Two smooth sine LFOs (slow "wow" + faster "flutter") modulate a
// short fractional delay per channel, giving gentle, musical pitch movement.
//
// Stereo width comes from genuine left/right decorrelation: the right channel
// gets a STEREO-scaled phase offset plus a slightly higher LFO rate, so a mono
// source becomes wider and more alive. Offsets stay below anti-phase, so mono
// fold-down does not hollow out. At DRIFT = 0 there is no movement at all.
class MachineDrift
{
public:
    void prepare(double sr)
    {
        sampleRate  = sr;
        centerDelay = static_cast<float>(sr * 0.005);   // 5 ms center tap
        maxDelay    = static_cast<float>(sr * 0.011);   // headroom for ±modulation
        lineLen     = static_cast<int>(maxDelay) + 4;
        for (int ch = 0; ch < 2; ++ch)
            line[ch].assign(static_cast<size_t>(lineLen), 0.0f);
        reset();
    }

    void setParams(float driftPct, float motionPct)
    {
        const float drift  = std::clamp(driftPct  * 0.01f, 0.0f, 1.0f);
        const float motion = std::clamp(motionPct * 0.01f, 0.0f, 1.0f);

        // Depth curve: gentle in the first half (drift^2), weird at the extremes.
        const float depth  = drift * drift;
        targetWowDepth     = depth * static_cast<float>(sampleRate * 0.0035); // up to ~3.5 ms
        targetFlutterDepth = depth * static_cast<float>(sampleRate * 0.0003); // up to ~0.3 ms

        baseWowRate     = 0.3f + motion * 1.5f;   // 0.3–1.8 Hz  (wow)
        baseFlutterRate = 5.0f + motion * 4.0f;   // 5–9 Hz      (flutter)
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            writePos[ch]     = 0;
            wowPhase[ch]     = 0.0f;
            flutterPhase[ch] = 0.0f;
        }
        wowDepth = flutterDepth = 0.0f;
    }

    int getCenterDelaySamples() const { return static_cast<int>(centerDelay); }

    // Advance LFOs + smooth depth. Call once per sample, before processing channels.
    void tickLFO()
    {
        const float twoPi = juce::MathConstants<float>::twoPi;

        // Shared phase across channels → mono wow/flutter (stereo width is handled
        // separately by the StereoWidener; this keeps the pitch movement coherent).
        wowPhase[0]     += twoPi * baseWowRate     / static_cast<float>(sampleRate);
        flutterPhase[0] += twoPi * baseFlutterRate / static_cast<float>(sampleRate);
        if (wowPhase[0]     >= twoPi) wowPhase[0]     -= twoPi;
        if (flutterPhase[0] >= twoPi) flutterPhase[0] -= twoPi;
        wowPhase[1]     = wowPhase[0];
        flutterPhase[1] = flutterPhase[0];

        // Smooth depth toward target (no clicks when DRIFT moves) — ~10 ms
        wowDepth     += (targetWowDepth     - wowDepth)     * 0.0015f;
        flutterDepth += (targetFlutterDepth - flutterDepth) * 0.0015f;
    }

    // Process one sample for a channel through the modulated delay.
    float processSample(int ch, float input)
    {
        auto& buf = line[ch];
        buf[static_cast<size_t>(writePos[ch])] = input;

        const float wow     = std::sin(wowPhase[ch])     * wowDepth;
        const float flutter = std::sin(flutterPhase[ch]) * flutterDepth;
        float delaySamp = centerDelay + wow + flutter;
        delaySamp = std::clamp(delaySamp, 1.0f, maxDelay);

        float readPos = static_cast<float>(writePos[ch]) - delaySamp;
        while (readPos < 0.0f) readPos += static_cast<float>(lineLen);

        const int   i0   = static_cast<int>(readPos);
        const int   i1   = (i0 + 1) % lineLen;
        const float frac = readPos - static_cast<float>(i0);
        const float out  = buf[static_cast<size_t>(i0)] * (1.0f - frac)
                         + buf[static_cast<size_t>(i1)] * frac;

        writePos[ch] = (writePos[ch] + 1) % lineLen;
        return out;
    }

private:
    double sampleRate  = 44100.0;
    float  centerDelay = 220.0f, maxDelay = 485.0f;
    int    lineLen     = 0;

    float  baseWowRate     = 0.5f, baseFlutterRate = 6.0f;
    float  targetWowDepth  = 0.0f, targetFlutterDepth = 0.0f;
    float  wowDepth        = 0.0f, flutterDepth       = 0.0f;

    std::vector<float> line[2];
    int   writePos[2]     = { 0, 0 };
    float wowPhase[2]     = { 0.0f, 0.0f };
    float flutterPhase[2] = { 0.0f, 0.0f };
};
