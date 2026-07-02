#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/Saturation.h"

// =============================================================================
//  TapeWitness ("Tape Witness") — gritty vintage tape-slap echo (Phase 8).
//
//  The rock-and-roll tape delay: direct, punchy, mid-forward and attitude-driven.
//  Audio is pushed through a hot, colored preamp, printed to a worn single-head
//  tape path, and returned as a thick slap/echo. Less ambient than Reel Echo,
//  less smooth/halo than Magnetic Drum — raw, vintage, slightly dangerous.
//
//  Front-panel macros (delayKnobLayout flavor 4):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Movement  p[5] Width     p[6] Duck  p[7] Mix
//
//  Character drives preamp gain, tape wear, feedback grit and transient rounding.
//  Movement = wow + flutter + occasional tape-SPLICE bumps (pitch/level glitch).
//  Repeats lose top and gain saturation each pass; feedback soft-limited for safe
//  tape runaway. Single head (slap), mid-forward voice, DC/low-cut + high-cut loop.
//
//  Deferred to the advanced pass: Mode switch (Slap/Echo/Long/Dirty/Worn/Reverse),
//  separate Preamp/Wear/Splice controls, tempo Sync, presets.
// =============================================================================
class TapeWitness : public DelayMachine
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
            wp[ch] = 0; inLP[ch] = hfState[ch] = lfState[ch] = outTone[ch] = 0.0f;
        }
        wowPh = flutPh = 0.0f; spliceCountdown = 2000; spliceEnv = 0.0f;
        duckEnv = 0.0f; rng = 0x1F123BB5u;
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

        preGain  = 1.0f + character * 1.8f;        // hot preamp: thickens as driven
        preDrive = 0.30f + character * 0.55f;
        fbDrive  = 0.30f + character * 0.65f;      // grittier feedback than tape/drum
        hissLvl  = character * 0.0009f;
        aPre     = onePole(5200.0f);               // mid-forward: roll the driven top

        const float darkness = juce::jlimit(0.0f, 1.0f, (1.0f - tone) * 0.6f + character * 0.45f);
        aHF  = onePole(juce::jmap(darkness, 0.0f, 1.0f, 10000.0f, 1700.0f));
        aLF  = onePole(100.0f);
        aOut = onePole(3400.0f);

        wowDepth   = movement * (float) (sampleRate * 0.0030);
        flutDepth  = movement * (float) (sampleRate * 0.0009);
        spliceDepth = (0.4f + movement * 0.6f + character * 0.5f) * (float) (sampleRate * 0.0009);
        widthOff   = width * (float) (sampleRate * 0.008);
        xfeed      = width * 0.22f;
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        advance(wowPh, 0.5f); advance(flutPh, 6.0f);

        // occasional tape-splice / tension bump (pitch + level glitch)
        if (--spliceCountdown <= 0)
        {
            spliceEnv = 0.6f + 0.4f * std::abs(nextFloat());
            const float rate = 0.6f + movement * 1.2f + character * 0.8f;       // more events when driven/moving
            spliceCountdown = (int) (sampleRate * (1.4f - juce::jmin(1.1f, rate)) + sampleRate * 0.2f);
        }
        spliceEnv *= 0.9990f;

        const float wow  = std::sin(twoPi * wowPh);
        const float flut = std::sin(twoPi * flutPh);
        const float mod  = wow * wowDepth + flut * flutDepth + spliceEnv * spliceDepth;
        const float spliceDip = 1.0f - spliceEnv * 0.18f;

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.9995f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 3.0f);

        const float preL = preamp(0, inL);
        const float preR = preamp(1, inR);

        const float wetL = readInterp(0, (float) wp[0] - (baseDelay + mod + widthOff * 0.5f));
        const float wetR = readInterp(1, (float) wp[1] - (baseDelay + mod - widthOff * 0.5f));

        const float fbL = feedbackPath(0, wetL);
        const float fbR = feedbackPath(1, wetR);
        line[0][(size_t) wp[0]] = preL + (fbL * (1.0f - xfeed) + fbR * xfeed) * fbGain;
        line[1][(size_t) wp[1]] = preR + (fbR * (1.0f - xfeed) + fbL * xfeed) * fbGain;
        wp[0] = (wp[0] + 1) % maxDelay;
        wp[1] = (wp[1] + 1) % maxDelay;

        float oL = (toneTilt(0, wetL) + nextFloat() * hissLvl) * spliceDip;
        float oR = (toneTilt(1, wetR) + nextFloat() * hissLvl) * spliceDip;

        const float midS  = (oL + oR) * 0.5f;
        const float sideS = (oL - oR) * 0.5f * (width * 1.6f);   // narrower than the ambient modes
        oL = std::tanh(midS + sideS);
        oR = std::tanh(midS - sideS);

        outL = inL * (1.0f - mix) + oL * mix * duckGain;
        outR = inR * (1.0f - mix) + oR * mix * duckGain;
    }

private:
    // Hot preamp: drive hard, then roll the top → thick, mid-forward, chewy.
    float preamp(int ch, float in)
    {
        const float driven = saturation.process(in * preGain, preDrive, character * 0.7f);
        inLP[ch] += aPre * (driven - inLP[ch]);
        return inLP[ch];
    }

    float feedbackPath(int ch, float tap)
    {
        float s = saturation.process(tap, fbDrive, character * 0.8f);   // grit grows per pass
        hfState[ch] += aHF * (s - hfState[ch]);                         // lose top each repeat
        const float hc = hfState[ch];
        lfState[ch] += aLF * (hc - lfState[ch]);
        return std::tanh(hc - lfState[ch]);                             // safe tape runaway
    }

    float toneTilt(int ch, float wet)
    {
        outTone[ch] += aOut * (wet - outTone[ch]);
        const float high = wet - outTone[ch];
        return outTone[ch] + high * (tone * 1.2f);   // mid-forward, modest top
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
    float  baseDelay = 13230.0f, fbGain = 0.30f, tone = 0.50f, character = 0.45f,
           movement = 0.20f, width = 0.35f, duck = 0.12f, mix = 0.30f;
    float  preGain = 1.5f, preDrive = 0.4f, fbDrive = 0.5f, hissLvl = 0.0f;
    float  aPre = 0.3f, aHF = 0.1f, aLF = 0.01f, aOut = 0.3f;
    float  wowDepth = 0.0f, flutDepth = 0.0f, spliceDepth = 0.0f, widthOff = 0.0f, xfeed = 0.0f;

    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  inLP[2] = { 0, 0 }, hfState[2] = { 0, 0 }, lfState[2] = { 0, 0 }, outTone[2] = { 0, 0 };
    float  wowPh = 0, flutPh = 0, spliceEnv = 0.0f, duckEnv = 0.0f;
    int    spliceCountdown = 2000;
    uint32_t rng = 0x1F123BB5u;
    Saturation saturation;
};
