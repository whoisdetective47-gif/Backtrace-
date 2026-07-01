#pragma once
#include <JuceHeader.h>

// =============================================================================
//  LiveScope — real-time output waveform for LIVE PREVERB mode.
//
//  Draws a decimated peak envelope of the plugin's OUTPUT (fed by a read-only
//  tap in processBlock), scrolling left→right so the reverse-reverb swell is
//  visible blooming into each incoming transient. Purely a display — it reads a
//  copy of the signal and never touches the audio path.
// =============================================================================
class LiveScope : public juce::Component
{
public:
    std::function<void (float*)> fetch;   // fills [size] floats, oldest→newest
    int size = 512;

    LiveScope() { setInterceptsMouseClicks(false, false); }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float r = 6.0f;

        g.setColour(juce::Colour(0xff0e1218));                 // scope panel
        g.fillRoundedRectangle(b, r);
        g.setColour(juce::Colour(0xff2b3240));
        g.drawRoundedRectangle(b.reduced(0.5f), r, 1.0f);

        const float cy = b.getCentreY();
        g.setColour(juce::Colours::white.withAlpha(0.06f));    // centre line
        g.drawHorizontalLine((int) cy, b.getX() + 8.0f, b.getRight() - 8.0f);

        if (! fetch || size < 2) return;
        std::vector<float> peaks((size_t) size, 0.0f);
        fetch(peaks.data());

        const float left = b.getX() + 8.0f, right = b.getRight() - 8.0f, w = right - left;
        const float halfH = b.getHeight() * 0.5f - 8.0f;

        juce::Path wave;
        for (int i = 0; i < size; ++i)                         // top edge oldest→newest
        {
            const float x = left + w * (float) i / (float) (size - 1);
            const float h = juce::jlimit(0.0f, 1.0f, peaks[(size_t) i]) * halfH;
            if (i == 0) wave.startNewSubPath(x, cy - h);
            else        wave.lineTo(x, cy - h);
        }
        for (int i = size - 1; i >= 0; --i)                    // mirror back for the fill
        {
            const float x = left + w * (float) i / (float) (size - 1);
            const float h = juce::jlimit(0.0f, 1.0f, peaks[(size_t) i]) * halfH;
            wave.lineTo(x, cy + h);
        }
        wave.closeSubPath();
        g.setColour(juce::Colour(0xff7ad1ff).withAlpha(0.85f));
        g.fillPath(wave);

        g.setColour(juce::Colours::white.withAlpha(0.35f));    // newest edge = playhead
        g.drawVerticalLine((int) right, b.getY() + 6.0f, b.getBottom() - 6.0f);

        g.setColour(juce::Colour(0xff7ad1ff));                 // LIVE tag
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("LIVE", (int) b.getX() + 10, (int) b.getY() + 6, 60, 14, juce::Justification::left);
    }
};
