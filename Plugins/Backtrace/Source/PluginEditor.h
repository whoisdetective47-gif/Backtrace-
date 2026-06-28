#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/WaveformCanvas.h"
#include "UI/SwellCanvas.h"
#include "UI/DragOutZone.h"
#include "UI/SlotButton.h"
#include "UI/KnobLNF.h"

// =============================================================================
//  Backtrace editor — Phase 1b (DAW Sync Capture).
//
//  Adds the capture-mode selector, a live host-transport readout, the detected
//  locator range (with musical-length fallback), the arm/disarm flow and the
//  Locator Lock toggle on top of the Phase 1 capture/waveform path. The full
//  three-panel face is built out in later phases (Docs/Backtrace_DesignSpec.md).
// =============================================================================
class BacktraceEditor : public juce::AudioProcessorEditor,
                        public  juce::DragAndDropContainer,
                        public  juce::FileDragAndDropTarget,
                        public  juce::TextDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit BacktraceEditor(BacktraceProcessor&);
    ~BacktraceEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Editor-wide catch-all: an audio FILE dropped anywhere on the plugin imports.
    // (Defined in the .cpp with debug logging while drag/drop import is verified.)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Some hosts put a file PATH / file:// URL on the drag as TEXT, not a real file.
    bool isInterestedInTextDrag(const juce::String& text) override;
    void textDropped(const juce::String& text, int x, int y) override;
    static juce::StringArray pathsFromDragText(const juce::String& text);

private:
    void timerCallback() override;
    void refreshWaveform();
    void updateSelection(int trimIn, int trimOut);
    juce::String transportText() const;
    juce::String locatorText() const;
    juce::String sampleToBarBeat(int sample) const;

    BacktraceProcessor& proc;

    juce::OwnedArray<juce::TextButton> modeButtons;   // Manual / 1·2·4 Bars / Loc / Cycle
    juce::OwnedArray<juce::TextButton> fallbackButtons; // 1 / 2 / 4 / 8 bars

    juce::Label       transportLabel;
    juce::Label       locatorLabel;
    juce::Label       fallbackLabel { {}, "fallback" };
    juce::TextButton  armButton     { "Capture" };
    juce::ToggleButton lockToggle   { "Locator Lock" };
    juce::TextButton  finalizeButton { "Print" };
    juce::TextButton  reverseButton { "Play Swell" };
    juce::TextButton  playSourceButton { "Play Source" };
    juce::TextButton  revealButton { "Reveal" };
    juce::ToggleButton landToggle   { "Land at Source" };
    juce::File        lastOutputFile;   // most recent print/export — for Reveal in Finder
    juce::File        lastBrowseDir { juce::File::getSpecialLocation(juce::File::userHomeDirectory) };  // Import chooser start
    juce::Label       swellCaption  { {}, "Swell" };
    juce::ComboBox    swellBox;
    juce::ToggleButton keepPitchToggle { "Keep Pitch" };
    juce::TextButton  swellButton   { "Create Swell" };

    bool lastStaleState = false;     // tracks the printed-swell "out of date" warning
    bool filterAnimating = false;    // HPF/LPF knobs showing a display-only motion sweep
    bool wasRendering = false;       // tracks the background-render busy state
    void setRenderingUI(bool busy);  // disable render-touching controls + show "Rendering…"

    // Tail Type (was "routing") — the tail generator that gets reversed
    juce::Label    routingCaption { {}, "TAIL TYPE" };
    juce::ComboBox routingBox;
    juce::Label    routingFlow;
    void updateRoutingFlow();
    juce::TextButton auditionTailButton { "Audition Tail" };
    juce::TextButton  exportButton  { "Export WAV" };
    juce::TextButton  importButton  { "Import Audio" };
    juce::Label       advancedCaption { {}, "ADVANCED CAPTURE  (DAW)" };
    DragOutZone       dragZone;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Label       pitchCaption { {}, "Pitch" };
    juce::TextButton  octDownButton { "-Oct" };
    juce::TextButton  octUpButton   { "+Oct" };
    juce::Slider      pitchSlider;
    juce::Label       pitchValue;

    // delay (Phase 8)
    juce::Label       delayCaption { {}, "DELAY MACHINE" };
    juce::ComboBox    delayFlavorBox;                        // Off / Reel Echo / Digital Pedal
    juce::ToggleButton delaySyncToggle { "Sync" };
    juce::ComboBox     delayDivBox;
    juce::OwnedArray<juce::Slider> delayKnobs;               // up to 8, remapped per flavor
    juce::OwnedArray<juce::Label>  delayKnobLabels;
    void pushDelayParams();
    void applyDelayLayout(int flavor, bool setDefaults = true);   // relabel/range/show knobs

    // reverb (Phase 9)
    juce::Label       reverbCaption { {}, "REVERB SPACE" };
    juce::ComboBox    reverbFlavorBox;                       // Off / Velvet Hall
    juce::OwnedArray<juce::Slider> reverbKnobs;              // up to 10, remapped per flavor
    juce::OwnedArray<juce::Label>  reverbKnobLabels;
    void pushReverbParams();
    void applyReverbLayout(int flavor, bool setDefaults = true);

    // header / branding (Phase 11B/11C)
    juce::Image logoImage;
    juce::Label titleLabel, subtitleLabel;

    // Vault: two banks (Source Slots / Printed Swells) shown via a tab toggle
    int slotTab = 0;   // 0 = Source Slots, 1 = Printed Swells
    juce::TextButton sourceTabBtn { "Source Slots" }, printTabBtn { "Printed Swells" };
    juce::OwnedArray<SlotButton> slotButtons;   // 8 — show the active tab's bank
    juce::TextButton slotClearBtn  { "Discard Evidence" };
    juce::TextButton slotRenameBtn { "Rename" };
    void rebuildSlotList();
    void setSlotTab(int tab);
    void selectSlot(int i);
    void discardSlot(int i, bool print);                 // "Discard Evidence" — clear one slot
    void showSlotMenu(int i);                            // right-click menu (discard + bulk)
    void confirmBulk(const juce::String& title, const juce::String& msg, std::function<void()> action);
    juce::TooltipWindow tooltipWindow { this };

    // 8-macro bottom strip (Phase 11B) — aliases key FX params per active flavor
    juce::OwnedArray<juce::Slider> macroKnobs;
    juce::OwnedArray<juce::Label>  macroLabels;
    void pushMacro(int idx);
    void syncMacros();
    int  delaySlotIndex(const juce::String& name) const;
    int  reverbSlotIndex(const juce::String& name) const;

    // presets (Phase 11A)
    juce::ComboBox   presetBox;
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "Save As" }, presetDelete { "Del" }, presetReset { "Reset" };
    juce::Label      presetStatus;
    std::unique_ptr<juce::AlertWindow> saveDialog;
    void rebuildPresetMenu();
    void syncControlsFromProcessor();
    void updatePresetStatus();
    juce::Label       statusLabel;
    juce::Label       selectionLabel;
    WaveformCanvas    waveform;

    // Printed-swell editor lane + final filter (this fix pass)
    juce::Label       sourceCaption  { {}, "1  SOURCE   (DRAG AUDIO IN)" };
    juce::Label       printedCaption { {}, "2  PRINTED SWELL   (3  DRAG TO DAW)" };
    SwellCanvas       swellCanvas;
    juce::Label       swellSelLabel;
    juce::Label       snapCaption { {}, "Snap" };
    juce::ComboBox    snapBox;
    juce::TextButton  zoomOutBtn { "-" }, zoomInBtn { "+" }, zoomFitBtn { "Fit" };

    // printed-swell fade controls (numeric length + curve type + reset)
    juce::Label       fadeInCap { {}, "Fade In" }, fadeOutCap { {}, "Out" };
    juce::Label       fadeInMs, fadeOutMs;          // editable ms fields
    juce::ComboBox    fadeInCurveBox, fadeOutCurveBox;
    juce::TextButton  fadeResetBtn { "Reset" };
    void commitFadeMs();
    void syncFadeControls();
    void resetFade(bool fin, bool fout);
    void showFadeMenu(int which);   // right-click curve menu (0 = in, 1 = out)
    juce::Label       filterCaption { {}, "FINAL FILTER" };
    juce::ToggleButton filterToggle       { "Filter" };
    juce::ToggleButton filterMotionToggle { "Motion" };
    juce::Slider      hpfKnob, hpfEndKnob, lpfKnob, lpfEndKnob;
    juce::Label       hpfLabel { {}, "HPF" }, hpfEndLabel { {}, "HPF End" },
                      lpfLabel { {}, "LPF" }, lpfEndLabel { {}, "LPF End" };
    juce::ComboBox    filterSlopeBox, filterCurveBox;   // Harrison-style slope + motion curve
    juce::Slider      filterDriveKnob;                  // Character / Edge
    juce::Label       filterSlopeLabel { {}, "Slope" }, filterCurveLabel { {}, "Curve" },
                      filterDriveLabel { {}, "Char" };
    void refreshSwellCanvas();
    void updateSwellSelLabel();
    void syncFilterControls();
    void updateWorkflowHints();
    void importFiles(const juce::StringArray& files, int slot);   // slot < 0 = active slot
    void importFromDragText(const juce::String& text);            // Cubase vst-xml or plain paths
    void onSourceImported(const juce::File& f);                   // shared post-import UI refresh

    KnobLNF           knobLNF;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BacktraceEditor)
};
