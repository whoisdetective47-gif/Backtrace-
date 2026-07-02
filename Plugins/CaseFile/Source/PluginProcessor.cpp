#include "PluginProcessor.h"
#if ! CASEFILE_NO_EDITOR
 #include "PluginEditor.h"
#endif

using namespace casefile;

//==============================================================================
// shared lists
//==============================================================================
const juce::StringArray& casefile::evidenceRoles()
{
    static const juce::StringArray t {
        "Current Mix", "Main Sonic Target", "Low-End Target", "Vocal Target",
        "Drum Target", "Width Target", "Loudness Target", "Vibe / Texture Target",
        "Arrangement Target"
    };
    return t;
}

const juce::StringArray& casefile::pluginCategories()
{
    static const juce::StringArray t {
        "EQ", "Compressor", "Limiter", "Saturation", "Tape", "Reverb", "Delay",
        "Modulation", "Pitch", "Vocal Tool", "Drum Tool", "Bass Tool",
        "Mastering Tool", "Utility", "Metering", "Sound Design", "Repair", "Other"
    };
    return t;
}

const juce::StringArray& casefile::ownershipTypes()
{
    static const juce::StringArray t { "Owned", "Demo", "Trial" };
    return t;
}

const juce::StringArray& casefile::hardwareTypes()
{
    static const juce::StringArray t {
        "Mic", "Preamp", "Compressor", "EQ", "Limiter", "Saturator",
        "Tape / Tape-Style", "Reverb", "Delay", "Modulation", "Synth", "Pedal",
        "Converter", "Clock", "Monitoring", "Patchbay", "Other"
    };
    return t;
}

const juce::StringArray& casefile::stereoMonoTypes()
{
    static const juce::StringArray t { "Mono", "Stereo", "Dual Mono" };
    return t;
}

const juce::StringArray& casefile::chainTemplates()
{
    static const juce::StringArray t {
        "Lead Vocal", "Background Vocals", "Drum Bus", "Kick", "Snare", "Bass",
        "Guitars", "Keys", "FX Returns", "Mix Bus", "Master Bus", "Custom Track"
    };
    return t;
}

const juce::StringArray& casefile::checklistGroups()
{
    static const juce::StringArray t { "Production", "Mix", "Master / Delivery", "Custom" };
    return t;
}

const juce::StringArray& casefile::severityNames()
{
    static const juce::StringArray t { "Low", "Medium", "High" };
    return t;
}

const juce::StringArray& casefile::energyLevels()
{
    static const juce::StringArray t { "Low", "Medium", "High", "Peak" };
    return t;
}

const juce::StringArray& casefile::sectionPresets()
{
    static const juce::StringArray t {
        "Intro", "Verse 1", "Pre-Chorus 1", "Chorus 1", "Verse 2", "Pre-Chorus 2",
        "Chorus 2", "Bridge", "Final Chorus", "Outro", "Custom Section"
    };
    return t;
}

const juce::StringArray& casefile::mixStages()
{
    static const juce::StringArray t {
        "Production", "Pre-Mix / Rough", "Mixing", "Mix Revisions",
        "Mastering", "Delivery"
    };
    return t;
}

juce::String casefile::csvCell (const juce::String& s)
{
    return "\"" + juce::String (s).replace ("\"", "\"\"") + "\"";
}

int casefile::safeIndex (const juce::var& v, const juce::StringArray& list)
{
    return juce::jlimit (0, list.size() - 1, (int) v);
}

//==============================================================================
CaseFileProcessor::CaseFileProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    ensureStructure();
}

void CaseFileProcessor::ensureStructure()
{
    auto getOrAdd = [this] (const juce::Identifier& id) -> juce::ValueTree
    {
        auto c = state.getChildWithName (id);
        if (! c.isValid()) { c = juce::ValueTree (id); state.appendChild (c, nullptr); }
        return c;
    };
    auto setIfMissing = [] (juce::ValueTree t, const juce::Identifier& p, const juce::var& v)
    { if (! t.hasProperty (p)) t.setProperty (p, v, nullptr); };

    // case meta — a fresh case gets a number and an opening date, like any file
    // worth keeping
    if (! state.hasProperty (ids::caseNumber))
    {
        juce::Random rnd;
        state.setProperty (ids::caseNumber,
                           "47-" + juce::String (1000 + rnd.nextInt (9000)), nullptr);
        state.setProperty (ids::openedAt, juce::Time::currentTimeMillis(), nullptr);
    }

    auto b = getOrAdd (ids::Brief);
    for (auto* p : { &ids::songTitle, &ids::artist, &ids::producer, &ids::mixer,
                     &ids::genre, &ids::tempo, &ids::songKey, &ids::sampleRateTxt,
                     &ids::bitDepthTxt, &ids::deadline, &ids::mainRefs,
                     &ids::emotionalTarget, &ids::mixGoal, &ids::biggestProblem,
                     &ids::clientNotes, &ids::deliveryReqs })
        setIfMissing (b, *p, "");
    setIfMissing (b, ids::mixStage, 2);   // Mixing
    setIfMissing (b, ids::interview,
        "What should the listener notice first?\n\n"
        "What is the hook?\n\n"
        "Clean, vintage, aggressive, dark, wide, intimate, raw, expensive, dirty, cinematic?\n\n"
        "Vocal in front, inside, or behind the track?\n\n"
        "Drums: live, sampled, crushed, roomy, dry, modern, vintage?\n\n"
        "Low end: kick above bass, bass above kick, or blended?\n\n");

    auto sm = getOrAdd (ids::SongMap);
    for (auto* p : { &ids::songKey, &ids::scaleMode, &ids::tempo, &ids::timeSig,
                     &ids::mainHook, &ids::lowEndRel, &ids::chordProgression })
        setIfMissing (sm, *p, "");
    setIfMissing (sm, ids::songNotes,
        "Energy arc:\n\nArrangement notes:\n\nInstrument roles:\n\n"
        "Vocal role:\n\nProduction notes:\n");
    if (! sm.getChildWithName (ids::Sections).isValid())
        sm.appendChild (juce::ValueTree (ids::Sections), nullptr);

    getOrAdd (ids::PluginLib);
    getOrAdd (ids::Hardware);
    getOrAdd (ids::Checklist);
    getOrAdd (ids::Evidence);
    getOrAdd (ids::Suspects);
    getOrAdd (ids::Chains);
    getOrAdd (ids::Versions);

    auto rep = getOrAdd (ids::Report);
    setIfMissing (rep, ids::finalDeliveryNotes, "");

    seedChecklist();
}

void CaseFileProcessor::seedChecklist()
{
    auto list = checklist();
    if ((bool) list.getProperty (ids::seeded, false))
        return;
    list.setProperty (ids::seeded, true, nullptr);

    static const char* production[] = {
        "Arrangement supports the hook", "Intro creates identity",
        "Transitions are interesting", "Verse/chorus contrast exists",
        "Vocal comps are clean", "Tuning/editing checked", "Drum pocket checked",
        "Low-end arrangement not fighting", "Ear candy placed before section changes" };
    static const char* mix[] = {
        "Gain staging checked", "Vocal level checked", "Kick/bass relationship checked",
        "Snare/clap level checked", "Low mids controlled", "Harshness checked",
        "Sibilance checked", "Mono checked", "Car/small speaker check done",
        "Reference check done", "Automation pass completed", "Mute pass completed",
        "FX throw pass completed", "Full mix printed", "Instrumental printed",
        "Acapella printed", "TV mix printed", "Clean version printed if needed" };
    static const char* master[] = {
        "Clipping checked", "True peak checked", "Fades checked",
        "Start/end silence checked", "File naming correct",
        "Sample rate/bit depth correct", "Metadata noted",
        "Version number documented", "Client notes addressed" };

    auto seed = [this] (const char* const* items, int n, int group)
    {
        for (int i = 0; i < n; ++i)
            addChecklistItem (items[i], group, {}, false);
    };
    seed (production, juce::numElementsInArray (production), 0);
    seed (mix,        juce::numElementsInArray (mix),        1);
    seed (master,     juce::numElementsInArray (master),     2);
}

//==============================================================================
// generic child helpers
//==============================================================================
juce::ValueTree CaseFileProcessor::addChildTo (juce::ValueTree parent, const juce::Identifier& type)
{
    juce::ValueTree c (type);
    parent.appendChild (c, nullptr);
    sendChangeMessage();
    return c;
}

void CaseFileProcessor::removeChild (juce::ValueTree parent, juce::ValueTree child)
{
    parent.removeChild (child, nullptr);
    sendChangeMessage();
}

//==============================================================================
// evidence & analysis
//==============================================================================
juce::ValueTree CaseFileProcessor::addEvidence (const juce::File& f, int role)
{
    juce::ValueTree e (ids::EvidenceItem);
    e.setProperty (ids::name, f.getFileNameWithoutExtension(), nullptr);
    e.setProperty (ids::path, f.getFullPathName(), nullptr);
    e.setProperty (ids::role, role, nullptr);
    e.setProperty (ids::notes, "", nullptr);
    e.setProperty (ids::analyzed, false, nullptr);
    evidence().appendChild (e, nullptr);
    sendChangeMessage();
    return e;
}

bool CaseFileProcessor::analyzeEvidenceItem (juce::ValueTree item)
{
    const juce::File f (item.getProperty (ids::path).toString());
    if (! f.existsAsFile()) return false;

    const auto res = analyzeFile (f);
    if (! res.valid) return false;

    item.setProperty (ids::lengthSec,      res.lengthSec,  nullptr);
    item.setProperty (ids::fileSampleRate, res.sampleRate, nullptr);
    item.setProperty (ids::channels,       res.channels,   nullptr);
    item.setProperty (ids::bitDepth,       res.bitDepth,   nullptr);
    item.setProperty (ids::peakDb,         res.peakDb,     nullptr);
    item.setProperty (ids::rmsDb,          res.rmsDb,      nullptr);
    item.setProperty (ids::crestDb,        res.crestDb,    nullptr);
    item.setProperty (ids::widthPct,       res.widthPct,   nullptr);
    item.setProperty (ids::lowWidthPct,    res.lowWidthPct, nullptr);
    item.setProperty (ids::corr,           res.corr,       nullptr);
    const juce::Identifier* bandIds[numBands] = { &ids::b0, &ids::b1, &ids::b2, &ids::b3,
                                                  &ids::b4, &ids::b5, &ids::b6 };
    for (int i = 0; i < numBands; ++i)
        item.setProperty (*bandIds[i], res.bandRel[i], nullptr);
    item.setProperty (ids::analyzed, true, nullptr);
    sendChangeMessage();
    return true;
}

int CaseFileProcessor::analyzeAllEvidence()
{
    int ok = 0;
    for (auto item : evidence())
        if (! (bool) item.getProperty (ids::analyzed, false))
            ok += analyzeEvidenceItem (item) ? 1 : 0;
    return ok;
}

AnalysisResult CaseFileProcessor::resultFromTree (const juce::ValueTree& item)
{
    AnalysisResult r;
    if (! (bool) item.getProperty (ids::analyzed, false)) return r;
    r.valid       = true;
    r.lengthSec   = item.getProperty (ids::lengthSec);
    r.sampleRate  = item.getProperty (ids::fileSampleRate);
    r.channels    = item.getProperty (ids::channels);
    r.bitDepth    = item.getProperty (ids::bitDepth);
    r.peakDb      = item.getProperty (ids::peakDb);
    r.rmsDb       = item.getProperty (ids::rmsDb);
    r.crestDb     = item.getProperty (ids::crestDb);
    r.widthPct    = item.getProperty (ids::widthPct);
    r.lowWidthPct = item.getProperty (ids::lowWidthPct);
    r.corr        = item.getProperty (ids::corr);
    const juce::Identifier* bandIds[numBands] = { &ids::b0, &ids::b1, &ids::b2, &ids::b3,
                                                  &ids::b4, &ids::b5, &ids::b6 };
    for (int i = 0; i < numBands; ++i)
        r.bandRel[i] = item.getProperty (*bandIds[i]);
    return r;
}

//==============================================================================
// suspects
//==============================================================================
static juce::ValueTree findEvidenceByRole (const juce::ValueTree& evid, int role)
{
    for (auto e : evid)
        if ((int) e.getProperty (ids::role, 1) == role
              && (bool) e.getProperty (ids::analyzed, false))
            return e;
    return {};
}

int CaseFileProcessor::investigate()
{
    analyzeAllEvidence();

    const auto evid = evidence();
    auto curTree = findEvidenceByRole (evid, 0);
    if (! curTree.isValid()) return -1;
    const auto cur = resultFromTree (curTree);

    // role-tagged references, falling back to the main sonic target, falling
    // back to any analyzed non-current-mix file
    auto mainTree = findEvidenceByRole (evid, 1);
    if (! mainTree.isValid())
        for (auto e : evid)
            if ((int) e.getProperty (ids::role, 1) != 0
                  && (bool) e.getProperty (ids::analyzed, false))
                { mainTree = e; break; }
    if (! mainTree.isValid()) return -1;

    const auto mainRef  = resultFromTree (mainTree);
    const auto lowRef   = resultFromTree (findEvidenceByRole (evid, 2));
    const auto vocalRef = resultFromTree (findEvidenceByRole (evid, 3));
    const auto widthRef = resultFromTree (findEvidenceByRole (evid, 5));
    const auto loudRef  = resultFromTree (findEvidenceByRole (evid, 6));

    RefSet refs;
    refs.main   = &mainRef;
    refs.lowEnd = lowRef.valid   ? &lowRef   : nullptr;
    refs.vocal  = vocalRef.valid ? &vocalRef : nullptr;
    refs.width  = widthRef.valid ? &widthRef : nullptr;
    refs.loud   = loudRef.valid  ? &loudRef  : nullptr;

    const auto defs = runSuspectRules (cur, refs);

    // replace stale auto-generated cards; keep solved cards and user cards
    auto sus = suspects();
    for (int i = sus.getNumChildren() - 1; i >= 0; --i)
    {
        auto s = sus.getChild (i);
        if (! (bool) s.getProperty (ids::custom, false)
              && ! (bool) s.getProperty (ids::solved, false))
            sus.removeChild (i, nullptr);
    }
    // don't re-open a case the detective already closed: a kept card with the
    // same title (solved or manual) suppresses the regenerated duplicate
    for (const auto& d : defs)
    {
        bool alreadyOnFile = false;
        for (auto s : sus)
            if (s.getProperty (ids::title).toString() == d.title)
                { alreadyOnFile = true; break; }
        if (! alreadyOnFile)
            addSuspect (d, false);
    }

    sendChangeMessage();
    return defs.size();
}

juce::ValueTree CaseFileProcessor::addSuspect (const SuspectDef& d, bool custom)
{
    juce::ValueTree s (ids::Suspect);
    s.setProperty (ids::title,    d.title,    nullptr);
    s.setProperty (ids::range,    d.range,    nullptr);
    s.setProperty (ids::severity, d.severity, nullptr);
    s.setProperty (ids::sources,  d.sources,  nullptr);
    s.setProperty (ids::why,      d.why,      nullptr);
    s.setProperty (ids::actions,  d.actions,  nullptr);
    s.setProperty (ids::solved,   false,      nullptr);
    s.setProperty (ids::custom,   custom,     nullptr);
    suspects().appendChild (s, nullptr);
    return s;
}

juce::ValueTree CaseFileProcessor::suspectToChecklistItem (juce::ValueTree s)
{
    auto text = "Investigate: " + s.getProperty (ids::title).toString();
    const auto range = s.getProperty (ids::range).toString();
    if (range.isNotEmpty()) text << " (" << range << ")";
    auto item = addChecklistItem (text, 3, {}, true);
    sendChangeMessage();
    return item;
}

//==============================================================================
// checklist / song map / chains / versions
//==============================================================================
juce::ValueTree CaseFileProcessor::addChecklistItem (const juce::String& text, int group,
                                                     const juce::String& section, bool fromSuspect)
{
    juce::ValueTree c (ids::ChecklistItem);
    c.setProperty (ids::text, text, nullptr);
    c.setProperty (ids::done, false, nullptr);
    c.setProperty (ids::group, group, nullptr);
    c.setProperty (ids::section, section, nullptr);
    c.setProperty (ids::fromSuspect, fromSuspect, nullptr);
    checklist().appendChild (c, nullptr);
    return c;
}

juce::ValueTree CaseFileProcessor::addSection (const juce::String& name)
{
    juce::ValueTree s (ids::Section);
    s.setProperty (ids::name, name, nullptr);
    for (auto* p : { &ids::startTime, &ids::endTime, &ids::startBar, &ids::endBar,
                     &ids::chords, &ids::notes, &ids::sectionMixGoal, &ids::prodGoal })
        s.setProperty (*p, "", nullptr);
    s.setProperty (ids::energy, 1, nullptr);
    sections().appendChild (s, nullptr);
    sendChangeMessage();
    return s;
}

juce::ValueTree CaseFileProcessor::addChain (int templateIndex)
{
    juce::ValueTree c (ids::Chain);
    c.setProperty (ids::trackType, templateIndex, nullptr);
    c.setProperty (ids::trackName, chainTemplates()[safeIndex (templateIndex, chainTemplates())], nullptr);
    for (auto* p : { &ids::pluginChain, &ids::outboardChain, &ids::mic, &ids::preamp,
                     &ids::compressor, &ids::eq, &ids::deesser, &ids::fxSends,
                     &ids::mainProblem, &ids::plan, &ids::notes, &ids::revisionHistory })
        c.setProperty (*p, "", nullptr);
    chains().appendChild (c, nullptr);
    sendChangeMessage();
    return c;
}

juce::ValueTree CaseFileProcessor::addVersion (const juce::String& name)
{
    juce::ValueTree v (ids::Version);
    v.setProperty (ids::name, name, nullptr);
    v.setProperty (ids::dateMs, juce::Time::currentTimeMillis(), nullptr);
    for (auto* p : { &ids::notes, &ids::clientFeedback, &ids::changes,
                     &ids::problems, &ids::printedFiles, &ids::analysisSummary })
        v.setProperty (*p, "", nullptr);
    versions().appendChild (v, nullptr);
    sendChangeMessage();
    return v;
}

//==============================================================================
// plugin library import/export
//==============================================================================
int CaseFileProcessor::bulkAddPlugins (const juce::String& pasted)
{
    int added = 0;
    for (auto line : juce::StringArray::fromLines (pasted))
    {
        line = line.trim();
        if (line.isEmpty()) continue;
        auto parts = juce::StringArray::fromTokens (line, ",", "\"");
        juce::ValueTree p (ids::PluginItem);
        p.setProperty (ids::name, parts[0].trim().unquoted(), nullptr);
        p.setProperty (ids::company, parts.size() > 1 ? parts[1].trim().unquoted() : juce::String(), nullptr);
        int cat = pluginCategories().size() - 1;   // Other
        if (parts.size() > 2)
        {
            const int found = pluginCategories().indexOf (parts[2].trim().unquoted(), true);
            if (found >= 0) cat = found;
        }
        p.setProperty (ids::category, cat, nullptr);
        p.setProperty (ids::favorite, false, nullptr);
        p.setProperty (ids::ownership, 0, nullptr);
        for (auto* prop : { &ids::notes, &ids::bestUse })
            p.setProperty (*prop, "", nullptr);
        p.setProperty (ids::cpuHeavy, false, nullptr);
        p.setProperty (ids::usedOften, false, nullptr);
        pluginLib().appendChild (p, nullptr);
        ++added;
    }
    if (added > 0) sendChangeMessage();
    return added;
}

juce::String CaseFileProcessor::pluginLibCSV() const
{
    juce::String s ("Name,Company,Category,Favorite,Ownership,CPU Heavy,Used Often,Best Use,Notes\n");
    for (auto p : pluginLib())
        s << csvCell (p.getProperty (ids::name).toString()) << ","
          << csvCell (p.getProperty (ids::company).toString()) << ","
          << csvCell (pluginCategories()[safeIndex (p.getProperty (ids::category), pluginCategories())]) << ","
          << ((bool) p.getProperty (ids::favorite) ? "Yes" : "No") << ","
          << csvCell (ownershipTypes()[safeIndex (p.getProperty (ids::ownership), ownershipTypes())]) << ","
          << ((bool) p.getProperty (ids::cpuHeavy) ? "Yes" : "No") << ","
          << ((bool) p.getProperty (ids::usedOften) ? "Yes" : "No") << ","
          << csvCell (p.getProperty (ids::bestUse).toString()) << ","
          << csvCell (p.getProperty (ids::notes).toString()) << "\n";
    return s;
}

int CaseFileProcessor::importPluginCSV (const juce::String& csvText)
{
    auto lines = juce::StringArray::fromLines (csvText);
    if (lines.size() > 0 && lines[0].startsWithIgnoreCase ("Name,"))
        lines.remove (0);   // header row
    return bulkAddPlugins (lines.joinIntoString ("\n"));
}

//==============================================================================
// plugin folder scan — file reads only, never loads plugin code
//==============================================================================
int casefile::categoryFromVst3Subcategories (const juce::String& subCatsIn)
{
    const auto s = subCatsIn.toLowerCase();
    if (s.contains ("eq"))           return pluginCategories().indexOf ("EQ");
    if (s.contains ("dynamics"))     return pluginCategories().indexOf ("Compressor");
    if (s.contains ("reverb"))       return pluginCategories().indexOf ("Reverb");
    if (s.contains ("delay"))        return pluginCategories().indexOf ("Delay");
    if (s.contains ("distortion"))   return pluginCategories().indexOf ("Saturation");
    if (s.contains ("mastering"))    return pluginCategories().indexOf ("Mastering Tool");
    if (s.contains ("restoration"))  return pluginCategories().indexOf ("Repair");
    if (s.contains ("analyzer"))     return pluginCategories().indexOf ("Metering");
    if (s.contains ("pitch"))        return pluginCategories().indexOf ("Pitch");
    if (s.contains ("modulation"))   return pluginCategories().indexOf ("Modulation");
    if (s.contains ("generator")
     || s.contains ("instrument")
     || s.contains ("synth"))        return pluginCategories().indexOf ("Sound Design");
    if (s.contains ("spatial")
     || s.contains ("tools"))        return pluginCategories().indexOf ("Utility");
    if (s.contains ("filter"))       return pluginCategories().indexOf ("Other");
    return -1;
}

int casefile::categoryFromName (const juce::String& pluginName)
{
    const auto n = pluginName.toLowerCase();
    auto any = [&n] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (n.contains (w)) return true;
        return false;
    };
    // ordered from most to least specific so e.g. "FreqEcho" hits Delay, not EQ
    if (any ({ "meter", "analyz", "scope", "lufs", "loudness", "spectrum" }))
        return pluginCategories().indexOf ("Metering");
    if (any ({ "de-ess", "deess", "vocal", "vox", "voice" }))
        return pluginCategories().indexOf ("Vocal Tool");
    if (any ({ "master" }))    return pluginCategories().indexOf ("Mastering Tool");
    if (any ({ "limit" }))     return pluginCategories().indexOf ("Limiter");
    if (any ({ "reverb", "verb", "plate", "spring", "room" }))
        return pluginCategories().indexOf ("Reverb");
    if (any ({ "echo", "delay" }))
        return pluginCategories().indexOf ("Delay");
    if (any ({ "tape" }))      return pluginCategories().indexOf ("Tape");
    if (any ({ "satur", "drive", "distort", "fuzz", "crush", "clip" }))
        return pluginCategories().indexOf ("Saturation");
    if (any ({ "chorus", "flang", "phaser", "tremolo", "vibrato", "rotary", "ensemble" }))
        return pluginCategories().indexOf ("Modulation");
    if (any ({ "pitch", "tune" }))
        return pluginCategories().indexOf ("Pitch");
    if (any ({ "comp", "gate", "expander", "transient" }))
        return pluginCategories().indexOf ("Compressor");
    if (any ({ "drum", "kick", "snare" }))
        return pluginCategories().indexOf ("Drum Tool");
    if (any ({ "bass", "808", "sub" }))
        return pluginCategories().indexOf ("Bass Tool");
    if (any ({ " eq", "-eq", "equaliz", "equalis" }))
        return pluginCategories().indexOf ("EQ");
    if (any ({ "gain", "util", "pan ", "meter" }))
        return pluginCategories().indexOf ("Utility");
    return -1;
}

// moduleinfo.json ships inside modern VST3 bundles (VST 3.7.5+) — plain JSON
// listing classes, sub-categories and vendor. Reading it is just a text read.
static void readVst3ModuleInfo (const juce::File& bundle, juce::String& vendorOut, int& categoryOut)
{
    for (const auto& rel : { "Contents/moduleinfo.json", "Contents/Resources/moduleinfo.json" })
    {
        auto f = bundle.getChildFile (rel);
        if (! f.existsAsFile()) continue;
        auto parsed = juce::JSON::parse (f.loadFileAsString());
        if (! parsed.isObject()) continue;

        auto factory = parsed.getProperty ("Factory Info", {});
        if (factory.isObject())
            vendorOut = factory.getProperty ("Vendor", "").toString().trim();

        auto classes = parsed.getProperty ("Classes", {});
        if (auto* arr = classes.getArray())
            for (const auto& cls : *arr)
            {
                if (cls.getProperty ("Category", "").toString() != "Audio Module Class")
                    continue;
                juce::String joined;
                auto subs = cls.getProperty ("Sub Categories", {});
                if (auto* subArr = subs.getArray())
                    for (const auto& sc : *subArr)
                        joined << sc.toString() << "|";
                const int cat = categoryFromVst3Subcategories (joined);
                if (cat >= 0) { categoryOut = cat; return; }
            }
        return;
    }
}

int CaseFileProcessor::scanPluginFolders()
{
    juce::Array<juce::File> folders;
    const auto userLib = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                             .getChildFile ("Library/Audio/Plug-Ins");
    folders.add (userLib.getChildFile ("VST3"));
    folders.add (userLib.getChildFile ("Components"));
    folders.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    folders.add (juce::File ("/Library/Audio/Plug-Ins/Components"));
    return scanPluginFolders (folders);
}

int CaseFileProcessor::scanPluginFolders (const juce::Array<juce::File>& foldersToScan)
{
    // names already on file (case-insensitive) — never duplicate an entry the
    // user may have annotated
    juce::StringArray known;
    for (auto p : pluginLib())
        known.add (p.getProperty (ids::name).toString().trim().toLowerCase());

    int added = 0;
    for (const auto& folder : foldersToScan)
    {
        if (! folder.isDirectory()) continue;

        juce::Array<juce::File> bundles;
        folder.findChildFiles (bundles, juce::File::findFilesAndDirectories, true, "*.vst3");
        folder.findChildFiles (bundles, juce::File::findFilesAndDirectories, true, "*.component");

        for (const auto& bundle : bundles)
        {
            const auto name = bundle.getFileNameWithoutExtension().trim();
            if (name.isEmpty() || known.contains (name.toLowerCase())) continue;
            known.add (name.toLowerCase());

            juce::String vendor;
            int cat = -1;
            if (bundle.hasFileExtension ("vst3"))
                readVst3ModuleInfo (bundle, vendor, cat);
            // vendors often nest bundles in a company subfolder — use it
            if (vendor.isEmpty() && bundle.getParentDirectory() != folder)
                vendor = bundle.getParentDirectory().getFileName();
            if (cat < 0) cat = categoryFromName (name);
            if (cat < 0) cat = pluginCategories().indexOf ("Other");

            juce::ValueTree p (ids::PluginItem);
            p.setProperty (ids::name, name, nullptr);
            p.setProperty (ids::company, vendor, nullptr);
            p.setProperty (ids::category, cat, nullptr);
            p.setProperty (ids::favorite, false, nullptr);
            p.setProperty (ids::ownership, 0, nullptr);
            p.setProperty (ids::notes, "", nullptr);
            p.setProperty (ids::bestUse, "", nullptr);
            p.setProperty (ids::cpuHeavy, false, nullptr);
            p.setProperty (ids::usedOften, false, nullptr);
            pluginLib().appendChild (p, nullptr);
            ++added;
        }
    }
    if (added > 0) sendChangeMessage();
    return added;
}

//==============================================================================
// gear photos
//==============================================================================
juce::File CaseFileProcessor::gearPhotosFolder()
{
    return caseFileFolder().getChildFile ("Gear Photos");
}

juce::ValueTree CaseFileProcessor::addHardwarePhoto (juce::ValueTree gearItem, const juce::File& image)
{
    if (! gearItem.isValid() || ! image.existsAsFile()) return {};

    auto photos = gearItem.getChildWithName (ids::Photos);
    if (! photos.isValid())
    {
        photos = juce::ValueTree (ids::Photos);
        gearItem.appendChild (photos, nullptr);
    }

    // archive a copy so recall photos survive the original moving/deleting
    auto sanitize = [] (juce::String s)
    {
        s = s.trim().replaceCharacters (" /\\:*?\"<>|", "__________");
        return s.isEmpty() ? juce::String ("Gear") : s;
    };
    auto folder = gearPhotosFolder();
    folder.createDirectory();
    auto target = folder.getChildFile (sanitize (gearItem.getProperty (ids::name).toString())
                                       + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S")
                                       + "_" + juce::String (photos.getNumChildren())
                                       + image.getFileExtension());
    const auto stored = image.copyFileTo (target) ? target : image;

    juce::ValueTree photo (ids::Photo);
    photo.setProperty (ids::path, stored.getFullPathName(), nullptr);
    photos.appendChild (photo, nullptr);
    sendChangeMessage();
    return photo;
}

juce::String CaseFileProcessor::hardwareCSV() const
{
    juce::String s ("Name,Brand,Type,Stereo/Mono,Channels,Insert Path,Favorite,Favorite Use,Notes,Recall Notes\n");
    for (auto h : hardware())
        s << csvCell (h.getProperty (ids::name).toString()) << ","
          << csvCell (h.getProperty (ids::brand).toString()) << ","
          << csvCell (hardwareTypes()[safeIndex (h.getProperty (ids::gearType), hardwareTypes())]) << ","
          << csvCell (stereoMonoTypes()[safeIndex (h.getProperty (ids::stereoMono), stereoMonoTypes())]) << ","
          << csvCell (h.getProperty (ids::numChannels).toString()) << ","
          << csvCell (h.getProperty (ids::insertPath).toString()) << ","
          << ((bool) h.getProperty (ids::favorite) ? "Yes" : "No") << ","
          << csvCell (h.getProperty (ids::favoriteUse).toString()) << ","
          << csvCell (h.getProperty (ids::notes).toString()) << ","
          << csvCell (h.getProperty (ids::recallNotes).toString()) << "\n";
    return s;
}

//==============================================================================
// analysis summary text
//==============================================================================
static juce::String dbStr (float v)      { return juce::String (v, 1) + " dB"; }
static juce::String pctStr (float v)     { return juce::String (v, 0) + "%"; }

juce::String CaseFileProcessor::buildAnalysisSummary() const
{
    const auto evid = evidence();
    auto curTree  = findEvidenceByRole (evid, 0);
    auto mainTree = findEvidenceByRole (evid, 1);
    if (! mainTree.isValid())
        for (auto e : evid)
            if ((int) e.getProperty (ids::role, 1) != 0
                  && (bool) e.getProperty (ids::analyzed, false))
                { mainTree = e; break; }

    juce::String s;
    s << "CASE ANALYSIS — MEASURED EVIDENCE\n";
    s << "=================================\n\n";

    if (! curTree.isValid())
        return s + "No analyzed Current Mix on file.\n\n"
                   "Evidence tab: import your current mix (role: Current Mix), add at least\n"
                   "one reference, then hit INVESTIGATE.\n";

    const auto cur = resultFromTree (curTree);

    auto describeFile = [] (const juce::ValueTree& t, const AnalysisResult& r)
    {
        juce::String d;
        d << t.getProperty (ids::name).toString() << "  ("
          << juce::String (r.lengthSec, 1) << "s, "
          << juce::String (r.sampleRate / 1000.0, 1) << "k, "
          << (r.channels == 1 ? "mono" : "stereo") << ")";
        return d;
    };

    s << "CURRENT MIX:  " << describeFile (curTree, cur) << "\n";

    if (! mainTree.isValid())
    {
        s << "\nNo analyzed reference yet — import one on the Evidence tab.\n\n";
        s << "Peak " << dbStr (cur.peakDb) << "   RMS " << dbStr (cur.rmsDb)
          << "   Crest " << dbStr (cur.crestDb) << "\n";
        s << "Width " << pctStr (cur.widthPct) << "   Low width (<120 Hz) "
          << pctStr (cur.lowWidthPct) << "   L/R corr " << juce::String (cur.corr, 2) << "\n";
        return s;
    }

    const auto ref = resultFromTree (mainTree);
    s << "REFERENCE:    " << describeFile (mainTree, ref) << "\n\n";

    s << "FREQUENCY BALANCE (band energy relative to whole mix)\n";
    s << juce::String ("BAND").paddedRight (' ', 20)
      << juce::String ("MIX").paddedRight (' ', 10)
      << juce::String ("REF").paddedRight (' ', 10) << "DELTA\n";
    s << juce::String::repeatedString ("-", 50) << "\n";
    for (int b = 0; b < numBands; ++b)
    {
        const float d = cur.bandRel[b] - ref.bandRel[b];
        s << bandNames()[b].paddedRight (' ', 20)
          << dbStr (cur.bandRel[b]).paddedRight (' ', 10)
          << dbStr (ref.bandRel[b]).paddedRight (' ', 10)
          << (d >= 0 ? "+" : "") << juce::String (d, 1) << " dB";
        if (std::abs (d) >= 4.0f)      s << "   << MAJOR";
        else if (std::abs (d) >= 2.5f) s << "   <  notable";
        s << "\n";
    }

    s << "\nLOUDNESS & DYNAMICS\n";
    s << juce::String::repeatedString ("-", 50) << "\n";
    s << juce::String ("Peak").paddedRight (' ', 20)   << dbStr (cur.peakDb).paddedRight (' ', 10)  << dbStr (ref.peakDb)  << "\n";
    s << juce::String ("RMS").paddedRight (' ', 20)    << dbStr (cur.rmsDb).paddedRight (' ', 10)   << dbStr (ref.rmsDb)   << "\n";
    s << juce::String ("Crest factor").paddedRight (' ', 20) << dbStr (cur.crestDb).paddedRight (' ', 10) << dbStr (ref.crestDb) << "\n";

    s << "\nSTEREO WIDTH\n";
    s << juce::String::repeatedString ("-", 50) << "\n";
    s << juce::String ("Overall width").paddedRight (' ', 20) << pctStr (cur.widthPct).paddedRight (' ', 10) << pctStr (ref.widthPct) << "\n";
    s << juce::String ("Low width <120Hz").paddedRight (' ', 20) << pctStr (cur.lowWidthPct).paddedRight (' ', 10) << pctStr (ref.lowWidthPct) << "\n";
    s << juce::String ("L/R correlation").paddedRight (' ', 20)
      << juce::String (cur.corr, 2).paddedRight (' ', 10) << juce::String (ref.corr, 2) << "\n";
    if (cur.corr < 0.35f)
        s << "!! Low correlation — check the mix in mono.\n";

    // role-tagged extras
    s << "\nREFERENCES ON FILE\n";
    s << juce::String::repeatedString ("-", 50) << "\n";
    for (auto e : evid)
        if ((int) e.getProperty (ids::role, 1) != 0)
            s << e.getProperty (ids::name).toString() << "  ["
              << evidenceRoles()[safeIndex (e.getProperty (ids::role), evidenceRoles())] << "]"
              << ((bool) e.getProperty (ids::analyzed, false) ? "" : "  (not analyzed)") << "\n";

    s << "\nRun INVESTIGATE to turn these numbers into suspect cards.\n";
    return s;
}

//==============================================================================
// report
//==============================================================================
juce::File CaseFileProcessor::caseFileFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Sound Detective").getChildFile ("Case File");
}

juce::String CaseFileProcessor::exportBaseName() const
{
    auto sanitize = [] (juce::String s)
    {
        s = s.trim().replaceCharacters (" /\\:*?\"<>|", "__________");
        return s.isEmpty() ? juce::String ("Untitled") : s;
    };
    return sanitize (brief().getProperty (ids::songTitle).toString()) + "_CaseFile_"
         + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
}

juce::String CaseFileProcessor::buildReport (bool md) const
{
    const auto h1  = md ? juce::String ("# ")   : juce::String();
    const auto h2  = md ? juce::String ("## ")  : juce::String();
    const auto h3  = md ? juce::String ("### ") : juce::String();
    const auto bul = md ? juce::String ("- ")   : juce::String ("  ");
    auto b   = brief();
    auto sm  = songMap();

    auto field = [&bul] (const juce::String& label, const juce::String& value) -> juce::String
    {
        if (value.trim().isEmpty()) return {};
        return bul + label + ": " + value.trim() + "\n";
    };
    auto block = [&h3] (const juce::String& label, const juce::String& value) -> juce::String
    {
        if (value.trim().isEmpty()) return {};
        return "\n" + h3 + label + "\n" + value.trim() + "\n";
    };
    auto divider = [md] { return md ? juce::String() : juce::String::repeatedString ("=", 46) + "\n"; };

    juce::String s;
    s << h1 << "SOUND DETECTIVE // CASE FILE\n" << divider();
    s << bul << "Case No. " << state.getProperty (ids::caseNumber).toString()
      << "  •  opened " << juce::Time ((juce::int64) state.getProperty (ids::openedAt, 0)).formatted ("%b %d %Y")
      << "  •  report filed " << juce::Time::getCurrentTime().formatted ("%b %d %Y %H:%M") << "\n";
    s << bul << "Detective 47 — Open the case. Find the suspects. Close the mix.\n";

    s << "\n" << h2 << "CASE BRIEF\n";
    s << field ("Song",     b.getProperty (ids::songTitle).toString());
    s << field ("Artist",   b.getProperty (ids::artist).toString());
    s << field ("Producer", b.getProperty (ids::producer).toString());
    s << field ("Mixer",    b.getProperty (ids::mixer).toString());
    s << field ("Genre",    b.getProperty (ids::genre).toString());
    s << field ("Tempo",    b.getProperty (ids::tempo).toString());
    s << field ("Key",      b.getProperty (ids::songKey).toString());
    s << field ("Format",   (b.getProperty (ids::sampleRateTxt).toString()
                              + " / " + b.getProperty (ids::bitDepthTxt).toString())
                              .trimCharactersAtStart (" /").trimCharactersAtEnd (" /"));
    s << bul << "Mix Stage: " << mixStages()[safeIndex (b.getProperty (ids::mixStage), mixStages())] << "\n";
    s << field ("Deadline",         b.getProperty (ids::deadline).toString());
    s << field ("Main References",  b.getProperty (ids::mainRefs).toString());
    s << field ("Emotional Target", b.getProperty (ids::emotionalTarget).toString());
    s << field ("Mix Goal",         b.getProperty (ids::mixGoal).toString());
    s << field ("Biggest Problem",  b.getProperty (ids::biggestProblem).toString());
    s << block ("Client Notes",           b.getProperty (ids::clientNotes).toString());
    s << block ("Delivery Requirements",  b.getProperty (ids::deliveryReqs).toString());

    // song map
    {
        juce::String m;
        m << field ("Key",      (sm.getProperty (ids::songKey).toString() + " "
                                 + sm.getProperty (ids::scaleMode).toString()).trim());
        m << field ("Tempo",    sm.getProperty (ids::tempo).toString());
        m << field ("Time Sig", sm.getProperty (ids::timeSig).toString());
        m << field ("Main Hook", sm.getProperty (ids::mainHook).toString());
        m << field ("Low-End Relationship", sm.getProperty (ids::lowEndRel).toString());
        m << block ("Chord Progression", sm.getProperty (ids::chordProgression).toString());

        juce::String secs;
        for (auto sec : sections())
        {
            secs << bul << sec.getProperty (ids::name).toString();
            const auto t0 = sec.getProperty (ids::startTime).toString().trim();
            const auto t1 = sec.getProperty (ids::endTime).toString().trim();
            const auto b0 = sec.getProperty (ids::startBar).toString().trim();
            const auto b1 = sec.getProperty (ids::endBar).toString().trim();
            if (t0.isNotEmpty() || t1.isNotEmpty()) secs << "  [" << t0 << " - " << t1 << "]";
            if (b0.isNotEmpty() || b1.isNotEmpty()) secs << "  bars " << b0 << "-" << b1;
            secs << "  energy: " << energyLevels()[safeIndex (sec.getProperty (ids::energy), energyLevels())] << "\n";
            auto sub = [&] (const juce::String& label, const juce::Identifier& p)
            {
                const auto v = sec.getProperty (p).toString().trim();
                if (v.isNotEmpty()) secs << "    " << label << ": " << v << "\n";
            };
            sub ("Chords",   ids::chords);
            sub ("Mix goal", ids::sectionMixGoal);
            sub ("Production goal", ids::prodGoal);
            sub ("Notes",    ids::notes);
        }
        if (secs.isNotEmpty()) m << "\n" << h3 << "Section Map\n" << secs;

        const auto notes = sm.getProperty (ids::songNotes).toString();
        m << block ("Song Notes", notes);
        if (m.isNotEmpty()) s << "\n" << h2 << "SONG MAP / MUSICALITY\n" << m;
    }

    // evidence
    if (evidence().getNumChildren() > 0)
    {
        s << "\n" << h2 << "EVIDENCE\n";
        for (auto e : evidence())
        {
            s << bul << e.getProperty (ids::name).toString()
              << "  [" << evidenceRoles()[safeIndex (e.getProperty (ids::role), evidenceRoles())] << "]";
            if ((bool) e.getProperty (ids::analyzed, false))
                s << "  peak " << dbStr (e.getProperty (ids::peakDb))
                  << ", RMS " << dbStr (e.getProperty (ids::rmsDb))
                  << ", crest " << dbStr (e.getProperty (ids::crestDb))
                  << ", width " << pctStr (e.getProperty (ids::widthPct));
            s << "\n";
        }
        s << "\n" << h2 << "CURRENT MIX ANALYSIS\n";
        if (md) s << "```\n";
        s << buildAnalysisSummary();
        if (md) s << "```\n";
    }

    // suspects
    if (suspects().getNumChildren() > 0)
    {
        s << "\n" << h2 << "SUSPECT AREAS\n";
        for (auto sus : suspects())
        {
            s << "\n" << h3 << (((bool) sus.getProperty (ids::solved)) ? "[SOLVED] " : "")
              << sus.getProperty (ids::title).toString()
              << "  (" << severityNames()[safeIndex (sus.getProperty (ids::severity), severityNames())]
              << ")\n";
            s << field ("Range",            sus.getProperty (ids::range).toString());
            s << field ("Possible Sources", sus.getProperty (ids::sources).toString());
            s << field ("Why It Matters",   sus.getProperty (ids::why).toString());
            const auto acts = sus.getProperty (ids::actions).toString().trim();
            if (acts.isNotEmpty()) s << bul << "Suggested Actions:\n"
                                     << (md ? acts : "      " + acts.replace ("\n", "\n      ")) << "\n";
        }
    }

    // checklist
    if (checklist().getNumChildren() > 0)
    {
        s << "\n" << h2 << "ACTION PLAN / CHECKLIST\n";
        for (int g = 0; g < checklistGroups().size(); ++g)
        {
            juce::String items;
            for (auto c : checklist())
            {
                if ((int) c.getProperty (ids::group, 3) != g) continue;
                items << bul << ((bool) c.getProperty (ids::done) ? "[x] " : "[ ] ")
                      << c.getProperty (ids::text).toString();
                const auto sec = c.getProperty (ids::section).toString().trim();
                if (sec.isNotEmpty()) items << "  (" << sec << ")";
                items << "\n";
            }
            if (items.isNotEmpty())
                s << "\n" << h3 << checklistGroups()[g] << "\n" << items;
        }
    }

    // plugin library / hardware — short summaries only, the full lists live in CSV
    if (pluginLib().getNumChildren() > 0)
    {
        juce::String favs;
        for (auto p : pluginLib())
            if ((bool) p.getProperty (ids::favorite))
                favs << bul << p.getProperty (ids::name).toString()
                     << " (" << pluginCategories()[safeIndex (p.getProperty (ids::category), pluginCategories())] << ")\n";
        s << "\n" << h2 << "PLUGIN LIBRARY\n"
          << bul << juce::String (pluginLib().getNumChildren()) << " plugins on file\n";
        if (favs.isNotEmpty()) s << h3 << "Favorites\n" << favs;
    }
    if (hardware().getNumChildren() > 0)
    {
        s << "\n" << h2 << "HARDWARE LOCKER\n"
          << bul << juce::String (hardware().getNumChildren()) << " pieces on file\n";
        for (auto hw : hardware())
            if ((bool) hw.getProperty (ids::favorite))
                s << bul << hw.getProperty (ids::name).toString()
                  << " (" << hardwareTypes()[safeIndex (hw.getProperty (ids::gearType), hardwareTypes())] << ")\n";
    }

    // chains
    if (chains().getNumChildren() > 0)
    {
        s << "\n" << h2 << "CHAINS / RECALL\n";
        for (auto c : chains())
        {
            s << "\n" << h3 << c.getProperty (ids::trackName).toString() << "\n";
            auto add = [&] (const juce::String& label, const juce::Identifier& p)
            { s << field (label, c.getProperty (p).toString()); };
            add ("Mic",          ids::mic);
            add ("Preamp",       ids::preamp);
            add ("Compressor",   ids::compressor);
            add ("EQ",           ids::eq);
            add ("De-esser",     ids::deesser);
            add ("Plugin Chain", ids::pluginChain);
            add ("Outboard",     ids::outboardChain);
            add ("FX Sends",     ids::fxSends);
            add ("Main Problem", ids::mainProblem);
            add ("Plan",         ids::plan);
            add ("Notes",        ids::notes);
        }
    }

    // versions
    if (versions().getNumChildren() > 0)
    {
        s << "\n" << h2 << "REVISION LOG\n";
        for (auto v : versions())
        {
            s << "\n" << h3 << v.getProperty (ids::name).toString()
              << "  (" << juce::Time ((juce::int64) v.getProperty (ids::dateMs, 0)).formatted ("%b %d %Y") << ")\n";
            s << block ("Notes",             v.getProperty (ids::notes).toString());
            s << block ("Client Feedback",   v.getProperty (ids::clientFeedback).toString());
            s << block ("Changes Made",      v.getProperty (ids::changes).toString());
            s << block ("Problems Remaining", v.getProperty (ids::problems).toString());
            s << field ("Printed Files",     v.getProperty (ids::printedFiles).toString());
        }
    }

    const auto fin = report().getProperty (ids::finalDeliveryNotes).toString();
    if (fin.trim().isNotEmpty())
        s << "\n" << h2 << "FINAL DELIVERY NOTES\n" << fin.trim() << "\n";

    s << "\n" << (md ? "---\n*" : "") << "Case File — Sound Detective / Detective 47"
      << (md ? "*" : "") << "\n";
    return s;
}

//==============================================================================
// AI-ready JSON packet — the whole ValueTree, verbatim
//==============================================================================
static juce::var treeToVar (const juce::ValueTree& t)
{
    auto* o = new juce::DynamicObject();
    for (int i = 0; i < t.getNumProperties(); ++i)
    {
        const auto name = t.getPropertyName (i);
        o->setProperty (name, t.getProperty (name));
    }
    if (t.getNumChildren() > 0)
    {
        juce::Array<juce::var> kids;
        for (auto c : t)
        {
            auto* wrap = new juce::DynamicObject();
            wrap->setProperty ("type", c.getType().toString());
            wrap->setProperty ("data", treeToVar (c));
            kids.add (juce::var (wrap));
        }
        o->setProperty ("children", kids);
    }
    return juce::var (o);
}

juce::String CaseFileProcessor::buildJSON() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("caseFileVersion", 1);
    root->setProperty ("exportedAt", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("case", treeToVar (state));
    return juce::JSON::toString (juce::var (root));
}

//==============================================================================
// audio passthrough & session state
//==============================================================================
bool CaseFileProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CaseFileProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Pure passthrough — input channels already hold the output. Just silence
    // any extra output channels the host may have handed us.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void CaseFileProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (stateLock);
    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void CaseFileProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! t.hasType (ids::CaseFile)) return;
    {
        const juce::ScopedLock sl (stateLock);
        state = t;
    }
    ensureStructure();
    sendChangeMessage();
}

juce::AudioProcessorEditor* CaseFileProcessor::createEditor()
{
#if CASEFILE_NO_EDITOR
    return nullptr;
#else
    return new CaseFileEditor (*this);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CaseFileProcessor();
}
