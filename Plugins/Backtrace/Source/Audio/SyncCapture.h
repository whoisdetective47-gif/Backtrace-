#pragma once
#include <JuceHeader.h>
#include "Vault/CaptureEngine.h"

// =============================================================================
//  SyncCapture — DAW-synced capture state machine (Phase 1b).
//
//  Reads host transport via AudioPlayHead and drives the Vault CaptureEngine:
//
//     IDLE ─arm─▶ ARMED ─reach start─▶ CAPTURING ─reach end─▶ FINISHED ─▶ IDLE
//                   │                       │
//                   └────── disarm ◀────────┘
//
//  Capture modes:
//     Manual        — arm starts immediately, disarm stops (no auto-stop).
//     1/2/4 Bars    — wait for the next bar line, capture N bars.
//     DAW Locators  — wait for the loop start, stop at the loop end.
//     Next Cycle    — wait for the next loop wrap, capture one cycle.
//
//  Fallback: if the host exposes no usable loop points, DAW Locators / Next
//  Cycle fall back to a musical length (fallbackBars), starting at the next bar.
//
//  Threading: UI-thread setters/getters use atomics; the state machine and all
//  CaptureEngine start/stop/push calls run on the audio thread only.
// =============================================================================
class SyncCapture
{
public:
    enum class Mode  { Manual = 0, Bars1, Bars2, Bars4, DawLocators, NextCycle };
    enum class State { Idle = 0, Armed, Capturing, Finished };

    void prepare(double sr) { sampleRate = sr; }

    // ---- UI thread: setters ----
    void setMode(int m)            { mode.store(m); }
    void setFallbackBars(int bars) { fallbackBars.store(bars); }
    void setLocatorLock(bool on)   { locatorLock.store(on); }
    void arm()                     { armRequest.store(true); }
    void disarm()                  { disarmRequest.store(true); }

    // ---- UI thread: getters (published from the audio thread) ----
    State  getState()          const { return (State) state.load(); }
    double getBpm()            const { return bpmPub.load(); }
    int    getTsNumerator()    const { return tsNumPub.load(); }
    int    getTsDenominator()  const { return tsDenPub.load(); }
    bool   isHostPlaying()     const { return playingPub.load(); }
    double getPpq()            const { return ppqPub.load(); }
    bool   locatorAvailable()  const { return locAvailPub.load(); }
    bool   fallbackActive()    const { return fallbackPub.load(); }
    double getLocStartPpq()    const { return locStartPub.load(); }
    double getLocEndPpq()      const { return locEndPub.load(); }
    int    getPlannedSamples() const { return plannedPub.load(); }
    bool   justFinished()            { return finishedFlag.exchange(false); }

    // ---- finished-capture descriptor (for slot metadata) ----
    int    finMode()      const { return finModeV.load(); }
    double finBpm()       const { return finBpmV.load(); }
    int    finTsNum()     const { return finTsNumV.load(); }
    int    finTsDen()     const { return finTsDenV.load(); }
    bool   finLocked()    const { return finLockedV.load(); }
    double finPpqStart()  const { return finPpqStartV.load(); }
    double finPpqEnd()    const { return finPpqEndV.load(); }
    int    finSamples()   const { return finSamplesV.load(); }

    // ---- audio thread ----
    void processBlock(CaptureEngine& cap, const float* L, const float* R, int n,
                      juce::AudioPlayHead* playHead) noexcept
    {
        // --- read + publish transport ---
        double bpm = 120.0; int tsNum = 4, tsDen = 4;
        bool playing = false; double ppq = 0.0;
        bool locAvail = false; double locStart = 0.0, locEnd = 0.0;
        double lastBar = 0.0; bool haveBar = false;

        if (playHead != nullptr)
            if (auto p = playHead->getPosition())
            {
                if (auto b  = p->getBpm())                    bpm = *b;
                if (auto ts = p->getTimeSignature())        { tsNum = ts->numerator; tsDen = ts->denominator; }
                playing = p->getIsPlaying();
                if (auto q  = p->getPpqPosition())            ppq = *q;
                if (auto lb = p->getPpqPositionOfLastBarStart()) { lastBar = *lb; haveBar = true; }
                if (auto lp = p->getLoopPoints())
                {
                    locStart = lp->ppqStart; locEnd = lp->ppqEnd;
                    locAvail = (locEnd > locStart + 1.0e-6);
                }
            }

        const double ppqPerSample = bpm / (60.0 * sampleRate);
        const double qpb = 4.0 * (double) tsNum / (double) juce::jmax(1, tsDen); // quarter-notes per bar
        const int    m   = mode.load();
        const bool   locatorMode = (m == (int) Mode::DawLocators || m == (int) Mode::NextCycle);
        const bool   fbActive = locatorMode && ! locAvail;

        bpmPub.store(bpm); tsNumPub.store(tsNum); tsDenPub.store(tsDen);
        playingPub.store(playing); ppqPub.store(ppq);
        locAvailPub.store(locAvail); locStartPub.store(locStart); locEndPub.store(locEnd);
        fallbackPub.store(fbActive);

        // planned length (for the readout)
        {
            int planned = 0;
            if (m == (int) Mode::DawLocators || m == (int) Mode::NextCycle)
                planned = locAvail ? (int) std::round((locEnd - locStart) / ppqPerSample)
                                   : (int) std::round(fallbackBars.load() * qpb / ppqPerSample);
            else if (m != (int) Mode::Manual)
                planned = (int) std::round(barsForMode(m) * qpb / ppqPerSample);
            plannedPub.store(planned);
        }

        int st = state.load();
        bool pushed = false;

        // FINISHED is a one-block pulse — the UI latches it via justFinished();
        // return to IDLE so the next arm() is accepted.
        if (st == (int) State::Finished) { st = (int) State::Idle; state.store(st); }

        // --- disarm ---
        if (disarmRequest.exchange(false))
        {
            cap.stop();
            if (st == (int) State::Capturing)   // stopping a live take → publish it
            {
                finish(cap);
                st = (int) State::Finished;
            }
            else
            {
                st = (int) State::Idle;
                state.store(st);
            }
        }

        // --- arm ---
        if (armRequest.exchange(false) && st == (int) State::Idle)
        {
            planLocator = false; waitForWrap = false; planNoAutoStop = false;
            planBars = 0;

            if (m == (int) Mode::Manual)
            {
                planNoAutoStop = true;
                planStartPpq = ppq; planEndPpq = ppq;
                cap.start();
                cap.pushBlock(L, R, n);
                st = (int) State::Capturing; state.store(st);
                pushed = true;
            }
            else if (locatorMode && locAvail)
            {
                planLocator = true;
                waitForWrap = (m == (int) Mode::NextCycle);
                armedLocStart = locStart; armedLocEnd = locEnd;     // snapshot (Locator Lock)
                planStartPpq = locStart;  planEndPpq = locEnd;
                st = (int) State::Armed; state.store(st);
            }
            else
            {
                // musical: bar modes, or locator modes falling back
                planBars = locatorMode ? fallbackBars.load() : barsForMode(m);
                st = (int) State::Armed; state.store(st);
            }
        }

        // --- armed: watch for the start edge ---
        if (st == (int) State::Armed && ! pushed)
        {
            bool started = false; int startOff = 0;
            const bool wrapped = haveLastPpq && playing && ppq < lastPpq - 1.0e-9;

            if (planLocator)
            {
                if (waitForWrap)
                {
                    if (wrapped)                 { started = true; startOff = 0; }
                    else if (playing)            { startOff = crossOffset(armedLocStart, ppq, ppqPerSample, n, started); }
                }
                else
                {
                    if (wrapped && armedLocStart <= ppq + 1.0e-6) { started = true; startOff = 0; }
                    else if (playing)            { startOff = crossOffset(armedLocStart, ppq, ppqPerSample, n, started); }
                }
                planStartPpq = armedLocStart; planEndPpq = armedLocEnd;
            }
            else if (playing) // musical
            {
                const double line = nextBarLine(ppq, qpb, lastBar, haveBar);
                startOff = crossOffset(line, ppq, ppqPerSample, n, started);
                if (started) { planStartPpq = line; planEndPpq = line + planBars * qpb; }
            }

            if (started)
            {
                cap.start();
                cap.pushBlock(L + startOff, (R != nullptr ? R + startOff : nullptr), n - startOff);
                st = (int) State::Capturing; state.store(st);
                pushed = true;
            }
        }

        // --- capturing: push, watch for the stop edge ---
        if (st == (int) State::Capturing && ! pushed)
        {
            if (planNoAutoStop)
            {
                cap.pushBlock(L, R, n);
                if (cap.hitMax()) finish(cap);
            }
            else
            {
                const bool wrapped = haveLastPpq && playing && ppq < lastPpq - 1.0e-9;
                if (wrapped)
                {
                    cap.stop(); finish(cap);                         // loop wrapped → cycle complete
                }
                else
                {
                    const double off = (planEndPpq - ppq) / ppqPerSample;
                    if (off <= 0.0)            { cap.stop(); finish(cap); }
                    else if (off < (double) n) { cap.pushBlock(L, R, (int) std::round(off)); cap.stop(); finish(cap); }
                    else                       { cap.pushBlock(L, R, n); if (cap.hitMax()) finish(cap); }
                }
            }
        }

        // --- track ppq for wrap detection ---
        if (playing) { lastPpq = ppq; haveLastPpq = true; }
        else           haveLastPpq = false;
    }

private:
    static int barsForMode(int m)
    {
        switch ((Mode) m)
        {
            case Mode::Bars1: return 1;
            case Mode::Bars2: return 2;
            case Mode::Bars4: return 4;
            default:          return 0;
        }
    }

    // Offset within the block where target ppq is crossed, or sets found=false.
    static int crossOffset(double targetPpq, double blockStartPpq,
                           double ppqPerSample, int n, bool& found) noexcept
    {
        const double off = (targetPpq - blockStartPpq) / ppqPerSample;
        if (off >= 0.0 && off < (double) n) { found = true; return (int) std::round(off); }
        found = false; return 0;
    }

    static double nextBarLine(double cur, double qpb, double lastBar, bool haveBar) noexcept
    {
        if (haveBar)
        {
            double nb = lastBar + qpb;
            while (nb <= cur + 1.0e-6) nb += qpb;
            return nb;
        }
        const double idx = std::floor((cur + 1.0e-6) / qpb);
        return (idx + 1.0) * qpb;
    }

    void finish(CaptureEngine& cap) noexcept
    {
        state.store((int) State::Finished);
        finishedFlag.store(true);

        finModeV.store(mode.load());
        finBpmV.store(bpmPub.load());
        finTsNumV.store(tsNumPub.load());
        finTsDenV.store(tsDenPub.load());
        finLockedV.store(locatorLock.load());
        finPpqStartV.store(planStartPpq);
        finPpqEndV.store(planEndPpq);
        finSamplesV.store(cap.getWritePos());
    }

    double sampleRate = 44100.0;

    // UI → audio
    std::atomic<int>  mode { 0 };
    std::atomic<int>  fallbackBars { 4 };
    std::atomic<bool> locatorLock { true };
    std::atomic<bool> armRequest { false }, disarmRequest { false };

    // audio → UI
    std::atomic<int>    state { 0 };
    std::atomic<double> bpmPub { 120.0 };
    std::atomic<int>    tsNumPub { 4 }, tsDenPub { 4 };
    std::atomic<bool>   playingPub { false };
    std::atomic<double> ppqPub { 0.0 };
    std::atomic<bool>   locAvailPub { false }, fallbackPub { false };
    std::atomic<double> locStartPub { 0.0 }, locEndPub { 0.0 };
    std::atomic<int>    plannedPub { 0 };
    std::atomic<bool>   finishedFlag { false };

    std::atomic<int>    finModeV { 0 }, finTsNumV { 4 }, finTsDenV { 4 }, finSamplesV { 0 };
    std::atomic<double> finBpmV { 120.0 }, finPpqStartV { 0.0 }, finPpqEndV { 0.0 };
    std::atomic<bool>   finLockedV { false };

    // audio-thread-only plan + history
    bool   planLocator = false, waitForWrap = false, planNoAutoStop = false;
    int    planBars = 0;
    double planStartPpq = 0.0, planEndPpq = 0.0;
    double armedLocStart = 0.0, armedLocEnd = 0.0;
    double lastPpq = 0.0;
    bool   haveLastPpq = false;
};
