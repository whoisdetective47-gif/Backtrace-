#include "PluginProcessor.h"
#ifndef BACKTRACE_NO_EDITOR          // headless render/measurement test links the processor without the GUI
 #include "PluginEditor.h"
#endif
#include "Utilities/FactoryPresets.h"

// Zero-delay-feedback (TPT) state-variable section — unconditionally stable at any
// cutoff/Q, so it sweeps cleanly with no zipper and no near-nyquist blow-up. One
// section = 12 dB/oct; cascade two for 24 dB/oct. Resonance (k = 1/Q) gives the
// Harrison-style edge near cutoff. Shared by the FINAL FILTER and the Reveal macro.
namespace {
struct TptSvf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }
    inline void step(float x, float g, float k, float& lp, float& hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        hp = x - k * v1 - v2;
    }
};
} // namespace

// ===========================================================================
//  Parameters
// ===========================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BacktraceProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Phase 1 keeps a single monitor-gain parameter; the FX/pitch/routing
    // parameters are added in their respective phases.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    return layout;
}

// ===========================================================================
//  Construction
// ===========================================================================
BacktraceProcessor::BacktraceProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    buildFactoryPresets();
    reloadUserPresets();
    loadPreset(0);   // open on the first factory preset
    renderThread = std::make_unique<RenderThread>(*this);
    renderThread->startThread();
}

BacktraceProcessor::~BacktraceProcessor()
{
    if (renderThread)   // stop the worker BEFORE members it touches are destroyed
    {
        renderThread->signalThreadShouldExit();
        renderThread->notify();
        renderThread->stopThread(3000);
        renderThread.reset();
    }
}

// Kick a background render (Create Swell / length / keep-pitch). The worker runs
// regenerateSwell() off the message thread; the editor swaps the result in on done.
void BacktraceProcessor::requestRender(bool autoPlay)
{
    pendingAutoPlay.store(autoPlay);
    rendering.store(true);
    renderRequested.store(true);
    if (renderThread) renderThread->notify();
}

// ===========================================================================
//  Lifecycle
// ===========================================================================
void BacktraceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSR = sampleRate;
    capture.prepare(sampleRate);
    syncCapture.prepare(sampleRate);
    swellWidener.prepare(sampleRate);   // Width macro widener (offline, reset per render)

    // Reverse audition buffer — sized to the 60 s capture ceiling so building a
    // take on the message thread never reallocates under the audio thread.
    playBuffer.setSize(2, (int) (sampleRate * CaptureEngine::kMaxSeconds) + 8);
    playBuffer.clear();
    playPlaying.store(false);

    pitchShifter.prepare(sampleRate, samplesPerBlock);
    tapeEcho.prepare(sampleRate);
    pedalDigital.prepare(sampleRate);
    magneticDrum.prepare(sampleRate);
    tapeWitness.prepare(sampleRate);
    coldRack.prepare(sampleRate);
    vaultDelay.prepare(sampleRate);
    velvetHall.prepare(sampleRate);
    modernSpace.prepare(sampleRate);
    shimmerVerb.prepare(sampleRate);
    plateVerb.prepare(sampleRate);
    springVerb.prepare(sampleRate);

    sOutput.reset(sampleRate, 0.02);
    sOutput.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain((float) *apvts.getRawParameterValue("output")));
}

void BacktraceProcessor::releaseResources() {}

// Accept mono and stereo, in == out. Keeps Logic's auval and other strict
// hosts happy across the formats we ship.
bool BacktraceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out) return false;
    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

// ===========================================================================
//  Audio
// ===========================================================================
void BacktraceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();

    if (numCh == 0 || numSmp == 0) return;

    // Backtrace captures the audio passing through this instance. The DAW-sync
    // state machine gates capture against the host transport and snapshots the
    // INPUT (pre output-stage), so the stored source is clean — this is the
    // material the reverse/FX chain will work on in later phases.
    syncCapture.processBlock(capture,
                             buffer.getReadPointer(0),
                             numCh > 1 ? buffer.getReadPointer(1) : nullptr,
                             numSmp,
                             getPlayHead());

    // Reverse audition — overwrites the output with the reversed selection.
    if (playPlaying.load())
    {
        int       pos = playPos.load();
        const int len = playLen.load();
        auto* o0 = buffer.getWritePointer(0);
        auto* o1 = numCh > 1 ? buffer.getWritePointer(1) : nullptr;
        const auto* p0 = playBuffer.getReadPointer(0);
        const auto* p1 = playBuffer.getReadPointer(1);

        const bool  stopping = playStopping.load();
        const float dec = 1.0f / juce::jmax(1.0f, (float) (currentSR * 0.015));   // ~15 ms release on Stop
        float g = playGain.load(std::memory_order_relaxed);   // block-local: no per-sample atomics, no race
        for (int i = 0; i < numSmp; ++i)
        {
            if (pos >= len || (stopping && g <= 0.0f))
            {
                o0[i] = 0.0f; if (o1) o1[i] = 0.0f;
                playPlaying.store(false); playStopping.store(false); g = 1.0f;
                continue;
            }
            if (stopping) g = juce::jmax(0.0f, g - dec);   // click-free fade-out
            o0[i] = p0[pos] * g;
            if (o1) o1[i] = p1[pos] * g;
            ++pos;
        }
        playGain.store(g, std::memory_order_relaxed);
        playPos.store(pos);
    }

    // Monitor gain. Passthrough otherwise (FX/pitch/routing arrive later).
    sOutput.setTargetValue(
        juce::Decibels::decibelsToGain((float) *apvts.getRawParameterValue("output")));

    for (int i = 0; i < numSmp; ++i)
    {
        const float g = sOutput.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer(ch)[i] *= g;
    }
}

// ===========================================================================
//  Editor / State
// ===========================================================================
juce::AudioProcessorEditor* BacktraceProcessor::createEditor()
{
   #ifdef BACKTRACE_NO_EDITOR
    return nullptr;                  // headless test build has no GUI
   #else
    return new BacktraceEditor(*this);
   #endif
}

// Full creative state as JSON — fixes session persistence (was APVTS-only) and
// powers presets. Uses stable string IDs so new flavors never break old data.
juce::var BacktraceProcessor::getStateVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("schema", 1);
    o->setProperty("plugin", "1.0.0");
    o->setProperty("output", (double) *apvts.getRawParameterValue("output"));
    o->setProperty("pitch",  pitchSemitones.load());
    o->setProperty("land",   landAtSource.load());
    o->setProperty("routing", routingModeName(routingMode.load()));
    o->setProperty("preset", (currentPreset >= 0) ? getPresetName(currentPreset) : juce::String());

    auto* mac = new juce::DynamicObject();   // global swell macros
    mac->setProperty("swell",  macroSwell.load());
    mac->setProperty("tail",   macroTail.load());
    mac->setProperty("ghost",  macroGhost.load());
    mac->setProperty("damage", macroDamage.load());
    mac->setProperty("reveal", macroReveal.load());
    mac->setProperty("width",  macroWidth.load());
    mac->setProperty("ringout", macroRingout.load());
    mac->setProperty("level",   macroLevel.load());
    o->setProperty("macros", juce::var(mac));

    auto* d = new juce::DynamicObject();
    const int dFl = delayFlavor.load();
    d->setProperty("flavor", delayFlavorName(dFl));
    d->setProperty("sync", delaySync.load());
    d->setProperty("division", delayDivision.load());
    auto* dp = new juce::DynamicObject();
    const auto dl = delayKnobLayout(dFl);
    for (int i = 0; i < dl.size(); ++i)
        if (dl[i].used()) dp->setProperty(dl[i].name.toLowerCase(), delayParam[(size_t) i].load());
    d->setProperty("params", juce::var(dp));
    o->setProperty("delay", juce::var(d));

    auto* r = new juce::DynamicObject();
    const int rFl = reverbFlavor.load();
    r->setProperty("flavor", reverbFlavorName(rFl));
    auto* rp = new juce::DynamicObject();
    const auto rl = reverbKnobLayout(rFl);
    for (int i = 0; i < rl.size(); ++i)
        if (rl[i].used()) rp->setProperty(rl[i].name.toLowerCase(), reverbParam[(size_t) i].load());
    r->setProperty("params", juce::var(rp));
    o->setProperty("reverb", juce::var(r));

    // Printed-swell editor stage: swell length, fades (seconds), filter motion.
    auto* e = new juce::DynamicObject();
    e->setProperty("swellBars", (double) swellLenBars.load());
    e->setProperty("fadeInSec",  fadeInLen.load()  / juce::jmax(1.0, currentSR));
    e->setProperty("fadeOutSec", fadeOutLen.load() / juce::jmax(1.0, currentSR));
    e->setProperty("fadeInCurve",  fadeInCurve.load());
    e->setProperty("fadeOutCurve", fadeOutCurve.load());
    e->setProperty("keepPitch",    keepPitch.load());
    e->setProperty("filterOn",     filterOn.load());
    e->setProperty("filterMotion", filterMotion.load());
    e->setProperty("hpfStart", (double) hpfStartHz.load());
    e->setProperty("hpfEnd",   (double) hpfEndHz.load());
    e->setProperty("lpfStart", (double) lpfStartHz.load());
    e->setProperty("lpfEnd",   (double) lpfEndHz.load());
    e->setProperty("filterSlope", filterSlope.load());
    e->setProperty("filterCurve", filterCurve.load());
    e->setProperty("filterMotionMode", filterMotionMode.load());
    e->setProperty("filterDrive", (double) filterDrive.load());
    o->setProperty("edit", juce::var(e));

    return juce::var(o);
}

void BacktraceProcessor::setStateVar(const juce::var& v)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr) return;

    suppressDirty = true;

    if (auto* p = apvts.getParameter("output"))
        p->setValueNotifyingHost(apvts.getParameterRange("output")
            .convertTo0to1((float) (double) o->getProperty("output")));
    pitchSemitones.store((float) (double) o->getProperty("pitch"));
    landAtSource.store((bool) o->getProperty("land"));
    routingMode.store(routingModeFromName(o->getProperty("routing").toString()));

    // Global swell macros — default to a strong, neutral set when absent (older
    // states / presets without macro data), so Swell is 100% out of the box.
    auto macv = [&](const juce::var& m, const char* k, float def)
    { auto* mo = m.getDynamicObject(); return (mo && mo->hasProperty(k)) ? (float) (double) mo->getProperty(k) : def; };
    const juce::var mv = o->getProperty("macros");
    macroSwell.store (juce::jlimit(0.0f, 1.0f, macv(mv, "swell",  0.5f)));   // Pull default = natural
    macroTail.store  (juce::jlimit(0.0f, 1.0f, macv(mv, "tail",   0.35f)));
    macroGhost.store (juce::jlimit(0.0f, 1.0f, macv(mv, "ghost",  0.0f)));
    macroDamage.store(juce::jlimit(0.0f, 1.0f, macv(mv, "damage", 0.0f)));
    macroReveal.store(juce::jlimit(0.0f, 1.0f, macv(mv, "reveal", 1.0f)));
    macroWidth.store (juce::jlimit(0.0f, 1.0f, macv(mv, "width",  0.7f)));
    macroRingout.store(juce::jlimit(0.0f, 1.0f, macv(mv, "ringout", 0.35f)));   // Ringout ON by default
    macroLevel.store (juce::jlimit(0.0f, 1.0f, macv(mv, "level",  0.8f)));

    if (auto* d = o->getProperty("delay").getDynamicObject())
    {
        const int fl = delayFlavorFromName(d->getProperty("flavor").toString());
        delayFlavor.store(fl);
        delaySync.store((bool) d->getProperty("sync"));
        if (d->hasProperty("division")) delayDivision.store((int) d->getProperty("division"));
        const auto dl = delayKnobLayout(fl);
        auto* dp = d->getProperty("params").getDynamicObject();
        for (int i = 0; i < 8; ++i)
        {
            float val = (i < dl.size() && dl[i].used()) ? dl[i].def : 0.0f;
            if (dp && i < dl.size() && dl[i].used())
            {
                const auto key = dl[i].name.toLowerCase();
                if (dp->hasProperty(key)) val = (float) (double) dp->getProperty(key);
            }
            delayParam[(size_t) i].store(val);
        }
    }

    if (auto* r = o->getProperty("reverb").getDynamicObject())
    {
        const int fl = reverbFlavorFromName(r->getProperty("flavor").toString());
        reverbFlavor.store(fl);
        const auto rl = reverbKnobLayout(fl);
        auto* rp = r->getProperty("params").getDynamicObject();
        for (int i = 0; i < 10; ++i)
        {
            float val = (i < rl.size() && rl[i].used()) ? rl[i].def : 0.0f;
            if (rp && i < rl.size() && rl[i].used())
            {
                const auto key = rl[i].name.toLowerCase();
                if (rp->hasProperty(key)) val = (float) (double) rp->getProperty(key);
            }
            reverbParam[(size_t) i].store(val);
        }
    }

    if (auto* e = o->getProperty("edit").getDynamicObject())
    {
        if (e->hasProperty("swellBars")) swellLenBars.store((float) (double) e->getProperty("swellBars"));
        fadeInLen.store ((int) ((double) e->getProperty("fadeInSec")  * currentSR));
        fadeOutLen.store((int) ((double) e->getProperty("fadeOutSec") * currentSR));
        if (e->hasProperty("fadeInCurve"))  fadeInCurve.store ((int) e->getProperty("fadeInCurve"));
        if (e->hasProperty("fadeOutCurve")) fadeOutCurve.store((int) e->getProperty("fadeOutCurve"));
        if (e->hasProperty("keepPitch"))    keepPitch.store((bool) e->getProperty("keepPitch"));
        filterOn.store    ((bool) e->getProperty("filterOn"));
        filterMotion.store((bool) e->getProperty("filterMotion"));
        if (e->hasProperty("hpfStart")) hpfStartHz.store((float) (double) e->getProperty("hpfStart"));
        if (e->hasProperty("hpfEnd"))   hpfEndHz.store  ((float) (double) e->getProperty("hpfEnd"));
        if (e->hasProperty("lpfStart")) lpfStartHz.store((float) (double) e->getProperty("lpfStart"));
        if (e->hasProperty("lpfEnd"))   lpfEndHz.store  ((float) (double) e->getProperty("lpfEnd"));
        if (e->hasProperty("filterSlope")) filterSlope.store((int) e->getProperty("filterSlope"));
        if (e->hasProperty("filterCurve")) filterCurve.store((int) e->getProperty("filterCurve"));
        if (e->hasProperty("filterMotionMode")) filterMotionMode.store((int) e->getProperty("filterMotionMode"));
        if (e->hasProperty("filterDrive")) filterDrive.store((float) (double) e->getProperty("filterDrive"));
    }

    swellStale.store(true);   // any new state invalidates the printed-swell render
    suppressDirty = false;
    presetLoadedFlag.store(true);
}

void BacktraceProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    const auto json = juce::JSON::toString(getStateVar());
    dest.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void BacktraceProcessor::setStateInformation(const void* data, int bytes)
{
    const auto json = juce::String::fromUTF8((const char*) data, bytes);
    const auto v = juce::JSON::parse(json);
    if (! v.isObject()) return;                         // unknown/old data fails safe

    setStateVar(v);
    const auto nm = v.getProperty("preset", juce::var()).toString();
    currentPreset = -1;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].name == nm) { currentPreset = i; break; }
    dirty.store(false);
}

// ===========================================================================
//  Preset bank (Phase 11A)
// ===========================================================================
juce::File BacktraceProcessor::presetsDir() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
             .getChildFile("Backtrace").getChildFile("Presets");
}

void BacktraceProcessor::buildFactoryPresets()
{
    presets.clear();
    for (auto& f : btpreset::all())
        presets.push_back({ f.name, f.category, true, f.state, {} });
}

void BacktraceProcessor::reloadUserPresets()
{
    presets.erase(std::remove_if(presets.begin(), presets.end(),
                                 [](const PresetEntry& e) { return ! e.factory; }),
                  presets.end());

    auto dir = presetsDir();
    if (! dir.isDirectory()) return;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        const auto v = juce::JSON::parse(f.loadFileAsString());
        if (auto* o = v.getDynamicObject())
        {
            PresetEntry e;
            e.factory  = false;
            e.state    = v;
            e.file     = f;
            e.name     = o->hasProperty("name") ? o->getProperty("name").toString() : f.getFileNameWithoutExtension();
            e.category = o->hasProperty("category") ? o->getProperty("category").toString() : "User";
            presets.push_back(e);
        }
    }
}

void BacktraceProcessor::loadPreset(int i)
{
    if (! juce::isPositiveAndBelow(i, (int) presets.size())) return;
    setStateVar(presets[(size_t) i].state);
    currentPreset = i;
    dirty.store(false);
}

// Restore a safe default FX state. Vault slots/audio are untouched.
void BacktraceProcessor::resetToDefault()
{
    setStateVar(btpreset::initState());   // neutral INIT state — pitch 0, no extreme FX
    currentPreset = -1;       // "Default" (not a saved preset)
    dirty.store(false);
}

juce::File BacktraceProcessor::saveUserPreset(const juce::String& name, const juce::String& category)
{
    auto dir = presetsDir();
    dir.createDirectory();

    auto s = getStateVar();
    if (auto* o = s.getDynamicObject())
    {
        o->setProperty("name", name);
        o->setProperty("category", category.isNotEmpty() ? category : juce::String("User"));
    }
    auto f = dir.getChildFile(juce::File::createLegalFileName(name) + ".json");
    f.replaceWithText(juce::JSON::toString(s));

    reloadUserPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].file == f) { currentPreset = i; break; }
    dirty.store(false);
    return f;
}

bool BacktraceProcessor::deleteUserPreset(int i)
{
    if (! juce::isPositiveAndBelow(i, (int) presets.size())) return false;
    auto& e = presets[(size_t) i];
    if (e.factory || ! e.file.existsAsFile()) return false;
    const bool ok = e.file.deleteFile();
    reloadUserPresets();
    currentPreset = juce::jlimit(0, juce::jmax(0, (int) presets.size() - 1), currentPreset);
    return ok;
}

// ===========================================================================
//  Vault — capture export
// ===========================================================================
juce::File BacktraceProcessor::vaultCapturesDir() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
             .getChildFile("Backtrace")
             .getChildFile("Captures");
}

void BacktraceProcessor::vaultOpenFolder()
{
    auto dir = vaultCapturesDir();
    dir.createDirectory();
    dir.revealToUser();
}

// Shared render + write: builds the processed (reversed) selection and writes a
// 24-bit WAV + sidecar into dir. Used by both Print (→ Captures) and Export (→
// Library). Returns the WAV file, or {} on failure.
juce::File BacktraceProcessor::writeProcessed(const juce::File& dir, const juce::String& prefix, bool storeInSlot)
{
    // Stage 1 (render the printed swell if needed) + stage 2 (the user's trim /
    // fades / filter edits). Print / export / drag all bake the SAME edited result,
    // so hear = print = export = drag stays true.
    if (rendering.load()) return {};   // a background render is in flight — don't touch swellBuffer
    ensureSwell();
    if (! swellValid.load()) return {};

    juce::AudioBuffer<float> dest;
    const int len = applyEdit(dest);
    if (len <= 0) return {};

    dir.createDirectory();
    juce::File f = dir.getChildFile(juce::File::createLegalFileName(prefix) + ".wav");
    if (f.existsAsFile()) f = f.getNonexistentSibling();   // never overwrite   // never overwrite

    if (auto os = f.createOutputStream())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(os.get(), capture.getSampleRate(), 2, 24, {}, 0));

        if (writer != nullptr)
        {
            os.release();   // writer now owns the stream
            writer->writeFromAudioSampleBuffer(dest, 0, len);
            writer.reset(); // flush + close
            writeSidecar(f, len, true);   // *.btmeta.json — reversed result

            // Print commits the edited printed swell into a free PRINT slot.
            if (storeInSlot)
            {
                int target = firstEmptyPrint();
                if (target < 0) target = activePrint;   // all full → overwrite the selected print
                if (target >= 0)
                {
                    auto& s = printSlots[(size_t) target];
                    s.buffer.makeCopyOf(dest);
                    s.length = len; s.status = SlotPrinted; s.sr = capture.getSampleRate();
                    s.routing = routingMode.load();
                    const juce::String preset = (currentPreset >= 0) ? getPresetName(currentPreset) : juce::String("Swell");
                    s.name = preset + " " + juce::String(getSwellLenBars(), getSwellLenBars() == (int) getSwellLenBars() ? 0 : 1) + "bar";
                    lastStoredSlot = target;
                    slotsChangedFlag.store(true);
                }
            }
            return f;
        }
    }
    return {};
}

// "Backtrace_<preset-or-Swell>_<bars>" — useful, host-friendly file name.
juce::String BacktraceProcessor::resultBaseName() const
{
    juce::String tag = (currentPreset >= 0) ? getPresetName(currentPreset) : juce::String("Swell");
    tag = juce::File::createLegalFileName(tag).removeCharacters(" ");
    const float bars = swellLenBars.load();
    const juce::String barsLabel = (bars < 1.0f) ? juce::String("1beat")
        : juce::String((int) std::lround(bars)) + (std::lround(bars) == 1 ? "bar" : "bars");
    return "Backtrace_" + tag + "_" + barsLabel;
}

juce::File BacktraceProcessor::vaultPrintProcessed()
{
    return writeProcessed(vaultCapturesDir(), resultBaseName(), true);
}

// Writes one Vault slot's stored audio verbatim so dragging a slot exports exactly
// what it holds (source = raw capture/import, print = rendered swell).
juce::File BacktraceProcessor::exportSlotFile(int i, bool print, const juce::File& dir)
{
    if (! slotValid(i)) return {};
    const auto& s = print ? printSlots[(size_t) i] : sourceSlots[(size_t) i];
    if (s.length <= 0) return {};

    dir.createDirectory();
    const juce::String base = juce::String(print ? "Backtrace_Print" : "Backtrace_Source")
                            + juce::String(i + 1).paddedLeft('0', 2)
                            + "_" + juce::File::createLegalFileName(s.name).removeCharacters(" ");
    juce::File f = dir.getChildFile(base + ".wav");
    if (f.existsAsFile()) f = f.getNonexistentSibling();

    if (auto os = f.createOutputStream())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(os.get(), s.sr > 0 ? s.sr : currentSR, 2, 24, {}, 0));
        if (writer != nullptr)
        {
            os.release();
            writer->writeFromAudioSampleBuffer(s.buffer, 0, s.length);
            writer.reset();
            return f;
        }
    }
    return {};
}

// Plays the raw selected SOURCE region (active slot) so the user can confirm what
// they captured — distinct from auditioning the processed swell.
void BacktraceProcessor::startSourceAudition()
{
    playPlaying.store(false);
    const int in = trimIn.load(), out = trimOut.load();
    int len = out - in;
    if (len <= 0) return;

    const auto& buf = sourceSlots[(size_t) activeSource].buffer;
    if (buf.getNumSamples() < out) return;
    len = juce::jmin(len, playBuffer.getNumSamples());
    const int sc = juce::jmax(1, juce::jmin(2, buf.getNumChannels()));
    for (int c = 0; c < playBuffer.getNumChannels(); ++c)
        playBuffer.copyFrom(c, 0, buf, juce::jmin(c, sc - 1), in, len);

    const int fade = juce::jmin(len / 2, (int) (currentSR * 0.003));   // click-safe boundaries
    for (int c = 0; c < playBuffer.getNumChannels(); ++c)
    {
        auto* d = playBuffer.getWritePointer(c);
        for (int i = 0; i < fade; ++i) { const float g = (float) i / (float) fade; d[i] *= g; d[len - 1 - i] *= g; }
    }

    auditionWhat.store(1);   // source
    playLen.store(len);
    playPos.store(0);
    playStopping.store(false); playGain.store(1.0f);
    playPlaying.store(true);
}

juce::File BacktraceProcessor::vaultLibraryDir() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
             .getChildFile("Backtrace")
             .getChildFile("Library");
}

juce::File BacktraceProcessor::vaultExportToLibrary()
{
    return writeProcessed(vaultLibraryDir(), resultBaseName());
}

juce::File BacktraceProcessor::vaultRenderForDrag()
{
    return writeProcessed(vaultCapturesDir(), resultBaseName());
}

// Loads an external audio file into a specific Vault slot and makes it the active
// source — works with the transport stopped (message thread). This is the primary
// "drag a word in" path; drag-onto-slot and Import both route through here.
juce::File BacktraceProcessor::importToSlot(int slot, const juce::File& wav,
                                            juce::int64 startSample, juce::int64 numSamples)
{
    if (! slotValid(slot)) return {};
    if (rendering.load()) return {};   // don't mutate a source buffer the render worker may be reading

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(wav));
    if (reader == nullptr) return {};

    // Optional region (e.g. the exact event range a DAW reports). 0 length = whole file.
    const double srcSR    = reader->sampleRate > 0.0 ? reader->sampleRate : currentSR;
    const juce::int64 fileLen = reader->lengthInSamples;
    const juce::int64 startS  = juce::jlimit((juce::int64) 0, juce::jmax((juce::int64) 0, fileLen), startSample);
    juce::int64 want = (numSamples > 0) ? numSamples : (fileLen - startS);
    want = juce::jmin(want, fileLen - startS);

    const double ratio  = srcSR / juce::jmax(1.0, currentSR);   // input samples per output sample
    const juce::int64 capRaw = (juce::int64) (currentSR * CaptureEngine::kMaxSeconds * juce::jmax(1.0, ratio));
    const int rawN = (int) juce::jmin(want, capRaw);
    if (rawN <= 0) return {};

    if (importTemp.getNumChannels() < 2 || importTemp.getNumSamples() < rawN)
        importTemp.setSize(2, rawN, false, false, true);   // reused scratch (no per-import 20 MB alloc)
    auto& raw = importTemp;
    for (int c = 0; c < 2; ++c) raw.clear(c, 0, rawN);
    reader->read(&raw, 0, rawN, startS, true, true);     // mono files fill both channels
    reader.reset();                                       // release the file reader/stream promptly

    auto& s = sourceSlots[(size_t) slot];
    int n = rawN;

    // Resample to the engine rate so dropped files keep correct pitch/length in any session.
    if (std::abs(srcSR - currentSR) > 1.0)
    {
        n = juce::jmin((int) (currentSR * CaptureEngine::kMaxSeconds), (int) (rawN / ratio));
        s.buffer.setSize(2, n, false, true, true);
        s.buffer.clear();
        for (int c = 0; c < 2; ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.process(ratio, raw.getReadPointer(c), s.buffer.getWritePointer(c), n);
        }
    }
    else
    {
        s.buffer.setSize(2, n, false, true, true);
        for (int c = 0; c < 2; ++c) s.buffer.copyFrom(c, 0, raw, c, 0, n);
    }

    s.length = n;
    s.status = SlotSource;
    s.sr = currentSR;     // stored at engine rate after resampling
    s.name = wav.getFileNameWithoutExtension();

    activeSource = slot;        // the dropped audio becomes the active source
    viewingPrint = false;
    trimIn.store(0);
    trimOut.store(n);
    tailStartSamp.store(-1);   // new source → Tail Start back to auto (= source end)
    swellStale.store(true);
    slotsChangedFlag.store(true);
    return wav;
}

juce::File BacktraceProcessor::importFromLibrary(const juce::File& wav)
{
    return importToSlot(activeSource, wav);
}

// Builds the reversed trimmed selection into playBuffer and starts auditioning
// it through the output. Stop-first makes overwriting playBuffer safe (the audio
// thread skips it while playPlaying is false), and the buffer is pre-sized so no
// reallocation occurs here.
void BacktraceProcessor::startReverseAudition(bool forceRegen)
{
    playPlaying.store(false);   // halt playback before touching the buffer

    if (forceRegen) regenerateSwell();
    else            ensureSwell();
    if (! swellValid.load()) return;

    const int len = applyEdit(playBuffer);   // playBuffer is pre-sized; applyEdit clamps to it
    if (len <= 0) return;

    auditionWhat.store(2);   // swell
    playLen.store(len);
    playPos.store(0);
    playStopping.store(false); playGain.store(1.0f);
    playPlaying.store(true);
}

// Audition Tail — render and play the FORWARD wet FX tail (before reversal), so the
// user can vet the reverb/delay character: "if the forward tail is weak, the reverse
// swell will be weak." Same generator (applySwellFX) the reverse swell reverses.
int BacktraceProcessor::renderTail(juce::AudioBuffer<float>& dest)
{
    if (rendering.load()) return 0;   // the worker is using the FX machines — don't run them here too
    const int selLen = trimOut.load() - trimIn.load();
    if (selLen <= 0) return 0;
    juce::ScopedNoDenormals noDenormals;
    const int    mode    = routingMode.load();
    const double swellSec = (double) swellLengthSamples() / juce::jmax(1.0, currentSR);
    const bool   hasFX   = (delayFlavor.load() != 0) || (reverbFlavor.load() != 0);

    const int tailLen = juce::jlimit(selLen + 1, juce::jmin(dest.getNumSamples(), (int) (currentSR * 12.0)),
                                     selLen + (int) (currentSR * juce::jmax(2.5, swellSec * 1.5)));
    dest.clear();
    const int base = fillSource(dest, false);          // forward source
    if (base <= 0) return 0;
    applyPitch(dest, base, pitchSemitones.load());
    if (hasFX) applySwellFX(dest, tailLen, mode, swellSec, -1.0f,
                            (float) juce::jlimit(0.0, 1.0, (swellSec - 1.0) / 6.0));

    const int fout = juce::jmin(tailLen / 4, (int) (currentSR * 0.03));   // ease the audition end
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        auto* d = dest.getWritePointer(c);
        for (int i = 0; i < fout; ++i) d[tailLen - 1 - i] *= (float) i / (float) fout;
    }
    applyOutputSafety(dest, tailLen);
    return tailLen;
}

void BacktraceProcessor::startTailAudition()
{
    playPlaying.store(false);
    const int len = renderTail(playBuffer);
    if (len <= 0) return;
    auditionWhat.store(3);   // forward wet tail
    playLen.store(len);
    playPos.store(0);
    playStopping.store(false); playGain.store(1.0f);
    playPlaying.store(true);
}

// ---- Stage 1: fill the printed-swell lane buffer (message thread) ----
// When viewing a Print slot, load its committed audio; otherwise render the
// active source through the FX/swell engine.
void BacktraceProcessor::regenerateSwell()
{
    int len = 0;
    if (viewingPrint)
    {
        const auto& slot = printSlots[(size_t) activePrint];
        if (slot.length <= 0) { swellValid.store(false); swellChangedFlag.store(true); return; }
        const int n = slot.length;
        swellBuffer.setSize(2, n, false, false, true);
        const int sc = juce::jmax(1, juce::jmin(2, slot.buffer.getNumChannels()));
        for (int c = 0; c < 2; ++c)
            swellBuffer.copyFrom(c, 0, slot.buffer, juce::jmin(c, sc - 1), 0, n);
        len = n;
    }
    else
    {
        if (trimOut.load() - trimIn.load() <= 0) { swellValid.store(false); swellChangedFlag.store(true); return; }
        const bool swell = swellMode.load();
        // Clamp to the fixed audition buffer (minus ringout headroom) so applyEdit() NEVER
        // reallocates playBuffer — a realloc there would race the audio thread reading it.
        // Only bites at absurd tempos where a swell would exceed ~57 s; normal use is far under.
        const int cap   = juce::jmax(1, playBuffer.getNumSamples() - (int) (currentSR * 3.5));
        const int total = juce::jmin(cap, swell ? swellLengthSamples() : (baseLen() + extraTail()));
        if (total <= 0) { swellValid.store(false); swellChangedFlag.store(true); return; }
        swellBuffer.setSize(2, total, false, false, true);
        len = swell ? renderSwell(swellBuffer, total) : renderProcessed(swellBuffer, total);
    }
    if (len <= 0) { swellValid.store(false); swellChangedFlag.store(true); return; }

    // Preserve the user's printed-swell trim + fade edits when re-rendering the
    // SAME length (e.g. pressing Reverse Swell again, or tweaking an FX knob): the
    // fade drawn on the lane must survive a re-render so Reverse Swell == Audition.
    // Only a length/source change resets to full-trim + default safety fades.
    const bool sameLength = ! viewingPrint && swellValid.load() && (len == swellRenderLen);

    swellRenderLen = len;
    swellRenderSR  = (viewingPrint && printSlots[(size_t) activePrint].sr > 0)
                       ? printSlots[(size_t) activePrint].sr
                       : (capture.getSampleRate() > 0 ? capture.getSampleRate() : currentSR);

    // Cache the raw bloom, then bake the Swell-macro envelope into swellBuffer so the
    // canvas + every output use the shaped swell. (A Print slot is already baked.)
    if (! viewingPrint)
    {
        const int sn = swellBuffer.getNumSamples();                       // reuse the cache buffer (no realloc churn)
        if (swellRawBuffer.getNumChannels() < 2 || swellRawBuffer.getNumSamples() < sn)
            swellRawBuffer.setSize(2, sn, false, false, true);
        for (int c = 0; c < 2; ++c) swellRawBuffer.copyFrom(c, 0, swellBuffer, c, 0, sn);
        reshapeSwellMacro();
    }

    if (sameLength)
    {
        const int a = juce::jlimit(0, len, swellTrimIn.load());
        const int b = juce::jlimit(a, len, swellTrimOut.load() > 0 ? swellTrimOut.load() : len);
        swellTrimIn.store(a);
        swellTrimOut.store(b);
        fadeInLen.store (juce::jlimit(0, b - a, fadeInLen.load()));
        fadeOutLen.store(juce::jlimit(0, b - a, fadeOutLen.load()));
    }
    else
    {
        swellTrimIn.store(0);
        swellTrimOut.store(len);
        // Subtle ~7% start fade ON by default (with the Exp curve) so the rise begins
        // smoothly without hiding the swell shape; the tail end keeps a short safety fade.
        fadeInLen.store (juce::jlimit(0, len / 2, (int) (len * 0.07f)));
        fadeOutLen.store(juce::jmin(len / 4, (int) (currentSR * 0.008)));
    }

    swellValid.store(true);
    swellStale.store(false);
    swellChangedFlag.store(true);

   #if JUCE_DEBUG
    if (! viewingPrint)   // log the generated swell's shape (energy must RISE Q1→Q4)
    {
        const double bpm = syncCapture.getBpm() > 1.0 ? syncCapture.getBpm() : 120.0;
        auto qRms = [this](int q) {
            const int n = swellRenderLen / 4; const int s = q * n;
            double sum = 0.0; const auto* d = swellBuffer.getReadPointer(0);
            for (int i = s; i < s + n; ++i) sum += (double) d[i] * d[i];
            return n > 0 ? std::sqrt(sum / n) : 0.0;
        };
        const double q1 = qRms(0), q4 = qRms(3);
        const double ratio = q1 > 1.0e-9 ? q4 / q1 : 0.0;
        DBG("[Backtrace swell] bars=" << swellLenBars.load() << " bpm=" << bpm
            << " targetSec=" << (swellLenBars.load() * (240.0 / bpm))
            << " samples=" << swellRenderLen << " sec=" << (swellRenderLen / juce::jmax(1.0, currentSR))
            << "  RMS q1=" << q1 << " q2=" << qRms(1) << " q3=" << qRms(2) << " q4=" << q4
            << "  Q4/Q1=" << ratio);
        if (ratio < 1.5)
            DBG("[Backtrace swell] WARNING: Swell envelope flattened (Q4/Q1=" << ratio << ")");
    }
   #endif
}

// Swell macro → secondary rise shaping over the REAL reversed reverb bloom (the
// bloom itself supplies the drama, like a Cubase reverse swell). At Swell 100% the
// envelope is unity → the natural bloom is heard untouched. Below 100% it lifts the
// early/quiet part (flattens the rise) for a subtler throw. env(1) == 1 ALWAYS, so
// the landing point and timing never move — only the early shape.
// PULL — the reverse-swell energy curve (was "Swell"). Shapes how dramatically the rise
// pulls into the landing, on top of the natural reverb decay. Applied ONLY over the rise
// region [0, landing); the aftertail/ringout past the landing keeps its natural decay.
// env(landing) == 1 ALWAYS, so the landing/timing never move. 0.5 = natural (no extra
// shaping). Below 0.5 lifts the quiet start (more even). Above 0.5 pulls the start down
// (more dramatic, late bloom) — but always quiet-but-present at the default/centre.
void BacktraceProcessor::reshapeSwellMacro()
{
    const int n = swellRenderLen;
    if (viewingPrint || n <= 1 || swellRawBuffer.getNumSamples() < n) return;
    const float p    = macroSwell.load();                  // "Pull": 0 even · 0.5 natural · 1 dramatic
    const float amt  = (p - 0.5f) * 2.0f;                  // -1 (even) .. +1 (dramatic)
    const int   land = juce::jlimit(2, n, swellLandingSamp.load() > 1 ? swellLandingSamp.load() : n);
    if (swellBuffer.getNumSamples() < n) swellBuffer.setSize(2, n, false, false, true);
    for (int c = 0; c < swellBuffer.getNumChannels(); ++c)
    {
        const float* raw = swellRawBuffer.getReadPointer(juce::jmin(c, swellRawBuffer.getNumChannels() - 1));
        float* d = swellBuffer.getWritePointer(c);
        for (int i = 0; i < n; ++i)
        {
            float env = 1.0f;
            if (i < land)                                  // shape the RISE only
            {
                const float x = (float) i / (float) (land - 1);          // 0 far-tail → 1 landing
                if (amt >= 0.0f) env = (1.0f - amt) + amt * (0.05f + 0.95f * std::pow(x, 2.5f));  // pull start DOWN
                else             env = 1.0f + (-amt) * std::pow(1.0f - x, 1.5f);                   // lift start UP
            }
            d[i] = raw[i] * env;
        }
    }
    swellChangedFlag.store(true);
}

// ---- Stage 2: trim → filter → fades → Damage/Reveal/Width → loudness → safety. ----
int BacktraceProcessor::applyEdit(juce::AudioBuffer<float>& dest)
{
    if (rendering.load()) return 0;   // worker owns swellBuffer mid-render — never read it here
    if (! swellValid.load() || swellRenderLen <= 0) return 0;

    const int a = juce::jlimit(0, swellRenderLen, swellTrimIn.load());
    const int b = juce::jlimit(0, swellRenderLen, swellTrimOut.load());
    int len = b - a;
    if (len <= 0) return 0;

    if (dest.getNumChannels() < 2 || dest.getNumSamples() < len)
        dest.setSize(2, len, false, false, true);   // resize only for caller-owned (print) buffers
    len = juce::jmin(len, dest.getNumSamples());     // clamp for the fixed-size audition buffer

    const int sc = juce::jmax(1, juce::jmin(2, swellBuffer.getNumChannels()));
    for (int c = 0; c < dest.getNumChannels(); ++c)
        dest.copyFrom(c, 0, swellBuffer, juce::jmin(c, sc - 1), a, len);

    applyFilterMotion(dest, len);

    int fin  = juce::jlimit(0, len, fadeInLen.load());
    int fout = juce::jlimit(0, len, fadeOutLen.load());
    fin  = juce::jmax(fin,  juce::jmin(len, (int) (currentSR * 0.001)));   // 1 ms click safety floor
    fout = juce::jmax(fout, juce::jmin(len, (int) (currentSR * 0.001)));
    const int ic = fadeInCurve.load(), oc = fadeOutCurve.load();
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        auto* d = dest.getWritePointer(c);
        for (int i = 0; i < fin;  ++i) d[i]           *= btFadeGain(ic, (float) i / (float) fin);
        for (int i = 0; i < fout; ++i) d[len - 1 - i] *= btFadeGain(oc, (float) i / (float) fout);
    }

    // ---- Reveal macro: global openness via the SAME Harrison-style filter engine ----
    // (resonant 24 dB/oct LPF). 1 = open/full-range (no effect), 0.5 = balanced,
    // 0 = dark/hidden. A global brightness ON TOP of the manual FINAL FILTER + Motion,
    // so it never touches the user's explicit HPF/LPF endpoints.
    {
        const float rv = macroReveal.load();
        if (rv < 0.97f)
        {
            const float fc = juce::jmin((float) (currentSR * 0.45), std::exp(juce::jmap(rv, std::log(650.0f), std::log(20000.0f))));
            const float g  = std::tan(juce::MathConstants<float>::pi * fc / (float) currentSR);
            const float k  = 1.0f / 0.95f;     // gentle Harrison-style resonant edge
            TptSvf rl[2][2];                   // [channel][section] — 24 dB/oct
            for (int c = 0; c < dest.getNumChannels() && c < 2; ++c)
            {
                float* d = dest.getWritePointer(c);
                for (int i = 0; i < len; ++i)
                {
                    float x = d[i], lp, hp;
                    rl[c][0].step(x, g, k, lp, hp); x = lp;
                    rl[c][1].step(x, g, k, lp, hp); x = lp;
                    d[i] = x;
                }
            }
        }
    }

    // ---- Damage macro: Dust-Vault saturation + age (safe, bounded). 0 = clean. ----
    {
        const float dm = macroDamage.load();
        if (dm > 0.001f)
        {
            const float drive = 1.0f + dm * 3.0f;
            juce::Random rng (0x5eed5eed);                        // fixed seed → reproducible render
            for (int c = 0; c < dest.getNumChannels(); ++c)
            {
                float* d = dest.getWritePointer(c);
                for (int i = 0; i < len; ++i)
                {
                    const float sat = std::tanh(d[i] * drive);
                    float o = (1.0f - dm) * d[i] + dm * sat;
                    o += dm * 0.02f * (rng.nextFloat() * 2.0f - 1.0f) * std::abs(o);   // signal-gated dust
                    d[i] = o;
                }
            }
        }
    }

    // Loudness compensation — ONE scalar over the WHOLE buffer so different swell
    // lengths land at a comparable level WITHOUT touching the rise/fall shape. Done
    // BEFORE Width so the mid (mono content) is fixed here; the widener then only
    // adds Side on top → the mono fold-down stays constant across width (mono-safe).
    float pk = 0.0f;
    for (int c = 0; c < dest.getNumChannels(); ++c)
        pk = juce::jmax(pk, dest.getMagnitude(c, 0, len));
    if (pk > 1.0e-4f)
        dest.applyGain(0, len, juce::jmin(0.60f / pk, 8.0f));   // consistent target w/ headroom (was 0.70 — tamed hot snares)

    // ---- Width macro: Dust 12.47 mono-safe two-stage widener (Common/DSP). ----
    // Stage A scales existing side; Stage B adds decorrelated artificial side from
    // the mid (so even a mono swell gains width); 150 Hz side HP keeps lows centred;
    // all width lives in Side → mono fold-down = 2·Mid (no hollowing). Reset per call
    // for a deterministic offline render; snap width (no smoothing ramp at the start).
    if (dest.getNumChannels() >= 2)
    {
        swellWidener.reset();
        swellWidener.setWidthImmediate(macroWidth.load() * 100.0f);
        swellWidener.process(dest.getWritePointer(0), dest.getWritePointer(1), len);
    }

    // ---- Swell Level — user output trim on the final swell (−24..+6 dB). Applied to the
    // whole stereo buffer equally (mono-safety ratio untouched), after loudness comp/Width
    // and before the safety ceiling so a hot push still can't run away. Same buffer feeds
    // Play Swell / Print / Export / Drag → they all match.
    {
        const float g = juce::Decibels::decibelsToGain(levelToDb(macroLevel.load()));
        if (std::abs(g - 1.0f) > 1.0e-4f) dest.applyGain(0, len, g);
    }

    applyOutputSafety(dest, len);   // peak ceiling (soft-clip > 0.9) catches wide-image peaks
    return len;
}

// FINAL FILTER — Harrison 32C-inspired resonant HPF/LPF with selectable slope, a
// gentle analog Character (resonance + soft saturation) and motion that sweeps the
// cutoffs in LOG-frequency space (with a time-curve option). Applied to the final
// printed buffer so audition = print = export = drag.
void BacktraceProcessor::applyFilterMotion(juce::AudioBuffer<float>& buf, int len)
{
    if (! filterOn.load() || len <= 0) return;

    juce::ScopedNoDenormals noDenormals;
    const int    nCh    = juce::jmin(2, buf.getNumChannels());
    const float  nyq    = (float) (currentSR * 0.45);
    const bool   motion = filterMotion.load();
    const int    nSec   = (filterSlope.load() >= 1) ? 2 : 1;     // 24 vs 12 dB/oct
    const int    curve  = filterCurve.load();
    const float  chr    = filterDrive.load();                    // Character / Edge
    const float  k      = 1.0f / juce::jlimit(0.5f, 2.4f, 0.78f + chr * 1.4f);   // resonance: gentle → edgy
    const float  drive  = 1.0f + chr * 1.6f;                     // analog saturation amount

    const float lnHpA = std::log(juce::jmax(20.0f, hpfStartHz.load())), lnHpB = std::log(juce::jmax(20.0f, hpfEndHz.load()));
    const float lnLpA = std::log(juce::jmax(20.0f, lpfStartHz.load())), lnLpB = std::log(juce::jmax(20.0f, lpfEndHz.load()));

    auto shape = [curve](float t)   // motion time-curve (log-freq interpolation is separate, always on)
    {
        switch (curve)
        {
            case 1:  return t * t;                         // Exp — slow start, rush at the end
            case 2:  return 1.0f - (1.0f - t) * (1.0f - t);// Log — fast start, ease into the end
            case 3:  return t * t * (3.0f - 2.0f * t);     // S-Curve
            default: return t;                             // Linear (in log-freq → perceptually even)
        }
    };

    // Motion Mode pivots the sweep on PEAK LAND (the landing), not the buffer end. The
    // ringout after the landing is where the tail HOLDS (Rise Only) or FALLS back (Tail Fall).
    const int   mmode    = filterMotionMode.load();
    const int   landSamp = swellLandingSamp.load() - swellTrimIn.load();
    const float landFrac = juce::jlimit(0.05f, 0.98f, (len > 1) ? (float) landSamp / (float) len : 0.85f);
    auto motionT = [mmode, landFrac, &shape](float p) -> float   // p 0..1 → t 0 (start A) .. 1 (Peak Land B)
    {
        switch (mmode)
        {
            case 1:  // Rise + Tail Fall — A→B by the landing, then B→A back down over the tail
                return (p <= landFrac) ? shape(p / landFrac)
                                       : 1.0f - shape((p - landFrac) / juce::jmax(1.0e-4f, 1.0f - landFrac));
            case 2:  // Fall Only — start open at B, close to A through the whole swell + tail
                return 1.0f - shape(p);
            default: // 0 Rise Only — A→B by the landing, then HOLD B over the tail
                return (p <= landFrac) ? shape(p / landFrac) : 1.0f;
        }
    };

    TptSvf hp[2][2], lp[2][2];   // [channel][section]
    const int blk = 64;
    for (int start = 0; start < len; start += blk)
    {
        const int   n  = juce::jmin(blk, len - start);
        const float p  = (len > 1) ? (float) start / (float) (len - 1) : 0.0f;
        const float t  = motionT(p);
        const float fcHP = juce::jmin(nyq, motion ? std::exp(juce::jmap(t, lnHpA, lnHpB)) : std::exp(lnHpA));
        const float fcLP = juce::jmin(nyq, motion ? std::exp(juce::jmap(t, lnLpA, lnLpB)) : std::exp(lnLpA));
        const float gHP = std::tan(juce::MathConstants<float>::pi * fcHP / (float) currentSR);
        const float gLP = std::tan(juce::MathConstants<float>::pi * fcLP / (float) currentSR);
        const bool  doHP = fcHP > 21.0f;        // skip when parked at the 20 Hz floor
        const bool  doLP = fcLP < nyq * 0.999f; // skip when wide open

        for (int c = 0; c < nCh; ++c)
        {
            auto* d = buf.getWritePointer(c);
            for (int i = 0; i < n; ++i)
            {
                float x = d[start + i], lpo, hpo;
                if (doHP) for (int s = 0; s < nSec; ++s) { hp[c][s].step(x, gHP, k, lpo, hpo); x = hpo; }
                if (doLP) for (int s = 0; s < nSec; ++s) { lp[c][s].step(x, gLP, k, lpo, hpo); x = lpo; }
                if (chr > 0.001f) x = (1.0f - chr) * x + chr * (std::tanh(x * drive) / drive);   // analog Character
                d[start + i] = x;
            }
        }
    }
}

// Swell length in samples from the chosen bar count and the captured take's tempo.
int BacktraceProcessor::swellLengthSamples() const
{
    double bpm = syncCapture.getBpm();                 // live project tempo (follows tempo changes)
    if (bpm <= 1.0) bpm = (syncCapture.finBpm() > 1.0 ? syncCapture.finBpm() : 120.0);
    int num = syncCapture.getTsNumerator(), den = syncCapture.getTsDenominator();
    if (num <= 0) num = 4;
    if (den <= 0) den = 4;
    const double secPerBar = (4.0 * num / (double) den) * (60.0 / bpm);
    return juce::jmax(1, (int) (swellLenBars.load() * secPerBar * currentSR));
}

// Routing-aware FX topology for the swell bloom (wet-only, decay-filled). This is
// THE place the Routing dropdown changes the DSP order — Delay→Reverb, Reverb→Delay,
// Parallel, Feedback Verb→Delay. Reverse-position modes use the default serial order
// here and differ via WHEN the reverse happens (handled in renderSwell).
void BacktraceProcessor::applySwellFX(juce::AudioBuffer<float>& buf, int total, int mode,
                                      double fillSec, float decayOverride, float density)
{
    const bool hasDelay  = delayFlavor.load()  != 0;
    const bool hasReverb = reverbFlavor.load() != 0;
    if (! hasDelay && ! hasReverb) return;
    const float dov = decayOverride, dns = density;

    switch ((RoutingMode) mode)   // Tail Type → which FX builds the wet tail
    {
        case RoutingMode::DelaySwell:                        // delay repeats only
            if (hasDelay)  applyDelay (buf, total, true, fillSec);
            else if (hasReverb) applyReverb(buf, total, true, fillSec, dov, dns);   // safe fallback
            break;

        case RoutingMode::DelayReverbSwell:                  // delay echoes smeared into the reverb
            if (hasDelay)  applyDelay (buf, total, true, fillSec);
            if (hasReverb) applyReverb(buf, total, true, fillSec, dov, dns);
            break;

        case RoutingMode::ReverbDelaySwell:                  // reverb wash repeated by the delay
            if (hasReverb) applyReverb(buf, total, true, fillSec, dov, dns);
            if (hasDelay)  applyDelay (buf, total, true, fillSec);
            break;

        case RoutingMode::ParallelSwell:                     // delay ∥ reverb, blended
            if (hasDelay && hasReverb)
            {
                juce::AudioBuffer<float> rev; rev.makeCopyOf(buf);
                applyDelay (buf, total, true, fillSec);
                applyReverb(rev, total, true, fillSec, dov, dns);
                for (int c = 0; c < buf.getNumChannels(); ++c)
                {
                    auto* d = buf.getWritePointer(c); const auto* r = rev.getReadPointer(c);
                    for (int i = 0; i < total; ++i) d[i] = (d[i] + r[i]) * 0.5f;
                }
            }
            else if (hasDelay)  applyDelay (buf, total, true, fillSec);
            else                applyReverb(buf, total, true, fillSec, dov, dns);
            break;

        case RoutingMode::ReverbSwell:                       // reverb tail only (DEFAULT)
        case RoutingMode::PostColor:                         // (post colour applied after reverse, later)
        default:
            if (hasReverb)      applyReverb(buf, total, true, fillSec, dov, dns);
            else if (hasDelay)  applyDelay (buf, total, true, fillSec);             // safe fallback
            break;
    }
}

// Reverse Swell — THE single render used by waveform/audition/print/export/drag.
// The Routing mode controls the ACTUAL DSP: (a) whether the source is reversed
// BEFORE the FX or the FX tail is reversed AFTER (reverse-position), and (b) the
// delay/reverb topology (Delay→Reverb, Reverb→Delay, Parallel, Feedback). The
// routed bloom is trimmed to real content and time-fit to the chosen Swell Length.
int BacktraceProcessor::renderSwell(juce::AudioBuffer<float>& dest, int swellLen)
{
    const int selLen = trimOut.load() - trimIn.load();
    if (selLen <= 0 || swellLen <= 0) return 0;

    juce::ScopedNoDenormals noDenormals;
    const double swellSec = (double) swellLen / juce::jmax(1.0, currentSR);
    const bool   hasFX    = (delayFlavor.load() != 0) || (reverbFlavor.load() != 0);
    const int    mode     = routingMode.load();
    // Every Tail Type builds a FORWARD wet tail and then reverses it — that's the swell.
    // The Tail Type only changes WHICH FX generate the tail (handled in applySwellFX).
    const bool reverseSrc  = false;

    dest.clear();

    // ---- Stage A: LENGTH-AWARE tail generation (iterative decay-fit) -----------
    // Render the wet FX tail, MEASURE where it decays to -40 dB, and re-render with
    // an adjusted reverb decay until the natural tail reaches the swell length — so
    // the reversed swell is a REAL tail of the right length, not a heavy stretch.
    // Density rises with length for body on long swells. The decay-fit IS the engine;
    // time-fit downstream is only the last-mile correction.
    const double densityAmt = juce::jlimit(0.0, 1.0, (swellSec - 1.0) / 6.0);   // longer swell → denser tail
    // Capture window MUST be long enough to catch the full FX tail (every delay repeat +
    // reverb decay), or repeats that fall past the window are lost and the delay seems to
    // "not catch". Size = max(swell-fit budget, source + measured FX tail + margin).
    const int fxTail = hasFX ? (tailLen() + reverbTailLen()) : 0;
    // The render window must hold: source + the swell-length rise tail + the requested
    // Ringout aftertail + margin. Sizing it to include the ringout is what lets EVERY reverb
    // continue its natural decay PAST the landing (the ringout reads this forward tail) — no
    // reverb gets hard-cut at the swell end. Delay modes still get room for their repeats.
    const int wantRing = hasFX ? swellRingoutSamples() : 0;
    const int renderLen = hasFX
        ? juce::jlimit(selLen + 1, (int) (currentSR * 30.0),
                       juce::jmax(selLen + swellLen + wantRing + (int) (currentSR * 0.7),
                                  selLen + fxTail + (int) (currentSR * 0.5)))
        : selLen;
    if (renderWork.getNumChannels() < 2 || renderWork.getNumSamples() < renderLen)
        renderWork.setSize(2, renderLen, false, false, true);   // reused scratch (grows, never per-render alloc)
    auto& work = renderWork;

    auto renderWet = [&](float decayOv) -> int
    {
        work.clear();
        const int b = fillSource(work, reverseSrc);            // forward OR reversed source per mode
        if (b <= 0) return 0;
        applyPitch(work, b, pitchSemitones.load());
        if (hasFX) applySwellFX(work, renderLen, mode, swellSec, decayOv, (float) densityAmt);
        return b;
    };
    auto measureTail = [&]() -> int                            // last sample above -40 dB of peak
    {
        float peak = 0.0f;
        for (int c = 0; c < work.getNumChannels(); ++c)
            peak = juce::jmax(peak, work.getMagnitude(c, 0, renderLen));
        const float thr = juce::jmax(1.0e-5f, peak * 0.01f);
        int wl = 1;
        for (int c = 0; c < work.getNumChannels(); ++c)
        {
            const auto* d = work.getReadPointer(c);
            for (int i = renderLen - 1; i > wl; --i)
                if (std::abs(d[i]) > thr) { wl = i + 1; break; }
        }
        return juce::jlimit(2, renderLen, wl);
    };

    int washLen = 0;
    // Source End / Tail Start — boundary (in work/source-sample coords) where the source
    // ends and the generated FX tail begins. The reverse swell is built from the WET
    // TAIL after this marker. Without an FX tail there's nothing to skip (= 0).
    const int tailStartFixed = hasFX ? getTailStart() : 0;
    const bool tailUsesReverb = (mode != (int) RoutingMode::DelaySwell) && reverbFlavor.load() != 0;
    if (hasFX && tailUsesReverb)                               // reverb generates the tail → fit it
    {
        // NATURAL DECAY (not sustain): aim for the -40 dB point to land ~AT the end of the
        // swell window (ratio ≈ 1), so across [tailStart, tailStart+swellLen] the forward
        // tail falls a full ~40 dB (loud just-after-source → quiet far tail). Reversed, that
        // becomes a real rising swell — NOT a constant drone. The old band [0.95,1.6] let the
        // tail run up to 1.6× the window (barely decaying → flat); the tight band + lower
        // start estimate make the reverb DECAY within the window instead of being pumped up.
        float decayOv = juce::jlimit(0.10f, 0.92f, 0.10f + (float) swellSec * 0.085f);   // first estimate
        for (int pass = 0; pass < 3; ++pass)
        {
            if (renderWet(decayOv) <= 0) return 0;
            washLen = measureTail();
            const double ratio = (double) (washLen - tailStartFixed) / (double) swellLen;   // -40 dB point vs target
            if (ratio >= 0.9 && ratio <= 1.2) break;                     // tail decays right at the landing window
            const float prev = decayOv;
            if (ratio < 0.9) decayOv = juce::jmin(0.97f, decayOv + juce::jmax(0.05f, (float) ((1.0  - ratio) * 0.45)));
            else             decayOv = juce::jmax(0.06f, decayOv - juce::jmax(0.05f, (float) ((ratio - 1.05) * 0.30)));
            if (std::abs(decayOv - prev) < 0.01f) break;                 // converged / hit a limit
        }
    }
    else
    {
        if (renderWet(-1.0f) <= 0) return 0;
        washLen = measureTail();
    }

    // ---- Stage B: produce EXACTLY swellLen, preferring a NATURAL bloom (no stretch) ---
    // The real producer move (and the Cubase reference) NEVER time-stretches: it
    // renders a reverb tail of the right length and reverses it. So when the bloom
    // fills the swell length (washLen >= swellLen, the common case), take exactly
    // swellLen of the real wet bloom and reverse it — no phase vocoder, no artifacts.
    // The phase-vocoder stretch is only a fallback for very long swells the reverb
    // genuinely can't reach.
    // Build the swell from the wet TAIL after Source End / Tail Start. Everything before
    // the marker is the source-feed region (FX excited by the source) and is skipped, so
    // the reverse lands on the post-source tail — the classic reverse-reverb/delay move.
    const int tailStart = juce::jlimit(0, juce::jmax(0, renderLen - 2), tailStartFixed);
    const int avail     = renderLen - tailStart;                          // forward tail from the marker
    const int tailLen   = juce::jlimit(0, avail, washLen - tailStart);    // -40 dB span (info / log only)

    if (avail >= swellLen)
    {
        // No stretch (the producer move): reverse exactly swellLen of the forward tail —
        // quiet far-tail → loud just-after-source landing at the end. work stays intact.
        for (int c = 0; c < dest.getNumChannels(); ++c)
        {
            const int sc = juce::jmin(c, work.getNumChannels() - 1);
            const float* w = work.getReadPointer(sc);
            float* d = dest.getWritePointer(c);
            for (int i = 0; i < swellLen; ++i) d[i] = w[tailStart + swellLen - 1 - i];
        }
    }
    else
    {
        // Rare (only if the 30 s window cap can't supply a full swell): reverse a COPY of
        // the available tail and fit it to length — never reverse the shared work buffer,
        // so the forward tail survives for the ringout below.
        std::vector<float> tmp((size_t) juce::jmax(1, avail));
        for (int c = 0; c < dest.getNumChannels(); ++c)
        {
            const int sc = juce::jmin(c, work.getNumChannels() - 1);
            const float* w = work.getReadPointer(sc);
            for (int i = 0; i < avail; ++i) tmp[(size_t) i] = w[tailStart + avail - 1 - i];
            float* d = dest.getWritePointer(c);
            if (keepPitch.load())
                btPhaseVocoderStretch(tmp.data(), avail, d, swellLen);
            else { juce::LagrangeInterpolator in; in.process((double) avail / juce::jmax(1, swellLen), tmp.data(), d, swellLen); }
        }
    }

    // ---- SHARED ringout: continue the FORWARD wet tail PAST the landing for EVERY reverb.
    // The rise lands on work[tailStart] (= dest[swellLen-1]); work[tailStart+1 ..] continues
    // it seamlessly. Clamp to where the forward tail naturally falls to ~-60 dB, so SHORT
    // reverbs ring their full natural length and LONG ones fill the requested Ringout —
    // never the -40 dB rise point, never per-flavor gated. (The old code gated on
    // tailLen >= swellLen and clamped to -40 dB, so only the longest-tail reverbs rang out.)
    int ringout = 0;
    if (avail >= swellLen)
    {
        const int want = swellRingoutSamples();
        if (want > 0)
        {
            float pk = 0.0f;
            for (int c = 0; c < work.getNumChannels(); ++c)
                pk = juce::jmax(pk, work.getMagnitude(c, tailStart, swellLen));
            const float thr = juce::jmax(1.0e-5f, pk * 0.001f);            // ~-60 dB of the landing region
            int fe = tailStart + 1;
            for (int c = 0; c < work.getNumChannels(); ++c)
            {
                const float* w = work.getReadPointer(c);
                for (int i = renderLen - 1; i > fe; --i)
                    if (std::abs(w[i]) > thr) { fe = i; break; }
            }
            ringout = juce::jlimit(0, juce::jmax(0, fe - tailStart - 1), want);
        }
    }

    const int outLen = swellLen + ringout;
    if (dest.getNumSamples() < outLen || dest.getNumChannels() < 2)
        dest.setSize(2, outLen, true, true, true);   // keep the rise, zero any new tail region
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        const int sc = juce::jmin(c, work.getNumChannels() - 1);
        auto* d = dest.getWritePointer(c);
        const auto* w = work.getReadPointer(sc);
        for (int i = 0; i < ringout; ++i) d[swellLen + i] = w[tailStart + 1 + i];
    }

    // Boundary shaping: a short fade-IN on the quiet rise start, and a musical release at
    // the VERY END (never at the landing) so nothing hard-cuts. Ringout off → the release
    // alone replaces the old 5 ms chop; Ringout on → it just seats the natural decay.
    const int fin = juce::jmin(swellLen / 2, (int) (currentSR * 0.010));
    const int rel = juce::jmin(outLen / 2, (int) (currentSR * (ringout > 0 ? 0.040 : 0.055)));   // gentler when no tail
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        auto* d = dest.getWritePointer(c);
        for (int i = 0; i < fin; ++i) d[i] *= (float) i / (float) fin;
        for (int i = 0; i < rel; ++i) d[outLen - 1 - i] *= (float) i / (float) rel;
    }

    swellLandingSamp.store(swellLen);   // landing = end of the rise; ringout extends past it
    applyOutputSafety(dest, outLen);

   #if JUCE_DEBUG
    {
        double sum = 0.0;                                   // cheap checksum to prove tail types differ
        const auto* d = dest.getReadPointer(0);
        for (int i = 0; i < swellLen; ++i) sum += std::abs((double) d[i]) * (1.0 + 0.001 * i);
        DBG("[Backtrace tail] type=" << routingModeName(mode)
            << " tailStart=" << tailStart << " tailLen=" << tailLen
            << " washLen=" << washLen << " swellLen=" << swellLen << " ringout=" << ringout
            << " stretch=" << (tailLen >= swellLen ? "none" : "fit") << " checksum=" << juce::String(sum, 3));
    }
   #endif
    return outLen;
}

int BacktraceProcessor::baseLen() const
{
    const int in = trimIn.load(), out = trimOut.load();
    return out - in;   // Tail Types always build a forward tail from the selection
}

// Loads the trimmed selection into dest[0,...): reversed (Land at Source honored)
// or forward, depending on the routing mode. dest is cleared first.
int BacktraceProcessor::fillSource(juce::AudioBuffer<float>& dest, bool reverse)
{
    const int in = trimIn.load(), out = trimOut.load();
    if (out - in <= 0) return 0;

    const auto& slot = sourceSlots[(size_t) activeSource].buffer;   // active source feeds the render

    if (reverse)
        return ReverseEngine::fill(dest, slot, in, out, landAtSource.load());

    const int selLen = juce::jmin(out - in, dest.getNumSamples());
    const auto& src = slot;
    const int srcCh = juce::jmax(1, juce::jmin(2, src.getNumChannels()));
    dest.clear();
    for (int c = 0; c < dest.getNumChannels(); ++c)
        dest.copyFrom(c, 0, src, juce::jmin(c, srcCh - 1), in, selLen);
    return selLen;
}

// ===========================================================================
//  Vault slots (Phase 11B)
// ===========================================================================
int BacktraceProcessor::firstEmptySource() const
{
    for (int i = 0; i < kNumSlots; ++i) if (sourceSlots[(size_t) i].status == SlotEmpty) return i;
    return -1;
}
int BacktraceProcessor::firstEmptyPrint() const
{
    for (int i = 0; i < kNumSlots; ++i) if (printSlots[(size_t) i].status == SlotEmpty) return i;
    return -1;
}

void BacktraceProcessor::setActiveSource(int i)
{
    if (! slotValid(i)) return;
    activeSource = i;
    viewingPrint = false;                 // source selection drives the render again
    trimIn.store(0);
    trimOut.store(sourceSlots[(size_t) i].length);
    tailStartSamp.store(-1);              // Tail Start back to auto (= source end)
    swellStale.store(true);
    slotsChangedFlag.store(true);
}

void BacktraceProcessor::setActivePrint(int i)
{
    if (! slotValid(i)) return;
    activePrint = i;
    viewingPrint = (printSlots[(size_t) i].length > 0);   // show this print in the bottom lane
    swellStale.store(true);
    slotsChangedFlag.store(true);
}

void BacktraceProcessor::clearSource(int i)
{
    if (! slotValid(i)) return;
    auto& s = sourceSlots[(size_t) i];
    s.buffer = juce::AudioBuffer<float>();   // move-assign empty → actually frees the old allocation
    s.length = 0; s.status = SlotEmpty; s.name = {};
    if (i == activeSource) { trimIn.store(0); trimOut.store(0); swellStale.store(true); }
    slotsChangedFlag.store(true);
}

void BacktraceProcessor::clearPrint(int i)
{
    if (! slotValid(i)) return;
    auto& s = printSlots[(size_t) i];
    s.buffer = juce::AudioBuffer<float>();   // move-assign empty → actually frees the old allocation
    s.length = 0; s.status = SlotEmpty; s.name = {};
    if (i == activePrint && viewingPrint) { viewingPrint = false; swellStale.store(true); }
    slotsChangedFlag.store(true);
}

// Copies the just-finished capture into the active SOURCE slot as a Source take.
void BacktraceProcessor::commitCaptureToActiveSource()
{
    const int len = capture.getWritePos();
    if (len <= 0) return;
    auto& s = sourceSlots[(size_t) activeSource];
    s.buffer.setSize(2, len, false, true, true);
    for (int c = 0; c < 2; ++c)
        s.buffer.copyFrom(c, 0, capture.getBuffer(), c, 0, len);
    s.length = len;
    s.status = SlotSource;
    s.sr = capture.getSampleRate();
    if (s.name.isEmpty()) s.name = "Capture " + juce::String(activeSource + 1);
    viewingPrint = false;
    trimIn.store(0);
    trimOut.store(len);
    tailStartSamp.store(-1);   // Tail Start back to auto (= source end)
    swellStale.store(true);
    slotsChangedFlag.store(true);
}

// Echo ring-out length: enough repeats to decay ~60 dB, clamped to a sane range.
// By convention delayParam[0] = Time(ms), delayParam[1] = Feedback/Repeats.
int BacktraceProcessor::tailLen() const
{
    if (delayFlavor.load() == 0) return 0;
    const float fb  = juce::jlimit(0.0f, 0.95f, delayParam[1].load());
    const float dtl = delayParam[0].load() * 0.001f;         // delay time, seconds
    const float repeatsTo60 = (fb < 0.05f) ? 1.0f
                            : 60.0f / (-20.0f * std::log10(fb));
    const float tailSec = juce::jlimit(0.3f, 10.0f, repeatsTo60 * dtl + dtl);
    return (int) (tailSec * currentSR);
}

// Central routing render — used by audition, print, export and drag so what the
// user hears always matches what is printed. Builds dest over [0,total) per the
// routing mode. dest must hold at least `total` samples. Returns the content length.
int BacktraceProcessor::renderProcessed(juce::AudioBuffer<float>& dest, int total)
{
    juce::ScopedNoDenormals noDenormals;   // FDN/feedback loops can produce denormals offline
    const int  m = routingMode.load();

    const int base = fillSource(dest, false);        // forward source
    if (base <= 0) return 0;

    applyPitch(dest, base, pitchSemitones.load());   // pitch the source region only

    const bool hasDelay  = delayFlavor.load()  != 0;
    const bool hasReverb = reverbFlavor.load() != 0;

    applySwellFX(dest, total, m, 0.0);               // Tail-Type FX generates the wet tail (forward)

    const bool tailUsed = hasDelay || hasReverb;
    const int len = tailUsed ? total : base;
    applyOutputSafety(dest, len);   // final global safety belt after all modes

    // short de-click fades at the render boundaries (timing preserved)
    const int fade = juce::jmin(len / 2, (int) (currentSR * 0.004));
    for (int c = 0; c < dest.getNumChannels(); ++c)
    {
        auto* d = dest.getWritePointer(c);
        for (int i = 0; i < fade; ++i) { const float g = (float) i / (float) fade; d[i] *= g; d[len - 1 - i] *= g; }
    }
    return len;
}

// Parallel: delay and reverb fed separately from the same pitched source, then
// averaged (source preserved, wets halved → clearer, not louder).
void BacktraceProcessor::applyParallel(juce::AudioBuffer<float>& buf, int total, bool hasDelay, bool hasReverb)
{
    if (! hasDelay && ! hasReverb) return;
    if (hasDelay != hasReverb) { hasDelay ? applyDelay(buf, total) : applyReverb(buf, total); return; }

    juce::AudioBuffer<float> rev;
    rev.makeCopyOf(buf);
    applyDelay(buf, total);     // buf becomes the delay path
    applyReverb(rev, total);    // rev becomes the reverb path

    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        auto* d = buf.getWritePointer(c);
        const auto* r = rev.getReadPointer(c);
        for (int i = 0; i < total; ++i) d[i] = (d[i] + r[i]) * 0.5f;
    }
}

// Feedback Verb → Delay: reverb of the source is injected into the delay input so
// the repeats smear/evolve. Conservative, safe approximation of true per-sample
// feedback interaction (each stage is internally limited; injection is clamped).
void BacktraceProcessor::applyFeedbackChain(juce::AudioBuffer<float>& buf, int total, bool hasDelay, bool hasReverb)
{
    if (! hasDelay)  { if (hasReverb) applyReverb(buf, total); return; }
    if (! hasReverb) { applyDelay(buf, total); return; }

    juce::AudioBuffer<float> rev;
    rev.makeCopyOf(buf);
    applyReverb(rev, total);    // reverb of the source

    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        auto* s = buf.getWritePointer(c);
        const auto* r = rev.getReadPointer(c);
        for (int i = 0; i < total; ++i) s[i] += r[i] * 0.35f;   // inject reverb into delay input
    }
    applyDelay(buf, total);     // delay repeats the reverb-smeared signal
}

// Offline reverb over buf[0,total). The active flavor owns its dry/wet mix.
// decayOverride >= 0 drives the Decay slot directly (the length-aware tail-fit loop
// sets it); density (0..1) adds diffusion/body for longer swells.
void BacktraceProcessor::applyReverb(juce::AudioBuffer<float>& buf, int total, bool wetOnly,
                                     double fillSec, float decayOverride, float density)
{
    const int flavor = reverbFlavor.load();
    ReverbSpace* space = (flavor == 1) ? (ReverbSpace*) &velvetHall
                       : (flavor == 2) ? (ReverbSpace*) &modernSpace
                       : (flavor == 3) ? (ReverbSpace*) &shimmerVerb
                       : (flavor == 4) ? (ReverbSpace*) &plateVerb
                       : (flavor == 5) ? (ReverbSpace*) &springVerb : nullptr;
    if (space == nullptr) return;

    float p[10];
    for (int i = 0; i < 10; ++i) p[i] = reverbParam[(size_t) i].load();
    const float tail  = macroTail.load();    // Tail macro → longer/lusher reverb trail
    const float ghost = macroGhost.load();   // Ghost macro → blur: diffusion/shimmer/mod/width
    const auto lay = reverbKnobLayout(flavor);
    for (int i = 0; i < lay.size() && i < 10; ++i)
    {
        const auto& nm = lay[i].name;
        if (wetOnly && nm.equalsIgnoreCase("Mix")) p[i] = 1.0f;   // full wet for the swell
        if (nm.equalsIgnoreCase("Decay"))
        {
            // Length-aware decay: the tail-fit loop's override wins; otherwise scale
            // so the natural tail reaches the swell length. Tail macro adds on top.
            float dec = p[i];
            if      (decayOverride >= 0.0f) dec = decayOverride;
            else if (fillSec > 0.0)         dec = juce::jmax(dec, 0.5f + (float) fillSec * 0.05f);
            p[i] = juce::jlimit(0.0f, 1.0f, dec + tail * 0.30f);
        }
        if (nm.equalsIgnoreCase("Diffuse")) p[i] = juce::jlimit(0.0f, 1.0f, p[i] + ghost * 0.45f + density * 0.30f);
        if (nm.equalsIgnoreCase("Shimmer")) p[i] = juce::jlimit(0.0f, 1.0f, p[i] + ghost * 0.45f);
        if (nm.equalsIgnoreCase("Mod"))     p[i] = juce::jlimit(0.0f, 1.0f, p[i] + ghost * 0.35f);
        if (nm.equalsIgnoreCase("Width"))   p[i] = juce::jlimit(0.0f, 1.0f, p[i] + ghost * 0.20f);
    }

    space->reset();
    space->setParams(p);

    const int chans = buf.getNumChannels();
    auto* l = buf.getWritePointer(0);
    auto* r = chans > 1 ? buf.getWritePointer(1) : nullptr;

    for (int i = 0; i < total; ++i)
    {
        const float dryL = l[i];
        const float dryR = r ? r[i] : dryL;
        float outL = 0.0f, outR = 0.0f;
        space->process(dryL, dryR, outL, outR);
        l[i] = outL;
        if (r) r[i] = outR;
    }
}

// Reverb decay ring-out: predelay + decay time + a margin, in samples.
int BacktraceProcessor::reverbTailLen() const
{
    if (reverbFlavor.load() == 0) return 0;
    const float preSec   = reverbParam[2].load() * 0.001f;                 // PreDelay (ms)
    const float decaySec = juce::jmap(juce::jlimit(0.0f, 1.0f, reverbParam[1].load()), 0.0f, 1.0f, 0.4f, 12.0f);
    return (int) ((preSec + decaySec + 0.3f) * currentSR);
}

// Combined FX tail: auto from delay feedback + reverb decay, with a 2 s minimum
// and a 45 s safety cap (per the routing spec). Zero when no FX is active.
int BacktraceProcessor::extraTail() const
{
    if (delayFlavor.load() == 0 && reverbFlavor.load() == 0) return 0;
    const int t = tailLen() + reverbTailLen();
    return juce::jlimit((int) (2.0 * currentSR), (int) (45.0 * currentSR), t);
}

// Desired aftertail length (samples) from the Ringout macro: 0 = off (a short
// release fade still prevents a hard cut), scaling up to ~3 s of natural forward
// FX tail printed AFTER the landing. The render clamps this to the tail actually
// available so it never invents silence.
int BacktraceProcessor::swellRingoutSamples() const
{
    const float rg = macroRingout.load();
    if (rg <= 0.001f) return 0;
    return (int) (juce::jlimit(0.0f, 1.0f, rg) * 3.0f * (float) currentSR);
}

// Global routing safety layer (applied after every mode): per-channel DC blocker
// + transparent-below-unity soft ceiling so summed/feedback paths can never spike.
void BacktraceProcessor::applyOutputSafety(juce::AudioBuffer<float>& buf, int total)
{
    const float thresh = 0.9f;
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        auto* d = buf.getWritePointer(c);
        float x1 = 0.0f, y1 = 0.0f;                 // DC blocker state (~5 Hz HP)
        for (int i = 0; i < total; ++i)
        {
            const float x = d[i];
            const float y = x - x1 + 0.9995f * y1;
            x1 = x; y1 = y;
            float o = y;
            if (o >  thresh) o =  thresh + (1.0f - thresh) * std::tanh((o - thresh) / (1.0f - thresh));
            else if (o < -thresh) o = -thresh - (1.0f - thresh) * std::tanh((-o - thresh) / (1.0f - thresh));
            d[i] = o;
        }
    }
}

// Offline delay over buf[0,total). Source sits in [0,base); the silent tail lets
// the feedback repeats ring out. The active flavor owns its full dry/wet mix.
void BacktraceProcessor::applyDelay(juce::AudioBuffer<float>& buf, int total, bool wetOnly, double fillSec)
{
    const int flavor = delayFlavor.load();
    DelayMachine* machine = (flavor == 1) ? (DelayMachine*) &tapeEcho
                          : (flavor == 2) ? (DelayMachine*) &pedalDigital
                          : (flavor == 3) ? (DelayMachine*) &magneticDrum
                          : (flavor == 4) ? (DelayMachine*) &tapeWitness
                          : (flavor == 5) ? (DelayMachine*) &coldRack
                          : (flavor == 6) ? (DelayMachine*) &vaultDelay
                                          : nullptr;
    if (machine == nullptr) return;

    float p[8];
    for (int i = 0; i < 8; ++i) p[i] = delayParam[(size_t) i].load();

    if (delaySync.load())   // lock delay Time (slot 0) to the host tempo
    {
        double bpm = syncCapture.getBpm();
        if (bpm <= 1.0) bpm = 120.0;
        const double ms = delayDivisionQuarters(delayDivision.load()) * (60000.0 / bpm);
        p[0] = juce::jlimit(1.0f, 2000.0f, (float) ms);
    }
    {
        const float tail  = macroTail.load();    // Tail macro → more feedback repeats
        const float ghost = macroGhost.load();   // Ghost macro → more movement/character (blur)
        const auto lay = delayKnobLayout(flavor);
        for (int i = 0; i < lay.size() && i < 8; ++i)
        {
            const auto& nm = lay[i].name;
            if (wetOnly && nm.equalsIgnoreCase("Mix")) p[i] = 1.0f;
            // Extend Feedback so the echoes sustain across a long swell length.
            if (fillSec > 0.0 && nm.equalsIgnoreCase("Feedback"))
                p[i] = juce::jmax(p[i], juce::jlimit(0.0f, 0.92f, (float) (0.55 + fillSec * 0.03)));
            if (nm.equalsIgnoreCase("Feedback")) p[i] = juce::jlimit(0.0f, 0.92f, p[i] + tail  * 0.30f);
            if (nm.equalsIgnoreCase("Movement")) p[i] = juce::jlimit(0.0f, 1.0f,  p[i] + ghost * 0.40f);
            if (nm.equalsIgnoreCase("Character")) p[i] = juce::jlimit(0.0f, 1.0f, p[i] + ghost * 0.25f);
        }
    }

    machine->reset();
    machine->setParams(p);

    const int chans = buf.getNumChannels();
    auto* l = buf.getWritePointer(0);
    auto* r = chans > 1 ? buf.getWritePointer(1) : nullptr;

    for (int i = 0; i < total; ++i)
    {
        const float dryL = l[i];
        const float dryR = r ? r[i] : dryL;
        float outL = 0.0f, outR = 0.0f;
        machine->process(dryL, dryR, outL, outR);

        l[i] = outL;
        if (r) r[i] = outR;
    }
}

// Offline pitch shift of buf[0..len), latency-compensated so the output stays
// time-aligned with the input. ModernPitchShifter is used here on the message
// thread only (never in processBlock), so there is no real-time contention.
void BacktraceProcessor::applyPitch(juce::AudioBuffer<float>& buf, int len, float semitones)
{
    if (std::abs(semitones) < 0.01f || len <= 0) return;

    pitchShifter.setPitch(semitones);
    pitchShifter.reset();                       // ratio jumps to target (no glide for offline)

    const int latency = pitchShifter.getLatencySamples();
    const int chans   = buf.getNumChannels();
    juce::AudioBuffer<float> tmp(chans, len);
    tmp.clear();

    for (int i = 0; i < len + latency; ++i)
    {
        const float ratio = pitchShifter.advanceAndGetRatio();   // once per sample, shared
        for (int ch = 0; ch < chans; ++ch)
        {
            const float in = (i < len) ? buf.getReadPointer(ch)[i] : 0.0f;
            const float y  = pitchShifter.processSample(ch, in, ratio);
            if (i >= latency) tmp.getWritePointer(ch)[i - latency] = y;
        }
    }

    for (int ch = 0; ch < chans; ++ch)
        buf.copyFrom(ch, 0, tmp, ch, 0, len);
}

// Writes the unified slot sidecar next to the WAV. Phase 1b populates the
// capture/sync block (mode, tempo, time signature, locator range, lock state);
// edit + fx blocks are filled in by later phases (see Docs/Backtrace_DesignSpec.md §6).
void BacktraceProcessor::writeSidecar(const juce::File& wav, int samples, bool reversed)
{
    static const char* kModeNames[] =
        { "manual", "1_bar", "2_bars", "4_bars", "daw_locators", "next_cycle" };
    const int modeIdx = juce::jlimit(0, 5, syncCapture.finMode());
    const double sr = capture.getSampleRate();

    auto* root = new juce::DynamicObject();
    root->setProperty("name",       wav.getFileNameWithoutExtension());
    root->setProperty("created",    juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("sampleRate", sr);
    root->setProperty("samples",    samples);
    root->setProperty("seconds",    samples / sr);

    auto* cap = new juce::DynamicObject();
    cap->setProperty("mode", kModeNames[modeIdx]);
    cap->setProperty("bpm",  syncCapture.finBpm());
    juce::Array<juce::var> ts;
    ts.add(syncCapture.finTsNum());
    ts.add(syncCapture.finTsDen());
    cap->setProperty("timeSig",       juce::var(ts));
    cap->setProperty("ppqStart",      syncCapture.finPpqStart());
    cap->setProperty("ppqEnd",        syncCapture.finPpqEnd());
    cap->setProperty("locatorLocked", syncCapture.finLocked());
    root->setProperty("capture", juce::var(cap));

    auto* edit = new juce::DynamicObject();
    edit->setProperty("trimIn",       trimIn.load());
    edit->setProperty("trimOut",      trimOut.load());
    edit->setProperty("tailStart",    tailStartSamp.load());   // Source End / Tail Start (-1 = auto)
    edit->setProperty("reverse",        reversed);
    edit->setProperty("landAtSource",   landAtSource.load());
    edit->setProperty("pitchSemitones", pitchSemitones.load());
    edit->setProperty("routing",        routingModeName(routingMode.load()));
    root->setProperty("edit", juce::var(edit));

    const int dFlavor = delayFlavor.load();
    const int rFlavor = reverbFlavor.load();
    if (dFlavor != 0 || rFlavor != 0)
    {
        auto* fx = new juce::DynamicObject();

        if (dFlavor != 0)
        {
            auto* delay = new juce::DynamicObject();
            delay->setProperty("model", delayFlavorName(dFlavor));
            const auto layout = delayKnobLayout(dFlavor);
            for (int i = 0; i < layout.size(); ++i)
                if (layout[i].used())
                    delay->setProperty(layout[i].name.toLowerCase(), delayParam[(size_t) i].load());
            fx->setProperty("delay", juce::var(delay));
        }

        if (rFlavor != 0)
        {
            auto* reverb = new juce::DynamicObject();
            reverb->setProperty("model", reverbFlavorName(rFlavor));
            const auto layout = reverbKnobLayout(rFlavor);
            for (int i = 0; i < layout.size(); ++i)
                if (layout[i].used())
                    reverb->setProperty(layout[i].name.toLowerCase(), reverbParam[(size_t) i].load());
            fx->setProperty("reverb", juce::var(reverb));
        }

        root->setProperty("fx", juce::var(fx));
    }

    auto side = wav.getParentDirectory()
                   .getChildFile(wav.getFileNameWithoutExtension() + ".btmeta.json");
    side.replaceWithText(juce::JSON::toString(juce::var(root)));
}

// ===========================================================================
//  Plugin entry point
// ===========================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BacktraceProcessor();
}
