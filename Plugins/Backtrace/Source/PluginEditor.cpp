#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const juce::StringArray kModeNames { "Manual", "DAW Loc", "Cycle" };
    const juce::Array<int>  kFallbackBars { 1, 2, 4, 8 };

    juce::String ppqToBarBeat(double ppq, int tsNum, int tsDen)
    {
        const double qpb  = 4.0 * (double) tsNum / (double) juce::jmax(1, tsDen);
        const double beatLen = 4.0 / (double) juce::jmax(1, tsDen);
        if (qpb <= 0.0) return "1.1";
        const int bar  = (int) std::floor(ppq / qpb) + 1;
        const int beat = (int) std::floor((ppq - (bar - 1) * qpb) / beatLen) + 1;
        return juce::String(bar) + "." + juce::String(beat);
    }
}

BacktraceEditor::BacktraceEditor(BacktraceProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p)
{
    addAndMakeVisible(waveform);
    waveform.onLocatorsChanged = [this](int a, int b) { updateSelection(a, b); };
    waveform.onTailStartChanged = [this](int offset)            // Source End / Tail Start moved
    {
        proc.setTailStart(offset);                             // markTailDirty → print goes stale
        updateWorkflowHints();
    };
    waveform.onFilesDropped = [this](const juce::StringArray& f) { importFiles(f, -1); };   // → active slot

    // --- lane captions ---
    for (auto* c : { &sourceCaption, &printedCaption })
    {
        c->setColour(juce::Label::textColourId, juce::Colour(0xffc8a24a));
        c->setFont(10.0f);
        addAndMakeVisible(c);
    }
    advancedCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.35f));
    advancedCaption.setFont(10.0f);
    addAndMakeVisible(advancedCaption);

    // --- printed-swell lane (the final editable stage) ---
    addAndMakeVisible(swellCanvas);
    swellCanvas.onLocatorsChanged = [this](int a, int b)
    { proc.setSwellTrim(a, b); updateSwellSelLabel(); };
    swellCanvas.onFadesChanged = [this](int fi, int fo)
    { proc.setFades(fi, fo); updateSwellSelLabel(); syncFadeControls(); };
    swellCanvas.provideDragFile = [this] { return proc.vaultRenderForDrag(); };   // drag the edited swell
    swellCanvas.onFadeMenu = [this](int which) { showFadeMenu(which); };

    swellSelLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    swellSelLabel.setFont(11.0f);
    addAndMakeVisible(swellSelLabel);

    // --- printed-swell editor: grid snap + zoom ---
    snapCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    snapCaption.setFont(11.0f);
    addAndMakeVisible(snapCaption);
    snapBox.addItem("Off", 1);  snapBox.addItem("1 Bar", 2); snapBox.addItem("1/2", 3);
    snapBox.addItem("1/4", 4);  snapBox.addItem("1/8", 5);   snapBox.addItem("1/16", 6);
    snapBox.setSelectedId(1, juce::dontSendNotification);
    snapBox.onChange = [this] { swellCanvas.setSnap(snapBox.getSelectedId() - 1); };
    addAndMakeVisible(snapBox);

    zoomOutBtn.onClick = [this] { swellCanvas.zoomOut(); };
    zoomInBtn.onClick  = [this] { swellCanvas.zoomIn();  };
    zoomFitBtn.onClick = [this] { swellCanvas.zoomFit(); };
    for (auto* b : { &zoomOutBtn, &zoomInBtn, &zoomFitBtn }) addAndMakeVisible(b);

    // --- printed-swell fades: numeric length (ms), curve type, reset ---
    for (auto* c : { &fadeInCap, &fadeOutCap })
    { c->setColour(juce::Label::textColourId, juce::Colour(0xff7ad1ff)); c->setFont(11.0f); addAndMakeVisible(c); }
    for (auto* f : { &fadeInMs, &fadeOutMs })
    {
        f->setEditable(true);
        f->setColour(juce::Label::backgroundColourId, juce::Colour(0xff14171a));
        f->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
        f->setColour(juce::Label::outlineColourId, juce::Colour(0x33ffffff));
        f->setJustificationType(juce::Justification::centred);
        f->setFont(11.0f);
        f->setTooltip("Fade length in ms. Press Return to confirm.");
        f->onEditorHide = [this] { commitFadeMs(); };
        addAndMakeVisible(f);
    }
    for (auto* cb : { &fadeInCurveBox, &fadeOutCurveBox })
    {
        for (int i = 0; i < kNumFadeCurves; ++i) cb->addItem(btFadeCurveName(i), i + 1);
        cb->setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(cb);
    }
    fadeInCurveBox.onChange  = [this] { proc.setFadeInCurve(fadeInCurveBox.getSelectedId() - 1);
                                        swellCanvas.setFadeCurves(proc.getFadeInCurve(), proc.getFadeOutCurve()); };
    fadeOutCurveBox.onChange = [this] { proc.setFadeOutCurve(fadeOutCurveBox.getSelectedId() - 1);
                                        swellCanvas.setFadeCurves(proc.getFadeInCurve(), proc.getFadeOutCurve()); };
    fadeResetBtn.setTooltip("Reset fades to a short safety fade.");
    fadeResetBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem("Reset Fade In",    [this] { resetFade(true,  false); });
        m.addItem("Reset Fade Out",   [this] { resetFade(false, true);  });
        m.addItem("Reset Both Fades", [this] { resetFade(true,  true);  });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fadeResetBtn));
    };
    addAndMakeVisible(fadeResetBtn);

    // --- final HPF / LPF filter (+ start→end motion) ---
    filterCaption.setColour(juce::Label::textColourId, juce::Colour(0xffc8a24a));
    filterCaption.setFont(10.0f);
    addAndMakeVisible(filterCaption);

    filterToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.85f));
    filterToggle.onClick = [this] { proc.setFilterOn(filterToggle.getToggleState()); syncFilterControls(); };
    addAndMakeVisible(filterToggle);
    filterMotionToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.85f));
    filterMotionToggle.onClick = [this] { proc.setFilterMotion(filterMotionToggle.getToggleState()); syncFilterControls(); };
    filterMotionToggle.setTooltip("Moves the filter cutoff from start to end across the printed swell.");
    addAndMakeVisible(filterMotionToggle);

    // --- Harrison-style filter: Slope, motion Curve, Character ---
    filterToggle.setTooltip("Harrison-style final filter for powerful musical HPF/LPF shaping.");
    filterSlopeBox.addItem("12 dB", 1);  filterSlopeBox.addItem("24 dB", 2);
    filterSlopeBox.onChange = [this] { proc.setFilterSlope(filterSlopeBox.getSelectedId() - 1); refreshSwellCanvas(); };
    filterSlopeBox.setTooltip("Filter slope: 12 or 24 dB/oct. 24 = stronger, more obvious.");
    addAndMakeVisible(filterSlopeBox);
    filterCurveBox.addItem("Linear", 1); filterCurveBox.addItem("Exp", 2); filterCurveBox.addItem("Log", 3); filterCurveBox.addItem("S-Curve", 4);
    filterCurveBox.onChange = [this] { proc.setFilterCurve(filterCurveBox.getSelectedId() - 1); refreshSwellCanvas(); };
    filterCurveBox.setTooltip("Motion time-curve (cutoff always sweeps in log-frequency space).");
    addAndMakeVisible(filterCurveBox);
    // Motion Mode — what the sweep does relative to Peak Land (the landing).
    filterMotionModeBox.addItem("Rise Only", 1); filterMotionModeBox.addItem("Rise+Fall", 2); filterMotionModeBox.addItem("Fall Only", 3);
    filterMotionModeBox.onChange = [this] { proc.setFilterMotionMode(filterMotionModeBox.getSelectedId() - 1); refreshSwellCanvas(); };
    filterMotionModeBox.setTooltip("Motion Mode: Rise Only = open to Peak Land and hold.  Rise+Fall = open to Peak Land, then close back down over the tail.  Fall Only = start open and close through the whole swell + tail.");
    addAndMakeVisible(filterMotionModeBox);
    filterDriveKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    filterDriveKnob.setRange(0.0, 1.0, 0.01);
    filterDriveKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 15);
    filterDriveKnob.setLookAndFeel(&knobLNF);
    filterDriveKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ad1ff));
    filterDriveKnob.onValueChange = [this] { proc.setFilterDrive((float) filterDriveKnob.getValue()); refreshSwellCanvas(); };
    filterDriveKnob.setTooltip("Character / Edge — adds resonance near cutoff and gentle analog drive.");
    addAndMakeVisible(filterDriveKnob);
    for (auto* l : { &filterSlopeLabel, &filterCurveLabel, &filterDriveLabel })
    {
        l->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        l->setJustificationType(juce::Justification::centred);
        l->setFont(10.0f);
        addAndMakeVisible(l);
    }

    struct FK { juce::Slider* k; juce::Label* l; double lo, hi, def; std::function<void(float)> set; };
    const std::vector<FK> fks {
        { &hpfKnob,    &hpfLabel,    20.0,  8000.0,  20.0,    [this](float v){ proc.setHpfStart(v); } },  // 20 = off; up to 8k kills rumble
        { &hpfEndKnob, &hpfEndLabel, 20.0,  8000.0,  20.0,    [this](float v){ proc.setHpfEnd(v);   } },
        { &lpfKnob,    &lpfLabel,    300.0, 20000.0, 20000.0, [this](float v){ proc.setLpfStart(v); } },  // 20k = open; down to 300 = very dark
        { &lpfEndKnob, &lpfEndLabel, 300.0, 20000.0, 20000.0, [this](float v){ proc.setLpfEnd(v);   } },
    };
    for (auto& fk : fks)
    {
        fk.k->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        fk.k->setRange(fk.lo, fk.hi, 1.0);
        fk.k->setSkewFactorFromMidPoint(std::sqrt(fk.lo * fk.hi));
        fk.k->setValue(fk.def, juce::dontSendNotification);
        fk.k->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 15);
        fk.k->setTextValueSuffix(" Hz");
        fk.k->setLookAndFeel(&knobLNF);
        fk.k->setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ad1ff));
        auto setter = fk.set;
        fk.k->onValueChange = [k = fk.k, setter] { setter((float) k->getValue()); };
        addAndMakeVisible(fk.k);

        fk.l->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        fk.l->setJustificationType(juce::Justification::centred);
        fk.l->setFont(11.0f);
        addAndMakeVisible(fk.l);
    }

    // --- capture-mode selector (radio group) ---
    // Capture mode buttons: Manual, DAW Locators, Next Cycle
    // (The old "1 Bar / 2 Bars / 4 Bars" buttons are removed to clarify the workflow:
    // capture a short source, then use the separate Swell Length selector for the final length.)
    const std::array<int, 3> kModeMap = { 0, 4, 5 };   // UI → SyncCapture::Mode (Manual=0, DawLoc=4, Cycle=5)
    for (int i = 0; i < kModeNames.size(); ++i)
    {
        auto* b = modeButtons.add(new juce::TextButton(kModeNames[i]));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(100);
        b->setConnectedEdges(((i > 0) ? juce::Button::ConnectedOnLeft : 0)
                           | ((i < kModeNames.size() - 1) ? juce::Button::ConnectedOnRight : 0));
        const int modeVal = kModeMap[i];
        b->onClick = [this, modeVal] { proc.setCaptureMode(modeVal); };
        addAndMakeVisible(b);
    }
    modeButtons[0]->setToggleState(true, juce::dontSendNotification);
    proc.setCaptureMode(0);

    // --- fallback musical-length selector ---
    // Fallback buttons (1/2/4/8 bar default capture lengths) are kept internally but hidden
    // from the UI to simplify the workflow. The swell length on the left panel is now
    // the primary control for setting the final reverse effect length.
    for (int i = 0; i < kFallbackBars.size(); ++i)
    {
        auto* b = fallbackButtons.add(new juce::TextButton(juce::String(kFallbackBars[i])));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(101);
        b->onClick = [this, i] { proc.setFallbackBars(kFallbackBars[i]); };
    }
    fallbackButtons[2]->setToggleState(true, juce::dontSendNotification);
    proc.setFallbackBars(4);

    // --- transport / locator readouts ---
    auto styleReadout = [](juce::Label& l, juce::Justification j)
    {
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
        l.setJustificationType(j);
        l.setFont(13.0f);
    };
    styleReadout(transportLabel, juce::Justification::centredLeft);
    styleReadout(locatorLabel,  juce::Justification::centredRight);
    addAndMakeVisible(transportLabel);
    addAndMakeVisible(locatorLabel);

    // --- arm / lock / print / status ---
    armButton.onClick = [this]
    {
        if (proc.getCaptureState() == 0) proc.armCapture();   // Idle → arm
        else                             proc.disarmCapture();
    };
    addAndMakeVisible(armButton);

    lockToggle.setToggleState(true, juce::dontSendNotification);
    lockToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.8f));
    lockToggle.onClick = [this] { proc.setLocatorLock(lockToggle.getToggleState()); };
    proc.setLocatorLock(true);
    addAndMakeVisible(lockToggle);

    finalizeButton.onClick = [this]
    {
        auto f = proc.vaultPrintProcessed();
        if (f.existsAsFile())
        {
            lastOutputFile = f;
            const int s = proc.getLastStoredSlot();
            slotTab = 1;                       // reveal the Print bank with the new print selected
            statusLabel.setText(s >= 0 ? "Printed to Print Slot " + juce::String(s + 1) + " - drag it to your DAW"
                                       : "Printed - drag it to your DAW",
                                juce::dontSendNotification);
            rebuildSlotList();
        }
        else statusLabel.setText("Nothing to print", juce::dontSendNotification);
    };
    addAndMakeVisible(finalizeButton);

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    statusLabel.setFont(12.0f);
    addAndMakeVisible(statusLabel);

    selectionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    selectionLabel.setJustificationType(juce::Justification::centredLeft);
    selectionLabel.setFont(12.0f);
    addAndMakeVisible(selectionLabel);

    // --- Play Source: plays the raw captured source region ---
    playSourceButton.onClick = [this]
    {
        if (proc.getAuditionWhat() == 1) proc.stopReverseAudition();
        else proc.startSourceAudition();
    };
    addAndMakeVisible(playSourceButton);

    // --- Play Swell: replays the CURRENT rendered swell (no rebuild). If a tail-generator
    // setting changed since the render, it honestly says it is playing the previous swell. ---
    reverseButton.onClick = [this]
    {
        if (proc.getAuditionWhat() == 2) { proc.stopReverseAudition(); return; }
        proc.setSwellMode(true);
        proc.startReverseAudition(false);   // replay edited swell, no re-render
        if (! proc.hasSwell())
            statusLabel.setText("Nothing to play - press Create Swell", juce::dontSendNotification);
        else if (proc.isSwellStale())
            statusLabel.setText("Playing previous swell - press Create Swell to update", juce::dontSendNotification);
        else
            statusLabel.setText("Playing swell", juce::dontSendNotification);
    };
    reverseButton.setTooltip("Replay the current rendered swell exactly as it will print/export/drag. "
                             "If a tail setting changed, press Create Swell to rebuild first.");
    addAndMakeVisible(reverseButton);

    // --- Audition Tail: hear the FORWARD wet FX tail BEFORE it is reversed ---
    auditionTailButton.onClick = [this]
    {
        if (proc.getAuditionWhat() == 3) proc.stopReverseAudition();
        else proc.startTailAudition();
    };
    auditionTailButton.setTooltip("Hear the forward wet reverb/delay tail before it is reversed. "
                                  "If the tail is weak, the reverse swell will be weak.");
    addAndMakeVisible(auditionTailButton);

    // --- Reverse Swell: force a fresh render into the printed-swell lane ---
    swellCaption.setText("Swell Length", juce::dontSendNotification);
    swellCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    swellCaption.setFont(12.0f);
    addAndMakeVisible(swellCaption);
    swellBox.addItem("1/8 note", 1);
    swellBox.addItem("1/4 note", 2);
    swellBox.addItem("1/2 note", 3);
    swellBox.addItem("1 Bar",    4);
    swellBox.addItem("2 Bars",   5);
    swellBox.addItem("4 Bars",   6);
    swellBox.addItem("8 Bars",   7);
    swellBox.setSelectedId(5, juce::dontSendNotification);   // 2 bars
    swellBox.onChange = [this]
    {
        const double bpb = juce::jmax(1, proc.getHostTsNum());   // beats per bar (note values are tempo-relative)
        float barsVal;
        switch (swellBox.getSelectedId())
        {
            case 1: barsVal = (float) (0.5 / bpb); break;   // 1/8 note
            case 2: barsVal = (float) (1.0 / bpb); break;   // 1/4 note
            case 3: barsVal = (float) (2.0 / bpb); break;   // 1/2 note
            case 4: barsVal = 1.0f; break;                  // 1 bar
            case 5: barsVal = 2.0f; break;                  // 2 bars
            case 6: barsVal = 4.0f; break;                  // 4 bars
            case 7: barsVal = 8.0f; break;                  // 8 bars
            default: barsVal = 2.0f; break;
        }
        proc.setSwellLenBars(barsVal);
        if (proc.hasSwell()) proc.requestRender(false);   // background re-render to the new length
        else refreshSwellCanvas();
    };
    addAndMakeVisible(swellBox);

    keepPitchToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.8f));
    keepPitchToggle.setToggleState(proc.getKeepPitch(), juce::dontSendNotification);
    keepPitchToggle.setTooltip("Stretch the swell to the chosen length while keeping the source pitch. Off = creative pitch drop.");
    keepPitchToggle.onClick = [this]
    {
        proc.setKeepPitch(keepPitchToggle.getToggleState());
        if (proc.hasSwell()) proc.requestRender(false);
        else refreshSwellCanvas();
    };
    addAndMakeVisible(keepPitchToggle);
    // --- Create Swell: rebuild the swell from the source with the CURRENT settings, then
    // auto-play the new result. This is the only action that re-renders the tail. ---
    swellButton.onClick = [this]
    {
        if (proc.isRendering()) return;
        if (proc.getCaptureLength() <= 0)
        { statusLabel.setText("Load a source first (drag audio in)", juce::dontSendNotification); return; }
        proc.stopReverseAudition();
        proc.setSwellMode(true);
        proc.requestRender(true);          // background render → auto-play when it completes
        statusLabel.setText("Rendering swell...", juce::dontSendNotification);
    };
    swellButton.setTooltip("Render the reverse swell from the current source + settings and play it. "
                           "Do this after changing any tail setting (delay, reverb, tail type, length, pitch).");
    addAndMakeVisible(swellButton);

    landToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.8f));
    landToggle.onClick = [this] { proc.setLandAtSource(landToggle.getToggleState()); };
    addAndMakeVisible(landToggle);

    // --- Export WAV (fallback) + Reveal in Finder ---
    exportButton.onClick = [this]
    {
        auto f = proc.vaultExportToLibrary();   // edited printed swell → ~/Music/Backtrace/Library
        if (f.existsAsFile())
        {
            lastOutputFile = f;
            statusLabel.setText("Exported: " + f.getFileName(), juce::dontSendNotification);
        }
        else statusLabel.setText("Nothing to export", juce::dontSendNotification);
    };
    addAndMakeVisible(exportButton);

    revealButton.onClick = [this]
    {
        if (lastOutputFile.existsAsFile()) lastOutputFile.revealToUser();
        else proc.vaultLibraryDir().revealToUser();
    };
    addAndMakeVisible(revealButton);

    importButton.onClick = [this]
    {
        auto startDir = lastBrowseDir.isDirectory() ? lastBrowseDir
                                                     : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        chooser = std::make_unique<juce::FileChooser>("Import audio into Backtrace", startDir,
                                                      "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.caf");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                lastBrowseDir = f.getParentDirectory();
                if (proc.importFromLibrary(f).existsAsFile()) onSourceImported(f);
                else statusLabel.setText("Could not import audio file", juce::dontSendNotification);
            });
    };
    addAndMakeVisible(importButton);

    dragZone.provideFile = [this] { return proc.vaultRenderForDrag(); };
    addAndMakeVisible(dragZone);

    // --- pitch wheel (semitones + octave buttons) ---
    pitchCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    pitchCaption.setFont(12.0f);
    addAndMakeVisible(pitchCaption);

    pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSlider.setRange(-24.0, 24.0, 1.0);          // semitone detents, +/-2 octaves
    pitchSlider.setValue(0.0, juce::dontSendNotification);
    pitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.onValueChange = [this]
    {
        const auto st = (float) pitchSlider.getValue();
        proc.setPitchSemitones(st);
        pitchValue.setText((st > 0 ? "+" : "") + juce::String((int) st) + " st",
                           juce::dontSendNotification);
    };
    addAndMakeVisible(pitchSlider);

    auto nudge = [this](double d)
    {
        pitchSlider.setValue(juce::jlimit(-24.0, 24.0, pitchSlider.getValue() + d));
    };
    octDownButton.onClick = [nudge] { nudge(-12.0); };
    octUpButton.onClick   = [nudge] { nudge(+12.0); };
    addAndMakeVisible(octDownButton);
    addAndMakeVisible(octUpButton);

    pitchValue.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    pitchValue.setJustificationType(juce::Justification::centred);
    pitchValue.setFont(13.0f);
    pitchValue.setText("0 st", juce::dontSendNotification);
    addAndMakeVisible(pitchValue);

    // --- delay machine: flavor dropdown + remappable knob cluster ---
    delayCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
    delayCaption.setFont(10.0f);
    addAndMakeVisible(delayCaption);

    delayFlavorBox.addItem("Off",           1);
    delayFlavorBox.addItem("Reel Echo",     2);
    delayFlavorBox.addItem("Digital Pedal", 3);
    delayFlavorBox.addItem("Magnetic Drum", 4);
    delayFlavorBox.addItem("Tape Witness",  5);
    delayFlavorBox.addItem("Cold Rack",     6);
    delayFlavorBox.addItem("Vault Delay",   7);
    delayFlavorBox.setSelectedId(1, juce::dontSendNotification);
    delayFlavorBox.onChange = [this]
    {
        const int flavor = delayFlavorBox.getSelectedId() - 1;   // 0 = Off
        proc.setDelayFlavor(flavor);
        applyDelayLayout(flavor);
    };
    addAndMakeVisible(delayFlavorBox);

    delaySyncToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.8f));
    delaySyncToggle.onClick = [this] { proc.setDelaySync(delaySyncToggle.getToggleState()); };
    addAndMakeVisible(delaySyncToggle);
    for (int i = 0; i < 9; ++i) delayDivBox.addItem(delayDivisionName(i), i + 1);
    delayDivBox.setSelectedId(4, juce::dontSendNotification);   // 1/4
    delayDivBox.onChange = [this] { proc.setDelayDivision(delayDivBox.getSelectedId() - 1); };
    addAndMakeVisible(delayDivBox);

    for (int i = 0; i < 8; ++i)
    {
        auto* s = delayKnobs.add(new juce::Slider());
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
        s->setLookAndFeel(&knobLNF);
        s->setColour(juce::Slider::thumbColourId, juce::Colour(0xff3aa0dd));
        s->onValueChange = [this, i] { proc.setDelayParam(i, (float) delayKnobs[i]->getValue()); };
        addAndMakeVisible(s);

        auto* lbl = delayKnobLabels.add(new juce::Label());
        lbl->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setFont(11.0f);
        addAndMakeVisible(lbl);
    }
    applyDelayLayout(0, false);   // initial layout only; syncControlsFromProcessor sets real state

    // --- reverb space: flavor dropdown + remappable knob cluster ---
    reverbCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
    reverbCaption.setFont(10.0f);
    addAndMakeVisible(reverbCaption);

    reverbFlavorBox.addItem("Off",            1);
    reverbFlavorBox.addItem("Velvet Hall",    2);
    reverbFlavorBox.addItem("Modern Space",   3);
    reverbFlavorBox.addItem("Ghost Shimmer",  4);
    reverbFlavorBox.addItem("Iron Plate 140", 5);
    reverbFlavorBox.addItem("Rust Spring 626", 6);
    reverbFlavorBox.setSelectedId(1, juce::dontSendNotification);
    reverbFlavorBox.onChange = [this]
    {
        const int flavor = reverbFlavorBox.getSelectedId() - 1;
        proc.setReverbFlavor(flavor);
        applyReverbLayout(flavor);
    };
    addAndMakeVisible(reverbFlavorBox);

    for (int i = 0; i < 10; ++i)
    {
        auto* s = reverbKnobs.add(new juce::Slider());
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
        s->setLookAndFeel(&knobLNF);
        s->setColour(juce::Slider::thumbColourId, juce::Colour(0xff3aa0dd));
        s->onValueChange = [this, i] { proc.setReverbParam(i, (float) reverbKnobs[i]->getValue()); };
        addAndMakeVisible(s);

        auto* lbl = reverbKnobLabels.add(new juce::Label());
        lbl->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setFont(11.0f);
        addAndMakeVisible(lbl);
    }
    applyReverbLayout(0, false);

    // --- routing selector + signal-flow bar ---
    routingCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
    routingCaption.setFont(10.0f);
    addAndMakeVisible(routingCaption);

    // Tail Type — which FX generates the wet tail that gets reversed into the swell.
    const juce::StringArray routes { "Reverb Swell", "Delay Swell", "Delay -> Reverb Swell",
        "Reverb -> Delay Swell", "Parallel Swell", "Post Color" };
    const juce::StringArray tips {
        "Reverb tail is rendered, then reversed. The classic producer reverse-reverb swell. (Default)",
        "Delay repeats are rendered, then reversed — rhythmic reverse-delay pulls.",
        "Delay feeds reverb, the combined tail is reversed — rhythmic but smoother.",
        "Reverb wash is repeated by the delay, then reversed — more obvious reverse echoes.",
        "Delay and reverb in parallel, summed, then reversed.",
        "Reverb swell with post-reverse colour (advanced)." };
    for (int i = 0; i < routes.size(); ++i) routingBox.addItem(routes[i], i + 1);
    routingBox.setSelectedId((int) RoutingMode::ReverbSwell + 1, juce::dontSendNotification);
    routingBox.onChange = [this]
    {
        proc.setRoutingMode(routingBox.getSelectedId() - 1);
        updateRoutingFlow();
    };
    routingBox.setTooltip(tips[(int) RoutingMode::ReverbSwell]);
    addAndMakeVisible(routingBox);

    routingFlow.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    routingFlow.setJustificationType(juce::Justification::centredLeft);
    routingFlow.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    addAndMakeVisible(routingFlow);
    updateRoutingFlow();

    // --- preset browser ---
    for (auto* b : { &presetPrev, &presetNext, &presetSave, &presetDelete, &presetReset }) addAndMakeVisible(b);
    presetReset.onClick = [this] { proc.resetToDefault(); syncControlsFromProcessor(); rebuildSlotList(); updatePresetStatus(); };
    addAndMakeVisible(presetBox);
    presetStatus.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    presetStatus.setFont(12.0f);
    addAndMakeVisible(presetStatus);

    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) { proc.loadPreset(idx); syncControlsFromProcessor(); updatePresetStatus(); }
    };
    presetPrev.onClick = [this] { proc.prevPreset(); presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification); syncControlsFromProcessor(); updatePresetStatus(); };
    presetNext.onClick = [this] { proc.nextPreset(); presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification); syncControlsFromProcessor(); updatePresetStatus(); };
    presetDelete.onClick = [this]
    {
        const int cur = proc.getCurrentPreset();
        if (! proc.isFactoryPreset(cur) && proc.deleteUserPreset(cur))
        {
            rebuildPresetMenu(); proc.loadPreset(juce::jmax(0, proc.getCurrentPreset()));
            syncControlsFromProcessor(); updatePresetStatus();
        }
    };
    presetSave.onClick = [this]
    {
        saveDialog = std::make_unique<juce::AlertWindow>("Save preset", "Name your preset:",
                                                         juce::MessageBoxIconType::NoIcon);
        saveDialog->addTextEditor("name", "My Preset");
        saveDialog->addButton("Save", 1);
        saveDialog->addButton("Cancel", 0);
        saveDialog->enterModalState(true, juce::ModalCallbackFunction::create([this](int r)
        {
            if (r == 1)
            {
                const auto nm = saveDialog->getTextEditorContents("name").trim();
                if (nm.isNotEmpty())
                {
                    proc.saveUserPreset(nm, "User");
                    rebuildPresetMenu();
                    presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification);
                    updatePresetStatus();
                }
            }
            saveDialog.reset();
        }), false);
    };

    // --- header / branding ---
    logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_detective47_dust1200_BLUE_png,
                                                BinaryData::logo_detective47_dust1200_BLUE_pngSize);
    titleLabel.setText("BACKTRACE", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe6dccb));
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).withExtraKerningFactor(0.08f));
    addAndMakeVisible(titleLabel);
    subtitleLabel.setText("Reverse Evidence Machine  |  Dust Vault FX", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
    subtitleLabel.setFont(11.0f);
    addAndMakeVisible(subtitleLabel);

    // --- Vault bank tabs ---
    for (auto* t : { &sourceTabBtn, &printTabBtn })
    {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(410);
        t->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a5070));
        addAndMakeVisible(t);
    }
    sourceTabBtn.setToggleState(true, juce::dontSendNotification);
    sourceTabBtn.onClick = [this] { setSlotTab(0); };
    printTabBtn.onClick  = [this] { setSlotTab(1); };

    // --- Vault slot list (click to select, drag to export, drop audio to import) ---
    for (int i = 0; i < BacktraceProcessor::kNumSlots; ++i)
    {
        auto* b = slotButtons.add(new SlotButton());
        b->provideDragFile = [this, i] { return proc.exportSlotFile(i, slotTab == 1, proc.vaultCapturesDir()); };
        b->onFilesDropped  = [this, i](const juce::StringArray& f) { importFiles(f, i); };   // drop audio → source slot i
        b->setClickingTogglesState(true);
        b->setRadioGroupId(400);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181b1f));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff7a5e1e));   // active = amber
        b->setColour(juce::TextButton::textColourOnId,  juce::Colour(0xfff2e6c8));
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.55f));
        b->onClick = [this, i] { selectSlot(i); };
        b->onContextMenu = [this, i] { showSlotMenu(i); };
        b->setTooltip("Click to select \xe2\x80\xa2 drag to export \xe2\x80\xa2 right-click to Discard Evidence");
        addAndMakeVisible(b);
    }
    slotClearBtn.setTooltip("Remove this audio from Backtrace. Original files are not deleted.");
    slotClearBtn.onClick = [this] { discardSlot(slotTab == 0 ? proc.getActiveSource() : proc.getActivePrint(), slotTab == 1); };
    slotRenameBtn.onClick = [this]
    {
        const int idx = (slotTab == 0) ? proc.getActiveSource() : proc.getActivePrint();
        const juce::String cur = (slotTab == 0) ? proc.getSourceName(idx) : proc.getPrintName(idx);
        saveDialog = std::make_unique<juce::AlertWindow>("Rename slot", "New name:", juce::MessageBoxIconType::NoIcon);
        saveDialog->addTextEditor("name", cur);
        saveDialog->addButton("OK", 1); saveDialog->addButton("Cancel", 0);
        if (auto* te = saveDialog->getTextEditor("name"))
        {
            auto* dlg = saveDialog.get();
            te->onReturnKey = [dlg] { dlg->exitModalState(1); };
            te->onEscapeKey = [dlg] { dlg->exitModalState(0); };
            te->grabKeyboardFocus(); te->selectAll();
        }
        saveDialog->enterModalState(true, juce::ModalCallbackFunction::create([this, idx](int r)
        {
            if (r == 1)
            {
                const auto nm = saveDialog->getTextEditorContents("name").trim();
                if (slotTab == 0) proc.renameSource(idx, nm); else proc.renamePrint(idx, nm);
                rebuildSlotList();
            }
            saveDialog.reset();
        }), false);
    };
    addAndMakeVisible(slotClearBtn);
    addAndMakeVisible(slotRenameBtn);

    // --- GLOBAL SWELL MACROS — final-swell shaping + output controls ---
    // Tail/Ghost/Ringout re-render the tail (mark stale); Swell/Damage/Reveal/Width/Level are live.
    const juce::StringArray macroNames { "Pull", "Tail", "Ghost", "Damage", "Reveal", "Width", "Level", "Ringout" };
    const juce::StringArray macroTips {
        "PULL - the reverse-swell curve. 50% = natural reverse-reverb rise. Below = more even. Above = dramatic late pull/suck into the landing. The start stays quiet-but-present; the bloom lands at the hit.",
        "Overall delay/reverb trail length and intensity. (Re-render: press Create Swell.)",
        "Adds blur, diffusion, modulation, shimmer influence and haunted atmosphere (not just more reverb). (Re-render: press Create Swell.)",
        "Adds Dust Vault-style age, grit, saturation and degradation. Safely bounded.",
        "Opens or hides the final swell with the Harrison-style filter (low = dark/hidden, high = open/bright). On top of the FINAL FILTER; leave at 100% to let your HPF/LPF endpoints win.",
        "Global mono-safe stereo width for the final printed swell (Dust 12.47-style widening). 0 = mono, 50 = natural, 70 = wide & safe, 100 = max wide.",
        "Swell Level — output trim on the finished swell (-24 to +6 dB). Tames hot sources like snares. Applies to Play / Print / Export / Drag.",
        "Tail Ringout — natural FX decay printed AFTER the landing so the swell doesn't dead-stop. Off = release fade only. (Re-render: press Create Swell.)" };
    for (int i = 0; i < macroNames.size(); ++i)
    {
        auto* s = macroKnobs.add(new juce::Slider());
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setRange(0.0, 1.0, 0.01);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
        s->setLookAndFeel(&knobLNF);
        s->setColour(juce::Slider::thumbColourId, juce::Colour(i >= 6 ? 0xff7ad1ff : 0xffd9a441));   // output controls = blue
        s->setTooltip(macroTips[i]);
        s->onValueChange = [this, i] { pushMacro(i); };
        addAndMakeVisible(s);

        auto* lbl = macroLabels.add(new juce::Label({}, macroNames[i]));
        lbl->setColour(juce::Label::textColourId, juce::Colour(i >= 6 ? 0xff8fc7ee : 0xffe0b85a));
        lbl->setJustificationType(juce::Justification::centred);
        lbl->setFont(juce::Font(i == 0 ? 14.5f : 13.5f, juce::Font::bold));   // Swell is the headline macro
        lbl->setTooltip(macroTips[i]);
        addAndMakeVisible(lbl);
    }
    // Readable units: Level shows dB, Ringout shows % / Off.
    macroKnobs[6]->textFromValueFunction = [](double v) { return juce::String(BacktraceProcessor::levelToDb((float) v), 1) + " dB"; };
    macroKnobs[6]->valueFromTextFunction = [](const juce::String& t) { return (double) juce::jlimit(0.0f, 1.0f, (t.getFloatValue() + 24.0f) / 30.0f); };
    macroKnobs[6]->updateText();
    macroKnobs[7]->textFromValueFunction = [](double v) { return v <= 0.001 ? juce::String("Off") : juce::String(juce::roundToInt(v * 100.0)) + "%"; };
    macroKnobs[7]->updateText();

    setSize(1000, 752);
    rebuildPresetMenu();
    rebuildSlotList();
    syncControlsFromProcessor();
    syncFilterControls();
    updatePresetStatus();
    startTimerHz(30);   // 30 fps — smooth playhead + filter-motion knob animation
}

BacktraceEditor::~BacktraceEditor()
{
    stopTimer();
    // Detach the custom LookAndFeel before it is destroyed (it outlives nothing).
    for (auto* s : macroKnobs)  s->setLookAndFeel(nullptr);
    for (auto* s : delayKnobs)  s->setLookAndFeel(nullptr);
    for (auto* s : reverbKnobs) s->setLookAndFeel(nullptr);
    for (auto* s : { &hpfKnob, &hpfEndKnob, &lpfKnob, &lpfEndKnob, &filterDriveKnob }) s->setLookAndFeel(nullptr);
}

void BacktraceEditor::pushDelayParams()
{
    for (int i = 0; i < delayKnobs.size(); ++i)
        proc.setDelayParam(i, (float) delayKnobs[i]->getValue());
}

// Relabel, re-range and show/hide each knob for the selected flavor. setDefaults
// resets to the flavor defaults and pushes them (user picked a flavor); when false
// the ranges/labels update but values are left for the caller (preset recall).
void BacktraceEditor::applyDelayLayout(int flavor, bool setDefaults)
{
    const auto layout = delayKnobLayout(flavor);
    for (int i = 0; i < delayKnobs.size(); ++i)
    {
        const auto& k = layout[i];
        if (k.used())
        {
            delayKnobs[i]->setRange(k.lo, k.hi, (k.hi > 2.0f ? 1.0 : 0.01));
            if (setDefaults) delayKnobs[i]->setValue(k.def, juce::dontSendNotification);
            delayKnobLabels[i]->setText(k.name, juce::dontSendNotification);
        }
        delayKnobs[i]->setVisible(k.used());
        delayKnobLabels[i]->setVisible(k.used());
    }
    if (setDefaults) pushDelayParams();
    resized();   // re-flow the knob row for the new visible count
}

void BacktraceEditor::pushReverbParams()
{
    for (int i = 0; i < reverbKnobs.size(); ++i)
        proc.setReverbParam(i, (float) reverbKnobs[i]->getValue());
}

void BacktraceEditor::applyReverbLayout(int flavor, bool setDefaults)
{
    const auto layout = reverbKnobLayout(flavor);
    for (int i = 0; i < reverbKnobs.size(); ++i)
    {
        const auto& k = layout[i];
        if (k.used())
        {
            reverbKnobs[i]->setRange(k.lo, k.hi, (k.hi > 2.0f ? 1.0 : 0.01));
            if (setDefaults) reverbKnobs[i]->setValue(k.def, juce::dontSendNotification);
            reverbKnobLabels[i]->setText(k.name, juce::dontSendNotification);
        }
        reverbKnobs[i]->setVisible(k.used());
        reverbKnobLabels[i]->setVisible(k.used());
    }
    if (setDefaults) pushReverbParams();
    resized();
}

void BacktraceEditor::rebuildPresetMenu()
{
    presetBox.clear(juce::dontSendNotification);
    juce::String lastCat;
    for (int i = 0; i < proc.getNumPresets(); ++i)
    {
        const auto cat = proc.getPresetCategory(i);
        if (cat != lastCat) { presetBox.addSectionHeading(cat); lastCat = cat; }
        presetBox.addItem(proc.getPresetName(i), i + 1);   // id = index + 1
    }
    presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification);
}

// Set every control from the processor's current state, without notifying (so a
// preset recall never marks the preset dirty or overwrites the recalled values).
void BacktraceEditor::syncControlsFromProcessor()
{
    const int df = proc.getDelayFlavor();
    delayFlavorBox.setSelectedId(df + 1, juce::dontSendNotification);
    delaySyncToggle.setToggleState(proc.getDelaySync(), juce::dontSendNotification);
    delayDivBox.setSelectedId(proc.getDelayDivision() + 1, juce::dontSendNotification);
    applyDelayLayout(df, false);
    for (int i = 0; i < delayKnobs.size(); ++i)
        if (delayKnobs[i]->isVisible()) delayKnobs[i]->setValue(proc.getDelayParam(i), juce::dontSendNotification);

    const int rf = proc.getReverbFlavor();
    reverbFlavorBox.setSelectedId(rf + 1, juce::dontSendNotification);
    applyReverbLayout(rf, false);
    for (int i = 0; i < reverbKnobs.size(); ++i)
        if (reverbKnobs[i]->isVisible()) reverbKnobs[i]->setValue(proc.getReverbParam(i), juce::dontSendNotification);

    routingBox.setSelectedId(proc.getRoutingMode() + 1, juce::dontSendNotification);
    updateRoutingFlow();

    const float st = proc.getPitchSemitones();
    pitchSlider.setValue(st, juce::dontSendNotification);
    pitchValue.setText((st > 0 ? "+" : "") + juce::String((int) st) + " st", juce::dontSendNotification);

    landToggle.setToggleState(proc.getLandAtSource(), juce::dontSendNotification);

    // swell length + final filter — pick the closest option to the stored bars value
    {
        const float b = proc.getSwellLenBars();
        const double bpb = juce::jmax(1, proc.getHostTsNum());
        const float vals[] = { (float) (0.5 / bpb), (float) (1.0 / bpb), (float) (2.0 / bpb), 1.0f, 2.0f, 4.0f, 8.0f };
        int best = 5; double bestd = 1e9;
        for (int i = 0; i < 7; ++i) { const double d = std::abs(vals[i] - b); if (d < bestd) { bestd = d; best = i + 1; } }
        swellBox.setSelectedId(best, juce::dontSendNotification);
    }
    keepPitchToggle.setToggleState(proc.getKeepPitch(), juce::dontSendNotification);
    syncFilterControls();
    syncFadeControls();
    syncMacros();
}

void BacktraceEditor::updatePresetStatus()
{
    const int i = proc.getCurrentPreset();
    juce::String t = (i < 0) ? juce::String("Default")
                             : proc.getPresetCategory(i) + " / " + proc.getPresetName(i);
    if (proc.isPresetDirty()) t += " *";
    presetStatus.setText(t, juce::dontSendNotification);
    presetDelete.setEnabled(i >= 0 && ! proc.isFactoryPreset(i));
}

void BacktraceEditor::setSlotTab(int tab)
{
    slotTab = tab;
    rebuildSlotList();
}

void BacktraceEditor::selectSlot(int i)
{
    if (slotTab == 0)   // Source bank → drives the source lane + render input
    {
        proc.setActiveSource(i);
        syncControlsFromProcessor();
        refreshWaveform();
        swellCanvas.clearSwell();
        updateSwellSelLabel();
    }
    else                // Print bank → load this printed swell into the bottom lane
    {
        proc.setActivePrint(i);
        proc.regenerateSwell();
        refreshSwellCanvas();
    }
    rebuildSlotList();
}

// "Discard Evidence" — remove one slot's audio from Backtrace (the disk file stays).
void BacktraceEditor::discardSlot(int i, bool print)
{
    if (print) proc.clearPrint(i);
    else      { proc.clearSource(i); refreshWaveform(); }
    swellCanvas.clearSwell();
    updateSwellSelLabel();
    rebuildSlotList();
}

void BacktraceEditor::confirmBulk(const juce::String& title, const juce::String& msg, std::function<void()> action)
{
    saveDialog = std::make_unique<juce::AlertWindow>(title, msg, juce::MessageBoxIconType::QuestionIcon);
    saveDialog->addButton("Discard", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog->addButton("Cancel",  0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog->enterModalState(true, juce::ModalCallbackFunction::create([this, action](int r)
    {
        if (r == 1 && action) action();
        saveDialog.reset();
    }), false);
}

void BacktraceEditor::showSlotMenu(int i)
{
    const bool print = (slotTab == 1);
    const int  status = print ? proc.getPrintStatus(i) : proc.getSourceStatus(i);
    constexpr int N = BacktraceProcessor::kNumSlots;

    juce::PopupMenu m;
    m.addSectionHeader((print ? "Print Slot " : "Source Slot ") + juce::String(i + 1));
    m.addItem("Discard Evidence", status != BacktraceProcessor::SlotEmpty, false,
              [this, i, print] { discardSlot(i, print); });
    m.addSeparator();
    m.addItem("Discard All Sources", [this, N]
    {
        confirmBulk("Discard Evidence", "Discard ALL Source slots from Backtrace?\nOriginal files are not deleted.",
                    [this, N] { for (int s = 0; s < N; ++s) proc.clearSource(s);
                                refreshWaveform(); swellCanvas.clearSwell(); updateSwellSelLabel(); rebuildSlotList(); });
    });
    m.addItem("Discard All Prints", [this, N]
    {
        confirmBulk("Discard Evidence", "Discard ALL Print slots from Backtrace?\nOriginal files are not deleted.",
                    [this, N] { for (int s = 0; s < N; ++s) proc.clearPrint(s);
                                swellCanvas.clearSwell(); updateSwellSelLabel(); rebuildSlotList(); });
    });
    m.addItem("Discard Entire Vault", [this, N]
    {
        confirmBulk("Discard Evidence", "Discard the ENTIRE Vault (all sources and prints)?\nOriginal files are not deleted.",
                    [this, N] { for (int s = 0; s < N; ++s) { proc.clearSource(s); proc.clearPrint(s); }
                                refreshWaveform(); swellCanvas.clearSwell(); updateSwellSelLabel(); rebuildSlotList(); });
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(slotButtons[i]));
}

void BacktraceEditor::rebuildSlotList()
{
    const bool print  = (slotTab == 1);
    const int  active = print ? proc.getActivePrint() : proc.getActiveSource();
    for (int i = 0; i < slotButtons.size(); ++i)
    {
        const int    status = print ? proc.getPrintStatus(i)  : proc.getSourceStatus(i);
        const auto   name   = print ? proc.getPrintName(i)    : proc.getSourceName(i);
        const double secs   = print ? proc.getPrintSeconds(i) : proc.getSourceSeconds(i);
        juce::String t = juce::String(i + 1) + "  ";
        if (status == BacktraceProcessor::SlotEmpty) t += "—";
        else t += name + "   " + juce::String(secs, 1) + "s  " + (print ? "Print" : "Source");
        slotButtons[i]->setButtonText(t);
        slotButtons[i]->setToggleState(i == active, juce::dontSendNotification);
        slotButtons[i]->setMouseCursor(status == BacktraceProcessor::SlotEmpty
                                       ? juce::MouseCursor::NormalCursor
                                       : juce::MouseCursor::DraggingHandCursor);
    }
    const int activeStatus = print ? proc.getPrintStatus(active) : proc.getSourceStatus(active);
    slotClearBtn.setEnabled(activeStatus != BacktraceProcessor::SlotEmpty);
    sourceTabBtn.setToggleState(slotTab == 0, juce::dontSendNotification);
    printTabBtn .setToggleState(slotTab == 1, juce::dontSendNotification);
    updateWorkflowHints();
}

// Keeps the printed-swell empty-state text in step with the workflow.
void BacktraceEditor::updateWorkflowHints()
{
    if (proc.getCaptureLength() <= 0)
        swellCanvas.setEmptyHint("Drag a source into the lane above,\nthen press Create Swell.");
    else
        swellCanvas.setEmptyHint("Source loaded.\nChoose Swell Length and press Create Swell.");
}

int BacktraceEditor::delaySlotIndex(const juce::String& name) const
{
    const auto lay = delayKnobLayout(proc.getDelayFlavor());
    for (int i = 0; i < lay.size(); ++i) if (lay[i].used() && lay[i].name.equalsIgnoreCase(name)) return i;
    return -1;
}

int BacktraceEditor::reverbSlotIndex(const juce::String& name) const
{
    const auto lay = reverbKnobLayout(proc.getReverbFlavor());
    for (int i = 0; i < lay.size(); ++i) if (lay[i].used() && lay[i].name.equalsIgnoreCase(name)) return i;
    return -1;
}

void BacktraceEditor::pushMacro(int idx)
{
    const float v = (float) macroKnobs[idx]->getValue();
    switch (idx)   // GLOBAL SWELL MACROS — shape the final printed swell
    {
        case 0: proc.setMacroSwell(v);  break;
        case 1: proc.setMacroTail(v);   break;
        case 2: proc.setMacroGhost(v);  break;
        case 3: proc.setMacroDamage(v); break;
        case 4: proc.setMacroReveal(v); break;
        case 5: proc.setMacroWidth(v);  break;
        case 6: proc.setMacroLevel(v);  break;   // Swell Level (live output trim)
        case 7: proc.setMacroRingout(v); break;  // Tail Ringout (tail-gen → marks stale)
        default: break;
    }
    refreshSwellCanvas();   // Swell/Reveal/Damage/Width/Level reshape the visible swell live
}

void BacktraceEditor::syncMacros()
{
    macroKnobs[0]->setValue(proc.getMacroSwell(),  juce::dontSendNotification);
    macroKnobs[1]->setValue(proc.getMacroTail(),   juce::dontSendNotification);
    macroKnobs[2]->setValue(proc.getMacroGhost(),  juce::dontSendNotification);
    macroKnobs[3]->setValue(proc.getMacroDamage(), juce::dontSendNotification);
    macroKnobs[4]->setValue(proc.getMacroReveal(), juce::dontSendNotification);
    macroKnobs[5]->setValue(proc.getMacroWidth(),  juce::dontSendNotification);
    macroKnobs[6]->setValue(proc.getMacroLevel(),  juce::dontSendNotification);
    macroKnobs[7]->setValue(proc.getMacroRingout(),juce::dontSendNotification);
}

void BacktraceEditor::updateRoutingFlow()
{
    static const char* flows[] = {
        "src -> reverb tail -> REVERSE -> swell",
        "src -> delay tail -> REVERSE -> swell",
        "src -> delay -> reverb tail -> REVERSE -> swell",
        "src -> reverb -> delay tail -> REVERSE -> swell",
        "src -> [delay + reverb] tail -> REVERSE -> swell",
        "src -> reverb tail -> REVERSE -> swell (+post colour)" };
    routingFlow.setText(flows[juce::jlimit(0, 5, proc.getRoutingMode())], juce::dontSendNotification);
}

void BacktraceEditor::refreshWaveform()
{
    waveform.setCapture(&proc.getCaptureBuffer(),
                        proc.getCaptureLength(),
                        proc.getCaptureSampleRate());
}

// Import drag/drop tracing → debug console only (DBG compiles out in Release).
namespace { void btImportLog(const juce::String& msg) { DBG("[Backtrace import] " << msg); juce::ignoreUnused(msg); } }

bool BacktraceEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    bool interested = false;
    for (auto& f : files) if (WaveformCanvas::isAudioFile(f)) interested = true;
    btImportLog("isInterestedInFileDrag: " + juce::String(files.size()) + " file(s) ["
                + files.joinIntoString(", ") + "] -> " + (interested ? "ACCEPT" : "reject"));
    return interested;
}

void BacktraceEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    btImportLog("filesDropped at (" + juce::String(x) + "," + juce::String(y) + "): ["
                + files.joinIntoString(", ") + "]");
    importFiles(files, -1);
}

bool BacktraceEditor::isInterestedInTextDrag(const juce::String& text)
{
    const bool ok = ! pathsFromDragText(text).isEmpty();
    btImportLog("isInterestedInTextDrag: \"" + text.substring(0, 200) + "\" -> "
                + (ok ? "ACCEPT (path found)" : "reject (no audio path)"));
    return ok;
}

void BacktraceEditor::textDropped(const juce::String& text, int, int)
{
    btImportLog("textDropped: \"" + text.substring(0, 200) + "\"");
    importFromDragText(text);
}

static juce::String btUnescapeXml(juce::String s)
{
    return s.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
            .replace("&quot;", "\"").replace("&apos;", "'");
}

static juce::String btXmlField(const juce::String& text, int from, const juce::String& tag)
{
    const juce::String open = "<" + tag + ">", close = "</" + tag + ">";
    const int a = text.indexOfIgnoreCase(from, open);
    if (a < 0) return {};
    const int b = text.indexOfIgnoreCase(a + open.length(), close);
    if (b < 0) return {};
    return text.substring(a + open.length(), b).trim();
}

// Cubase/Steinberg drag a `vst-xml` document with <filename> (+ optional
// <start>/<end> region). Extract the real file path and import that region.
// Falls back to plain path / file:// text for other hosts.
void BacktraceEditor::importFromDragText(const juce::String& text)
{
    if (text.containsIgnoreCase("<filename>"))
    {
        int pos = 0, imported = 0, skipped = 0, firstTarget = -1;
        juce::File firstF;
        for (;;)
        {
            const int a = text.indexOfIgnoreCase(pos, "<filename>");
            if (a < 0) break;
            const int b = text.indexOfIgnoreCase(a, "</filename>");
            if (b < 0) break;
            const juce::String path = btUnescapeXml(text.substring(a + 10, b).trim());
            pos = b + 11;

            juce::File f(path);
            if (! (f.existsAsFile() && WaveformCanvas::isAudioFile(f.getFullPathName())))
            {
                btImportLog("  vst-xml filename not usable: \"" + path + "\"");
                continue;
            }

            // Best-effort event region from this <region> block (file-sample offsets).
            const int regionEnd = text.indexOfIgnoreCase(b, "</region>");
            const juce::String chunk = (regionEnd > b) ? text.substring(b, regionEnd) : text.substring(b);
            juce::int64 start = 0, len = 0;
            const juce::String sStr = btXmlField(chunk, 0, "start");
            const juce::String eStr = btXmlField(chunk, 0, "end");
            if (sStr.isNotEmpty() && eStr.isNotEmpty())
            {
                const juce::int64 s = sStr.getLargeIntValue(), e = eStr.getLargeIntValue();
                if (e > s && s >= 0) { start = s; len = e - s; }
            }

            int target = proc.firstEmptySource();
            if (target < 0) { ++skipped; continue; }            // source bank full
            btImportLog("  vst-xml import \"" + f.getFileName() + "\" -> source " + juce::String(target + 1)
                        + "  region start=" + juce::String(start) + " len=" + juce::String(len));
            if (proc.importToSlot(target, f, start, len).existsAsFile())
            {
                if (firstTarget < 0) { firstTarget = target; firstF = f; }
                ++imported;
            }
        }

        if (imported > 0)
        {
            proc.setActiveSource(firstTarget);
            onSourceImported(firstF);
            if (skipped > 0)
                statusLabel.setText("Imported " + juce::String(imported) + ", " + juce::String(skipped)
                                    + " skipped (source slots full)", juce::dontSendNotification);
            return;
        }
    }

    importFiles(pathsFromDragText(text), -1);   // fallback: plain paths
}

// Updates the UI after a successful source import (shared by all import paths).
void BacktraceEditor::onSourceImported(const juce::File& f)
{
    if (f.getParentDirectory().isDirectory()) lastBrowseDir = f.getParentDirectory();
    slotTab = 0;   // show the Source bank
    const int a = proc.getActiveSource();
    btImportLog("  LOADED \"" + f.getFileName() + "\" -> source " + juce::String(a + 1)
                + "  len=" + juce::String(proc.getSourceLength(a)) + " smp  ("
                + juce::String(proc.getSourceSeconds(a), 2) + " s)");
    syncControlsFromProcessor();
    refreshWaveform();
    swellCanvas.clearSwell();
    updateSwellSelLabel();
    rebuildSlotList();
    statusLabel.setText("Source loaded - Play Source or create a swell", juce::dontSendNotification);
}

// Resolves drag TEXT into real audio-file paths: Cubase <filename> tags first,
// then plain path / file:// URL lines (Finder and other hosts).
juce::StringArray BacktraceEditor::pathsFromDragText(const juce::String& text)
{
    juce::StringArray out;

    int p = 0;
    for (;;)
    {
        const int a = text.indexOfIgnoreCase(p, "<filename>");
        if (a < 0) break;
        const int b = text.indexOfIgnoreCase(a, "</filename>");
        if (b < 0) break;
        juce::File f(btUnescapeXml(text.substring(a + 10, b).trim()));
        if (f.existsAsFile() && WaveformCanvas::isAudioFile(f.getFullPathName()))
            out.addIfNotAlreadyThere(f.getFullPathName());
        p = b + 11;
    }

    for (auto line : juce::StringArray::fromLines(text))
    {
        line = line.trim().unquoted();
        if (line.isEmpty() || line.startsWithChar('<')) continue;

        juce::File f;
        if (line.startsWithIgnoreCase("file://")) f = juce::URL(line).getLocalFile();
        else if (juce::File::isAbsolutePath(line))  f = juce::File(line);

        if (f.existsAsFile() && WaveformCanvas::isAudioFile(f.getFullPathName()))
            out.addIfNotAlreadyThere(f.getFullPathName());
    }
    return out;
}

void BacktraceEditor::importFiles(const juce::StringArray& files, int slot)
{
    // gather the valid audio files
    juce::StringArray valid;
    for (auto& f : files)
    {
        const bool audio = WaveformCanvas::isAudioFile(f), exists = juce::File(f).existsAsFile();
        if (audio && exists) valid.add(f);
        else btImportLog("  reject \"" + f + "\" (audioExt=" + (audio ? "yes" : "no") + ", exists=" + (exists ? "yes" : "no") + ")");
    }
    btImportLog("importFiles: " + juce::String(valid.size()) + " valid, target=" + (slot < 0 ? "source bank" : "source " + juce::String(slot + 1)));
    if (valid.isEmpty()) { statusLabel.setText("Could not import audio file", juce::dontSendNotification); return; }

    // dropped onto a specific source slot → replace it with the first file
    if (slot >= 0)
    {
        if (proc.importToSlot(slot, juce::File(valid[0])).existsAsFile()) onSourceImported(juce::File(valid[0]));
        else statusLabel.setText("Could not import audio file", juce::dontSendNotification);
        return;
    }

    // general drop → fill empty source slots in order
    int imported = 0, skipped = 0, firstTarget = -1;
    juce::File firstF;
    for (auto& p : valid)
    {
        const int target = proc.firstEmptySource();
        if (target < 0) { ++skipped; continue; }
        if (proc.importToSlot(target, juce::File(p)).existsAsFile())
        {
            if (firstTarget < 0) { firstTarget = target; firstF = juce::File(p); }
            ++imported;
        }
    }
    if (imported > 0)
    {
        proc.setActiveSource(firstTarget);
        onSourceImported(firstF);
        if (skipped > 0)
            statusLabel.setText("Imported " + juce::String(imported) + " files. " + juce::String(skipped)
                                + " skipped (source slots full).", juce::dontSendNotification);
    }
    else statusLabel.setText("Could not import audio file", juce::dontSendNotification);
}

// Disable the controls that touch the render path while a background render runs, and
// show "Rendering…". Play Source stays enabled (read-only on the source). Kept brief —
// renders are sub-second; this just prevents a concurrent mutate of shared render state.
void BacktraceEditor::setRenderingUI(bool busy)
{
    const bool en = ! busy;
    for (auto* b : { &swellButton, &reverseButton, &auditionTailButton, &finalizeButton,
                     &exportButton, &importButton, &sourceTabBtn, &printTabBtn,
                     &presetPrev, &presetNext, &presetSave, &presetReset })
        b->setEnabled(en);
    swellBox.setEnabled(en); keepPitchToggle.setEnabled(en); presetBox.setEnabled(en);
    pitchSlider.setEnabled(en); octUpButton.setEnabled(en); octDownButton.setEnabled(en);
    routingBox.setEnabled(en); delayFlavorBox.setEnabled(en); reverbFlavorBox.setEnabled(en);
    for (auto* s : macroKnobs)  s->setEnabled(en);
    for (auto* s : delayKnobs)  s->setEnabled(en);
    for (auto* s : reverbKnobs) s->setEnabled(en);
    for (auto* b : slotButtons) b->setEnabled(en);
    if (busy) statusLabel.setText("Rendering swell...", juce::dontSendNotification);
}

void BacktraceEditor::refreshSwellCanvas()
{
    if (proc.isRendering()) return;   // the worker owns swellBuffer mid-render — don't read it
    // Ruler bars derived from the ACTUAL buffer duration + tempo, so the timeline,
    // the audio, and the swell-length selector always agree.
    const int    beatsPerBar = proc.getHostTsNum() > 0 ? proc.getHostTsNum() : 4;
    const int    den = proc.getHostTsDen() > 0 ? proc.getHostTsDen() : 4;
    const double bpm = proc.getHostBpm() > 1.0 ? proc.getHostBpm() : 120.0;
    const double secPerBar = (4.0 * beatsPerBar / den) * (60.0 / bpm);
    double bars = proc.getSwellLenBars();
    if (proc.hasSwell() && secPerBar > 0.0 && proc.getSwellRenderSR() > 0.0)
        bars = (proc.getSwellRenderLen() / proc.getSwellRenderSR()) / secPerBar;
    swellCanvas.setMusical(bars, beatsPerBar);

    if (proc.hasSwell())
    {
        swellCanvas.setSwell(&proc.getSwellBuffer(), proc.getSwellRenderLen(), proc.getSwellRenderSR(),
                             proc.getSwellTrimIn(), proc.getSwellTrimOut(),
                             proc.getFadeIn(), proc.getFadeOut());
        swellCanvas.setLanding(proc.getSwellLanding());   // marker at the rise end; ringout follows
    }
    else
        swellCanvas.clearSwell();
    updateSwellSelLabel();
    syncFadeControls();
}

void BacktraceEditor::syncFadeControls()
{
    const double sr = proc.getSwellRenderSR() > 0 ? proc.getSwellRenderSR() : 44100.0;
    fadeInMs.setText (juce::String(proc.getFadeIn()  / sr * 1000.0, 0), juce::dontSendNotification);
    fadeOutMs.setText(juce::String(proc.getFadeOut() / sr * 1000.0, 0), juce::dontSendNotification);
    fadeInCurveBox.setSelectedId (proc.getFadeInCurve()  + 1, juce::dontSendNotification);
    fadeOutCurveBox.setSelectedId(proc.getFadeOutCurve() + 1, juce::dontSendNotification);
    swellCanvas.setFadeCurves(proc.getFadeInCurve(), proc.getFadeOutCurve());
}

void BacktraceEditor::commitFadeMs()
{
    const double sr = proc.getSwellRenderSR() > 0 ? proc.getSwellRenderSR() : 44100.0;
    const int fi = juce::jmax(0, (int) (fadeInMs.getText().getDoubleValue()  * 0.001 * sr));
    const int fo = juce::jmax(0, (int) (fadeOutMs.getText().getDoubleValue() * 0.001 * sr));
    proc.setFades(fi, fo);
    refreshSwellCanvas();
}

void BacktraceEditor::showFadeMenu(int which)
{
    const bool in  = (which == 0);
    const int  cur = in ? proc.getFadeInCurve() : proc.getFadeOutCurve();
    juce::PopupMenu m;
    m.addSectionHeader(in ? "Fade In Curve" : "Fade Out Curve");
    for (int i = 0; i < kNumFadeCurves; ++i)
        m.addItem(juce::String(btFadeCurveName(i)) + " Fade", true, cur == i, [this, in, i]
        {
            if (in) proc.setFadeInCurve(i); else proc.setFadeOutCurve(i);
            swellCanvas.setFadeCurves(proc.getFadeInCurve(), proc.getFadeOutCurve());
            syncFadeControls();
        });
    m.addSeparator();
    m.addItem(in ? "Reset Fade In" : "Reset Fade Out", [this, in] { resetFade(in, ! in); });
    m.addItem("Reset Both Fades", [this] { resetFade(true, true); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&swellCanvas));
}

void BacktraceEditor::resetFade(bool fin, bool fout)
{
    const double sr = proc.getSwellRenderSR() > 0 ? proc.getSwellRenderSR() : 44100.0;
    int f1 = proc.getFadeIn(), f2 = proc.getFadeOut();
    if (fin)  f1 = (int) (sr * 0.005);   // short safety fade-in
    if (fout) f2 = (int) (sr * 0.008);   // short safety fade-out
    proc.setFades(f1, f2);
    refreshSwellCanvas();
}

void BacktraceEditor::updateSwellSelLabel()
{
    if (! proc.hasSwell()) { swellSelLabel.setText("printed swell: (none)", juce::dontSendNotification); return; }
    const double sr  = proc.getSwellRenderSR() > 0 ? proc.getSwellRenderSR() : 44100.0;
    const double sel = (proc.getSwellTrimOut() - proc.getSwellTrimIn()) / sr;
    swellSelLabel.setText("out " + juce::String(sel, 2) + " s    fade "
                          + juce::String(proc.getFadeIn()  / sr * 1000.0, 0) + " / "
                          + juce::String(proc.getFadeOut() / sr * 1000.0, 0) + " ms",
                          juce::dontSendNotification);
}

void BacktraceEditor::syncFilterControls()
{
    filterToggle.setToggleState(proc.getFilterOn(), juce::dontSendNotification);
    filterMotionToggle.setToggleState(proc.getFilterMotion(), juce::dontSendNotification);
    hpfKnob.setValue(proc.getHpfStart(), juce::dontSendNotification);
    hpfEndKnob.setValue(proc.getHpfEnd(), juce::dontSendNotification);
    lpfKnob.setValue(proc.getLpfStart(), juce::dontSendNotification);
    lpfEndKnob.setValue(proc.getLpfEnd(), juce::dontSendNotification);
    filterSlopeBox.setSelectedId(proc.getFilterSlope() + 1, juce::dontSendNotification);
    filterCurveBox.setSelectedId(proc.getFilterCurve() + 1, juce::dontSendNotification);
    filterMotionModeBox.setSelectedId(proc.getFilterMotionMode() + 1, juce::dontSendNotification);
    filterDriveKnob.setValue(proc.getFilterDrive(), juce::dontSendNotification);

    const bool on = proc.getFilterOn();
    const bool motion = on && proc.getFilterMotion();
    hpfKnob.setEnabled(on);    lpfKnob.setEnabled(on);
    hpfEndKnob.setEnabled(motion); lpfEndKnob.setEnabled(motion);
    hpfEndKnob.setAlpha(motion ? 1.0f : 0.4f);
    lpfEndKnob.setAlpha(motion ? 1.0f : 0.4f);
    filterSlopeBox.setEnabled(on); filterDriveKnob.setEnabled(on);
    filterCurveBox.setEnabled(motion); filterMotionModeBox.setEnabled(motion);
    hpfEndLabel.setAlpha(motion ? 1.0f : 0.4f);
    lpfEndLabel.setAlpha(motion ? 1.0f : 0.4f);
    filterMotionToggle.setEnabled(on);
}

juce::String BacktraceEditor::sampleToBarBeat(int sample) const
{
    const double sr  = proc.getCaptureSampleRate();
    const double bpm = proc.getCaptureBpm();
    const int    num = proc.getCaptureTsNum(), den = proc.getCaptureTsDen();
    if (sr <= 0.0 || bpm <= 0.0) return juce::String(sample) + " smp";

    const double secs = (double) sample / sr;
    const double qpb  = 4.0 * (double) num / (double) juce::jmax(1, den);
    const double beatLen = 4.0 / (double) juce::jmax(1, den);
    const double ppq  = secs * bpm / 60.0;             // relative to capture start
    const int bar  = (int) std::floor(ppq / qpb) + 1;
    const int beat = (int) std::floor((ppq - (bar - 1) * qpb) / beatLen) + 1;
    return juce::String(bar) + "." + juce::String(beat);
}

void BacktraceEditor::updateSelection(int trimIn, int trimOut)
{
    proc.setTrim(trimIn, trimOut);
    swellCanvas.clearSwell();   // source trim changed → re-render the swell to hear it
    updateSwellSelLabel();

    const double sr = proc.getCaptureSampleRate();
    const double sel = (sr > 0.0) ? (trimOut - trimIn) / sr : 0.0;
    selectionLabel.setText("A " + sampleToBarBeat(trimIn) + " (" + juce::String(trimIn) + ")"
                         + "    B " + sampleToBarBeat(trimOut) + " (" + juce::String(trimOut) + ")"
                         + "    sel " + juce::String(sel, 2) + " s",
                           juce::dontSendNotification);
}

juce::String BacktraceEditor::transportText() const
{
    const double bpm = proc.getHostBpm();
    const int    num = proc.getHostTsNum(), den = proc.getHostTsDen();
    const juce::String play = proc.isHostPlaying() ? "PLAY" : "STOP";
    return juce::String(bpm, 1) + " BPM   " + juce::String(num) + "/" + juce::String(den)
         + "   " + play + "   " + ppqToBarBeat(proc.getHostPpq(), num, den);
}

juce::String BacktraceEditor::locatorText() const
{
    const int    mode = -1; // not needed; readout reflects detection
    juce::ignoreUnused(mode);

    if (proc.locatorAvailable())
    {
        const double a = proc.getLocStartPpq(), b = proc.getLocEndPpq();
        const int num = proc.getHostTsNum(), den = proc.getHostTsDen();
        const double qpb = 4.0 * (double) num / (double) juce::jmax(1, den);
        const double bars = (qpb > 0.0) ? (b - a) / qpb : 0.0;
        const int    smp  = proc.getPlannedSamples();
        return "loop " + ppqToBarBeat(a, num, den) + " - " + ppqToBarBeat(b, num, den)
             + "   " + juce::String(bars, 1) + " bars   "
             + juce::String(smp) + " smp";
    }
    if (proc.fallbackActive())
        return "host locator unavailable";

    return {};
}

void BacktraceEditor::timerCallback()
{
    transportLabel.setText(transportText(), juce::dontSendNotification);
    locatorLabel.setText(locatorText(), juce::dontSendNotification);

    // fallback length selector only matters when a locator mode has no locators
    const bool showFallback = proc.fallbackActive();
    fallbackLabel.setVisible(showFallback);
    for (auto* b : fallbackButtons) b->setVisible(showFallback);
    locatorLabel.setColour(juce::Label::textColourId,
                           proc.fallbackActive() ? juce::Colour(0xffd9a441)
                                                 : juce::Colours::white.withAlpha(0.85f));

    // arm button + status reflect the capture state
    switch (proc.getCaptureState())
    {
        case 0: // Idle
            armButton.setButtonText("Capture");
            break;
        case 1: // Armed
            armButton.setButtonText("Disarm");
            statusLabel.setText(proc.isHostPlaying() ? "Armed - playback will capture the range"
                                                     : "Armed - press play in host",
                                juce::dontSendNotification);
            break;
        case 2: // Capturing
            armButton.setButtonText("Stop");
            statusLabel.setText("Capturing  " + juce::String(proc.vaultElapsed(), 1) + " s",
                                juce::dontSendNotification);
            break;
        default: break;
    }

    const int aw = proc.getAuditionWhat();
    playSourceButton.setButtonText(aw == 1 ? "Stop" : "Play Source");
    reverseButton.setButtonText   (aw == 2 ? "Stop" : "Play Swell");
    auditionTailButton.setButtonText(aw == 3 ? "Stop" : "Audition Tail");

    // audition playhead — source lane for Play Source, printed lane for Audition Swell
    if (aw == 1)      { waveform.setPlayhead(proc.getTrimIn() + proc.getPlayPos()); swellCanvas.setPlayhead(-1); }
    else if (aw == 2) { swellCanvas.setPlayhead(proc.getSwellTrimIn() + proc.getPlayPos()); waveform.setPlayhead(-1); }
    else              { waveform.setPlayhead(-1); swellCanvas.setPlayhead(-1); }

    // ---- Filter Motion knob animation — DISPLAY-ONLY, follows the internal Audition
    // Swell playhead with the SAME curve as the DSP. Never writes to the parameter
    // state (dontSendNotification); the End knobs stay put as the destination values.
    const bool animateFilter = (aw == 2) && proc.getFilterOn() && proc.getFilterMotion();
    if (animateFilter)
    {
        const int   pl = proc.getPlayLen();
        const float tt = pl > 0 ? juce::jlimit(0.0f, 1.0f, (float) proc.getPlayPos() / (float) pl) : 0.0f;
        const int   curve = proc.getFilterCurve();
        auto shapeC = [curve](float x) -> float   // identical time-curve to applyFilterMotion()
        {
            switch (curve) { case 1: return x * x; case 2: return 1.0f - (1.0f - x) * (1.0f - x);
                             case 3: return x * x * (3.0f - 2.0f * x); default: return x; }
        };
        // Pivot the visual on Peak Land (the landing), following the same Motion Mode as the DSP.
        const float lf = juce::jlimit(0.05f, 0.98f, pl > 0 ? (float) proc.getSwellLanding() / (float) pl : 0.85f);
        float t;
        switch (proc.getFilterMotionMode())
        {
            case 1:  t = (tt <= lf) ? shapeC(tt / lf) : 1.0f - shapeC((tt - lf) / juce::jmax(1.0e-4f, 1.0f - lf)); break;
            case 2:  t = 1.0f - shapeC(tt); break;
            default: t = (tt <= lf) ? shapeC(tt / lf) : 1.0f; break;
        }
        auto interp = [t](float a, float b)   // log-frequency interpolation, like the DSP
        { return std::exp(juce::jmap(t, std::log(juce::jmax(20.0f, a)), std::log(juce::jmax(20.0f, b)))); };
        hpfKnob.setValue(interp(proc.getHpfStart(), proc.getHpfEnd()), juce::dontSendNotification);
        lpfKnob.setValue(interp(proc.getLpfStart(), proc.getLpfEnd()), juce::dontSendNotification);
        filterAnimating = true;
    }
    else if (filterAnimating)   // stopped → restore the editable start values (state never changed)
    {
        filterAnimating = false;
        hpfKnob.setValue(proc.getHpfStart(), juce::dontSendNotification);
        lpfKnob.setValue(proc.getLpfStart(), juce::dontSendNotification);
    }

    // Stale-print warning — a tail-generator param (Tail Type, delay/reverb, pitch,
    // Tail/Ghost) changed but the print has NOT been re-rendered. The cached print is
    // still what plays/exports; pressing Reverse Swell rebuilds it.
    const bool stale = proc.isSwellStale();
    if (stale != lastStaleState)
    {
        lastStaleState = stale;
        printedCaption.setText(stale ? "2  PRINTED SWELL  -  SETTINGS CHANGED: press Create Swell to update"
                                     : "2  PRINTED SWELL   (3  DRAG TO DAW)", juce::dontSendNotification);
        printedCaption.setColour(juce::Label::textColourId, stale ? juce::Colour(0xffe6892e) : juce::Colour(0xffc8a24a));
        swellButton.setColour(juce::TextButton::buttonColourId, stale ? juce::Colour(0xffb5651d) : juce::Colour(0xff34383d));
        if (stale && proc.hasSwell())
            statusLabel.setText("Settings changed - press Create Swell to update", juce::dontSendNotification);
        repaint();
    }

    if (proc.consumePresetLoaded())   // preset load or DAW session restore
    {
        presetBox.setSelectedId(proc.getCurrentPreset() + 1, juce::dontSendNotification);
        syncControlsFromProcessor();
        rebuildSlotList();
        swellCanvas.clearSwell();      // printed swell is stale after a preset change
        updateSwellSelLabel();
    }
    if (proc.consumeSlotsChanged()) rebuildSlotList();

    // Background render: toggle the busy UI on transition, and on completion swap in the
    // freshly rendered swell (+ auto-play if Create Swell asked for it).
    const bool busy = proc.isRendering();
    if (busy != wasRendering) { wasRendering = busy; setRenderingUI(busy); }
    if (proc.consumeRenderDone())
    {
        refreshSwellCanvas();
        updateSwellSelLabel();
        if (proc.consumePendingAutoPlay()) { proc.setSwellMode(true); proc.startReverseAudition(false); }
        statusLabel.setText(proc.hasSwell()
            ? "Swell created - playing. Print / Export / Drag to use it."
            : "Load a source first (drag audio in)", juce::dontSendNotification);
    }
    if (! busy && proc.consumeSwellChanged()) refreshSwellCanvas();
    updatePresetStatus();

    if (proc.captureJustFinished())
    {
        proc.commitCaptureToActiveSource();   // store the take into the active Source slot
        slotTab = 0;
        refreshWaveform();
        swellCanvas.clearSwell();             // new source → printed swell must be re-rendered
        updateSwellSelLabel();
        armButton.setButtonText("Capture");
        rebuildSlotList();
        statusLabel.setText(proc.vaultHitMax()
                                ? "Reached 60 s limit"
                                : "Captured to Source " + juce::String(proc.getActiveSource() + 1),
                            juce::dontSendNotification);
    }
}

void BacktraceEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1012));                          // noir background

    if (logoImage.isValid())
        g.drawImageWithin(logoImage, 12, 16, 176, 44,
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
    else
    {
        g.setColour(juce::Colour(0x55c8a24a));
        g.drawRoundedRectangle(12.0f, 14.0f, 112.0f, 48.0f, 4.0f, 1.0f);
        g.setColour(juce::Colour(0xff8a7a55)); g.setFont(11.0f);
        g.drawText("Detective 47\nLogo", 12, 14, 112, 48, juce::Justification::centred);
    }

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine(76, 0.0f, (float) getWidth());           // header divider

    auto panel = [&g](juce::Rectangle<int> rr)
    {
        g.setColour(juce::Colour(0xff14181b));
        g.fillRoundedRectangle(rr.toFloat(), 6.0f);
        g.setColour(juce::Colour(0x14ffffff));
        g.drawRoundedRectangle(rr.toFloat().reduced(0.5f), 6.0f, 1.0f);
    };
    panel({ 8, 80, 210, 560 });     // left
    panel({ 226, 80, 458, 560 });   // center
    panel({ 692, 80, 300, 560 });   // right
    panel({ 8, 648, 984, 96 });     // macro strip

    g.setColour(juce::Colour(0xffc8a24a));                        // amber section captions
    g.setFont(11.0f);
    g.drawText("DUST VAULT", 18, 86, 180, 14, juce::Justification::left);
    g.drawText("FX",         702, 86, 180, 14, juce::Justification::left);
    g.drawText("GLOBAL SWELL MACROS", 18, 654, 280, 14, juce::Justification::left);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.setFont(10.5f);
    g.drawText("Fast controls for shaping the final printed swell — use the FX panel for detailed delay, reverb, pitch & routing.",
               210, 654, 782, 14, juce::Justification::left);
}

void BacktraceEditor::resized()
{
    // ---- header ----
    titleLabel.setBounds(202, 12, 320, 30);
    subtitleLabel.setBounds(204, 44, 380, 16);
    {
        const int rx = 590;
        presetPrev.setBounds(rx, 14, 22, 26);
        presetBox.setBounds(rx + 26, 14, 150, 26);
        presetNext.setBounds(rx + 180, 14, 22, 26);
        presetSave.setBounds(rx + 208, 14, 62, 26);
        presetDelete.setBounds(rx + 274, 14, 40, 26);
        presetReset.setBounds(rx + 318, 14, 50, 26);
        presetStatus.setBounds(rx, 44, 378, 16);
        presetStatus.setJustificationType(juce::Justification::centredRight);
    }

    // ---- LEFT: Dust Vault (drag-in primary; capture demoted to the bottom) ----
    {
        auto a = juce::Rectangle<int>(8, 80, 210, 560).reduced(8);
        a.removeFromTop(18);   // "DUST VAULT"
        importButton.setBounds(a.removeFromTop(28)); a.removeFromTop(6);   // primary: Import Audio
        {
            auto row = a.removeFromTop(24);   // [Source Slots | Printed Swells] tabs
            sourceTabBtn.setBounds(row.removeFromLeft(row.getWidth() / 2 - 1)); row.removeFromLeft(2);
            printTabBtn.setBounds(row);
        }
        a.removeFromTop(4);
        for (auto* b : slotButtons) b->setBounds(a.removeFromTop(25).reduced(0, 1));
        a.removeFromTop(4);
        {
            auto row = a.removeFromTop(26);
            slotClearBtn.setBounds(row.removeFromLeft(120)); row.removeFromLeft(4);   // "Discard Evidence"
            slotRenameBtn.setBounds(row);
        }

        // --- secondary: advanced live/locator capture ---
        a.removeFromTop(12);
        advancedCaption.setBounds(a.removeFromTop(14));
        a.removeFromTop(2);
        {
            auto grid = a.removeFromTop(24);
            const int cw = grid.getWidth() / 3;
            for (int i = 0; i < modeButtons.size(); ++i)
                modeButtons[i]->setBounds(grid.getX() + i * cw, grid.getY(), cw - 2, 23);
        }
        a.removeFromTop(4);
        {
            auto row = a.removeFromTop(24);
            armButton.setBounds(row.removeFromLeft(row.getWidth() - 100)); row.removeFromLeft(4);
            lockToggle.setBounds(row);
        }
        a.removeFromTop(3);
        transportLabel.setBounds(a.removeFromTop(14));
        locatorLabel.setBounds(a.removeFromTop(14));
    }

    // ---- CENTER: SOURCE CAPTURE lane / PRINTED SWELL lane / filter / output ----
    {
        auto a = juce::Rectangle<int>(226, 80, 458, 560).reduced(8);
        sourceCaption.setBounds(a.removeFromTop(14));
        waveform.setBounds(a.removeFromTop(130));
        {
            auto row = a.removeFromTop(22);
            playSourceButton.setBounds(row.removeFromLeft(96)); row.removeFromLeft(8);
            selectionLabel.setBounds(row);
        }
        a.removeFromTop(2);
        printedCaption.setBounds(a.removeFromTop(14));
        swellCanvas.setBounds(a.removeFromTop(130));
        {
            auto row = a.removeFromTop(14);
            swellSelLabel.setBounds(row.removeFromLeft((int) (row.getWidth() * 0.55f)));
            statusLabel.setBounds(row);
            statusLabel.setJustificationType(juce::Justification::centredRight);
        }
        a.removeFromTop(3);
        {
            auto row = a.removeFromTop(22);   // FADES: In [ms][curve]  Out [ms][curve]  Reset
            fadeInCap.setBounds(row.removeFromLeft(46));
            fadeInMs.setBounds(row.removeFromLeft(36)); row.removeFromLeft(2);
            fadeInCurveBox.setBounds(row.removeFromLeft(78)); row.removeFromLeft(8);
            fadeOutCap.setBounds(row.removeFromLeft(26));
            fadeOutMs.setBounds(row.removeFromLeft(36)); row.removeFromLeft(2);
            fadeOutCurveBox.setBounds(row.removeFromLeft(78)); row.removeFromLeft(6);
            fadeResetBtn.setBounds(row);
        }
        a.removeFromTop(3);
        {
            auto row = a.removeFromTop(22);
            swellCaption.setBounds(row.removeFromLeft(58));   // "Swell Length"
            swellBox.setBounds(row.removeFromLeft(78));
            row.removeFromLeft(6);
            keepPitchToggle.setBounds(row.removeFromLeft(86));
            // zoom + snap on the right
            zoomFitBtn.setBounds(row.removeFromRight(32)); row.removeFromRight(2);
            zoomInBtn.setBounds (row.removeFromRight(22)); row.removeFromRight(2);
            zoomOutBtn.setBounds(row.removeFromRight(22)); row.removeFromRight(6);
            snapBox.setBounds(row.removeFromRight(56));
            snapCaption.setBounds(row.removeFromRight(30));
        }
        a.removeFromTop(3);
        {
            auto row = a.removeFromTop(22);
            filterCaption.setBounds(row.removeFromLeft(46).withTrimmedTop(5));
            // Filter BEFORE Motion (Motion depends on Filter being engaged): Filter on the
            // left, Motion to its right.
            filterMotionToggle.setBounds(row.removeFromRight(64).withTrimmedTop(2));   // Motion (rightmost)
            filterToggle.setBounds(row.removeFromRight(54).withTrimmedTop(2));         // Filter (left of Motion)
            row.removeFromLeft(2);
            filterSlopeBox.setBounds(row.removeFromLeft(68));               // fits "24 dB"
            row.removeFromLeft(3);
            filterMotionModeBox.setBounds(row.removeFromRight(90));         // fixed — fits "Rise+Fall"
            row.removeFromRight(3);
            filterCurveBox.setBounds(row);   // time-curve gets the remaining middle (fits "S-Curve")
        }
        {
            auto fr = a.removeFromTop(56);
            const int kw = fr.getWidth() / 5;
            juce::Slider* fk[] = { &hpfKnob, &hpfEndKnob, &lpfKnob, &lpfEndKnob, &filterDriveKnob };
            juce::Label*  fl[] = { &hpfLabel, &hpfEndLabel, &lpfLabel, &lpfEndLabel, &filterDriveLabel };
            for (int i = 0; i < 5; ++i)
            {
                auto cell = juce::Rectangle<int>(fr.getX() + i * kw, fr.getY(), kw, fr.getHeight());
                fl[i]->setBounds(cell.removeFromTop(13));
                fk[i]->setBounds(cell);
            }
        }
        a.removeFromTop(4);
        {
            auto row = a.removeFromTop(26);   // create + hear + print
            const int bw = row.getWidth() / 3;
            swellButton.setBounds(row.removeFromLeft(bw).reduced(1, 0));
            reverseButton.setBounds(row.removeFromLeft(bw).reduced(1, 0));
            finalizeButton.setBounds(row.reduced(1, 0));
        }
        a.removeFromTop(3);
        {
            auto row = a.removeFromTop(26);   // get it out: drag / export / reveal
            const int bw = row.getWidth() / 3;
            dragZone.setBounds(row.removeFromLeft(bw).reduced(1, 0));
            exportButton.setBounds(row.removeFromLeft(bw).reduced(1, 0));
            revealButton.setBounds(row.reduced(1, 0));
        }
        a.removeFromTop(2);
        routingFlow.setBounds(a.removeFromTop(14));
    }

    // ---- RIGHT: FX ----
    auto layoutKnobs = [](juce::Rectangle<int> area, juce::OwnedArray<juce::Slider>& knobs,
                          juce::OwnedArray<juce::Label>& labels, int cols)
    {
        juce::Array<int> vis;
        for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->isVisible()) vis.add(i);
        if (vis.isEmpty()) return;
        const int rows = (vis.size() + cols - 1) / cols;
        const int cw = area.getWidth() / cols;
        const int ch = area.getHeight() / rows;
        for (int k = 0; k < vis.size(); ++k)
        {
            auto cell = juce::Rectangle<int>(area.getX() + (k % cols) * cw, area.getY() + (k / cols) * ch, cw, ch);
            labels[vis[k]]->setBounds(cell.removeFromTop(14));
            knobs[vis[k]]->setBounds(cell);
        }
    };
    {
        auto a = juce::Rectangle<int>(692, 80, 300, 560).reduced(8);
        a.removeFromTop(18);   // "FX"
        {
            auto row = a.removeFromTop(28);
            pitchCaption.setBounds(row.removeFromLeft(40));
            octDownButton.setBounds(row.removeFromLeft(46));
            pitchValue.setBounds(row.removeFromRight(54));
            octUpButton.setBounds(row.removeFromRight(46));
            row.removeFromLeft(4); row.removeFromRight(4);
            pitchSlider.setBounds(row);
        }
        a.removeFromTop(10);
        delayCaption.setBounds(a.removeFromTop(14));
        {
            auto row = a.removeFromTop(24);
            delayFlavorBox.setBounds(row.removeFromLeft(132)); row.removeFromLeft(6);
            delaySyncToggle.setBounds(row.removeFromLeft(58));
            delayDivBox.setBounds(row.removeFromLeft(66));
        }
        a.removeFromTop(6);
        layoutKnobs(a.removeFromTop(150), delayKnobs, delayKnobLabels, 4);
        a.removeFromTop(10);
        reverbCaption.setBounds(a.removeFromTop(14));
        reverbFlavorBox.setBounds(a.removeFromTop(24).removeFromLeft(150));
        a.removeFromTop(6);
        layoutKnobs(a.removeFromTop(150), reverbKnobs, reverbKnobLabels, 5);
        a.removeFromTop(10);
        routingCaption.setBounds(a.removeFromTop(14));
        routingBox.setBounds(a.removeFromTop(26));
        a.removeFromTop(4);
        auditionTailButton.setBounds(a.removeFromTop(24));
    }

    // ---- BOTTOM: 8-macro strip ----
    {
        auto a = juce::Rectangle<int>(8, 648, 984, 96).reduced(10);
        a.removeFromTop(16);   // "GLOBAL SWELL MACROS"
        const int mw = a.getWidth() / juce::jmax(1, macroKnobs.size());
        for (int i = 0; i < macroKnobs.size(); ++i)
        {
            auto cell = a.removeFromLeft(mw);
            macroLabels[i]->setBounds(cell.removeFromTop(16));
            macroKnobs[i]->setBounds(cell);
        }
    }
}
