#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/BitReducer.h"

// =============================================================================
//  PedalDigital ("Digital Pedal") — clean, punchy Boss-DD-style pedal delay.
//
//  Clear digital repeats with a defined attack: brighter and tighter than the
//  tape modes, articulate feedback with a soft limiter (intense but no smear),
//  a morphing Tone (dark→neutral→bright repeats), Character that adds early
//  digital grain via BitReducer, a simple/obvious chorus-like Mod, M/S Width,
//  and input-keyed Ducking. Great for rhythmic throws and cutting reverse stabs.
//
//  Params (per delayKnobLayout flavor 2):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Mod       p[5] Width     p[6] Mix   p[7] Duck
//
//  Hold/Freeze + Standard/Short/Long/Glitch modes are noted for a later pass.
// =============================================================================
class PedalDigital : public DelayMachine
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        maxDelay   = (int) (sr * 2.5) + 4;
        for (int ch = 0; ch < 2; ++ch)
        {
            line[ch].assign((size_t) maxDelay, 0.0f);
            wp[ch] = 0; toneState[ch] = 0.0f;
        }
        toneCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 3000.0f / (float) sampleRate);
        modPhase = 0.0f; duckEnv = 0.0f;
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            wp[ch] = 0; toneState[ch] = 0.0f;
        }
        modPhase = 0.0f; duckEnv = 0.0f;
    }

    void setParams(const float* p) override
    {
        delaySamples = juce::jlimit(1.0f, (float) (maxDelay - 4), (float) (p[0] * 0.001 * sampleRate));
        fb        = juce::jlimit(0.0f, 0.95f, p[1]);
        tone      = juce::jlimit(0.0f, 1.0f, p[2]);
        character = juce::jlimit(0.0f, 1.0f, p[3]);
        mod       = juce::jlimit(0.0f, 1.0f, p[4]);
        width     = juce::jlimit(0.0f, 1.0f, p[5]);
        mix       = juce::jlimit(0.0f, 1.0f, p[6]);
        duck      = juce::jlimit(0.0f, 1.0f, p[7]);

        // Character → early digital grain (16 bits = clean, down to ~6 bits = gritty)
        bitReducer.setBitDepth (juce::jmap(character, 0.0f, 1.0f, 16.0f, 6.0f));
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        // simple, obvious chorus-style modulation (clean/tight at mod = 0)
        modPhase += kModRate / (float) sampleRate; if (modPhase >= 1.0f) modPhase -= 1.0f;
        const float modOff = std::sin(juce::MathConstants<float>::twoPi * modPhase)
                           * mod * (float) (sampleRate * 0.004);

        // input-keyed ducking (peak follower on the dry signal)
        const float dryPk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(dryPk, duckEnv * kDuckRelease);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 4.0f);

        float wetL = tap(0, inL, modOff);
        float wetR = tap(1, inR, modOff);

        // stereo width (M/S): 0 = mono, 0.5 = neutral, 1 = wide
        const float midS  = (wetL + wetR) * 0.5f;
        const float sideS = (wetL - wetR) * 0.5f * (width * 2.0f);
        wetL = midS + sideS;
        wetR = midS - sideS;

        outL = inL * (1.0f - mix) + wetL * mix * duckGain;
        outR = inR * (1.0f - mix) + wetR * mix * duckGain;
    }

private:
    float tap(int ch, float in, float mod)
    {
        const float wet = readInterp(ch, (float) wp[ch] - (delaySamples + mod));

        // Tone in the feedback path: morph dark→neutral→bright around a fixed split.
        toneState[ch] = wet * (1.0f - toneCoeff) + toneState[ch] * toneCoeff;
        const float high = wet - toneState[ch];
        float shaped = toneState[ch] + high * (tone * 1.8f);

        if (character > 0.001f)
            shaped = bitReducer.process(shaped);          // early digital grain

        const float lim = std::tanh(shaped);              // articulate safe limiting
        line[ch][(size_t) wp[ch]] = in + lim * fb;
        wp[ch] = (wp[ch] + 1) % maxDelay;
        return shaped;
    }

    float readInterp(int ch, float pos) const
    {
        while (pos < 0.0f)              pos += (float) maxDelay;
        while (pos >= (float) maxDelay) pos -= (float) maxDelay;
        const int i0 = (int) pos, i1 = (i0 + 1) % maxDelay;
        const float frac = pos - (float) i0;
        return line[ch][(size_t) i0] * (1.0f - frac) + line[ch][(size_t) i1] * frac;
    }

    static constexpr float kModRate = 1.2f, kDuckRelease = 0.9995f;

    double sampleRate = 44100.0;
    int    maxDelay = 4;
    float  delaySamples = 4410.0f, fb = 0.4f, tone = 0.5f, character = 0.2f,
           mod = 0.0f, width = 0.5f, mix = 0.4f, duck = 0.0f;
    float  toneCoeff = 0.0f;
    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  toneState[2] = { 0.0f, 0.0f }, modPhase = 0.0f, duckEnv = 0.0f;
    BitReducer bitReducer;
};
