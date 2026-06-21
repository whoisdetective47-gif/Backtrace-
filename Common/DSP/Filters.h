#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

// Per-channel LPF + HPF pair using JUCE's topology-preserving TPT filter.
// TPT filters stay stable when modulated at audio rate — safe for automation.
// One instance per channel (always uses channel index 0 internally).
class ChannelFilter
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
        spec.numChannels      = 1;
        lpf.prepare(spec);
        hpf.prepare(spec);
        lpf.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        hpf.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        lpf.setCutoffFrequency(12000.0f);
        hpf.setCutoffFrequency(20.0f);
        lpf.setResonance(0.65f);
        hpf.setResonance(0.65f);
    }

    void setLPF(float hz)
    {
        hz = std::clamp(hz, 20.0f, 20000.0f);
        if (hz != lastLPF) { lpf.setCutoffFrequency(hz); lastLPF = hz; }
    }

    void setHPF(float hz)
    {
        hz = std::clamp(hz, 10.0f, 5000.0f);
        if (hz != lastHPF) { hpf.setCutoffFrequency(hz); lastHPF = hz; }
    }

    float process(float s)
    {
        float out = lpf.processSample(0, s);
        return hpf.processSample(0, out);
    }

    void reset()
    {
        lpf.reset(); hpf.reset();
        lastLPF = lastHPF = -1.0f;
    }

private:
    juce::dsp::StateVariableTPTFilter<float> lpf, hpf;
    float lastLPF = -1.0f, lastHPF = -1.0f;
};
