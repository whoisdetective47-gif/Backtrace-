#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"

// =============================================================================
//  ModernSpace ("Modern Space") — clean modern room/chamber/hall (Phase 9).
//
//  Clean, deep, dimensional, realistic reverb — the high-end natural space that
//  contrasts with Velvet Hall's colorful modulated gloss. A convincing stereo
//  early-reflection engine places the source in a real environment; a smooth,
//  densely-diffused 4-line FDN provides a natural tail with only subtle internal
//  modulation (felt as depth, not heard as chorus).
//
//  Front-panel macros (reverbKnobLayout flavor 2):
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Duck   p[9] Output(dB)
//
//  Decay → RT60 0.2–12 s (from line lengths, Size-independent). Mod 0.03–0.7 Hz,
//  low depth. Refined damping + input/output cuts. Stable, mono-safe, dB output.
//
//  Deferred: Space Type sub-modes, Early/Tail/Distance/Density, separate damping,
//  width modes, Freeze, presets.
// =============================================================================
class ModernSpace : public ReverbSpace
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.55) + 4;          // room for predelay + early-reflection taps
        pre.assign((size_t) preLen, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            fdnMax[i] = (int) (sr * 0.13) + 8;
            fdn[i].assign((size_t) fdnMax[i], 0.0f);
        }
        for (int i = 0; i < 6; ++i)
            diff[i].prepare((int) (sr * (0.0041 + 0.0013 * i)));   // ~4.1..10.6 ms, denser
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
        inHPs = inLPs = outHP = outHC[0] = outHC[1] = 0.0f; duckEnv = 0.0f;
    }

    void setParams(const float* p) override
    {
        const float size   = juce::jlimit(0.0f, 1.0f, p[0]);
        const float decay01 = juce::jlimit(0.0f, 1.0f, p[1]);
        preSamp = juce::jlimit(0.0f, (float) (preLen / 2), (float) (p[2] * 0.001 * sampleRate));
        const float tone  = juce::jlimit(0.0f, 1.0f, p[3]);
        const float diffA = juce::jlimit(0.0f, 1.0f, p[4]);
        mod       = juce::jlimit(0.0f, 1.0f, p[5]);
        width     = juce::jlimit(0.0f, 1.0f, p[6]);
        mix       = juce::jlimit(0.0f, 1.0f, p[7]);
        duck      = juce::jlimit(0.0f, 1.0f, p[8]);
        outGain   = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, p[9]));

        erScale = 0.5f + size * 1.0f;
        const float lineScale = 0.7f + size * 1.1f;
        static const float baseMs[4] = { 30.5f, 36.3f, 40.9f, 44.3f };
        float sumLen = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            lineLen[i] = juce::jlimit(4.0f, (float) (fdnMax[i] - 4), baseMs[i] * lineScale * 0.001f * (float) sampleRate);
            sumLen += lineLen[i];
        }
        for (int i = 0; i < 6; ++i) diff[i].g = 0.45f + diffA * 0.32f;

        const float meanSec = (sumLen * 0.25f) / (float) sampleRate;
        const float rt = juce::jlimit(0.2f, 12.0f, 0.85f * std::exp(2.65f * decay01));   // tighter, natural RT
        decayGain = juce::jlimit(0.0f, 0.985f, std::pow(10.0f, -3.0f * meanSec / rt));

        aDamp  = onePole(juce::jmap(tone, 0.0f, 1.0f, 4000.0f, 14000.0f));
        aInHP  = onePole(110.0f);
        aInLP  = onePole(16000.0f);
        aOutHP = onePole(160.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 6000.0f, 14000.0f));

        modDepth = mod * (float) (sampleRate * 0.0014);                 // lower than Velvet Hall
        modScale = 0.4f + mod * 1.6f;                                   // 0.03–0.7 Hz
        erLevel  = 0.35f;
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

        // stereo early-reflection engine — placement + depth + spread
        float erL = 0.0f, erR = 0.0f;
        for (int i = 0; i < kNumER; ++i)
        {
            const float v = readPre(preSamp + kErMs[i] * erScale * 0.001f * (float) sampleRate) * kErG[i];
            if ((i & 1) == 0) { erL += v;        erR += v * 0.45f; }
            else              { erL += v * 0.45f; erR += v;        }
        }

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

        const float m0 = (y[0] + y[1] + y[2] + y[3]) * 0.5f;
        const float m1 = (y[0] - y[1] + y[2] - y[3]) * 0.5f;
        const float m2 = (y[0] + y[1] - y[2] - y[3]) * 0.5f;
        const float m3 = (y[0] - y[1] - y[2] + y[3]) * 0.5f;
        const float mm[4] = { m0, m1, m2, m3 };
        const float inj = d * 0.28f;
        for (int i = 0; i < 4; ++i)
        {
            fdn[i][(size_t) fw[i]] = inj + mm[i] * decayGain;
            if (++fw[i] >= fdnMax[i]) fw[i] = 0;
        }

        float wetL = (y[0] - y[3]) + erL * erLevel;
        float wetR = (y[1] - y[2]) + erR * erLevel;

        outHP += aOutHP * (((wetL + wetR) * 0.5f) - outHP);
        wetL -= outHP; wetR -= outHP;
        const float midS  = (wetL + wetR) * 0.5f;
        const float sideS = (wetL - wetR) * 0.5f * (width * 2.0f);
        wetL = midS + sideS; wetR = midS - sideS;

        outHC[0] += aOutHC * (wetL - outHC[0]); wetL = std::tanh(outHC[0] * outGain);
        outHC[1] += aOutHC * (wetR - outHC[1]); wetR = std::tanh(outHC[1] * outGain);

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99970f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        outL = inL * (1.0f - mix) + wetL * mix * duckGain;
        outR = inR * (1.0f - mix) + wetR * mix * duckGain;
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

    static constexpr int   kNumER = 12;
    static constexpr float kErMs[kNumER] = { 7.3f, 11.1f, 16.7f, 21.3f, 28.9f, 34.7f, 41.3f, 48.7f, 56.1f, 63.3f, 71.9f, 79.1f };
    static constexpr float kErG [kNumER] = { 0.70f, 0.62f, 0.55f, 0.49f, 0.43f, 0.38f, 0.34f, 0.30f, 0.26f, 0.23f, 0.20f, 0.17f };
    static constexpr float kModRate[4] = { 0.10f, 0.16f, 0.24f, 0.34f };

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0;
    float  preSamp = 0.0f, decayGain = 0.7f, mod = 0.15f, width = 0.65f, mix = 0.22f,
           duck = 0.15f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f, erScale = 1.0f, erLevel = 0.35f;
    float  aDamp = 0.3f, aInHP = 0.02f, aInLP = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f;
    float  inHPs = 0.0f, inLPs = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 }, duckEnv = 0.0f;

    std::vector<float> pre, fdn[4];
    int    fdnMax[4] = { 4, 4, 4, 4 }, fw[4] = { 0, 0, 0, 0 };
    float  lineLen[4] = { 1, 1, 1, 1 }, damp[4] = { 0, 0, 0, 0 }, modPh[4] = { 0, 0, 0, 0 };
    AllPass diff[6];
};
