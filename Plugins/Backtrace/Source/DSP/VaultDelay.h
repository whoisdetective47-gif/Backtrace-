#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/SampleRateReducer.h"
#include "DSP/BitReducer.h"
#include "DSP/JitterEngine.h"

// =============================================================================
//  VaultDelay ("Vault Delay") — dirty sampler / Dust Vault delay (Phase 8, final).
//
//  The signature Sound Detective delay: a phrase captured into the Vault, copied
//  to an old sampler, played back through a broken digital clock, and fed back
//  until the evidence falls apart. Damaged digital — crunchy, aliased, jittery,
//  haunted. Not tape (Reel Echo), not oily (Magnetic Drum), not preamp grit
//  (Tape Witness), not polished (Cold Rack).
//
//  Front-panel macros (delayKnobLayout flavor 6):
//    p[0] Time(ms)  p[1] Feedback  p[2] Tone  p[3] Character
//    p[4] Movement  p[5] Width     p[6] Duck  p[7] Mix
//
//  KEY: degradation lives INSIDE the feedback loop. SR + bit reduction sit at the
//  write point (input + feedback), so each recirculation degrades further. Jitter
//  + crunch + filtering shape the feedback signal. Reuses SampleRateReducer,
//  BitReducer and JitterEngine from Common/DSP.
//    Character → SR reduction, bit depth, crunch, dust, bandwidth loss.
//    Movement  → clock jitter + random-walk pitch drift (broken clock, not wow).
//  Safety: DC/low-cut + high-cut in loop, soft limit, output ceiling, mono-safe.
//
//  Deferred to the advanced pass: sub-modes (12-Bit Vault/8-Bit Evidence/Broken
//  Clock/VHS Ghost/Dust Loop/Crushed Ping-Pong/Haunted Buffer), separate SR/Bit/
//  Jitter/Drift/Dust controls, tempo Sync, presets.
// =============================================================================
class VaultDelay : public DelayMachine
{
public:
    void prepare(double sr) override
    {
        sampleRate = sr;
        maxDelay   = (int) (sr * 3.0) + 8;
        for (int ch = 0; ch < 2; ++ch)
        {
            line[ch].assign((size_t) maxDelay, 0.0f);
            srReducer[ch].prepare(sr);
        }
        jitter.prepare(sr);
        reset();
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(line[ch].begin(), line[ch].end(), 0.0f);
            wp[ch] = 0; hfState[ch] = lfState[ch] = outTone[ch] = 0.0f;
            srReducer[ch].reset();
        }
        jitter.reset();
        driftSmooth = 0.0f; driftTarget = 0.0f; driftCountdown = 1000;
        duckEnv = 0.0f; rng = 0x6C8E9CF5u;
    }

    void setParams(const float* p) override
    {
        baseDelay = juce::jlimit(2.0f, (float) (maxDelay - 4), (float) (p[0] * 0.001 * sampleRate));
        // Cap feedback below unity: the loop saturation is now unity-gain limiting (no
        // in-loop drive), so the tail DECAYS at ~fbGain per repeat instead of building
        // into a chaotic wall. Dirt is added on the wet OUTPUT, not recirculated.
        fbGain    = juce::jlimit(0.0f, 0.85f, p[1]);
        tone      = juce::jlimit(0.0f, 1.0f, p[2]);
        character = juce::jlimit(0.0f, 1.0f, p[3]);
        movement  = juce::jlimit(0.0f, 1.0f, p[4]);
        width     = juce::jlimit(0.0f, 1.0f, p[5]);
        duck      = juce::jlimit(0.0f, 1.0f, p[6]);
        mix       = juce::jlimit(0.0f, 1.0f, p[7]);

        // Character → sample damage. SR reduction + bit depth follow the
        // 16→12→8-bit / full→broken arc.
        const float targetSR = juce::jmin((float) sampleRate * 0.95f,
                                          juce::jmap(character, 0.0f, 1.0f, 28000.0f, 6000.0f));
        for (int ch = 0; ch < 2; ++ch)
        {
            srReducer[ch].setTargetSampleRate(targetSR);
            srReducer[ch].setJitter(movement * 0.6f);     // clock instability on the converter
        }
        bitReducer.setBitDepth(juce::jmap(character, 0.0f, 1.0f, 16.0f, 8.0f));

        satDrive = 0.20f + character * 0.65f;
        dustLvl  = character * 0.0015f;

        // Movement → broken-clock jitter + pitch drift (not tape wow).
        jitter.setDepth(movement * 60.0f);
        jitter.setRate(30.0f + movement * 50.0f);
        jitter.setBlend(100.0f);
        driftDepth = movement * (float) (sampleRate * 0.0025);

        // Tone darker than Cold Rack; Character lowers bandwidth (hides alias when dark).
        const float darkness = juce::jlimit(0.0f, 1.0f, (1.0f - tone) * 0.6f + character * 0.40f);
        aHF  = onePole(juce::jmap(darkness, 0.0f, 1.0f, 9000.0f, 1500.0f));
        aLF  = onePole(110.0f);
        aOut = onePole(3000.0f);

        widthOff = width * (float) (sampleRate * 0.010);
        xfeed    = width * 0.40f;
    }

    void process(float inL, float inR, float& outL, float& outR) override
    {
        // random-walk pitch drift — broken clock wander, not a sine wobble
        if (--driftCountdown <= 0)
        {
            driftTarget = nextFloat();
            driftCountdown = (int) (sampleRate * 0.05f * (1.6f - juce::jmin(1.2f, movement)));
        }
        driftSmooth += (driftTarget - driftSmooth) * 0.0009f;
        const float drift = driftSmooth * driftDepth;

        const float pk = juce::jmax(std::abs(inL), std::abs(inR));
        duckEnv = juce::jmax(pk, duckEnv * 0.99965f);
        const float duckGain = juce::jlimit(0.0f, 1.0f, 1.0f - duck * duckEnv * 2.6f);

        const float tapL = readInterp(0, (float) wp[0] - (baseDelay + drift + widthOff * 0.5f));
        const float tapR = readInterp(1, (float) wp[1] - (baseDelay + drift - widthOff * 0.5f));

        const float fbL = feedback(0, tapL);
        const float fbR = feedback(1, tapR);

        // SR + bit reduction at the WRITE point → degradation accumulates per pass
        const float wL = bitReducer.process(srReducer[0].process(inL + (fbL * (1.0f - xfeed) + fbR * xfeed) * fbGain + nextFloat() * dustLvl));
        const float wR = bitReducer.process(srReducer[1].process(inR + (fbR * (1.0f - xfeed) + fbL * xfeed) * fbGain + nextFloat() * dustLvl));
        line[0][(size_t) wp[0]] = wL;
        line[1][(size_t) wp[1]] = wR;
        wp[0] = (wp[0] + 1) % maxDelay;
        wp[1] = (wp[1] + 1) % maxDelay;

        float oL = toneTilt(0, tapL);
        float oR = toneTilt(1, tapR);
        oL = std::tanh(oL * (1.0f + satDrive * 2.0f));   // crunchy converter character on the WET output (out of loop)
        oR = std::tanh(oR * (1.0f + satDrive * 2.0f));
        const float midS  = (oL + oR) * 0.5f;
        const float sideS = (oL - oR) * 0.5f * (width * 2.0f);
        oL = std::tanh(midS + sideS);
        oR = std::tanh(midS - sideS);

        outL = inL * (1.0f - mix) + oL * mix * duckGain;
        outR = inR * (1.0f - mix) + oR * mix * duckGain;
    }

private:
    // Feedback conditioning: clock jitter → high/low-cut → UNITY-gain soft limit.
    // No drive multiply here — keeping small-signal gain ≤ 1 so the loop decays.
    float feedback(int ch, float tap)
    {
        float s = jitter.processSample(ch, tap);                 // unstable digital clock
        hfState[ch] += aHF * (s - hfState[ch]);                  // control aliasing/harshness
        const float hc = hfState[ch];
        lfState[ch] += aLF * (hc - lfState[ch]);                 // low-cut / DC
        return std::tanh(hc - lfState[ch]);                      // unity small-signal, limits large
    }

    float toneTilt(int ch, float wet)
    {
        outTone[ch] += aOut * (wet - outTone[ch]);
        const float high = wet - outTone[ch];
        return outTone[ch] + high * (tone * 1.3f);
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
    float nextFloat() { rng = rng * 1664525u + 1013904223u; return (float) (int32_t) rng * (1.0f / 2147483648.0f); }

    double sampleRate = 44100.0;
    int    maxDelay = 8;
    float  baseDelay = 22050.0f, fbGain = 0.30f, tone = 0.45f, character = 0.35f,
           movement = 0.20f, width = 0.45f, duck = 0.20f, mix = 0.25f;
    float  satDrive = 0.4f, dustLvl = 0.0f, aHF = 0.1f, aLF = 0.01f, aOut = 0.3f;
    float  driftDepth = 0.0f, widthOff = 0.0f, xfeed = 0.0f;
    float  driftSmooth = 0.0f, driftTarget = 0.0f, duckEnv = 0.0f;
    int    driftCountdown = 1000;
    uint32_t rng = 0x6C8E9CF5u;

    std::vector<float> line[2];
    int    wp[2] = { 0, 0 };
    float  hfState[2] = { 0, 0 }, lfState[2] = { 0, 0 }, outTone[2] = { 0, 0 };

    SampleRateReducer srReducer[2];
    BitReducer        bitReducer;
    JitterEngine      jitter;
};
