#pragma once
#include <JuceHeader.h>
#include <complex>
#include <vector>

// =============================================================================
//  Phase-vocoder time-stretch — change duration while PRESERVING pitch.
//
//  Stretches in[0..inLen] to out[0..outLen]. Used by the reverse-swell engine
//  so a swell can fill any musical length (1/8 note … 8 bars) without the pitch
//  drop a plain resample would cause.
//
//  Synthesis hop is fixed (Hs = N/4 → 75% overlap) and the analysis hop is
//  derived (Ha = Hs / ratio), so arbitrarily large stretch ratios still overlap
//  cleanly (no gaps). Offline / message-thread only.
// =============================================================================
inline void btPhaseVocoderStretch (const float* in, int inLen, float* out, int outLen)
{
    if (in == nullptr || out == nullptr || outLen <= 0) return;
    for (int i = 0; i < outLen; ++i) out[i] = 0.0f;
    if (inLen <= 0) return;

    constexpr int order = 11;            // FFT size 2048
    const int N  = 1 << order;
    const int Hs = N / 4;                // synthesis hop (fixed → 75% overlap)
    const double ratio = (double) outLen / (double) juce::jmax(1, inLen);   // stretch factor
    const int Ha = juce::jmax(1, (int) std::lround((double) Hs / ratio));   // analysis hop

    static const double twoPi = juce::MathConstants<double>::twoPi;

    // Hann window (used on both analysis and synthesis).
    std::vector<float> win((size_t) N);
    for (int i = 0; i < N; ++i)
        win[(size_t) i] = (float) (0.5 - 0.5 * std::cos(twoPi * i / (N - 1)));

    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> frame((size_t) N), spec((size_t) N), tdom((size_t) N);
    std::vector<double> lastPhase((size_t) (N / 2 + 1), 0.0);
    std::vector<double> sumPhase ((size_t) (N / 2 + 1), 0.0);

    std::vector<double> acc   ((size_t) (outLen + N), 0.0);
    std::vector<double> accWin((size_t) (outLen + N), 0.0);

    const int numFrames = inLen / Ha + 2;
    for (int f = 0; f < numFrames; ++f)
    {
        const int inPos = f * Ha;
        for (int i = 0; i < N; ++i)
        {
            const int idx = inPos + i;
            const float s = (idx >= 0 && idx < inLen) ? in[idx] : 0.0f;
            frame[(size_t) i] = std::complex<float> (s * win[(size_t) i], 0.0f);
        }

        fft.perform (frame.data(), spec.data(), false);

        for (int k = 0; k <= N / 2; ++k)
        {
            const double mag   = std::abs (spec[(size_t) k]);
            const double phase = std::arg (spec[(size_t) k]);
            const double expected = (double) Ha * twoPi * k / N;
            double delta = phase - lastPhase[(size_t) k] - expected;
            delta -= twoPi * std::round (delta / twoPi);                 // princarg
            lastPhase[(size_t) k] = phase;
            const double trueFreq = twoPi * k / N + delta / Ha;
            sumPhase[(size_t) k] += (double) Hs * trueFreq;
            spec[(size_t) k] = std::polar ((float) mag, (float) sumPhase[(size_t) k]);
        }
        for (int k = N / 2 + 1; k < N; ++k)                              // conjugate-symmetric tail
            spec[(size_t) k] = std::conj (spec[(size_t) (N - k)]);

        fft.perform (spec.data(), tdom.data(), true);                    // inverse (JUCE scales by 1/N)

        const int outPos = f * Hs;
        for (int i = 0; i < N; ++i)
        {
            const int idx = outPos + i;
            if (idx >= 0 && idx < (int) acc.size())
            {
                acc[(size_t) idx]    += (double) tdom[(size_t) i].real() * win[(size_t) i];
                accWin[(size_t) idx] += (double) win[(size_t) i] * win[(size_t) i];
            }
        }
    }

    for (int i = 0; i < outLen; ++i)
    {
        const double w = accWin[(size_t) i];
        out[i] = (w > 1.0e-6) ? (float) (acc[(size_t) i] / w) : 0.0f;
    }
}
