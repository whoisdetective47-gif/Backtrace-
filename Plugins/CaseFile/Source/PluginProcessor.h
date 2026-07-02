#pragma once
#include <JuceHeader.h>
#include "AnalysisEngine.h"
#include "SuspectRules.h"

//==============================================================================
//  Sound Detective: Case File — the mix/production command center.
//
//  Open the case. Load the evidence. Analyze the record. Identify the
//  suspects. Build the checklist. Log the revisions. Close the case.
//
//  No audio processing: processBlock is a passthrough. The entire case —
//  brief, song map, evidence, analysis, suspects, checklists, plugin
//  library, hardware locker, chains, versions, report notes — lives in one
//  ValueTree that rides inside the DAW session state. That same tree is the
//  AI-ready packet for later versions (buildJSON exports it verbatim).
//==============================================================================

namespace casefile
{
namespace ids
{
    #define CF_ID(name) static const juce::Identifier name(#name);
    // top level + branches
    CF_ID(CaseFile) CF_ID(Brief) CF_ID(SongMap) CF_ID(Sections) CF_ID(Section)
    CF_ID(PluginLib) CF_ID(PluginItem) CF_ID(Hardware) CF_ID(HardwareItem)
    CF_ID(Checklist) CF_ID(ChecklistItem) CF_ID(Evidence) CF_ID(EvidenceItem)
    CF_ID(Suspects) CF_ID(Suspect) CF_ID(Chains) CF_ID(Chain)
    CF_ID(Versions) CF_ID(Version) CF_ID(Report)
    // case meta
    CF_ID(caseNumber) CF_ID(openedAt)
    // brief
    CF_ID(songTitle) CF_ID(artist) CF_ID(producer) CF_ID(mixer) CF_ID(genre)
    CF_ID(tempo) CF_ID(songKey) CF_ID(sampleRateTxt) CF_ID(bitDepthTxt)
    CF_ID(mixStage) CF_ID(deadline) CF_ID(mainRefs) CF_ID(emotionalTarget)
    CF_ID(mixGoal) CF_ID(biggestProblem) CF_ID(clientNotes) CF_ID(deliveryReqs)
    CF_ID(interview)
    // song map
    CF_ID(scaleMode) CF_ID(timeSig) CF_ID(chordProgression) CF_ID(mainHook)
    CF_ID(lowEndRel) CF_ID(songNotes)
    // section
    CF_ID(name) CF_ID(startTime) CF_ID(endTime) CF_ID(startBar) CF_ID(endBar)
    CF_ID(chords) CF_ID(energy) CF_ID(notes) CF_ID(sectionMixGoal) CF_ID(prodGoal)
    // plugin library
    CF_ID(company) CF_ID(category) CF_ID(favorite) CF_ID(ownership)
    CF_ID(bestUse) CF_ID(cpuHeavy) CF_ID(usedOften)
    // hardware
    CF_ID(brand) CF_ID(gearType) CF_ID(stereoMono) CF_ID(numChannels)
    CF_ID(insertPath) CF_ID(favoriteUse) CF_ID(recallNotes) CF_ID(maintenanceNotes)
    // checklist
    CF_ID(text) CF_ID(done) CF_ID(group) CF_ID(section) CF_ID(fromSuspect) CF_ID(seeded)
    // evidence
    CF_ID(path) CF_ID(role) CF_ID(lengthSec) CF_ID(fileSampleRate) CF_ID(channels)
    CF_ID(bitDepth) CF_ID(analyzed) CF_ID(peakDb) CF_ID(rmsDb) CF_ID(crestDb)
    CF_ID(widthPct) CF_ID(lowWidthPct) CF_ID(corr)
    CF_ID(b0) CF_ID(b1) CF_ID(b2) CF_ID(b3) CF_ID(b4) CF_ID(b5) CF_ID(b6)
    // suspects
    CF_ID(title) CF_ID(range) CF_ID(severity) CF_ID(sources) CF_ID(why)
    CF_ID(actions) CF_ID(solved) CF_ID(custom)
    // chains
    CF_ID(trackType) CF_ID(trackName) CF_ID(pluginChain) CF_ID(outboardChain)
    CF_ID(mic) CF_ID(preamp) CF_ID(compressor) CF_ID(eq) CF_ID(deesser)
    CF_ID(fxSends) CF_ID(mainProblem) CF_ID(plan) CF_ID(revisionHistory)
    // versions
    CF_ID(dateMs) CF_ID(clientFeedback) CF_ID(changes) CF_ID(problems)
    CF_ID(printedFiles) CF_ID(analysisSummary)
    // report
    CF_ID(finalDeliveryNotes)
    #undef CF_ID
}

const juce::StringArray& evidenceRoles();      // Current Mix, Main Sonic Target, ...
const juce::StringArray& pluginCategories();
const juce::StringArray& ownershipTypes();     // Owned, Demo, Trial
const juce::StringArray& hardwareTypes();
const juce::StringArray& stereoMonoTypes();    // Mono, Stereo, Dual Mono
const juce::StringArray& chainTemplates();     // Lead Vocal, Drum Bus, ...
const juce::StringArray& checklistGroups();    // Production, Mix, Master/Delivery, Custom
const juce::StringArray& severityNames();      // Low, Medium, High
const juce::StringArray& energyLevels();       // Low, Medium, High, Peak
const juce::StringArray& sectionPresets();     // Intro, Verse 1, ...
const juce::StringArray& mixStages();          // Production, Pre-Mix, Mixing, ...

juce::String csvCell (const juce::String&);
int safeIndex (const juce::var& v, const juce::StringArray& list);

} // namespace casefile

//==============================================================================
class CaseFileProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster
{
public:
    CaseFileProcessor();
    ~CaseFileProcessor() override = default;

    //=== case data (message thread only) ======================================
    juce::ValueTree state { casefile::ids::CaseFile };
    juce::ValueTree brief()     const { return state.getChildWithName (casefile::ids::Brief); }
    juce::ValueTree songMap()   const { return state.getChildWithName (casefile::ids::SongMap); }
    juce::ValueTree sections()  const { return songMap().getChildWithName (casefile::ids::Sections); }
    juce::ValueTree pluginLib() const { return state.getChildWithName (casefile::ids::PluginLib); }
    juce::ValueTree hardware()  const { return state.getChildWithName (casefile::ids::Hardware); }
    juce::ValueTree checklist() const { return state.getChildWithName (casefile::ids::Checklist); }
    juce::ValueTree evidence()  const { return state.getChildWithName (casefile::ids::Evidence); }
    juce::ValueTree suspects()  const { return state.getChildWithName (casefile::ids::Suspects); }
    juce::ValueTree chains()    const { return state.getChildWithName (casefile::ids::Chains); }
    juce::ValueTree versions()  const { return state.getChildWithName (casefile::ids::Versions); }
    juce::ValueTree report()    const { return state.getChildWithName (casefile::ids::Report); }

    juce::ValueTree addChildTo (juce::ValueTree parent, const juce::Identifier& type);
    void removeChild (juce::ValueTree parent, juce::ValueTree child);
    void notify() { sendChangeMessage(); }

    //=== evidence & analysis ==================================================
    juce::ValueTree addEvidence (const juce::File&, int role);
    bool analyzeEvidenceItem (juce::ValueTree item);          // offline, message thread
    int  analyzeAllEvidence();                                // returns #analyzed ok
    static casefile::AnalysisResult resultFromTree (const juce::ValueTree& item);

    //=== suspects =============================================================
    // Analyze anything pending, then regenerate rule-based suspect cards.
    // Solved and user-created cards are kept; stale auto cards are replaced.
    // Returns #cards generated, or -1 if there's no analyzed current mix +
    // reference pair to compare.
    int investigate();
    juce::ValueTree addSuspect (const casefile::SuspectDef&, bool custom);
    juce::ValueTree suspectToChecklistItem (juce::ValueTree suspect);

    //=== checklist ============================================================
    juce::ValueTree addChecklistItem (const juce::String& text, int group,
                                      const juce::String& section, bool fromSuspect);

    //=== song map =============================================================
    juce::ValueTree addSection (const juce::String& name);

    //=== chains ===============================================================
    juce::ValueTree addChain (int templateIndex);

    //=== versions =============================================================
    juce::ValueTree addVersion (const juce::String& name);

    //=== plugin library / hardware locker =====================================
    int  bulkAddPlugins (const juce::String& pastedLines);   // "Name, Company, Category" per line
    juce::String pluginLibCSV() const;
    int  importPluginCSV (const juce::String& csvText);
    juce::String hardwareCSV() const;

    //=== analysis & report text ===============================================
    juce::String buildAnalysisSummary() const;   // Analysis tab readout
    juce::String buildReport (bool markdown) const;
    juce::String buildJSON() const;              // AI-ready case packet
    static juce::File caseFileFolder();          // ~/Documents/Sound Detective/Case File
    juce::String exportBaseName() const;

    //=== boilerplate ==========================================================
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }
    const juce::String getName() const override            { return "Detective 47s Case File"; }
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
    void seedChecklist();

    juce::CriticalSection stateLock;   // guards (de)serialization vs message-thread edits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaseFileProcessor)
};
