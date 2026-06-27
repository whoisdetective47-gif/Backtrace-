#pragma once
#include <JuceHeader.h>

// =============================================================================
//  KnobLNF — larger, higher-contrast rotary knob.
//
//  Replaces JUCE's tiny default rotary dot with a bold value ring + a bright
//  pointer dot, so the macros read clearly and feel playable. The accent colour
//  comes from each slider's thumbColourId (amber macros / blue FX knobs).
// =============================================================================
class KnobLNF : public juce::LookAndFeel_V4
{
public:
    KnobLNF()
    {
        setColour(juce::Slider::textBoxTextColourId,    juce::Colour(0xffe8e8ea));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  centre = bounds.getCentre();
        const float ang    = startAngle + pos * (endAngle - startAngle);
        const float ringR  = radius - 2.0f;
        const float thick  = juce::jmax(3.0f, radius * 0.16f);

        const auto accent = s.findColour(juce::Slider::thumbColourId);

        // background track arc
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff2b3138));
        g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        juce::Path val;
        val.addCentredArc(centre.x, centre.y, ringR, ringR, 0.0f, startAngle, ang, true);
        g.setColour(accent);
        g.strokePath(val, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob body
        const float bodyR = ringR - thick * 0.85f;
        g.setColour(juce::Colour(0xff1b2026));
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(juce::Colour(0x33ffffff));
        g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // pointer dot
        const float dotDist = bodyR * 0.62f;
        const float dotR    = juce::jmax(2.5f, radius * 0.16f);
        const juce::Point<float> dot(centre.x + dotDist * std::cos(ang - juce::MathConstants<float>::halfPi),
                                     centre.y + dotDist * std::sin(ang - juce::MathConstants<float>::halfPi));
        g.setColour(accent.brighter(0.4f));
        g.fillEllipse(dot.x - dotR, dot.y - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    juce::Label* createSliderTextBox(juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(juce::Font(13.0f, juce::Font::bold));
        return l;
    }
};
