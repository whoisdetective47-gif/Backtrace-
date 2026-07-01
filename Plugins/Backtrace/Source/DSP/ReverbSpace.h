#pragma once
#include <JuceHeader.h>

// =============================================================================
//  ReverbSpace — common interface for the reverb flavors (Phase 9).
//
//  Like DelayMachine: each flavor owns its full sound; process() returns the
//  final dry/wet-mixed stereo. Params arrive as p[0..9] per the flavor's layout.
//  Convention: p[1] = Decay, p[2] = PreDelay(ms) — used to estimate the tail.
// =============================================================================
class ReverbSpace
{
public:
    virtual ~ReverbSpace() = default;
    virtual void prepare(double sr) = 0;
    virtual void reset() = 0;
    virtual void setParams(const float* p) = 0;
    virtual void process(float inL, float inR, float& outL, float& outR) = 0;
};

struct ReverbKnob
{
    juce::String name;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    bool  used() const { return name.isNotEmpty(); }
};

// Per-flavor knob layout (max 10 slots). Display names are provisional.
inline juce::Array<ReverbKnob> reverbKnobLayout(int flavor)
{
    juce::Array<ReverbKnob> k;
    auto add = [&k](const char* n, float lo, float hi, float def) { k.add({ n, lo, hi, def }); };

    switch (flavor)
    {
        case 1: // Velvet Hall — Lexicon-style hall/chamber
            add("Size",   0.0f, 1.0f,   0.55f);  add("Decay",     0.0f, 1.0f,   0.35f);
            add("PreDly", 0.0f, 250.0f, 20.0f);  add("Tone",      0.0f, 1.0f,   0.66f);
            add("Diffuse",0.0f, 1.0f,   0.72f);  add("Mod",       0.0f, 1.0f,   0.25f);
            add("Width",  0.0f, 1.0f,   0.70f);  add("Mix",       0.0f, 1.0f,   0.25f);
            add("Duck",   0.0f, 1.0f,   0.20f);  add("Output",  -24.0f, 12.0f,  0.00f);
            break;

        case 2: // Modern Space — Bricasti-style modern room/chamber/hall
            add("Size",   0.0f, 1.0f,   0.50f);  add("Decay",     0.0f, 1.0f,   0.32f);
            add("PreDly", 0.0f, 200.0f, 15.0f);  add("Tone",      0.0f, 1.0f,   0.55f);
            add("Diffuse",0.0f, 1.0f,   0.55f);  add("Mod",       0.0f, 1.0f,   0.15f);
            add("Width",  0.0f, 1.0f,   0.65f);  add("Mix",       0.0f, 1.0f,   0.22f);
            add("Duck",   0.0f, 1.0f,   0.15f);  add("Output",  -24.0f, 12.0f,  0.00f);
            break;

        case 3: // Shimmer — octave-bloom ambient reverb (Duck → Shimmer)
            add("Size",   0.0f, 1.0f,   0.65f);  add("Decay",     0.0f, 1.0f,   0.55f);
            add("PreDly", 0.0f, 150.0f, 35.0f);  add("Tone",      0.0f, 1.0f,   0.60f);
            add("Diffuse",0.0f, 1.0f,   0.75f);  add("Mod",       0.0f, 1.0f,   0.30f);
            add("Width",  0.0f, 1.0f,   0.80f);  add("Mix",       0.0f, 1.0f,   0.28f);
            add("Shimmer",0.0f, 1.0f,   0.35f);  add("Output",  -24.0f, 12.0f,  0.00f);
            break;

        case 4: // Studio Plate — EMT-140-style dense plate
            add("Size",   0.0f, 1.0f,   0.55f);  add("Decay",     0.0f, 1.0f,   0.42f);
            add("PreDly", 0.0f, 120.0f, 22.0f);  add("Tone",      0.0f, 1.0f,   0.58f);
            add("Diffuse",0.0f, 1.0f,   0.85f);  add("Mod",       0.0f, 1.0f,   0.18f);
            add("Width",  0.0f, 1.0f,   0.72f);  add("Mix",       0.0f, 1.0f,   0.24f);
            add("Duck",   0.0f, 1.0f,   0.18f);  add("Output",  -24.0f, 12.0f,  0.00f);
            break;

        case 5: // 626 Spring — metallic spring tank
            add("Size",   0.0f, 1.0f,   0.45f);  add("Decay",     0.0f, 1.0f,   0.38f);
            add("PreDly", 0.0f, 80.0f,  12.0f);  add("Tone",      0.0f, 1.0f,   0.52f);
            add("Diffuse",0.0f, 1.0f,   0.42f);  add("Mod",       0.0f, 1.0f,   0.20f);
            add("Width",  0.0f, 1.0f,   0.58f);  add("Mix",       0.0f, 1.0f,   0.22f);
            add("Duck",   0.0f, 1.0f,   0.12f);  add("Output",  -24.0f, 12.0f,  0.00f);
            break;

        default: break;
    }
    while (k.size() < 10) k.add({});
    return k;
}

inline juce::String reverbFlavorName(int flavor)
{
    switch (flavor) { case 1: return "velvet_hall"; case 2: return "modern_space";
                      case 3: return "shimmer"; case 4: return "studio_plate";
                      case 5: return "626_spring"; default: return "off"; }
}

inline int reverbFlavorFromName(const juce::String& n)
{
    for (int i = 0; i <= 5; ++i) if (reverbFlavorName(i) == n) return i;
    return 0;
}
