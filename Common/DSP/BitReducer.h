#pragma once
#include <cmath>
#include <algorithm>

// µ-law companded quantization — the authentic vintage-sampler approach.
//
// Vintage 12-bit samplers did NOT store linear PCM. They companded: compress
// the signal before quantizing, expand after. This packs more quantization
// resolution near zero (where the ear is most sensitive) and coarser steps up
// high, so the quantization noise scales with the signal like analog tape hiss
// rather than the harsh, evenly-spaced steps of linear quantization.
//
// The difference is subtle at 12-bit and dramatic at low bit depths: linear
// 4-bit sounds like gravelly digital garbage; companded 4-bit sounds like warm,
// crushed lo-fi. No dithering — the companding curve IS the character.
class BitReducer
{
public:
    void setBitDepth(float bits)
    {
        int b      = static_cast<int>(std::round(std::clamp(bits, 2.0f, 16.0f)));
        halfLevels = static_cast<float>(1 << (b - 1));
        invHalf    = 1.0f / halfLevels;
    }

    float process(float s) const
    {
        // Full-scale clip (the converter's ±1 ceiling)
        const float x  = std::clamp(s, -1.0f, 1.0f);
        const float sx = (x < 0.0f) ? -1.0f : 1.0f;

        // Compress (µ-law) → quantize → expand
        const float comp = sx * std::log1p(kMu * std::fabs(x)) * kInvLnMu;   // [-1, 1]
        const float q    = std::round(comp * halfLevels) * invHalf;         // quantize
        const float sq   = (q < 0.0f) ? -1.0f : 1.0f;
        return sq * std::expm1(std::fabs(q) * kLnMu) / kMu;                  // expand
    }

private:
    static constexpr float kMu     = 255.0f;          // companding amount
    static constexpr float kLnMu   = 5.5451774f;      // ln(1 + µ)
    static constexpr float kInvLnMu = 1.0f / 5.5451774f;

    float halfLevels = 2048.0f;   // 2^11 — default 12-bit
    float invHalf    = 1.0f / 2048.0f;
};
