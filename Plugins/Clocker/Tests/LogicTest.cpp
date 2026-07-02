// Headless logic test for Clocker — drives the real processor with no GUI and
// checks the timer engine, billing math, rounding, exports, and the
// save/restore auto-close path. Simulated elapsed time is created by shifting
// the Active tree's timestamps backwards, so the test runs instantly.
#include <JuceHeader.h>
#include "PluginProcessor.h"

using namespace clocker;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static void shiftActiveBack (ClockerProcessor& p, juce::int64 minutes)
{
    auto a = p.state.getChildWithName (ids::Active);
    a.setProperty (ids::lastResume,
                   (juce::int64) a.getProperty (ids::lastResume) - minutes * 60000, nullptr);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    ClockerProcessor p;
    p.project().setProperty (ids::projectType, 0, nullptr);      // hourly
    p.project().setProperty (ids::hourlyRate, 100.0, nullptr);
    p.project().setProperty (ids::client, "Client A", nullptr);
    p.project().setProperty (ids::project, "Song Title", nullptr);

    // --- clock in / pause / resume / clock out --------------------------------
    check (p.clockState() == ClockerProcessor::ClockState::idle, "starts idle");
    p.clockIn();
    check (p.clockState() == ClockerProcessor::ClockState::running, "clock in -> running");

    shiftActiveBack (p, 90);                                     // simulate 90 min of work
    const auto e1 = p.elapsedMs();
    check (std::abs (e1 - 90 * 60000LL) < 2000, "elapsed ~90m while running (" + formatDuration (e1) + ")");

    p.pauseTimer();
    check (p.clockState() == ClockerProcessor::ClockState::paused, "pause -> paused");
    const auto paused = p.elapsedMs();
    juce::Thread::sleep (50);
    check (p.elapsedMs() == paused, "elapsed frozen while paused");

    p.resumeTimer();
    shiftActiveBack (p, 30);                                     // 30 more minutes
    check (std::abs (p.elapsedMs() - 120 * 60000LL) < 2000, "elapsed ~2h after resume");

    p.clockOut ("Balanced drums, first vocal pass.");
    check (p.clockState() == ClockerProcessor::ClockState::idle, "clock out -> idle");
    check (p.entries().getNumChildren() == 1, "entry logged on clock out");

    // --- manual entry, billable split ----------------------------------------
    p.addManualEntry (65 * 60000LL, false, 9, "Organized files"); // Admin, non-billable
    check (p.entries().getNumChildren() == 2, "manual entry logged");

    auto t = p.computeTotals();
    check (std::abs (t.totalMs - 185 * 60000LL) < 2000, "total ~3h05m (" + formatDuration (t.totalMs) + ")");
    check (std::abs (t.billableMs - 120 * 60000LL) < 2000, "billable ~2h");
    check (std::abs (t.nonBillableMs - 65 * 60000LL) < 1000, "non-billable ~1h05m");
    check (std::abs (t.amount - 200.0) < 0.1, "hourly billing ~$200 (" + formatMoney (t.amount) + ")");

    // --- rounding -------------------------------------------------------------
    // exact billable is ~119:59.xx; 15-min rounding must snap it to 2h00m
    p.settings().setProperty (ids::rounding, 3, nullptr);        // nearest 15 min
    t = p.computeTotals();
    check (t.billableRoundedMs == 120 * 60000LL, "15-min rounding snaps to 2h00m");

    // --- flat rate / effective hourly -----------------------------------------
    p.project().setProperty (ids::projectType, 1, nullptr);      // flat
    p.project().setProperty (ids::flatFee, 1500.0, nullptr);
    t = p.computeTotals();
    const double hours = t.totalMs / 3600000.0;
    check (std::abs (t.effectiveRate - 1500.0 / hours) < 0.5,
           "flat-rate effective hourly = " + formatMoney (t.effectiveRate) + "/hr");

    // --- exports ---------------------------------------------------------------
    check (p.buildReport (false).contains ("Effective Hourly Rate"), "text report has effective rate");
    check (p.buildCSV().contains ("Balanced drums"), "CSV contains entry notes");
    auto parsed = juce::JSON::parse (p.buildJSON());
    check (parsed.isObject() && parsed["entries"].size() == 2, "JSON parses with 2 entries");

    // --- session save while clocked in -> restore auto-closes entry ------------
    p.clockIn();
    shiftActiveBack (p, 45);
    juce::MemoryBlock mb;
    p.getStateInformation (mb);

    ClockerProcessor p2;
    p2.setStateInformation (mb.getData(), (int) mb.getSize());
    check (p2.clockState() == ClockerProcessor::ClockState::idle, "restored instance is idle");
    check (p2.entries().getNumChildren() == 3, "running timer auto-closed into an entry on restore");
    auto last = p2.entries().getChild (2);
    check (std::abs ((juce::int64) last.getProperty (ids::durationMs) - 45 * 60000LL) < 2000,
           "auto-closed entry ~45m (" + formatDuration (last.getProperty (ids::durationMs)) + ")");
    check (last.getProperty (ids::notes).toString().contains ("auto-closed"), "auto-close is marked in notes");

    // --- audio passthrough is untouched ----------------------------------------
    p2.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buf.setSample (ch, i, std::sin (0.01f * (float) i) * (ch == 0 ? 1.0f : -0.5f));
    juce::AudioBuffer<float> ref;
    ref.makeCopyOf (buf);
    juce::MidiBuffer midi;
    p2.processBlock (buf, midi);
    bool identical = true;
    for (int ch = 0; ch < 2 && identical; ++ch)
        for (int i = 0; i < 512; ++i)
            if (buf.getSample (ch, i) != ref.getSample (ch, i)) { identical = false; break; }
    check (identical, "processBlock is a bit-exact passthrough");

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURES")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
