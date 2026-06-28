#pragma once
#include <JuceHeader.h>
#include "FadeCurve.h"

// =============================================================================
//  SwellCanvas — the PRINTED SWELL lane (the final editable musical object).
//
//  Shows the rendered reverse swell with a bars/beats ruler, subtle beat grid,
//  visual zoom, optional grid-snap, its own A/B locators and draggable fade-in /
//  fade-out handles. Everything here is what gets auditioned / printed / exported
//  / dragged, so the edits are exactly the edits baked into the output.
//
//  Zoom is purely visual (a view window into the buffer); it never changes the
//  audio. Snap quantises handle POSITIONS to the musical grid — it does not
//  time-stretch or alter audio content. Fades are sample lengths relative to the
//  trimmed [A,B] region; locators are sample positions into the full render.
// =============================================================================
class SwellCanvas : public juce::Component
{
public:
    std::function<void(int, int)> onLocatorsChanged;   // (A, B) samples into the render
    std::function<void(int, int)> onFadesChanged;       // (fadeInLen, fadeOutLen) samples
    std::function<juce::File()>   provideDragFile;      // returns the edited swell WAV to drag out
    std::function<void(int)>      onFadeMenu;           // right-click a fade region (0 = in, 1 = out)

    void setSwell(const juce::AudioBuffer<float>* buf, int length, double sr,
                  int a, int b, int fadeIn, int fadeOut)
    {
        source     = buf;
        sourceLen  = length;
        sampleRate = sr;
        trimIn     = juce::jlimit(0, juce::jmax(0, length), a);
        trimOut    = juce::jlimit(trimIn, juce::jmax(0, length), b > 0 ? b : length);
        fadeInLen  = juce::jlimit(0, trimOut - trimIn, fadeIn);
        fadeOutLen = juce::jlimit(0, trimOut - trimIn, fadeOut);
        viewStart = 0; viewEnd = juce::jmax(1, length);   // fit
        rebuildPeaks();
        repaint();
    }

    void clearSwell() { source = nullptr; sourceLen = 0; peaks.clear(); repaint(); }
    void setEmptyHint(const juce::String& h) { emptyHint = h; if (peaks.empty()) repaint(); }
    void setPlayhead(int sample) { if (sample != playhead) { playhead = sample; repaint(); } }
    // Landing sample = where the reverse rise resolves; the Ringout (if any) follows it.
    // -1 = fall back to the trim end (legacy / no ringout).
    void setLanding(int sample) { if (sample != landing) { landing = sample; repaint(); } }
    void setMusical(double barsIn, int beatsPerBarIn)
    { bars = juce::jmax(0.0625, barsIn); beatsPerBar = juce::jmax(1, beatsPerBarIn); repaint(); }
    void setSnap(int div) { snapDiv = div; }
    void setFadeCurves(int in, int out) { fadeInCurve = in; fadeOutCurve = out; repaint(); }

    void zoomFit()  { viewStart = 0; viewEnd = juce::jmax(1, sourceLen); rebuildPeaks(); repaint(); }
    void zoomIn()   { applyZoom(0.66); }
    void zoomOut()  { applyZoom(1.0 / 0.66); }

    int getTrimIn()  const { return trimIn; }
    int getTrimOut() const { return trimOut; }
    int getFadeIn()  const { return fadeInLen; }
    int getFadeOut() const { return fadeOutLen; }

    void resized() override { rebuildPeaks(); }

    void paint(juce::Graphics& g) override
    {
        auto rf = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff141a18));
        g.fillRoundedRectangle(rf, 6.0f);

        if (peaks.empty())
        {
            g.setColour(juce::Colour(0x18ffffff));
            g.drawHorizontalLine((int) rf.getCentreY(), rf.getX(), rf.getRight());
            g.setColour(juce::Colour(0x88ffd9a4));
            g.setFont(13.0f);
            g.drawFittedText(emptyHint, getLocalBounds().reduced(20), juce::Justification::centred, 3);
            return;
        }

        const float waveTop = (float) kRulerH;
        const float midY    = waveTop + (getHeight() - waveTop) * 0.5f;
        const float scale   = (getHeight() - waveTop) * 0.44f;

        const float xa  = sampleToX(trimIn);
        const float xb  = sampleToX(trimOut);
        const float xfi = sampleToX(trimIn + fadeInLen);
        const float xfo = sampleToX(trimOut - fadeOutLen);

        // selection highlight (below ruler)
        g.setColour(juce::Colour(0x22d9a441));
        g.fillRect(juce::Rectangle<float>(xa, waveTop, xb - xa, (float) getHeight() - waveTop));

        drawGrid(g, waveTop);

        // waveform — amber inside the selection, dim outside
        for (size_t x = 0; x < peaks.size(); ++x)
        {
            const bool inSel = ((float) x >= xa && (float) x <= xb);
            g.setColour(inSel ? juce::Colour(0xffd9a441) : juce::Colour(0x40916b2a));
            const float h = peaks[x] * scale;
            g.drawVerticalLine((int) x, midY - h, midY + h);
        }

        // gain-envelope overlay — the ACTUAL fade-curve shapes + shaded fade regions
        const float yTop = waveTop + 4.0f, yBot = (float) getHeight() - 5.0f;
        auto yForGain = [&](float gain) { return yBot + (yTop - yBot) * gain; };

        g.setColour(juce::Colour(0x1cffffff));   // shade the fade regions
        if (xfi > xa) g.fillRect(juce::Rectangle<float>(xa, waveTop, xfi - xa, (float) getHeight() - waveTop));
        if (xb > xfo) g.fillRect(juce::Rectangle<float>(xfo, waveTop, xb - xfo, (float) getHeight() - waveTop));

        juce::Path env;
        env.startNewSubPath(xa, yForGain(0.0f));
        for (int k = 1; k <= 24; ++k) { const float t = k / 24.0f; env.lineTo(xa + (xfi - xa) * t, yForGain(btFadeGain(fadeInCurve, t))); }
        env.lineTo(xfo, yTop);
        for (int k = 1; k <= 24; ++k) { const float t = k / 24.0f; env.lineTo(xfo + (xb - xfo) * t, yForGain(btFadeGain(fadeOutCurve, 1.0f - t))); }
        g.setColour(juce::Colour(0xdd7ad1ff));
        g.strokePath(env, juce::PathStrokeType(1.8f));
        drawFadeHandle(g, xfi, yTop);
        drawFadeHandle(g, xfo, yTop);

        if (playhead >= 0 && playhead >= viewStart && playhead <= viewEnd)   // audition playhead
        {
            const float px = sampleToX(playhead);
            g.setColour(juce::Colour(0xffffffff));
            g.fillRect(px - 0.5f, waveTop, 1.5f, (float) getHeight() - waveTop);
        }

        drawLocator(g, xa, waveTop, juce::Colour(0xff3ad17a), "A");
        drawLocator(g, xb, waveTop, juce::Colour(0xffe2a44a), "B");

        // Landing Point — where the reverse rise resolves (lands into the source word).
        // It sits at the END OF THE RISE; any Tail Ringout continues AFTER it (the marker
        // never moves with ringout). Drawn in the Source-End / Tail-Start colour so the
        // two read as a pair: source ends → tail reverses → swell LANDS here → rings out.
        {
            const int   lSamp = (landing >= 0 && landing <= sourceLen) ? landing : trimOut;
            const float xl    = sampleToX(lSamp);
            const juce::Colour lc(0xffd1593a);
            g.setColour(lc.withAlpha(0.85f));
            g.fillRect(xl - 0.75f, waveTop, 1.5f, (float) getHeight() - waveTop);   // landing line
            juce::Rectangle<float> tab(xl - 64.0f, (float) getHeight() - 14.0f, 62.0f, 13.0f);
            if (tab.getX() < 2.0f) tab.setX(2.0f);
            g.setColour(lc);
            g.fillRect(tab);
            g.setColour(juce::Colour(0xff14171a));
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText("LANDING >", tab, juce::Justification::centred);
        }

        drawRuler(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (peaks.empty()) return;
        const int xa  = (int) sampleToX(trimIn);
        const int xb  = (int) sampleToX(trimOut);
        const int xfi = (int) sampleToX(trimIn + fadeInLen);
        const int xfo = (int) sampleToX(trimOut - fadeOutLen);
        const int fadeBot = kRulerH + 26;
        const int grab = 16;

        if (e.y < kRulerH) { dragTarget = 0; bodyDrag = false; return; }   // ruler: ignore

        // Right-click → curve menu for the nearer fade region.
        if (e.mods.isPopupMenu())
        {
            if (onFadeMenu) onFadeMenu(std::abs(e.x - xfi) <= std::abs(e.x - xfo) ? 0 : 1);
            dragTarget = 0; bodyDrag = false; return;
        }

        // Fade strip → pick the fade handle by CLOSEST pixel distance to its actual
        // anchor (fade-in end / fade-out start). Never by which half — that was the
        // bug that made the left handle unreachable once pushed past centre.
        if (e.y < fadeBot)
        {
            dragTarget = (std::abs(e.x - xfi) <= std::abs(e.x - xfo)) ? 3 : 4;
            applyDrag(e.x); return;
        }
        if (std::abs(e.x - xa) <= grab) { dragTarget = 1; applyDrag(e.x); return; }
        if (std::abs(e.x - xb) <= grab) { dragTarget = 2; applyDrag(e.x); return; }

        dragTarget = 0; bodyDrag = true;   // middle → drag the clip out
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragTarget != 0) { applyDrag(e.x); return; }
        if (bodyDrag && ! dragging && provideDragFile != nullptr && e.getDistanceFromDragStart() >= 8)
        {
            auto f = provideDragFile();
            if (! f.existsAsFile()) return;
            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                { f.getFullPathName() }, false, this,
                [safe = juce::Component::SafePointer<SwellCanvas>(this)] { if (safe != nullptr) safe->dragging = false; });
        }
    }

    void mouseUp(const juce::MouseEvent&) override { dragTarget = 0; bodyDrag = false; }

private:
    void applyZoom(double factor)
    {
        if (sourceLen <= 0) return;
        const double centre = (viewStart + viewEnd) * 0.5;
        double len = juce::jlimit(64.0, (double) sourceLen, (viewEnd - viewStart) * factor);
        viewStart = (int) juce::jlimit(0.0, sourceLen - len, centre - len * 0.5);
        viewEnd   = (int) juce::jmin((double) sourceLen, viewStart + len);
        rebuildPeaks(); repaint();
    }

    double samplesPerBeat() const { return (double) sourceLen / juce::jmax(1.0, bars * beatsPerBar); }

    int snap(int s) const
    {
        if (snapDiv <= 0 || sourceLen <= 0) return s;
        double beats = 0.0;
        switch (snapDiv) { case 1: beats = beatsPerBar; break; case 2: beats = 2.0; break;
                           case 3: beats = 1.0; break; case 4: beats = 0.5; break; case 5: beats = 0.25; break; }
        const double gs = beats * samplesPerBeat();
        if (gs < 1.0) return s;
        return juce::jlimit(0, sourceLen, (int) std::llround(s / gs) * (int) std::llround(gs));
    }

    void applyDrag(int x)
    {
        const int s   = snap(xToSample(x));
        const int sel = juce::jmax(1, trimOut - trimIn);
        switch (dragTarget)
        {
            case 1: trimIn  = juce::jlimit(0, juce::jmax(0, trimOut - 1), s);
                    fadeInLen  = juce::jlimit(0, trimOut - trimIn, fadeInLen);
                    fadeOutLen = juce::jlimit(0, trimOut - trimIn, fadeOutLen);
                    if (onLocatorsChanged) onLocatorsChanged(trimIn, trimOut);
                    if (onFadesChanged)    onFadesChanged(fadeInLen, fadeOutLen);
                    break;
            case 2: trimOut = juce::jlimit(juce::jmin(sourceLen, trimIn + 1), sourceLen, s);
                    fadeInLen  = juce::jlimit(0, trimOut - trimIn, fadeInLen);
                    fadeOutLen = juce::jlimit(0, trimOut - trimIn, fadeOutLen);
                    if (onLocatorsChanged) onLocatorsChanged(trimIn, trimOut);
                    if (onFadesChanged)    onFadesChanged(fadeInLen, fadeOutLen);
                    break;
            case 3: fadeInLen  = juce::jlimit(0, sel - fadeOutLen, s - trimIn);   // don't cross fade-out
                    if (onFadesChanged) onFadesChanged(fadeInLen, fadeOutLen); break;
            case 4: fadeOutLen = juce::jlimit(0, sel - fadeInLen, trimOut - s);   // don't cross fade-in
                    if (onFadesChanged) onFadesChanged(fadeInLen, fadeOutLen); break;
            default: break;
        }
        repaint();
    }

    void drawGrid(juce::Graphics& g, float waveTop)
    {
        const double spb = samplesPerBeat();
        if (spb < 1.0) return;
        const int totalBeats = (int) std::ceil(bars * beatsPerBar);
        for (int beat = 0; beat <= totalBeats; ++beat)
        {
            const int sm = (int) (beat * spb);
            if (sm < viewStart || sm > viewEnd) continue;
            const float x = sampleToX(sm);
            const bool barLine = (beat % beatsPerBar == 0);
            g.setColour(barLine ? juce::Colour(0x33ffffff) : juce::Colour(0x14ffffff));
            g.drawVerticalLine((int) x, waveTop, (float) getHeight());
        }
    }

    void drawRuler(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff10120f));
        g.fillRect(0, 0, getWidth(), kRulerH);
        const double spb = samplesPerBeat();
        if (spb < 1.0) return;

        const int totalBeats = (int) std::ceil(bars * beatsPerBar);
        const float beatPx = sampleToX((int) spb) - sampleToX(0);   // visible width of one beat
        const bool labelEveryBeat = beatPx > 26.0f;
        g.setFont(9.5f);
        for (int beat = 0; beat <= totalBeats; ++beat)
        {
            const int sm = (int) (beat * spb);
            if (sm < viewStart || sm > viewEnd) continue;
            const float x = sampleToX(sm);
            const bool barLine = (beat % beatsPerBar == 0);
            g.setColour(barLine ? juce::Colour(0x99ffd9a4) : juce::Colour(0x44ffffff));
            g.drawVerticalLine((int) x, barLine ? 2.0f : 6.0f, (float) kRulerH);
            if (barLine || labelEveryBeat)
            {
                const int bar = beat / beatsPerBar + 1;
                const int bb  = beat % beatsPerBar + 1;
                g.setColour(barLine ? juce::Colour(0xffd9a441) : juce::Colour(0x88ffffff));
                g.drawText(juce::String(bar) + "." + juce::String(bb),
                           (int) x + 2, 1, 36, kRulerH - 1, juce::Justification::left);
            }
        }
    }

    void drawFadeHandle(juce::Graphics& g, float x, float y)
    {
        g.setColour(juce::Colour(0x557ad1ff));
        g.drawVerticalLine((int) x, y, (float) getHeight());
        g.setColour(juce::Colour(0xff7ad1ff));                  // bigger, easier grab target
        juce::Path d; d.addTriangle(x - 8.0f, y - 3.0f, x + 8.0f, y - 3.0f, x, y + 12.0f);
        g.fillPath(d);
        g.setColour(juce::Colour(0xff0e1416));
        g.fillEllipse(x - 2.0f, y + 1.0f, 4.0f, 4.0f);
    }

    void drawLocator(juce::Graphics& g, float x, float top, juce::Colour c, const juce::String& label)
    {
        g.setColour(c);
        g.drawVerticalLine((int) x, top, (float) getHeight());
        juce::Rectangle<float> tab(x - 9.0f, (float) getHeight() - 15.0f, 18.0f, 15.0f);
        g.fillRect(tab);
        g.setColour(juce::Colour(0xff141a18));
        g.setFont(11.0f);
        g.drawText(label, tab, juce::Justification::centred);
    }

    float sampleToX(int s) const
    {
        const double span = juce::jmax(1, viewEnd - viewStart);
        return (float) ((s - viewStart) / span * getWidth());
    }
    int xToSample(int x) const
    {
        if (getWidth() <= 0) return viewStart;
        const double span = juce::jmax(1, viewEnd - viewStart);
        return juce::jlimit(0, sourceLen, viewStart + (int) (x / (double) getWidth() * span));
    }

    void rebuildPeaks()
    {
        peaks.clear();
        if (source == nullptr || sourceLen <= 0 || getWidth() <= 0) return;
        const int w  = getWidth();
        const int ch = source->getNumChannels();
        const double span = juce::jmax(1, viewEnd - viewStart);
        const double spp  = span / w;     // samples per pixel in the current view
        peaks.resize((size_t) w, 0.0f);
        for (int x = 0; x < w; ++x)
        {
            const int start = viewStart + (int) (x * spp);
            const int end   = juce::jmin(sourceLen, viewStart + (int) ((x + 1) * spp) + 1);
            if (start >= sourceLen) break;
            float pk = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float* d = source->getReadPointer(c);
                for (int i = start; i < end; ++i) pk = juce::jmax(pk, std::abs(d[i]));
            }
            peaks[(size_t) x] = juce::jmin(1.0f, pk);
        }
    }

    static constexpr int kRulerH = 15;

    const juce::AudioBuffer<float>* source = nullptr;
    int    sourceLen  = 0;
    double sampleRate = 44100.0;
    std::vector<float> peaks;

    int trimIn = 0, trimOut = 0;
    int fadeInLen = 0, fadeOutLen = 0;
    int fadeInCurve = 0, fadeOutCurve = 0;
    int viewStart = 0, viewEnd = 0;        // zoom window (samples)
    double bars = 2.0;
    int    beatsPerBar = 4;
    int    snapDiv = 0;                    // 0 Off, 1 1Bar, 2 1/2, 3 1/4, 4 1/8, 5 1/16
    int  dragTarget = 0;                   // 0 none, 1 A, 2 B, 3 fade-in, 4 fade-out
    int  playhead = -1;                    // audition playhead sample (-1 = hidden)
    int  landing  = -1;                    // landing sample (end of rise; -1 = use trim end)
    bool bodyDrag = false, dragging = false;
    juce::String emptyHint { "Drag a source into the lane above,\nthen press Create Swell." };
};
