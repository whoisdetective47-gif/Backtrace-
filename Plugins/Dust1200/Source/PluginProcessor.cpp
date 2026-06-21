#include "PluginProcessor.h"
#include "PluginEditor.h"

// ===========================================================================
//  Parameters
// ===========================================================================
juce::AudioProcessorValueTreeState::ParameterLayout Dust1200Processor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ---- Dust engine ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"bitDepth",1},    "Bit Depth",
        juce::NormalisableRange<float>(2.0f, 16.0f, 0.01f), 12.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sampleRate",1},  "Sample Rate",
        juce::NormalisableRange<float>(4000.0f, 44100.0f, 1.0f), 26040.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"drive",1},       "Drive",
        juce::NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"crunch",1},      "Crunch",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 20.0f));

    // ---- Pitch ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"samplerSpeed",1}, "Sampler Speed",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"modernPitch",1},  "Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"speedGlide",1},  "Speed Glide",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f), 50.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pitchGlide",1},  "Pitch Glide",
        juce::NormalisableRange<float>(0.0f, 500.0f, 1.0f), 40.0f));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"snap",1},        "Snap", true));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"samplerLink",1}, "Sampler Link", true));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"speedPitchLink",1}, "Speed/Pitch Link", false));

    // ---- Filters ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"hpf",1},         "HPF",
        juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.5f), 20.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"lpf",1},         "LPF",
        juce::NormalisableRange<float>(3000.0f, 20000.0f, 1.0f, 0.4f), 12000.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"tone",1},        "Tone",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));

    // ---- Character ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"jitter",1},      "Jitter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 5.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"jitterRate",1},  "Jitter Rate",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"jitterTrans",1}, "Jitter Transient",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"jitterBlend",1}, "Jitter Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noise",1},       "Noise",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"noiseType",1},   "Noise Type",
        juce::StringArray{ "White", "Pink", "Moog", "ARP", "Vinyl" }, 1));  // default Pink

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noiseHP",1},     "Noise HP",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.5f), 20.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noiseLP",1},     "Noise LP",
        juce::NormalisableRange<float>(500.0f, 20000.0f, 1.0f, 0.4f), 20000.0f));

    // ---- Level ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"mix",1},         "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"output",1},      "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f));

    // ---- Machine Drift ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"machineDrift",1}, "Machine Drift",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 15.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"driftMotion",1},  "Drift Motion",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f));

    // Stereo width: 0 = mono, 50 = neutral/original, 100 = wide
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"driftStereo",1},  "Stereo Width",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"driftTone",1},    "Drift Tone",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));

    // ---- Gate ----
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gateThresh",1},  "Gate Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -60.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gateRelease",1}, "Gate Release",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f, 0.4f), 80.0f));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"gateNoiseSC",1}, "Gate Noise Sidechain", false));

    // ---- Delta monitor ----
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"deltaMode",1}, "Delta Mode", false));

    return { p.begin(), p.end() };
}

// ===========================================================================
//  Constructor
// ===========================================================================
Dust1200Processor::Dust1200Processor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    initPresets();
}

Dust1200Processor::~Dust1200Processor() {}

// ===========================================================================
//  Presets
//  Fields: bits, sr, drive, crunch, samplerSpeed, modernPitch,
//          speedGlide, pitchGlide, snap, samplerLink, speedPitchLink,
//          hpf, lpf, tone, jitter, mix, output, noise,
//          machineDrift, driftMotion, driftStereo, driftTone,
//          gateThresh, gateRelease, gateNoiseSC
// ===========================================================================
void Dust1200Processor::initPresets()
{
    presets = {
        // ---- Classic originals ----
        { "Classic 12/26",
           12.0f, 26040.0f,  0.0f, 20.0f,   0.0f,  0.0f,  50.0f, 40.0f,
           true, true, false, 20.0f, 12000.0f,  0.0f,  5.0f, 100.0f,  0.0f, 0.0f,
           15.0f, 25.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Classic Speed Down",
           12.0f, 26040.0f,  2.0f, 30.0f, -12.0f,  0.0f,  60.0f, 40.0f,
           true, true, false, 20.0f,  9500.0f,-15.0f,  5.0f, 100.0f, -2.0f, 0.0f,
           10.0f, 20.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Pitch Drop Throw",
           12.0f, 24000.0f,  1.0f, 25.0f,   0.0f,-12.0f,  50.0f,120.0f,
           true, true, false, 20.0f,  8000.0f,-20.0f,  4.0f,  40.0f,  0.0f, 2.0f,
            0.0f, 25.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Stretched But Tuned",
           12.0f, 26040.0f,  3.0f, 35.0f, -12.0f,+12.0f,  80.0f, 50.0f,
           true, true, false, 20.0f, 10000.0f,-10.0f,  6.0f,  80.0f, -1.0f, 2.0f,
           20.0f, 20.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Fast Crunch Tuned Down",
           12.0f, 28000.0f,  5.0f, 50.0f, +12.0f,-12.0f,  40.0f, 40.0f,
           true, true, false, 60.0f, 14000.0f, 15.0f,  6.0f, 100.0f,  0.0f, 0.0f,
            0.0f, 25.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Drum Machine Fall",
           12.0f, 26040.0f,  4.0f, 35.0f, -12.0f,  0.0f,  80.0f, 40.0f,
           true, true, false, 20.0f, 10500.0f,-10.0f,  5.0f, 100.0f, -2.0f, 0.0f,
           15.0f, 20.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Vocal Case Drop",
           12.0f, 22000.0f,  1.0f, 25.0f,   0.0f,-12.0f,  50.0f,150.0f,
           false, true, false, 20.0f,  8500.0f,-18.0f,  4.0f,  40.0f,  0.0f, 3.0f,
            0.0f, 25.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Breakbeat Tightener",
           12.0f, 26040.0f,  3.0f, 30.0f,  +5.0f, -3.0f,  40.0f, 40.0f,
           true, true, false, 40.0f, 12000.0f,  5.0f,  5.0f, 100.0f,  0.0f, 0.0f,
           10.0f, 30.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Full Mix Dust",
           12.0f, 26040.0f,  0.0f, 12.0f,   0.0f,  0.0f,  50.0f, 40.0f,
           true, true, false, 20.0f, 14000.0f,  0.0f,  2.0f,  10.0f,  0.0f, 0.0f,
           15.0f, 25.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },

        { "Destroyed Loop",
            8.0f, 12000.0f, 14.0f, 75.0f,   0.0f,  0.0f,  50.0f, 40.0f,
           true, false, false, 30.0f,  6000.0f, 15.0f, 10.0f, 100.0f, -5.0f, 8.0f,
           30.0f, 40.0f, 50.0f, 50.0f,  -60.0f, 80.0f, false },

        // ---- Machine Drift presets ----
        { "Machine Alive",
           12.0f, 26040.0f,  2.0f, 28.0f,   0.0f,  0.0f,  60.0f, 60.0f,
           true, true, false, 20.0f, 11000.0f,  0.0f,  6.0f, 100.0f, -1.0f, 1.0f,
           40.0f, 35.0f, 50.0f, 20.0f,  -60.0f, 80.0f, false },

        { "Old Sampler Bus",
           12.0f, 24000.0f,  3.0f, 22.0f,  -2.0f,  0.0f,  80.0f, 50.0f,
           true, true, false, 25.0f, 10000.0f, -8.0f,  7.0f, 100.0f, -1.0f, 2.0f,
           60.0f, 15.0f, 50.0f, 40.0f,  -60.0f, 80.0f, false },

        { "Wide Dust",
           12.0f, 26040.0f,  1.0f, 18.0f,   0.0f,  0.0f,  70.0f, 50.0f,
           true, true, false, 20.0f, 13000.0f,  0.0f,  4.0f, 100.0f,  0.0f, 0.0f,
           50.0f, 30.0f, 72.0f, 10.0f,  -60.0f, 80.0f, false },

        { "Abused Circuit",
            9.0f, 18000.0f,  8.0f, 55.0f,  -3.0f,  0.0f,  50.0f, 40.0f,
           true, false, false, 30.0f,  8500.0f, 10.0f,  8.0f, 100.0f, -2.0f, 5.0f,
           80.0f, 50.0f, 50.0f, 60.0f,  -60.0f, 80.0f, false },

        { "Full Mix Ghost",
           12.0f, 26040.0f,  0.0f, 10.0f,   0.0f,  0.0f,  50.0f, 40.0f,
           true, true, false, 20.0f, 15000.0f,  0.0f,  2.0f,   8.0f,  0.0f, 0.0f,
           25.0f, 20.0f, 50.0f, 0.0f,  -60.0f, 80.0f, false },
    };
}

void Dust1200Processor::applyPreset(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(presets.size())) return;
    const auto& pr = presets[static_cast<size_t>(idx)];

    auto setF = [&](const juce::String& id, float v) { setParamValue(apvts, id, v); };
    auto setB = [&](const juce::String& id, bool on) {
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(id)))
            b->setValueNotifyingHost(on ? 1.0f : 0.0f);
    };

    setF("bitDepth",      pr.bitDepth);
    setF("sampleRate",    pr.sampleRate);
    setF("drive",         pr.drive);
    setF("crunch",        pr.crunch);
    setF("samplerSpeed",  pr.samplerSpeed);
    setF("modernPitch",   pr.modernPitch);
    setF("speedGlide",    pr.speedGlide);
    setF("pitchGlide",    pr.pitchGlide);
    setB("snap",          pr.snap);
    setB("samplerLink",   pr.samplerLink);
    setB("speedPitchLink",pr.speedPitchLink);
    setF("hpf",           pr.hpf);
    setF("lpf",           pr.lpf);
    setF("tone",          pr.tone);
    setF("jitter",        pr.jitter);
    setF("mix",           pr.mix);
    setF("output",        pr.output);
    setF("noise",         pr.noise);
    setF("machineDrift",  pr.machineDrift);
    setF("driftMotion",   pr.driftMotion);
    setF("driftStereo",   pr.driftStereo);
    setF("driftTone",     pr.driftTone);
    setF("gateThresh",    pr.gateThresh);
    setF("gateRelease",   pr.gateRelease);
    setB("gateNoiseSC",   pr.gateNoiseSC);
}

int  Dust1200Processor::getNumPrograms()  { return static_cast<int>(presets.size()); }
int  Dust1200Processor::getCurrentProgram() { return currentPreset; }
const juce::String Dust1200Processor::getProgramName(int i)
{
    return (i >= 0 && i < static_cast<int>(presets.size())) ? presets[static_cast<size_t>(i)].name
                                                             : juce::String{};
}
void Dust1200Processor::setCurrentProgram(int i) { currentPreset = i; applyPreset(i); }

// ===========================================================================
//  Prepare
// ===========================================================================
void Dust1200Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSR = sampleRate;
    dryBuffer.setSize(kMaxCh, samplesPerBlock);

    samplerEngine.prepare(sampleRate, samplesPerBlock);
    pitchShifter .prepare(sampleRate, samplesPerBlock);
    drift        .prepare(sampleRate);
    widener      .prepare(sampleRate);
    jitterEngine .prepare(sampleRate);
    noiseEngine  .prepare(sampleRate);
#if DUST_VAULT_ENABLED
    capture      .prepare(sampleRate);
    playBuffer.setSize(2, (int)(sampleRate * CaptureEngine::kMaxSeconds) + 8);
    playBuffer.clear();
    playPlaying.store(false);
    playPos.store(0);
    playLen.store(0);
#endif

    juce::dsp::ProcessSpec reconSpec;
    reconSpec.sampleRate       = sampleRate;
    reconSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    reconSpec.numChannels      = 1;

    for (int ch = 0; ch < kMaxCh; ++ch)
    {
        srReducer[ch].prepare(sampleRate);  srReducer[ch].reset();
        filter   [ch].prepare(sampleRate, samplesPerBlock); filter[ch].reset();
        dcBlocker[ch].reset();
        gate     [ch].prepare(sampleRate);  gate[ch].reset();

        reconFilter[ch].prepare(reconSpec);
        reconFilter[ch].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        reconFilter[ch].setResonance(0.707f);   // Butterworth — smooth, no peak
        reconFilter[ch].reset();
    }

    const int totalLatency = SamplerPitchEngine::kInitialDelay
                           + pitchShifter.getLatencySamples()
                           + drift.getCenterDelaySamples()        // wow/flutter center tap
                           + jitterEngine.getCenterDelaySamples(); // jitter center tap
    setLatencySamples(totalLatency);

    // Dry delay line — matches the wet path's latency for a phase-aligned MIX blend.
    dryDelaySamples = totalLatency;
    dryDelayLine.setSize(kMaxCh, totalLatency + 16);
    dryDelayLine.clear();
    dryWritePos = 0;

    const double smt = 0.02;
    auto init = [&](juce::SmoothedValue<float>& sv, float v) {
        sv.reset(sampleRate, smt);
        sv.setCurrentAndTargetValue(v);
    };
    init(sBitDepth,  *apvts.getRawParameterValue("bitDepth"));
    init(sSampleRate,*apvts.getRawParameterValue("sampleRate"));
    init(sDrive,     *apvts.getRawParameterValue("drive"));
    init(sCrunch,    *apvts.getRawParameterValue("crunch")   * 0.01f);
    init(sLPF,       *apvts.getRawParameterValue("lpf"));
    init(sHPF,       *apvts.getRawParameterValue("hpf"));
    init(sTone,      *apvts.getRawParameterValue("tone")     * 0.01f);
    init(sJitter,    *apvts.getRawParameterValue("jitter")   * 0.01f);
    init(sMix,       *apvts.getRawParameterValue("mix")      * 0.01f);
    init(sOutput,    *apvts.getRawParameterValue("output"));
    init(sNoise,     *apvts.getRawParameterValue("noise")    * 0.01f);
}

void Dust1200Processor::releaseResources() {}

// ===========================================================================
//  Processing
// ===========================================================================
void Dust1200Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh  = juce::jmin(buffer.getNumChannels(), kMaxCh);
    const int numSmp = buffer.getNumSamples();

    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSmp);

    // -- Controls --
    const bool  snap      = *apvts.getRawParameterValue("snap")           > 0.5f;
    const bool  linkSR    = *apvts.getRawParameterValue("samplerLink")    > 0.5f;
    const bool  linkSP    = *apvts.getRawParameterValue("speedPitchLink") > 0.5f;
    const float speedGlMs = *apvts.getRawParameterValue("speedGlide");
    const float pitchGlMs = *apvts.getRawParameterValue("pitchGlide");

    float rawSpeed = *apvts.getRawParameterValue("samplerSpeed");
    float rawPitch = *apvts.getRawParameterValue("modernPitch");
    if (snap)  { rawSpeed = std::round(rawSpeed); rawPitch = std::round(rawPitch); }
    if (linkSP)  rawPitch = rawSpeed;

    samplerEngine.setPitch(rawSpeed, 0.0f);
    samplerEngine.setGlideTime(speedGlMs);
    pitchShifter .setPitch(rawPitch);
    pitchShifter .setGlideTime(pitchGlMs);

    sBitDepth  .setTargetValue(*apvts.getRawParameterValue("bitDepth"));
    sSampleRate.setTargetValue(*apvts.getRawParameterValue("sampleRate"));
    sDrive     .setTargetValue(*apvts.getRawParameterValue("drive"));
    sCrunch    .setTargetValue(*apvts.getRawParameterValue("crunch")  * 0.01f);
    sLPF       .setTargetValue(*apvts.getRawParameterValue("lpf"));
    sHPF       .setTargetValue(*apvts.getRawParameterValue("hpf"));
    sTone      .setTargetValue(*apvts.getRawParameterValue("tone")    * 0.01f);
    sJitter    .setTargetValue(*apvts.getRawParameterValue("jitter")  * 0.01f);
    sMix       .setTargetValue(*apvts.getRawParameterValue("mix")     * 0.01f);
    sOutput    .setTargetValue(*apvts.getRawParameterValue("output"));
    sNoise     .setTargetValue(*apvts.getRawParameterValue("noise")   * 0.01f);

    // -- Gate: update threshold and release once per block --
    {
        const float gateThreshDB  = *apvts.getRawParameterValue("gateThresh");
        const float gateReleaseMs = *apvts.getRawParameterValue("gateRelease");
        for (int ch = 0; ch < numCh; ++ch)
        {
            gate[ch].setThreshold(gateThreshDB);
            gate[ch].setRelease(gateReleaseMs);
        }
    }
    const bool gateNoiseSC = *apvts.getRawParameterValue("gateNoiseSC") > 0.5f;
    const bool gateActive  = *apvts.getRawParameterValue("gateThresh") > -59.5f || gateNoiseSC;
    const bool deltaMode   = *apvts.getRawParameterValue("deltaMode")   > 0.5f;

    // -- Machine Drift (mono wow/flutter): update once per block --
    drift.setParams(
        *apvts.getRawParameterValue("machineDrift"),
        *apvts.getRawParameterValue("driftMotion"));

    // -- Stereo widener width (driven by the STEREO control) --
    widener.setWidth(*apvts.getRawParameterValue("driftStereo"));

    // -- Jitter (unstable sample-clock instability) --
    jitterEngine.setDepth    (*apvts.getRawParameterValue("jitter"));
    jitterEngine.setRate     (*apvts.getRawParameterValue("jitterRate"));
    jitterEngine.setTransient(*apvts.getRawParameterValue("jitterTrans"));
    jitterEngine.setBlend    (*apvts.getRawParameterValue("jitterBlend"));

    // -- Noise engine: type + HP/LP shaping --
    noiseEngine.setType((int)*apvts.getRawParameterValue("noiseType"));
    noiseEngine.setFilters(*apvts.getRawParameterValue("noiseHP"),
                           *apvts.getRawParameterValue("noiseLP"));

    static constexpr int kFilterStep = 32;

    const int dryRing = dryDelayLine.getNumSamples();
    int       wp      = dryWritePos;

    for (int i = 0; i < numSmp; ++i)
    {
        const float bitDepth  = sBitDepth  .getNextValue();
        const float srTarget  = sSampleRate.getNextValue();
        const float driveDb   = sDrive     .getNextValue();
        const float crunch    = sCrunch    .getNextValue();
        const float lpfHz     = sLPF       .getNextValue();
        const float hpfHz     = sHPF       .getNextValue();
        const float tone      = sTone      .getNextValue();
        const float mix       = sMix       .getNextValue();
        const float outGainDb = sOutput    .getNextValue();
        const float noiseAmt  = sNoise     .getNextValue();

        const float driveLinear = juce::Decibels::decibelsToGain(driveDb);
        const float outGainLin  = juce::Decibels::decibelsToGain(outGainDb);
        const float tonedLPF    = juce::jlimit(300.0f, 20000.0f, lpfHz * std::pow(2.0f, tone));

        const float speedRatio = samplerEngine.advanceAndGetRatio();
        const float pitchRatio = pitchShifter .advanceAndGetRatio();

        float baseSR = srTarget;
        if (linkSR)
            baseSR = juce::jlimit(1000.0f, 44100.0f, srTarget * speedRatio);

        // Update filters every 32 samples
        if (i % kFilterStep == 0)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                filter[ch].setLPF(tonedLPF);
                filter[ch].setHPF(hpfHz);
            }
        }

        // Advance wow/flutter LFOs once per sample (shared across channels)
        drift.tickLFO();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float chSR = juce::jlimit(1000.0f, 44100.0f, baseSR);

            srReducer[ch].setTargetSampleRate(chSR);
            srReducer[ch].setJitter(0.0f);   // jitter handled by dedicated engine below
            bitReducer[ch].setBitDepth(bitDepth);

            float* data = buffer.getWritePointer(ch);
            float  s    = data[i];

            // 1. Drive
            s = saturation.process(s, driveLinear, crunch);

            // 2. Sampler Speed (varispeed)
            s = samplerEngine.processSample(ch, s, speedRatio);

            // 3. Modern Pitch
            s = pitchShifter.processSample(ch, s, pitchRatio);

            // 3.5 Jitter — unstable sample-clock timing instability feeding the converter
            s = jitterEngine.processSample(ch, s);

            // 4. Sample-rate reduction
            s = srReducer[ch].process(s);

            // 5. Bit reduction
            s = bitReducer[ch].process(s);

            // 5.5 Reconstruction filter — analog-style output LPF tracking the
            //     reduced Nyquist (chSR/2). Tames ZOH images + quantization hiss
            //     into a warm vintage tone instead of harsh fold-back fizz.
            //     Wide open at high SR; closes down as SAMPLE RATE is lowered.
            {
                const float reconHz = juce::jlimit(200.0f,
                                                   static_cast<float>(currentSR * 0.49),
                                                   chSR * 0.5f);
                reconFilter[ch].setCutoffFrequency(reconHz);
                s = reconFilter[ch].processSample(0, s);
            }

            // 6. Noise floor — shaped character noise (type + HP/LP)
            float noiseSmp = (noiseAmt > 0.0f) ? noiseEngine.process(ch) * noiseAmt * 0.03f : 0.0f;
            s += noiseSmp;

            // 7. Filters (per-channel, updated every 32 samples above)
            s = filter[ch].process(s);

            // 8. DC blocker
            s = dcBlocker[ch].process(s);

            // 8.2 Machine Drift — wow/flutter pitch movement + stereo width.
            //     Per-channel modulated delay; left/right decorrelated by STEREO.
            s = drift.processSample(ch, s);

            // Latency-aligned dry: delay the dry by the wet path's latency so the
            // MIX blend, gate key, and Delta are all time-aligned. Without this the
            // dry sits ahead of the wet → flamming/comb whenever MIX is not full wet.
            float* dl  = dryDelayLine.getWritePointer(ch);
            dl[wp]     = dryBuffer.getReadPointer(ch)[i];
            int    rp  = wp - dryDelaySamples;
            if (rp < 0) rp += dryRing;
            const float dry = dl[rp];

            // 8.5. Gate — processes the full wet signal.
            //   Standard mode : keyed by the latency-aligned dry level.
            //   Noise SC mode : keyed by the noise burst amplitude.
            if (gateActive)
            {
                float keySignal = gateNoiseSC ? noiseSmp : dry;
                s = gate[ch].process(s, keySignal);
            }

            // 9. Output gain
            s *= outGainLin;

            // 10. Dry/wet mix  — or delta (wet − dry) for monitoring
            data[i] = deltaMode ? (s - dry)
                                : dry + mix * (s - dry);
        }

        if (++wp >= dryRing) wp = 0;
    }

    dryWritePos = wp;

    // -- Stereo widener: two-stage M/S width + decorrelated side for mono sources.
    //    Applied to the final stereo output; mid is untouched so mono fold-down
    //    stays clean. Skipped for mono buses.
    if (numCh == 2)
        widener.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSmp);

#if DUST_VAULT_ENABLED
    // -- Dust Vault: capture the final output (post mix/output/delta/widener) --
    capture.pushBlock(buffer.getReadPointer(0),
                      numCh > 1 ? buffer.getReadPointer(1) : nullptr,
                      numSmp);

    // -- Dust Vault: region audition — overwrites output with the captured region --
    if (playPlaying.load())
    {
        int       pos = playPos.load();
        const int len = playLen.load();
        auto* o0 = buffer.getWritePointer(0);
        auto* o1 = numCh > 1 ? buffer.getWritePointer(1) : nullptr;
        const auto* p0 = playBuffer.getReadPointer(0);
        const auto* p1 = playBuffer.getReadPointer(1);

        for (int i = 0; i < numSmp; ++i)
        {
            if (pos >= len) { o0[i] = 0.0f; if (o1) o1[i] = 0.0f; playPlaying.store(false); continue; }
            o0[i] = p0[pos];
            if (o1) o1[i] = p1[pos];
            ++pos;
        }
        playPos.store(pos);
    }
#endif
}

// ===========================================================================
//  Editor / State
// ===========================================================================
juce::AudioProcessorEditor* Dust1200Processor::createEditor()
{
    return new Dust1200Editor(*this);
}

void Dust1200Processor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}

void Dust1200Processor::setStateInformation(const void* data, int bytes)
{
    if (auto xml = getXmlFromBinary(data, bytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

#if DUST_VAULT_ENABLED
// ===========================================================================
//  Dust Vault — capture export / library
// ===========================================================================
juce::File Dust1200Processor::vaultCapturesDir() const
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
             .getChildFile("Dust Vault")
             .getChildFile("Captures");
}

void Dust1200Processor::vaultOpenFolder()
{
    auto dir = vaultCapturesDir();
    dir.createDirectory();
    dir.revealToUser();
}

juce::File Dust1200Processor::vaultFinalizeCapture()
{
    const int len = capture.getWritePos();
    if (len <= 0) return {};

    auto dir = vaultCapturesDir();
    dir.createDirectory();

    const auto ts   = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S");
    const int  bits = (int) *apvts.getRawParameterValue("bitDepth");
    const int  srHz = (int) *apvts.getRawParameterValue("sampleRate");
    const int  spd  = (int) std::round(*apvts.getRawParameterValue("samplerSpeed"));
    const int  pit  = (int) std::round(*apvts.getRawParameterValue("modernPitch"));

    juce::String fname = "DUST1247_Capture_" + ts + "_"
                       + juce::String(bits) + "bit_"
                       + juce::String(srHz) + "Hz_"
                       + juce::String(spd)  + "st_"
                       + juce::String(pit)  + "st.wav";

    juce::File f = dir.getChildFile(fname);
    if (f.existsAsFile()) f = f.getNonexistentSibling();   // never overwrite

    if (auto os = f.createOutputStream())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(os.get(), capture.getSampleRate(), 2, 24, {}, 0));

        if (writer != nullptr)
        {
            os.release();   // writer now owns the stream
            writer->writeFromAudioSampleBuffer(capture.getBuffer(), 0, len);
            writer.reset(); // flush + close

            VaultCapture vc { f, f.getFileNameWithoutExtension(),
                              len / capture.getSampleRate() };
            vaultCaptures.insert(vaultCaptures.begin(), vc);   // newest first
            return f;
        }
    }
    return {};
}

void Dust1200Processor::vaultStartPlayback(const juce::AudioBuffer<float>& src, int start, int end)
{
    playPlaying.store(false);   // stop first → safe to overwrite playBuffer (audio thread idle on it)

    const int cap = playBuffer.getNumSamples();
    start = juce::jlimit(0, src.getNumSamples(), start);
    end   = juce::jlimit(0, src.getNumSamples(), end);
    const int len = juce::jlimit(0, cap, end - start);
    if (len <= 0) return;

    const int srcCh = src.getNumChannels();
    for (int ch = 0; ch < 2; ++ch)
        playBuffer.copyFrom(ch, 0, src, juce::jmin(ch, srcCh - 1), start, len);

    playLen.store(len);
    playPos.store(0);
    playPlaying.store(true);
}
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Dust1200Processor();
}
