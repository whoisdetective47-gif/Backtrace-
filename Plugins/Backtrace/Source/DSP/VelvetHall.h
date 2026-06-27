#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"

// =============================================================================
//  VelvetHall ("Velvet Hall") — Lexicon-style hall/chamber reverb (Phase 9).
//
//  Lush, smooth, modulated studio reverb: a 4-line Hadamard feedback delay
//  network with input diffusers, an early-reflection stage, in-loop HF damping
//  and internal modulation that keeps the tail alive (no static ring, no metal).
//
//  Front-panel macros (reverbKnobLayout flavor 1):
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Duck   p[9] Output(dB)
//
//  Tuned to spec: Decay maps to RT60 0.3–18 s (derived from the actual line
//  lengths, so Size doesn't change the decay time); mod rate 0.05–1.2 Hz; strong
//  HF damping; input/output low+high cuts; output dB trim. Lossless (Hadamard)
//  feedback × gain < 1 → stable at long decay; tanh ceiling; mono-safe M/S width.
//
//  Deferred: sub-modes, separate Early/Tail levels, Lo/Hi damping knobs, Freeze.
// =============================================================================
class VelvetHall : public ReverbSpace
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.26) + 4;          // up to 250 ms predelay
        pre.assign((size_t) preLen, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            fdnMax[i] = (int) (sr * 0.12) + 8;
            fdn[i].assign((size_t) fdnMax[i], 0.0f);
            diff[i].prepare((int) (sr * (0.0047 + 0.0017 * i)));
        }
        reset();
    }

    void reset() override
    {
        std::fill(pre.begin(), pre.end(), 0.0f); preW = 0;
        for (int i = 0; i < 4; ++i)
        {
            std::fill(fdn[i].begin(), fdn[i].end(), 0.0f);
            fw[i] = 0; damp[i] = 0.0f; modPh[i] = (float) i * 0.25f;
            diff[i].reset();
        }
        inHPs = inLPs = outHP = outHC[0] = outHC[1] = 0.0f; duckEnv = 0.0f;
    }

    void setParams(const float* p) override
    {
        const float size   = juce::jlimit(0.0f, 1.0f, p[0]);
        const float decay01 = juce::jlimit(0.0f, 1.0f, p[1]);
        preSamp = juce::jlimit(0.0f, (float) (preLen - 4), (float) (p[2] * 0.001 * sampleRate));
        const float tone  = juce::jlimit(0.0f, 1.0f, p[3]);
        const float diffA = juce::jlimit(0.0f, 1.0f, p[4]);
        mod       = juce::jlimit(0.0f, 1.0f, p[5]);
        width     = juce::jlimit(0.0f, 1.0f, p[6]);
        mix       = juce::jlimit(0.0f, 1.0f, p[7]);
        duck      = juce::jlimit(0.0f, 1.0f, p[8]);
        outGain   = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, p[9]));

        // Line lengths scale with Size; decay is derived from them so the RT60
        // stays put when Size changes.
        const float scale = 0.6f + size * 1.0f;
        static const float baseMs[4] = { 29.7f, 37.1f, 41.1f, 43.7f };
        float sumLen = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            lineLen[i] = juce::jlimit(4.0f, (float) (fdnMax[i] - 4), baseMs[i] * scale * 0.001f * (float) sampleRate);
            sumLen += lineLen[i];
            diff[i].g = 0.4f + diffA * 0.35f;
        }
        const float meanSec = (sumLen * 0.25f) / (float) sampleRate;
        const float rt = juce::jlimit(0.3f, 18.0f, 0.71f * std::exp(3.23f * decay01));   // RT60 seconds
        decayGain = juce::jlimit(0.0f, 0.985f, std::pow(10.0f, -3.0f * meanSec / rt));

        aDamp  = onePole(juce::jmap(tone, 0.0f, 1.0f, 2800.0f, 11000.0f));   // in-loop HF damping
        aInHP  = onePole(120.0f);
        aInLP  = onePole(14000.0f);
        aOutHP = onePole(150.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 6500.0f, 13000.0f));   // output air with Tone

        modDepth = mod * (float) (sampleRate * 0.0022);
        modScale = 0.6f + mod * 1.4f;                                        // 0.05–1.2 Hz range
        erScale  = scale;
        injLevel = 0.30f;
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;

        // input conditioning: low-cut then high-cut
        float mono = (inL + inR) * 0.5f;
        inHPs += aInHP * (mono - inHPs); mono -= inHPs;
        inLPs += aInLP * (mono - inLPs); mono  = inLPs;

        const float preOut = readPre(preSamp);
        pre[(size_t) preW] = mono;
        if (++preW >= preLen) preW = 0;

        // early reflections (sparse, size-scaled taps off the predelay line)
        static const float erMs[4] = { 7.0f, 13.0f, 19.0f, 23.0f };
        static const float erG [4] = { 0.8f, 0.6f, 0.5f, 0.4f };
        float early = 0.0f;
        for (int i = 0; i < 4; ++i)
            early += readPre(preSamp + erMs[i] * erScale * 0.001f * (float) sampleRate) * erG[i];
        early *= 0.30f;

        float d = preOut;
        for (int i = 0; i < 4; ++i) d = diff[i].process(d);

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
        const float inj = d * injLevel;
        for (int i = 0; i < 4; ++i)
        {
            fdn[i][(size_t) fw[i]] = inj + mm[i] * decayGain;
            if (++fw[i] >= fdnMax[i]) fw[i] = 0;
        }

        float wetL = (y[0] - y[3]) + early;
        float wetR = (y[1] - y[2]) + early;

        // output low-cut (shared) then width
        outHP += aOutHP * (((wetL + wetR) * 0.5f) - outHP);
        wetL -= outHP; wetR -= outHP;
        const float midS  = (wetL + wetR) * 0.5f;
        const float sideS = (wetL - wetR) * 0.5f * (width * 2.0f);
        wetL = midS + sideS; wetR = midS - sideS;

        // output high-cut then gain + ceiling
        outHC[0] += aOutHC * (wetL - outHC[0]); wetL = std::tanh(outHC[0] * outGain);
        outHC[1] += aOutHC * (wetR - outHC[1]); wetR = std::tanh(outHC[1] * outGain);

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99965f);
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

    static constexpr float kModRate[4] = { 0.21f, 0.29f, 0.37f, 0.45f };

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0;
    float  preSamp = 0.0f, decayGain = 0.7f, mod = 0.25f, width = 0.7f, mix = 0.25f,
           duck = 0.2f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f, erScale = 1.0f, injLevel = 0.3f;
    float  aDamp = 0.3f, aInHP = 0.02f, aInLP = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f;
    float  inHPs = 0.0f, inLPs = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 }, duckEnv = 0.0f;

    std::vector<float> pre, fdn[4];
    int    fdnMax[4] = { 4, 4, 4, 4 }, fw[4] = { 0, 0, 0, 0 };
    float  lineLen[4] = { 1, 1, 1, 1 }, damp[4] = { 0, 0, 0, 0 }, modPh[4] = { 0, 0, 0, 0 };
    AllPass diff[4];
};
