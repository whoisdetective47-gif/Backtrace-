#pragma once

// First-order IIR DC blocker. Prevents DC buildup from accumulating
// through the saturation and quantizer chain.
class DCBlocker
{
public:
    float process(float x)
    {
        float y = x - xp + 0.9975f * yp;
        xp = x; yp = y;
        return y;
    }
    void reset() { xp = yp = 0.0f; }

private:
    float xp = 0.0f, yp = 0.0f;
};
