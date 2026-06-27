#pragma once
#include <JuceHeader.h>
#include "Vault/CaptureEngine.h"
#include "Audio/SyncCapture.h"
#include "Audio/ReverseEngine.h"
#include "DSP/ModernPitchShifter.h"
#include "DSP/DelayMachine.h"
#include "DSP/TapeEcho.h"
#include "DSP/PedalDigital.h"
#include "DSP/MagneticDrum.h"
#include "DSP/TapeWitness.h"
#include "DSP/ColdRack.h"
#include "DSP/VaultDelay.h"
#include "DSP/ReverbSpace.h"
#include "DSP/VelvetHall.h"
#include "DSP/ModernSpace.h"
#include "DSP/ShimmerVerb.h"
#include "DSP/PlateVerb.h"
#include "DSP/SpringVerb.h"
#include "DSP/TimeStretch.h"
#include "DSP/StereoWidener.h"   // shared Common/DSP — Dust 12.47 mono-safe widener
#include "UI/FadeCurve.h"

// =============================================================================
//  Backtrace — capture-based reverse FX workstation (Dust Vault engine).
//
//  Phase 1: Manual Capture. Records the audio passing through this instance
//  into the Vault buffer, then prints it to a WAV slot. Waveform display,
//  trim locators, reverse, FX, DAW Sync and the Library arrive in later phases
//  (see Docs/Backtrace_DesignSpec.md).
// =============================================================================
// Tail Type — WHICH FX generates the forward wet tail that is then reversed into the
// swell. (Internal enum name kept as RoutingMode for code stability; UI label is
// "Tail Type".) Every type renders a forward wet tail, then reverses it — the only
// difference is the tail generator. Post-reverse "colour" FX are a separate concept.
enum class RoutingMode
{
    ReverbSwell = 0,      // reverb tail → reverse            (DEFAULT — classic producer move)
    DelaySwell,           // delay tail → reverse
    DelayReverbSwell,     // delay → reverb tail → reverse
    ReverbDelaySwell,     // reverb → delay tail → reverse
    ParallelSwell,        // [delay ∥ reverb] tail → reverse
    PostColor,            // reverb swell + post-reverse colour (advanced; reverb tail for now)
    NumModes
};

inline juce::String routingModeName(int m)
{
    static const char* n[] = { "reverb_swell", "delay_swell", "delay_reverb_swell",
        "reverb_delay_swell", "parallel_swell", "post_color" };
    return n[juce::jlimit(0, 5, m)];
}

inline int routingModeFromName(const juce::String& s)
{
    for (int i = 0; i < 6; ++i) if (routingModeName(i) == s) return i;
    // migrate legacy routing names → nearest Tail Type
    if (s == "delay_to_reverb")        return (int) RoutingMode::DelayReverbSwell;
    if (s == "reverb_to_delay")        return (int) RoutingMode::ReverbDelaySwell;
    if (s == "parallel")               return (int) RoutingMode::ParallelSwell;
    if (s == "feedback_verb_to_delay") return (int) RoutingMode::ReverbDelaySwell;
    return (int) RoutingMode::ReverbSwell;   // reverse_tail / reverse_before_fx / unknown → default
}

// Tempo-sync divisions for the delay (quarter-note multiples). 0..8.
inline double delayDivisionQuarters(int d)
{
    static const double q[] = { 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
    return q[juce::jlimit(0, 8, d)];
}
inline juce::String delayDivisionName(int d)
{
    static const char* n[] = { "1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16" };
    return n[juce::jlimit(0, 8, d)];
}

class BacktraceProcessor : public juce::AudioProcessor
{
public:
    BacktraceProcessor();
    ~BacktraceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Detective 47s Backtrace"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- Vault: separate Source and Print banks ----
    enum SlotStatus { SlotEmpty = 0, SlotSource = 1, SlotProcessed = 2, SlotPrinted = 3 };
    static constexpr int kNumSlots = 8;
    struct VaultSlot { juce::AudioBuffer<float> buffer; int length = 0; juce::String name;
                       int status = SlotEmpty; double sr = 44100.0; int routing = 0; };

    // Source bank — imported/captured originals that feed the reverse-swell engine.
    int  getActiveSource() const        { return activeSource; }
    void setActiveSource(int i);
    juce::String getSourceName(int i) const { return slotValid(i) ? sourceSlots[(size_t) i].name : juce::String(); }
    int  getSourceLength(int i) const   { return slotValid(i) ? sourceSlots[(size_t) i].length : 0; }
    int  getSourceStatus(int i) const   { return slotValid(i) ? sourceSlots[(size_t) i].status : (int) SlotEmpty; }
    double getSourceSeconds(int i) const{ return slotValid(i) && sourceSlots[(size_t) i].sr > 0 ? sourceSlots[(size_t) i].length / sourceSlots[(size_t) i].sr : 0.0; }
    void clearSource(int i);
    void renameSource(int i, const juce::String& n) { if (slotValid(i)) { sourceSlots[(size_t) i].name = n; slotsChangedFlag.store(true); } }
    int  firstEmptySource() const;

    // Print bank — rendered reverse swells (outputs).
    int  getActivePrint() const         { return activePrint; }
    void setActivePrint(int i);          // selecting a print loads it into the printed-swell lane
    juce::String getPrintName(int i) const { return slotValid(i) ? printSlots[(size_t) i].name : juce::String(); }
    int  getPrintLength(int i) const    { return slotValid(i) ? printSlots[(size_t) i].length : 0; }
    int  getPrintStatus(int i) const    { return slotValid(i) ? printSlots[(size_t) i].status : (int) SlotEmpty; }
    double getPrintSeconds(int i) const { return slotValid(i) && printSlots[(size_t) i].sr > 0 ? printSlots[(size_t) i].length / printSlots[(size_t) i].sr : 0.0; }
    int  getPrintRouting(int i) const   { return slotValid(i) ? printSlots[(size_t) i].routing : 0; }
    void clearPrint(int i);
    void renamePrint(int i, const juce::String& n) { if (slotValid(i)) { printSlots[(size_t) i].name = n; slotsChangedFlag.store(true); } }
    int  firstEmptyPrint() const;

    void commitCaptureToActiveSource();  // copy the capture buffer into the active source slot
    bool consumeSlotsChanged() { return slotsChangedFlag.exchange(false); }
    int  getLastStoredSlot() const { return lastStoredSlot; }   // print slot a Print landed in (-1 if none)

    // Playback position for the audition playhead (audio-thread published).
    int  getPlayPos() const { return playPos.load(); }
    int  getPlayLen() const { return playLen.load(); }

    // ---- Vault capture ----
    struct VaultCapture { juce::File file; juce::String name; double seconds = 0.0; };

    // Capture is driven by the DAW-sync state machine (Phase 1b). Manual mode
    // starts/stops on arm/disarm; sync modes wait for the host transport.
    void armCapture()        { syncCapture.arm(); }
    void disarmCapture()     { syncCapture.disarm(); }
    int  getCaptureState() const { return (int) syncCapture.getState(); }
    bool captureJustFinished()   { return syncCapture.justFinished(); }
    double vaultElapsed()  const { return capture.elapsedSeconds(); }
    bool   vaultHitMax()   const { return capture.hitMax(); }

    // Sync-capture controls + transport readout (forwarded to the editor).
    void setCaptureMode(int m)       { syncCapture.setMode(m); }
    void setFallbackBars(int bars)   { syncCapture.setFallbackBars(bars); }
    void setLocatorLock(bool on)     { syncCapture.setLocatorLock(on); }
    double getHostBpm()        const { return syncCapture.getBpm(); }
    int    getHostTsNum()      const { return syncCapture.getTsNumerator(); }
    int    getHostTsDen()      const { return syncCapture.getTsDenominator(); }
    bool   isHostPlaying()     const { return syncCapture.isHostPlaying(); }
    double getHostPpq()        const { return syncCapture.getPpq(); }
    bool   locatorAvailable()  const { return syncCapture.locatorAvailable(); }
    bool   fallbackActive()    const { return syncCapture.fallbackActive(); }
    double getLocStartPpq()    const { return syncCapture.getLocStartPpq(); }
    double getLocEndPpq()      const { return syncCapture.getLocEndPpq(); }
    int    getPlannedSamples() const { return syncCapture.getPlannedSamples(); }

    // ---- Trim locators (Phase 2) — feed reverse/print in later phases ----
    void setTrim(int inSample, int outSample) { trimIn.store(inSample); trimOut.store(outSample); tailStartSamp.store(-1); swellStale.store(true); }
    int  getTrimIn()  const { return trimIn.load(); }
    int  getTrimOut() const { return trimOut.load(); }

    // Source End / Tail Start — boundary (samples from the trim start) where the source
    // ends and the generated FX tail begins. The reverse swell is built from the wet
    // tail AFTER this. -1 = auto (= end of the trimmed source); manual override wins.
    int  getTailStart() const { const int sel = trimOut.load() - trimIn.load();
                                const int t = tailStartSamp.load();
                                return (t < 0) ? sel : juce::jlimit(0, juce::jmax(0, sel), t); }
    void setTailStart(int samplesFromTrimStart) { tailStartSamp.store(samplesFromTrimStart); markTailDirty(); }
    bool isTailStartAuto() const { return tailStartSamp.load() < 0; }
    // Tempo/metre of the captured take (set when capture finishes).
    double getCaptureBpm()   const { return syncCapture.finBpm(); }
    int    getCaptureTsNum() const { return syncCapture.finTsNum(); }
    int    getCaptureTsDen() const { return syncCapture.finTsDen(); }

    // ---- Pitch wheel — applied to the source BEFORE FX → tail-generator param ----
    void  setPitchSemitones(float st) { pitchSemitones.store(st); markTailDirty(); }
    float getPitchSemitones()  const  { return pitchSemitones.load(); }

    // ---- Delay — generates the wet tail → tail-generator params (mark stale) ----
    void  setDelayFlavor(int f) { delayFlavor.store(f); markTailDirty(); }   // 0 = Off, 1 = Reel Echo, ...
    int   getDelayFlavor() const { return delayFlavor.load(); }
    void  setDelayParam(int i, float v) { if ((unsigned) i < 8) { delayParam[(size_t) i].store(v); markTailDirty(); } }
    float getDelayParam(int i) const    { return ((unsigned) i < 8) ? delayParam[(size_t) i].load() : 0.0f; }
    void  setDelaySync(bool on)      { delaySync.store(on); markTailDirty(); }   // lock delay time to host tempo
    bool  getDelaySync() const       { return delaySync.load(); }
    void  setDelayDivision(int d)    { delayDivision.store(d); markTailDirty(); } // 0..8: 1/1..1/16
    int   getDelayDivision() const   { return delayDivision.load(); }

    // ---- Reverb — generates the wet tail → tail-generator params (mark stale) ----
    void  setReverbFlavor(int f) { reverbFlavor.store(f); markTailDirty(); }   // 0 = Off, 1 = Velvet Hall, ...
    int   getReverbFlavor() const { return reverbFlavor.load(); }
    void  setReverbParam(int i, float v) { if ((unsigned) i < 10) { reverbParam[(size_t) i].store(v); markTailDirty(); } }
    float getReverbParam(int i) const    { return ((unsigned) i < 10) ? reverbParam[(size_t) i].load() : 0.0f; }

    // ---- Tail Type — which FX generates the tail → tail-generator param ----
    void setRoutingMode(int m) { routingMode.store(m); markTailDirty(); }
    int  getRoutingMode() const { return routingMode.load(); }

    // ---- Global Swell Macros (Phase 24) — shape the FINAL printed swell ----
    // Swell/Damage/Reveal/Width are cheap final-stage controls (no bloom re-render);
    // Tail/Ghost modulate the FX bloom and so mark the swell stale (re-render).
    void  setMacroSwell (float v) { macroSwell.store(juce::jlimit(0.0f,1.0f,v));  reshapeSwellMacro();   markDirty(); }      // live
    void  setMacroTail  (float v) { macroTail.store(juce::jlimit(0.0f,1.0f,v));   markTailDirty(); }                          // tail-gen
    void  setMacroGhost (float v) { macroGhost.store(juce::jlimit(0.0f,1.0f,v));  markTailDirty(); }                          // tail-gen
    void  setMacroDamage(float v) { macroDamage.store(juce::jlimit(0.0f,1.0f,v)); swellChangedFlag.store(true); markDirty(); } // live
    void  setMacroReveal(float v) { macroReveal.store(juce::jlimit(0.0f,1.0f,v)); swellChangedFlag.store(true); markDirty(); } // live
    void  setMacroWidth (float v) { macroWidth.store(juce::jlimit(0.0f,1.0f,v));  swellChangedFlag.store(true); markDirty(); } // live
    float getMacroSwell()  const { return macroSwell.load(); }
    float getMacroTail()   const { return macroTail.load(); }
    float getMacroGhost()  const { return macroGhost.load(); }
    float getMacroDamage() const { return macroDamage.load(); }
    float getMacroReveal() const { return macroReveal.load(); }
    float getMacroWidth()  const { return macroWidth.load(); }
    void  reshapeSwellMacro();   // re-apply the Swell envelope to the cached raw bloom (cheap, no re-render)

    // ---- Reverse Swell (locator-capture workflow) ----
    void setSwellBars(int b) { setSwellLenBars((float) b); }
    int  getSwellBars() const { return (int) std::lround(swellLenBars.load()); }
    void setSwellLenBars(float b) { if (b != swellLenBars.load()) swellStale.store(true); swellLenBars.store(b); }
    float getSwellLenBars() const { return swellLenBars.load(); }
    void setSwellMode(bool m) { if (m != swellMode.load()) swellStale.store(true); swellMode.store(m); }
    bool getSwellMode() const { return swellMode.load(); }
    void setKeepPitch(bool b) { if (b != keepPitch.load()) swellStale.store(true); keepPitch.store(b); }
    bool getKeepPitch() const { return keepPitch.load(); }

    // ---- Printed-swell editor stage (post-render trim / fades / filter) ----
    // The render (stage 1) fills swellBuffer; trim+fades+filter (stage 2) shape it
    // identically for audition / print / export / drag.
    void regenerateSwell();                       // force a fresh stage-1 render + reset edits
    // Build the print only if none exists yet. A STALE print (tail-gen param changed)
    // is NOT auto-rebuilt — the user presses Reverse Swell — so Audition/Print/Export
    // all use the same cached print and the UI shows an "out of date" warning.
    void ensureSwell() { if (! swellValid.load()) regenerateSwell(); }
    bool   hasSwell() const             { return swellValid.load(); }
    bool   consumeSwellChanged()        { return swellChangedFlag.exchange(false); }
    const juce::AudioBuffer<float>& getSwellBuffer() const { return swellBuffer; }
    int    getSwellRenderLen() const    { return swellRenderLen; }
    double getSwellRenderSR()  const    { return swellRenderSR; }

    void setSwellTrim(int a, int b)     { swellTrimIn.store(a); swellTrimOut.store(b); }
    int  getSwellTrimIn()  const        { return swellTrimIn.load(); }
    int  getSwellTrimOut() const        { return swellTrimOut.load(); }
    // Fades + final filter are FINAL-EDIT params — live (applyEdit re-runs), preset-dirty only.
    void setFades(int inSamps, int outSamps) { fadeInLen.store(inSamps); fadeOutLen.store(outSamps); markDirty(); }
    int  getFadeIn()  const             { return fadeInLen.load(); }
    int  getFadeOut() const             { return fadeOutLen.load(); }
    void setFadeInCurve(int c)          { fadeInCurve.store(c); markDirty(); }
    int  getFadeInCurve() const         { return fadeInCurve.load(); }
    void setFadeOutCurve(int c)         { fadeOutCurve.store(c); markDirty(); }
    int  getFadeOutCurve() const        { return fadeOutCurve.load(); }

    void  setFilterOn(bool on)          { filterOn.store(on); markDirty(); }
    bool  getFilterOn() const           { return filterOn.load(); }
    void  setFilterMotion(bool on)      { filterMotion.store(on); markDirty(); }
    bool  getFilterMotion() const       { return filterMotion.load(); }
    void  setHpfStart(float hz)         { hpfStartHz.store(hz); markDirty(); }
    float getHpfStart() const           { return hpfStartHz.load(); }
    void  setHpfEnd(float hz)           { hpfEndHz.store(hz); markDirty(); }
    float getHpfEnd() const             { return hpfEndHz.load(); }
    void  setLpfStart(float hz)         { lpfStartHz.store(hz); markDirty(); }
    float getLpfStart() const           { return lpfStartHz.load(); }
    void  setLpfEnd(float hz)           { lpfEndHz.store(hz); markDirty(); }
    float getLpfEnd() const             { return lpfEndHz.load(); }

    // Harrison-style filter character (live, final-edit).
    void  setFilterSlope(int s)         { filterSlope.store(s); markDirty(); }   // 0 = 12 dB/oct, 1 = 24 dB/oct
    int   getFilterSlope() const        { return filterSlope.load(); }
    void  setFilterCurve(int c)         { filterCurve.store(c); markDirty(); }   // motion time-curve 0..3
    int   getFilterCurve() const        { return filterCurve.load(); }
    void  setFilterDrive(float d)       { filterDrive.store(juce::jlimit(0.0f,1.0f,d)); markDirty(); } // Character/Edge
    float getFilterDrive() const        { return filterDrive.load(); }

    // ---- Presets (Phase 11A) ----
    struct PresetEntry { juce::String name, category; bool factory = true; juce::var state; juce::File file; };

    juce::var getStateVar() const;          // full creative state → JSON (stable string IDs)
    void      setStateVar(const juce::var&); // restore (missing keys fall back safely)

    int  getNumPresets() const            { return (int) presets.size(); }
    juce::String getPresetName(int i) const     { return juce::isPositiveAndBelow(i, (int) presets.size()) ? presets[(size_t) i].name : juce::String(); }
    juce::String getPresetCategory(int i) const { return juce::isPositiveAndBelow(i, (int) presets.size()) ? presets[(size_t) i].category : juce::String(); }
    bool isFactoryPreset(int i) const     { return juce::isPositiveAndBelow(i, (int) presets.size()) ? presets[(size_t) i].factory : true; }
    int  getCurrentPreset() const         { return currentPreset; }
    bool isPresetDirty() const            { return dirty.load(); }
    // Final-edit change: preset modified, but the printed swell stays valid (live).
    void markDirty()                      { if (! suppressDirty) dirty.store(true); }
    // Tail-generator change: the printed swell must be re-rendered (press Reverse Swell).
    void markTailDirty()                  { if (! suppressDirty) dirty.store(true); swellStale.store(true); }
    bool isSwellStale() const             { return swellStale.load() && swellValid.load(); }   // print exists but out of date

    void loadPreset(int i);
    void resetToDefault();   // restore default FX state (does NOT clear Vault audio)
    void nextPreset() { if (! presets.empty()) loadPreset((currentPreset + 1) % (int) presets.size()); }
    void prevPreset() { if (! presets.empty()) loadPreset((currentPreset + (int) presets.size() - 1) % (int) presets.size()); }
    juce::File saveUserPreset(const juce::String& name, const juce::String& category);
    bool deleteUserPreset(int i);
    juce::File presetsDir() const;
    bool consumePresetLoaded() { return presetLoadedFlag.exchange(false); }

    // ---- Reverse audition (Phase 3) ----
    void setLandAtSource(bool on)    { landAtSource.store(on); markDirty(); }
    bool getLandAtSource()     const { return landAtSource.load(); }
    void startReverseAudition(bool forceRegen = false);  // (re)build printed swell → play
    void startTailAudition();        // play the FORWARD wet tail (pre-reverse) so the user vets the FX
    void startSourceAudition();      // play the raw selected source region (active slot)
    void stopReverseAudition()       { playPlaying.store(false); }
    bool isReverseAuditioning() const { return playPlaying.load(); }
    int  getAuditionWhat()     const { return playPlaying.load() ? auditionWhat.load() : 0; }  // 0 none, 1 source, 2 swell

    // Slot-as-file export (drag any occupied slot's audio out verbatim).
    juce::File exportSlotFile(int i, bool print, const juce::File& dir);

    // Renders the reversed/processed selection (honoring Land at Source) into a
    // NEW Vault slot — WAV + *.btmeta.json — distinct from the source capture.
    juce::File vaultPrintProcessed();
    juce::File vaultCapturesDir() const;
    void       vaultOpenFolder();
    std::vector<VaultCapture>& vaultGetCaptures() { return vaultCaptures; }

    // ---- Sample Library (Phase 5b/5c) ----
    juce::File vaultLibraryDir() const;       // persistent collection / evidence folder
    juce::File vaultExportToLibrary();        // render processed result into the Library
    juce::File importToSlot(int slot, const juce::File& wav,
                            juce::int64 startSample = 0, juce::int64 numSamples = 0);  // drag-in; optional region
    juce::File importFromLibrary(const juce::File& wav);       // load a file into the active slot
    juce::File vaultRenderForDrag();          // render processed result for drag-and-drop export

    // Active source-slot audio for the source waveform + render input (message thread).
    const juce::AudioBuffer<float>& getCaptureBuffer() const { return sourceSlots[(size_t) activeSource].buffer; }
    int    getCaptureLength()     const { return sourceSlots[(size_t) activeSource].length; }
    double getCaptureSampleRate() const { const double s = sourceSlots[(size_t) activeSource].sr; return s > 0 ? s : currentSR; }

private:
    juce::String resultBaseName() const;   // "Backtrace_<preset>_<bars>" for printed-swell files
    void writeSidecar(const juce::File& wav, int samples, bool reversed);   // *.btmeta.json provenance
    juce::File writeProcessed(const juce::File& dir, const juce::String& prefix, bool storeInSlot = false);
    int  renderProcessed(juce::AudioBuffer<float>& dest, int total);  // central routing render, returns length
    int  fillSource(juce::AudioBuffer<float>& dest, bool reverse);    // load selection forward or reversed
    void applyPitch(juce::AudioBuffer<float>& buf, int len, float semitones);  // offline, latency-compensated
    void applyDelay(juce::AudioBuffer<float>& buf, int total, bool wetOnly = false, double fillSec = 0.0);
    void applyReverb(juce::AudioBuffer<float>& buf, int total, bool wetOnly = false, double fillSec = 0.0,
                     float decayOverride = -1.0f, float density = 0.0f);
    int  renderSwell(juce::AudioBuffer<float>& dest, int swellLen);   // wet-only reversed swell, end-aligned
    int  renderTail (juce::AudioBuffer<float>& dest);                 // FORWARD wet tail (Audition Tail, pre-reverse)
    void applySwellFX(juce::AudioBuffer<float>& buf, int total, int mode, double fillSec,
                      float decayOverride = -1.0f, float density = 0.0f);  // routed topology, length-aware
    int  swellLengthSamples() const;                                  // bars → samples (captured tempo)
    int  applyEdit(juce::AudioBuffer<float>& dest);                   // stage-2: trim + fades + filter
    void applyFilterMotion(juce::AudioBuffer<float>& buf, int len);   // HPF/LPF (static or start→end sweep)
    void applyParallel(juce::AudioBuffer<float>& buf, int total, bool hasDelay, bool hasReverb);
    void applyFeedbackChain(juce::AudioBuffer<float>& buf, int total, bool hasDelay, bool hasReverb);
    int  baseLen() const;       // processed source length (Land at Source honored)
    int  tailLen() const;       // extra samples for the delay ring-out
    int  reverbTailLen() const; // extra samples for the reverb decay
    int  extraTail() const;     // combined FX tail, min 2 s, capped 45 s
    void applyOutputSafety(juce::AudioBuffer<float>& buf, int total);  // global DC block + soft ceiling

    CaptureEngine             capture;
    SyncCapture               syncCapture;
    std::vector<VaultCapture> vaultCaptures;

    std::array<VaultSlot, kNumSlots> sourceSlots, printSlots;
    int               activeSource = 0, activePrint = 0;
    bool              viewingPrint = false;   // printed-swell lane shows a print slot vs the temp render
    int               lastStoredSlot = -1;
    std::atomic<bool> slotsChangedFlag { false };
    bool slotValid(int i) const { return juce::isPositiveAndBelow(i, kNumSlots); }

    std::atomic<int> trimIn { 0 }, trimOut { 0 };

    // Reverse audition playback (message thread builds, audio thread plays).
    juce::AudioBuffer<float> playBuffer;
    std::atomic<bool> playPlaying { false };
    std::atomic<int>  playPos { 0 }, playLen { 0 };
    std::atomic<int>  auditionWhat { 0 };   // 0 none, 1 source, 2 swell
    std::atomic<bool> landAtSource { false };
    std::atomic<int>   routingMode { (int) RoutingMode::ReverbSwell };   // Tail Type (default Reverb Swell)
    std::atomic<float> swellLenBars { 2.0f };
    std::atomic<bool>  swellMode { true };   // core workflow renders the fit-to-length swell
    std::atomic<bool>  keepPitch { true };   // time-fit preserves source pitch (vs creative pitch-drop)

    // Global Swell Macros (Phase 24) — default Swell 100% so a fresh swell is dramatic.
    std::atomic<float> macroSwell  { 1.0f };   // reverse-rise intensity (THE Backtrace control)
    std::atomic<float> macroTail   { 0.35f };  // overall delay/reverb trail length + amount
    std::atomic<float> macroGhost  { 0.0f };   // diffusion / modulation / shimmer / blur
    std::atomic<float> macroDamage { 0.0f };   // Dust-Vault saturation / age / degradation
    std::atomic<float> macroReveal { 1.0f };   // final filter openness (1 = open/bright, 0 = dark)
    std::atomic<float> macroWidth  { 0.7f };   // mono-safe final stereo width

    StereoWidener swellWidener;   // Width macro — Dust 12.47 mono-safe final widener

    // Printed-swell editor stage (stage-1 render result + stage-2 edit params).
    juce::AudioBuffer<float> swellBuffer;       // raw bloom × Swell-macro envelope (what's drawn/edited)
    juce::AudioBuffer<float> swellRawBuffer;    // raw bloom BEFORE the Swell envelope (for cheap reshaping)
    int    swellRenderLen = 0;
    double swellRenderSR  = 44100.0;
    std::atomic<bool> swellValid { false };
    std::atomic<bool> swellStale { true };
    std::atomic<bool> swellChangedFlag { false };
    std::atomic<int>  tailStartSamp { -1 };   // Source End / Tail Start (samples from trim start; -1 = auto)
    std::atomic<int>  swellTrimIn { 0 }, swellTrimOut { 0 };
    std::atomic<int>  fadeInLen { 0 }, fadeOutLen { 0 };
    std::atomic<int>  fadeInCurve { FadeLinear }, fadeOutCurve { FadeLinear };
    std::atomic<bool>  filterOn { false }, filterMotion { false };
    std::atomic<float> hpfStartHz { 20.0f }, hpfEndHz { 20.0f };
    std::atomic<float> lpfStartHz { 20000.0f }, lpfEndHz { 20000.0f };
    std::atomic<int>   filterSlope { 1 };       // 0 = 12 dB/oct, 1 = 24 dB/oct (default = strong)
    std::atomic<int>   filterCurve { 0 };       // motion time-curve: 0 Linear 1 Exp 2 Log 3 S
    std::atomic<float> filterDrive { 0.25f };   // Harrison Character/Edge (resonance + analog drive)
    std::atomic<float> pitchSemitones { 0.0f };
    ModernPitchShifter pitchShifter;   // offline render only (message thread)

    std::atomic<int>   delayFlavor { 0 };
    std::atomic<float> delayParam[8];   // interpreted per the active flavor's layout
    std::atomic<bool>  delaySync { false };
    std::atomic<int>   delayDivision { 3 };   // default 1/4 note
    TapeEcho           tapeEcho;        // offline render only (message thread)
    PedalDigital       pedalDigital;
    MagneticDrum       magneticDrum;
    TapeWitness        tapeWitness;
    ColdRack           coldRack;
    VaultDelay         vaultDelay;

    std::atomic<int>   reverbFlavor { 0 };
    std::atomic<float> reverbParam[10];   // interpreted per the active reverb's layout
    VelvetHall         velvetHall;
    ModernSpace        modernSpace;
    ShimmerVerb        shimmerVerb;
    PlateVerb          plateVerb;
    SpringVerb         springVerb;

    std::vector<PresetEntry> presets;
    int  currentPreset = 0;
    std::atomic<bool> dirty { false };
    bool suppressDirty = false;
    std::atomic<bool> presetLoadedFlag { false };
    void buildFactoryPresets();
    void reloadUserPresets();

    juce::SmoothedValue<float> sOutput;
    double currentSR = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BacktraceProcessor)
};
