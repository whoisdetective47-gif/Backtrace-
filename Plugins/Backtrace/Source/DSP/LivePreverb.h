#pragma once
#include <JuceHeader.h>

// =============================================================================
//  LivePreverb — real-time DAW-synced reverse-reverb / PREVERB engine (Phase 1).
//
//  Reverse reverb = convolution with a TIME-REVERSED reverb impulse response.
//  A forward reverb IR starts loud (the onset) and decays; reversed, it builds
//  UP to a spike at the end. Convolving the live input with that reversed IR
//  makes every sound bloom a swell that LEADS INTO it — the reverb crescendos
//  before the transient. We then delay the DRY so it lands on the swell's peak,
//  and report that delay as plugin latency so the host pulls the whole thing
//  early — the preverb ends up sitting in front of the original sound.
//
//  This is linear convolution (juce::dsp::Convolution, non-uniform partitioned),
//  so it is glitch-free and stable; changing pre-swell length or reverb type
//  just swaps the IR (thread-safe loadImpulseResponse). The Capture/Print engine
//  is untouched — this is a separate real-time path.
//
//  Phase-1 limitation: introduces latency = pre-swell length (a mix effect, not a
//  zero-latency tracking effect), as intended for a preverb/transition designer.
// =============================================================================
class LivePreverb
{
public:
    void prepare(double sr, int blockSize, int numCh)
    {
        sampleRate = sr;
        maxBlock   = juce::jmax(16, blockSize);
        channels   = juce::jlimit(1, 2, numCh);

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, (juce::uint32) channels };
        conv.prepare(spec);
        conv.reset();

        wetBuf.setSize(channels, maxBlock, false, false, true);
        const int maxDelay = (int) (sr * 9.0) + maxBlock + 8;   // pre-swell ceiling + headroom
        dryDelay.setSize(channels, maxDelay, false, false, true);
        dryDelay.clear();
        dryWp = 0;
        ready.store(false);
    }

    void reset()
    {
        conv.reset();
        if (dryDelay.getNumSamples() > 0) dryDelay.clear();
        dryWp = 0; prevWet = 0.0f; prevDry = 1.0f;
    }

    // FIXED at the NonUniform head size. conv.getLatency() is unreliable (0 before the async
    // kernel load, the real value after) — using it makes the reported latency oscillate,
    // which triggers an endless host PDC re-sync loop (frozen transport). NonUniform's latency
    // IS the head size, so this constant is both correct AND deterministic.
    int  convLatency() const { return kConvHead; }
    int  getLatencySamples() const { return dryLatency.load(); }
    bool isReady() const { return ready.load(); }
    int  loadedIRSize() const { return (int) conv.getCurrentIRSize(); }   // 0 until the kernel finishes loading

    // Load a reversed reverb IR (the preverb kernel). Safe to call off the audio
    // thread — Convolution swaps the kernel atomically. irLen = pre-swell samples.
    void setIR(juce::AudioBuffer<float>&& reversedIR, double sr)
    {
        const int irLen = reversedIR.getNumSamples();
        conv.loadImpulseResponse(std::move(reversedIR), sr,
                                 channels > 1 ? juce::dsp::Convolution::Stereo::yes
                                              : juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::no);
        // Dry must land on the swell PEAK (end of the reversed IR) + the convolution's
        // own processing latency, so the swell builds in the samples before it.
        const int d = juce::jlimit(0, dryDelay.getNumSamples() - 1, convLatency() + juce::jmax(0, irLen - 1));
        dryLatency.store(d);
        ready.store(true);
    }

    // Real-time block: wet = reversed-IR convolution of the input; out = delayed dry
    // (× dryGain) + wet (× wetGain), with a soft ceiling. Bypasses to delayed dry if
    // the IR isn't loaded yet (so the audio path is always continuous — no hard-cut).
    void process(juce::AudioBuffer<float>& buffer, float wetGain, float dryGain)
    {
        const int n  = buffer.getNumSamples();
        const int ch = juce::jmin(channels, buffer.getNumChannels());
        if (n <= 0 || ch <= 0 || dryDelay.getNumSamples() <= 0) return;   // not prepared → leave audio untouched

        const bool wet  = ready.load();
        const int  wetN = wet ? juce::jmin(n, maxBlock) : 0;   // conv + wetBuf sized for maxBlock — NEVER exceed
        if (wetN > 0)
        {
            for (int c = 0; c < ch; ++c) wetBuf.copyFrom(c, 0, buffer, c, 0, wetN);
            juce::dsp::AudioBlock<float> block(wetBuf.getArrayOfWritePointers(), (size_t) ch, (size_t) wetN);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            conv.process(ctx);
        }

        const int dN = dryDelay.getNumSamples();
        const int dl = dryLatency.load();
        // Linear-ramp the gains from last block's values to the new targets → no zipper on
        // Mix moves. Both channels re-ramp from the same start, so they stay phase-locked.
        const float wet0 = prevWet, dry0 = prevDry;
        const float wetInc = (wetGain - wet0) / (float) juce::jmax(1, n);
        const float dryInc = (dryGain - dry0) / (float) juce::jmax(1, n);
        for (int c = 0; c < ch; ++c)
        {
            auto* io = buffer.getWritePointer(c);
            auto* dline = dryDelay.getWritePointer(c);
            const float* w = wet ? wetBuf.getReadPointer(c) : nullptr;
            int wp = dryWp; float wg = wet0, dg = dry0;
            for (int i = 0; i < n; ++i)
            {
                const float in = io[i];
                dline[wp] = in;
                int rp = wp - dl; if (rp < 0) rp += dN;
                float o = dline[rp] * dg + ((wet && i < wetN) ? w[i] * wg : 0.0f);
                o = std::tanh(o);                                    // soft ceiling — protection only
                io[i] = o;
                wg += wetInc; dg += dryInc;
                if (++wp >= dN) wp = 0;
            }
        }
        dryWp = (dryWp + n) % dN;
        prevWet = wetGain; prevDry = dryGain;
    }

private:
    // The preverb already has seconds of (pre-swell) latency, so a low-latency convolution
    // head is wasted CPU. A larger head (~43 ms) means far fewer small FFTs → much lower CPU
    // for long IRs (the main cause of Cubase stalls on longer Swell Times).
    static constexpr int kConvHead = 2048;                                       // NonUniform head = conv latency
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { kConvHead } };   // low-CPU partitioned conv
    juce::AudioBuffer<float> wetBuf, dryDelay;
    double sampleRate = 44100.0;
    int    channels = 2, maxBlock = 512, dryWp = 0;
    float  prevWet = 0.0f, prevDry = 1.0f;   // gain-ramp state (zipper-free Mix)
    std::atomic<int>  dryLatency { 0 };
    std::atomic<bool> ready { false };
};
