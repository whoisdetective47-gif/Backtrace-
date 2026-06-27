#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/Saturation.h"

// =============================================================================
//  TapeEcho ("Reel Echo") — Space-Echo-inspired multi-head tape delay (Phase 8).
//
//  Warm, smoky, unstable tape echo: repeats are recorded to a moving tape line,
//  read from multiple heads, saturated, darkened and softened with each pass,
//  and destabilised by wow/flutter/motor drift. Built for reverse vocal throws,
//  guitar swells, dub feedback and noir sound design.
//
//  Front-panel macros (delayKnobLayout flavor 1):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Movement  p[5] Width     p[6] Duck  p[7] Mix
//
//  Character is a macro over tape age (HF loss, saturation, hiss, dropout).
//  Movement is a macro over wow + flutter + motor drift, modulating delay TIME.
//  Safety: feedback soft-limited (tanh), DC/low-cut + high-cut inside the loop,
//  fractional (interpolated) reads, output ceiling. Feedback clamped < 1.
//
//  Deferred to the advanced pass: selectable Head Pattern (Single/Dual/Triple/
//  Gallop/Wide Ping/Dub Cloud) — this build uses a fixed dual-head voicing.
// =============================================================================
class TapeEcho : public DelayMachine
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        maxDelay   = (int) (sr * 3.0) + 8;
        for (int ch = 0; ch < 2; ++ch) line[ch].assign((size_t) maxDelay, 0.0f);
        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            wp[ch] = 0;
            hfState[ch] = lfState[ch] = outTone[ch] = 0.0f;
        }
        wowPh = wow2Ph = flutPh = motorPh = 0.0f;
        duckEnv = 0.0f; dropGain = 1.0f; rng = 0x2545F491u;
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

        // Character macro → degradation amounts.
        inDrive  = 0.15f + character * 0.55f;
        fbDrive  = 0.25f + character * 0.60f;
        hissLvl  = character * 0.0012f;
        dropRate = character * character * 0.00004f;

        // Darkness = low Tone and/or high Character → lower feedback high-cut.
        const float darkness = juce::jlimit(0.0f, 1.0f, (1.0f - tone) * 0.7f + character * 0.45f);
        aHF  = onePole(juce::jmap(darkness, 0.0f, 1.0f, 11000.0f, 1600.0f));
        aLF  = onePole(95.0f);     // feedback low-cut / DC control
        aOut = onePole(3200.0f);   // output tone split

        // Movement macro → modulation depths (in samples), modulating read time.
        wowDepth   = movement * (float) (sampleRate * 0.0035);
        flutDepth  = movement * (float) (sampleRate * 0.0006);
        motorDepth = movement * (float) (sampleRate * 0.0050);

        widthOff = width * (float) (sampleRate * 0.010);   // up to ~10 ms head spread
        xfeed    = width * 0.30f;                          // crossfeed for stereo glue
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        advance(wowPh,   0.70f); advance(wow2Ph, 0.13f);
        advance(flutPh,  7.00f); advance(motorPh, 0.07f);

        const float wow   = std::sin(twoPi * wowPh) * 0.7f + std::sin(twoPi * wow2Ph) * 0.3f;
        const float flut  = std::sin(twoPi * flutPh);
        const float motor = std::sin(twoPi * motorPh);
        const float mod   = wow * wowDepth + flut * flutDepth + motor * motorDepth;

        // ducking (peak follower on the dry source)
        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.9995f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 3.0f);

        // dropout (rare tape level dips at high Character)
        if (nextFloat() * 0.5f + 0.5f < dropRate) dropTarget = 0.35f;
        dropTarget += (1.0f - dropTarget) * 0.0009f;
        dropGain   += (dropTarget - dropGain) * 0.01f;

        const float inSatL = saturation.process(inL, inDrive, character);
        const float inSatR = saturation.process(inR, inDrive, character);

        // read taps (before writing) — dual head: main + half-time secondary
        const float wetL = readHeads(0, mod, +widthOff * 0.5f);
        const float wetR = readHeads(1, mod, -widthOff * 0.5f);

        // feedback from the main head, filtered + limited, with crossfeed
        const float fbL = feedbackPath(0, mainTap[0]);
        const float fbR = feedbackPath(1, mainTap[1]);
        line[0][(size_t) wp[0]] = inSatL + (fbL * (1.0f - xfeed) + fbR * xfeed) * fbGain;
        line[1][(size_t) wp[1]] = inSatR + (fbR * (1.0f - xfeed) + fbL * xfeed) * fbGain;
        wp[0] = (wp[0] + 1) % maxDelay;
        wp[1] = (wp[1] + 1) % maxDelay;

        // output: tone tilt → hiss/dropout → stereo width → duck → mix
        float oL = toneTilt(0, wetL) + nextFloat() * hissLvl;
        float oR = toneTilt(1, wetR) + nextFloat() * hissLvl;
        oL *= dropGain; oR *= dropGain;

        const float midS  = (oL + oR) * 0.5f;
        const float sideS = (oL - oR) * 0.5f * (width * 2.0f);
        oL = std::tanh(midS + sideS);   // output ceiling protection
        oR = std::tanh(midS - sideS);

        outL = inL * (1.0f - mix) + oL * mix * duckGain;
        outR = inR * (1.0f - mix) + oR * mix * duckGain;
    }

private:
    float readHeads(int ch, float mod, float spread)
    {
        const float pMain = (float) wp[ch] - (baseDelay + mod + spread);
        const float pSec  = (float) wp[ch] - (baseDelay * 0.5f + mod * 0.7f + spread);
        mainTap[ch] = readInterp(ch, pMain);
        const float sec = readInterp(ch, pSec) * 0.55f;
        return mainTap[ch] + sec;
    }

    float feedbackPath(int ch, float tap)
    {
        float s = saturation.process(tap, fbDrive, character);   // tape grit
        hfState[ch] += aHF * (s - hfState[ch]);                  // high-cut: darker repeats
        const float hc = hfState[ch];
        lfState[ch] += aLF * (hc - lfState[ch]);                 // low-cut tracker
        const float band = hc - lfState[ch];                     // remove sub + DC
        return std::tanh(band);                                  // soft safety limit
    }

    float toneTilt(int ch, float wet)
    {
        outTone[ch] += aOut * (wet - outTone[ch]);
        const float high = wet - outTone[ch];
        return outTone[ch] + high * (tone * 1.8f);
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
    void  advance(float& ph, float hz) { ph += hz / (float) sampleRate; if (ph >= 1.0f) ph -= 1.0f; }
    float nextFloat() { rng = rng * 1664525u + 1013904223u; return (float) (int32_t) rng * (1.0f / 2147483648.0f); }

    double sampleRate = 44100.0;
    int    maxDelay = 8;
    float  baseDelay = 22050.0f, fbGain = 0.28f, tone = 0.55f, character = 0.35f,
           movement = 0.20f, width = 0.45f, duck = 0.15f, mix = 0.25f;
    float  inDrive = 0.3f, fbDrive = 0.4f, hissLvl = 0.0f, dropRate = 0.0f;
    float  aHF = 0.1f, aLF = 0.01f, aOut = 0.3f;
    float  wowDepth = 0.0f, flutDepth = 0.0f, motorDepth = 0.0f, widthOff = 0.0f, xfeed = 0.0f;

    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  hfState[2] = { 0, 0 }, lfState[2] = { 0, 0 }, outTone[2] = { 0, 0 }, mainTap[2] = { 0, 0 };
    float  wowPh = 0, wow2Ph = 0, flutPh = 0, motorPh = 0;
    float  duckEnv = 0.0f, dropGain = 1.0f, dropTarget = 1.0f;
    uint32_t rng = 0x2545F491u;
    Saturation saturation;
};
