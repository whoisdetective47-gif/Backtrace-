#pragma once
#include <JuceHeader.h>
#include "DSP/ReverbSpace.h"

// =============================================================================
//  SpringVerb ("Rust Spring") — metallic spring-tank reverb (Phase 9, final).
//
//  The evidence tank: splashy, metallic, chirpy, dubby, imperfect, alive. The
//  spring identity comes from a long DISPERSIVE ALLPASS CASCADE inside a feedback
//  loop (frequency-dependent group delay → the chirp/sproing), a modulated tank
//  delay, and a transient-splash exciter that fires the dispersion on hits — not
//  from a metallic EQ on a smooth tail. Lower diffusion than plate/hall by design.
//
//  Front-panel macros (reverbKnobLayout flavor 5):
//    p[0] Size   p[1] Decay  p[2] PreDelay(ms)  p[3] Tone   p[4] Diffusion
//    p[5] Mod    p[6] Width  p[7] Mix           p[8] Duck   p[9] Output(dB)
//
//  Size = tank length / dispersion amount (chirp depth), NOT room. Decay→RT60
//  0.25–6 s (kept weird/dubby, not hall-length). SAFETY-FIRST: tanh in the loop,
//  feedback capped ≤ 0.9, in-loop low control + damping, output ceiling, no
//  runaway resonance. Dual slightly-detuned tanks per channel for mono-safe width.
//
//  Deferred: dedicated tank-drive/rattle knob, modal-network refinement, presets.
// =============================================================================
class SpringVerb : public ReverbSpace
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        preLen = (int) (sr * 0.09) + 4;
        pre.assign((size_t) preLen, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            sd[ch].prepare((int) (sr * 0.12) + 4);
            df[ch][0].prepare((int) (sr * 0.0047) + 4);
            df[ch][1].prepare((int) (sr * 0.0061) + 4);
        }
        reset();
    }

    void reset() override
    {
        std::fill(pre.begin(), pre.end(), 0.0f); preW = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            sd[ch].reset(); df[ch][0].reset(); df[ch][1].reset();
            for (int k = 0; k < kMaxAP; ++k) { apX1[ch][k] = apY1[ch][k] = 0.0f; }
            damp[ch] = lc[ch] = pf[ch] = ps[ch] = tankFB[ch] = 0.0f;
        }
        inHPs = inLPs = outHP = outHC[0] = outHC[1] = 0.0f;
        envFast = envSlow = duckEnv = 0.0f; modPh = modPh2 = 0.0f;
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

        const float tankMs = 20.0f + size * 60.0f;                 // tank length grows with Size
        sd[0].setDelay(tankMs * 0.001f * (float) sampleRate);
        sd[1].setDelay(tankMs * 1.08f * 0.001f * (float) sampleRate);  // detuned 2nd tank
        numAP = 60 + (int) (size * 60.0f);                          // dispersion / chirp depth
        df[0][0].g = df[0][1].g = df[1][0].g = df[1][1].g = 0.6f;
        diffMix = diffA * 0.7f;

        const float loopSec = (tankMs * 0.001f) + (float) numAP * 4.0f / (float) sampleRate;
        const float rt = juce::jlimit(0.25f, 6.0f, 0.6f * std::exp(2.30f * decay01));   // RT60 0.25–6 s
        decayGain = juce::jlimit(0.0f, 0.90f, std::pow(10.0f, -3.0f * loopSec / rt));   // capped for safety

        aDamp = onePole(juce::jmap(tone, 0.0f, 1.0f, 3500.0f, 10000.0f));
        aLC   = onePole(juce::jmap(tone, 0.0f, 1.0f, 180.0f, 120.0f));   // in-loop low control
        aInHP = onePole(160.0f);
        aInLP = onePole(12000.0f);
        aOutHP = onePole(210.0f);
        aOutHC = onePole(juce::jmap(tone, 0.0f, 1.0f, 4000.0f, 12000.0f));
        aPF = onePole(3800.0f); aPS = onePole(1300.0f);                  // presence band ~1.3–3.8 kHz

        modDepth = (0.2f + mod * 0.8f) * 0.001f * (float) sampleRate;    // 0.2–1.0 ms
        modScale = 0.4f + mod * 1.6f;                                    // 0.05–0.9 Hz
        splashGain = 0.6f;
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        modPh  += (0.17f * modScale) / (float) sampleRate; if (modPh  >= 1.0f) modPh  -= 1.0f;
        modPh2 += (0.31f * modScale) / (float) sampleRate; if (modPh2 >= 1.0f) modPh2 -= 1.0f;
        const float wob = std::sin(twoPi * modPh) * 0.7f + std::sin(twoPi * modPh2) * 0.3f;
        const float modA = wob * modDepth;
        const float modB = (std::sin(twoPi * (modPh + 0.4f))) * modDepth;

        float mono = (inL + inR) * 0.5f;
        inHPs += aInHP * (mono - inHPs); mono -= inHPs;
        inLPs += aInLP * (mono - inLPs); mono  = inLPs;

        const float preOut = readPre(preSamp);
        pre[(size_t) preW] = mono;
        if (++preW >= preLen) preW = 0;

        // transient splash exciter — fires the dispersion on hits, level-protected
        const float af = std::abs(preOut);
        envFast += 0.30f * (af - envFast);
        envSlow += 0.0020f * (af - envSlow);
        const float splash = juce::jmin(0.5f, juce::jmax(0.0f, envFast - envSlow) * 4.0f) * splashGain;

        float wL = springTank(0, preOut, modA, splash);
        float wR = springTank(1, preOut, modB, splash * 0.9f);

        // diffusion blend (keeps spring identity even at high Diffuse)
        wL = wL * (1.0f - diffMix) + df[0][1].process(df[0][0].process(wL, 0.0f), 0.0f) * diffMix;
        wR = wR * (1.0f - diffMix) + df[1][1].process(df[1][0].process(wR, 0.0f), 0.0f) * diffMix;

        // presence peak (~1.3–3.8 kHz) for spring identity
        pf[0] += aPF * (wL - pf[0]); ps[0] += aPS * (wL - ps[0]); wL += (pf[0] - ps[0]) * 0.30f;
        pf[1] += aPF * (wR - pf[1]); ps[1] += aPS * (wR - ps[1]); wR += (pf[1] - ps[1]) * 0.30f;

        outHP += aOutHP * (((wL + wR) * 0.5f) - outHP);
        wL -= outHP; wR -= outHP;
        const float midS  = (wL + wR) * 0.5f;
        const float sideS = (wL - wR) * 0.5f * (width * 1.6f);     // focused, not the widest
        wL = midS + sideS; wR = midS - sideS;

        outHC[0] += aOutHC * (wL - outHC[0]); wL = std::tanh(outHC[0] * outGain);
        outHC[1] += aOutHC * (wR - outHC[1]); wR = std::tanh(outHC[1] * outGain);

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99965f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        outL = inL * (1.0f - mix) + wL * mix * duckGain;
        outR = inR * (1.0f - mix) + wR * mix * duckGain;
    }

private:
    struct Delay
    {
        std::vector<float> buf; int w = 0, len = 1; float dly = 1.0f;
        void prepare(int n) { len = juce::jmax(2, n); buf.assign((size_t) len, 0.0f); w = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        void setDelay(float d) { dly = juce::jlimit(1.0f, (float) (len - 2), d); }
        float processMod(float x, float modOff)
        {
            float rd = (float) w - (dly + modOff);
            while (rd < 0.0f) rd += (float) len;
            while (rd >= (float) len) rd -= (float) len;
            const int i0 = (int) rd, i1 = (i0 + 1) % len;
            const float f = rd - (float) i0;
            const float y = buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
            buf[(size_t) w] = x; if (++w >= len) w = 0;
            return y;
        }
    };

    struct AllPass
    {
        std::vector<float> buf; int w = 0, len = 1; float g = 0.6f;
        void prepare(int n) { len = juce::jmax(2, n); buf.assign((size_t) len, 0.0f); w = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        float process(float x, float)
        {
            const float d = buf[(size_t) w];
            const float y = -g * x + d;
            buf[(size_t) w] = x + g * y; if (++w >= len) w = 0;
            return y;
        }
    };

    // Spring tank: feedback loop = delay → dispersive allpass cascade → damping →
    // low control → soft limit. The cascade creates the chirp; splash excites it.
    float springTank(int ch, float in, float modOff, float splash)
    {
        float loopIn = in + tankFB[ch] * decayGain;
        float t = sd[ch].processMod(loopIn, modOff);
        t += splash;                                   // excite the dispersion on transients

        for (int k = 0; k < numAP; ++k)                // dispersive allpass cascade → spring chirp
        {
            const float x = t;
            const float y = kDisp * x + apX1[ch][k] - kDisp * apY1[ch][k];
            apX1[ch][k] = x; apY1[ch][k] = y; t = y;
        }

        damp[ch] += aDamp * (t - damp[ch]); t = damp[ch];
        lc[ch]   += aLC   * (t - lc[ch]);   t -= lc[ch];          // low control (no rumble)
        t = std::tanh(t * 1.2f) * 0.85f;                          // safety limit in the loop
        tankFB[ch] = t;
        return t;
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

    float onePole(float fc) const
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
    }

    static constexpr int   kMaxAP = 120;
    static constexpr float kDisp  = 0.62f;   // dispersion coefficient

    double sampleRate = 44100.0;
    int    preLen = 4, preW = 0, numAP = 80;
    float  preSamp = 0.0f, decayGain = 0.6f, mod = 0.2f, width = 0.58f, mix = 0.22f,
           duck = 0.12f, outGain = 1.0f, modDepth = 0.0f, modScale = 1.0f, diffMix = 0.3f, splashGain = 0.6f;
    float  aDamp = 0.3f, aLC = 0.02f, aInHP = 0.02f, aInLP = 0.5f, aOutHP = 0.02f, aOutHC = 0.5f, aPF = 0.4f, aPS = 0.1f;
    float  inHPs = 0.0f, inLPs = 0.0f, outHP = 0.0f, outHC[2] = { 0, 0 };
    float  envFast = 0.0f, envSlow = 0.0f, duckEnv = 0.0f, modPh = 0.0f, modPh2 = 0.0f;

    std::vector<float> pre;
    Delay   sd[2];
    AllPass df[2][2];
    float   apX1[2][kMaxAP] = {}, apY1[2][kMaxAP] = {};
    float   damp[2] = { 0, 0 }, lc[2] = { 0, 0 }, pf[2] = { 0, 0 }, ps[2] = { 0, 0 }, tankFB[2] = { 0, 0 };
};
