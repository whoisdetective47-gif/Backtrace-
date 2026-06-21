#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SDColors.h"
#include "SnapSlider.h"

// Noir vertical fader — matches the SVG panel design.
// When the slider is a SnapSlider with snapEnabled=true, draws prominent
// interval markers instead of plain semitone ticks.
class PitchSliderLAF : public juce::LookAndFeel_V4
{
public:
    PitchSliderLAF()
    {
        setColour(juce::Slider::textBoxTextColourId,       SDCol::textGold);
        setColour(juce::Slider::textBoxBackgroundColourId, SDCol::dispBg);
        setColour(juce::Slider::textBoxOutlineColourId,    SDCol::dispBorder);
    }

    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float minPos,    // bottom of track  (larger y)
                          float maxPos,    // top of track     (smaller y)
                          const juce::Slider::SliderStyle /*style*/,
                          juce::Slider& slider) override
    {
        // ── Layout constants ───────────────────────────────────────────────
        const float labelW    = (float)width * 0.40f;
        const float trackW    = 12.0f;
        const float handleW   = (float)width * 0.60f - 4.0f;
        const float handleH   = 32.0f;
        const float rightZoneX = (float)x + labelW;
        const float rightZoneW = (float)width - labelW;
        const float trackX    = rightZoneX + (rightZoneW - trackW) * 0.5f;

        auto* snap      = dynamic_cast<SnapSlider*>(&slider);
        bool  snapMode  = snap && snap->snapEnabled;

        // ── Track slot (recessed channel) ─────────────────────────────────
        g.setColour(juce::Colour(0xFF080604));
        g.fillRoundedRectangle(trackX - 3.0f, (float)y, trackW + 6.0f, (float)height, 4.0f);

        g.setColour(SDCol::faderTrack);
        g.fillRoundedRectangle(trackX, (float)y, trackW, (float)height, 3.0f);

        // ── Active fill (center→thumb, amber glow) ────────────────────────
        {
            auto   range      = slider.getRange();
            double normCenter = (0.0 - range.getStart()) / range.getLength();
            float  centerY    = minPos + (float)((maxPos - minPos) * normCenter);
            float  top        = std::min(sliderPos, centerY);
            float  bot        = std::max(sliderPos, centerY);
            if (bot - top > 1.0f)
            {
                g.setColour(SDCol::faderFill);
                g.fillRoundedRectangle(trackX, top, trackW, bot - top, 2.0f);
                g.setColour(SDCol::textGold.withAlpha(0.12f));
                g.fillRoundedRectangle(trackX - 2.0f, top, trackW + 4.0f, bot - top, 3.0f);
            }
        }

        auto range = slider.getRange();

        if (snapMode && snap != nullptr)
        {
            // ── SNAP MODE: prominent interval markers ──────────────────────
            for (auto& pt : snap->snapPoints)
            {
                double norm  = (pt.value - range.getStart()) / range.getLength();
                float  tickY = minPos + (float)((maxPos - minPos) * norm);
                bool   isRoot = (pt.value == 0.0);
                bool   isOct  = (std::abs(pt.value) == 12.0 || std::abs(pt.value) == 24.0);

                // Highlight current snapped value
                bool isActive = std::abs(slider.getValue() - pt.value) < 0.1;

                // Tick line into track — minor (unlabelled) intervals get shorter ticks
                float tickLen = isRoot ? 16.0f : (isOct ? 12.0f : (pt.major ? 9.0f : 6.0f));
                float tickH   = isRoot ? 2.5f  : (isOct ? 2.0f  : (pt.major ? 1.5f : 1.2f));
                auto  tickCol = isActive ? SDCol::textGold
                              : isRoot   ? SDCol::textGold.withAlpha(0.70f)
                              : isOct    ? SDCol::textDim
                              : pt.major ? SDCol::textDim
                              :            SDCol::textSub;

                g.setColour(tickCol);
                g.fillRect(trackX - tickLen, tickY - tickH * 0.5f, tickLen, tickH);

                // Snap-indicator dot — smaller for minor detents
                float dotR = (pt.major || isActive) ? 3.0f : 2.0f;
                g.setColour(isActive ? SDCol::textGold : tickCol.withAlpha(0.6f));
                g.fillEllipse(trackX - tickLen - 4.0f, tickY - dotR, dotR * 2.0f, dotR * 2.0f);

                // Label — landmarks always; minor intervals only when selected
                if (pt.major || isActive)
                {
                    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                         isRoot ? 9.5f : 8.0f,
                                         isActive ? juce::Font::bold : juce::Font::plain));
                    g.setColour(isActive ? SDCol::textGold : tickCol);
                    g.drawText(pt.label,
                               x, (int)(tickY - 8.0f),
                               (int)(trackX - tickLen - 6.0f) - x, 15,
                               juce::Justification::centredRight, false);
                }
            }

            // Zero line on track
            {
                double norm  = (0.0 - range.getStart()) / range.getLength();
                float  zeroY = minPos + (float)((maxPos - minPos) * norm);
                g.setColour(SDCol::textGold.withAlpha(0.50f));
                g.fillRect(trackX, zeroY - 0.8f, trackW, 1.6f);
            }
        }
        else
        {
            // ── FREE MODE: semitone tick marks ─────────────────────────────
            const float semis[] = { -24.0f, -12.0f, -7.0f, 0.0f, 7.0f, 12.0f, 24.0f };
            const char* lbls[]  = { "-24", "-12", "-7", "0", "+7", "+12", "+24" };

            for (int i = 0; i < 7; ++i)
            {
                double norm  = (semis[i] - range.getStart()) / range.getLength();
                float  tickY = minPos + (float)((maxPos - minPos) * norm);
                bool   isZ   = (semis[i] == 0.0f);
                bool   isKey = isZ || std::abs(semis[i]) == 24.0f || std::abs(semis[i]) == 12.0f;

                float tickLen = isZ ? 14.0f : (isKey ? 9.0f : 6.0f);
                g.setColour(isZ ? SDCol::textGold : (isKey ? SDCol::textDim : SDCol::textSub));
                g.fillRect(trackX - tickLen, tickY - (isZ ? 1.0f : 0.7f), tickLen, isZ ? 2.0f : 1.4f);

                if (isKey)
                {
                    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                         isZ ? 9.5f : 8.0f, juce::Font::plain));
                    g.setColour(isZ ? SDCol::textGold : SDCol::textDim);
                    g.drawText(lbls[i],
                               x, (int)(tickY - 8.0f),
                               (int)(trackX - tickLen - 3.0f) - x + 2, 15,
                               juce::Justification::centredRight, false);
                }
            }

            // Center-zero indicator on track
            {
                double norm  = (0.0 - range.getStart()) / range.getLength();
                float  zeroY = minPos + (float)((maxPos - minPos) * norm);
                g.setColour(SDCol::textGold.withAlpha(0.40f));
                g.fillRect(trackX, zeroY - 0.8f, trackW, 1.6f);
            }
        }

        // ── Handle (fader cap) ────────────────────────────────────────────
        {
            float hx = rightZoneX + (rightZoneW - handleW) * 0.5f;
            float hy = sliderPos - handleH * 0.5f;
            juce::Rectangle<float> handle(hx, hy, handleW, handleH);

            // Snap-mode handle gets a subtle amber tint to signal active mode
            auto handleEdge = snapMode ? juce::Colour(0xFF6A5010) : juce::Colour(0xFF3A3630);

            // Drop shadow
            g.setColour(juce::Colour(0x60000000));
            g.fillRoundedRectangle(handle.translated(2.0f, 3.0f), 4.0f);

            // Body gradient
            juce::ColourGradient body(
                SDCol::faderThumb1, hx,           hy + handleH * 0.5f,
                SDCol::faderThumb1, hx + handleW, hy + handleH * 0.5f, false);
            body.addColour(0.20, SDCol::faderThumb2);
            body.addColour(0.50, juce::Colour(0xFF949080));
            body.addColour(0.80, SDCol::faderThumb2);
            g.setGradientFill(body);
            g.fillRoundedRectangle(handle, 4.0f);

            // Grooves
            float midY = hy + handleH * 0.5f;
            for (int gi = -1; gi <= 1; ++gi)
            {
                float gy = midY + gi * 5.5f;
                g.setColour(juce::Colour(0x50000000));
                g.fillRect(hx + 6.0f, gy - 1.0f, handleW - 12.0f, 2.0f);
                g.setColour(juce::Colour(0x18FFFFFF));
                g.fillRect(hx + 6.0f, gy + 1.0f, handleW - 12.0f, 1.0f);
            }

            // Amber centre stripe
            g.setColour(SDCol::textGold.withAlpha(0.85f));
            g.fillRect(hx + 6.0f, midY - 1.0f, handleW - 12.0f, 2.0f);
            g.setColour(SDCol::textGold.withAlpha(0.20f));
            g.fillRect(hx + 6.0f, midY - 4.0f, handleW - 12.0f, 8.0f);

            // Top highlight
            g.setColour(juce::Colour(0x30FFFFFF));
            g.fillRoundedRectangle(hx + 3.0f, hy + 2.0f, handleW - 6.0f, 5.0f, 2.0f);

            // Outline — amber tint in snap mode
            g.setColour(handleEdge);
            g.drawRoundedRectangle(handle, 4.0f, 1.0f);
        }
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(SDCol::dispBg);
        g.setColour(SDCol::textGold);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
        g.drawText(label.getText(), label.getLocalBounds().reduced(2, 0),
                   juce::Justification::centred, false);
    }
};
