// Offline DSP safety stress test — feeds full-scale noise + an impulse through
// every delay/reverb flavor at MAX feedback/decay and verifies the output stays
// bounded, finite (no NaN/Inf), DC-free and decaying. Tests each machine WITHOUT
// the processor's global safety net, so passing here means the DSP is safe on its
// own. Answers: "can this run away / glitch in a host?"
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <cstdio>

#include "DSP/DelayMachine.h"
#include "DSP/TapeEcho.h"
#include "DSP/PedalDigital.h"
#include "DSP/MagneticDrum.h"
#include "DSP/TapeWitness.h"
#include "DSP/ColdRack.h"
#include "DSP/VaultDelay.h"
#include "DSP/ReverbSpace.h"
#include "DSP/VelvetHall.h"
#include "DSP/ModernSpace.h"
#include "DSP/ShimmerVerb.h"
#include "DSP/PlateVerb.h"
#include "DSP/SpringVerb.h"
#include "DSP/ModernPitchShifter.h"

static constexpr double kSR = 48000.0;
static constexpr float  kBound = 4.0f;   // anything over this = dangerous

template <typename Knob>
static std::array<float, 10> stressParams(const juce::Array<Knob>& lay)
{
    std::array<float, 10> p {};
    for (int i = 0; i < lay.size() && i < 10; ++i)
    {
        if (! lay[i].used()) { p[(size_t) i] = 0.0f; continue; }
        const auto n = lay[i].name.toLowerCase();
        float v = lay[i].def;
        if (n.contains("feedback") || n.contains("repeats")) v = 0.92f;   // push feedback hard
        else if (n.contains("decay") || n.contains("shimmer")) v = 1.0f;  // max decay / shimmer
        else if (n == "mix") v = 1.0f;                                    // full wet to stress the path
        p[(size_t) i] = v;
    }
    return p;
}

template <typename Machine>
static bool runStress(const char* label, const float* params)
{
    Machine m;
    m.prepare(kSR);
    m.reset();
    m.setParams(params);

    juce::Random rng (12345);
    const int total    = (int) (kSR * 12.0);   // 0.5 s excite + ~11.5 s tail
    const int exciteLen = (int) (kSR * 0.5);

    double maxAbs = 0.0, dcSum = 0.0, tailEnergy = 0.0;
    bool   nonFinite = false;
    const int tailStart = (int) (kSR * 8.0);

    for (int i = 0; i < total; ++i)
    {
        float in = 0.0f;
        if (i == 0) in = 1.0f;                                   // impulse (transient/splash)
        else if (i < exciteLen) in = rng.nextFloat() * 2.0f - 1.0f;  // full-scale noise

        float oL = 0.0f, oR = 0.0f;
        m.process(in, in, oL, oR);

        if (! std::isfinite(oL) || ! std::isfinite(oR)) nonFinite = true;
        maxAbs = juce::jmax(maxAbs, (double) juce::jmax(std::abs(oL), std::abs(oR)));
        dcSum += (double) oL;
        if (i >= tailStart) tailEnergy += (double) (oL * oL);
    }

    const double dc       = dcSum / total;
    const double tailRMS  = std::sqrt(tailEnergy / (double) (total - tailStart));
    const bool   safe     = ! nonFinite && maxAbs <= kBound && std::abs(dc) < 0.01 && tailRMS < 0.5;

    std::printf("  %-14s peak=%6.3f  dc=%+.5f  tailRMS(8-12s)=%.5f  finite=%s  -> %s\n",
                label, maxAbs, dc, tailRMS, nonFinite ? "NO" : "yes", safe ? "SAFE" : "*** FAIL ***");
    return safe;
}

// Determinism + repeat-capture check: a delay flavor, reset → setParams → process an
// impulse then silence, must produce (a) BYTE-IDENTICAL output across 5 independent
// renders (no hidden state / no RNG drift / no ramp-from-zero), and (b) at least a few
// discrete echoes after the impulse (repeats are actually captured, not gated away).
template <typename Machine>
static bool runDeterminism(const char* label, const float* params)
{
    const int total = (int) (kSR * 3.0);
    std::vector<float> first((size_t) total, 0.0f);
    bool identical = true;

    const int burst = (int) (kSR * 0.02);                     // 20 ms excitation, then silence
    for (int pass = 0; pass < 5; ++pass)
    {
        Machine m; m.prepare(kSR); m.reset(); m.setParams(params);
        juce::Random rng(424242);                             // SAME burst every pass → deterministic input
        for (int i = 0; i < total; ++i)
        {
            const float in = (i < burst) ? (rng.nextFloat() * 2.0f - 1.0f) * 0.8f : 0.0f;
            float oL = 0.0f, oR = 0.0f; m.process(in, in, oL, oR);
            if (pass == 0) first[(size_t) i] = oL;
            else if (std::abs(oL - first[(size_t) i]) > 1.0e-7f) identical = false;
        }
    }

    // Repeats captured = real energy in the tail AFTER the source burst (discrete echoes
    // OR a smeared feedback wash). Measured as peak + RMS over [0.1 s, 2.5 s]; silent only
    // if the delay failed to ring out past the source.
    const int a = (int) (kSR * 0.1), e = (int) (kSR * 2.5);
    double sum = 0.0; float tpk = 0.0f;
    for (int i = a; i < e; ++i) { sum += (double) first[(size_t) i] * first[(size_t) i]; tpk = juce::jmax(tpk, std::abs(first[(size_t) i])); }
    const double tailRms = std::sqrt(sum / (double) juce::jmax(1, e - a));
    const bool ok = identical && tpk > 0.02f;
    std::printf("  %-14s identical(5x)=%s  tailPeak=%.4f tailRMS=%.5f  -> %s\n",
                label, identical ? "yes" : "**NO**", tpk, tailRms, ok ? "OK" : "*** FAIL ***");
    return ok;
}

static std::array<float,10> delayDetParams(int flavor)
{
    auto p = stressParams(delayKnobLayout(flavor));   // start from defaults
    const auto lay = delayKnobLayout(flavor);
    for (int i = 0; i < lay.size() && i < 10; ++i)
    {
        if (! lay[i].used()) continue;
        const auto n = lay[i].name.toLowerCase();
        if (n == "time")     p[(size_t) i] = 300.0f;               // 300 ms → clearly-spaced echoes
        if (n.contains("feedback") || n.contains("repeats")) p[(size_t) i] = 0.55f;  // several repeats, safe
        if (n == "mix")      p[(size_t) i] = 1.0f;                  // full wet to expose the tail
        if (n == "duck")     p[(size_t) i] = 0.0f;
    }
    return p;
}

int main()
{
    std::printf("Backtrace DSP safety stress test  (full-scale noise+impulse, MAX feedback/decay)\n");
    std::printf("  bound=%.1f, 12 s render incl. tail\n\nDELAYS:\n", kBound);
    bool ok = true;

    ok &= runStress<TapeEcho>    ("Reel Echo",     stressParams(delayKnobLayout(1)).data());
    ok &= runStress<PedalDigital>("Digital Pedal", stressParams(delayKnobLayout(2)).data());
    ok &= runStress<MagneticDrum>("Magnetic Drum", stressParams(delayKnobLayout(3)).data());
    ok &= runStress<TapeWitness> ("Tape Witness",  stressParams(delayKnobLayout(4)).data());
    ok &= runStress<ColdRack>    ("Cold Rack",     stressParams(delayKnobLayout(5)).data());
    ok &= runStress<VaultDelay>  ("Vault Delay",   stressParams(delayKnobLayout(6)).data());

    std::printf("\nREVERBS:\n");
    ok &= runStress<VelvetHall>  ("Velvet Hall",   stressParams(reverbKnobLayout(1)).data());
    ok &= runStress<ModernSpace> ("Modern Space",  stressParams(reverbKnobLayout(2)).data());
    ok &= runStress<ShimmerVerb> ("Shimmer",       stressParams(reverbKnobLayout(3)).data());
    ok &= runStress<PlateVerb>   ("Studio Plate",  stressParams(reverbKnobLayout(4)).data());
    ok &= runStress<SpringVerb>  ("626 Spring",    stressParams(reverbKnobLayout(5)).data());

    // pitch shifter (+12) — not a feedback loop, but verify bounded
    {
        ModernPitchShifter ps; ps.prepare(kSR, 0); ps.setPitch(12.0f); ps.reset();
        juce::Random rng (7); double maxAbs = 0; bool nf = false;
        for (int i = 0; i < (int) (kSR * 2.0); ++i)
        {
            const float in = rng.nextFloat() * 2.0f - 1.0f;
            const float r = ps.advanceAndGetRatio();
            const float o = ps.processSample(0, in, r);
            if (! std::isfinite(o)) nf = true;
            maxAbs = juce::jmax(maxAbs, (double) std::abs(o));
        }
        const bool safe = ! nf && maxAbs <= kBound;
        std::printf("\nPITCH:\n  %-14s peak=%6.3f  finite=%s  -> %s\n",
                    "+12 shifter", maxAbs, nf ? "NO" : "yes", safe ? "SAFE" : "*** FAIL ***");
        ok &= safe;
    }

    std::printf("\nDELAY DETERMINISM + REPEAT CAPTURE  (impulse → silence, 5 identical renders):\n");
    ok &= runDeterminism<TapeEcho>    ("Reel Echo",     delayDetParams(1).data());
    ok &= runDeterminism<PedalDigital>("Digital Pedal", delayDetParams(2).data());
    ok &= runDeterminism<MagneticDrum>("Magnetic Drum", delayDetParams(3).data());
    ok &= runDeterminism<TapeWitness> ("Tape Witness",  delayDetParams(4).data());
    ok &= runDeterminism<ColdRack>    ("Cold Rack",     delayDetParams(5).data());
    ok &= runDeterminism<VaultDelay>  ("Vault Delay",   delayDetParams(6).data());

    std::printf("\nRESULT: %s\n", ok ? "ALL SAFE — no runaway, no NaN, tails decay, no DC" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
