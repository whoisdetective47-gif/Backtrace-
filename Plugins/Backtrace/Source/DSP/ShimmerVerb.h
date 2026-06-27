#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"
#include "DSP/ModernPitchShifter.h"

// =============================================================================
//  ShimmerVerb ("Shimmer") — ethereal octave-bloom ambient reverb (Phase 9).
//
//  The cinematic evidence cloud: a clean, wide ambient bloom with an octave-up
//  halo that blooms and regenerates from inside the tail. ModernPitchShifter
//  (+12) sits IN the FDN feedback path — not as a wet insert — so the shimmer
//  grows naturally and musically. From subtle angelic air to huge octave clouds.
//
//  Front-panel macros (reverbKnobLayout flavor 3) — Duck is replaced by Shimmer:
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Shimmer p[9] Output(dB)
//
//  Decay → RT60 0.5–30 s. Shimmer feedback: low-cut before pitch (no octave-mud),
//  HF damping after, tanh limit, and decay reduced as Shimmer rises → no runaway
//  at Decay/Shimmer 100%. Structured so Freeze (decay=1, input muted) can drop in.
//
//  Deferred: secondary -12/+7 voices, L/R micro-detune, Freeze button, presets.
// =============================================================================
class ShimmerVerb : public ReverbSpace
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.18) + 4;
        pre.assign((size_t) preLen, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            fdnMax[i] = (int) (sr * 0.14) + 8;
            fdn[i].assign((size_t) fdnMax[i], 0.0f);
        }
        for (int i = 0; i < 6; ++i)
            diff[i].prepare((int) (sr * (0.0043 + 0.0015 * i)));
        pitch.prepare(sr, 0);
        pitch.setPitch(12.0f);          // octave up
        reset();
    }

    void reset() override
    {
        std::fill(pre.begin(), pre.end(), 0.0f); preW = 0;
        for (int i = 0; i < 4; ++i)
        {
            std::fill(fdn[i].begin(), fdn[i].end(), 0.0f);
            fw[i] = 0; damp[i] = 0.0f; modPh[i] = (float) i * 0.25f;
        }
        for (int i = 0; i < 6; ++i) diff[i].reset();
        pitch.reset();
        inHPs = inLPs = sLC = sHC = outHP = outHC[0] = outHC[1] = 0.0f;
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
        shimmer   = juce::jlimit(0.0f, 1.0f, p[8]);
        outGain   = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, p[9]));

        const float lineScale = 0.7f + size * 1.2f;
        static const float baseMs[4] = { 31.7f, 37.9f, 42.3f, 46.1f };
        float sumLen = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            lineLen[i] = juce::jlimit(4.0f, (float) (fdnMax[i] - 4), baseMs[i] * lineScale * 0.001f * (float) sampleRate);
            sumLen += lineLen[i];
        }
        for (int i = 0; i < 6; ++i) diff[i].g = 0.5f + diffA * 0.30f;

        const float meanSec = (sumLen * 0.25f) / (float) sampleRate;
        const float rt = juce::jlimit(0.5f, 30.0f, 0.5f * std::exp(4.094f * decay01));   // RT60 0.5–30 s
        decayGain = juce::jlimit(0.0f, 0.990f, std::pow(10.0f, -3.0f * meanSec / rt));

        aDamp  = onePole(juce::jmap(tone, 0.0f, 1.0f, 4500.0f, 12000.0f));
        aInHP  = onePole(120.0f);
        aInLP  = onePole(16000.0f);
        aSLC   = onePole(320.0f);                                       // low-cut BEFORE pitch
        aSHC   = onePole(juce::jmap(tone, 0.0f, 1.0f, 6000.0f, 12000.0f)); // damp AFTER pitch
        aOutHP = onePole(190.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 6500.0f, 14000.0f));

        modDepth = mod * (float) (sampleRate * 0.0016);
        modScale = 0.3f + mod * 1.5f;                                   // 0.02–0.6 Hz
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;

        float mono = (inL + inR) * 0.5f;
        inHPs += aInHP * (mono - inHPs); mono -= inHPs;
        inLPs += aInLP * (mono - inLPs); mono  = inLPs;

        const float preOut = readPre(preSamp);
        pre[(size_t) preW] = mono;
        if (++preW >= preLen) preW = 0;

        float d = preOut;
        for (int i = 0; i < 6; ++i) d = diff[i].process(d);

        float y[4];
        for (int i = 0; i < 4; ++i)
        {
            modPh[i] += (kModRate[i] * modScale) / (float) sampleRate; if (modPh[i] >= 1.0f) modPh[i] -= 1.0f;
            const float rp = (float) fw[i] - (lineLen[i] + std::sin(twoPi * modPh[i]) * modDepth);
            float s = readFdn(i, rp);
            damp[i] += aDamp * (s - damp[i]);
            y[i] = damp[i];
        }

        // --- shimmer feedback: low-cut → +12 pitch → damp → limit ---
        const float tailMono = (y[0] + y[1] + y[2] + y[3]) * 0.25f;
        sLC += aSLC * (tailMono - sLC);
        const float sIn  = tailMono - sLC;                  // remove lows before pitching
        const float ratio = pitch.advanceAndGetRatio();
        float sP = pitch.processSample(0, sIn, ratio);      // octave up
        sHC += aSHC * (sP - sHC); sP = sHC;                 // damp the octave top
        sP = std::tanh(sP);                                 // safety limit
        const float shimmerInj = sP * (shimmer * 0.55f);

        const float m0 = (y[0] + y[1] + y[2] + y[3]) * 0.5f;
        const float m1 = (y[0] - y[1] + y[2] - y[3]) * 0.5f;
        const float m2 = (y[0] + y[1] - y[2] - y[3]) * 0.5f;
        const float m3 = (y[0] - y[1] - y[2] + y[3]) * 0.5f;
        const float mm[4] = { m0, m1, m2, m3 };
        const float inj = d * 0.25f + shimmerInj;
        const float effDecay = decayGain * (1.0f - shimmer * 0.12f);    // tame rising-octave energy
        for (int i = 0; i < 4; ++i)
        {
            fdn[i][(size_t) fw[i]] = inj + mm[i] * effDecay;
            if (++fw[i] >= fdnMax[i]) fw[i] = 0;
        }

        float wetL = (y[0] - y[3]) + sP * 0.30f;
        float wetR = (y[1] - y[2]) + sP * 0.30f;

        outHP += aOutHP * (((wetL + wetR) * 0.5f) - outHP);
        wetL -= outHP; wetR -= outHP;
        const float midS  = (wetL + wetR) * 0.5f;
        const float sideS = (wetL - wetR) * 0.5f * (width * 2.0f);
        wetL = midS + sideS; wetR = midS - sideS;

        outHC[0] += aOutHC * (wetL - outHC[0]); wetL = std::tanh(outHC[0] * outGain);
        outHC[1] += aOutHC * (wetR - outHC[1]); wetR = std::tanh(outHC[1] * outGain);

        outL = inL * (1.0f - mix) + wetL * mix;
        outR = inR * (1.0f - mix) + wetR * mix;
    }

private:
    struct AllPass
    {
        std::vector<float> buf; int idx = 0, len = 1; float g = 0.5f;
        void prepare(int n) { len = juce::jmax(1, n); buf.assign((size_t) len, 0.0f); idx = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process(float x)
        {
            const float dd = buf[(size_t) idx];
            const float y = -g * x + dd;
            buf[(size_t) idx] = x + g * y;
            if (++idx >= len) idx = 0;
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

    float readFdn(int k, float pos) const
    {
        const int m = fdnMax[k];
        while (pos < 0.0f) pos += (float) m;
        while (pos >= (float) m) pos -= (float) m;
        const int i0 = (int) pos, i1 = (i0 + 1) % m;
        const float f = pos - (float) i0;
        return fdn[k][(size_t) i0] * (1.0f - f) + fdn[k][(size_t) i1] * f;
    }

    float onePole(float fc) const
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
    }

    static constexpr float kModRate[4] = { 0.07f, 0.11f, 0.17f, 0.23f };

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0;
    float  preSamp = 0.0f, decayGain = 0.8f, mod = 0.30f, width = 0.80f, mix = 0.28f,
           shimmer = 0.35f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f;
    float  aDamp = 0.3f, aInHP = 0.02f, aInLP = 0.5f, aSLC = 0.05f, aSHC = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f;
    float  inHPs = 0.0f, inLPs = 0.0f, sLC = 0.0f, sHC = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 };

    std::vector<float> pre, fdn[4];
    int    fdnMax[4] = { 4, 4, 4, 4 }, fw[4] = { 0, 0, 0, 0 };
    float  lineLen[4] = { 1, 1, 1, 1 }, damp[4] = { 0, 0, 0, 0 }, modPh[4] = { 0, 0, 0, 0 };
    AllPass diff[6];
    ModernPitchShifter pitch;
};
