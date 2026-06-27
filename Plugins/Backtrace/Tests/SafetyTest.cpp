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

    std::printf("\nRESULT: %s\n", ok ? "ALL SAFE — no runaway, no NaN, tails decay, no DC" : "FAILURES ABOVE");
    return ok ? 0 : 1;
}
