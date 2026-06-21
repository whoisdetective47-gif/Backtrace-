#pragma once
#include <juce_graphics/juce_graphics.h>

// Sound Detective noir palette — drawn from dust1200_noir_v5 design.
// Dark near-black panel, brass/gold knobs, amber displays, warm hardware feel.
namespace SDCol
{
    // ---- Panel / Structure ----
    inline const juce::Colour bg          { 0xFF0A0806 };  // overall background: deep warm black
    inline const juce::Colour panelFace   { 0xFF1E1C18 };  // main panel face (dark warm brown-black)
    inline const juce::Colour panelDeep   { 0xFF141210 };  // deeper gradient end
    inline const juce::Colour panelBorder { 0xFF2A2418 };  // panel edge
    inline const juce::Colour panelSection{ 0xFF18160E };  // sub-panel / section background
    inline const juce::Colour divider     { 0xFF2E2A1C };  // section divider lines

    // ---- Knob brass (matches knobBrass gradient in SVG) ----
    inline const juce::Colour knobBrass1  { 0xFFA08030 };  // highlight (top-left)
    inline const juce::Colour knobBrass2  { 0xFF6A5020 };  // mid-tone
    inline const juce::Colour knobBrass3  { 0xFF3A2810 };  // shadow
    inline const juce::Colour knobBrass4  { 0xFF1E1408 };  // deep shadow edge
    inline const juce::Colour knobInner1  { 0xFF2A2010 };  // inner face highlight
    inline const juce::Colour knobInner2  { 0xFF100C06 };  // inner face deep

    // ---- Display / readout ----
    inline const juce::Colour dispBg      { 0xFF0C0A04 };  // display background (near-black warm)
    inline const juce::Colour dispBorder  { 0xFF0A0806 };  // display border

    // ---- Text / labels ----
    inline const juce::Colour textGold    { 0xFFD09020 };  // primary label: bright amber/gold
    inline const juce::Colour textAmber   { 0xFF907050 };  // secondary label: mid amber
    inline const juce::Colour textDim     { 0xFF7A6030 };  // dim label: dark gold
    inline const juce::Colour textSub     { 0xFF4A3818 };  // very dim / inactive
    inline const juce::Colour textDisplay { 0xFFD09020 };  // display readout: amber

    // ---- Fader / slider ----
    inline const juce::Colour faderTrack  { 0xFF141008 };  // track background
    inline const juce::Colour faderThumb1 { 0xFF282420 };  // thumb dark edge
    inline const juce::Colour faderThumb2 { 0xFF787060 };  // thumb highlight centre
    inline const juce::Colour faderFill   { 0xFF3A2E10 };  // active fill

    // ---- Button states ----
    inline const juce::Colour btnOff      { 0xFF1E1C14 };
    inline const juce::Colour btnOn       { 0xFF3A2E10 };
    inline const juce::Colour btnOnBorder { 0xFFD09020 };
    inline const juce::Colour btnLED      { 0xFFE0A820 };  // lit LED amber

    // ---- Machine Drift accent ----
    inline const juce::Colour driftAccent { 0xFF604818 };  // subtle warm highlight for drift section

    // ---- Convenience aliases ----
    inline const juce::Colour gold        = textGold;
    inline const juce::Colour amber       = textAmber;
}
