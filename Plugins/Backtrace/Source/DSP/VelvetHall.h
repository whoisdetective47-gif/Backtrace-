#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"

// =============================================================================
//  VelvetHall ("Concert Hall") — rich, expensive hall/chamber reverb.
//
//  8-line orthonormal (Hadamard) feedback delay network with a 6-stage input
//  diffuser, an early-reflection stage, per-line frequency-dependent decay
//  (highs die faster than lows), and independent per-line modulation that keeps
//  the tail alive (no static ring, no metal). Doubling the FDN from 4 to 8 lines
//  and deepening the diffusion is what turns a competent reverb into a lush,
//  glassy, "expensive" one — the tail is dense enough that reversing it (the
//  Live Preverb kernel) yields a smooth premium swell.
//
//  Front-panel macros (reverbKnobLayout flavor 1):
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Duck   p[9] Output(dB)
//
//  Decay maps to RT60 0.3–18 s (derived from the actual line lengths, so Size
//  doesn't change the decay time). Orthonormal Hadamard feedback × gain < 1 →
//  provably stable at long decay; tanh ceiling; mono-safe M/S width. This same
//  core is voiced huge/dark for the Cathedral flavor.
// =============================================================================
class VelvetHall : public ReverbSpace
{
public:
    static constexpr int kLines = 8;     // FDN size (was 4) — the density upgrade
    static constexpr int kDiff  = 6;     // input diffusion stages (was 4)

    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.26) + 4;          // up to 250 ms predelay
        pre.assign((size_t) preLen, 0.0f);
        static const double diffMs[kDiff] = { 4.7, 6.4, 8.1, 9.8, 12.1, 14.3 };
        for (int i = 0; i < kLines; ++i)
        {
            fdnMax[i] = (int) (sr * 0.15) + 8;   // headroom for size×1.6 + modulation
            fdn[i].assign((size_t) fdnMax[i], 0.0f);
        }
        for (int i = 0; i < kDiff; ++i)
            diff[i].prepare((int) (sr * diffMs[i] * 0.001));
        reset();
    }

    void reset() override
    {
        std::fill(pre.begin(), pre.end(), 0.0f); preW = 0;
        for (int i = 0; i < kLines; ++i)
        {
            std::fill(fdn[i].begin(), fdn[i].end(), 0.0f);
            fw[i] = 0; damp[i] = 0.0f; modPh[i] = (float) i * 0.13f;
        }
        for (int i = 0; i < kDiff; ++i) diff[i].reset();
        inHPs = inLPs = outHP = outHC[0] = outHC[1] = 0.0f; duckEnv = 0.0f;
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

        // Line lengths scale with Size; decay is derived from them so the RT60
        // stays put when Size changes. 8 mutually-prime-ish lengths → dense tail.
        const float scale = 0.6f + size * 1.0f;
        static const float baseMs[kLines] = { 23.3f, 29.7f, 31.5f, 37.1f, 41.1f, 43.7f, 47.3f, 53.9f };
        float sumLen = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            lineLen[i] = juce::jlimit(4.0f, (float) (fdnMax[i] - 4), baseMs[i] * scale * 0.001f * (float) sampleRate);
            sumLen += lineLen[i];
        }
        for (int i = 0; i < kDiff; ++i) diff[i].g = 0.4f + diffA * 0.35f;

        const float meanSec = (sumLen / (float) kLines) / (float) sampleRate;
        const float rt = juce::jlimit(0.3f, 18.0f, 0.71f * std::exp(3.23f * decay01));   // RT60 seconds
        decayGain = juce::jlimit(0.0f, 0.985f, std::pow(10.0f, -3.0f * meanSec / rt));

        aDamp  = onePole(juce::jmap(tone, 0.0f, 1.0f, 2800.0f, 11000.0f));   // in-loop HF damping
        aInHP  = onePole(120.0f);
        aInLP  = onePole(14000.0f);
        aOutHP = onePole(150.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 6500.0f, 13000.0f));   // output air with Tone

        modDepth = mod * (float) (sampleRate * 0.0020);
        modScale = 0.6f + mod * 1.4f;                                        // ~0.05–1.2 Hz range
        erScale  = scale;
        injLevel = 0.28f;
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
        early *= 0.28f;

        // deep input diffusion → silky wash before the tank
        float d = preOut;
        for (int i = 0; i < kDiff; ++i) d = diff[i].process(d);

        // read the 8 lines with modulation + frequency-dependent damping
        float y[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            modPh[i] += (kModRate[i] * modScale) / (float) sampleRate; if (modPh[i] >= 1.0f) modPh[i] -= 1.0f;
            const float rp = (float) fw[i] - (lineLen[i] + std::sin(twoPi * modPh[i]) * modDepth);
            float s = readFdn(i, rp);
            damp[i] += aDamp * (s - damp[i]);                 // HF damping → frequency-dependent decay
            y[i] = damp[i];
        }

        // orthonormal 8×8 Hadamard feedback mix (energy-preserving → smooth + stable)
        float mm[kLines];
        for (int i = 0; i < kLines; ++i) mm[i] = y[i];
        hadamard8(mm);

        const float inj = d * injLevel;
        for (int i = 0; i < kLines; ++i)
        {
            fdn[i][(size_t) fw[i]] = inj + mm[i] * decayGain;
            if (++fw[i] >= fdnMax[i]) fw[i] = 0;
        }

        // decorrelated stereo tap set + early
        float wetL = (y[0] - y[2] + y[5] - y[7]) * 0.5f + early;
        float wetR = (y[1] - y[3] + y[4] - y[6]) * 0.5f + early;

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

    // fast in-place 8-point Walsh–Hadamard transform, orthonormalised (×1/√8).
    static void hadamard8(float* v)
    {
        for (int len = 1; len < kLines; len <<= 1)
            for (int i = 0; i < kLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = v[j], b = v[j + len];
                    v[j] = a + b; v[j + len] = a - b;
                }
        const float norm = 0.35355339059f;   // 1/sqrt(8)
        for (int i = 0; i < kLines; ++i) v[i] *= norm;
    }

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

    static constexpr float kModRate[kLines] = { 0.21f, 0.24f, 0.29f, 0.33f, 0.37f, 0.41f, 0.45f, 0.49f };

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0;
    float  preSamp = 0.0f, decayGain = 0.7f, mod = 0.25f, width = 0.7f, mix = 0.25f,
           duck = 0.2f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f, erScale = 1.0f, injLevel = 0.28f;
    float  aDamp = 0.3f, aInHP = 0.02f, aInLP = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f;
    float  inHPs = 0.0f, inLPs = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 }, duckEnv = 0.0f;

    std::vector<float> pre, fdn[kLines];
    int    fdnMax[kLines] = { 4, 4, 4, 4, 4, 4, 4, 4 }, fw[kLines] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    float  lineLen[kLines] = { 1, 1, 1, 1, 1, 1, 1, 1 }, damp[kLines] = { 0 }, modPh[kLines] = { 0 };
    AllPass diff[kDiff];
};
