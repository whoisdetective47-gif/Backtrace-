#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/Saturation.h"

// =============================================================================
//  MagneticDrum ("Magnetic Drum") — rotating magnetic-drum style echo (Phase 8).
//
//  Haunting, oily, circular drum echo: audio written to a rotating magnetic
//  surface, read by multiple heads, smeared into a halo and destabilised by
//  rotational motor drift. Rounder, darker and more hypnotic than Reel Echo —
//  the cinematic vintage delay. Excellent on reverse vocal/synth swells.
//
//  Front-panel macros (delayKnobLayout flavor 3):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Movement  p[5] Width     p[6] Duck  p[7] Mix
//
//  vs Reel Echo: 3-head drum taps, an all-pass diffusion network in the feedback
//  loop (Halo/smear), slow circular+uneven drift (not tape flutter), rounder
//  magnetic saturation, faint mechanical hum, orbital stereo. Safety: soft-
//  limited + DC/low-cut + high-cut feedback, interpolated reads, output ceiling.
//
//  Deferred to the advanced pass: selectable Head Pattern, separate Halo/Smear/
//  Drift controls, tempo Sync, named presets. Fixed triple-head voicing for now.
// =============================================================================
class MagneticDrum : public DelayMachine
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        maxDelay   = (int) (sr * 3.0) + 8;
        for (int ch = 0; ch < 2; ++ch)
        {
            line[ch].assign((size_t) maxDelay, 0.0f);
            ap1[ch].prepare((int) (sr * 0.0056));   // ~5.6 ms diffusion
            ap2[ch].prepare((int) (sr * 0.0093));   // ~9.3 ms diffusion
        }
        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            wp[ch] = 0; hfState[ch] = lfState[ch] = outTone[ch] = 0.0f;
            ap1[ch].reset(); ap2[ch].reset();
        }
        rotPh = rot2Ph = orbitPh = humPh = 0.0f;
        duckEnv = 0.0f; rng = 0x9E3779B1u;
    }

    void setParams(const float* p) override
    {
        baseDelay = juce::jlimit(2.0f, (float) (maxDelay - 4), (float) (p[0] * 0.001 * sampleRate));
        fbGain    = juce::jlimit(0.0f, 0.95f, p[1]);
        tone      = juce::jlimit(0.0f, 1.0f, p[2]);
        character = juce::jlimit(0.0f, 1.0f, p[3]);
        movement  = juce::jlimit(0.0f, 1.0f, p[4]);
        width     = juce::jlimit(0.0f, 1.0f, p[5]);
        duck      = juce::jlimit(0.0f, 1.0f, p[6]);
        mix       = juce::jlimit(0.0f, 1.0f, p[7]);

        inDrive  = 0.10f + character * 0.45f;
        fbDrive  = 0.20f + character * 0.55f;
        humLvl   = character * 0.0007f;
        haloMix  = juce::jlimit(0.0f, 0.9f, 0.20f + character * 0.45f + fbGain * 0.25f); // halo grows with fb
        ap1[0].g = ap2[0].g = ap1[1].g = ap2[1].g = 0.5f + character * 0.18f;

        // Rounder/darker than tape: stronger high-cut, gentler output brightness.
        const float darkness = juce::jlimit(0.0f, 1.0f, (1.0f - tone) * 0.65f + character * 0.40f);
        aHF  = onePole(juce::jmap(darkness, 0.0f, 1.0f, 9000.0f, 1400.0f));
        aLF  = onePole(110.0f);
        aOut = onePole(3000.0f);

        // Circular drift (slow + uneven), little fast flutter.
        driftDepth = movement * (float) (sampleRate * 0.0045);
        widthOff   = width * (float) (sampleRate * 0.012);
        orbitAmt   = width * 0.30f;
        xfeed      = width * 0.28f;
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        advance(rotPh, 0.30f); advance(rot2Ph, 0.077f); advance(orbitPh, 0.15f); advance(humPh, 60.0f);

        const float drift = std::sin(twoPi * rotPh) * 0.6f + std::sin(twoPi * rot2Ph) * 0.4f;
        const float mod   = drift * driftDepth;
        const float hum   = std::sin(twoPi * humPh) * humLvl;

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99975f);                 // smoother release than tape
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        const float inSatL = saturation.process(inL, inDrive, character * 0.5f);
        const float inSatR = saturation.process(inR, inDrive, character * 0.5f);

        const float wetL = readHeads(0, mod, +widthOff * 0.5f);
        const float wetR = readHeads(1, mod, -widthOff * 0.5f);

        const float fbL = feedbackPath(0, mainTap[0]);
        const float fbR = feedbackPath(1, mainTap[1]);
        line[0][(size_t) wp[0]] = inSatL + (fbL * (1.0f - xfeed) + fbR * xfeed) * fbGain;
        line[1][(size_t) wp[1]] = inSatR + (fbR * (1.0f - xfeed) + fbL * xfeed) * fbGain;
        wp[0] = (wp[0] + 1) % maxDelay;
        wp[1] = (wp[1] + 1) % maxDelay;

        float oL = toneTilt(0, wetL) + hum;
        float oR = toneTilt(1, wetR) + hum;

        // orbital stereo: slow L/R level sway + M/S widen
        const float orbit = std::sin(twoPi * orbitPh) * orbitAmt;
        oL *= (1.0f + orbit); oR *= (1.0f - orbit);
        const float midS  = (oL + oR) * 0.5f;
        const float sideS = (oL - oR) * 0.5f * (width * 2.0f);
        oL = std::tanh(midS + sideS);
        oR = std::tanh(midS - sideS);

        outL = inL * (1.0f - mix) + oL * mix * duckGain;
        outR = inR * (1.0f - mix) + oR * mix * duckGain;
    }

private:
    struct AllPass
    {
        std::vector<float> buf; int idx = 0, len = 1; float g = 0.5f;
        void prepare(int n) { len = juce::jmax(1, n); buf.assign((size_t) len, 0.0f); idx = 0; }
        void reset() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process(float x)
        {
            const float d = buf[(size_t) idx];
            const float y = -g * x + d;
            buf[(size_t) idx] = x + g * y;
            if (++idx >= len) idx = 0;
            return y;
        }
    };

    // Three drum heads at 1.0 / 0.75 / 0.5 of the base, with small fixed offsets.
    float readHeads(int ch, float mod, float spread)
    {
        const float base = (float) wp[ch];
        mainTap[ch]      = readInterp(ch, base - (baseDelay + mod + spread));
        const float h2   = readInterp(ch, base - (baseDelay * 0.75f + mod * 0.8f + spread + 13.0f)) * 0.65f;
        const float h3   = readInterp(ch, base - (baseDelay * 0.50f + mod * 0.6f + spread - 7.0f))  * 0.45f;
        return mainTap[ch] + h2 + h3;
    }

    // Magnetic feedback: round saturation → all-pass diffusion (halo) → tone/DC
    // shaping → soft limit.
    float feedbackPath(int ch, float tap)
    {
        // Round magnetic saturation normalised to ~unity small-signal gain, so the
        // loop decays at ~fbGain per repeat (clean sustaining drum tail, not buildup).
        float s = std::tanh(tap * (1.0f + fbDrive)) / (1.0f + fbDrive * 0.7f);
        const float diff = ap2[ch].process(ap1[ch].process(s));
        s = s * (1.0f - haloMix) + diff * haloMix;                                     // halo / smear
        hfState[ch] += aHF * (s - hfState[ch]);
        const float hc = hfState[ch];
        lfState[ch] += aLF * (hc - lfState[ch]);
        return std::tanh(hc - lfState[ch]);
    }

    float toneTilt(int ch, float wet)
    {
        outTone[ch] += aOut * (wet - outTone[ch]);
        const float high = wet - outTone[ch];
        return outTone[ch] + high * (tone * 1.4f);   // rounder than tape's 1.8
    }

    float readInterp(int ch, float pos) const
    {
        while (pos < 0.0f)              pos += (float) maxDelay;
        while (pos >= (float) maxDelay) pos -= (float) maxDelay;
        const int i0 = (int) pos, i1 = (i0 + 1) % maxDelay;
        const float frac = pos - (float) i0;
        return line[ch][(size_t) i0] * (1.0f - frac) + line[ch][(size_t) i1] * frac;
    }

    float onePole(float fc) const
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
    }
    void advance(float& ph, float hz) { ph += hz / (float) sampleRate; if (ph >= 1.0f) ph -= 1.0f; }

    double sampleRate = 44100.0;
    int    maxDelay = 8;
    float  baseDelay = 22050.0f, fbGain = 0.32f, tone = 0.50f, character = 0.40f,
           movement = 0.25f, width = 0.50f, duck = 0.15f, mix = 0.25f;
    float  inDrive = 0.3f, fbDrive = 0.4f, humLvl = 0.0f, haloMix = 0.3f;
    float  aHF = 0.1f, aLF = 0.01f, aOut = 0.3f, driftDepth = 0.0f, widthOff = 0.0f, orbitAmt = 0.0f, xfeed = 0.0f;

    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  hfState[2] = { 0, 0 }, lfState[2] = { 0, 0 }, outTone[2] = { 0, 0 }, mainTap[2] = { 0, 0 };
    float  rotPh = 0, rot2Ph = 0, orbitPh = 0, humPh = 0, duckEnv = 0.0f;
    uint32_t rng = 0x9E3779B1u;
    AllPass ap1[2], ap2[2];
    Saturation saturation;
};
