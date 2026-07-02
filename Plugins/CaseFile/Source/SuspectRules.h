#pragma once
#include "AnalysisEngine.h"

//==============================================================================
//  Rule-based suspect generation — V1, deliberately not AI.
//
//  Compares the current mix's analysis against reference analyses and emits
//  detective-style suspect cards. Pure functions over AnalysisResult structs
//  so the logic test can drive them without any files or GUI.
//
//  Thresholds: a band delta of 2.5 dB (relative-energy terms) is audible and
//  worth a card; 4 dB+ is flagged high severity.
//==============================================================================

namespace casefile
{

struct SuspectDef
{
    juce::String title, range, sources, why, actions;
    int severity = 1;   // 0 low, 1 medium, 2 high
};

// role-specific references; any null pointer falls back to `main`
struct RefSet
{
    const AnalysisResult* main   = nullptr;
    const AnalysisResult* lowEnd = nullptr;
    const AnalysisResult* vocal  = nullptr;
    const AnalysisResult* width  = nullptr;
    const AnalysisResult* loud   = nullptr;

    const AnalysisResult* pick (const AnalysisResult* preferred) const
    { return preferred != nullptr ? preferred : main; }
};

inline int severityForDelta (float absDelta)
{
    return absDelta >= 4.0f ? 2 : absDelta >= 2.5f ? 1 : 0;
}

inline juce::Array<SuspectDef> runSuspectRules (const AnalysisResult& cur, const RefSet& refs)
{
    juce::Array<SuspectDef> out;
    if (! cur.valid || refs.main == nullptr || ! refs.main->valid)
        return out;

    auto deltaStr = [] (float d)
    { return (d >= 0.0f ? "+" : "") + juce::String (d, 1) + " dB vs reference"; };

    // --- low-mid buildup -----------------------------------------------------
    if (const auto* ref = refs.pick (refs.lowEnd); ref->valid)
    {
        const float d = cur.bandRel[2] - ref->bandRel[2];
        if (d >= 2.5f)
            out.add ({ "Low-Mid Buildup", "180-350 Hz (" + deltaStr (d) + ")",
                       "Bass, piano, guitars, vocal body, drum room",
                       "This range may be making the mix feel cloudy compared to the reference.",
                       "1. Check bass and piano together.\n"
                       "2. Try subtractive EQ on supporting instruments before cutting the mix bus.\n"
                       "3. Compare vocal level after low-mid cleanup.\n"
                       "4. Recheck at low monitoring volume.",
                       severityForDelta (d) });
    }

    // --- weak sub / dominant bass ---------------------------------------------
    if (const auto* ref = refs.pick (refs.lowEnd); ref->valid)
    {
        const float dSub = cur.bandRel[0] - ref->bandRel[0];
        if (dSub <= -3.0f)
            out.add ({ "Weak Sub", "20-60 Hz (" + deltaStr (dSub) + ")",
                       "Kick, 808/sub bass, high-pass filters set too high",
                       "The reference carries noticeably more sub weight. The record may feel small on big systems.",
                       "1. Check the kick/bass relationship and sub level.\n"
                       "2. Look for over-aggressive high-pass filters on kick and bass.\n"
                       "3. Verify on a system or headphones that reproduce sub.",
                       severityForDelta (dSub) });

        const float dBass = cur.bandRel[1] - ref->bandRel[1];
        if (dBass >= 3.0f)
            out.add ({ "Bass Too Dominant", "60-120 Hz (" + deltaStr (dBass) + ")",
                       "Bass, kick fundamentals, low synths",
                       "This range is running hot against the reference and can mask the rest of the mix.",
                       "1. Check bass level and kick/bass balance.\n"
                       "2. Try gentle cuts on the bass instrument before touching the mix bus.\n"
                       "3. Recheck the vocal after taming the low end.",
                       severityForDelta (dBass) });
    }

    // --- presence / vocal forwardness ------------------------------------------
    if (const auto* ref = refs.pick (refs.vocal); ref->valid)
    {
        const float d = cur.bandRel[4] - ref->bandRel[4];
        if (d <= -2.5f)
            out.add ({ "Presence / Vocal Forwardness", "1.5-5 kHz (" + deltaStr (d) + ")",
                       "Vocal level, masking from guitars, keys, or synths",
                       "The reference has more presence energy — the vocal may be sitting behind the track.",
                       "1. Check vocal intelligibility and masking from guitars, keys, or synths.\n"
                       "2. Compare vocal level against the reference at low monitor volume.\n"
                       "3. Consider carving competing instruments before boosting the vocal.",
                       severityForDelta (d) });
    }

    // --- dark top end / missing air --------------------------------------------
    {
        const auto* ref = refs.main;
        const float d = cur.bandRel[6] - ref->bandRel[6];
        if (d <= -2.5f)
            out.add ({ "Dark Top End / Missing Air", "8-16 kHz (" + deltaStr (d) + ")",
                       "Dull cymbals, dark vocal chain, missing air band EQ",
                       "The mix reads darker than the target — it can feel less expensive and less open.",
                       "1. Add air carefully after checking sibilance and cymbal harshness.\n"
                       "2. Check HF loss from heavy de-essing or dark saturation.\n"
                       "3. A/B against the reference at matched loudness.",
                       severityForDelta (d) });
    }

    // --- harshness risk ---------------------------------------------------------
    {
        const auto* ref = refs.main;
        const float d = cur.bandRel[5] - ref->bandRel[5];
        if (d >= 2.5f)
            out.add ({ "Harshness Risk", "5-8 kHz (" + deltaStr (d) + ")",
                       "Vocal sibilance, cymbals, guitars, aggressive saturation",
                       "This range is hotter than the reference — long listens may fatigue.",
                       "1. Check vocal sibilance, cymbals, guitars, and aggressive saturation.\n"
                       "2. Try dynamic EQ or a de-esser on the offenders before darkening the mix bus.\n"
                       "3. Recheck after any air-band boosts.",
                       severityForDelta (d) });
    }

    // --- over-compression / reduced punch ----------------------------------------
    if (const auto* ref = refs.pick (refs.loud); ref->valid)
    {
        const float crestLoss = ref->crestDb - cur.crestDb;
        if (cur.rmsDb > ref->rmsDb + 1.0f && crestLoss >= 2.0f)
            out.add ({ "Over-Compression / Reduced Punch",
                       "Crest factor " + juce::String (cur.crestDb, 1) + " dB vs reference "
                           + juce::String (ref->crestDb, 1) + " dB",
                       "Mix bus compression, limiter drive, stacked track compression",
                       "The mix is louder than the reference but has less transient headroom — punch is being traded for level.",
                       "1. Back off bus limiting/compression or restore transient impact.\n"
                       "2. Compare at matched loudness, not matched faders.\n"
                       "3. Check whether drums still snap at the reference's crest factor.",
                       crestLoss >= 4.0f ? 2 : 1 });
    }

    // --- low-end width instability ------------------------------------------------
    if (const auto* ref = refs.pick (refs.width); ref->valid)
    {
        if (cur.lowWidthPct > 25.0f && cur.lowWidthPct > ref->lowWidthPct + 10.0f)
            out.add ({ "Low-End Width Instability",
                       "Below 120 Hz (" + juce::String (cur.lowWidthPct, 0) + "% side energy vs reference "
                           + juce::String (ref->lowWidthPct, 0) + "%)",
                       "Stereo bass patches, wideners on the low end, out-of-phase kick layers",
                       "Too much stereo information below 120 Hz — the low end can fall apart in mono and on club systems.",
                       "1. Check mono compatibility and consider narrowing sub/bass information.\n"
                       "2. Look for stereo wideners or chorus on bass sources.\n"
                       "3. Verify kick layers are phase-aligned.",
                       cur.lowWidthPct > 40.0f ? 2 : 1 });
    }

    // --- mono compatibility (no reference needed) -----------------------------------
    if (cur.corr < 0.35f)
        out.add ({ "Mono Compatibility Risk",
                   "L/R correlation " + juce::String (cur.corr, 2),
                   "Wide stereo FX, out-of-phase layers, heavy wideners",
                   "Overall correlation is low — parts of the mix may cancel when summed to mono.",
                   "1. Mono check the full mix on one speaker.\n"
                   "2. Hunt for out-of-phase or over-widened elements.\n"
                   "3. Recheck the low end and lead vocal in mono first.",
                   cur.corr < 0.1f ? 2 : 1 });

    return out;
}

} // namespace casefile
