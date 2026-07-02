#pragma once
#include <JuceHeader.h>

// =============================================================================
//  DelayMachine — common interface for the delay flavors (Phase 8).
//
//  Each flavor owns its full sound: process() returns the FINAL dry/wet-mixed
//  stereo output (the machine applies its own mix/width/duck/etc). Params arrive
//  as a flat array p[0..7] interpreted per the flavor's knob layout below.
//
//  Convention: p[0] = Time (ms), p[1] = Feedback/Repeats — used by the host to
//  estimate the echo ring-out tail regardless of flavor.
// =============================================================================
class DelayMachine
{
public:
    virtual ~DelayMachine() = default;
    virtual void prepare(double sr) = 0;
    virtual void reset() = 0;
    virtual void setParams(const float* p) = 0;
    virtual void process(float inL, float inR, float& outL, float& outR) = 0;
};

struct DelayKnob
{
    juce::String name;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    bool  used() const { return name.isNotEmpty(); }
};

// Per-flavor knob layout (max 8 slots). The editor reads this to label, range,
// and default each knob, hiding unused slots. Display names are provisional.
inline juce::Array<DelayKnob> delayKnobLayout(int flavor)
{
    juce::Array<DelayKnob> k;
    auto add = [&k](const char* n, float lo, float hi, float def) { k.add({ n, lo, hi, def }); };

    switch (flavor)
    {
        case 1: // Reel Echo — multi-head tape / space echo
            add("Time", 1.0f, 2000.0f, 500.0f); add("Feedback",  0.0f, 0.95f, 0.28f);
            add("Tone",      0.0f, 1.0f, 0.55f); add("Character", 0.0f, 1.0f, 0.35f);
            add("Movement",  0.0f, 1.0f, 0.20f); add("Width",     0.0f, 1.0f, 0.45f);
            add("Duck",      0.0f, 1.0f, 0.15f); add("Mix",       0.0f, 1.0f, 0.25f);
            break;

        case 2: // Digital Pedal — clean/punchy digital pedal delay
            add("Time", 1.0f, 2000.0f, 350.0f); add("Feedback",  0.0f, 0.95f, 0.40f);
            add("Tone",      0.0f, 1.0f, 0.50f); add("Character", 0.0f, 1.0f, 0.20f);
            add("Mod",       0.0f, 1.0f, 0.00f); add("Width",     0.0f, 1.0f, 0.50f);
            add("Mix",       0.0f, 1.0f, 0.40f); add("Duck",      0.0f, 1.0f, 0.00f);
            break;

        case 3: // Magnetic Drum — rotating drum echo
            add("Time", 1.0f, 2000.0f, 500.0f); add("Feedback",  0.0f, 0.95f, 0.32f);
            add("Tone",      0.0f, 1.0f, 0.50f); add("Character", 0.0f, 1.0f, 0.40f);
            add("Movement",  0.0f, 1.0f, 0.25f); add("Width",     0.0f, 1.0f, 0.50f);
            add("Duck",      0.0f, 1.0f, 0.15f); add("Mix",       0.0f, 1.0f, 0.25f);
            break;

        case 4: // Tape Witness — gritty vintage tape slap
            add("Time", 1.0f, 2000.0f, 300.0f); add("Feedback",  0.0f, 0.95f, 0.30f);
            add("Tone",      0.0f, 1.0f, 0.50f); add("Character", 0.0f, 1.0f, 0.45f);
            add("Movement",  0.0f, 1.0f, 0.20f); add("Width",     0.0f, 1.0f, 0.35f);
            add("Duck",      0.0f, 1.0f, 0.12f); add("Mix",       0.0f, 1.0f, 0.30f);
            break;

        case 5: // Cold Rack — PCM-style studio digital delay
            add("Time", 1.0f, 2000.0f, 500.0f); add("Feedback",  0.0f, 0.95f, 0.30f);
            add("Tone",      0.0f, 1.0f, 0.60f); add("Character", 0.0f, 1.0f, 0.30f);
            add("Movement",  0.0f, 1.0f, 0.20f); add("Width",     0.0f, 1.0f, 0.60f);
            add("Duck",      0.0f, 1.0f, 0.20f); add("Mix",       0.0f, 1.0f, 0.25f);
            break;

        case 6: // Vault Delay — dirty sampler / Dust Vault delay
            add("Time", 1.0f, 2000.0f, 500.0f); add("Feedback",  0.0f, 0.95f, 0.30f);
            add("Tone",      0.0f, 1.0f, 0.45f); add("Character", 0.0f, 1.0f, 0.35f);
            add("Movement",  0.0f, 1.0f, 0.20f); add("Width",     0.0f, 1.0f, 0.45f);
            add("Duck",      0.0f, 1.0f, 0.20f); add("Mix",       0.0f, 1.0f, 0.25f);
            break;

        default: break;
    }
    while (k.size() < 8) k.add({});
    return k;
}

inline juce::String delayFlavorName(int flavor)
{
    switch (flavor) { case 1: return "reel_echo"; case 2: return "digital_pedal";
                      case 3: return "magnetic_drum"; case 4: return "tape_witness";
                      case 5: return "cold_rack"; case 6: return "vault_delay";
                      default: return "off"; }
}

inline int delayFlavorFromName(const juce::String& n)
{
    for (int i = 0; i <= 6; ++i) if (delayFlavorName(i) == n) return i;
    return 0;
}
