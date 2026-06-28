#pragma once
#include <JuceHeader.h>

// =============================================================================
//  Peak-decimated waveform view with draggable trim locators (Phase 2).
//
//  Displays the captured Vault buffer and lets the user frame a selection with
//  A (left) and B (right) handles. The trimmed region is highlighted, the
//  trimmed-off tails dimmed. Reports locator changes via onLocatorsChanged so
//  the processor can store them for the reverse/print stages (Phase 3+).
//
//  Reads the capture buffer on the message thread after capture has stopped —
//  never while the audio thread is writing.
// =============================================================================
class WaveformCanvas : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    // Called with (trimIn, trimOut) in samples whenever the selection changes.
    std::function<void(int, int)> onLocatorsChanged;
    // Called with the Source End / Tail Start offset (samples FROM the trim start).
    std::function<void(int)> onTailStartChanged;
    // Called when an audio file is dropped onto the source lane (primary import path).
    std::function<void(const juce::StringArray&)> onFilesDropped;

    static bool isAudioFile(const juce::String& f)
    {
        return f.endsWithIgnoreCase(".wav")  || f.endsWithIgnoreCase(".aif")
            || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac")
            || f.endsWithIgnoreCase(".mp3")  || f.endsWithIgnoreCase(".m4a")
            || f.endsWithIgnoreCase(".ogg")  || f.endsWithIgnoreCase(".caf");
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile(f)) return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true;  repaint(); }
    void fileDragExit (const juce::StringArray&)            override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHover = false; repaint();
        if (onFilesDropped) onFilesDropped(files);
    }
    void setEmptyHint(const juce::String& h) { emptyHint = h; if (peaks.empty()) repaint(); }

    void setCapture(const juce::AudioBuffer<float>* buf, int length, double sr)
    {
        if (buf == nullptr || length <= 0 || buf->getNumSamples() < length) { clearCapture(); return; }
        source     = buf;
        sourceLen  = length;
        sampleRate = sr;
        trimIn     = 0;
        trimOut    = juce::jmax(0, length);
        tailStart  = trimOut;                 // Tail Start defaults to the source end
        rebuildPeaks();                        // always rebuild on a new source (content may have changed)
        if (onLocatorsChanged) onLocatorsChanged(trimIn, trimOut);
        repaint();
    }

    void clearCapture()
    {
        source = nullptr; sourceLen = 0; trimIn = trimOut = tailStart = 0;
        peaks.clear(); valid = false; lastWidth = -1; repaint();
    }

    void setPlayhead(int sample) { if (sample != playhead) { playhead = sample; repaint(); } }

    // Sync the Source End / Tail Start marker from the processor (absolute source sample).
    void setTailStart(int absSample)
    {
        const int v = juce::jlimit(trimIn, trimOut, absSample);
        if (v != tailStart) { tailStart = v; repaint(); }
    }

    int getTrimIn()  const { return trimIn; }
    int getTrimOut() const { return trimOut; }

    void resized() override { if (getWidth() != lastWidth) rebuildPeaks(); }   // skip redundant rebuilds

    void paint(juce::Graphics& g) override
    {
        auto rf = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff14171a));
        g.fillRoundedRectangle(rf, 6.0f);

        if (dropHover)   // highlight while an audio file hovers over the lane
        {
            g.setColour(juce::Colour(0x333aa0dd));
            g.fillRoundedRectangle(rf, 6.0f);
            g.setColour(juce::Colour(0xff3aa0dd));
            g.drawRoundedRectangle(rf.reduced(1.0f), 6.0f, 2.0f);
        }

        if (! valid || peaks.empty())
        {
            g.setColour(juce::Colour(0x18ffffff));
            g.drawHorizontalLine((int) rf.getCentreY(), rf.getX(), rf.getRight());
            g.setColour(juce::Colour(0xaa3aa0dd));
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawFittedText(emptyHint, getLocalBounds().reduced(20), juce::Justification::centred, 3);
            return;
        }

        const float xa = sampleToX(trimIn);
        const float xb = sampleToX(trimOut);

        // selection highlight
        g.setColour(juce::Colour(0x223aa0dd));
        g.fillRect(juce::Rectangle<float>(xa, rf.getY(), xb - xa, rf.getHeight()));

        // centre line
        g.setColour(juce::Colour(0x18ffffff));
        g.drawHorizontalLine((int) rf.getCentreY(), rf.getX(), rf.getRight());

        // waveform — bright inside the selection, dim outside
        const float midY  = rf.getCentreY();
        const float scale = rf.getHeight() * 0.46f;
        for (size_t x = 0; x < peaks.size(); ++x)
        {
            const bool inSel = ((float) x >= xa && (float) x <= xb);
            g.setColour(inSel ? juce::Colour(0xff3aa0dd) : juce::Colour(0x402a6f96));
            const float h = peaks[x] * scale;
            g.drawVerticalLine((int) x, midY - h, midY + h);
        }

        if (playhead >= 0 && playhead <= sourceLen)         // audition playhead
        {
            const float px = sampleToX(playhead);
            g.setColour(juce::Colour(0xffffffff));
            g.fillRect(px - 0.5f, rf.getY(), 1.5f, rf.getHeight());
        }

        drawHandle(g, xa, juce::Colour(0xff3ad17a), "A");   // left  = green
        drawHandle(g, xb, juce::Colour(0xffe2a44a), "B");   // right = amber

        // Source End / Tail Start marker — everything left of this is the source, the
        // generated FX tail begins here, and the reverse swell is built from that tail.
        // Drawn as a dashed warm-red line with a bottom tab (so it doesn't collide with
        // the A/B tabs at the top) showing the position in ms.
        {
            const float xt = sampleToX(tailStart);
            const juce::Colour tc(0xffd1593a);
            g.setColour(tc.withAlpha(0.85f));
            for (float y = rf.getY(); y < rf.getBottom(); y += 7.0f)
                g.fillRect(xt - 0.75f, y, 1.5f, 4.0f);

            juce::Rectangle<float> tab(xt - 33.0f, rf.getBottom() - 15.0f, 66.0f, 15.0f);
            if (tab.getRight() > rf.getRight()) tab.setX(rf.getRight() - tab.getWidth());
            if (tab.getX()     < rf.getX())     tab.setX(rf.getX());
            g.setColour(tc);
            g.fillRect(tab);
            g.setColour(juce::Colour(0xff14171a));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            const int ms = (int) ((double) (tailStart - trimIn) / juce::jmax(1.0, sampleRate) * 1000.0);
            g.drawText("TAIL " + juce::String(ms) + " ms", tab, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (peaks.empty()) return;
        const int xa = (int) sampleToX(trimIn);
        const int xb = (int) sampleToX(trimOut);
        const int xt = (int) sampleToX(tailStart);
        const int da = std::abs(e.x - xa), db = std::abs(e.x - xb), dt = std::abs(e.x - xt);
        const int grab = 12;

        // The Tail Start handle lives along the BOTTOM (tab there), so a low-ish click
        // near its line grabs it even when it sits on top of the B locator by default.
        if (dt <= grab && e.y > getHeight() * 0.5f) dragTarget = 3;
        else if (da <= grab && da <= db)            dragTarget = 1;
        else if (db <= grab)                        dragTarget = 2;
        else                                        dragTarget = (da < db) ? 1 : 2;   // grab nearest

        applyDrag(e.x);
    }

    void mouseDrag(const juce::MouseEvent& e) override { if (dragTarget != 0) applyDrag(e.x); }
    void mouseUp(const juce::MouseEvent&) override     { dragTarget = 0; }

private:
    void applyDrag(int x)
    {
        const int s = xToSample(x);
        if (dragTarget == 3)   // Source End / Tail Start — report offset from the trim start
        {
            tailStart = juce::jlimit(trimIn, trimOut, s);
            if (onTailStartChanged) onTailStartChanged(tailStart - trimIn);
            repaint();
            return;
        }
        if (dragTarget == 1)      trimIn  = juce::jlimit(0, juce::jmax(0, trimOut - 1), s);
        else if (dragTarget == 2) trimOut = juce::jlimit(juce::jmin(sourceLen, trimIn + 1), sourceLen, s);

        tailStart = trimOut;   // trim defines the source → Tail Start re-defaults to source end
        if (onLocatorsChanged) onLocatorsChanged(trimIn, trimOut);
        repaint();
    }

    void drawHandle(juce::Graphics& g, float x, juce::Colour c, const juce::String& label)
    {
        auto rf = getLocalBounds().toFloat();
        g.setColour(c);
        g.drawVerticalLine((int) x, rf.getY(), rf.getBottom());

        juce::Rectangle<float> tab(x - 9.0f, rf.getY(), 18.0f, 15.0f);
        g.fillRect(tab);
        g.setColour(juce::Colour(0xff14171a));
        g.setFont(11.0f);
        g.drawText(label, tab, juce::Justification::centred);
    }

    float sampleToX(int s) const
    {
        if (sourceLen <= 0) return 0.0f;
        return (float) s / (float) sourceLen * (float) getWidth();
    }

    int xToSample(int x) const
    {
        if (getWidth() <= 0) return 0;
        return juce::jlimit(0, sourceLen, (int) ((float) x / (float) getWidth() * (float) sourceLen));
    }

    void rebuildPeaks()
    {
        peaks.clear();
        valid = false;
        lastWidth = getWidth();
        if (source == nullptr || sourceLen <= 0 || getWidth() <= 0
            || source->getNumSamples() < sourceLen || source->getNumChannels() <= 0) return;

        const int w   = getWidth();
        const int ch  = source->getNumChannels();
        const int per = juce::jmax(1, sourceLen / w);
        peaks.resize((size_t) w, 0.0f);

        for (int x = 0; x < w; ++x)
        {
            const int start = x * per;
            const int end   = juce::jmin(sourceLen, start + per);
            if (start >= sourceLen) break;

            float pk = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float* d = source->getReadPointer(c);
                for (int i = start; i < end; ++i)
                    pk = juce::jmax(pk, std::abs(d[i]));
            }
            peaks[(size_t) x] = juce::jmin(1.0f, pk);
        }
        valid = true;   // peaks reflect a real, in-bounds source buffer
    }

    const juce::AudioBuffer<float>* source = nullptr;
    int    sourceLen  = 0;
    double sampleRate = 44100.0;
    std::vector<float> peaks;

    int trimIn = 0, trimOut = 0;
    int tailStart = 0;    // Source End / Tail Start (absolute source sample)
    int dragTarget = 0;   // 0 none, 1 = A, 2 = B, 3 = Tail Start
    int playhead = -1;    // audition playhead sample (-1 = hidden)
    bool valid = false;   // peaks reflect a real in-bounds buffer (else show the empty hint)
    int  lastWidth = -1;  // skip rebuildPeaks() on resize when width is unchanged
    bool dropHover = false;
    juce::String emptyHint { "Drag a word, hit, or phrase here.\nThen choose Swell Length and press Create Swell." };
};
