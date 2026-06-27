#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/BitReducer.h"

// =============================================================================
//  ColdRack ("Cold Rack") — PCM-style studio digital delay (Phase 8).
//
//  The polished, mix-ready rack delay: clean, wide, controlled, modulated.
//  Not tape, drum or pedal — its character is smooth digital modulation, stereo
//  imaging, all-pass diffusion, controlled filtering and ducking, with a refined
//  early-digital sheen. The "finished record" delay for vocals/synths/reverse.
//
//  Front-panel macros (delayKnobLayout flavor 5):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Movement  p[5] Width     p[6] Duck  p[7] Mix
//
//  Character → diffusion amount + early-digital color (light BitReducer) + polish.
//  Movement  → smooth, intentional modulation (depth + rate + stereo offset).
//  Width     → stereo offset + crossfeed/ping-pong + stereo mod variation.
//  Clean stable feedback (low/high-cut + soft limit + DC), mono-compatible width.
//
//  Deferred to the advanced pass: Mode switch (Stereo/Ping-Pong/Dual/Mod/Diffused/
//  Wide Vocal/Reverse), separate Mod Rate/Depth/Diffusion/Dual-time controls,
//  tempo Sync, presets.
// =============================================================================
class ColdRack : public DelayMachine
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        maxDelay   = (int) (sr * 3.0) + 8;
        for (int ch = 0; ch < 2; ++ch)
        {
            line[ch].assign((size_t) maxDelay, 0.0f);
            ap1[ch].prepare((int) (sr * 0.0123));   // ~12 ms
            ap2[ch].prepare((int) (sr * 0.0190));   // ~19 ms
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
        modPh = 0.0f; duckEnv = 0.0f;
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

        diffAmt  = juce::jlimit(0.0f, 0.95f, character * 0.9f);   // smear into ambient cloud
        ap1[0].g = ap2[0].g = ap1[1].g = ap2[1].g = 0.62f;
        bitReducer.setBitDepth(juce::jmap(character, 0.0f, 1.0f, 16.0f, 12.5f)); // subtle early-digital color

        // Polished tilt: clear top, controlled low end.
        const float darkness = juce::jlimit(0.0f, 1.0f, (1.0f - tone) * 0.8f);
        aHF  = onePole(juce::jmap(darkness, 0.0f, 1.0f, 14000.0f, 2500.0f));
        aLF  = onePole(120.0f);
        aOut = onePole(2800.0f);

        modRate  = 0.20f + movement * 3.5f;                       // intentional, controlled
        modDepth = movement * (float) (sampleRate * 0.0030);
        modOff   = 0.25f * width;                                 // stereo mod variation
        stereoOff = width * (float) (sampleRate * 0.011);
        xfeed     = width * 0.45f;                                // crossfeed / ping-pong
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        modPh += modRate / (float) sampleRate; if (modPh >= 1.0f) modPh -= 1.0f;
        const float modL = std::sin(twoPi * modPh) * modDepth;
        const float modR = std::sin(twoPi * (modPh + modOff)) * modDepth;

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99965f);             // smooth, slow release
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        // diffused wet feeds BOTH output and feedback → cloud builds with feedback
        const float wetL = diffuse(0, readInterp(0, (float) wp[0] - (baseDelay + stereoOff + modL)));
        const float wetR = diffuse(1, readInterp(1, (float) wp[1] - (baseDelay - stereoOff + modR)));

        const float fbL = feedbackPath(0, wetL);
        const float fbR = feedbackPath(1, wetR);
        line[0][(size_t) wp[0]] = inL + (fbL * (1.0f - xfeed) + fbR * xfeed) * fbGain;
        line[1][(size_t) wp[1]] = inR + (fbR * (1.0f - xfeed) + fbL * xfeed) * fbGain;
        wp[0] = (wp[0] + 1) % maxDelay;
        wp[1] = (wp[1] + 1) % maxDelay;

        float oL = toneTilt(0, wetL);
        float oR = toneTilt(1, wetR);
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
        std::vector<float> buf; int idx = 0, len = 1; float g = 0.6f;
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

    float diffuse(int ch, float wet)
    {
        const float d = ap2[ch].process(ap1[ch].process(wet));
        return wet * (1.0f - diffAmt) + d * diffAmt;
    }

    float feedbackPath(int ch, float wet)
    {
        float s = bitReducer.process(wet);          // subtle early-digital color
        hfState[ch] += aHF * (s - hfState[ch]);     // controlled high-cut
        const float hc = hfState[ch];
        lfState[ch] += aLF * (hc - lfState[ch]);    // controlled low-cut / DC
        return std::tanh(hc - lfState[ch]);         // clean soft limit
    }

    float toneTilt(int ch, float wet)
    {
        outTone[ch] += aOut * (wet - outTone[ch]);
        const float high = wet - outTone[ch];
        return outTone[ch] + high * (tone * 1.6f);  // glossy but clear
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

    double sampleRate = 44100.0;
    int    maxDelay = 8;
    float  baseDelay = 22050.0f, fbGain = 0.30f, tone = 0.60f, character = 0.30f,
           movement = 0.20f, width = 0.60f, duck = 0.20f, mix = 0.25f;
    float  diffAmt = 0.3f, aHF = 0.1f, aLF = 0.01f, aOut = 0.3f;
    float  modRate = 1.0f, modDepth = 0.0f, modOff = 0.0f, stereoOff = 0.0f, xfeed = 0.0f;

    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  hfState[2] = { 0, 0 }, lfState[2] = { 0, 0 }, outTone[2] = { 0, 0 };
    float  modPh = 0.0f, duckEnv = 0.0f;
    AllPass ap1[2], ap2[2];
    BitReducer bitReducer;
};
