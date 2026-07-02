#pragma once
#include <JuceHeader.h>

//==============================================================================
//  Sound Detective: Clocker — DAW-native time tracking & project profitability.
//
//  No audio processing: processBlock is a passthrough. All tracking is simple
//  timestamp math on the message thread, persisted in a ValueTree that rides
//  inside the DAW session state. Entries are only written on meaningful events
//  (clock in/out, pause, manual entry, session save) — never per-second.
//==============================================================================

namespace clocker
{
namespace ids
{
    #define CLKR_ID(name) static const juce::Identifier name(#name);
    CLKR_ID(Clocker) CLKR_ID(Project) CLKR_ID(Settings) CLKR_ID(Entries)
    CLKR_ID(Entry)   CLKR_ID(Active)
    // project / client
    CLKR_ID(client) CLKR_ID(project) CLKR_ID(song) CLKR_ID(engineer)
    CLKR_ID(projectType) CLKR_ID(hourlyRate) CLKR_ID(flatFee)
    CLKR_ID(billingNotes) CLKR_ID(invoiceNotes)
    // settings
    CLKR_ID(defaultRate) CLKR_ID(defaultBillable) CLKR_ID(defaultType)
    CLKR_ID(rounding) CLKR_ID(timeFormat)
    // entry
    CLKR_ID(start) CLKR_ID(end) CLKR_ID(durationMs) CLKR_ID(billable)
    CLKR_ID(type) CLKR_ID(notes) CLKR_ID(manual)
    // active timer
    CLKR_ID(clockIn) CLKR_ID(accumulated) CLKR_ID(running)
    CLKR_ID(lastResume) CLKR_ID(savedAt)
    CLKR_ID(onBreak) CLKR_ID(prevType) CLKR_ID(prevBillable)
    #undef CLKR_ID
}

const juce::StringArray& sessionTypes();          // Production, Recording, ...
const juce::StringArray& projectTypes();          // Hourly, Flat Rate, Hybrid, Internal

juce::String formatClock(juce::int64 ms);                       // 01:42:18
juce::String formatDuration(juce::int64 ms);                    // 2h 15m
juce::String formatHours(juce::int64 ms);                       // 2.25 h
juce::String formatMoney(double amount);                        // $1,500.00
juce::String formatDate(juce::int64 epochMs);                   // Jul 02 14:30

struct Totals
{
    juce::int64 totalMs = 0, billableMs = 0, nonBillableMs = 0;
    juce::int64 billableRoundedMs = 0;            // after the billing rounding pref
    juce::int64 typeMs[16] = {};
    int    projectType   = 0;
    double hourlyRate    = 0.0, flatFee = 0.0;
    double amount        = 0.0;                   // estimated billing
    double effectiveRate = 0.0;                   // amount / total hours
};
} // namespace clocker

//==============================================================================
class ClockerProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster
{
public:
    ClockerProcessor();
    ~ClockerProcessor() override = default;

    //=== timer engine (message thread only) ===================================
    enum class ClockState { idle, running, paused };
    ClockState clockState() const;
    juce::int64 elapsedMs() const;                // live elapsed incl. running span

    void clockIn();
    void pauseTimer();
    void resumeTimer();
    void clockOut (const juce::String& notes);    // closes entry, clears Active
    void discardActive();                         // reset without logging

    bool activeBillable() const;
    int  activeType() const;
    void setActiveBillable (bool b);
    void setActiveType (int t);

    // Break: closes the current work span into the log, then starts a
    // non-billable "Break" entry so break time is tracked, not lost.
    bool isOnBreak() const;
    void startBreak (const juce::String& notesForCurrentEntry);
    void endBreak();                     // resumes previous type/billable state

    void addManualEntry (juce::int64 durMs, bool billable, int type,
                         const juce::String& notes);

    //=== data =================================================================
    juce::ValueTree state { clocker::ids::Clocker };
    juce::ValueTree project()  const { return state.getChildWithName (clocker::ids::Project); }
    juce::ValueTree settings() const { return state.getChildWithName (clocker::ids::Settings); }
    juce::ValueTree entries()  const { return state.getChildWithName (clocker::ids::Entries); }

    clocker::Totals computeTotals() const;
    int  roundingMinutes() const;
    juce::int64 roundMsForBilling (juce::int64 ms) const;
    void resetProject();                          // New Project / Reset

    //=== exports ==============================================================
    juce::String buildReport (bool markdown) const;
    juce::String buildCSV()  const;
    juce::String buildJSON() const;
    static juce::File clockerFolder();            // ~/Documents/Sound Detective/Clocker
    juce::String exportBaseName() const;          // sanitized client_project_stamp

    //=== boilerplate ==========================================================
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }
    const juce::String getName() const override            { return "Detective 47s Clocker"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    void ensureStructure();
    void appendEntry (juce::int64 start, juce::int64 end, juce::int64 durMs,
                      bool billable, int type, const juce::String& notes, bool manual);
    juce::ValueTree activeTree() const { return state.getChildWithName (clocker::ids::Active); }

    juce::CriticalSection stateLock;   // guards (de)serialization vs message-thread edits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClockerProcessor)
};
