#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"

// =============================================================================
//  PlateVerb ("Studio Plate") — EMT-140-style dense studio plate (Phase 9).
//
//  The classic vocal/snare plate: dense, smooth, bright-but-controlled, fast-
//  building. No room/hall placement and no early-reflection engine — a Dattorro-
//  style figure-8 tank (serial allpass diffusion + cross-coupled delays + damping)
//  that blooms into a glowing sheet of reverb wrapped around the source.
//
//  Front-panel macros (reverbKnobLayout flavor 4):
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Duck   p[9] Output(dB)
//
//  Size = plate spread / modal density (scales tank delays), NOT room size.
//  Decay → RT60 0.4–8 s (derived so Size doesn't change the decay). Tone-swept
//  damping (bright, not icy). Stable: lossless allpasses, decay capped, tanh
//  ceiling. Mono-safe decorrelated stereo taps.
//
//  Deferred: dynamic de-ess damping, separate Lo/Hi damping, sub-modes, presets.
// =============================================================================
class PlateVerb : public ReverbSpace
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.13) + 4;
        pre.assign((size_t) preLen, 0.0f);
        const float sfSR = (float) (sr / 29761.0);
        idf[0].prepare((int) (142 * sfSR) + 4);
        idf[1].prepare((int) (107 * sfSR) + 4);
        idf[2].prepare((int) (379 * sfSR) + 4);
        idf[3].prepare((int) (277 * sfSR) + 4);
        const int modMax = (int) (sr * 0.003) + 4;
        apL1.prepare((int) (1.8f * 672 * sfSR) + modMax);
        apL2.prepare((int) (1.8f * 1800 * sfSR) + 4);
        apR1.prepare((int) (1.8f * 908 * sfSR) + modMax);
        apR2.prepare((int) (1.8f * 2656 * sfSR) + 4);
        dL1.prepare((int) (1.8f * 4453 * sfSR) + 4);
        dL2.prepare((int) (1.8f * 3720 * sfSR) + 4);
        dR1.prepare((int) (1.8f * 4217 * sfSR) + 4);
        dR2.prepare((int) (1.8f * 3163 * sfSR) + 4);
        reset();
    }

    void reset() override
    {
        std::fill(pre.begin(), pre.end(), 0.0f); preW = 0;
        for (auto* a : { &idf[0], &idf[1], &idf[2], &idf[3], &apL1, &apL2, &apR1, &apR2 }) a->reset();
        for (auto* d : { &dL1, &dL2, &dR1, &dR2 }) d->reset();
        dampL = dampR = inHPs = inLPs = outHP = outHC[0] = outHC[1] = 0.0f;
        tankFB = 0.0f; modPh = 0.0f; duckEnv = 0.0f;
    }

    void setParams(const float* p) override
    {
        const float size    = juce::jlimit(0.0f, 1.0f, p[0]);
        const float decay01 = juce::jlimit(0.0f, 1.0f, p[1]);
        preSamp = juce::jlimit(0.0f, (float) (preLen - 4), (float) (p[2] * 0.001 * sampleRate));
        const float tone  = juce::jlimit(0.0f, 1.0f, p[3]);
        const float diffA = juce::jlimit(0.0f, 1.0f, p[4]);
        mod       = juce::jlimit(0.0f, 1.0f, p[5]);
        width     = juce::jlimit(0.0f, 1.0f, p[6]);
        mix       = juce::jlimit(0.0f, 1.0f, p[7]);
        duck      = juce::jlimit(0.0f, 1.0f, p[8]);
        outGain   = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, p[9]));

        const float sf = (float) (sampleRate / 29761.0) * (0.7f + size * 0.8f);
        apL1.setDelay(672 * sf); apL2.setDelay(1800 * sf);
        apR1.setDelay(908 * sf); apR2.setDelay(2656 * sf);
        dL1.setDelay(4453 * sf); dL2.setDelay(3720 * sf);
        dR1.setDelay(4217 * sf); dR2.setDelay(3163 * sf);

        const float idg = 0.55f + diffA * 0.20f;
        idf[0].g = idf[1].g = idg; idf[2].g = idf[3].g = idg - 0.12f;
        apL1.g = apR1.g = 0.70f; apL2.g = apR2.g = 0.50f;

        const float loopSec = ((672 + 1800 + 4453 + 3720 + 908 + 2656 + 4217 + 3163) * sf) / (float) sampleRate;
        const float rt = juce::jlimit(0.4f, 8.0f, 0.7f * std::exp(2.43f * decay01));   // RT60 0.4–8 s
        decay = juce::jlimit(0.0f, 0.86f, std::pow(10.0f, -1.5f * loopSec / rt));       // applied twice per loop

        aDamp  = onePole(juce::jmap(tone, 0.0f, 1.0f, 3500.0f, 13000.0f));
        aInHP  = onePole(120.0f);
        aInLP  = onePole(15000.0f);
        aOutHP = onePole(200.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 5500.0f, 14000.0f));

        modDepth = (0.4f + mod * 0.8f) * 0.001f * (float) sampleRate;   // 0.4–1.2 ms
        modScale = 0.3f + mod * 1.0f;                                   // 0.03–0.45 Hz
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        modPh += (0.30f * modScale) / (float) sampleRate; if (modPh >= 1.0f) modPh -= 1.0f;
        const float mL = std::sin(twoPi * modPh) * modDepth;
        const float mR = std::sin(twoPi * (modPh + 0.27f)) * modDepth;

        float mono = (inL + inR) * 0.5f;
        inHPs += aInHP * (mono - inHPs); mono -= inHPs;
        inLPs += aInLP * (mono - inLPs); mono  = inLPs;

        const float preOut = readPre(preSamp);
        pre[(size_t) preW] = mono;
        if (++preW >= preLen) preW = 0;

        // input diffusion cluster (plate excitation — no room ERs)
        float x = preOut;
        for (int i = 0; i < 4; ++i) x = idf[i].process(x, 0.0f);

        // figure-8 tank loop
        float t = x + tankFB;
        t = apL1.process(t, mL);
        t = dL1.process(t);
        dampL += aDamp * (t - dampL); t = dampL;
        t *= decay;
        t = apL2.process(t, 0.0f);
        const float lOut = dL2.process(t);

        t = lOut + x * 0.5f;
        t = apR1.process(t, mR);
        t = dR1.process(t);
        dampR += aDamp * (t - dampR); t = dampR;
        t *= decay;
        t = apR2.process(t, 0.0f);
        tankFB = dR2.process(t);

        // decorrelated stereo taps
        float wetL = dL2.tapAt((int) (dL2.len * 0.20f)) + dR1.tapAt((int) (dR1.len * 0.55f)) * 0.8f;
        float wetR = dR2.tapAt((int) (dR2.len * 0.20f)) + dL1.tapAt((int) (dL1.len * 0.55f)) * 0.8f;

        outHP += aOutHP * (((wetL + wetR) * 0.5f) - outHP);
        wetL -= outHP; wetR -= outHP;
        const float midS  = (wetL + wetR) * 0.5f;
        const float sideS = (wetL - wetR) * 0.5f * (width * 2.0f);
        wetL = midS + sideS; wetR = midS - sideS;

        outHC[0] += aOutHC * (wetL - outHC[0]); wetL = std::tanh(outHC[0] * outGain);
        outHC[1] += aOutHC * (wetR - outHC[1]); wetR = std::tanh(outHC[1] * outGain);

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99965f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        outL = inL * (1.0f - mix) + wetL * mix * duckGain;
        outR = inR * (1.0f - mix) + wetR * mix * duckGain;
    }

private:
    struct Delay
    {
        std::vector<float> buf; int w = 0, len = 1; float dly = 1.0f;
        void prepare(int n) { len = juce::jmax(2, n); buf.assign((size_t) len, 0.0f); w = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        void setDelay(float d) { dly = juce::jlimit(1.0f, (float) (len - 2), d); }
        float process(float x)
        {
            float rd = (float) w - dly;
            while (rd < 0.0f) rd += (float) len;
            const int i0 = (int) rd, i1 = (i0 + 1) % len;
            const float f = rd - (float) i0;
            const float y = buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
            buf[(size_t) w] = x; if (++w >= len) w = 0;
            return y;
        }
        float tapAt(int off) const { int i = (w - 1 - off) % len; if (i < 0) i += len; return buf[(size_t) i]; }
    };

    struct ModAllPass
    {
        std::vector<float> buf; int w = 0, len = 1; float g = 0.5f, dly = 1.0f;
        void prepare(int n) { len = juce::jmax(2, n); buf.assign((size_t) len, 0.0f); w = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        void setDelay(float d) { dly = juce::jlimit(1.0f, (float) (len - 2), d); }
        float process(float x, float modOff)
        {
            float rd = (float) w - (dly + modOff);
            while (rd < 0.0f) rd += (float) len;
            while (rd >= (float) len) rd -= (float) len;
            const int i0 = (int) rd, i1 = (i0 + 1) % len;
            const float f = rd - (float) i0;
            const float d = buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
            const float y = -g * x + d;
            buf[(size_t) w] = x + g * y; if (++w >= len) w = 0;
            return y;
        }
    };

    float readPre(float pos) const
    {
        float p = (float) preW - pos;
        while (p < 0.0f) p += (float) preLen;
        while (p >= (float) preLen) p -= (float) preLen;
        const int i0 = (int) p, i1 = (i0 + 1) % preLen;
        const float f = p - (float) i0;
        return pre[(size_t) i0] * (1.0f - f) + pre[(size_t) i1] * f;
    }

    float onePole(float fc) const
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
    }

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0;
    float  preSamp = 0.0f, decay = 0.4f, mod = 0.18f, width = 0.72f, mix = 0.24f,
           duck = 0.18f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f;
    float  aDamp = 0.3f, aInHP = 0.02f, aInLP = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f;
    float  inHPs = 0.0f, inLPs = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 };
    float  dampL = 0.0f, dampR = 0.0f, tankFB = 0.0f, modPh = 0.0f, duckEnv = 0.0f;

    std::vector<float> pre;
    ModAllPass idf[4], apL1, apL2, apR1, apR2;
    Delay      dL1, dL2, dR1, dR2;
};
