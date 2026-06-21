#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// Dust Vault capture buffer.
//
// Records the FINAL stereo plugin output into a pre-allocated 60-second buffer.
// The audio thread is the sole writer; the message thread reads after stop.
// No allocation, locks, or file I/O on the audio thread — when not capturing,
// pushBlock() returns immediately, so it adds no latency and no measurable cost.
class CaptureEngine
{
public:
    static constexpr double kMaxSeconds = 60.0;

    void prepare(double sr)
    {
        sampleRate = sr;
        capacity   = (int)(sr * kMaxSeconds) + 8;
        buffer.setSize(2, capacity, false, true, true);
        buffer.clear();
        writePos.store(0);
        capturing.store(false);
        reachedMax.store(false);
    }

    void start()
    {
        writePos.store(0);
        reachedMax.store(false);
        capturing.store(true);
    }

    void stop() { capturing.store(false); }

    bool   isCapturing()    const { return capturing.load(); }
    bool   hitMax()         const { return reachedMax.load(); }
    int    getWritePos()    const { return writePos.load(); }
    double elapsedSeconds() const { return writePos.load() / sampleRate; }
    double getSampleRate()  const { return sampleRate; }

    // ---- audio thread ----
    void pushBlock(const float* L, const float* R, int n) noexcept
    {
        if (! capturing.load(std::memory_order_relaxed)) return;

        int wp = writePos.load(std::memory_order_relaxed);
        auto* d0 = buffer.getWritePointer(0);
        auto* d1 = buffer.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            if (wp >= capacity)
            {
                capturing.store(false);
                reachedMax.store(true);
                break;
            }
            d0[wp] = L[i];
            d1[wp] = (R != nullptr) ? R[i] : L[i];
            ++wp;
        }
        writePos.store(wp, std::memory_order_relaxed);
    }

    // ---- message thread (after stop) ----
    const juce::AudioBuffer<float>& getBuffer() const { return buffer; }

private:
    juce::AudioBuffer<float> buffer;
    int    capacity   = 0;
    double sampleRate = 44100.0;

    std::atomic<int>  writePos   { 0 };
    std::atomic<bool> capturing  { false };
    std::atomic<bool> reachedMax { false };
};
