#pragma once
#include <cmath>

// tanh soft clip with optional asymmetric 2nd-harmonic crunch.
// drive: linear multiplier applied before clipping.
// crunch: 0–1 asymmetry amount — positive half squashed more than negative,
// adding even harmonics (warm, vintage converter edge).
class Saturation
{
public:
    float process(float s, float drive, float crunch) const
    {
        s = std::tanh(s * drive);

        if (crunch > 0.001f)
        {
            float a = crunch * 0.45f;
            if (s > 0.0f)
                s = std::tanh(s * (1.0f + a)) / std::tanh(1.0f + a);
            else
                s = std::tanh(s * (1.0f + a * 0.35f)) / std::tanh(1.0f + a * 0.35f);
        }

        return s;
    }
};
