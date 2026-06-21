#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Convenience: set a parameter by its plain value (not normalized 0–1).
inline void setParamValue(juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& id, float plainValue)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(plainValue));
}

// Get current plain value for a parameter.
inline float getParamValue(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id)
{
    return *apvts.getRawParameterValue(id);
}
