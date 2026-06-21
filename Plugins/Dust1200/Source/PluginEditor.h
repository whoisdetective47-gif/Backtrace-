#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/SquareKnobLookAndFeel.h"
#include "UI/PitchSliderLookAndFeel.h"
#include "UI/SDColors.h"
#include "UI/CaseFileOverlay.h"
#include "UI/SnapSlider.h"

// ===========================================================================
//  Brass round knob + label + optional sweet-spot tag
// ===========================================================================
class DustKnob : public juce::Component
{
public:
    juce::Slider slider;
    juce::Label  nameLabel;
    juce::String tag;

    DustKnob(const juce::String& name, const juce::String& sweetSpotTag = {})
        : tag(sweetSpotTag)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);

        addAndMakeVisible(nameLabel);
        nameLabel.setText(name, juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
        nameLabel.setColour(juce::Label::textColourId, SDCol::textAmber);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        nameLabel.setBounds(area.removeFromBottom(15));
        if (tag.isNotEmpty()) area.removeFromBottom(11);
        slider.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        if (tag.isNotEmpty())
        {
            g.setColour(SDCol::textDim);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            g.drawText(tag, getLocalBounds().removeFromBottom(25).removeFromTop(11),
                       juce::Justification::centred, false);
        }
    }
};

// ===========================================================================
//  LED toggle button — amber on dark
// ===========================================================================
class LEDButton : public juce::ToggleButton
{
public:
    LEDButton(const juce::String& label) { setButtonText(label); }

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        auto b  = getLocalBounds().toFloat().reduced(1.0f);
        bool on = getToggleState();

        // Body
        g.setColour(on ? SDCol::btnOn : SDCol::btnOff);
        g.fillRoundedRectangle(b, 3.0f);

        // Border
        g.setColour(on ? SDCol::btnOnBorder : SDCol::divider);
        g.drawRoundedRectangle(b, 3.0f, 1.0f);

        // LED dot
        const float dotR = 3.5f;
        juce::Rectangle<float> led(b.getX() + 7.0f, b.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour(on ? SDCol::btnLED : SDCol::textSub);
        g.fillEllipse(led);

        // Text
        g.setColour(on ? SDCol::textGold : SDCol::textAmber);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
        g.drawText(getButtonText(),
                   static_cast<int>(led.getRight()) + 3, 0,
                   getWidth() - static_cast<int>(led.getRight()) - 8, getHeight(),
                   juce::Justification::centredLeft, false);
    }
};

// ===========================================================================
//  Pitch display — amber readout "SPEED: +0.00 st"
// ===========================================================================
class PitchDisplay : public juce::Label, private juce::Timer
{
public:
    PitchDisplay(juce::AudioProcessorValueTreeState& vts,
                 const juce::String& paramId,
                 const juce::String& prefix)
        : apvts(vts), pid(paramId), prf(prefix)
    {
        setJustificationType(juce::Justification::centred);
        setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        setColour(juce::Label::backgroundColourId, SDCol::dispBg);
        setColour(juce::Label::textColourId,       SDCol::textDisplay);
        setColour(juce::Label::outlineColourId,    SDCol::dispBorder);
        startTimerHz(24);
    }
    ~PitchDisplay() override { stopTimer(); }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String pid, prf;
    void timerCallback() override
    {
        float v = *apvts.getRawParameterValue(pid);
        juce::String sign = (v >= 0.0f) ? "+" : "";
        setText(prf + ":  " + sign + juce::String(v, 2) + " st",
                juce::dontSendNotification);
    }
};

// ===========================================================================
//  Output meter (L/R stereo bars)
// ===========================================================================
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    OutputMeter() { startTimerHz(30); }
    ~OutputMeter() override { stopTimer(); }

    void pushLevels(float l, float r)
    {
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        float barW = b.getWidth() * 0.44f, gap = b.getWidth() * 0.12f;

        auto drawBar = [&](float x, float peak)
        {
            g.setColour(SDCol::dispBg);
            g.fillRect(x, b.getY(), barW, b.getHeight());

            float h = juce::jlimit(0.0f, 1.0f,
                                   1.0f + juce::Decibels::gainToDecibels(peak) / 60.0f);
            juce::Colour col = (peak > 0.9f) ? juce::Colour(0xFFDD4422)
                             : (peak > 0.5f) ? juce::Colour(0xFFD08020)
                             : SDCol::textGold;
            g.setColour(col);
            g.fillRect(x, b.getBottom() - h * b.getHeight(), barW, h * b.getHeight());
        };

        drawBar(b.getX(),              peakL);
        drawBar(b.getX() + barW + gap, peakR);
    }

private:
    float peakL = 0.0f, peakR = 0.0f;
    void timerCallback() override { peakL *= 0.84f; peakR *= 0.84f; repaint(); }
};

// ===========================================================================
//  NOISE detail drawer — slide-out overlay opened from the NOISE tab
// ===========================================================================
class NoiseDrawer : public juce::Component
{
public:
    std::function<void()> onClose;

    explicit NoiseDrawer(juce::AudioProcessorValueTreeState& apvts)
    {
        setInterceptsMouseClicks(true, true);

        typeBox.addItemList({ "White", "Pink", "Moog", "ARP", "Vinyl" }, 1);
        typeBox.setColour(juce::ComboBox::backgroundColourId, SDCol::dispBg);
        typeBox.setColour(juce::ComboBox::textColourId,       SDCol::textGold);
        typeBox.setColour(juce::ComboBox::outlineColourId,    SDCol::dispBorder);
        typeBox.setColour(juce::ComboBox::arrowColourId,      SDCol::textAmber);
        addAndMakeVisible(typeBox);
        aType = std::make_unique<ComboBoxAt>(apvts, "noiseType", typeBox);

        for (auto* k : { &kAmount, &kHP, &kLP }) addAndMakeVisible(k);
        aAmount = std::make_unique<SliderAt>(apvts, "noise",   kAmount.slider);
        aHP     = std::make_unique<SliderAt>(apvts, "noiseHP", kHP.slider);
        aLP     = std::make_unique<SliderAt>(apvts, "noiseLP", kLP.slider);

        btnClose.setButtonText("CLOSE");
        btnClose.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF1A1408));
        btnClose.setColour(juce::TextButton::textColourOffId, SDCol::textDim);
        btnClose.onClick = [this] { close(); };
        addAndMakeVisible(btnClose);
    }

    juce::Rectangle<int> panel() const
    {
        const int pw = 560, ph = 250;
        return { (getWidth() - pw) / 2, (getHeight() - ph) / 2, pw, ph };
    }

    void close() { setVisible(false); if (onClose) onClose(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xC0050402));
        g.fillAll();                                   // dimming veil

        auto p = panel();
        juce::ColourGradient grad(SDCol::panelFace, (float)p.getX(), (float)p.getY(),
                                  SDCol::bg,         (float)p.getX(), (float)p.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(p.toFloat(), 6.0f);
        g.setColour(SDCol::knobBrass3.withAlpha(0.5f));
        g.drawRoundedRectangle(p.toFloat().reduced(0.5f), 6.0f, 1.0f);

        g.setColour(SDCol::textGold);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        g.drawText("NOISE", p.getX() + 18, p.getY() + 12, 200, 18, juce::Justification::centredLeft, false);
        g.setColour(SDCol::textSub);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText("CHARACTER NOISE", p.getX() + 18, p.getY() + 30, 260, 12, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider);
        g.fillRect(p.getX() + 16, p.getY() + 46, p.getWidth() - 32, 1);

        g.setColour(SDCol::textDim);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
        g.drawText("TYPE", p.getX() + 18, p.getY() + 56, 80, 12, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto p = panel();
        typeBox .setBounds(p.getX() + 18, p.getY() + 72, 220, 24);
        btnClose.setBounds(p.getRight() - 78, p.getY() + 12, 60, 20);

        const int kW = 92, kH = 96, gap = 18, ky = p.getBottom() - 116;
        int kx = p.getX() + 18;
        kAmount.setBounds(kx, ky, kW, kH); kx += kW + gap;
        kHP    .setBounds(kx, ky, kW, kH); kx += kW + gap;
        kLP    .setBounds(kx, ky, kW, kH);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! panel().contains(e.getPosition())) close();   // click outside to dismiss
    }

private:
    using SliderAt   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::ComboBox   typeBox;
    DustKnob         kAmount { "AMOUNT" }, kHP { "HIGH PASS" }, kLP { "LOW PASS" };
    juce::TextButton btnClose;

    std::unique_ptr<ComboBoxAt> aType;
    std::unique_ptr<SliderAt>   aAmount, aHP, aLP;
};

// ===========================================================================
//  JITTER detail drawer — slide-out overlay opened from the JITTER tab
// ===========================================================================
class JitterDrawer : public juce::Component
{
public:
    std::function<void()> onClose;

    explicit JitterDrawer(juce::AudioProcessorValueTreeState& apvts)
    {
        setInterceptsMouseClicks(true, true);

        for (auto* k : { &kDepth, &kRate, &kTrans, &kBlend }) addAndMakeVisible(k);
        aDepth = std::make_unique<SliderAt>(apvts, "jitter",      kDepth.slider);
        aRate  = std::make_unique<SliderAt>(apvts, "jitterRate",  kRate.slider);
        aTrans = std::make_unique<SliderAt>(apvts, "jitterTrans", kTrans.slider);
        aBlend = std::make_unique<SliderAt>(apvts, "jitterBlend", kBlend.slider);

        btnClose.setButtonText("CLOSE");
        btnClose.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF1A1408));
        btnClose.setColour(juce::TextButton::textColourOffId, SDCol::textDim);
        btnClose.onClick = [this] { close(); };
        addAndMakeVisible(btnClose);
    }

    juce::Rectangle<int> panel() const
    {
        const int pw = 560, ph = 230;
        return { (getWidth() - pw) / 2, (getHeight() - ph) / 2, pw, ph };
    }

    void close() { setVisible(false); if (onClose) onClose(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xC0050402));
        g.fillAll();

        auto p = panel();
        juce::ColourGradient grad(SDCol::panelFace, (float)p.getX(), (float)p.getY(),
                                  SDCol::bg,         (float)p.getX(), (float)p.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(p.toFloat(), 6.0f);
        g.setColour(SDCol::knobBrass3.withAlpha(0.5f));
        g.drawRoundedRectangle(p.toFloat().reduced(0.5f), 6.0f, 1.0f);

        g.setColour(SDCol::textGold);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        g.drawText("JITTER", p.getX() + 18, p.getY() + 12, 200, 18, juce::Justification::centredLeft, false);
        g.setColour(SDCol::textSub);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText("UNSTABLE SAMPLE-CLOCK", p.getX() + 18, p.getY() + 30, 280, 12, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider);
        g.fillRect(p.getX() + 16, p.getY() + 46, p.getWidth() - 32, 1);
    }

    void resized() override
    {
        auto p = panel();
        btnClose.setBounds(p.getRight() - 78, p.getY() + 12, 60, 20);

        const int kW = 116, kH = 96, gap = 8, ky = p.getBottom() - 116;
        int kx = p.getX() + 18;
        kDepth.setBounds(kx, ky, kW, kH); kx += kW + gap;
        kRate .setBounds(kx, ky, kW, kH); kx += kW + gap;
        kTrans.setBounds(kx, ky, kW, kH); kx += kW + gap;
        kBlend.setBounds(kx, ky, kW, kH);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! panel().contains(e.getPosition())) close();
    }

private:
    using SliderAt = juce::AudioProcessorValueTreeState::SliderAttachment;

    DustKnob kDepth { "DEPTH" }, kRate { "RATE" }, kTrans { "TRANSIENT" }, kBlend { "BLEND" };
    juce::TextButton btnClose;
    std::unique_ptr<SliderAt> aDepth, aRate, aTrans, aBlend;
};

#if DUST_VAULT_ENABLED
// ---------------------------------------------------------------------------
//  Waveform view with draggable start/end trim locators
// ---------------------------------------------------------------------------
class WaveformView : public juce::Component
{
public:
    std::function<void()> onTrimChanged;
    float start01 = 0.0f, end01 = 1.0f;

    void setBuffer(const juce::AudioBuffer<float>* b, int len)
    {
        buf = b; validLen = len; start01 = 0.0f; end01 = 1.0f; repaint();
    }
    void clearBuffer() { buf = nullptr; validLen = 0; repaint(); }
    bool hasAudio() const { return buf != nullptr && validLen > 0; }

    void paint(juce::Graphics& g) override
    {
        auto rf = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF0E0B07));
        g.fillRoundedRectangle(rf, 4.0f);
        g.setColour(SDCol::dispBorder);
        g.drawRoundedRectangle(rf.reduced(0.5f), 4.0f, 1.0f);

        const int W = getWidth(), H = getHeight();
        const float midY = H * 0.5f;

        if (! hasAudio())
        {
            g.setColour(SDCol::textSub);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
            g.drawText("no capture selected", getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        const float* L = buf->getReadPointer(0);
        const float* R = buf->getNumChannels() > 1 ? buf->getReadPointer(1) : L;

        g.setColour(SDCol::textAmber.withAlpha(0.85f));
        for (int x = 0; x < W; ++x)
        {
            int s0 = (int)((double)x       / W * validLen);
            int s1 = (int)((double)(x + 1) / W * validLen);
            if (s1 <= s0) s1 = s0 + 1;
            if (s1 > validLen) s1 = validLen;
            float mn = 0.0f, mx = 0.0f;
            for (int s = s0; s < s1; ++s)
            {
                float v = (L[s] + R[s]) * 0.5f;
                mn = juce::jmin(mn, v); mx = juce::jmax(mx, v);
            }
            g.drawVerticalLine(x, midY - mx * midY * 0.95f, midY - mn * midY * 0.95f);
        }

        const float sx = start01 * W, ex = end01 * W;
        g.setColour(juce::Colour(0x88000000));                 // dim outside region
        g.fillRect(0.0f, 0.0f, sx, (float)H);
        g.fillRect(ex, 0.0f, (float)W - ex, (float)H);

        g.setColour(SDCol::textGold);                          // locators
        g.fillRect(sx - 1.0f, 0.0f, 2.0f, (float)H);
        g.fillRect(ex - 1.0f, 0.0f, 2.0f, (float)H);
        g.fillRect(sx - 3.0f, 0.0f, 6.0f, 6.0f);
        g.fillRect(ex - 3.0f, (float)H - 6.0f, 6.0f, 6.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        float mx = (float)e.x / (float)juce::jmax(1, getWidth());
        dragging = (std::abs(mx - start01) <= std::abs(mx - end01)) ? 0 : 1;
        drag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e); }

private:
    const juce::AudioBuffer<float>* buf = nullptr;
    int   validLen = 0;
    int   dragging = -1;

    void drag(const juce::MouseEvent& e)
    {
        if (! hasAudio()) return;
        float mx = juce::jlimit(0.0f, 1.0f, (float)e.x / (float)juce::jmax(1, getWidth()));
        const float gap = 0.005f;
        if (dragging == 0) start01 = juce::jmin(mx, end01 - gap);
        else               end01   = juce::jmax(mx, start01 + gap);
        start01 = juce::jlimit(0.0f, 1.0f, start01);
        end01   = juce::jlimit(0.0f, 1.0f, end01);
        if (onTrimChanged) onTrimChanged();
        repaint();
    }
};

// ===========================================================================
//  DUST VAULT drawer — capture / review / trim / export ("evidence locker")
// ===========================================================================
class VaultDrawer : public juce::Component,
                    public juce::ListBoxModel,
                    private juce::Timer
{
public:
    std::function<void()> onClose;

    explicit VaultDrawer(Dust1200Processor& proc) : processor(proc)
    {
        setInterceptsMouseClicks(true, true);
        formatManager.registerBasicFormats();

        auto styleBtn = [this](juce::TextButton& b, const juce::String& txt,
                               juce::Colour bg, juce::Colour tx)
        {
            b.setButtonText(txt);
            b.setColour(juce::TextButton::buttonColourId,  bg);
            b.setColour(juce::TextButton::textColourOffId, tx);
            addAndMakeVisible(b);
        };
        styleBtn(btnCapture, "CAPTURE", juce::Colour(0xFF3A1010), juce::Colour(0xFFE08070));
        styleBtn(btnStop,    "STOP",    juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnPlay,    "PLAY",    juce::Colour(0xFF14240E), juce::Colour(0xFF90C070));
        styleBtn(btnStopPlay,"\xe2\x96\xa0", juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnAutoTrim,"AUTO TRIM",  juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnResetTrim,"RESET",     juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnExport,  "EXPORT",  juce::Colour(0xFF1A1408), SDCol::textAmber);
        styleBtn(btnFolder,  "FOLDER",  juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnClear,   "CLEAR",   juce::Colour(0xFF1A1408), SDCol::textDim);
        styleBtn(btnClose,   "CLOSE",   juce::Colour(0xFF1A1408), SDCol::textDim);

        btnCapture .onClick = [this] { processor.vaultStartCapture(); capturing = true; repaint(); };
        btnStop    .onClick = [this] { processor.vaultStopCapture(); };
        btnPlay    .onClick = [this] { playRegion(); };
        btnStopPlay.onClick = [this] { processor.vaultStopPlayback(); };
        btnAutoTrim.onClick = [this] { autoTrim(); };
        btnResetTrim.onClick= [this] { wave.start01 = 0.0f; wave.end01 = 1.0f; wave.repaint(); };
        btnExport  .onClick = [this] { auto f = exportTrimmed(); if (f.existsAsFile()) refreshAfterExport(f); };
        btnFolder  .onClick = [this] { processor.vaultOpenFolder(); };
        btnClear   .onClick = [this] { clearSelected(); };
        btnClose   .onClick = [this] { close(); };

        nameField.setColour(juce::TextEditor::backgroundColourId, SDCol::dispBg);
        nameField.setColour(juce::TextEditor::textColourId,       SDCol::textGold);
        nameField.setColour(juce::TextEditor::outlineColourId,    SDCol::dispBorder);
        nameField.setTextToShowWhenEmpty("name this capture...", SDCol::textSub);
        nameField.onReturnKey = [this] { renameSelected(); };
        addAndMakeVisible(nameField);

        list.setModel(this);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF0E0B07));
        list.setRowHeight(20);
        addAndMakeVisible(list);

        addAndMakeVisible(wave);

        dragZone.getFile = [this]() -> juce::File
        {
            auto& caps = processor.vaultGetCaptures();
            int rr = list.getSelectedRow();
            if (rr < 0 || rr >= (int)caps.size()) return {};
            // full range → drag the raw file; trimmed → export the region first
            if (wave.start01 <= 0.001f && wave.end01 >= 0.999f) return caps[(size_t)rr].file;
            return exportTrimmed();
        };
        addAndMakeVisible(dragZone);

        startTimerHz(20);
    }

    ~VaultDrawer() override { stopTimer(); list.setModel(nullptr); }

    juce::Rectangle<int> panel() const
    {
        const int pw = 640, ph = 440;
        return { (getWidth() - pw) / 2, (getHeight() - ph) / 2, pw, ph };
    }

    void close() { processor.vaultStopPlayback(); setVisible(false); if (onClose) onClose(); }

    // ---- ListBoxModel ----
    int getNumRows() override { return (int) processor.vaultGetCaptures().size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        auto& caps = processor.vaultGetCaptures();
        if (row < 0 || row >= (int)caps.size()) return;
        if (sel) { g.setColour(SDCol::btnOn); g.fillRect(0, 0, w, h); }
        g.setColour(sel ? SDCol::textGold : SDCol::textDim);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
        g.drawText(caps[(size_t)row].name, 8, 0, w - 70, h, juce::Justification::centredLeft, true);
        g.setColour(SDCol::textSub);
        g.drawText(juce::String(caps[(size_t)row].seconds, 1) + "s",
                   w - 60, 0, 52, h, juce::Justification::centredRight, false);
    }

    void selectedRowsChanged(int) override { loadSelected(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xC0050402));
        g.fillAll();

        auto p = panel();
        juce::ColourGradient grad(SDCol::panelFace, (float)p.getX(), (float)p.getY(),
                                  SDCol::bg,         (float)p.getX(), (float)p.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(p.toFloat(), 6.0f);
        g.setColour(SDCol::knobBrass3.withAlpha(0.5f));
        g.drawRoundedRectangle(p.toFloat().reduced(0.5f), 6.0f, 1.0f);

        g.setColour(SDCol::textGold);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        g.drawText("DUST VAULT", p.getX() + 18, p.getY() + 12, 240, 18, juce::Justification::centredLeft, false);
        g.setColour(SDCol::textSub);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText("EVIDENCE LOCKER", p.getX() + 18, p.getY() + 30, 240, 12, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider);
        g.fillRect(p.getX() + 16, p.getY() + 46, p.getWidth() - 32, 1);

        const bool rec = processor.vaultIsCapturing();
        const float lx = (float)p.getRight() - 156.0f, ly = (float)p.getY() + 24.0f;
        g.setColour(rec ? juce::Colour(0xFFE03020) : juce::Colour(0xFF402018));
        g.fillEllipse(lx, ly - 5.0f, 10.0f, 10.0f);
        g.setColour(rec ? juce::Colour(0xFFE08070) : SDCol::textSub);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        g.drawText(rec ? "REC " + juce::String(processor.vaultElapsed(), 1) + "s" : "READY",
                   (int)lx + 16, (int)p.getY() + 14, 140, 18, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto p = panel();
        const int x = p.getX() + 18, r = p.getRight() - 18;

        btnCapture.setBounds(x,       p.getY() + 58, 96, 24);
        btnStop   .setBounds(x + 104, p.getY() + 58, 70, 24);

        list.setBounds(x, p.getY() + 90, r - x, 84);
        wave.setBounds(x, p.getY() + 182, r - x, 92);

        btnPlay    .setBounds(x,       p.getY() + 284, 70, 24);
        btnStopPlay.setBounds(x + 78,  p.getY() + 284, 36, 24);
        btnAutoTrim.setBounds(x + 130, p.getY() + 284, 96, 24);
        btnResetTrim.setBounds(x + 234,p.getY() + 284, 76, 24);

        nameField.setBounds(x, p.getY() + 318, r - x, 24);

        dragZone .setBounds(x,       p.getBottom() - 72, 150, 50);
        btnExport.setBounds(x + 162, p.getBottom() - 60, 80, 24);
        btnFolder.setBounds(x + 250, p.getBottom() - 60, 84, 24);
        btnClear .setBounds(x + 342, p.getBottom() - 60, 64, 24);
        btnClose .setBounds(r - 64,  p.getBottom() - 60, 64, 24);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! panel().contains(e.getPosition())) close();
    }

private:
    struct DragZone : juce::Component
    {
        std::function<juce::File()> getFile;
        bool started = false;
        void paint(juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(juce::Colour(0xFF161009));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(SDCol::knobBrass3.withAlpha(0.5f));
            g.drawRoundedRectangle(b, 4.0f, 1.0f);
            g.setColour(SDCol::textAmber);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
            g.drawText("DRAG TO DAW", getLocalBounds(), juce::Justification::centred, false);
        }
        void mouseDown(const juce::MouseEvent&) override { started = false; }
        void mouseDrag(const juce::MouseEvent&) override
        {
            if (started || ! getFile) return;
            started = true;
            auto f = getFile();
            if (f.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles(
                    { f.getFullPathName() }, false, this);
        }
    };

    void timerCallback() override
    {
        if (capturing && ! processor.vaultIsCapturing())   // stopped (manual or 60 s max)
        {
            capturing = false;
            auto f = processor.vaultFinalizeCapture();
            list.updateContent();
            if (f.existsAsFile()) { list.selectRow(0); loadSelected(); }
        }
        if (processor.vaultIsCapturing()) repaint();
    }

    void loadSelected()
    {
        auto& caps = processor.vaultGetCaptures();
        int rr = list.getSelectedRow();
        if (rr < 0 || rr >= (int)caps.size()) { editLen = 0; wave.clearBuffer(); return; }

        nameField.setText(caps[(size_t)rr].name, juce::dontSendNotification);

        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(caps[(size_t)rr].file));
        if (reader == nullptr) { editLen = 0; wave.clearBuffer(); return; }

        editLen      = (int) reader->lengthInSamples;
        editSampleRate = reader->sampleRate;
        editBuffer.setSize((int) juce::jmax((juce::uint32)1, reader->numChannels), juce::jmax(1, editLen));
        reader->read(&editBuffer, 0, editLen, 0, true, true);
        wave.setBuffer(&editBuffer, editLen);
    }

    void playRegion()
    {
        if (editLen <= 0) return;
        int s = (int)(wave.start01 * editLen);
        int e = (int)(wave.end01   * editLen);
        processor.vaultStartPlayback(editBuffer, s, e);
    }

    void autoTrim()
    {
        if (editLen <= 0) return;
        const float thr = 0.0025f;
        const float* L = editBuffer.getReadPointer(0);
        const float* R = editBuffer.getNumChannels() > 1 ? editBuffer.getReadPointer(1) : L;
        int s = 0, e = editLen - 1;
        while (s < editLen && std::abs((L[s] + R[s]) * 0.5f) < thr) ++s;
        while (e > s       && std::abs((L[e] + R[e]) * 0.5f) < thr) --e;
        wave.start01 = juce::jlimit(0.0f, 1.0f, (float)s / (float)editLen);
        wave.end01   = juce::jlimit(0.0f, 1.0f, (float)(e + 1) / (float)editLen);
        wave.repaint();
    }

    juce::File exportTrimmed()
    {
        if (editLen <= 0) return {};
        int s = juce::jlimit(0, editLen, (int)(wave.start01 * editLen));
        int e = juce::jlimit(0, editLen, (int)(wave.end01   * editLen));
        const int len = e - s;
        if (len <= 0) return {};

        auto dir = processor.vaultCapturesDir();
        dir.createDirectory();
        juce::String base = nameField.getText().trim();
        if (base.isEmpty()) base = "DustVault_Trim";
        base = juce::File::createLegalFileName(base);
        juce::File f = dir.getChildFile(base + ".wav");
        if (f.existsAsFile()) f = f.getNonexistentSibling();

        if (auto os = f.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(os.get(), editSampleRate > 0 ? editSampleRate : 44100.0, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                os.release();
                juce::AudioBuffer<float> region(2, len);
                for (int ch = 0; ch < 2; ++ch)
                    region.copyFrom(ch, 0, editBuffer,
                                    juce::jmin(ch, editBuffer.getNumChannels() - 1), s, len);
                writer->writeFromAudioSampleBuffer(region, 0, len);
                writer.reset();
                return f;
            }
        }
        return {};
    }

    void refreshAfterExport(const juce::File& f)
    {
        Dust1200Processor::VaultCapture vc { f, f.getFileNameWithoutExtension(),
                                             (double)0.0 };
        // duration from buffer length / sr
        vc.seconds = (double)(editBuffer.getNumSamples()) / juce::jmax(1.0, editSampleRate);
        processor.vaultGetCaptures().insert(processor.vaultGetCaptures().begin(), vc);
        list.updateContent();
        list.selectRow(0);
        loadSelected();
    }

    void renameSelected()
    {
        auto& caps = processor.vaultGetCaptures();
        int rr = list.getSelectedRow();
        if (rr < 0 || rr >= (int)caps.size()) return;
        auto t = nameField.getText().trim();
        if (t.isEmpty()) return;
        auto dir = caps[(size_t)rr].file.getParentDirectory();
        juce::File nf = dir.getChildFile(juce::File::createLegalFileName(t) + ".wav");
        if (nf == caps[(size_t)rr].file) return;
        if (nf.existsAsFile()) nf = nf.getNonexistentSibling();
        if (caps[(size_t)rr].file.moveFileTo(nf))
        {
            caps[(size_t)rr].file = nf;
            caps[(size_t)rr].name = nf.getFileNameWithoutExtension();
            list.updateContent(); list.repaint();
        }
    }

    void clearSelected()
    {
        auto& caps = processor.vaultGetCaptures();
        int rr = list.getSelectedRow();
        if (rr >= 0 && rr < (int)caps.size())   // removes from list only; file stays on disk
        {
            caps.erase(caps.begin() + rr);
            list.updateContent();
            editLen = 0; wave.clearBuffer();
        }
    }

    Dust1200Processor& processor;
    bool   capturing = false;

    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> editBuffer;
    int    editLen = 0;
    double editSampleRate = 44100.0;

    juce::TextButton btnCapture, btnStop, btnPlay, btnStopPlay,
                     btnAutoTrim, btnResetTrim, btnExport, btnFolder, btnClear, btnClose;
    juce::TextEditor nameField;
    juce::ListBox    list;
    WaveformView     wave;
    DragZone         dragZone;
};
#endif // DUST_VAULT_ENABLED

// ===========================================================================
//  Main editor
// ===========================================================================
class Dust1200Editor : public juce::AudioProcessorEditor
{
public:
    explicit Dust1200Editor(Dust1200Processor&);
    ~Dust1200Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    Dust1200Processor& processor;
    SquareKnobLAF  sqLAF;
    PitchSliderLAF pitchLAF;

    // ---- Clickable tab strip + detail drawers ----
    struct TabItem { juce::String name; int width; juce::Rectangle<int> rect; };
    std::vector<TabItem> tabs { { "SAMPLER", 94, {} }, { "NOISE", 64, {} }, { "JITTER", 68, {} }
#if DUST_VAULT_ENABLED
        , { "VAULT", 62, {} }
#endif
    };
    juce::String activeTab { "SAMPLER" };
    void layoutTabs();
    std::unique_ptr<NoiseDrawer>  noiseDrawer;
    std::unique_ptr<JitterDrawer> jitterDrawer;
#if DUST_VAULT_ENABLED
    std::unique_ptr<VaultDrawer>  vaultDrawer;
#endif

    juce::Image logoImage;

    // ---- SAMPLE section ----
    DustKnob kBitDepth  { "BIT DEPTH",   "12-bit" };
    DustKnob kSampleRate{ "SAMPLE RATE", "26.04k" };
    juce::TextButton btnClassic { "12 / 26k" };

    // ---- PITCH section ----
    SnapSlider speedSlider;
    SnapSlider pitchSlider;
    PitchDisplay speedDisplay;
    PitchDisplay pitchDisplay;
    juce::Label  speedSubLabel;
    juce::Label  pitchSubLabel;
    DustKnob     kSpeedGlide { "SPD GLIDE" };
    DustKnob     kPitchGlide { "PCH GLIDE" };
    LEDButton    btnSnap        { "SNAP" };
    LEDButton    btnPitchMusical{ "MUSICAL" };
    LEDButton    btnSpeedGrid   { "GRID" };
    LEDButton    btnSpeedMusical{ "MUS" };
    juce::TextButton btnSpeedReset { "0" };   // snap SAMPLER SPEED back to 0
    juce::TextButton btnPitchReset { "0" };   // snap PITCH back to 0
    juce::TextButton btnManual  { "MANUAL" };
    LEDButton    btnSampLink { "S.LINK" };
    LEDButton    btnSPLink   { "S+P LINK" };

    // ---- DUST section ----
    DustKnob kDrive  { "DRIVE" };
    DustKnob kCrunch { "CRUNCH" };
    DustKnob kJitter { "JITTER" };
    DustKnob kNoise  { "NOISE" };

    // ---- SHAPE / GATE section ----
    DustKnob kHPF       { "HIGH PASS" };
    DustKnob kLPF       { "LOW PASS" };
    DustKnob kTone      { "TONE" };
    DustKnob kGateThresh{ "THRESHOLD" };
    DustKnob kGateRel   { "RELEASE" };
    LEDButton btnGateNoiseSC { "NOISE SC" };

    // ---- MACHINE DRIFT section ----
    DustKnob kDrift  { "DRIFT" };
    DustKnob kMotion { "MOTION" };
    DustKnob kStereo { "STEREO" };

    // ---- LEVEL section ----
    DustKnob kMix    { "MIX" };
    DustKnob kOutput { "OUTPUT" };

    // ---- Top bar ----
    juce::ComboBox presetBox;
    juce::Label    lblPreset;
    OutputMeter    meter;
    juce::Label    versionLabel;
    LEDButton      btnDelta { "DELTA" };

    // APVTS attachments
    using SliderAt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAt> aBitDepth, aSampleRate, aDrive, aCrunch;
    std::unique_ptr<SliderAt> aSpeed, aPitch, aSpeedGlide, aPitchGlide;
    std::unique_ptr<SliderAt> aHPF, aLPF, aTone, aJitter, aMix, aOutput, aNoise;
    std::unique_ptr<SliderAt> aGateThresh, aGateRel;
    std::unique_ptr<ButtonAt> aGateNoiseSC, aDeltaMode;
    std::unique_ptr<SliderAt> aDrift, aMotion, aStereo;
    std::unique_ptr<ButtonAt> aSnap, aSampLink, aSPLink;

    void tryLoadLogo();

    // Helpers
    void drawSectionPanel(juce::Graphics&, juce::Rectangle<int> area, bool accent = false);
    void drawSectionHeader(juce::Graphics&, const juce::String& text, int x, int y, int w,
                           bool bright = false);

    std::unique_ptr<CaseFileOverlay> caseFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dust1200Editor)
};
