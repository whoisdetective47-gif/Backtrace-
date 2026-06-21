#pragma once
#include <JuceHeader.h>

// Dust Vault capture/export/library feature — set to 0 to compile it out entirely.
#ifndef DUST_VAULT_ENABLED
 #define DUST_VAULT_ENABLED 1
#endif
#include "DSP/BitReducer.h"
#include "DSP/SampleRateReducer.h"
#include "DSP/SamplerPitchEngine.h"
#include "DSP/ModernPitchShifter.h"
#include "DSP/Saturation.h"
#include "DSP/Filters.h"
#include "DSP/DCBlocker.h"
#include "DSP/MachineDrift.h"
#include "DSP/StereoWidener.h"
#include "DSP/JitterEngine.h"
#include "DSP/NoiseEngine.h"
#include "DSP/NoiseGate.h"
#include "Vault/CaptureEngine.h"
#include "Utilities/ParameterHelpers.h"

class Dust1200Processor : public juce::AudioProcessor
{
public:
    Dust1200Processor();
    ~Dust1200Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Detective 47s Dust 12.47"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#if DUST_VAULT_ENABLED
    // ---- Dust Vault (capture / export / library) ----
    struct VaultCapture { juce::File file; juce::String name; double seconds = 0.0; };

    void   vaultStartCapture()        { capture.start(); }
    void   vaultStopCapture()         { capture.stop();  }
    bool   vaultIsCapturing()  const  { return capture.isCapturing(); }
    double vaultElapsed()      const  { return capture.elapsedSeconds(); }

    juce::File vaultFinalizeCapture();        // writes WAV, prepends to list, returns file
    juce::File vaultCapturesDir() const;
    void       vaultOpenFolder();
    std::vector<VaultCapture>& vaultGetCaptures() { return vaultCaptures; }

    // ---- region playback (audition the trimmed region through the plugin output) ----
    void vaultStartPlayback(const juce::AudioBuffer<float>& src, int start, int end);
    void vaultStopPlayback() { playPlaying.store(false); }
    bool vaultIsPlaying() const { return playPlaying.load(); }
#endif

private:
    static constexpr int kMaxCh = 2;

    // DSP chain
    SamplerPitchEngine  samplerEngine;
    ModernPitchShifter  pitchShifter;
    BitReducer          bitReducer  [kMaxCh];
    SampleRateReducer   srReducer   [kMaxCh];
    // Analog-style reconstruction LPF — tracks the reduced Nyquist to tame the
    // zero-order-hold images (warm vintage output stage, not harsh aliasing).
    juce::dsp::StateVariableTPTFilter<float> reconFilter[kMaxCh];
    ChannelFilter       filter      [kMaxCh];
    DCBlocker           dcBlocker   [kMaxCh];
    Saturation          saturation;
    MachineDrift        drift;
    StereoWidener       widener;
    JitterEngine        jitterEngine;
    NoiseEngine         noiseEngine;
    NoiseGate           gate[kMaxCh];
#if DUST_VAULT_ENABLED
    CaptureEngine             capture;
    std::vector<VaultCapture> vaultCaptures;
    juce::AudioBuffer<float>  playBuffer;
    std::atomic<bool>         playPlaying { false };
    std::atomic<int>          playPos { 0 }, playLen { 0 };
#endif

    juce::AudioBuffer<float> dryBuffer;

    // Delays the dry signal by the wet path's latency so the MIX blend, gate key,
    // and Delta monitor are all time-aligned (no flam/comb when blending).
    juce::AudioBuffer<float> dryDelayLine;
    int dryDelaySamples = 0;
    int dryWritePos     = 0;

    uint32_t noiseRng = 0xF1234567u;
    inline float nextNoise()
    {
        noiseRng = noiseRng * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noiseRng)) * (1.0f / 2147483648.0f);
    }

    juce::SmoothedValue<float> sBitDepth, sSampleRate, sDrive, sCrunch;
    juce::SmoothedValue<float> sLPF, sHPF, sTone, sJitter, sMix, sOutput, sNoise;

    struct Preset {
        juce::String name;
        float bitDepth, sampleRate, drive, crunch;
        float samplerSpeed, modernPitch, speedGlide, pitchGlide;
        bool  snap, samplerLink, speedPitchLink;
        float hpf, lpf, tone, jitter, mix, output, noise;
        // Machine Drift
        float machineDrift, driftMotion, driftStereo, driftTone;
        // Gate
        float gateThresh, gateRelease;
        bool  gateNoiseSC;
    };
    std::vector<Preset> presets;
    int currentPreset = 0;

    void initPresets();
    void applyPreset(int index);

    double currentSR = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dust1200Processor)
};
