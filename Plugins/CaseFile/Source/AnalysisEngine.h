#pragma once
#include <JuceHeader.h>

//==============================================================================
//  Offline evidence analysis for Case File.
//
//  Runs on the message thread against imported audio files (never in
//  processBlock). Streams the file in FFT-sized blocks so a full-length
//  mix never has to sit in memory. Produces broad-band frequency balance,
//  peak/RMS/crest, stereo width (overall + below 120 Hz) and L/R correlation.
//
//  Band levels are stored RELATIVE to the file's total energy so two mixes
//  at different loudness compare fairly — deltas between mixes are what the
//  suspect rules care about.
//==============================================================================

namespace casefile
{

static constexpr int numBands = 7;

// Sub / Bass / Low Mids / Mids / Presence / Harshness / Air
inline const float* bandEdges()
{
    static const float edges[numBands + 1] = { 20.0f, 60.0f, 120.0f, 350.0f, 1500.0f,
                                               5000.0f, 8000.0f, 16000.0f };
    return edges;
}

inline const juce::StringArray& bandNames()
{
    static const juce::StringArray n { "Sub 20-60", "Bass 60-120", "Low Mids 120-350",
                                       "Mids 350-1.5k", "Presence 1.5k-5k",
                                       "Harsh 5k-8k", "Air 8k-16k" };
    return n;
}

struct AnalysisResult
{
    bool   valid       = false;
    double lengthSec   = 0.0;
    double sampleRate  = 0.0;
    int    channels    = 0;
    int    bitDepth    = 0;
    float  peakDb      = -120.0f;
    float  rmsDb       = -120.0f;
    float  crestDb     = 0.0f;
    float  widthPct    = 0.0f;    // side energy % of total (0 mono .. 100 anti-phase)
    float  lowWidthPct = 0.0f;    // same, bins below 120 Hz
    float  corr        = 1.0f;    // L/R correlation, -1..1
    float  bandRel[numBands] = {};// band energy dB relative to total energy
};

//==============================================================================
class AnalysisAccumulator
{
public:
    explicit AnalysisAccumulator (double sr)
        : sampleRate (sr), fft (fftOrder)
    {
        window.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)   // Hann
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) (fftSize - 1)));
        midPow.resize (fftSize / 2 + 1, 0.0);
        sidePow.resize (fftSize / 2 + 1, 0.0);
    }

    // feed consecutive samples; right may be nullptr for mono
    void addBlock (const float* left, const float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            peak    = juce::jmax (peak, std::abs (l), std::abs (r));
            sumSq  += 0.5 * ((double) l * l + (double) r * r);
            sumLR  += (double) l * r;
            sumLL  += (double) l * l;
            sumRR  += (double) r * r;

            fifoMid[(size_t) fifoFill]  = 0.5f * (l + r);
            fifoSide[(size_t) fifoFill] = 0.5f * (l - r);
            if (++fifoFill == fftSize)
            {
                flushFrame();
                fifoFill = 0;
            }
            ++totalSamples;
        }
    }

    AnalysisResult finish()
    {
        if (fifoFill > fftSize / 4)         // analyse a meaningful final partial frame
        {
            std::fill (fifoMid.begin()  + fifoFill, fifoMid.end(),  0.0f);
            std::fill (fifoSide.begin() + fifoFill, fifoSide.end(), 0.0f);
            flushFrame();
        }

        AnalysisResult res;
        res.valid      = totalSamples > 0;
        res.sampleRate = sampleRate;
        res.lengthSec  = sampleRate > 0.0 ? (double) totalSamples / sampleRate : 0.0;

        res.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        res.rmsDb  = totalSamples > 0
                       ? (float) (10.0 * std::log10 (juce::jmax (1.0e-12, sumSq / (double) totalSamples)))
                       : -120.0f;
        res.crestDb = res.peakDb - res.rmsDb;

        const double denom = std::sqrt (juce::jmax (1.0e-12, sumLL * sumRR));
        res.corr = (float) juce::jlimit (-1.0, 1.0, sumLR / denom);

        // band energies from the averaged power spectra
        double bandPow[numBands] = {};
        double midTot = 0.0, sideTot = 0.0, lowMid = 0.0, lowSide = 0.0;
        const auto* edges = bandEdges();
        const double binHz = sampleRate / (double) fftSize;

        for (size_t bin = 1; bin < midPow.size(); ++bin)
        {
            const double f = binHz * (double) bin;
            if (f < edges[0] || f > edges[numBands]) continue;
            const double p = midPow[bin] + sidePow[bin];
            for (int b = 0; b < numBands; ++b)
                if (f >= edges[b] && f < edges[b + 1]) { bandPow[b] += p; break; }
            midTot  += midPow[bin];
            sideTot += sidePow[bin];
            if (f < 120.0) { lowMid += midPow[bin]; lowSide += sidePow[bin]; }
        }

        const double total = juce::jmax (1.0e-12, midTot + sideTot);
        for (int b = 0; b < numBands; ++b)
            res.bandRel[b] = (float) (10.0 * std::log10 (juce::jmax (1.0e-12, bandPow[b] / total)));

        res.widthPct    = (float) (100.0 * sideTot / total);
        const double lowTot = lowMid + lowSide;
        res.lowWidthPct = lowTot > 1.0e-12 ? (float) (100.0 * lowSide / lowTot) : 0.0f;
        return res;
    }

private:
    void flushFrame()
    {
        auto run = [this] (std::vector<float>& fifo, std::vector<double>& powAcc)
        {
            std::vector<float> buf ((size_t) fftSize * 2, 0.0f);
            for (int i = 0; i < fftSize; ++i)
                buf[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
            fft.performRealOnlyForwardTransform (buf.data());
            auto* cplx = reinterpret_cast<const std::complex<float>*> (buf.data());
            for (size_t bin = 0; bin < powAcc.size(); ++bin)
                powAcc[bin] += (double) std::norm (cplx[bin]);
        };
        run (fifoMid, midPow);
        run (fifoSide, sidePow);
    }

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;

    double sampleRate;
    juce::dsp::FFT fft;
    std::vector<float> window;
    std::vector<float> fifoMid  = std::vector<float> ((size_t) fftSize, 0.0f);
    std::vector<float> fifoSide = std::vector<float> ((size_t) fftSize, 0.0f);
    int fifoFill = 0;

    std::vector<double> midPow, sidePow;
    float  peak = 0.0f;
    double sumSq = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    juce::int64 totalSamples = 0;
};

//==============================================================================
inline AnalysisResult analyzeBuffer (const juce::AudioBuffer<float>& buf, double sampleRate)
{
    AnalysisAccumulator acc (sampleRate);
    const float* l = buf.getReadPointer (0);
    const float* r = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : nullptr;
    acc.addBlock (l, r, buf.getNumSamples());
    auto res = acc.finish();
    res.channels = buf.getNumChannels();
    return res;
}

inline AnalysisResult analyzeFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return {};

    AnalysisAccumulator acc (reader->sampleRate);

    const int blockLen = 1 << 16;
    juce::AudioBuffer<float> block ((int) juce::jmin (2u, reader->numChannels) == 1 ? 1 : 2, blockLen);
    // cap at 15 minutes so a stray multi-hour bounce can't stall the UI
    const juce::int64 maxSamples = juce::jmin (reader->lengthInSamples,
                                               (juce::int64) (reader->sampleRate * 60.0 * 15.0));
    for (juce::int64 pos = 0; pos < maxSamples;)
    {
        const int n = (int) juce::jmin ((juce::int64) blockLen, maxSamples - pos);
        if (! reader->read (&block, 0, n, pos, true, true))
            break;
        acc.addBlock (block.getReadPointer (0),
                      block.getNumChannels() > 1 ? block.getReadPointer (1) : nullptr, n);
        pos += n;
    }

    auto res = acc.finish();
    res.channels  = (int) reader->numChannels;
    res.bitDepth  = (int) reader->bitsPerSample;
    res.lengthSec = (double) reader->lengthInSamples / reader->sampleRate;
    return res;
}

} // namespace casefile
