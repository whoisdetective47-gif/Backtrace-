#include "PluginEditor.h"

// ===========================================================================
//  Constructor
// ===========================================================================
Dust1200Editor::Dust1200Editor(Dust1200Processor& p)
    : juce::AudioProcessorEditor(&p), processor(p),
      speedDisplay(p.apvts, "samplerSpeed", "SPEED"),
      pitchDisplay(p.apvts, "modernPitch",  "PITCH")
{
    setLookAndFeel(&sqLAF);
    setSize(900, 548);

    tryLoadLogo();

    // ---- Preset label + box ----
    lblPreset.setText("PRESET", juce::dontSendNotification);
    lblPreset.setJustificationType(juce::Justification::centredRight);
    lblPreset.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    lblPreset.setColour(juce::Label::textColourId, SDCol::textDim);
    addAndMakeVisible(lblPreset);

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem(p.getProgramName(i), i + 1);
    presetBox.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [&] { processor.setCurrentProgram(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetBox);

    versionLabel.setText("v2.1", juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setColour(juce::Label::textColourId, SDCol::textSub);
    versionLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain));
    addAndMakeVisible(versionLabel);

    addAndMakeVisible(meter);
    addAndMakeVisible(btnDelta);

    // ---- Detail drawers (hidden until their tab is clicked) ----
    noiseDrawer = std::make_unique<NoiseDrawer>(p.apvts);
    noiseDrawer->onClose = [this] { activeTab = "SAMPLER"; repaint(); };
    addChildComponent(*noiseDrawer);
    noiseDrawer->setBounds(getLocalBounds());

    jitterDrawer = std::make_unique<JitterDrawer>(p.apvts);
    jitterDrawer->onClose = [this] { activeTab = "SAMPLER"; repaint(); };
    addChildComponent(*jitterDrawer);
    jitterDrawer->setBounds(getLocalBounds());

#if DUST_VAULT_ENABLED
    vaultDrawer = std::make_unique<VaultDrawer>(p);
    vaultDrawer->onClose = [this] { activeTab = "SAMPLER"; repaint(); };
    addChildComponent(*vaultDrawer);
    vaultDrawer->setBounds(getLocalBounds());
#endif

    // ---- Speed slider — VERTICAL ----
    speedSlider.setLookAndFeel(&pitchLAF);
    speedSlider.setSliderStyle(juce::Slider::LinearVertical);
    speedSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // Velocity drag: ~20% slower / finer, and the handle nudges from its current
    // position instead of jumping to the click. Hold Cmd/Ctrl/Alt for absolute.
    speedSlider.setVelocityBasedMode(true);
    speedSlider.setVelocityModeParameters(1.22, 1, 0.0, true);
    speedSlider.setDoubleClickReturnValue(true, 0.0);   // double-click → 0
    addAndMakeVisible(speedSlider);
    addAndMakeVisible(speedDisplay);

    // Sub-labels not needed in vertical layout — hide them
    speedSubLabel.setVisible(false);
    addChildComponent(speedSubLabel);

    // ---- Pitch slider — VERTICAL ----
    pitchSlider.setLookAndFeel(&pitchLAF);
    pitchSlider.setSliderStyle(juce::Slider::LinearVertical);
    pitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.setVelocityBasedMode(true);
    pitchSlider.setVelocityModeParameters(1.22, 1, 0.0, true);
    pitchSlider.setDoubleClickReturnValue(true, 0.0);   // double-click → 0
    addAndMakeVisible(pitchSlider);
    addAndMakeVisible(pitchDisplay);

    pitchSubLabel.setVisible(false);
    addChildComponent(pitchSubLabel);

    // ---- Classic shortcut ----
    btnClassic.onClick = [&] {
        setParamValue(p.apvts, "bitDepth",   12.0f);
        setParamValue(p.apvts, "sampleRate", 26040.0f);
    };
    addAndMakeVisible(btnClassic);

    // ---- All knobs ----
    for (auto* k : { &kBitDepth, &kSampleRate,
                     &kSpeedGlide, &kPitchGlide,
                     &kDrive, &kCrunch, &kJitter, &kNoise,
                     &kHPF, &kLPF, &kTone,
                     &kGateThresh, &kGateRel,
                     &kDrift, &kMotion, &kStereo,
                     &kMix, &kOutput })
        addAndMakeVisible(k);

    // ---- Speed fader — snap points ----
    speedSlider.snapPoints  = SnapSlider::speedRatioPoints();
    speedSlider.snapEnabled = false;

    // ---- Pitch fader — snap points ----
    pitchSlider.snapPoints  = SnapSlider::musicalPoints();
    pitchSlider.snapEnabled = false;

    // ---- Speed fader snap modes: GRID (ratios) and MUS (musical intervals) ----
    //  Mutually exclusive. GRID = tempo/chop ratios (1/2x, 2x, 4x).
    //  MUS  = authentic, perfectly-in-tune musical interval jumps via varispeed.
    btnSpeedGrid.onClick = [this] {
        const bool on = btnSpeedGrid.getToggleState();
        if (on)
        {
            btnSpeedMusical.setToggleState(false, juce::dontSendNotification);
            speedSlider.snapPoints = SnapSlider::speedRatioPoints();
        }
        speedSlider.snapEnabled = on;
        speedSlider.repaint();
    };
    addAndMakeVisible(btnSpeedGrid);

    btnSpeedMusical.onClick = [this] {
        const bool on = btnSpeedMusical.getToggleState();
        if (on)
        {
            btnSpeedGrid.setToggleState(false, juce::dontSendNotification);
            speedSlider.snapPoints = SnapSlider::musicalPoints();
        }
        else
        {
            speedSlider.snapPoints = SnapSlider::speedRatioPoints();
        }
        speedSlider.snapEnabled = on;
        speedSlider.repaint();
    };
    addAndMakeVisible(btnSpeedMusical);

    // ---- MUSICAL button (pitch interval snap) ----
    btnPitchMusical.onClick = [this] {
        pitchSlider.snapEnabled = btnPitchMusical.getToggleState();
        pitchSlider.repaint();
    };
    addAndMakeVisible(btnPitchMusical);

    // ---- MANUAL button — opens documentation URL ----
    btnManual.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF1A1408));
    btnManual.setColour(juce::TextButton::textColourOffId, SDCol::textDim);
    btnManual.onClick = [] {
        juce::URL("https://thesounddetective.com/manual/dust1247").launchInDefaultBrowser();
    };
    addAndMakeVisible(btnManual);

    // ---- Fader reset buttons — snap SPEED / PITCH back to 0 ----
    for (auto* b : { &btnSpeedReset, &btnPitchReset })
    {
        b->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF1A1408));
        b->setColour(juce::TextButton::textColourOffId, SDCol::textDim);
        addAndMakeVisible(b);
    }
    btnSpeedReset.onClick = [this] { speedSlider.setValue(0.0, juce::sendNotificationSync); };
    btnPitchReset.onClick = [this] { pitchSlider.setValue(0.0, juce::sendNotificationSync); };

    // ---- Toggles ----
    addAndMakeVisible(btnSnap);
    addAndMakeVisible(btnSampLink);
    addAndMakeVisible(btnSPLink);
    addAndMakeVisible(btnGateNoiseSC);

    // ---- APVTS wiring ----
    auto& apvts = p.apvts;
    aBitDepth   = std::make_unique<SliderAt>(apvts, "bitDepth",      kBitDepth.slider);
    aSampleRate = std::make_unique<SliderAt>(apvts, "sampleRate",    kSampleRate.slider);
    aDrive      = std::make_unique<SliderAt>(apvts, "drive",         kDrive.slider);
    aCrunch     = std::make_unique<SliderAt>(apvts, "crunch",        kCrunch.slider);
    aSpeed      = std::make_unique<SliderAt>(apvts, "samplerSpeed",  speedSlider);
    aPitch      = std::make_unique<SliderAt>(apvts, "modernPitch",   pitchSlider);
    aSpeedGlide = std::make_unique<SliderAt>(apvts, "speedGlide",    kSpeedGlide.slider);
    aPitchGlide = std::make_unique<SliderAt>(apvts, "pitchGlide",    kPitchGlide.slider);
    aSnap       = std::make_unique<ButtonAt>(apvts, "snap",          btnSnap);
    aSampLink   = std::make_unique<ButtonAt>(apvts, "samplerLink",   btnSampLink);
    aSPLink     = std::make_unique<ButtonAt>(apvts, "speedPitchLink",btnSPLink);
    aHPF        = std::make_unique<SliderAt>(apvts, "hpf",           kHPF.slider);
    aLPF        = std::make_unique<SliderAt>(apvts, "lpf",           kLPF.slider);
    aTone       = std::make_unique<SliderAt>(apvts, "tone",          kTone.slider);
    aJitter     = std::make_unique<SliderAt>(apvts, "jitter",        kJitter.slider);
    aMix        = std::make_unique<SliderAt>(apvts, "mix",           kMix.slider);
    aOutput     = std::make_unique<SliderAt>(apvts, "output",        kOutput.slider);
    aNoise      = std::make_unique<SliderAt>(apvts, "noise",         kNoise.slider);
    aGateThresh  = std::make_unique<SliderAt>(apvts, "gateThresh",    kGateThresh.slider);
    aGateRel     = std::make_unique<SliderAt>(apvts, "gateRelease",   kGateRel.slider);
    aGateNoiseSC = std::make_unique<ButtonAt>(apvts, "gateNoiseSC",   btnGateNoiseSC);
    aDeltaMode   = std::make_unique<ButtonAt>(apvts, "deltaMode",      btnDelta);
    aDrift       = std::make_unique<SliderAt>(apvts, "machineDrift",  kDrift.slider);
    aMotion     = std::make_unique<SliderAt>(apvts, "driftMotion",   kMotion.slider);
    aStereo     = std::make_unique<SliderAt>(apvts, "driftStereo",   kStereo.slider);

    // ── Case File first-launch walkthrough ────────────────────────────────
    if (CaseFileOverlay::shouldShow("D47-001"))
    {
        caseFile = std::make_unique<CaseFileOverlay>(
            "D47-001",
            "DUST 12.47",
            std::vector<std::pair<juce::String, juce::String>>{
                {
                    "WHAT YOU ARE HOLDING",
                    "DUST 12.47 is an original vintage-inspired sampler degradation processor "
                    "built around 12-bit texture, low-clock aliasing, varispeed movement, "
                    "machine drift, noise, gate, and musical destruction.\n\n"
                    "Run anything through it — drums, bass, vocals, a full mix. "
                    "It destroys beautifully.\n\n"
                    "Use MIX to blend processed with dry. Use DELTA to hear only what the processor is doing."
                },
                {
                    "THE FADERS — SPEED + PITCH",
                    "SAMPLER SPEED changes pitch and time together — like a real varispeed "
                    "deck slowing down or speeding up. Everything moves.\n\n"
                    "PITCH shifts pitch only. Time stays fixed.\n\n"
                    "GLIDE controls how fast each fader reaches its target. "
                    "Crank it up for slow pitch bends and lazy speed shifts.\n\n"
                    "Range: -24 to +24 semitones on both."
                },
                {
                    "THE DUST — BIT DEPTH + SAMPLE RATE",
                    "BIT DEPTH: 12-bit is the classic sweet spot. Go lower for heavy crunch. "
                    "4-bit is broken in the best way.\n\n"
                    "SAMPLE RATE: ~26k is the classic 12-bit sampler reference clock. Lower values "
                    "introduce aliasing and lo-fi smear.\n\n"
                    "DRIVE pushes saturation before the converters. CRUNCH adds asymmetric "
                    "harmonic distortion.\n\n"
                    "Press 12 / 26k to snap both to the classic settings instantly."
                },
                {
                    "NOISE + GATE",
                    "NOISE adds a vinyl-style noise floor.\n\n"
                    "THRESHOLD and RELEASE control the gate. Raise THRESHOLD to gate "
                    "everything below a certain level — tighten up loose material or "
                    "create rhythmic chops.\n\n"
                    "NOISE SC: engage this to sidechain the gate to the noise itself. "
                    "The noise density triggers the gate open and closed across the whole signal. "
                    "Organic, unpredictable, alive."
                },
                {
                    "MACHINE DRIFT + THE MIX",
                    "MACHINE DRIFT makes the plugin behave like aging hardware. "
                    "Drive, sample rate, and tone wander independently on L and R.\n\n"
                    "DRIFT = amount.  MOTION = speed.  STEREO = how much L and R diverge.\n\n"
                    "HIGH PASS / LOW PASS shape the wet signal. TONE tilts the frequency "
                    "balance. MIX blends wet and dry. OUTPUT is your final level.\n\n"
                    "You are now cleared for operation.\n\n"
                    "                                          — Detective 47"
                }
            }
        );
        addAndMakeVisible(*caseFile);
        caseFile->setBounds(getLocalBounds());
    }
}

Dust1200Editor::~Dust1200Editor()
{
    speedSlider.setLookAndFeel(nullptr);
    pitchSlider.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ===========================================================================
//  Logo loading
// ===========================================================================
void Dust1200Editor::tryLoadLogo()
{
    // Logo is embedded at compile time via juce_add_binary_data — no file path needed.
    juce::Image raw = juce::ImageFileFormat::loadFrom(
        BinaryData::logo_detective47_dust1200_png,
        (size_t)BinaryData::logo_detective47_dust1200_pngSize);

    if (!raw.isValid()) return;

    // Convert blue-on-white PNG → amber-on-transparent
    juce::Image mask(juce::Image::ARGB, raw.getWidth(), raw.getHeight(), true);
    for (int py = 0; py < raw.getHeight(); ++py)
        for (int px = 0; px < raw.getWidth(); ++px)
        {
            auto    col   = raw.getPixelAt(px, py);
            uint8_t alpha = static_cast<uint8_t>((1.0f - col.getBrightness()) * 255.0f);
            mask.setPixelAt(px, py, juce::Colour(
                static_cast<uint8_t>(210),
                static_cast<uint8_t>(148),
                static_cast<uint8_t>(32),
                alpha));
        }
    logoImage = mask;
}

// ===========================================================================
//  Paint helpers
// ===========================================================================
void Dust1200Editor::drawSectionPanel(juce::Graphics& g, juce::Rectangle<int> area, bool accent)
{
    // Subtle inset panel (used for bottom strip sub-sections)
    juce::ColourGradient grad(
        SDCol::panelSection, (float)area.getX(), (float)area.getY(),
        SDCol::panelDeep,    (float)area.getX(), (float)area.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(area.toFloat(), 4.0f);

    g.setColour(accent ? SDCol::driftAccent : SDCol::panelBorder);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void Dust1200Editor::drawSectionHeader(juce::Graphics& g, const juce::String& text,
                                       int x, int y, int w, bool bright)
{
    g.setColour(bright ? SDCol::textGold : SDCol::textDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold));
    g.drawText(text, x + 6, y - 12, w - 6, 11, juce::Justification::centredLeft, false);

    g.setColour(bright ? SDCol::textGold.withAlpha(0.35f) : SDCol::divider);
    g.fillRect(x, y, w, 1);
}

// ===========================================================================
//  Corner screw decoration
// ===========================================================================
static void drawScrew(juce::Graphics& g, float cx, float cy)
{
    const float r = 7.0f;
    g.setColour(juce::Colour(0xFF181410));
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour(juce::Colour(0xFF2A2418));
    g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
    g.setColour(juce::Colour(0xFF3A3020));
    g.drawLine(cx - 3.5f, cy, cx + 3.5f, cy, 1.2f);
    g.drawLine(cx, cy - 3.5f, cx, cy + 3.5f, 1.2f);
}

// ===========================================================================
//  Paint  (900 × 520)
//
//  Zone map:
//    y=  0..56  : top bar (logo  preset  meter)
//    y= 56..72  : tab strip  [SAMPLER | PATTERN | MIXER | TUNE | FX]
//    y= 72..394 : main section — SAMPLE | FADER CENTER | DUST
//    y=394..520 : bottom strip — SHAPE | MACHINE DRIFT | LEVEL
// ===========================================================================
void Dust1200Editor::paint(juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();

    // ── Overall panel background ──────────────────────────────────────────
    {
        juce::ColourGradient bg(SDCol::panelFace, 0.0f, 0.0f,
                                SDCol::bg,         0.0f, (float)H, false);
        g.setGradientFill(bg);
        g.fillAll();
    }

    // ── Top bar ───────────────────────────────────────────────────────────
    {
        juce::ColourGradient topBar(SDCol::panelDeep, 0.0f, 0.0f,
                                    SDCol::panelFace, 0.0f, 56.0f, false);
        g.setGradientFill(topBar);
        g.fillRect(0, 0, W, 56);

        g.setColour(SDCol::panelBorder);
        g.fillRect(0, 55, W, 1);
    }

    // ── Logo or text badge ────────────────────────────────────────────────
    if (logoImage.isValid())
    {
        float maxW = 260.0f, maxH = 44.0f;
        float ratio = juce::jmin(maxW / logoImage.getWidth(), maxH / logoImage.getHeight());
        int dw = (int)(logoImage.getWidth()  * ratio);
        int dh = (int)(logoImage.getHeight() * ratio);
        g.drawImage(logoImage, 14, (56 - dh) / 2, dw, dh,
                    0, 0, logoImage.getWidth(), logoImage.getHeight());
    }
    else
    {
        // "DETECTIVE" in gold
        g.setColour(SDCol::textGold);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
        g.drawText("DETECTIVE", 14, 6, 148, 20, juce::Justification::centredLeft, false);

        // "47" badge — small brass circle with number
        const float bx = 164.0f, by = 7.0f, br = 10.0f;
        juce::ColourGradient badge(SDCol::knobBrass1, bx, by,
                                   SDCol::knobBrass3, bx + br*2, by + br*2, false);
        g.setGradientFill(badge);
        g.fillEllipse(bx, by, br * 2.0f, br * 2.0f);
        g.setColour(SDCol::panelBorder);
        g.drawEllipse(bx, by, br * 2.0f, br * 2.0f, 1.0f);
        g.setColour(juce::Colour(0xFF100C06));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        g.drawText("47", (int)bx, (int)by, (int)(br * 2.0f), (int)(br * 2.0f),
                   juce::Justification::centred, false);

        // "DUST 12.47" subtitle
        g.setColour(SDCol::textAmber);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
        g.drawText("D U S T   1 2 . 4 7", 14, 28, 196, 15, juce::Justification::centredLeft, false);

        // Thin brass rule under logo area
        g.setColour(SDCol::knobBrass3.withAlpha(0.45f));
        g.drawHorizontalLine(46, 14.0f, 210.0f);
    }

    // ── Center title ──────────────────────────────────────────────────────
    g.setColour(SDCol::textAmber.withAlpha(0.55f));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
    g.drawText("VINTAGE SAMPLER DEGRADATION PROCESSOR", 280, 14, 340, 12,
               juce::Justification::centred, false);
    g.setColour(SDCol::textSub.withAlpha(0.6f));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
    g.drawText("D47-SN:1247", 280, 36, 340, 10, juce::Justification::centred, false);

    // ── Amber power LED ───────────────────────────────────────────────────
    {
        const float lcx = 874.0f, lcy = 28.0f, lr = 5.5f;
        // Outer glow
        g.setColour(SDCol::btnLED.withAlpha(0.18f));
        g.fillEllipse(lcx - lr * 2.2f, lcy - lr * 2.2f, lr * 4.4f, lr * 4.4f);
        // Main LED
        juce::ColourGradient ledGrad(juce::Colour(0xFFFFD040), lcx - lr * 0.3f, lcy - lr * 0.3f,
                                      juce::Colour(0xFFA06010), lcx + lr, lcy + lr, true);
        g.setGradientFill(ledGrad);
        g.fillEllipse(lcx - lr, lcy - lr, lr * 2.0f, lr * 2.0f);
        g.setColour(juce::Colour(0x60FFFFFF));
        g.fillEllipse(lcx - lr * 0.6f, lcy - lr * 0.7f, lr * 0.8f, lr * 0.5f);
        // Label
        g.setColour(SDCol::textSub);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
        g.drawText("ON", 862, 37, 24, 10, juce::Justification::centred, false);
    }

    // ── Tab strip (y=56..72) ──────────────────────────────────────────────
    {
        // Strip background
        g.setColour(SDCol::panelDeep);
        g.fillRect(0, 56, W, 16);
        g.setColour(SDCol::divider.withAlpha(0.5f));
        g.fillRect(0, 71, W, 1);

        for (auto& t : tabs)
        {
            const bool active = (t.name == activeTab);
            if (active)
            {
                g.setColour(SDCol::btnOn);
                g.fillRect(t.rect.getX(), 56, t.rect.getWidth(), 16);
                g.setColour(SDCol::textGold);
                g.fillRect(t.rect.getX(), 71, t.rect.getWidth(), 1);
            }

            g.setColour(active ? SDCol::textGold : SDCol::textSub);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(),
                                 active ? 9.0f : 8.5f,
                                 active ? juce::Font::bold : juce::Font::plain));
            g.drawText(t.name, t.rect, juce::Justification::centred, false);
        }

        // Serial area on right of tab strip
        g.setColour(SDCol::textSub.withAlpha(0.5f));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
        g.drawText("REV.2  //  DETECTIVE 47  //  DUST-1200", 420, 56, 450, 16,
                   juce::Justification::centredRight, false);
    }

    // ── Main section horizontal border ────────────────────────────────────
    g.setColour(SDCol::divider);
    g.fillRect(0, 72, W, 1);

    // ── Vertical section dividers (main area only) ────────────────────────
    // SAMPLE | CENTER
    g.setColour(SDCol::panelBorder);
    g.fillRect(198, 72, 1, 322);
    // CENTER | DUST
    g.fillRect(702, 72, 1, 322);

    // ── Horizontal divider above bottom strip ─────────────────────────────
    g.setColour(SDCol::panelBorder);
    g.fillRect(0, 394, W, 1);
    // Amber accent rule
    g.setColour(SDCol::knobBrass3.withAlpha(0.3f));
    g.fillRect(16, 393, W - 32, 1);

    // ── Section labels (main) ─────────────────────────────────────────────
    {
        auto mono9 = juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::bold);
        auto mono8 = juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain);

        // SAMPLE
        g.setColour(SDCol::textDim);
        g.setFont(mono9);
        g.drawText("SAMPLE", 12, 76, 180, 10, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider);
        g.fillRect(12, 87, 180, 1);

        // DUST
        g.setColour(SDCol::textDim);
        g.setFont(mono9);
        g.drawText("DUST", 706, 76, 186, 10, juce::Justification::centredLeft, false);
        g.fillRect(706, 87, 186, 1);

        // Center section: SPEED label above left fader, PITCH above right
        g.setColour(SDCol::textGold);
        g.setFont(mono9);
        g.drawText("SAMPLER SPEED", 206, 76, 190, 10, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider);
        g.fillRect(206, 87, 190, 1);

        g.setColour(SDCol::textAmber);
        g.setFont(mono9);
        g.drawText("PITCH", 314, 76, 100, 10, juce::Justification::centredLeft, false);
        g.setColour(SDCol::divider.withAlpha(0.5f));
        g.fillRect(314, 87, 100, 1);

        // Vertical rule between speed and pitch faders
        g.setColour(SDCol::divider.withAlpha(0.5f));
        g.fillRect(307, 88, 1, 286);

        // Center section right side labels
        g.setColour(SDCol::textDim);
        g.setFont(mono8);
        g.drawText("GLIDE", 404, 76, 180, 10, juce::Justification::centredLeft, false);
        g.fillRect(404, 87, 290, 1);
    }

    // ── Bottom strip section panels ───────────────────────────────────────
    //  SHAPE/GATE: x=4..348 (344px)  DRIFT: x=354..608 (254px)  LEVEL: x=614..896 (282px)
    drawSectionPanel(g, {   4, 398, 344, 144 });        // SHAPE / GATE
    drawSectionPanel(g, { 354, 398, 254, 144 }, true);  // MACHINE DRIFT
    drawSectionPanel(g, { 614, 398, 282, 144 });        // LEVEL

    // Bottom section headers
    drawSectionHeader(g, "SHAPE / GATE",  6,   411, 340);
    drawSectionHeader(g, "MACHINE DRIFT", 356, 411, 250, true);
    drawSectionHeader(g, "LEVEL",         616, 411, 278);

    // Vertical rule inside SHAPE/GATE: separates HPF/LPF/TONE from GATE knobs
    g.setColour(SDCol::divider.withAlpha(0.6f));
    g.fillRect(195, 415, 1, 96);

    // "GATE" sub-label inside SHAPE panel
    g.setColour(SDCol::textDim);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.5f, juce::Font::plain));
    g.drawText("GATE", 200, 400, 140, 11, juce::Justification::centred, false);

    // DRIFT sub-tag
    g.setColour(SDCol::driftAccent);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 7.0f, juce::Font::plain));
    g.drawText("ANALOG MOVEMENT", 292, 503, 312, 9, juce::Justification::centred, false);

    // ── Corner screws ─────────────────────────────────────────────────────
    drawScrew(g, 10.0f,          10.0f);
    drawScrew(g, (float)W - 10.0f, 10.0f);
    drawScrew(g, 10.0f,          (float)H - 10.0f);
    drawScrew(g, (float)W - 10.0f, (float)H - 10.0f);
}

// ===========================================================================
//  Layout  (900 × 520)
//
//  Main section:   y=72..394  (322 px)
//  Bottom section: y=394..520 (126 px)
//
//  Columns:
//    SAMPLE  x=0..198   (198 px)
//    CENTER  x=198..702 (504 px)
//    DUST    x=702..900 (198 px)
// ===========================================================================
void Dust1200Editor::layoutTabs()
{
    int tx = 18;
    for (auto& t : tabs) { t.rect = { tx, 56, t.width, 16 }; tx += t.width + 3; }
}

void Dust1200Editor::mouseDown(const juce::MouseEvent& e)
{
    for (auto& t : tabs)
    {
        if (! t.rect.contains(e.getPosition())) continue;

        if (noiseDrawer)  noiseDrawer->setVisible(false);
        if (jitterDrawer) jitterDrawer->setVisible(false);
#if DUST_VAULT_ENABLED
        if (vaultDrawer)  vaultDrawer->setVisible(false);
#endif

        if (t.name == "NOISE" && noiseDrawer)
        {
            activeTab = "NOISE";
            noiseDrawer->setVisible(true);
            noiseDrawer->toFront(true);
        }
        else if (t.name == "JITTER" && jitterDrawer)
        {
            activeTab = "JITTER";
            jitterDrawer->setVisible(true);
            jitterDrawer->toFront(true);
        }
#if DUST_VAULT_ENABLED
        else if (t.name == "VAULT" && vaultDrawer)
        {
            activeTab = "VAULT";
            vaultDrawer->setVisible(true);
            vaultDrawer->toFront(true);
        }
#endif
        else
        {
            activeTab = "SAMPLER";
        }
        repaint();
        return;
    }
}

void Dust1200Editor::resized()
{
    if (caseFile)     caseFile->setBounds(getLocalBounds());
    if (noiseDrawer)  noiseDrawer->setBounds(getLocalBounds());
    if (jitterDrawer) jitterDrawer->setBounds(getLocalBounds());
#if DUST_VAULT_ENABLED
    if (vaultDrawer)  vaultDrawer->setBounds(getLocalBounds());
#endif
    layoutTabs();
    const int W = getWidth();

    // ── Top bar ────────────────────────────────────────────────────────────
    lblPreset   .setBounds(W - 390, 19,  58, 18);
    presetBox   .setBounds(W - 325, 18, 200, 20);
    btnDelta    .setBounds(W - 176, 19,  54, 18);
    meter       .setBounds(W - 116, 12,  40, 32);
    versionLabel.setBounds(W -  68, 29,  56, 12);
    btnManual   .setBounds(W - 390 - 66, 19, 60, 18);

    // Hidden sub-labels (zero size)
    speedSubLabel.setBounds(0, 0, 0, 0);
    pitchSubLabel.setBounds(0, 0, 0, 0);

    // ── SAMPLE section  (x=0..198, y=72..394) ─────────────────────────────
    {
        // Two brass knobs side by side
        const int kW = 82, kH = 98, kY = 90;
        kBitDepth  .setBounds( 10, kY, kW, kH);
        kSampleRate.setBounds(102, kY, kW, kH);

        // 12/26k shortcut button
        btnClassic.setBounds(16, 222, 162, 22);

        // Spacer — no more space needed (section ends at y=394)
    }

    // ── CENTER section  (x=198..702, y=72..394) ──────────────────────────
    {
        // Mode buttons above each fader (visible only when you know to look)
        // GRID = speed ratio lock,  MUSICAL = pitch interval snap
        btnSpeedGrid   .setBounds(208, 90, 48, 16);
        btnSpeedMusical.setBounds(260, 90, 48, 16);
        btnPitchMusical.setBounds(320, 90, 100, 16);

        // SPEED fader — vertical, component wide enough for labels on the left
        speedSlider  .setBounds(206, 108, 104, 262);
        speedDisplay .setBounds(212, 376,  74, 16);
        btnSpeedReset.setBounds(288, 376,  20, 16);

        // PITCH fader — vertical
        pitchSlider  .setBounds(318, 108, 104, 262);
        pitchDisplay .setBounds(324, 376,  74, 16);
        btnPitchReset.setBounds(400, 376,  20, 16);

        // SPD / PCH GLIDE knobs (stacked in right portion of center)
        const int gkW = 78, gkH = 90;
        kSpeedGlide.setBounds(414, 92,  gkW, gkH);
        kPitchGlide.setBounds(414, 200, gkW, gkH);

        // Snap / link toggle buttons
        btnSnap    .setBounds(510, 108, 74, 22);
        btnSampLink.setBounds(592, 108, 84, 22);
        btnSPLink  .setBounds(510, 138, 166, 22);
    }

    // ── DUST section  (x=702..900, y=72..394) ────────────────────────────
    {
        const int kW = 82, kH = 98;
        kDrive .setBounds(706,  90, kW, kH);
        kCrunch.setBounds(800,  90, kW, kH);

        const int kWs = 74, kHs = 88;
        kJitter.setBounds(710, 212, kWs, kHs);
        kNoise .setBounds(806, 212, kWs, kHs);
    }

    // ── BOTTOM STRIP  (y=394..520) ────────────────────────────────────────
    {
        const int ky = 414, kH = 92;

        // SHAPE/GATE — HPF, LPF, TONE | THRESH, RELEASE + NOISE SC button
        //   Section spans x=4..348 (344px)
        //   Left (HPF/LPF/TONE): kW=56, gap=8, start x=8
        const int shKW = 56, shGap = 8, shX = 8;
        kHPF .setBounds(shX,                      ky, shKW, kH);
        kLPF .setBounds(shX + shKW + shGap,       ky, shKW, kH);
        kTone.setBounds(shX + (shKW + shGap) * 2, ky, shKW, kH);

        //   Right (GATE THRESH / RELEASE): kW=62, gap=8, start x=200
        const int gX = 200, gKW = 62, gGap = 8;
        kGateThresh.setBounds(gX,              ky, gKW, kH);
        kGateRel   .setBounds(gX + gKW + gGap, ky, gKW, kH);
        //   NOISE SC toggle below gate knobs
        btnGateNoiseSC.setBounds(gX, ky + kH + 2, gKW * 2 + gGap, 16);

        // MACHINE DRIFT — DRIFT, MOTION, STEREO  (x=354..608, 254px)
        const int drX = 360, drKW = 64, drGap = 12;
        kDrift .setBounds(drX,                    ky, drKW, kH);
        kMotion.setBounds(drX + drKW + drGap,     ky, drKW, kH);
        kStereo.setBounds(drX + (drKW + drGap)*2, ky, drKW, kH);

        // LEVEL — MIX, OUTPUT  (x=614..896, 282px)
        const int lvX = 626, lvKW = 82, lvGap = 28;
        kMix   .setBounds(lvX,               ky, lvKW, kH);
        kOutput.setBounds(lvX + lvKW + lvGap, ky, lvKW, kH);
    }
}
