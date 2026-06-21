#pragma once
#include <cmath>
#include <algorithm>

// Downward noise gate with optional noise-sidechain mode.
//
// Standard mode: key signal = dry (pre-processing) input.
//   Gate opens when the input signal exceeds the threshold.
//
// Noise SC mode: key signal = noise burst amplitude generated per-sample.
//   The LCG noise floor randomly triggers the gate open, creating
//   rhythmic stutter and chop tied to the NOISE knob density.
//   Higher NOISE amount → more frequent / longer gate openings.
//
// Attack is fixed at 2ms (snappy). Release is user-controlled.
// Gain is smoothed to avoid hard clicks on gate transitions.
class NoiseGate
{
public:
    void prepare(double sr)
    {
        sampleRate   = sr;
        envLevel     = 0.0f;
        gainSmoothed = 0.0f;
        // Fast attack: ~2 ms
        attackCoeff = computeCoeff(0.002f);
        setRelease(80.0f);
        setThreshold(-60.0f);
    }

    void setThreshold(float threshDB)
    {
        // -60 dB effectively bypasses the gate (threshold ≈ 0.001 linear)
        threshLin = std::pow(10.0f, threshDB / 20.0f);
    }

    void setRelease(float releaseMs)
    {
        releaseCoeff    = computeCoeff(releaseMs * 0.001f);
        gainCloseCoeff  = computeCoeff(releaseMs * 0.001f * 0.6f);
    }

    // input     : audio sample to gate
    // keySignal : the signal whose level controls gate open/close
    //             (pass dry sample for standard, noise sample for SC mode)
    inline float process(float input, float keySignal) noexcept
    {
        // Envelope follower on key signal
        float keyLevel = std::abs(keySignal);
        if (keyLevel > envLevel)
            envLevel = attackCoeff  * envLevel + (1.0f - attackCoeff)  * keyLevel;
        else
            envLevel = releaseCoeff * envLevel;

        // Hard gate decision → smoothed gain to avoid clicks
        float targetGain = (envLevel >= threshLin) ? 1.0f : 0.0f;
        float speed = (targetGain > gainSmoothed)
                    ? (1.0f - attackCoeff)    // fast open
                    : (1.0f - gainCloseCoeff); // slow close (user release)
        gainSmoothed += (targetGain - gainSmoothed) * speed;

        return input * gainSmoothed;
    }

    void reset() { envLevel = 0.0f; gainSmoothed = 0.0f; }

private:
    double sampleRate     = 44100.0;
    float  threshLin      = 0.0f;
    float  envLevel       = 0.0f;
    float  gainSmoothed   = 0.0f;
    float  attackCoeff    = 0.0f;
    float  releaseCoeff   = 0.0f;
    float  gainCloseCoeff = 0.0f;

    float computeCoeff(float timeSeconds) const
    {
        return std::exp(-1.0f / (timeSeconds * (float)sampleRate));
    }
};
