// End-to-end render measurement test. Drives the REAL BacktraceProcessor offline
// (import a synthetic source → set settings → Create-Swell render → export WAV) and
// MEASURES the musical behaviour the correction pass promised: ringout past the
// landing (no dead-stop), Swell Level, gain staging, filter-motion darkening, and
// bit-exact determinism. Links the processor headless via BACKTRACE_NO_EDITOR.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "DSP/DelayMachine.h"
#include "DSP/ReverbSpace.h"
#include "Utilities/FactoryPresets.h"
#include <cstdio>
#include <cmath>

static constexpr double SR = 48000.0;

static void writeWav(const juce::File& f, const juce::AudioBuffer<float>& buf, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
    if (w) { os.release(); w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); }
}

static juce::AudioBuffer<float> readWav(const juce::File& f, double& sr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    juce::AudioBuffer<float> b;
    if (r) { b.setSize((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); sr = r->sampleRate; }
    return b;
}

// A snare-like hit: short noise burst, fast exponential decay (hot transient).
static void synthSnare(const juce::File& f)
{
    const int n = (int) (SR * 0.16);
    juce::AudioBuffer<float> b(2, n); juce::Random rng(7);
    for (int i = 0; i < n; ++i)
    {
        const float env = std::exp(-(float) i / (float) (SR * 0.03));
        const float s = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.9f;
        b.setSample(0, i, s); b.setSample(1, i, s);
    }
    writeWav(f, b, SR);
}

// A vocal-like word: 180 Hz tone + a couple of harmonics with an amplitude envelope.
static void synthVocal(const juce::File& f)
{
    const int n = (int) (SR * 0.40);
    juce::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) SR;
        const float env = std::sin(juce::MathConstants<float>::pi * (float) i / (float) n);   // soft in/out
        const float s = env * 0.6f * (std::sin(juce::MathConstants<float>::twoPi * 180.0f * t)
                                    + 0.4f * std::sin(juce::MathConstants<float>::twoPi * 360.0f * t)
                                    + 0.2f * std::sin(juce::MathConstants<float>::twoPi * 540.0f * t));
        b.setSample(0, i, s); b.setSample(1, i, s);
    }
    writeWav(f, b, SR);
}

static double rms(const juce::AudioBuffer<float>& b, int a, int e)
{
    a = juce::jmax(0, a); e = juce::jmin(b.getNumSamples(), e);
    if (e <= a) return 0.0;
    double s = 0.0; const auto* d = b.getReadPointer(0);
    for (int i = a; i < e; ++i) s += (double) d[i] * d[i];
    return std::sqrt(s / (double) (e - a));
}
static float peakRange(const juce::AudioBuffer<float>& b, int a, int e)
{
    a = juce::jmax(0, a); e = juce::jmin(b.getNumSamples(), e);
    float p = 0.0f; for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = a; i < e; ++i) p = juce::jmax(p, std::abs(b.getSample(c, i)));
    return p;
}
// HF/LF energy ratio in [a,e) via a one-pole split at fc — higher = brighter.
static double hflfRatio(const juce::AudioBuffer<float>& b, int a, int e, double fc)
{
    a = juce::jmax(0, a); e = juce::jmin(b.getNumSamples(), e);
    const double k = std::exp(-2.0 * juce::MathConstants<double>::pi * fc / SR);
    double lp = 0.0, lfE = 0.0, hfE = 0.0; const auto* d = b.getReadPointer(0);
    for (int i = a; i < e; ++i) { lp = (1.0 - k) * d[i] + k * lp; const double hf = d[i] - lp; lfE += lp * lp; hfE += hf * hf; }
    return hfE / juce::jmax(1.0e-12, lfE);
}

static double maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int n  = juce::jmin(a.getNumSamples(),  b.getNumSamples());
    const int ch = juce::jmin(a.getNumChannels(), b.getNumChannels());
    double m = 0.0;
    for (int c = 0; c < ch; ++c)
    {
        const float* x = a.getReadPointer(c); const float* y = b.getReadPointer(c);
        for (int i = 0; i < n; ++i) m = juce::jmax(m, (double) std::abs(x[i] - y[i]));
    }
    return m;
}

static void setReverb(BacktraceProcessor& p, int flavor)
{
    p.setReverbFlavor(flavor);
    const auto rl = reverbKnobLayout(flavor);
    for (int i = 0; i < rl.size() && i < 10; ++i) if (rl[i].used()) p.setReverbParam(i, rl[i].def);
}
static void setDelay(BacktraceProcessor& p, int flavor, float timeMs, float feedback)
{
    p.setDelayFlavor(flavor);
    const auto dl = delayKnobLayout(flavor);
    for (int i = 0; i < dl.size() && i < 8; ++i)
    {
        if (! dl[i].used()) continue;
        float v = dl[i].def; const auto n = dl[i].name.toLowerCase();
        if (n == "time") v = timeMs; else if (n.contains("feedback") || n.contains("repeats")) v = feedback;
        else if (n == "mix") v = 1.0f;
        p.setDelayParam(i, v);
    }
}

static int g_pass = 0, g_fail = 0;
static void check(const juce::String& name, bool ok, const juce::String& detail)
{
    std::printf("  [%s] %-42s %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    if (ok) ++g_pass; else ++g_fail;
}

// Force a fresh render (mirrors pressing Create Swell) and export the final buffer.
static juce::AudioBuffer<float> createSwell(BacktraceProcessor& p, const juce::File& dir, int& landing, double& sr)
{
    p.setSwellMode(true);
    p.regenerateSwell();
    auto f = p.vaultRenderForDrag();
    landing = p.getSwellLanding();
    return readWav(f, sr);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("Backtrace END-TO-END render measurement test (real processor, offline)\n\n");

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("backtrace_rtest");
    tmp.createDirectory();
    juce::File snare = tmp.getChildFile("snare.wav"), vocal = tmp.getChildFile("vocal.wav");
    synthSnare(snare); synthVocal(vocal);

    BacktraceProcessor proc;
    proc.prepareToPlay(SR, 512);

    auto loadVocal = [&] { proc.importToSlot(0, vocal); proc.setActiveSource(0); proc.setTrim(0, proc.getSourceLength(0)); };
    auto loadSnare = [&] { proc.importToSlot(0, snare); proc.setActiveSource(0); proc.setTrim(0, proc.getSourceLength(0)); };

    int landing; double sr;

    // ---- 0. CRASH GUARD: host restores a Live-mode session BEFORE prepareToPlay --------
    // Cubase can call setStateInformation before prepareToPlay; the live-IR rebuild must
    // NOT touch unprepared reverb objects / convolution. (This reproduced a session-open
    // crash — the test process would die here without the dspPrepared guard.)
    std::printf("0. State-restore-before-prepare crash guard:\n");
    {
        juce::MemoryBlock liveState;
        { BacktraceProcessor src; src.setLiveMode(true); src.setLiveWet(0.7f);
          src.getStateInformation(liveState); }                 // a saved Live-mode session

        BacktraceProcessor p;                                    // fresh instance, NOT prepared yet
        p.setStateInformation(liveState.getData(), (int) liveState.getSize());  // restore FIRST
        juce::Thread::sleep(60);                                 // let the worker attempt the (guarded) build
        p.prepareToPlay(SR, 512);                                // prepare AFTER restore
        juce::Thread::sleep(60);
        juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer mi;
        bool finite = true;
        for (int k = 0; k < 8; ++k)
        {
            b.clear(); b.setSample(0, 10, 1.0f); b.setSample(1, 10, 1.0f);
            p.processBlock(b, mi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
                if (! std::isfinite(b.getSample(c, i))) finite = false;
        }
        check("survives state-restore-before-prepare (Live mode)", finite && p.getLiveMode(),
              "liveMode=" + juce::String((int) p.getLiveMode()) + " wet=" + juce::String(p.getLiveWet(), 2));
    }

    // ---- 1. RINGOUT: audio continues after the landing; no hard dead-stop -------------
    std::printf("1. Ringout / dead-stop:\n");
    {
        loadVocal();
        setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2);
        proc.setMacroRingout(0.3f);
        auto b = createSwell(proc, tmp, landing, sr);
        const int len = b.getNumSamples();
        const double tailRms = rms(b, landing, len);
        const float endPeak = peakRange(b, len - (int) (SR * 0.001), len);   // last 1 ms = boundary into silence
        check("buffer extends past landing", len > landing + (int) (SR * 0.05),
              "len=" + juce::String(len) + " landing=" + juce::String(landing));
        check("audible tail after landing",  tailRms > 0.02, "tailRMS=" + juce::String(tailRms, 4));
        check("no hard cut at end (boundary ~0)",  endPeak < 0.03f, "endPeak=" + juce::String(endPeak, 4));

        proc.setMacroRingout(0.0f);
        auto b0 = createSwell(proc, tmp, landing, sr);
        const float endPeak0 = peakRange(b0, b0.getNumSamples() - (int) (SR * 0.001), b0.getNumSamples());
        check("ringout OFF still has no hard cut", endPeak0 < 0.03f, "endPeak=" + juce::String(endPeak0, 4));
        check("landing stable regardless of ringout", std::abs(landing - (int) (SR * 4.0)) < (int) (SR * 0.25),
              "landing=" + juce::String(landing) + " (~2 bars @120 = " + juce::String((int) (SR * 4.0)) + ")");
    }

    // ---- 1b. RINGOUT IS GLOBAL: it must work IDENTICALLY for every reverb algorithm.
    // (Regression guard for the shared-tail refactor — Rust Spring / Ghost Shimmer used to hard-cut
    // at the landing because ringout was gated on the natural -40 dB tail reaching swellLen.)
    std::printf("1b. Ringout global across ALL reverbs:\n");
    {
        loadVocal();
        proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2);
        proc.setMacroRingout(0.3f);
        for (int fl = 1; fl <= 5; ++fl)
        {
            setReverb(proc, fl);
            auto b = createSwell(proc, tmp, landing, sr);
            const int    len     = b.getNumSamples();
            const double tailRms = rms(b, landing, len);
            const float  endPeak = peakRange(b, len - (int) (SR * 0.001), len);
            const auto   nm      = reverbFlavorName(fl);
            check(nm + " extends past landing", len > landing + (int) (SR * 0.10),
                  "len=" + juce::String(len) + " landing=" + juce::String(landing));
            check(nm + " audible ringout tail",  tailRms > 0.01, "tailRMS=" + juce::String(tailRms, 4));
            check(nm + " no hard-cut at clip end", endPeak < 0.03f, "endPeak=" + juce::String(endPeak, 4));
        }
    }

    // ---- 2. DETERMINISM: same settings → bit-identical render (5x) --------------------
    std::printf("2. Determinism (full render path, Digital Pedal delay swell):\n");
    {
        loadVocal();
        proc.setReverbFlavor(0);
        setDelay(proc, 2, 220.0f, 0.55f);
        proc.setRoutingMode((int) RoutingMode::DelaySwell);
        proc.setSwellBars(1);
        proc.setMacroRingout(0.2f);
        auto ref = createSwell(proc, tmp, landing, sr);
        bool identical = true; double worst = 0.0;
        for (int k = 0; k < 4; ++k)
        {
            auto b = createSwell(proc, tmp, landing, sr);
            if (b.getNumSamples() != ref.getNumSamples()) { identical = false; break; }
            for (int i = 0; i < ref.getNumSamples(); ++i)
                worst = juce::jmax(worst, (double) std::abs(b.getSample(0, i) - ref.getSample(0, i)));
        }
        identical = identical && worst < 1.0e-6;
        check("delay swell bit-identical across 5 renders", identical, "maxDiff=" + juce::String(worst, 8));
        check("delay swell is non-silent (repeats captured)", rms(ref, 0, ref.getNumSamples()) > 0.02,
              "rms=" + juce::String(rms(ref, 0, ref.getNumSamples()), 4));
    }

    // ---- 3. SWELL LEVEL + gain staging (snare tamed) ---------------------------------
    std::printf("3. Swell Level + gain staging:\n");
    {
        loadSnare();
        setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2); proc.setMacroRingout(0.2f);

        proc.setMacroLevel(0.8f);   // 0 dB
        auto b0 = createSwell(proc, tmp, landing, sr); const float p0 = peakRange(b0, 0, b0.getNumSamples());
        proc.setMacroLevel(0.46f);  // ~ -10 dB
        auto b1 = createSwell(proc, tmp, landing, sr); const float p1 = peakRange(b1, 0, b1.getNumSamples());

        check("snare at 0 dB not blasting (peak <= 0.85)", p0 <= 0.85f, "peak=" + juce::String(p0, 3));
        check("Swell Level -10 dB clearly quieter",        p1 < p0 * 0.55f,
              "p0=" + juce::String(p0, 3) + " p1=" + juce::String(p1, 3));
    }

    // ---- 4. FILTER MOTION: LPF 20k->500 darkens; HPF off->2k thins -------------------
    // Uses the SNARE (broadband) so the spectral split can actually see the sweep — a
    // low-content vocal would hide an LPF move above its own bandwidth.
    std::printf("4. Filter motion range (broadband snare):\n");
    {
        loadSnare();
        setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2); proc.setMacroRingout(0.0f);
        proc.setFilterOn(true); proc.setFilterMotion(true); proc.setFilterSlope(1);

        // LPF motion → dark vs an identical render that stays open (control). Comparing the
        // SAME landing region of two renders that differ ONLY in lpfEnd isolates the filter
        // sweep from the audio content — the move must clearly darken the landing.
        proc.setHpfStart(20.0f); proc.setHpfEnd(20.0f);
        proc.setLpfStart(20000.0f); proc.setLpfEnd(500.0f);
        auto bMot = createSwell(proc, tmp, landing, sr);
        proc.setLpfEnd(20000.0f);                                   // control: motion on, but no darkening
        auto bCtl = createSwell(proc, tmp, landing, sr);
        const int q = landing / 5;
        const double rMot = hflfRatio(bMot, landing - q, landing, 1500.0);
        const double rCtl = hflfRatio(bCtl, landing - q, landing, 1500.0);
        check("LPF 20k->500 reaches dark at the landing", rMot < rCtl * 0.5,
              "HF/LF dark=" + juce::String(rMot, 4) + " open=" + juce::String(rCtl, 4));

        // HPF off -> 2k removes lows over the rise
        proc.setLpfStart(20000.0f); proc.setLpfEnd(20000.0f);
        proc.setHpfStart(20.0f); proc.setHpfEnd(2000.0f);
        auto bh = createSwell(proc, tmp, landing, sr);
        const int q2 = landing / 5;
        const double lfStart = std::sqrt(1.0 / juce::jmax(1.0e-9, hflfRatio(bh, 0, q2, 400.0)));        // ~LF share
        const double lfEnd   = std::sqrt(1.0 / juce::jmax(1.0e-9, hflfRatio(bh, landing - q2, landing, 400.0)));
        check("HPF off->2k thins low end by the landing", lfEnd < lfStart * 0.8,
              "LFshare start=" + juce::String(lfStart, 4) + " end=" + juce::String(lfEnd, 4));
    }

    // ---- 4b. MOTION MODE: Rise Only holds at Peak Land through the tail; Rise+Fall closes
    // back down over the tail; Fall Only starts open. Measured via LPF brightness, with
    // B (= Peak Land) set OPEN/bright so the modes diverge in the post-landing tail. --------
    std::printf("4b. Motion Mode (post-landing filter return):\n");
    {
        loadVocal();
        setReverb(proc, 1); proc.setDelayFlavor(0);                 // Velvet Hall (clean HF for the measurement)
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2); proc.setMacroRingout(0.45f);          // a clear tail to fall over
        proc.setFilterOn(true); proc.setFilterMotion(true); proc.setFilterSlope(1); proc.setFilterCurve(0);
        proc.setLpfStart(20000.0f); proc.setLpfEnd(20000.0f);       // LPF off
        proc.setHpfStart(20.0f); proc.setHpfEnd(1500.0f);           // A=20 (full lows) -> B=1500 = Peak Land ("open"/thin)

        proc.setFilterMotionMode(0); auto bRise  = createSwell(proc, tmp, landing, sr); const int lenR = bRise.getNumSamples();
        proc.setFilterMotionMode(1); auto bFall  = createSwell(proc, tmp, landing, sr); const int lenF = bFall.getNumSamples();
        proc.setFilterMotionMode(2); auto bFonly = createSwell(proc, tmp, landing, sr);

        // LF share (sub-300 Hz energy / total) — HIGH = full lows, LOW = thinned by the HPF.
        auto lfShare = [&](const juce::AudioBuffer<float>& b, int a, int e)
        { return std::sqrt(1.0 / juce::jmax(1.0e-9, hflfRatio(b, a, e, 300.0))); };
        auto tailLF = [&](const juce::AudioBuffer<float>& b, int len)   // latter half of the tail
        { return lfShare(b, landing + (len - landing) / 2, len); };

        const double riseTail = tailLF(bRise, lenR);   // HPF held thin → fewer lows
        const double fallTail = tailLF(bFall, lenF);   // HPF returns to 20 → lows restored
        check("Rise+Fall restores lows in tail vs Rise Only held thin", fallTail > riseTail * 1.3,
              "fallTailLF=" + juce::String(fallTail, 3) + " riseTailLF=" + juce::String(riseTail, 3));

        const double riseStart  = lfShare(bRise,  0, landing / 4);   // starts at A=20 → full lows
        const double fonlyStart = lfShare(bFonly, 0, landing / 4);   // Fall Only starts at Peak Land → thin
        check("Fall Only starts at Peak Land (thinner start than Rise)", riseStart > fonlyStart * 1.3,
              "riseStartLF=" + juce::String(riseStart, 3) + " fonlyStartLF=" + juce::String(fonlyStart, 3));

        check("all 3 motion modes render distinct buffers",
              maxAbsDiff(bRise, bFall) > 1e-4 && maxAbsDiff(bRise, bFonly) > 1e-4 && maxAbsDiff(bFall, bFonly) > 1e-4,
              "RvF=" + juce::String(maxAbsDiff(bRise, bFall), 5) + " RvFo=" + juce::String(maxAbsDiff(bRise, bFonly), 5));
    }

    // ---- 5. NATURAL SWELL SHAPE: rising over the rise region, Pull contrast ----------
    std::printf("5. Natural swell shape (rise, not drone):\n");
    {
        loadVocal();
        setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2); proc.setMacroRingout(0.2f);
        proc.setFilterOn(false); proc.setMacroLevel(0.8f);

        proc.setMacroSwell(0.5f);                 // Pull neutral (natural)
        auto b = createSwell(proc, tmp, landing, sr);
        auto qd = [&](int k){ return rms(b, k * landing / 4, (k + 1) * landing / 4); };
        const double q1 = qd(0), q2 = qd(1), q3 = qd(2), q4 = qd(3);
        check("rise is monotonic Q1<Q2<Q3<Q4", q1 < q2 && q2 < q3 && q3 < q4,
              "Q1=" + juce::String(q1, 4) + " Q2=" + juce::String(q2, 4) + " Q3=" + juce::String(q3, 4) + " Q4=" + juce::String(q4, 4));
        check("strong contrast (Q4/Q1 >= 4x, not a drone)", q1 > 1e-6 && q4 / q1 >= 4.0,
              "Q4/Q1=" + juce::String(q1 > 1e-6 ? q4 / q1 : 0.0, 2));

        proc.setMacroSwell(1.0f); auto bd = createSwell(proc, tmp, landing, sr);   // dramatic
        proc.setMacroSwell(0.0f); auto be = createSwell(proc, tmp, landing, sr);   // even
        const double earlyDram = rms(bd, 0, landing / 3), earlyEven = rms(be, 0, landing / 3);
        check("Pull up = more dramatic (quieter early third)", earlyDram < earlyEven * 0.9,
              "earlyDramatic=" + juce::String(earlyDram, 4) + " earlyEven=" + juce::String(earlyEven, 4));
    }

    // ---- 6. Render performance (wall-clock per Create Swell) --------------------------
    std::printf("6. Render performance:\n");
    {
        loadVocal();
        setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        proc.setSwellBars(2); proc.setMacroRingout(0.2f); proc.setMacroSwell(0.5f);
        proc.regenerateSwell();                                   // warm up
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const int N = 5;
        for (int k = 0; k < N; ++k) proc.regenerateSwell();
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / N;
        check("Create Swell render < 80 ms (2-bar)", ms < 80.0, "avg=" + juce::String(ms, 1) + " ms/render");
    }

    // ---- 7. STRESS: 8 imports + 20 renders, timing stays flat, buffers stay valid ----
    std::printf("7. Stress (8 imports, 20 renders, multi-length):\n");
    {
        for (int i = 0; i < 8; ++i)
        {
            juce::File f = tmp.getChildFile("src" + juce::String(i) + ".wav");
            const int n = (int) (SR * (0.1 + 0.05 * i));
            juce::AudioBuffer<float> b(2, n); juce::Random rng(100 + i);
            for (int k = 0; k < n; ++k) { const float e = std::exp(-(float) k / (float) (SR * 0.05));
                const float s = (rng.nextFloat() * 2.0f - 1.0f) * e * 0.7f; b.setSample(0, k, s); b.setSample(1, k, s); }
            writeWav(f, b, SR);
            proc.importToSlot(i, f);
        }
        setReverb(proc, 1); proc.setDelayFlavor(0); proc.setRoutingMode((int) RoutingMode::ReverbSwell);
        const int barv[] = { 1, 2, 4, 8, 2 };
        double firstAvg = 0, lastAvg = 0, maxMs = 0; bool allValid = true;
        for (int it = 0; it < 20; ++it)
        {
            proc.setActiveSource(it % 8);
            proc.setTrim(0, proc.getSourceLength(it % 8));
            proc.setSwellBars(barv[it % 5]);
            proc.setMacroSwell((float) (it % 10) / 10.0f);
            proc.setMacroRingout((float) ((it + 3) % 6) / 6.0f);
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            proc.regenerateSwell();
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            maxMs = juce::jmax(maxMs, ms);
            if (it < 5)  firstAvg += ms / 5.0;
            if (it >= 15) lastAvg += ms / 5.0;
            if (! proc.hasSwell() || proc.getSwellRenderLen() <= 0) allValid = false;
        }
        check("all 20 renders valid + non-empty", allValid, "");
        check("no timing growth (last5 <= 1.6x first5)", lastAvg <= firstAvg * 1.6 + 5.0,
              "first5=" + juce::String(firstAvg, 1) + "ms last5=" + juce::String(lastAvg, 1) + "ms max=" + juce::String(maxMs, 0) + "ms");
    }

    // ---- 8. Render time by length (worker-thread time; async so it never blocks the UI) -
    std::printf("8. Render time by length (off-thread, non-blocking):\n");
    {
        loadVocal(); setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell); proc.setMacroSwell(0.5f); proc.setMacroRingout(0.2f);
        for (int bv : { 2, 4, 8 })
        {
            proc.setSwellBars(bv); proc.regenerateSwell();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < 3; ++k) proc.regenerateSwell();
            const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 3.0;
            check(juce::String(bv) + "-bar render < 500 ms (worker)", ms < 500.0, juce::String(ms, 1) + " ms");
        }
    }

    // ---- 9. Background render thread: completes off-thread, survives rapid requests ---
    std::printf("9. Background render thread:\n");
    {
        loadVocal(); setReverb(proc, 1); proc.setDelayFlavor(0);
        proc.setRoutingMode((int) RoutingMode::ReverbSwell); proc.setSwellBars(2); proc.setMacroSwell(0.5f);
        proc.requestRender(true);
        int waited = 0; bool done = false;
        while (waited < 6000) { if (proc.consumeRenderDone()) { done = true; break; } juce::Thread::sleep(10); waited += 10; }
        check("requestRender completes off-thread", done, "waited=" + juce::String(waited) + "ms");
        check("async render produced a valid swell", proc.hasSwell() && proc.getSwellRenderLen() > 0,
              "len=" + juce::String(proc.getSwellRenderLen()));
        check("idle after done", ! proc.isRendering(), "");

        // Exercise the in-flight window: fire a render and IMMEDIATELY attempt Print + Play.
        // The processor guards must make these safe no-ops (never touch swellBuffer/machines
        // mid-render). Reaching the next check at all proves no crash/use-after-free.
        proc.setSwellBars(4); proc.requestRender(false);
        const bool hitWindow = proc.isRendering();
        proc.vaultRenderForDrag();        // Print/Drag path during render → guarded, returns {}
        proc.startReverseAudition(false); // Play Swell path during render → guarded no-op
        waited = 0;
        while (proc.isRendering() && waited < 10000) { proc.consumeRenderDone(); juce::Thread::sleep(10); waited += 10; }
        check("Print/Play during in-flight render are safe", true,
              hitWindow ? "hit the render window" : "(render finished before calls)");

        for (int k = 0; k < 8; ++k) { proc.setSwellBars(k % 2 ? 2 : 1); proc.requestRender(false); juce::Thread::sleep(3); }
        waited = 0;
        while (proc.isRendering() && waited < 10000) { proc.consumeRenderDone(); juce::Thread::sleep(10); waited += 10; }
        check("rapid coalesced requests settle (no hang/crash)", ! proc.isRendering() && proc.hasSwell(),
              "waited=" + juce::String(waited) + "ms len=" + juce::String(proc.getSwellRenderLen()));
    }

    // ---- 10. LIVE PREVERB (Mode 2): real-time reverse-reverb that LEADS INTO the sound --
    std::printf("10. Live Preverb (real-time reverse reverb):\n");
    {
        setReverb(proc, 3);                            // Ghost Shimmer (default reverb)
        proc.setDelayFlavor(0);
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); // 1/4 Straight pre-swell
        proc.setLiveWet(1.0f); proc.setLiveDry(0.0f); proc.setMacroMix(1.0f);  // full wet → measure the swell
        proc.setLiveMode(true);
        proc.prepareToPlay(SR, 512);
        proc.rebuildLiveIR();                          // synchronous kernel build on this thread

        // The convolution loads the kernel on a background thread and swaps it into the
        // active path on a subsequent process() call. Poll until loaded, then run priming
        // blocks so the swap is committed before we measure (deterministic in a DAW).
        juce::AudioBuffer<float> warm(2, 512);
        int waited = 0;
        while (! proc.liveConvLoaded() && waited < 4000) { juce::Thread::sleep(20); waited += 20; }
        juce::Thread::sleep(40);
        for (int k = 0; k < 96; ++k)
        { warm.clear(); juce::AudioBuffer<float> ws(warm.getArrayOfWritePointers(), 2, 512); proc.liveProcessBlock(ws); }

        proc.pushLiveLatencyIfChanged();               // mirror the editor timer: publish the loaded-IR latency
        const int lat = proc.getLatencySamples();
        const int total = juce::jmax(4, lat) * 5 / 2;
        juce::AudioBuffer<float> out(2, total); out.clear();
        const int IMP = (int) (SR * 0.02);             // impulse 20 ms into the live stream
        juce::AudioBuffer<float> blk(2, 512);
        int pos = 0; bool finite = true; float peak = 0.0f; int peakIdx = 0;
        while (pos < total)
        {
            const int n = juce::jmin(512, total - pos);
            blk.clear();
            for (int i = 0; i < n; ++i)
                if (pos + i == IMP) { blk.setSample(0, i, 1.0f); blk.setSample(1, i, 1.0f); }
            juce::AudioBuffer<float> sub(blk.getArrayOfWritePointers(), 2, n);
            proc.liveProcessBlock(sub);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
            {
                const float v = sub.getSample(c, i);
                if (! std::isfinite(v)) finite = false;
                const float a = std::abs(v);
                if (a > peak && c == 0) { peak = a; peakIdx = pos + i; }
                out.setSample(c, pos + i, v);
            }
            pos += n;
        }

        check("live preverb kernel loaded", proc.liveConvLoaded(),
              "latency=" + juce::String(lat) + " (" + juce::String(1000.0 * lat / SR, 0) + " ms) " + proc.liveTimeLabel());
        check("live output finite + bounded (no runaway)", finite && peak <= 1.01f && peak > 0.0005f,
              "peak=" + juce::String(peak, 4) + " @" + juce::String(peakIdx));

        // The swell peak appears AFTER the impulse (→ after host PDC it sits in front of the
        // original sound), and the energy RISES into that peak (a lead-in, not a dead burst).
        const int w = juce::jlimit(1, juce::jmax(1, peakIdx / 3), lat / 4);
        const double early = rms(out, peakIdx - 3 * w, peakIdx - 2 * w);   // far build-up
        const double late  = rms(out, peakIdx - w,     peakIdx);           // just before the peak
        check("preverb swell follows the trigger (leads source after PDC)", peakIdx > IMP + lat / 4,
              "peak@" + juce::String(peakIdx) + " IMP=" + juce::String(IMP));
        check("preverb RISES into its peak (leads in, not a burst)", late > early * 1.5 && late > 0.0002,
              "early=" + juce::String(early, 5) + " late=" + juce::String(late, 5));

        // GAIN STAGING — the headline Phase-1.5 complaint was "way too loud". On a sustained
        // tonal source (the worst case for a resonant reverb kernel): full wet must stay
        // controlled (not slam the ceiling), and the DEFAULT 25% Mix must not push the output
        // past the dry source.
        auto sustainedPeak = [&](float mix) -> float
        {
            proc.setMacroMix(mix); proc.setLiveWet(1.0f); proc.setLiveDry(1.0f);
            juce::AudioBuffer<float> tb(2, 512);
            const float amp = 0.5f, inc = juce::MathConstants<float>::twoPi * 220.0f / (float) SR;
            float phase = 0.0f, pk = 0.0f;
            for (int k = 0; k < 240; ++k)                  // ~2.5 s → steady state past the latency
            {
                for (int i = 0; i < 512; ++i) { const float s = amp * std::sin(phase); phase += inc;
                    tb.setSample(0, i, s); tb.setSample(1, i, s); }
                juce::AudioBuffer<float> ts(tb.getArrayOfWritePointers(), 2, 512);
                proc.liveProcessBlock(ts);
                if (k > 200) for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
                    pk = juce::jmax(pk, std::abs(ts.getSample(c, i)));
            }
            return pk;
        };
        const float fullWet = sustainedPeak(1.0f);
        const float defMix  = sustainedPeak(0.25f);
        check("NO BLAST: full-wet sustained stays controlled (<= 0.85)", fullWet <= 0.85f,
              "fullWetPeak=" + juce::String(fullWet, 3) + " (in 0.5)");
        check("default Mix 25% not louder than the dry source", defMix <= 0.6f,
              "defMixPeak=" + juce::String(defMix, 3) + " (dry 0.5)");

        // Build + settle the live kernel for a given TIME / octave, feed an impulse, capture.
        auto liveImpulse = [&](int timeIdx, float pitch) -> juce::AudioBuffer<float>
        {
            proc.setLiveTimeIndex(timeIdx); proc.setLiveFeel(0); proc.setPitchSemitones(pitch);
            proc.setMacroMix(1.0f); proc.setLiveWet(1.0f); proc.setLiveDry(0.0f);
            proc.rebuildLiveIR();
            juce::AudioBuffer<float> warm(2, 512); int w = 0;
            while (! proc.liveConvLoaded() && w < 4000) { juce::Thread::sleep(20); w += 20; }
            juce::Thread::sleep(80);
            for (int k = 0; k < 90; ++k)
            { warm.clear(); juce::AudioBuffer<float> ws(warm.getArrayOfWritePointers(), 2, 512); proc.liveProcessBlock(ws); }
            proc.pushLiveLatencyIfChanged();
            const int lat = proc.getLatencySamples();
            const int total = juce::jmax(1024, lat * 2);
            juce::AudioBuffer<float> out(2, total); out.clear();
            int pos = 0; juce::AudioBuffer<float> blk(2, 512);
            while (pos < total)
            {
                const int n = juce::jmin(512, total - pos); blk.clear();
                for (int i = 0; i < n; ++i) if (pos + i == 240) { blk.setSample(0, i, 1.0f); blk.setSample(1, i, 1.0f); }
                juce::AudioBuffer<float> sub(blk.getArrayOfWritePointers(), 2, n); proc.liveProcessBlock(sub);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) out.setSample(c, pos + i, sub.getSample(c, i));
                pos += n;
            }
            return out;
        };

        // 1/16 MICRO must still produce an audible preverb (not go dry/silent) — the
        // Phase-1.6B complaint. Continuous noise → measure STEADY-STATE output, so the result
        // is independent of the async kernel-swap timing.
        { setReverb(proc, 3); proc.setLiveTimeIndex(1); proc.setLiveFeel(0); proc.setPitchSemitones(0.0f);
          proc.setMacroMix(1.0f); proc.setLiveWet(1.0f); proc.setLiveDry(0.0f);
          proc.rebuildLiveIR();
          int w = 0; while (! proc.liveConvLoaded() && w < 4000) { juce::Thread::sleep(20); w += 20; }
          juce::Thread::sleep(120);
          juce::Random rng(7); juce::AudioBuffer<float> nb(2, 512); double sum = 0.0; int cnt = 0;
          for (int k = 0; k < 220; ++k)
          {
              for (int i = 0; i < 512; ++i) { const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.4f; nb.setSample(0, i, s); nb.setSample(1, i, s); }
              juce::AudioBuffer<float> ns(nb.getArrayOfWritePointers(), 2, 512);
              proc.liveProcessBlock(ns);
              if (k > 140) for (int i = 0; i < 512; ++i) { const float v = ns.getSample(0, i); sum += (double) v * v; ++cnt; }
          }
          const double outRms = std::sqrt(sum / juce::jmax(1, cnt));
          check("1/16 micro produces an audible preverb (not silent/dry)", outRms > 0.01,
                "outRms=" + juce::String(outRms, 4) + " " + proc.liveTimeLabel()); }

        // OCTAVE must audibly change the live kernel (was dead in live mode).
        { setReverb(proc, 3);
          auto o0 = liveImpulse(3, 0.0f);                // 1/4, no octave
          auto oU = liveImpulse(3, 12.0f);               // 1/4, +1 octave
          check("octave audibly changes the live kernel", maxAbsDiff(o0, oU) > 1.0e-3,
                "maxDiff=" + juce::String(maxAbsDiff(o0, oU), 5)); }
        proc.setPitchSemitones(0.0f); proc.setMacroMix(0.25f);

        proc.setLiveMode(false);                       // restore Capture mode default
    }

    std::printf("11. Live Preverb OFFLINE preview (audible in the standalone):\n");
    {
        loadSnare();                                   // hot transient into slot 0, trim = full
        setReverb(proc, 3); proc.setDelayFlavor(0);
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); // 1/4 Straight pre-swell
        proc.setPitchSemitones(0.0f);
        proc.setLiveWet(1.0f); proc.setLiveDry(1.0f); proc.setMacroMix(0.5f);
        proc.setLiveMode(true);
        proc.prepareToPlay(SR, 512);

        proc.startLivePreview();                       // synchronous offline render → playBuffer
        const int plen = proc.getPlayLen();
        const int M    = proc.preverbLengthSamples();
        check("preview ran (auditionWhat=4, playLen>0)", proc.getAuditionWhat() == 4 && plen > 0,
              "playLen=" + juce::String(plen) + " M=" + juce::String(M));

        // Drain the preview through the playback path (processBlock) and capture it — this is
        // exactly what the standalone plays when you hit Preview Live.
        juce::AudioBuffer<float> cap(2, juce::jmax(1, plen)); cap.clear();
        juce::AudioBuffer<float> blk(2, 512);
        int pos = 0; bool finite = true; float peak = 0.0f;
        while (pos < plen && proc.getAuditionWhat() == 4)
        {
            const int n = juce::jmin(512, plen - pos);
            blk.clear();
            juce::MidiBuffer mi;
            juce::AudioBuffer<float> sub(blk.getArrayOfWritePointers(), 2, n);
            proc.processBlock(sub, mi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
            {
                const float v = sub.getSample(c, i);
                if (! std::isfinite(v)) finite = false;
                cap.setSample(c, pos + i, v);
                peak = juce::jmax(peak, std::abs(v));
            }
            pos += n;
        }
        check("preview output finite + bounded + audible", finite && peak <= 1.0f && peak > 0.02f,
              "peak=" + juce::String(peak, 4));

        // The preverb must LEAD IN: real swell energy exists BEFORE the dry landing (~sample M),
        // and the output rises into the landing (a swell, not a flat wash).
        auto rmsRange = [&](int a, int b) { double s = 0.0; int c2 = 0;
            for (int c = 0; c < 2; ++c) for (int i = juce::jmax(0, a); i < b && i < plen; ++i)
            { const float v = cap.getSample(c, i); s += (double) v * v; ++c2; }
            return std::sqrt(s / juce::jmax(1, c2)); };
        const double preRms  = rmsRange(M / 4, M * 3 / 4);                               // swell-only pre-roll
        const double landRms = rmsRange(M, juce::jmin(plen, M + (int) (SR * 0.03)));      // at the dry landing
        check("preview preverb leads in (audible swell before the source)", preRms > 0.002,
              "preRms=" + juce::String(preRms, 4));
        check("preview rises into the landing (swell, not a wash)", landRms > preRms,
              "landRms=" + juce::String(landRms, 4) + " preRms=" + juce::String(preRms, 4));

        proc.setMacroMix(0.25f); proc.setLiveMode(false);
    }

    std::printf("12. Live Preverb SHAPE control (swell build-up curve):\n");
    {
        setReverb(proc, 3); proc.setDelayFlavor(0);
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); proc.setPitchSemitones(0.0f);
        proc.prepareToPlay(SR, 512);
        const int M = proc.preverbLengthSamples();

        // late/early energy ratio of the kernel — a bigger ratio = a more back-loaded (dramatic) bloom.
        auto lateEarlyRatio = [&](float shape)
        {
            proc.setLiveShape(shape);
            juce::AudioBuffer<float> k(2, M); proc.buildLiveKernel(k);
            double eEarly = 0.0, eLate = 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const float* d = k.getReadPointer(c);
                for (int i = 0;     i < M / 2; ++i) eEarly += (double) d[i] * d[i];
                for (int i = M / 2; i < M;     ++i) eLate  += (double) d[i] * d[i];
            }
            return eLate / juce::jmax(1.0e-9, eEarly);
        };
        const double rGentle = lateEarlyRatio(0.15f);
        const double rBloom  = lateEarlyRatio(0.85f);
        check("SHAPE steepens the build-up (higher = more back-loaded bloom)", rBloom > rGentle * 1.25,
              "gentle late/early=" + juce::String(rGentle, 2) + "  bloom=" + juce::String(rBloom, 2));

        // Default (0.5) kernel stays finite and still rises into the landing (late half louder).
        proc.setLiveShape(0.5f);
        juce::AudioBuffer<float> k(2, M); proc.buildLiveKernel(k);
        bool finite = true; double eA = 0.0, eB = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const float* d = k.getReadPointer(c);
            for (int i = 0; i < M; ++i) if (! std::isfinite(d[i])) finite = false;
            for (int i = 0;     i < M / 2; ++i) eA += (double) d[i] * d[i];
            for (int i = M / 2; i < M;     ++i) eB += (double) d[i] * d[i];
        }
        check("SHAPE default kernel finite + rises (late half louder)", finite && eB > eA,
              "earlyE=" + juce::String(eA, 4) + " lateE=" + juce::String(eB, 4));
        proc.setLiveShape(0.5f);
    }

    std::printf("13. Live kernel loudness across reverb flavors (peak/presence):\n");
    {
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); proc.setLiveShape(0.5f); proc.setPitchSemitones(0.0f);
        proc.prepareToPlay(SR, 512);
        const int M = proc.preverbLengthSamples();
        auto kpeak = [&](int fl)
        {
            proc.setReverbFlavor(fl);
            juce::AudioBuffer<float> k(2, M); proc.buildLiveKernel(k);
            return juce::jmax(k.getMagnitude(0, 0, M), k.getMagnitude(1, 0, M));
        };
        // crest (peak/rms) → how concentrated vs spread; hf (>2kHz share) → brightness/presence.
        auto stats = [&](int fl, float& crest, float& hf)
        {
            proc.setReverbFlavor(fl);
            juce::AudioBuffer<float> k(2, M); proc.buildLiveKernel(k);
            const float* d = k.getReadPointer(0);
            double eTot = 0.0, eHf = 0.0; float hp = 0.0f;
            const float a = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2000.0f / (float) SR);
            float pk = 0.0f;
            for (int i = 0; i < M; ++i) { const float x = d[i]; hp += a * (x - hp); const float h = x - hp;
                eTot += (double) x * x; eHf += (double) h * h; pk = juce::jmax(pk, std::abs(x)); }
            const float rms = (float) std::sqrt(eTot / juce::jmax(1, M));
            crest = pk / juce::jmax(1.0e-6f, rms);
            hf = (float) (eHf / juce::jmax(1.0e-9, eTot));
        };
        const float pHall  = kpeak(1);
        const float pModern= kpeak(2);
        const float pShim  = kpeak(3);
        const float pPlate = kpeak(4);
        const float pSpring= kpeak(5);
        float cHall, hHall, cShim, hShim;
        stats(1, cHall, hHall); stats(3, cShim, hShim);
        std::printf("     kernel peak: Hall=%.4f Modern=%.4f Shimmer=%.4f Plate=%.4f Spring=%.4f\n",
                    pHall, pModern, pShim, pPlate, pSpring);
        std::printf("     Hall   crest=%.1f  hf(>2k)=%.3f\n", cHall, hHall);
        std::printf("     Shimmer crest=%.1f  hf(>2k)=%.3f\n", cShim, hShim);
        const float hi = juce::jmax(juce::jmax(pHall, pModern), juce::jmax(juce::jmax(pShim, pPlate), pSpring));
        const float lo = juce::jmin(juce::jmin(pHall, pModern), juce::jmin(juce::jmin(pShim, pPlate), pSpring));
        check("live kernel peak consistent across flavors (<1.8x spread)", hi / juce::jmax(1.0e-6f, lo) < 1.8f,
              "hi/lo=" + juce::String(hi / juce::jmax(1.0e-6f, lo), 2));
        proc.setReverbFlavor(1);
    }

    std::printf("14. Live Preverb presets (additive bank, live params restore):\n");
    {
        int liveCount = 0; bool loadedGuitar = false, allEnableLive = true;
        for (auto& f : btpreset::all())
        {
            if (f.category != juce::String("Live Preverb")) continue;
            ++liveCount;
            proc.setStateVar(f.state);
            if (! proc.getLiveMode()) allEnableLive = false;   // every Live preset must enable Mode 2
            if (f.name == juce::String("Guitar Swell"))
            {
                loadedGuitar = true;
                check("'Guitar Swell' enables live mode",   proc.getLiveMode(), "");
                check("'Guitar Swell' Time = 1/4 (idx 3)",  proc.getLiveTimeIndex() == 3,
                      "idx=" + juce::String(proc.getLiveTimeIndex()));
                check("'Guitar Swell' Shape ~ 0.50",        std::abs(proc.getLiveShape() - 0.50f) < 0.02f,
                      "shape=" + juce::String(proc.getLiveShape(), 2));
                check("'Guitar Swell' Mix ~ 0.40",          std::abs(proc.getMacroMix() - 0.40f) < 0.02f,
                      "mix=" + juce::String(proc.getMacroMix(), 2));
            }
        }
        check("Live Preverb preset bank present (>= 8)", liveCount >= 8, "count=" + juce::String(liveCount));
        check("every Live preset enables live mode",     allEnableLive, "");
        check("'Guitar Swell' preset found",             loadedGuitar, "");

        // INIT / offline presets must NOT enable live mode (additive only, no regression)
        proc.setStateVar(btpreset::initState());
        check("INIT preset stays in Capture mode (live off)", ! proc.getLiveMode(), "");
        proc.setLiveMode(false);
    }

    std::printf("15. Delay layer (Blend/Swell) + copyright-safe names:\n");
    {
        check("legacy '626_spring' id migrates to Rust Spring", reverbFlavorFromName("626_spring") == 5, "");
        check("Rust Spring id is clean ('rust_spring')", reverbFlavorName(5) == "rust_spring", "");

        loadSnare();
        setReverb(proc, 1);
        setDelay(proc, 1, 150.0f, 0.85f);   // dense, long-feedback repeats → content spans the window
        proc.setDelaySync(false);

        // Baseline: Delay Blend 0 (default) = the classic reverb-only swell.
        proc.setDelayBlend(0.0f); proc.setReverbBlend(1.0f); proc.setDelaySwell(0.5f);
        auto base = createSwell(proc, tmp, landing, sr);

        // Delay Blend up → an audibly different swell (the layer is real).
        proc.setDelayBlend(1.0f);
        auto withDelay = createSwell(proc, tmp, landing, sr);
        check("Delay Blend adds an audible delay layer", maxAbsDiff(base, withDelay) > 1.0e-3,
              "maxDiff=" + juce::String(maxAbsDiff(base, withDelay), 5));

        // Delay Swell controls WHEN the layer blooms: with the reverb muted, a LOW setting keeps
        // audible delay in the first half of the rise; a HIGH setting suppresses it there (the
        // layer only arrives late). Compare the first-half energy share.
        proc.setReverbBlend(0.0f);
        auto firstHalfShare = [&](juce::AudioBuffer<float>& b, int land)
        {
            const int n = juce::jmin(land, b.getNumSamples());
            double eAll = 0.0, eFirst = 0.0; const auto* d = b.getReadPointer(0);
            for (int i = 0; i < n; ++i) { const double v = (double) d[i] * d[i]; eAll += v; if (i < n / 2) eFirst += v; }
            return eFirst / juce::jmax(1.0e-12, eAll);
        };
        proc.setDelaySwell(0.05f); int landE = 0; auto early = createSwell(proc, tmp, landE, sr);
        proc.setDelaySwell(0.95f); int landL = 0; auto late  = createSwell(proc, tmp, landL, sr);
        const double sE = firstHalfShare(early, landE), sL = firstHalfShare(late, landL);
        check("Delay Swell high = later bloom (early energy suppressed)", sE > sL * 1.3,
              "firstHalfShare early=" + juce::String(sE, 4) + " late=" + juce::String(sL, 4));

        // Live kernel: the delay layer reaches the preverb kernel too; default stays reverb-only.
        proc.setReverbBlend(1.0f); proc.setLiveTimeIndex(3); proc.setLiveFeel(0);
        proc.prepareToPlay(SR, 512);
        const int M = proc.preverbLengthSamples();
        juce::AudioBuffer<float> k0(2, M), k1(2, M);
        proc.setDelayBlend(0.0f); proc.buildLiveKernel(k0);
        proc.setDelayBlend(1.0f); proc.buildLiveKernel(k1);
        check("live kernel gains the delay layer with Blend up", maxAbsDiff(k0, k1) > 1.0e-4,
              "maxDiff=" + juce::String(maxAbsDiff(k0, k1), 6));
        bool finite = true;
        for (int c = 0; c < 2; ++c) { const float* d = k1.getReadPointer(c);
            for (int i = 0; i < M; ++i) if (! std::isfinite(d[i])) finite = false; }
        check("delay-layer kernel finite", finite, "");

        proc.setDelayBlend(0.0f); proc.setReverbBlend(1.0f); proc.setDelaySwell(0.5f);
        proc.setDelayFlavor(0);
    }

    std::printf("16. Pad-don't-stretch (no 'flying saucer') + delay trail past landing:\n");
    {
        // A short vocal into a LONG swell with NO FX must be the reversed word placed at the
        // END of the window with silence before it — never a phase-vocoder-stretched wash.
        loadVocal();                                  // 0.4 s word
        proc.setDelayFlavor(0); proc.setReverbFlavor(0);
        proc.setKeepPitch(true);
        proc.setSwellLenBars(2.0f);                   // @120 BPM = 4 s >> the word
        int landP = 0; auto padded = createSwell(proc, tmp, landP, sr);
        const int n = juce::jmin(landP, padded.getNumSamples());
        const double headRms = rms(padded, 0, n / 2);                       // pad region ≈ silence
        const double endRms  = rms(padded, n - (int) (SR * 0.35), n);       // reversed word at the landing
        check("no FX + long window: silence pad, REAL word at landing", endRms > 0.02 && headRms < endRms * 0.02,
              "headRms=" + juce::String(headRms, 5) + " endRms=" + juce::String(endRms, 4));
        check("padded print fills the full musical window", landP >= (int) (SR * 3.5),
              "landing=" + juce::String(landP) + " (~" + juce::String(landP / SR, 2) + " s)");

        // Delay trail: with Delay Blend up and Ringout on, echoes must CONTINUE past the
        // landing (real feedback trail) instead of hard-cutting at the swell end.
        loadSnare();
        setReverb(proc, 1);
        setDelay(proc, 1, 150.0f, 0.85f);
        proc.setDelaySync(false);
        proc.setMacroRingout(1.0f);
        proc.setSwellLenBars(1.0f);
        // Direct proof, unconfounded by per-render normalisation: with the reverb layer muted
        // (pure delay layer), the print must still RING past the landing — echoes trailing,
        // not a hard cut at the swell end.
        proc.setDelayBlend(1.0f); proc.setReverbBlend(0.0f);
        int landB = 0; auto trail = createSwell(proc, tmp, landB, sr);
        const double postRms = rms(trail, landB + (int) (SR * 0.05), landB + (int) (SR * 1.0));
        check("delay repeats trail LONG past the landing (no hard cut)",
              trail.getNumSamples() > landB + (int) (SR * 3.2) && postRms > 0.01,
              "trail=" + juce::String((trail.getNumSamples() - landB) / SR, 2) + " s"
              + " postRms=" + juce::String(postRms, 4));
        proc.setReverbBlend(1.0f);

        proc.setDelayBlend(0.0f); proc.setMacroRingout(0.35f); proc.setDelayFlavor(0);
    }

    std::printf("17. Live kernel: EVERY delay flavor audible in a short window + SHAPE extremes:\n");
    {
        // The bug this guards: default delay Times (300-500 ms) fit ZERO repeats in a 1/4-note
        // live window (0.5 s) → Digital Pedal / Cold Rack / Vault Delay produced literal silence.
        setReverb(proc, 1);
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); proc.setLiveShape(0.5f); proc.setPitchSemitones(0.0f);
        proc.setDelaySync(false); proc.setReverbBlend(1.0f);
        proc.prepareToPlay(SR, 512);
        const int M = proc.preverbLengthSamples();
        juce::AudioBuffer<float> kOff(2, M);
        proc.setDelayBlend(0.0f); proc.setDelayFlavor(1); proc.buildLiveKernel(kOff);
        static const char* dn[] = { "", "Reel Echo", "Digital Pedal", "Magnetic Drum",
                                    "Tape Witness", "Cold Rack", "Vault Delay" };
        for (int fl = 1; fl <= 6; ++fl)
        {
            proc.setDelayFlavor(fl);
            const auto dl = delayKnobLayout(fl);
            for (int i = 0; i < dl.size() && i < 8; ++i) if (dl[i].used()) proc.setDelayParam(i, dl[i].def);
            proc.setDelayBlend(1.0f);
            juce::AudioBuffer<float> k(2, M);
            proc.buildLiveKernel(k);
            const double diff = maxAbsDiff(kOff, k);
            check(juce::String(dn[fl]) + " audible in a 1/4 live window", diff > 2.0e-3,
                  "kernelDiff=" + juce::String(diff, 5));
        }
        proc.setDelayBlend(0.0f); proc.setDelayFlavor(0);

        // SHAPE extremes: Bloom(1.0) now gates the onset (even more back-loaded than 0.85);
        // Gentle(0.0) floors the early region (audibly present from the start).
        auto lateEarly = [&](float shape)
        {
            proc.setLiveShape(shape);
            juce::AudioBuffer<float> k(2, M); proc.buildLiveKernel(k);
            double eE = 0.0, eL = 0.0; const float* d = k.getReadPointer(0);
            for (int i = 0; i < M / 2; ++i) eE += (double) d[i] * d[i];
            for (int i = M / 2; i < M; ++i) eL += (double) d[i] * d[i];
            return eL / juce::jmax(1.0e-12, eE);
        };
        const double rG0 = lateEarly(0.0f), rG = lateEarly(0.15f), rB = lateEarly(0.85f), rB1 = lateEarly(1.0f);
        check("SHAPE 1.0 gate = even later than 0.85", rB1 > rB * 1.2,
              "0.85=" + juce::String(rB, 1) + " 1.0=" + juce::String(rB1, 1));
        check("SHAPE 0.0 floor = even earlier than 0.15", rG0 < rG * 0.85,
              "0.15=" + juce::String(rG, 2) + " 0.0=" + juce::String(rG0, 2));
        proc.setLiveShape(0.5f);
    }

    std::printf("18. Global output filter (real-time + baked print, hear = print):\n");
    {
        // Baked print: LPF 800 Hz must darken the exported WAV vs fully open.
        loadSnare();
        setReverb(proc, 1); proc.setDelayFlavor(0); proc.setSwellLenBars(1.0f);
        proc.setOutHpf(20.0f); proc.setOutLpf(20000.0f);
        auto open = createSwell(proc, tmp, landing, sr);
        proc.setOutLpf(800.0f);
        auto dark = createSwell(proc, tmp, landing, sr);
        const double brOpen = hflfRatio(open, 0, landing, 2000.0), brDark = hflfRatio(dark, 0, landing, 2000.0);
        check("LPF 800 bakes into the print (darker export)", brDark < brOpen * 0.4,
              "hf/lf open=" + juce::String(brOpen, 3) + " lpf800=" + juce::String(brDark, 4));
        proc.setOutLpf(20000.0f); proc.setOutHpf(400.0f);
        auto thin = createSwell(proc, tmp, landing, sr);
        const double brThin = hflfRatio(thin, 0, landing, 300.0);
        const double brFull = hflfRatio(open, 0, landing, 300.0);
        check("HPF 400 bakes into the print (lows removed)", brThin > brFull * 1.5,
              "hf/lf open=" + juce::String(brFull, 3) + " hpf400=" + juce::String(brThin, 3));
        proc.setOutHpf(20.0f);

        // Real-time path: noise through processBlock with LPF 500 must lose HF energy.
        proc.setLiveMode(false);
        proc.prepareToPlay(SR, 512);
        auto rtHfShare = [&]() {
            juce::Random rng(11); juce::AudioBuffer<float> blk(2, 512); juce::MidiBuffer mi;
            juce::AudioBuffer<float> capRT(1, 512 * 40);
            int pos = 0;
            for (int k = 0; k < 40; ++k)
            {
                for (int i = 0; i < 512; ++i) { const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.3f;
                    blk.setSample(0, i, s); blk.setSample(1, i, s); }
                proc.processBlock(blk, mi);
                capRT.copyFrom(0, pos, blk, 0, 0, 512); pos += 512;
            }
            return hflfRatio(capRT, 512 * 10, pos, 2000.0);   // skip smoothing warm-up
        };
        proc.setOutLpf(20000.0f); const double rtOpen = rtHfShare();
        proc.setOutLpf(500.0f);   const double rtDark = rtHfShare();
        check("real-time output filter darkens the stream", rtDark < rtOpen * 0.25,
              "hf/lf open=" + juce::String(rtOpen, 3) + " lpf500=" + juce::String(rtDark, 4));
        proc.setOutLpf(20000.0f);
    }

    std::printf("19. Live REAL pitch (wet-feed transpose) + seamless kernel swap:\n");
    {
        setReverb(proc, 1); proc.setDelayFlavor(0); proc.setDelayBlend(0.0f);
        proc.setLiveTimeIndex(3); proc.setLiveFeel(0); proc.setLiveShape(0.5f);
        proc.setMacroMix(1.0f); proc.setLiveWet(1.0f); proc.setLiveDry(0.0f);   // wet only
        proc.setLiveMode(true);
        proc.prepareToPlay(SR, 512);
        proc.rebuildLiveIR();
        { int w = 0; while (! proc.liveConvLoaded() && w < 4000) { juce::Thread::sleep(20); w += 20; } }

        // Estimate the wet output's dominant frequency via zero crossings for a 220 Hz sine.
        auto domFreq = [&](float semis)
        {
            proc.setPitchSemitones(semis);
            juce::Thread::sleep(250);                        // let the worker settle any rebuild
            juce::AudioBuffer<float> blk(2, 512); juce::MidiBuffer mi;
            double phase = 0.0; const double w0 = juce::MathConstants<double>::twoPi * 220.0 / SR;
            int zc = 0, cnt = 0; float prev = 0.0f;
            for (int k = 0; k < 400; ++k)                    // ~4.3 s: fill the preverb + settle
            {
                for (int i = 0; i < 512; ++i)
                { const float s = 0.4f * (float) std::sin(phase); phase += w0;
                  blk.setSample(0, i, s); blk.setSample(1, i, s); }
                juce::AudioBuffer<float> sub(blk.getArrayOfWritePointers(), 2, 512);
                proc.processBlock(sub, mi);
                if (k > 250)                                  // steady state only
                    for (int i = 0; i < 512; ++i)
                    { const float v = sub.getSample(0, i);
                      if (prev <= 0.0f && v > 0.0f) ++zc;
                      prev = v; ++cnt; }
            }
            return (double) zc * SR / juce::jmax(1, cnt);    // positive-going crossings = Hz
        };
        const double f0 = domFreq(0.0f);
        const double f12 = domFreq(12.0f);
        check("live pitch 0 st: wet tracks the input (~220 Hz)", std::abs(f0 - 220.0) < 25.0,
              "f=" + juce::String(f0, 1) + " Hz");
        check("live pitch +12 st: wet is a REAL octave up (~440 Hz)",
              std::abs(f12 - 440.0) < 80.0 && f12 > f0 * 1.7,
              "f=" + juce::String(f12, 1) + " Hz (unity was " + juce::String(f0, 1) + ")");
        proc.setPitchSemitones(0.0f);

        // Seamless kernel swap: steady noise, rebuild the (same-length) kernel mid-stream, keep
        // processing through the crossfade — output must never drop out.
        proc.setMacroMix(1.0f); proc.setLiveWet(1.0f); proc.setLiveDry(0.0f);
        juce::Thread::sleep(250);
        juce::Random rng(23); juce::AudioBuffer<float> nb(2, 512); juce::MidiBuffer mi2;
        double minRms = 1.0e9; int measured = 0;
        for (int k = 0; k < 300; ++k)
        {
            for (int i = 0; i < 512; ++i) { const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.4f;
                nb.setSample(0, i, s); nb.setSample(1, i, s); }
            juce::AudioBuffer<float> ns(nb.getArrayOfWritePointers(), 2, 512);
            proc.processBlock(ns, mi2);
            if (k == 150) { proc.setReverbParam(1, 0.5f); proc.rebuildLiveIR(); }   // knob change mid-stream
            if (k >= 150) juce::Thread::sleep(2);   // ~real-time pacing: the async kernel load must
                                                    // land in wall-clock time, as it does in a DAW
            if (k > 120)
            {
                double s2 = 0.0; for (int i = 0; i < 512; ++i) { const double v = ns.getSample(0, i); s2 += v * v; }
                minRms = juce::jmin(minRms, std::sqrt(s2 / 512.0)); ++measured;
            }
        }
        check("kernel swap mid-stream: no dropout (min block RMS holds)", minRms > 0.01 && measured > 0,
              "minRms=" + juce::String(minRms, 4));

        proc.setLiveMode(false); proc.setMacroMix(0.25f); proc.setLiveDry(1.0f);
        setReverb(proc, 1);
    }

    std::printf("\nRESULT: %d passed, %d failed -> %s\n", g_pass, g_fail,
                g_fail == 0 ? "ALL RENDER CHECKS PASS" : "FAILURES ABOVE");
    tmp.deleteRecursively();
    return g_fail == 0 ? 0 : 1;
}
