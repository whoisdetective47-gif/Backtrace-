#pragma once
#include <JuceHeader.h>
#include "DSP/DelayMachine.h"
#include "DSP/ReverbSpace.h"

// =============================================================================
//  Backtrace factory preset bank (Phase 11A) — 40 musical starting points.
//
//  Each preset is a state var matching BacktraceProcessor::getStateVar(): stable
//  string flavor/routing IDs + named params (defaults from the layout, with a few
//  targeted overrides per preset). Settings only — never captured audio.
// =============================================================================
namespace btpreset
{
    using Ov = std::initializer_list<std::pair<const char*, float>>;

    inline juce::var buildDelay(int fl, Ov ov)
    {
        auto* d = new juce::DynamicObject();
        d->setProperty("flavor", delayFlavorName(fl));
        auto* p = new juce::DynamicObject();
        const auto lay = delayKnobLayout(fl);
        for (int i = 0; i < lay.size(); ++i)
            if (lay[i].used()) p->setProperty(lay[i].name.toLowerCase(), lay[i].def);
        for (auto& o : ov) p->setProperty(juce::String(o.first), o.second);
        d->setProperty("params", juce::var(p));
        return juce::var(d);
    }

    inline juce::var buildReverb(int fl, Ov ov)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty("flavor", reverbFlavorName(fl));
        auto* p = new juce::DynamicObject();
        const auto lay = reverbKnobLayout(fl);
        for (int i = 0; i < lay.size(); ++i)
            if (lay[i].used()) p->setProperty(lay[i].name.toLowerCase(), lay[i].def);
        for (auto& o : ov) p->setProperty(juce::String(o.first), o.second);
        r->setProperty("params", juce::var(p));
        return juce::var(r);
    }

    inline juce::var buildState(int dFl, Ov dOv, int rFl, Ov rOv,
                                const char* routing, float pitch, bool land)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("schema", 1);
        o->setProperty("plugin", "1.0.0");
        o->setProperty("output", 0.0);
        o->setProperty("pitch", pitch);
        o->setProperty("land", land);
        o->setProperty("routing", routing);
        o->setProperty("liveMode", false);   // offline presets are Capture mode — so switching off a
                                             // Live preset reliably leaves Live mode (buildLive re-sets true)
        o->setProperty("delay",  buildDelay(dFl, dOv));
        o->setProperty("reverb", buildReverb(rFl, rOv));
        return juce::var(o);
    }

    // LIVE PREVERB preset: a normal state (reverb flavor + neutral routing) with the live-preverb
    // keys added, so loading it enables real-time Mode 2 with a tuned Time/Feel/Shape/Mix. Additive
    // only — never touches the live DSP path. timeIdx 0..7 = 1/32,1/16,1/8,1/4,1/2,1bar,2bar,4bar;
    // feel 0/1/2 = Straight/Dotted/Triplet; shape 0.5 = default (lower gentler, higher a late bloom).
    inline juce::var buildLive(int rFl, Ov rOv, int timeIdx, int feel, float shape,
                               float mix, float pitch, float wet = 1.0f)
    {
        juce::var st = buildState(0, {}, rFl, rOv, "reverb_swell", pitch, false);
        if (auto* o = st.getDynamicObject())
        {
            o->setProperty("liveMode",  true);
            o->setProperty("liveTime",  timeIdx);
            o->setProperty("liveFeel",  feel);
            o->setProperty("liveShape", (double) shape);
            o->setProperty("liveMix",   (double) mix);
            o->setProperty("liveWet",   (double) wet);
            o->setProperty("liveDry",   1.0);
        }
        return st;
    }

    // Neutral INIT state — pitch 0, no extreme FX. Used for fresh insert + Reset
    // so Backtrace NEVER starts on a pitched/odd default. Ghost Shimmer at a modest
    // mix is the default reverb: a lush, blooming reverse swell out of the box.
    inline juce::var initState()
    {
        // Reverb Swell = the classic producer move (reverb tail → reverse), so a fresh
        // insert nails the main Backtrace product out of the box. Default reverb = Ghost
        // Shimmer (flavor 3). Fade/Ringout/Motion defaults live in the processor atomics.
        return buildState(0, {}, 1, { {"mix", 0.30f} }, "reverb_swell", 0.0f, false);   // Velvet Hall (warm/smooth)
    }

    struct FactoryDef { juce::String name, category; juce::var state; };

    // Delay ids: 1 reel 2 pedal 3 drum 4 witness 5 rack 6 vault | Reverb ids: 1 velvet 2 modern 3 ghost-shimmer 4 iron-plate 5 rust-spring
    inline std::vector<FactoryDef> all()
    {
        std::vector<FactoryDef> v;
        auto add = [&v](const char* nm, const char* cat, juce::var st) { v.push_back({ nm, cat, st }); };

        // ============================== Utility / Init ==============================
        // MUST stay first — the plugin opens on preset 0 on a fresh insert, so this
        // neutral pitch-0 state is what every new instance starts from.
        add("INIT - Clean Backtrace", "Utility / Init", initState());

        // ============================== Live Preverb ==============================
        // Real-time Mode 2 starting points. Loading one enables LIVE PREVERB with a tuned
        // Time/Feel/Shape/Mix (+ reverb tone). Engage while the transport is STOPPED, then play.
        // buildLive(reverbFl, reverbOv, timeIdx, feel, shape, mix, pitch[, wet]).
        add("Vocal Lift",        "Live Preverb", buildLive(3, {},                 3, 0, 0.50f, 0.40f,  0.0f));
        add("Guitar Swell",      "Live Preverb", buildLive(1, {},                 3, 0, 0.50f, 0.40f,  0.0f));
        add("Snare Halo",        "Live Preverb", buildLive(4, {{"tone",0.66f}},   2, 0, 0.40f, 0.30f,  0.0f));
        add("808 Sub Bloom",     "Live Preverb", buildLive(1, {{"tone",0.34f}},   4, 0, 0.56f, 0.35f,  0.0f));
        add("Big Riser",         "Live Preverb", buildLive(3, {},                 5, 0, 0.82f, 0.50f, 12.0f));
        add("Tight Transition",  "Live Preverb", buildLive(2, {},                 2, 0, 0.50f, 0.35f,  0.0f));
        add("Cathedral Rise",    "Live Preverb", buildLive(1, {{"tone",0.42f}},   6, 0, 0.70f, 0.45f,  0.0f));
        add("Dotted Pulse",      "Live Preverb", buildLive(3, {},                 2, 1, 0.50f, 0.35f,  0.0f));
        add("Micro Shimmer",     "Live Preverb", buildLive(3, {},                 1, 0, 0.42f, 0.30f,  0.0f));
        add("Plate Vocal Air",   "Live Preverb", buildLive(4, {{"tone",0.72f}},   3, 0, 0.50f, 0.35f,  0.0f));

        // ============================== Vocal Throws ==============================
        add("Ghost Vocal Rise", "Vocal Throws",
            buildState(0, {}, 3, {{"shimmer",0.4f},{"mix",0.35f}}, "reverb_swell", 7.0f, false));
        add("Dirty Vocal Throw", "Vocal Throws",
            buildState(4, {{"tone",0.4f}}, 1, {{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Cold Rack Wide Throw", "Vocal Throws",
            buildState(5, {{"width",0.7f},{"mix",0.28f}}, 2, {{"width",0.78f},{"mix",0.24f}}, "parallel", 0.0f, false));
        add("Tape Witness Slapback", "Vocal Throws",
            buildState(4, {{"time",140.0f},{"feedback",0.18f},{"mix",0.35f}}, 1, {{"mix",0.18f}}, "delay_to_reverb", 0.0f, false));
        add("Velvet Hall Vocal Bloom", "Vocal Throws",
            buildState(0, {}, 1, {{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Magnetic Noir Vocal", "Vocal Throws",
            buildState(3, {{"tone",0.4f}}, 1, {{"tone",0.42f},{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Reverse Tail Vocal Pull", "Vocal Throws",
            buildState(0, {}, 4, {{"decay",0.45f},{"mix",0.4f}}, "reverse_tail", 0.0f, false));

        // ============================== Reverse Blooms ==============================
        add("Reel Echo Reverse", "Reverse Blooms",
            buildState(1, {{"feedback",0.3f}}, 1, {{"mix",0.32f}}, "reverse_before_fx", 0.0f, false));
        add("Velvet Hall Reverse Bloom", "Reverse Blooms",
            buildState(0, {}, 1, {{"decay",0.5f},{"mix",0.35f}}, "reverse_before_fx", 0.0f, false));
        add("Reverse Tail Pull", "Reverse Blooms",
            buildState(0, {}, 4, {{"decay",0.45f},{"mix",0.4f}}, "reverse_tail", 0.0f, false));
        add("Full Reverse Evidence", "Reverse Blooms",
            buildState(4, {{"mix",0.3f}}, 2, {{"mix",0.3f}}, "full_reverse_print", 0.0f, false));
        add("Parallel Dream Throw", "Reverse Blooms",
            buildState(2, {{"mix",0.3f}}, 3, {{"shimmer",0.35f},{"mix",0.3f}}, "parallel", 0.0f, false));
        add("Modern Space Reverse Pull", "Reverse Blooms",
            buildState(0, {}, 2, {{"mix",0.32f}}, "reverse_before_fx", 0.0f, false));
        add("Ghost Shimmer Lift", "Reverse Blooms",
            buildState(0, {}, 3, {{"shimmer",0.5f},{"decay",0.6f},{"mix",0.4f}}, "reverse_before_fx", 12.0f, false));

        // ============================== Drum FX ==============================
        add("Snare Suckback", "Drum FX",
            buildState(0, {}, 4, {{"decay",0.35f},{"tone",0.65f},{"mix",0.4f}}, "reverse_tail", 0.0f, false));
        add("Reverse Crash Builder", "Drum FX",
            buildState(0, {}, 1, {{"decay",0.65f},{"mix",0.4f}}, "reverse_before_fx", 0.0f, false));
        add("Dirty Drum Pull", "Drum FX",
            buildState(6, {{"character",0.5f},{"mix",0.25f}}, 2, {{"mix",0.2f}}, "reverse_before_fx", 0.0f, false));
        add("Crushed Ping-Pong", "Drum FX",
            buildState(6, {{"character",0.65f},{"width",0.85f},{"mix",0.35f}}, 0, {}, "delay_to_reverb", 0.0f, false));
        add("Room Hit Reverse", "Drum FX",
            buildState(0, {}, 2, {{"mix",0.35f}}, "reverse_tail", 0.0f, false));
        add("Rust Spring Snare Throw", "Drum FX",
            buildState(0, {}, 5, {{"predly",8.0f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));

        // ============================== Guitar Swells ==============================
        add("Psychedelic Guitar Pull", "Guitar Swells",
            buildState(3, {{"mix",0.25f}}, 3, {{"shimmer",0.4f},{"mix",0.35f}}, "reverse_before_fx", 0.0f, false));
        add("Tape Witness Guitar Swell", "Guitar Swells",
            buildState(4, {{"tone",0.4f}}, 1, {{"mix",0.3f}}, "reverse_before_fx", 0.0f, false));
        add("Magnetic Drum Ghost Lead", "Guitar Swells",
            buildState(3, {{"mix",0.28f}}, 1, {{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Reel Echo Guitar Bloom", "Guitar Swells",
            buildState(1, {{"mix",0.28f}}, 1, {{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Phantom Plate Guitar Wash", "Guitar Swells",
            buildState(0, {}, 4, {{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));

        // ============================== Synth / Cinematic ==============================
        add("Cinematic Backtrace", "Synth / Cinematic",
            buildState(0, {}, 3, {{"shimmer",0.45f},{"decay",0.7f},{"width",0.85f},{"mix",0.4f}}, "reverse_before_fx", 0.0f, false));
        add("Dark Chamber Pull", "Synth / Cinematic",
            buildState(0, {}, 1, {{"tone",0.35f},{"decay",0.4f},{"mix",0.35f}}, "reverse_tail", 0.0f, false));
        add("Deep Space Rise", "Synth / Cinematic",
            buildState(0, {}, 2, {{"decay",0.6f},{"width",0.85f},{"mix",0.35f}}, "reverse_before_fx", 7.0f, false));
        add("Ghost Shimmer Pad", "Synth / Cinematic",
            buildState(0, {}, 3, {{"shimmer",0.4f},{"width",0.85f},{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Modern Space Trailer Pull", "Synth / Cinematic",
            buildState(0, {}, 2, {{"decay",0.7f},{"width",0.9f},{"mix",0.4f}}, "reverse_before_fx", 0.0f, false));
        add("Wide Ghost Hall", "Synth / Cinematic",
            buildState(0, {}, 1, {{"width",0.9f},{"decay",0.6f},{"mix",0.35f}}, "delay_to_reverb", 0.0f, false));

        // ============================== Dirty Vault ==============================
        add("Vault Delay Broken Clock", "Dirty Vault",
            buildState(6, {{"character",0.6f},{"movement",0.6f},{"mix",0.3f}}, 0, {}, "delay_to_reverb", 0.0f, false));
        add("Dust Loop Transition", "Dirty Vault",
            buildState(6, {{"character",0.5f},{"mix",0.3f}}, 2, {{"mix",0.2f}}, "delay_to_reverb", 0.0f, false));
        add("8-Bit Evidence", "Dirty Vault",
            buildState(6, {{"character",0.85f},{"mix",0.35f}}, 0, {}, "delay_to_reverb", 0.0f, false));
        add("Haunted Buffer", "Dirty Vault",
            buildState(6, {{"character",0.6f},{"mix",0.3f}}, 3, {{"shimmer",0.4f},{"mix",0.3f}}, "reverse_before_fx", 0.0f, false));
        add("Dirty Reverse Artifact", "Dirty Vault",
            buildState(6, {{"character",0.7f},{"movement",0.4f},{"mix",0.3f}}, 0, {}, "reverse_before_fx", 0.0f, false));
        add("Broken Converter Throw", "Dirty Vault",
            buildState(6, {{"character",0.75f},{"mix",0.3f}}, 1, {{"mix",0.2f}}, "delay_to_reverb", 0.0f, false));

        // ============================== Clean Studio ==============================
        add("Cold Rack Clean Throw", "Clean Studio",
            buildState(5, {{"mix",0.28f}}, 2, {{"mix",0.22f}}, "delay_to_reverb", 0.0f, false));
        add("Modern Space Vocal Depth", "Clean Studio",
            buildState(0, {}, 2, {{"predly",45.0f},{"mix",0.25f},{"duck",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Velvet Hall Studio Throw", "Clean Studio",
            buildState(1, {{"mix",0.18f}}, 1, {{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Phantom Plate Vocal Sheen", "Clean Studio",
            buildState(0, {}, 4, {{"tone",0.65f},{"duck",0.25f},{"mix",0.24f}}, "delay_to_reverb", 0.0f, false));
        add("Parallel Clean Bloom", "Clean Studio",
            buildState(2, {{"mix",0.28f}}, 2, {{"mix",0.22f}}, "parallel", 0.0f, false));

        // ============================== Experimental ==============================
        add("Feedback Evidence Loop", "Experimental",
            buildState(1, {{"feedback",0.45f}}, 1, {{"mix",0.3f}}, "feedback_verb_to_delay", 0.0f, false));
        add("Full Reverse Mutation", "Experimental",
            buildState(6, {{"character",0.6f},{"mix",0.3f}}, 3, {{"shimmer",0.5f},{"mix",0.4f}}, "full_reverse_print", 12.0f, false));
        add("Redacted Memory", "Experimental",
            buildState(6, {{"character",0.7f},{"mix",0.3f}}, 1, {{"tone",0.4f},{"mix",0.3f}}, "reverse_before_fx", 0.0f, false));
        add("Pitch Drop Witness", "Experimental",
            buildState(4, {{"tone",0.4f},{"mix",0.3f}}, 1, {{"mix",0.3f}}, "reverse_before_fx", -12.0f, false));
        add("Wide Reverse Glitch", "Experimental",
            buildState(6, {{"character",0.65f},{"width",0.8f},{"mix",0.35f}}, 2, {{"width",0.9f},{"mix",0.3f}}, "reverse_before_fx", 0.0f, false));

        return v;
    }

    // (legacy builder kept below — superseded by the bank above)
    inline std::vector<FactoryDef> legacyUnused()
    {
        std::vector<FactoryDef> v;
        auto add = [&v](const char* nm, const char* cat, juce::var st) { v.push_back({ nm, cat, st }); };

        // ---- Vocal Throws ----
        add("Detective Throw", "Vocal Throws",
            buildState(1, {{"feedback",0.32f},{"mix",0.28f}}, 4, {{"predly",25.0f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Reverse Whisper Throw", "Vocal Throws",
            buildState(1, {{"feedback",0.28f},{"tone",0.5f}}, 1, {{"mix",0.32f}}, "reverse_before_fx", 0.0f, false));
        add("Plate Lead Lift", "Vocal Throws",
            buildState(0, {}, 4, {{"tone",0.7f},{"duck",0.3f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Ghost Repeat Vocal", "Vocal Throws",
            buildState(5, {{"feedback",0.4f},{"mix",0.3f}}, 1, {{"mix",0.3f}}, "reverb_to_delay", 0.0f, false));
        add("Shimmer Adlib Cloud", "Vocal Throws",
            buildState(0, {}, 3, {{"shimmer",0.3f},{"mix",0.3f},{"width",0.85f}}, "delay_to_reverb", 0.0f, false));
        add("Dark Hook Throw", "Vocal Throws",
            buildState(4, {{"tone",0.35f}}, 1, {{"tone",0.4f},{"mix",0.28f}}, "delay_to_reverb", -1.0f, false));
        add("Parallel Clean Vocal Space", "Vocal Throws",
            buildState(2, {{"mix",0.3f}}, 2, {{"mix",0.22f}}, "parallel", 0.0f, false));

        // ---- Reverse FX ----
        add("Full Reverse Print", "Reverse FX",
            buildState(4, {{"mix",0.3f}}, 2, {{"mix",0.3f}}, "full_reverse_print", 0.0f, false));
        add("Reverse Tail Swell", "Reverse FX",
            buildState(0, {}, 4, {{"decay",0.45f},{"mix",0.4f}}, "reverse_tail", 0.0f, false));
        add("Reverse Snare Lift", "Reverse FX",
            buildState(0, {}, 4, {{"decay",0.35f},{"tone",0.65f},{"mix",0.4f}}, "reverse_tail", 0.0f, false));
        add("Reverse Guitar Bloom", "Reverse FX",
            buildState(3, {{"mix",0.25f}}, 1, {{"decay",0.5f},{"mix",0.35f}}, "reverse_before_fx", 0.0f, false));
        add("Evidence Riser", "Reverse FX",
            buildState(0, {}, 3, {{"shimmer",0.5f},{"decay",0.6f},{"mix",0.4f}}, "reverse_before_fx", 12.0f, false));
        add("Backwards Room Ghost", "Reverse FX",
            buildState(0, {}, 2, {{"mix",0.32f}}, "reverse_before_fx", 0.0f, false));

        // ---- Drums & Percussion ----
        add("Spring Snare Splash", "Drums & Percussion",
            buildState(0, {}, 5, {{"predly",8.0f},{"tone",0.5f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Dub Rim Tank", "Drums & Percussion",
            buildState(1, {{"feedback",0.45f}}, 5, {{"mix",0.3f}}, "feedback_verb_to_delay", 0.0f, false));
        add("Dusty Drum Room", "Drums & Percussion",
            buildState(6, {{"mix",0.18f},{"character",0.5f}}, 2, {{"mix",0.18f}}, "delay_to_reverb", 0.0f, false));
        add("Plate Snare Classic", "Drums & Percussion",
            buildState(0, {}, 4, {{"decay",0.42f},{"predly",18.0f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Percussion Scatter", "Drums & Percussion",
            buildState(2, {{"feedback",0.45f},{"mix",0.3f}}, 2, {{"mix",0.2f}}, "parallel", 0.0f, false));

        // ---- Guitar & Amp ----
        add("Amp Rust Spring", "Guitar & Amp",
            buildState(0, {}, 5, {{"width",0.3f},{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Tape Guitar Ghost", "Guitar & Amp",
            buildState(4, {{"tone",0.35f},{"movement",0.4f}}, 1, {{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Clean Lead Space", "Guitar & Amp",
            buildState(2, {{"mix",0.3f}}, 2, {{"mix",0.22f}}, "parallel", 0.0f, false));
        add("Reverse Guitar Case", "Guitar & Amp",
            buildState(3, {{"mix",0.25f}}, 1, {{"decay",0.5f},{"mix",0.35f}}, "reverse_before_fx", 0.0f, false));

        // ---- Keys & Synths ----
        add("Polymoog Cloud", "Keys & Synths",
            buildState(0, {}, 3, {{"shimmer",0.4f},{"width",0.85f},{"decay",0.5f}}, "delay_to_reverb", 0.0f, false));
        add("Dark Synth Alley", "Keys & Synths",
            buildState(5, {{"tone",0.4f},{"mix",0.25f}}, 1, {{"tone",0.4f},{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Modern Pad Space", "Keys & Synths",
            buildState(0, {}, 2, {{"width",0.8f},{"mix",0.3f}}, "delay_to_reverb", 0.0f, false));
        add("Vintage Keys Plate", "Keys & Synths",
            buildState(1, {{"mix",0.18f}}, 4, {{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));

        // ---- Dub & Space ----
        add("Feedback Dub Throw", "Dub & Space",
            buildState(1, {{"feedback",0.45f}}, 1, {{"mix",0.3f}}, "feedback_verb_to_delay", 0.0f, false));
        add("Space Tape Spiral", "Dub & Space",
            buildState(4, {{"feedback",0.4f},{"mix",0.32f}}, 3, {{"shimmer",0.35f},{"mix",0.35f}}, "delay_to_reverb", 0.0f, false));
        add("Cold Rack Dimension", "Dub & Space",
            buildState(5, {{"mix",0.28f}}, 2, {{"width",0.75f},{"mix",0.25f}}, "delay_to_reverb", 0.0f, false));
        add("Parallel Dub Chamber", "Dub & Space",
            buildState(3, {{"mix",0.3f}}, 1, {{"mix",0.25f}}, "parallel", 0.0f, false));
        add("Shimmer Freeze Prep", "Dub & Space",
            buildState(0, {}, 3, {{"shimmer",0.45f},{"decay",0.8f},{"mix",0.4f}}, "delay_to_reverb", 0.0f, false));

        // ---- Lo-Fi / Dust ----
        add("Dust Vault Default", "Lo-Fi / Dust",
            buildState(1, {{"mix",0.28f}}, 4, {{"mix",0.24f}}, "delay_to_reverb", 0.0f, false));
        add("Sampler Crime Scene", "Lo-Fi / Dust",
            buildState(6, {{"character",0.6f},{"movement",0.4f},{"mix",0.3f}}, 2, {{"mix",0.2f}}, "delay_to_reverb", 0.0f, false));
        add("Broken VHS Throw", "Lo-Fi / Dust",
            buildState(4, {{"movement",0.5f},{"tone",0.35f}}, 1, {{"tone",0.4f},{"mix",0.3f}}, "delay_to_reverb", -1.0f, false));
        add("Basement Cassette", "Lo-Fi / Dust",
            buildState(3, {{"tone",0.3f},{"mix",0.28f}}, 1, {{"width",0.3f},{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Evidence Degraded", "Lo-Fi / Dust",
            buildState(6, {{"character",0.8f},{"movement",0.5f},{"mix",0.35f}}, 5, {{"mix",0.2f}}, "delay_to_reverb", 0.0f, false));

        // ---- Utility / Starting Points ----
        add("Clean Start", "Utility / Starting Points",
            buildState(2, {{"mix",0.2f}}, 2, {{"mix",0.18f}}, "delay_to_reverb", 0.0f, false));
        add("Delay Only Start", "Utility / Starting Points",
            buildState(2, {{"mix",0.3f}}, 0, {}, "delay_to_reverb", 0.0f, false));
        add("Reverb Only Start", "Utility / Starting Points",
            buildState(0, {}, 4, {{"mix",0.28f}}, "delay_to_reverb", 0.0f, false));
        add("Print Safe Reverse", "Utility / Starting Points",
            buildState(1, {{"mix",0.25f}}, 2, {{"mix",0.25f}}, "reverse_before_fx", 0.0f, false));

        return v;
    }
}
