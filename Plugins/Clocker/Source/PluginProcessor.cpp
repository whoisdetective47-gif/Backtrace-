#include "PluginProcessor.h"
#if ! CLOCKER_NO_EDITOR
 #include "PluginEditor.h"
#endif

using namespace clocker;

//==============================================================================
// shared lists & formatting
//==============================================================================
const juce::StringArray& clocker::sessionTypes()
{
    static const juce::StringArray t {
        "Production", "Recording", "Editing", "Vocal Production", "Mixing",
        "Mastering", "Revision", "Recall", "Printing / Delivery", "Admin",
        "Troubleshooting", "Break", "Other"
    };
    return t;
}

const juce::StringArray& clocker::projectTypes()
{
    static const juce::StringArray t { "Hourly", "Flat Rate", "Hybrid", "Internal / Non-Billable" };
    return t;
}

juce::String clocker::formatClock (juce::int64 ms)
{
    auto totalSec = juce::jmax ((juce::int64) 0, ms) / 1000;
    return juce::String::formatted ("%02d:%02d:%02d",
                                    (int) (totalSec / 3600),
                                    (int) ((totalSec / 60) % 60),
                                    (int) (totalSec % 60));
}

juce::String clocker::formatDuration (juce::int64 ms)
{
    auto totalMin = juce::jmax ((juce::int64) 0, ms) / 60000;
    return juce::String ((int) (totalMin / 60)) + "h "
         + juce::String ((int) (totalMin % 60)).paddedLeft ('0', 2) + "m";
}

juce::String clocker::formatHours (juce::int64 ms)
{
    return juce::String (ms / 3600000.0, 2) + " h";
}

juce::String clocker::formatMoney (double amount)
{
    const bool neg = amount < 0.0;
    auto cents = (juce::int64) std::llround (std::abs (amount) * 100.0);
    auto whole = juce::String (cents / 100);
    // insert thousands separators
    for (int i = whole.length() - 3; i > 0; i -= 3)
        whole = whole.substring (0, i) + "," + whole.substring (i);
    return (neg ? "-$" : "$") + whole + "." + juce::String (cents % 100).paddedLeft ('0', 2);
}

juce::String clocker::formatDate (juce::int64 epochMs)
{
    return juce::Time (epochMs).formatted ("%b %d %H:%M");
}

//==============================================================================
ClockerProcessor::ClockerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    ensureStructure();
}

void ClockerProcessor::ensureStructure()
{
    auto getOrAdd = [this] (const juce::Identifier& id) -> juce::ValueTree
    {
        auto c = state.getChildWithName (id);
        if (! c.isValid()) { c = juce::ValueTree (id); state.appendChild (c, nullptr); }
        return c;
    };

    auto proj = getOrAdd (ids::Project);
    auto setIfMissing = [] (juce::ValueTree t, const juce::Identifier& p, const juce::var& v)
    { if (! t.hasProperty (p)) t.setProperty (p, v, nullptr); };

    setIfMissing (proj, ids::client,       "");
    setIfMissing (proj, ids::project,      "");
    setIfMissing (proj, ids::song,         "");
    setIfMissing (proj, ids::engineer,     "");
    setIfMissing (proj, ids::projectType,  0);
    setIfMissing (proj, ids::hourlyRate,   100.0);
    setIfMissing (proj, ids::flatFee,      0.0);
    setIfMissing (proj, ids::billingNotes, "");
    setIfMissing (proj, ids::invoiceNotes, "");

    auto set = getOrAdd (ids::Settings);
    setIfMissing (set, ids::defaultRate,     100.0);
    setIfMissing (set, ids::defaultBillable, true);
    setIfMissing (set, ids::defaultType,     4);      // Mixing
    setIfMissing (set, ids::rounding,        0);      // exact
    setIfMissing (set, ids::timeFormat,      0);      // hh:mm:ss

    getOrAdd (ids::Entries);
}

//==============================================================================
// timer engine — pure timestamp math, nothing runs in the background
//==============================================================================
ClockerProcessor::ClockState ClockerProcessor::clockState() const
{
    auto a = activeTree();
    if (! a.isValid())                      return ClockState::idle;
    return (bool) a.getProperty (ids::running) ? ClockState::running : ClockState::paused;
}

juce::int64 ClockerProcessor::elapsedMs() const
{
    auto a = activeTree();
    if (! a.isValid()) return 0;
    juce::int64 acc = a.getProperty (ids::accumulated);
    if ((bool) a.getProperty (ids::running))
        acc += juce::jmax ((juce::int64) 0,
                           juce::Time::currentTimeMillis() - (juce::int64) a.getProperty (ids::lastResume));
    return acc;
}

void ClockerProcessor::clockIn()
{
    if (activeTree().isValid()) return;
    const auto now = juce::Time::currentTimeMillis();
    juce::ValueTree a (ids::Active);
    a.setProperty (ids::clockIn,     now, nullptr);
    a.setProperty (ids::accumulated, (juce::int64) 0, nullptr);
    a.setProperty (ids::running,     true, nullptr);
    a.setProperty (ids::lastResume,  now, nullptr);
    a.setProperty (ids::billable,    settings().getProperty (ids::defaultBillable, true), nullptr);
    a.setProperty (ids::type,        settings().getProperty (ids::defaultType, 4), nullptr);
    state.appendChild (a, nullptr);
    sendChangeMessage();
}

void ClockerProcessor::pauseTimer()
{
    auto a = activeTree();
    if (! a.isValid() || ! (bool) a.getProperty (ids::running)) return;
    const auto now = juce::Time::currentTimeMillis();
    a.setProperty (ids::accumulated,
                   (juce::int64) a.getProperty (ids::accumulated)
                       + juce::jmax ((juce::int64) 0, now - (juce::int64) a.getProperty (ids::lastResume)),
                   nullptr);
    a.setProperty (ids::running, false, nullptr);
    sendChangeMessage();
}

void ClockerProcessor::resumeTimer()
{
    auto a = activeTree();
    if (! a.isValid() || (bool) a.getProperty (ids::running)) return;
    a.setProperty (ids::lastResume, juce::Time::currentTimeMillis(), nullptr);
    a.setProperty (ids::running, true, nullptr);
    sendChangeMessage();
}

void ClockerProcessor::clockOut (const juce::String& notes)
{
    auto a = activeTree();
    if (! a.isValid()) return;
    const auto total = elapsedMs();
    const auto now   = juce::Time::currentTimeMillis();
    if (total >= 1000)  // ignore sub-second misclicks
        appendEntry (a.getProperty (ids::clockIn), now, total,
                     (bool) a.getProperty (ids::billable),
                     (int) a.getProperty (ids::type), notes, false);
    state.removeChild (a, nullptr);
    sendChangeMessage();
}

void ClockerProcessor::discardActive()
{
    auto a = activeTree();
    if (a.isValid()) { state.removeChild (a, nullptr); sendChangeMessage(); }
}

bool ClockerProcessor::activeBillable() const  { return (bool) activeTree().getProperty (ids::billable, true); }
int  ClockerProcessor::activeType() const      { return (int)  activeTree().getProperty (ids::type, (int) settings().getProperty (ids::defaultType, 4)); }

void ClockerProcessor::setActiveBillable (bool b)
{
    auto a = activeTree();
    if (a.isValid()) a.setProperty (ids::billable, b, nullptr);
    else settings().setProperty (ids::defaultBillable, b, nullptr);
}

void ClockerProcessor::setActiveType (int t)
{
    auto a = activeTree();
    if (a.isValid()) a.setProperty (ids::type, t, nullptr);
    else settings().setProperty (ids::defaultType, t, nullptr);
}

void ClockerProcessor::addManualEntry (juce::int64 durMs, bool billable, int type,
                                       const juce::String& notes)
{
    if (durMs <= 0) return;
    const auto now = juce::Time::currentTimeMillis();
    appendEntry (now - durMs, now, durMs, billable, type, notes, true);
    sendChangeMessage();
}

void ClockerProcessor::appendEntry (juce::int64 start, juce::int64 end, juce::int64 durMs,
                                    bool billable, int type, const juce::String& notes, bool manual)
{
    juce::ValueTree e (ids::Entry);
    e.setProperty (ids::start,      start,    nullptr);
    e.setProperty (ids::end,        end,      nullptr);
    e.setProperty (ids::durationMs, durMs,    nullptr);
    e.setProperty (ids::billable,   billable, nullptr);
    e.setProperty (ids::type,       type,     nullptr);
    e.setProperty (ids::notes,      notes,    nullptr);
    e.setProperty (ids::manual,     manual,   nullptr);
    entries().appendChild (e, nullptr);
}

void ClockerProcessor::resetProject()
{
    discardActive();
    auto ent = entries();
    ent.removeAllChildren (nullptr);
    auto proj = project();
    for (auto* p : { &ids::client, &ids::project, &ids::song,
                     &ids::billingNotes, &ids::invoiceNotes })
        proj.setProperty (*p, "", nullptr);
    proj.setProperty (ids::flatFee, 0.0, nullptr);
    proj.setProperty (ids::hourlyRate, settings().getProperty (ids::defaultRate, 100.0), nullptr);
    sendChangeMessage();
}

//==============================================================================
// totals & rounding
//==============================================================================
int ClockerProcessor::roundingMinutes() const
{
    static const int opts[] = { 0, 5, 10, 15, 30 };
    const int idx = juce::jlimit (0, 4, (int) settings().getProperty (ids::rounding, 0));
    return opts[idx];
}

juce::int64 ClockerProcessor::roundMsForBilling (juce::int64 ms) const
{
    const auto r = (juce::int64) roundingMinutes();
    if (r <= 0) return ms;
    const auto inc = r * 60000;
    return ((ms + inc / 2) / inc) * inc;
}

Totals ClockerProcessor::computeTotals() const
{
    Totals t;
    auto proj      = project();
    t.projectType  = juce::jlimit (0, projectTypes().size() - 1,
                                   (int) proj.getProperty (ids::projectType, 0));
    t.hourlyRate   = (double) proj.getProperty (ids::hourlyRate, 0.0);
    t.flatFee      = (double) proj.getProperty (ids::flatFee, 0.0);

    for (auto e : entries())
    {
        const juce::int64 d = e.getProperty (ids::durationMs);
        t.totalMs += d;
        t.typeMs[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))] += d;
        if ((bool) e.getProperty (ids::billable))
        {
            t.billableMs        += d;
            t.billableRoundedMs += roundMsForBilling (d);
        }
        else
            t.nonBillableMs += d;
    }

    const double billHours = t.billableRoundedMs / 3600000.0;
    switch (t.projectType)
    {
        case 0:  t.amount = billHours * t.hourlyRate;             break;  // hourly
        case 1:  t.amount = t.flatFee;                            break;  // flat
        case 2:  t.amount = t.flatFee + billHours * t.hourlyRate; break;  // hybrid
        default: t.amount = 0.0;                                  break;  // internal
    }
    if (t.totalMs > 0)
        t.effectiveRate = t.amount / (t.totalMs / 3600000.0);
    return t;
}

//==============================================================================
// exports
//==============================================================================
juce::File ClockerProcessor::clockerFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Sound Detective").getChildFile ("Clocker");
}

juce::String ClockerProcessor::exportBaseName() const
{
    auto sanitize = [] (juce::String s)
    {
        s = s.trim().replaceCharacters (" /\\:*?\"<>|", "__________");
        return s.isEmpty() ? juce::String ("Untitled") : s;
    };
    return sanitize (project().getProperty (ids::client).toString()) + "_"
         + sanitize (project().getProperty (ids::project).toString()) + "_"
         + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
}

juce::String ClockerProcessor::buildReport (bool md) const
{
    const auto t   = computeTotals();
    auto proj      = project();
    const auto h1  = md ? juce::String ("# ")  : juce::String();
    const auto h2  = md ? juce::String ("## ") : juce::String();
    const auto bul = md ? juce::String ("- ")  : juce::String ("  ");
    juce::String s;

    s << h1 << "Sound Detective: Clocker — Time Report\n";
    if (! md) s << "======================================\n";
    s << "\n" << h2 << "Project\n";
    s << bul << "Client: "   << proj.getProperty (ids::client).toString()   << "\n";
    s << bul << "Project: "  << proj.getProperty (ids::project).toString()  << "\n";
    s << bul << "Song: "     << proj.getProperty (ids::song).toString()     << "\n";
    s << bul << "Engineer: " << proj.getProperty (ids::engineer).toString() << "\n";
    s << bul << "Billing: "  << projectTypes()[t.projectType];
    if (t.projectType == 0 || t.projectType == 2) s << " @ " << formatMoney (t.hourlyRate) << "/hr";
    if (t.projectType == 1 || t.projectType == 2) s << ", flat fee " << formatMoney (t.flatFee);
    s << "\n";

    s << "\n" << h2 << "Totals\n";
    s << bul << "Total Time: "        << formatDuration (t.totalMs)       << "\n";
    s << bul << "Billable Time: "     << formatDuration (t.billableMs)    << "\n";
    s << bul << "Non-Billable Time: " << formatDuration (t.nonBillableMs) << "\n";
    if (roundingMinutes() > 0)
        s << bul << "Billed Time (rounded to " << roundingMinutes() << " min): "
          << formatDuration (t.billableRoundedMs) << "\n";
    if (t.projectType != 3)
    {
        s << bul << "Estimated Billing: "     << formatMoney (t.amount) << "\n";
        s << bul << "Effective Hourly Rate: " << formatMoney (t.effectiveRate) << "/hr\n";
    }

    s << "\n" << h2 << "Time Log\n";
    for (auto e : entries())
    {
        s << bul << juce::Time ((juce::int64) e.getProperty (ids::start)).formatted ("%b %d")
          << " — " << sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))]
          << " — " << ((bool) e.getProperty (ids::billable) ? "Billable" : "Non-Billable")
          << " — " << formatDuration (e.getProperty (ids::durationMs));
        if ((bool) e.getProperty (ids::manual)) s << " (manual)";
        s << "\n";
        auto n = e.getProperty (ids::notes).toString().trim();
        if (n.isNotEmpty())
            s << (md ? "  - Notes: " : "      Notes: ") << n << "\n";
    }

    auto bn = proj.getProperty (ids::billingNotes).toString().trim();
    if (bn.isNotEmpty()) s << "\n" << h2 << "Billing Notes\n" << bn << "\n";
    auto in = proj.getProperty (ids::invoiceNotes).toString().trim();
    if (in.isNotEmpty()) s << "\n" << h2 << "Invoice Notes\n" << in << "\n";
    return s;
}

static juce::String csvCell (const juce::String& s)
{
    return "\"" + juce::String (s).replace ("\"", "\"\"") + "\"";
}

juce::String ClockerProcessor::buildCSV() const
{
    auto proj = project();
    juce::String s ("Date,Clock In,Clock Out,Duration,Hours,Billable,Session Type,Client,Project,Song,Rate Context,Notes\n");
    const int pt = juce::jlimit (0, projectTypes().size() - 1, (int) proj.getProperty (ids::projectType, 0));
    juce::String rateCtx = projectTypes()[pt];
    if (pt == 0 || pt == 2) rateCtx << " " << formatMoney ((double) proj.getProperty (ids::hourlyRate, 0.0)) << "/hr";
    if (pt == 1 || pt == 2) rateCtx << " flat " << formatMoney ((double) proj.getProperty (ids::flatFee, 0.0));

    for (auto e : entries())
    {
        const juce::int64 st = e.getProperty (ids::start), en = e.getProperty (ids::end);
        const juce::int64 d  = e.getProperty (ids::durationMs);
        s << csvCell (juce::Time (st).formatted ("%Y-%m-%d")) << ","
          << csvCell ((bool) e.getProperty (ids::manual) ? "manual" : juce::Time (st).formatted ("%H:%M")) << ","
          << csvCell ((bool) e.getProperty (ids::manual) ? "manual" : juce::Time (en).formatted ("%H:%M")) << ","
          << csvCell (formatDuration (d)) << ","
          << juce::String (d / 3600000.0, 3) << ","
          << ((bool) e.getProperty (ids::billable) ? "Yes" : "No") << ","
          << csvCell (sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))]) << ","
          << csvCell (proj.getProperty (ids::client).toString()) << ","
          << csvCell (proj.getProperty (ids::project).toString()) << ","
          << csvCell (proj.getProperty (ids::song).toString()) << ","
          << csvCell (rateCtx) << ","
          << csvCell (e.getProperty (ids::notes).toString()) << "\n";
    }
    return s;
}

juce::String ClockerProcessor::buildJSON() const
{
    auto proj = project();
    const auto t = computeTotals();

    auto* root = new juce::DynamicObject();
    root->setProperty ("clockerVersion", 1);
    root->setProperty ("exportedAt", juce::Time::getCurrentTime().toISO8601 (true));

    auto* p = new juce::DynamicObject();
    p->setProperty ("client",       proj.getProperty (ids::client));
    p->setProperty ("project",      proj.getProperty (ids::project));
    p->setProperty ("song",         proj.getProperty (ids::song));
    p->setProperty ("engineer",     proj.getProperty (ids::engineer));
    p->setProperty ("projectType",  projectTypes()[t.projectType]);
    p->setProperty ("hourlyRate",   t.hourlyRate);
    p->setProperty ("flatFee",      t.flatFee);
    p->setProperty ("billingNotes", proj.getProperty (ids::billingNotes));
    p->setProperty ("invoiceNotes", proj.getProperty (ids::invoiceNotes));
    root->setProperty ("project", juce::var (p));

    auto* tot = new juce::DynamicObject();
    tot->setProperty ("totalMs",           t.totalMs);
    tot->setProperty ("billableMs",        t.billableMs);
    tot->setProperty ("nonBillableMs",     t.nonBillableMs);
    tot->setProperty ("billableRoundedMs", t.billableRoundedMs);
    tot->setProperty ("roundingMinutes",   roundingMinutes());
    tot->setProperty ("estimatedBilling",  t.amount);
    tot->setProperty ("effectiveHourlyRate", t.effectiveRate);
    root->setProperty ("totals", juce::var (tot));

    juce::Array<juce::var> arr;
    for (auto e : entries())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("start",      e.getProperty (ids::start));
        o->setProperty ("end",        e.getProperty (ids::end));
        o->setProperty ("durationMs", e.getProperty (ids::durationMs));
        o->setProperty ("billable",   e.getProperty (ids::billable));
        o->setProperty ("sessionType", sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))]);
        o->setProperty ("manual",     e.getProperty (ids::manual));
        o->setProperty ("notes",      e.getProperty (ids::notes));
        arr.add (juce::var (o));
    }
    root->setProperty ("entries", arr);
    return juce::JSON::toString (juce::var (root));
}

//==============================================================================
// audio passthrough & session state
//==============================================================================
bool ClockerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ClockerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Pure passthrough — input channels already hold the output. Just silence
    // any extra output channels the host may have handed us.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void ClockerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (stateLock);
    auto a = activeTree();
    if (a.isValid())   // stamp save time so a restore can close the entry accurately
        a.setProperty (ids::savedAt, juce::Time::currentTimeMillis(), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void ClockerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! t.hasType (ids::Clocker)) return;
    {
        const juce::ScopedLock sl (stateLock);
        state = t;
    }
    ensureStructure();

    // If a timer was live when the session was saved, close it at the save
    // timestamp so hours stay honest even if the session is reopened days later.
    auto a = activeTree();
    if (a.isValid())
    {
        const juce::int64 savedAt = a.getProperty (ids::savedAt, a.getProperty (ids::lastResume));
        juce::int64 acc = a.getProperty (ids::accumulated);
        if ((bool) a.getProperty (ids::running))
            acc += juce::jmax ((juce::int64) 0, savedAt - (juce::int64) a.getProperty (ids::lastResume));
        if (acc >= 1000)
            appendEntry (a.getProperty (ids::clockIn), savedAt, acc,
                         (bool) a.getProperty (ids::billable), (int) a.getProperty (ids::type),
                         "(auto-closed when session was saved)", false);
        state.removeChild (a, nullptr);
    }
    sendChangeMessage();
}

juce::AudioProcessorEditor* ClockerProcessor::createEditor()
{
#if CLOCKER_NO_EDITOR
    return nullptr;
#else
    return new ClockerEditor (*this);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClockerProcessor();
}
