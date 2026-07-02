// Headless logic test for Case File — drives the real processor with no GUI:
// state round-trip through the DAW-session path, checklist seeding, the
// analysis engine on synthetic signals, the suspect rules, suspect→checklist,
// plugin library import/export, and the report/JSON builders.
#include <JuceHeader.h>
#include "PluginProcessor.h"

using namespace casefile;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static bool near (float a, float b, float tol) { return std::abs (a - b) <= tol; }

// render a test signal to a wav so the full file-import path gets exercised
static juce::File writeWav (const juce::File& folder, const juce::String& name,
                            const juce::AudioBuffer<float>& buf, double sr)
{
    auto f = folder.getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (f), sr,
                             (unsigned int) buf.getNumChannels(), 24, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    return f;
}

static juce::AudioBuffer<float> makeSine (double freq, double sr, int seconds,
                                          float amp, bool antiPhase = false)
{
    juce::AudioBuffer<float> buf (2, (int) (sr * seconds));
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr);
        buf.setSample (0, i, v);
        buf.setSample (1, i, antiPhase ? -v : v);
    }
    return buf;
}

// noise with energy across all bands, identical L/R
static juce::AudioBuffer<float> makeNoise (double sr, int seconds, float amp)
{
    juce::AudioBuffer<float> buf (2, (int) (sr * seconds));
    juce::Random rnd (47);
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float v = amp * (rnd.nextFloat() * 2.0f - 1.0f);
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
    return buf;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    //=== analysis engine on synthetic signals ==================================
    {
        const double sr = 48000.0;
        auto sine = makeSine (100.0, sr, 4, 0.5f);           // 100 Hz, -6 dB peak
        auto res = analyzeBuffer (sine, sr);
        check (res.valid, "analyzer produces a result");
        check (near (res.peakDb, -6.0f, 0.3f),  "sine peak ~-6 dB  (" + juce::String (res.peakDb, 2) + ")");
        check (near (res.rmsDb,  -9.0f, 0.3f),  "sine RMS ~-9 dB   (" + juce::String (res.rmsDb, 2) + ")");
        check (near (res.crestDb, 3.0f, 0.4f),  "sine crest ~3 dB  (" + juce::String (res.crestDb, 2) + ")");
        check (res.corr > 0.99f,                "identical L/R -> corr ~1");
        check (res.widthPct < 1.0f,             "identical L/R -> width ~0%");
        // 100 Hz belongs to the Bass 60-120 band and should dominate
        int biggest = 0;
        for (int b = 1; b < numBands; ++b)
            if (res.bandRel[b] > res.bandRel[biggest]) biggest = b;
        check (biggest == 1, "100 Hz energy lands in Bass 60-120 band");

        auto anti = analyzeBuffer (makeSine (80.0, sr, 4, 0.5f, true), sr);
        check (anti.corr < -0.99f,        "anti-phase -> corr ~-1");
        check (anti.widthPct > 99.0f,     "anti-phase -> width ~100%");
        check (anti.lowWidthPct > 99.0f,  "anti-phase 80 Hz -> low width ~100%");
    }

    //=== suspect rules on crafted results ======================================
    {
        AnalysisResult cur, ref;
        cur.valid = ref.valid = true;
        for (int b = 0; b < numBands; ++b) { cur.bandRel[b] = -10.0f; ref.bandRel[b] = -10.0f; }
        cur.corr = ref.corr = 0.9f;
        cur.crestDb = ref.crestDb = 10.0f;
        cur.rmsDb = ref.rmsDb = -12.0f;

        RefSet refs; refs.main = &ref;
        check (runSuspectRules (cur, refs).isEmpty(), "identical mixes -> no suspects");

        cur.bandRel[2] = -5.5f;                       // +4.5 dB low mids
        auto out = runSuspectRules (cur, refs);
        check (out.size() == 1 && out[0].title == "Low-Mid Buildup",
               "low-mid delta fires Low-Mid Buildup");
        check (out[0].severity == 2, "4.5 dB delta -> High severity");
        cur.bandRel[2] = -10.0f;

        cur.bandRel[4] = -13.0f;                      // -3 dB presence
        out = runSuspectRules (cur, refs);
        check (out.size() == 1 && out[0].title == "Presence / Vocal Forwardness",
               "presence deficit fires vocal card");
        cur.bandRel[4] = -10.0f;

        cur.rmsDb = -8.0f;  cur.crestDb = 6.0f;       // louder + squashed
        out = runSuspectRules (cur, refs);
        check (out.size() == 1 && out[0].title == "Over-Compression / Reduced Punch",
               "louder + lower crest fires over-compression");
        cur.rmsDb = -12.0f; cur.crestDb = 10.0f;

        cur.lowWidthPct = 40.0f; ref.lowWidthPct = 5.0f;
        out = runSuspectRules (cur, refs);
        check (out.size() == 1 && out[0].title == "Low-End Width Instability",
               "wide low end fires width card");
        cur.lowWidthPct = 0.0f; ref.lowWidthPct = 0.0f;

        cur.corr = 0.1f;
        out = runSuspectRules (cur, refs);
        check (out.size() == 1 && out[0].title == "Mono Compatibility Risk",
               "low correlation fires mono card");
    }

    //=== full case: evidence files -> investigate -> suspects ==================
    CaseFileProcessor p;
    {
        check (p.checklist().getNumChildren() == 36, "standard checklists seeded (36 items)");

        auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("CaseFileLogicTest");
        temp.createDirectory();
        const double sr = 44100.0;

        // current mix: pure 250 Hz (all energy in low mids, nothing anywhere else)
        auto curFile = writeWav (temp, "current.wav", makeSine (250.0, sr, 3, 0.4f), sr);
        // reference: broadband noise
        auto refFile = writeWav (temp, "reference.wav", makeNoise (sr, 3, 0.3f), sr);

        auto curItem = p.addEvidence (curFile, 0);    // Current Mix
        auto refItem = p.addEvidence (refFile, 1);    // Main Sonic Target
        check (p.evidence().getNumChildren() == 2, "evidence filed");

        const int n = p.investigate();
        check (n > 0, "investigate generates suspects (" + juce::String (n) + ")");
        check ((bool) curItem.getProperty (ids::analyzed, false)
                 && (bool) refItem.getProperty (ids::analyzed, false),
               "investigate analyzes pending evidence");

        bool foundLowMid = false;
        for (auto s : p.suspects())
            if (s.getProperty (ids::title).toString() == "Low-Mid Buildup")
                foundLowMid = true;
        check (foundLowMid, "250 Hz mix vs noise ref -> Low-Mid Buildup card");

        // solved + custom survive re-investigation, stale auto cards don't pile up
        auto first = p.suspects().getChild (0);
        first.setProperty (ids::solved, true, nullptr);
        SuspectDef d; d.title = "My Own Hunch"; d.severity = 0;
        p.addSuspect (d, true);
        const int before = p.suspects().getNumChildren();
        p.investigate();
        check (p.suspects().getNumChildren() == before, "re-investigate doesn't duplicate cards");
        bool solvedKept = false, customKept = false;
        for (auto s : p.suspects())
        {
            if ((bool) s.getProperty (ids::solved)) solvedKept = true;
            if (s.getProperty (ids::title).toString() == "My Own Hunch") customKept = true;
        }
        check (solvedKept && customKept, "solved + custom suspects survive re-investigation");

        // suspect -> checklist
        const int itemsBefore = p.checklist().getNumChildren();
        auto ci = p.suspectToChecklistItem (p.suspects().getChild (0));
        check (p.checklist().getNumChildren() == itemsBefore + 1
                 && ci.getProperty (ids::text).toString().startsWith ("Investigate:"),
               "suspect card becomes checklist item");

        temp.deleteRecursively();
    }

    //=== brief / song map / library / chains / versions =========================
    {
        p.brief().setProperty (ids::songTitle, "Midnight Alibi", nullptr);
        p.brief().setProperty (ids::artist, "The Regulars", nullptr);
        p.brief().setProperty (ids::mixGoal, "Wide hook, intimate verses", nullptr);

        auto sec = p.addSection ("Chorus 1");
        sec.setProperty (ids::startTime, "1:02", nullptr);
        sec.setProperty (ids::startBar, "33", nullptr);
        sec.setProperty (ids::sectionMixGoal, "Open the width", nullptr);
        p.songMap().setProperty (ids::chordProgression, "Am - F - C - G", nullptr);

        check (p.bulkAddPlugins ("Pro-Q 3, FabFilter, EQ\nCLA-2A, Waves, Compressor") == 2,
               "bulk paste adds 2 plugins");
        check (p.pluginLibCSV().contains ("\"Pro-Q 3\",\"FabFilter\",\"EQ\""),
               "plugin CSV round-trips name/company/category");
        check (p.importPluginCSV ("Name,Company,Category\nDecapitator, Soundtoys, Saturation") == 1,
               "CSV import skips header and adds row");

        auto hw = p.addChildTo (p.hardware(), ids::HardwareItem);
        hw.setProperty (ids::name, "Distressor", nullptr);
        hw.setProperty (ids::gearType, 2, nullptr);
        hw.setProperty (ids::favorite, true, nullptr);
        check (p.hardwareCSV().contains ("\"Distressor\""), "hardware CSV export");

        auto chain = p.addChain (0);                 // Lead Vocal template
        check (chain.getProperty (ids::trackName).toString() == "Lead Vocal",
               "chain template pre-fills track name");
        chain.setProperty (ids::compressor, "1176 fast attack", nullptr);

        auto v = p.addVersion ("Mix 1");
        v.setProperty (ids::changes, "Lowered vocal 0.7 dB", nullptr);
    }

    //=== plugin folder scan (safe, file reads only) =============================
    {
        check (categoryFromVst3Subcategories ("Fx|Dynamics") == pluginCategories().indexOf ("Compressor"),
               "VST3 Fx|Dynamics -> Compressor");
        check (categoryFromVst3Subcategories ("Fx|EQ") == pluginCategories().indexOf ("EQ"),
               "VST3 Fx|EQ -> EQ");
        check (categoryFromVst3Subcategories ("Fx|Reverb") == pluginCategories().indexOf ("Reverb"),
               "VST3 Fx|Reverb -> Reverb");
        check (categoryFromName ("Valhalla VintageVerb") == pluginCategories().indexOf ("Reverb"),
               "name heuristic: VintageVerb -> Reverb");
        check (categoryFromName ("EchoBoy") == pluginCategories().indexOf ("Delay"),
               "name heuristic: EchoBoy -> Delay");
        check (categoryFromName ("SSL Bus Compressor") == pluginCategories().indexOf ("Compressor"),
               "name heuristic: Bus Compressor -> Compressor");

        // fake plugin folder: one VST3 with moduleinfo.json, one bare AU bundle
        auto scanDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("CaseFileScanTest");
        scanDir.deleteRecursively();
        auto v3 = scanDir.getChildFile ("TestVendor/Cool Space.vst3/Contents");
        v3.createDirectory();
        v3.getChildFile ("moduleinfo.json").replaceWithText (
            R"({ "Name": "Cool Space",
                 "Factory Info": { "Vendor": "Detective Labs" },
                 "Classes": [ { "Name": "Cool Space", "Category": "Audio Module Class",
                                "Sub Categories": [ "Fx", "Reverb" ] } ] })");
        scanDir.getChildFile ("Tape Crusher.component/Contents").createDirectory();

        const int found = p.scanPluginFolders ({ scanDir });
        check (found == 2, "scan finds 2 bundles (" + juce::String (found) + ")");

        juce::ValueTree coolSpace, crusher;
        for (auto pl : p.pluginLib())
        {
            if (pl.getProperty (ids::name).toString() == "Cool Space")   coolSpace = pl;
            if (pl.getProperty (ids::name).toString() == "Tape Crusher") crusher   = pl;
        }
        check (coolSpace.isValid()
                 && (int) coolSpace.getProperty (ids::category) == pluginCategories().indexOf ("Reverb")
                 && coolSpace.getProperty (ids::company).toString() == "Detective Labs",
               "scan reads category + vendor from moduleinfo.json");
        check (crusher.isValid()
                 && (int) crusher.getProperty (ids::category) == pluginCategories().indexOf ("Tape"),
               "scan falls back to name heuristics for bare bundles");
        check (p.scanPluginFolders ({ scanDir }) == 0, "rescan adds no duplicates");
        scanDir.deleteRecursively();
    }

    //=== hardware gear photos ===================================================
    {
        auto gear = p.hardware().getChild (0);
        auto tempImg = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("casefile_gear_test.png");
        juce::Image img (juce::Image::RGB, 8, 8, true);
        juce::PNGImageFormat png;
        { juce::FileOutputStream os (tempImg); png.writeImageToStream (img, os); }

        auto photo = p.addHardwarePhoto (gear, tempImg);
        check (photo.isValid()
                 && juce::File (photo.getProperty (ids::path).toString()).existsAsFile(),
               "gear photo archived and on file");
        check (gear.getChildWithName (ids::Photos).getNumChildren() == 1,
               "photo attached to gear item");
        tempImg.deleteFile();
        juce::File (photo.getProperty (ids::path).toString()).deleteFile();
    }

    //=== report & JSON ==========================================================
    {
        const auto rep = p.buildReport (false);
        check (rep.contains ("Midnight Alibi"),      "report carries song title");
        check (rep.contains ("CASE BRIEF"),          "report has brief section");
        check (rep.contains ("Chorus 1"),            "report carries section map");
        check (rep.contains ("Am - F - C - G"),      "report carries chord progression");
        check (rep.contains ("SUSPECT AREAS"),       "report has suspects section");
        check (rep.contains ("Lead Vocal"),          "report carries chains");
        check (rep.contains ("Mix 1"),               "report carries versions");
        check (rep.contains ("CASE NO.") || rep.contains ("Case No."), "report carries case number");

        const auto md = p.buildReport (true);
        check (md.startsWith ("# "), "markdown report uses headings");

        auto parsed = juce::JSON::parse (p.buildJSON());
        check (parsed.isObject()
                 && (int) parsed.getProperty ("caseFileVersion", 0) == 1,
               "JSON case packet parses");
    }

    //=== DAW-session save / reload =============================================
    {
        juce::MemoryBlock blob;
        p.getStateInformation (blob);

        CaseFileProcessor q;
        q.setStateInformation (blob.getData(), (int) blob.getSize());

        check (q.brief().getProperty (ids::songTitle).toString() == "Midnight Alibi",
               "restore: brief survives");
        check (q.sections().getNumChildren() == p.sections().getNumChildren()
                 && q.sections().getChild (0).getProperty (ids::startTime).toString() == "1:02",
               "restore: song map sections survive");
        check (q.pluginLib().getNumChildren() == p.pluginLib().getNumChildren(),
               "restore: plugin library survives");
        check (q.hardware().getNumChildren() == 1, "restore: hardware locker survives");
        check (q.hardware().getChild (0).getChildWithName (ids::Photos).getNumChildren() == 1,
               "restore: gear photo paths survive");
        check (q.suspects().getNumChildren() == p.suspects().getNumChildren(),
               "restore: suspect cards survive");
        check (q.chains().getNumChildren() == 1 && q.versions().getNumChildren() == 1,
               "restore: chains + versions survive");
        check (q.checklist().getNumChildren() == p.checklist().getNumChildren(),
               "restore: checklist not re-seeded on reload");
        check (q.state.getProperty (ids::caseNumber) == p.state.getProperty (ids::caseNumber),
               "restore: case number stable");

        // evidence analysis numbers ride along, no re-analysis needed
        auto e0 = q.evidence().getChild (0);
        check ((bool) e0.getProperty (ids::analyzed, false)
                 && CaseFileProcessor::resultFromTree (e0).valid,
               "restore: evidence analysis survives");
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " FAILURES")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
