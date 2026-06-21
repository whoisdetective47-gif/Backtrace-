#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// Zero-order hold sample-rate reducer. Fractional phase accumulator ensures
// the hold frequency is accurate regardless of host sample rate.
// ZOH produces characteristic aliased artifacts — intentionally not a clean SRC.
class SampleRateReducer
{
public:
    void prepare(double hostSampleRate)
    {
        hostRate = hostSampleRate;
        reset();
    }

    void setTargetSampleRate(float targetHz)
    {
        phaseInc = static_cast<float>(hostRate) / std::max(targetHz, 10.0f);
    }

    void setJitter(float amount) // 0–1
    {
        jitter = std::clamp(amount, 0.0f, 1.0f);
    }

    float process(float sample)
    {
        phase += 1.0f;

        float wobble = 0.0f;
        if (jitter > 0.0f)
        {
            rng   = rng * 1664525u + 1013904223u;
            float noise = static_cast<float>(static_cast<int32_t>(rng)) * (1.0f / 2147483648.0f);
            wobble = noise * jitter * 0.4f;
        }

        if (phase >= phaseInc + wobble)
        {
            phase      -= phaseInc + wobble;
            heldSample  = sample;
        }

        return heldSample;
    }

    void reset()
    {
        phase = heldSample = 0.0f;
        rng   = 0xABCDEF01u;
    }

private:
    double   hostRate   = 44100.0;
    float    phaseInc   = 1.0f;
    float    phase      = 0.0f;
    float    heldSample = 0.0f;
    float    jitter     = 0.0f;
    uint32_t rng        = 0xABCDEF01u;
};
