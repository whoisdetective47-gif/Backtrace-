#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SDColors.h"

// Noir brass round knob — matches the Detective 47 panel aesthetic.
// Radial brass gradient body, dark inner face, amber indicator arc + line.
class SquareKnobLAF : public juce::LookAndFeel_V4
{
public:
    SquareKnobLAF()
    {
        setColour(juce::Label::textColourId,                      SDCol::textGold);
        setColour(juce::Slider::textBoxTextColourId,              SDCol::textGold);
        setColour(juce::Slider::textBoxBackgroundColourId,        SDCol::dispBg);
        setColour(juce::Slider::textBoxOutlineColourId,           SDCol::dispBorder);
        setColour(juce::ComboBox::backgroundColourId,             SDCol::panelSection);
        setColour(juce::ComboBox::textColourId,                   SDCol::textGold);
        setColour(juce::ComboBox::outlineColourId,                SDCol::panelBorder);
        setColour(juce::ComboBox::arrowColourId,                  SDCol::textAmber);
        setColour(juce::PopupMenu::backgroundColourId,            SDCol::panelFace);
        setColour(juce::PopupMenu::textColourId,                  SDCol::textGold);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, SDCol::btnOn);
        setColour(juce::PopupMenu::highlightedTextColourId,       SDCol::textGold);
        setColour(juce::TextButton::buttonColourId,               SDCol::btnOff);
        setColour(juce::TextButton::buttonOnColourId,             SDCol::btnOn);
        setColour(juce::TextButton::textColourOffId,              SDCol::textAmber);
        setColour(juce::TextButton::textColourOnId,               SDCol::textGold);
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int w, int h,
                          float sliderPos,
                          float startAngle, float endAngle,
                          juce::Slider& /*slider*/) override
    {
        const float cx    = x + w * 0.5f;
        const float cy    = y + h * 0.5f;
        const float r     = std::min(w, h) * 0.42f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        // ---- Drop shadow ----
        g.setColour(juce::Colour(0x55000000));
        g.fillEllipse(cx - r - 1.0f, cy - r + 2.5f, (r + 1.0f) * 2.0f, (r + 1.0f) * 2.0f);

        // ---- Brass body (radial gradient: top-left gold → bottom-right deep brown) ----
        {
            juce::ColourGradient brass(
                SDCol::knobBrass1, cx - r * 0.28f, cy - r * 0.28f,
                SDCol::knobBrass4, cx + r * 0.72f, cy + r * 0.72f, true);
            brass.addColour(0.38, SDCol::knobBrass2);
            brass.addColour(0.68, SDCol::knobBrass3);
            g.setGradientFill(brass);
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        // ---- Inner dark face ----
        {
            const float ir = r * 0.70f;
            juce::ColourGradient inner(
                SDCol::knobInner1, cx - ir * 0.3f, cy - ir * 0.4f,
                SDCol::knobInner2, cx + ir * 0.5f, cy + ir * 0.6f, true);
            g.setGradientFill(inner);
            g.fillEllipse(cx - ir, cy - ir, ir * 2.0f, ir * 2.0f);
        }

        // ---- Arc track (full range, very dark) ----
        {
            const float ar = r * 0.58f;
            juce::Path track;
            track.addArc(cx - ar, cy - ar, ar * 2.0f, ar * 2.0f, startAngle, endAngle, true);
            g.setColour(SDCol::bg.withAlpha(0.75f));
            g.strokePath(track, juce::PathStrokeType(1.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- Active amber arc ----
        {
            const float ar = r * 0.58f;
            juce::Path arc;
            arc.addArc(cx - ar, cy - ar, ar * 2.0f, ar * 2.0f, startAngle, angle, true);
            g.setColour(SDCol::textGold.withAlpha(0.85f));
            g.strokePath(arc, juce::PathStrokeType(1.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- Indicator line ----
        {
            const float len  = r * 0.70f * 0.68f;
            const float dotR = 1.8f;
            const float px   = cx + std::sin(angle) * len;
            const float py   = cy - std::cos(angle) * len;
            g.setColour(SDCol::textGold);
            g.drawLine(cx, cy, px, py, 1.6f);
            g.fillEllipse(px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
        }

        // ---- Top-left brass rim highlight ----
        {
            const float ar = r - 1.5f;
            juce::Path rim;
            rim.addArc(cx - ar, cy - ar, ar * 2.0f, ar * 2.0f,
                       -juce::MathConstants<float>::pi * 0.78f,
                       -juce::MathConstants<float>::pi * 0.06f, true);
            g.setColour(SDCol::knobBrass1.withAlpha(0.45f));
            g.strokePath(rim, juce::PathStrokeType(1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ---- Centre pip ----
        g.setColour(SDCol::panelDeep);
        g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);
    }
};
