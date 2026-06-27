#pragma once
#include <JuceHeader.h>

// =============================================================================
//  ReverseEngine — builds a reversed copy of a trimmed selection (Phase 3).
//
//  The selection [trimIn, trimOut) is flipped so the original last sample plays
//  first and the original first sample plays last.
//
//  Land at Source: the reversed audio keeps the selection's ORIGINAL position,
//  so its endpoint still lands at trimOut (the right locator) — the lead-in is
//  silence. Useful for reverse swells that must crest on a downbeat. With it
//  off, the reversed clip is butted to t=0 and plays immediately.
//
//  fill() writes into a pre-sized destination (no allocation) so it is safe to
//  call from the message thread without reallocating a buffer the audio thread
//  may still be reading.
// =============================================================================
class ReverseEngine
{
public:
    // dest must already hold at least trimOut samples (2 channels). Returns the
    // number of valid samples written (the playable length).
    static int fill(juce::AudioBuffer<float>& dest,
                    const juce::AudioBuffer<float>& src,
                    int trimIn, int trimOut, bool landAtSource)
    {
        const int selLen = trimOut - trimIn;
        if (selLen <= 0 || dest.getNumSamples() <= 0) return 0;

        const int outLen = juce::jmin(landAtSource ? trimOut : selLen, dest.getNumSamples());
        const int offset = landAtSource ? trimIn : 0;
        const int srcCh  = juce::jmax(1, juce::jmin(2, src.getNumChannels()));

        dest.clear();

        const int writeable = juce::jmin(selLen, outLen - offset);
        for (int c = 0; c < dest.getNumChannels(); ++c)
        {
            const float* s = src.getReadPointer(juce::jmin(c, srcCh - 1));
            float*       d = dest.getWritePointer(c);
            for (int i = 0; i < writeable; ++i)
                d[offset + i] = s[trimOut - 1 - i];   // flip
        }
        return outLen;
    }
};
