#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Character noise generator with selectable types + HP/LP shaping.
//
// The old noise was flat white added after the converters, which read as cheap
// digital hiss. These types are spectrally shaped so they sit like analogue
// noise floors:
//   White — flat (classic digital).
//   Pink  — -3 dB/oct, the analogue staple.
//   Moog  — warm/dark: pink rolled off (smooth low-mid weighted hiss).
//   ARP   — bright/airy: high-tilted white.
//   Vinyl — pink bed plus sparse, tasteful crackle.
//
// Per-channel state (decorrelated L/R). A user HP/LP pair shapes the result.
class NoiseEngine
{
public:
    enum Type { kWhite = 0, kPink, kMoog, kArp, kVinyl };

    void prepare(double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 512u, 1u };
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].prepare(spec); hp[ch].setType(juce::dsp::StateVariableTPTFilterType::highpass); hp[ch].setResonance(0.707f);
            lp[ch].prepare(spec); lp[ch].setType(juce::dsp::StateVariableTPTFilterType::lowpass);  lp[ch].setResonance(0.707f);
        }
        // one-pole shaping coefficients (sample-rate aware)
        moogCoef = onePole(2500.0f);
        arpCoef  = onePole(1200.0f);
        reset();
        setFilters(20.0f, 20000.0f);
    }

    void setType(int t) { type = std::clamp(t, 0, 4); }

    void setFilters(float hpHz, float lpHz)
    {
        hpHz = std::clamp(hpHz, 20.0f,  2000.0f);
        lpHz = std::clamp(lpHz, 500.0f, 20000.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].setCutoffFrequency(hpHz);
            lp[ch].setCutoffFrequency(lpHz);
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            rng[ch] = 0x1000u + (uint32_t)ch * 0x9E3779B9u;
            for (auto& b : pinkB[ch]) b = 0.0f;
            moogLP[ch] = arpLP[ch] = crackEnv[ch] = 0.0f;
            hp[ch].reset();
            lp[ch].reset();
        }
    }

    // Returns one shaped noise sample (~0.5 RMS) for the channel.
    float process(int ch)
    {
        const float w = white(rng[ch]);
        float n;
        switch (type)
        {
            case kPink:  n = pink(ch, w);        break;
            case kMoog:  n = moog(ch, w);        break;
            case kArp:   n = arp(ch, w);         break;
            case kVinyl: n = vinyl(ch, w);       break;
            default:     n = w * 0.85f;          break;   // White
        }
        n = hp[ch].processSample(0, n);
        n = lp[ch].processSample(0, n);
        return n;
    }

private:
    double sampleRate = 44100.0;
    int    type = kPink;

    uint32_t rng[2]      = { 1u, 2u };
    float    pinkB[2][7] = {};
    float    moogLP[2]   = {}, arpLP[2] = {}, crackEnv[2] = {};
    float    moogCoef = 0.3f, arpCoef = 0.15f;

    juce::dsp::StateVariableTPTFilter<float> hp[2], lp[2];

    float onePole(float fc) const
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float)sampleRate);
    }

    static float white(uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return (float)(int32_t)s * (1.0f / 2147483648.0f);
    }

    // Paul Kellet's economical pink filter (normalised ~0.5 RMS)
    float pink(int ch, float w)
    {
        auto& b = pinkB[ch];
        b[0] = 0.99886f * b[0] + w * 0.0555179f;
        b[1] = 0.99332f * b[1] + w * 0.0750759f;
        b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f;
        b[4] = 0.55000f * b[4] + w * 0.5329522f;
        b[5] = -0.7616f * b[5] - w * 0.0168980f;
        const float p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
        b[6] = w * 0.115926f;
        return p * 0.20f;
    }

    float moog(int ch, float w)               // warm/dark
    {
        const float p = pink(ch, w);
        moogLP[ch] += moogCoef * (p - moogLP[ch]);
        return moogLP[ch] * 1.7f;
    }

    float arp(int ch, float w)                // bright/airy (high tilt)
    {
        arpLP[ch] += arpCoef * (w - arpLP[ch]);
        return (w - arpLP[ch] * 0.85f) * 0.9f;
    }

    float vinyl(int ch, float w)              // pink bed + sparse crackle
    {
        const float p = pink(ch, w);
        rng[ch] = rng[ch] * 1664525u + 1013904223u;
        const float r = (float)(rng[ch] >> 9) * (1.0f / 8388608.0f);  // 0..1
        if (r < 0.0006f) crackEnv[ch] = 1.0f;                          // trigger
        crackEnv[ch] *= 0.55f;                                         // fast decay
        const float crack = crackEnv[ch] * white(rng[ch]);
        return p * 0.8f + crack * 1.1f;
    }
};
