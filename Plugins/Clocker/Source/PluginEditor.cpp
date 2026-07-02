#include "PluginEditor.h"
#include "BinaryData.h"

using namespace clocker;

//==============================================================================
// theme helpers
//==============================================================================
juce::Font theme::mono (float size, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font theme::ui (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

void theme::styleButton (juce::TextButton& b, juce::Colour accent)
{
    b.setColour (juce::TextButton::buttonColourId,   panel);
    b.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.6f));
    b.setColour (juce::TextButton::textColourOffId,  text);
    b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
    b.setColour (juce::ComboBox::outlineColourId,    accent.withAlpha (0.45f));
}

void theme::styleEditor (juce::TextEditor& e, bool multiline)
{
    e.setColour (juce::TextEditor::backgroundColourId,     inset);
    e.setColour (juce::TextEditor::textColourId,           text);
    e.setColour (juce::TextEditor::outlineColourId,        juce::Colour (0x33ffffff));
    e.setColour (juce::TextEditor::focusedOutlineColourId, amber);
    e.setColour (juce::CaretComponent::caretColourId,      amber);
    e.setFont (ui (13.0f));
    if (multiline)
    {
        e.setMultiLine (true);
        e.setReturnKeyStartsNewLine (true);
    }
}

void theme::styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, inset);
    c.setColour (juce::ComboBox::textColourId,       text);
    c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0x33ffffff));
    c.setColour (juce::ComboBox::arrowColourId,      amber);
}

void theme::caption (juce::Label& l, const juce::String& s)
{
    l.setText (s, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, amber);
    l.setFont (ui (10.5f, true));
}

static void fillSessionTypes (juce::ComboBox& box)
{
    for (int i = 0; i < sessionTypes().size(); ++i)
        box.addItem (sessionTypes()[i], i + 1);
}

//==============================================================================
// CLOCK TAB
//==============================================================================
ClockTab::ClockTab (ClockerProcessor& p) : ClockerTab (p)
{
    timerLabel.setFont (theme::mono (58.0f, true));
    timerLabel.setColour (juce::Label::textColourId, theme::text);
    timerLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (timerLabel);

    statusLabel.setFont (theme::ui (14.0f, true));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    contextLabel.setFont (theme::ui (12.0f));
    contextLabel.setColour (juce::Label::textColourId, theme::dim);
    contextLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (contextLabel);

    theme::styleButton (clockInBtn,  theme::green);
    theme::styleButton (pauseBtn,    theme::amber);
    theme::styleButton (resumeBtn,   theme::green);
    theme::styleButton (clockOutBtn, theme::red);
    clockInBtn.onClick  = [this] { proc.clockIn(); refresh(); };
    pauseBtn.onClick    = [this] { proc.pauseTimer(); refresh(); };
    resumeBtn.onClick   = [this] { proc.resumeTimer(); refresh(); };
    clockOutBtn.onClick = [this] { proc.clockOut (notesEd.getText().trim()); notesEd.clear(); refresh(); };
    for (auto* b : { &clockInBtn, &pauseBtn, &resumeBtn, &clockOutBtn })
        addAndMakeVisible (b);

    billableToggle.setColour (juce::ToggleButton::textColourId, theme::text);
    billableToggle.setColour (juce::ToggleButton::tickColourId, theme::green);
    billableToggle.onClick = [this] { proc.setActiveBillable (billableToggle.getToggleState()); refresh(); };
    addAndMakeVisible (billableToggle);

    theme::caption (typeCap, "SESSION TYPE");
    addAndMakeVisible (typeCap);
    fillSessionTypes (typeBox);
    theme::styleCombo (typeBox);
    typeBox.onChange = [this]
    {
        if (typeBox.getSelectedId() > 0)
            proc.setActiveType (typeBox.getSelectedId() - 1);
    };
    addAndMakeVisible (typeBox);

    theme::caption (notesCap, "NOTES FOR THIS ENTRY");
    addAndMakeVisible (notesCap);
    theme::styleEditor (notesEd, true);
    addAndMakeVisible (notesEd);

    theme::caption (manualCap, "ADD MANUAL TIME  (H : M)");
    addAndMakeVisible (manualCap);
    for (auto* e : { &manualHoursEd, &manualMinsEd })
    {
        theme::styleEditor (*e);
        e->setInputRestrictions (3, "0123456789");
        e->setJustification (juce::Justification::centred);
        addAndMakeVisible (e);
    }
    theme::styleButton (manualAddBtn, theme::blue);
    manualAddBtn.onClick = [this]
    {
        const auto h = manualHoursEd.getText().getIntValue();
        const auto m = manualMinsEd.getText().getIntValue();
        const juce::int64 ms = ((juce::int64) h * 60 + m) * 60000;
        if (ms > 0)
        {
            proc.addManualEntry (ms, billableToggle.getToggleState(),
                                 juce::jmax (0, typeBox.getSelectedId() - 1),
                                 notesEd.getText().trim());
            manualHoursEd.clear();
            manualMinsEd.clear();
            notesEd.clear();
        }
    };
    addAndMakeVisible (manualAddBtn);

    refresh();
    startTimer (500);
}

void ClockTab::refresh()
{
    const auto st = proc.clockState();
    const bool decimal = (int) proc.settings().getProperty (ids::timeFormat, 0) == 1;
    const auto ms = proc.elapsedMs();
    timerLabel.setText (decimal ? juce::String (ms / 3600000.0, 2) + " h" : formatClock (ms),
                        juce::dontSendNotification);

    switch (st)
    {
        case ClockerProcessor::ClockState::running:
            statusLabel.setText ("ON THE CASE — CLOCKED IN", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, theme::green);
            break;
        case ClockerProcessor::ClockState::paused:
            statusLabel.setText ("PAUSED", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, theme::amber);
            break;
        default:
            statusLabel.setText ("OFF THE CLOCK", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, theme::dim);
            break;
    }

    auto proj = proc.project();
    juce::String ctx;
    ctx << (proc.activeBillable() ? "Billable: ON" : "Billable: OFF")
        << "  •  " << sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, proc.activeType())];
    auto client = proj.getProperty (ids::client).toString().trim();
    auto song   = proj.getProperty (ids::song).toString().trim();
    if (client.isNotEmpty()) ctx << "  •  " << client;
    if (song.isNotEmpty())   ctx << "  •  " << song;
    contextLabel.setText (ctx, juce::dontSendNotification);

    clockInBtn.setEnabled  (st == ClockerProcessor::ClockState::idle);
    pauseBtn.setEnabled    (st == ClockerProcessor::ClockState::running);
    resumeBtn.setEnabled   (st == ClockerProcessor::ClockState::paused);
    clockOutBtn.setEnabled (st != ClockerProcessor::ClockState::idle);

    if (billableToggle.getToggleState() != proc.activeBillable())
        billableToggle.setToggleState (proc.activeBillable(), juce::dontSendNotification);
    if (typeBox.getSelectedId() != proc.activeType() + 1)
        typeBox.setSelectedId (proc.activeType() + 1, juce::dontSendNotification);
}

void ClockTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto r = getLocalBounds().reduced (18).removeFromTop (170).toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (theme::amber.withAlpha (0.25f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void ClockTab::resized()
{
    auto r = getLocalBounds().reduced (18);

    auto top = r.removeFromTop (170);
    timerLabel.setBounds (top.removeFromTop (100).reduced (8, 10));
    statusLabel.setBounds (top.removeFromTop (26));
    contextLabel.setBounds (top.removeFromTop (22));

    r.removeFromTop (14);
    auto btns = r.removeFromTop (42);
    const int bw = (btns.getWidth() - 3 * 10) / 4;
    clockInBtn.setBounds  (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    pauseBtn.setBounds    (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    resumeBtn.setBounds   (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    clockOutBtn.setBounds (btns);

    r.removeFromTop (14);
    auto row = r.removeFromTop (26);
    billableToggle.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    typeCap.setBounds (row.removeFromLeft (90));
    typeBox.setBounds (row.removeFromLeft (190).withHeight (26));

    r.removeFromTop (12);
    notesCap.setBounds (r.removeFromTop (16));
    notesEd.setBounds (r.removeFromTop (58));

    r.removeFromTop (12);
    manualCap.setBounds (r.removeFromTop (16));
    auto mrow = r.removeFromTop (28);
    manualHoursEd.setBounds (mrow.removeFromLeft (54));
    mrow.removeFromLeft (8);
    manualMinsEd.setBounds (mrow.removeFromLeft (54));
    mrow.removeFromLeft (12);
    manualAddBtn.setBounds (mrow.removeFromLeft (180));
}

//==============================================================================
// TIME LOG TAB
//==============================================================================
TimeLogTab::TimeLogTab (ClockerProcessor& p) : ClockerTab (p)
{
    list.setColour (juce::ListBox::backgroundColourId, theme::inset);
    list.setColour (juce::ListBox::outlineColourId, juce::Colour (0x33ffffff));
    list.setOutlineThickness (1);
    list.setRowHeight (24);
    addAndMakeVisible (list);

    theme::caption (editCap, "SELECTED ENTRY");
    addAndMakeVisible (editCap);
    theme::caption (hoursCap, "H");
    theme::caption (minsCap,  "M");
    theme::caption (typeCap2, "TYPE");
    theme::caption (notesCap2, "NOTES");
    for (auto* c : { &hoursCap, &minsCap, &typeCap2, &notesCap2 })
        addAndMakeVisible (c);

    for (auto* e : { &hoursEd, &minsEd })
    {
        theme::styleEditor (*e);
        e->setInputRestrictions (3, "0123456789");
        e->setJustification (juce::Justification::centred);
        addAndMakeVisible (e);
    }
    theme::styleEditor (notesEd);
    addAndMakeVisible (notesEd);

    billableToggle.setColour (juce::ToggleButton::textColourId, theme::text);
    billableToggle.setColour (juce::ToggleButton::tickColourId, theme::green);
    addAndMakeVisible (billableToggle);

    fillSessionTypes (typeBox);
    theme::styleCombo (typeBox);
    addAndMakeVisible (typeBox);

    theme::styleButton (saveBtn, theme::green);
    saveBtn.onClick = [this] { saveSelected(); };
    addAndMakeVisible (saveBtn);
    theme::styleButton (deleteBtn, theme::red);
    deleteBtn.onClick = [this] { deleteSelected(); };
    addAndMakeVisible (deleteBtn);
}

int TimeLogTab::getNumRows() { return proc.entries().getNumChildren(); }

juce::ValueTree TimeLogTab::entryForRow (int row) const
{
    auto ent = proc.entries();
    return ent.getChild (ent.getNumChildren() - 1 - row);   // newest first
}

void TimeLogTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    auto e = entryForRow (row);
    if (! e.isValid()) return;

    if (selected)             g.fillAll (theme::amber.withAlpha (0.18f));
    else if ((row & 1) == 0)  g.fillAll (juce::Colour (0x0dffffff));

    const bool billable = e.getProperty (ids::billable);
    g.setFont (theme::ui (12.0f));
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (8, 0);

    g.setColour (theme::dim);
    g.drawText (formatDate (e.getProperty (ids::start)), r.removeFromLeft (92), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.drawText (sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))],
                r.removeFromLeft (120), juce::Justification::centredLeft);
    g.setColour (billable ? theme::green : theme::red);
    g.drawText (billable ? "BILL" : "N/B", r.removeFromLeft (42), juce::Justification::centredLeft);
    g.setColour (theme::blue);
    g.drawText (formatDuration (e.getProperty (ids::durationMs)), r.removeFromLeft (72), juce::Justification::centredLeft);
    g.setColour (theme::dim);
    juce::String n = e.getProperty (ids::notes).toString().replace ("\n", " ");
    if ((bool) e.getProperty (ids::manual)) n = "[manual] " + n;
    g.drawText (n, r, juce::Justification::centredLeft);
}

void TimeLogTab::selectedRowsChanged (int)
{
    auto e = entryForRow (list.getSelectedRow());
    if (! e.isValid()) return;
    const juce::int64 d = e.getProperty (ids::durationMs);
    hoursEd.setText (juce::String ((int) (d / 3600000)), false);
    minsEd.setText (juce::String ((int) ((d / 60000) % 60)), false);
    billableToggle.setToggleState ((bool) e.getProperty (ids::billable), juce::dontSendNotification);
    typeBox.setSelectedId ((int) e.getProperty (ids::type) + 1, juce::dontSendNotification);
    notesEd.setText (e.getProperty (ids::notes).toString(), false);
}

void TimeLogTab::saveSelected()
{
    auto e = entryForRow (list.getSelectedRow());
    if (! e.isValid()) return;
    const juce::int64 ms = ((juce::int64) hoursEd.getText().getIntValue() * 60
                            + minsEd.getText().getIntValue()) * 60000;
    e.setProperty (ids::durationMs, juce::jmax ((juce::int64) 60000, ms), nullptr);
    e.setProperty (ids::end, (juce::int64) e.getProperty (ids::start)
                                 + (juce::int64) e.getProperty (ids::durationMs), nullptr);
    e.setProperty (ids::billable, billableToggle.getToggleState(), nullptr);
    e.setProperty (ids::type, juce::jmax (0, typeBox.getSelectedId() - 1), nullptr);
    e.setProperty (ids::notes, notesEd.getText(), nullptr);
    list.repaint();
    proc.sendChangeMessage();
}

void TimeLogTab::deleteSelected()
{
    auto e = entryForRow (list.getSelectedRow());
    if (! e.isValid()) return;
    proc.entries().removeChild (e, nullptr);
    list.deselectAllRows();
    refresh();
    proc.sendChangeMessage();
}

void TimeLogTab::refresh()
{
    list.updateContent();
    list.repaint();
}

void TimeLogTab::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void TimeLogTab::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto edit = r.removeFromBottom (96);
    list.setBounds (r);

    edit.removeFromTop (6);
    editCap.setBounds (edit.removeFromTop (16));
    auto row1 = edit.removeFromTop (30);
    hoursCap.setBounds (row1.removeFromLeft (14));
    hoursEd.setBounds  (row1.removeFromLeft (48)); row1.removeFromLeft (8);
    minsCap.setBounds  (row1.removeFromLeft (16));
    minsEd.setBounds   (row1.removeFromLeft (48)); row1.removeFromLeft (12);
    billableToggle.setBounds (row1.removeFromLeft (96)); row1.removeFromLeft (8);
    typeBox.setBounds  (row1.removeFromLeft (170)); row1.removeFromLeft (12);
    saveBtn.setBounds  (row1.removeFromLeft (86)); row1.removeFromLeft (8);
    deleteBtn.setBounds (row1.removeFromLeft (86));

    edit.removeFromTop (6);
    auto row2 = edit.removeFromTop (28);
    notesCap2.setBounds (row2.removeFromLeft (50));
    notesEd.setBounds (row2);
}

//==============================================================================
// CLIENT / PROJECT TAB
//==============================================================================
ClientTab::ClientTab (ClockerProcessor& p) : ClockerTab (p)
{
    const char* names[9] = { "CLIENT NAME", "PROJECT NAME", "SONG TITLE", "ENGINEER NAME",
                             "PROJECT TYPE", "HOURLY RATE ($/HR)", "FLAT PROJECT FEE ($)",
                             "BILLING NOTES", "INVOICE NOTES" };
    for (int i = 0; i < 9; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    for (auto* e : { &clientEd, &projectEd, &songEd, &engineerEd, &rateEd, &feeEd })
    {
        theme::styleEditor (*e);
        addAndMakeVisible (e);
    }
    for (auto* e : { &billingNotesEd, &invoiceNotesEd })
    {
        theme::styleEditor (*e, true);
        addAndMakeVisible (e);
    }
    rateEd.setInputRestrictions (10, "0123456789.");
    feeEd.setInputRestrictions (10, "0123456789.");

    bindText (clientEd,   ids::client);
    bindText (projectEd,  ids::project);
    bindText (songEd,     ids::song);
    bindText (engineerEd, ids::engineer);
    bindText (billingNotesEd, ids::billingNotes);
    bindText (invoiceNotesEd, ids::invoiceNotes);
    bindDouble (rateEd, ids::hourlyRate);
    bindDouble (feeEd,  ids::flatFee);

    for (int i = 0; i < projectTypes().size(); ++i)
        projectTypeBox.addItem (projectTypes()[i], i + 1);
    theme::styleCombo (projectTypeBox);
    projectTypeBox.onChange = [this]
    {
        if (projectTypeBox.getSelectedId() > 0)
            proc.project().setProperty (ids::projectType, projectTypeBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (projectTypeBox);

    theme::styleButton (resetBtn, theme::red);
    resetBtn.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
            "New Project / Reset",
            "Clear all time entries and project details?\nExport your time log first if you need it.",
            "Reset", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1) { proc.resetProject(); refresh(); }
            }));
    };
    addAndMakeVisible (resetBtn);

    refresh();
}

void ClientTab::bindText (juce::TextEditor& ed, const juce::Identifier& prop)
{
    ed.onTextChange = [this, &ed, prop] { proc.project().setProperty (prop, ed.getText(), nullptr); };
}

void ClientTab::bindDouble (juce::TextEditor& ed, const juce::Identifier& prop)
{
    ed.onTextChange = [this, &ed, prop] { proc.project().setProperty (prop, ed.getText().getDoubleValue(), nullptr); };
}

void ClientTab::refresh()
{
    auto proj = proc.project();
    auto set = [] (juce::TextEditor& e, const juce::String& v)
    { if (! e.hasKeyboardFocus (true) && e.getText() != v) e.setText (v, false); };

    set (clientEd,   proj.getProperty (ids::client).toString());
    set (projectEd,  proj.getProperty (ids::project).toString());
    set (songEd,     proj.getProperty (ids::song).toString());
    set (engineerEd, proj.getProperty (ids::engineer).toString());
    set (rateEd,     juce::String ((double) proj.getProperty (ids::hourlyRate, 0.0), 2));
    set (feeEd,      juce::String ((double) proj.getProperty (ids::flatFee, 0.0), 2));
    set (billingNotesEd, proj.getProperty (ids::billingNotes).toString());
    set (invoiceNotesEd, proj.getProperty (ids::invoiceNotes).toString());
    projectTypeBox.setSelectedId ((int) proj.getProperty (ids::projectType, 0) + 1,
                                  juce::dontSendNotification);
}

void ClientTab::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void ClientTab::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto left  = r.removeFromLeft ((r.getWidth() - 20) / 2);
    r.removeFromLeft (20);
    auto right = r;

    auto place = [] (juce::Rectangle<int>& col, juce::Label& cap, juce::Component& comp)
    {
        cap.setBounds (col.removeFromTop (16));
        comp.setBounds (col.removeFromTop (28));
        col.removeFromTop (10);
    };

    place (left, caps[0], clientEd);
    place (left, caps[1], projectEd);
    place (left, caps[2], songEd);
    place (left, caps[3], engineerEd);

    place (right, caps[4], projectTypeBox);
    place (right, caps[5], rateEd);
    place (right, caps[6], feeEd);
    right.removeFromTop (16);
    resetBtn.setBounds (right.removeFromTop (32));

    left.removeFromTop (4);
    caps[7].setBounds (left.removeFromTop (16));
    billingNotesEd.setBounds (left.removeFromTop (juce::jmax (40, left.getHeight() - 4)));
    right.removeFromTop (14);
    caps[8].setBounds (right.removeFromTop (16));
    invoiceNotesEd.setBounds (right.removeFromTop (juce::jmax (40, right.getHeight() - 4)));
}

//==============================================================================
// PROFITABILITY TAB
//==============================================================================
ProfitTab::ProfitTab (ClockerProcessor& p) : ClockerTab (p)
{
    theme::styleEditor (view, true);
    view.setReadOnly (true);
    view.setFont (theme::mono (13.0f));
    addAndMakeVisible (view);

    theme::styleButton (refreshBtn, theme::blue);
    refreshBtn.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshBtn);
}

void ProfitTab::refresh()
{
    const auto t = proc.computeTotals();
    auto proj = proc.project();
    juce::String s;

    s << "CASE PROFITABILITY\n";
    s << "==================\n\n";
    s << "Client:   " << proj.getProperty (ids::client).toString()  << "\n";
    s << "Project:  " << proj.getProperty (ids::project).toString() << "\n";
    s << "Song:     " << proj.getProperty (ids::song).toString()    << "\n";
    s << "Billing:  " << projectTypes()[t.projectType] << "\n\n";

    s << "Total Time:          " << formatDuration (t.totalMs)       << "   (" << formatHours (t.totalMs)       << ")\n";
    s << "Billable Time:       " << formatDuration (t.billableMs)    << "   (" << formatHours (t.billableMs)    << ")\n";
    s << "Non-Billable Time:   " << formatDuration (t.nonBillableMs) << "   (" << formatHours (t.nonBillableMs) << ")\n";
    if (proc.roundingMinutes() > 0)
        s << "Billed (rounded " << proc.roundingMinutes() << "m):  " << formatDuration (t.billableRoundedMs) << "\n";
    s << "\n";

    if (t.projectType == 0 || t.projectType == 2)
        s << "Hourly Rate:         " << formatMoney (t.hourlyRate) << "/hr\n";
    if (t.projectType == 1 || t.projectType == 2)
        s << "Flat Fee:            " << formatMoney (t.flatFee) << "\n";
    if (t.projectType != 3)
    {
        s << "Estimated Billing:   " << formatMoney (t.amount) << "\n";
        s << "Effective Rate:      " << formatMoney (t.effectiveRate) << "/hr (vs total time)\n";
    }
    s << "\nTIME BY SESSION TYPE\n--------------------\n";
    for (int i = 0; i < sessionTypes().size(); ++i)
        if (t.typeMs[i] > 0)
            s << sessionTypes()[i].paddedRight (' ', 20) << formatDuration (t.typeMs[i]) << "\n";

    // simple business insight
    s << "\nINSIGHT\n-------\n";
    if (t.totalMs <= 0)
        s << "No time logged yet. Clock in and the numbers will follow.\n";
    else
    {
        if (t.projectType == 1 && t.flatFee > 0.0)
            s << "This project has taken " << formatDuration (t.totalMs) << ". At a "
              << formatMoney (t.flatFee) << " flat rate, the effective hourly rate is "
              << formatMoney (t.effectiveRate) << "/hr.\n";
        const auto revMs = t.typeMs[6] + t.typeMs[7];  // Revision + Recall
        if (revMs > 0 && revMs * 100 / t.totalMs >= 15)
            s << "Revisions/recalls are " << juce::String ((int) (revMs * 100 / t.totalMs))
              << "% of total time. Consider revision limits or hourly revisions.\n";
        if (t.nonBillableMs > 0 && t.nonBillableMs * 100 / t.totalMs >= 20)
            s << juce::String ((int) (t.nonBillableMs * 100 / t.totalMs))
              << "% of tracked time is non-billable. Look at where those hours go.\n";
    }

    view.setText (s, false);
}

void ProfitTab::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void ProfitTab::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto top = r.removeFromTop (30);
    refreshBtn.setBounds (top.removeFromRight (110));
    r.removeFromTop (8);
    view.setBounds (r);
}

//==============================================================================
// EXPORT TAB
//==============================================================================
ExportTab::ExportTab (ClockerProcessor& p) : ClockerTab (p)
{
    formatBox.addItem ("Text Report", 1);
    formatBox.addItem ("Markdown",    2);
    formatBox.addItem ("CSV",         3);
    formatBox.addItem ("JSON",        4);
    formatBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (formatBox);
    formatBox.onChange = [this] { refresh(); };
    addAndMakeVisible (formatBox);

    theme::styleEditor (preview, true);
    preview.setReadOnly (true);
    preview.setFont (theme::mono (12.0f));
    addAndMakeVisible (preview);

    theme::styleButton (copyBtn, theme::blue);
    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (buildCurrent());
        savedLabel.setText ("Copied to clipboard.", juce::dontSendNotification);
    };
    addAndMakeVisible (copyBtn);

    theme::styleButton (saveBtn, theme::green);
    saveBtn.onClick = [this]
    {
        static const char* exts[] = { ".txt", ".md", ".csv", ".json" };
        auto folder = ClockerProcessor::clockerFolder();
        folder.createDirectory();
        auto file = folder.getChildFile (proc.exportBaseName()
                                         + exts[juce::jlimit (0, 3, formatBox.getSelectedId() - 1)]);
        if (file.replaceWithText (buildCurrent()))
        {
            savedLabel.setText ("Saved: " + file.getFullPathName(), juce::dontSendNotification);
            file.revealToUser();
        }
        else
            savedLabel.setText ("Could not write file.", juce::dontSendNotification);
    };
    addAndMakeVisible (saveBtn);

    savedLabel.setFont (theme::ui (11.0f));
    savedLabel.setColour (juce::Label::textColourId, theme::dim);
    addAndMakeVisible (savedLabel);

    folderLabel.setFont (theme::ui (11.0f));
    folderLabel.setColour (juce::Label::textColourId, theme::dim);
    folderLabel.setText ("Clocker folder: " + ClockerProcessor::clockerFolder().getFullPathName(),
                         juce::dontSendNotification);
    addAndMakeVisible (folderLabel);
}

juce::String ExportTab::buildCurrent() const
{
    switch (formatBox.getSelectedId())
    {
        case 2:  return proc.buildReport (true);
        case 3:  return proc.buildCSV();
        case 4:  return proc.buildJSON();
        default: return proc.buildReport (false);
    }
}

void ExportTab::refresh() { preview.setText (buildCurrent(), false); }

void ExportTab::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void ExportTab::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto top = r.removeFromTop (30);
    formatBox.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (10);
    copyBtn.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (10);
    saveBtn.setBounds (top.removeFromLeft (200));
    r.removeFromTop (6);
    savedLabel.setBounds (r.removeFromTop (16));
    folderLabel.setBounds (r.removeFromBottom (16));
    r.removeFromTop (6);
    r.removeFromBottom (4);
    preview.setBounds (r);
}

//==============================================================================
// SETTINGS TAB
//==============================================================================
SettingsTab::SettingsTab (ClockerProcessor& p) : ClockerTab (p)
{
    const char* names[6] = { "DEFAULT HOURLY RATE ($/HR)", "BILLING ROUNDING",
                             "TIMER DISPLAY", "DEFAULT SESSION TYPE", "", "" };
    for (int i = 0; i < 4; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleEditor (defaultRateEd);
    defaultRateEd.setInputRestrictions (10, "0123456789.");
    defaultRateEd.onTextChange = [this]
    { proc.settings().setProperty (ids::defaultRate, defaultRateEd.getText().getDoubleValue(), nullptr); };
    addAndMakeVisible (defaultRateEd);

    roundingBox.addItem ("Exact time",                 1);
    roundingBox.addItem ("Round to nearest 5 minutes", 2);
    roundingBox.addItem ("Round to nearest 10 minutes", 3);
    roundingBox.addItem ("Round to nearest 15 minutes", 4);
    roundingBox.addItem ("Round to nearest 30 minutes", 5);
    theme::styleCombo (roundingBox);
    roundingBox.onChange = [this]
    {
        if (roundingBox.getSelectedId() > 0)
            proc.settings().setProperty (ids::rounding, roundingBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (roundingBox);

    timeFormatBox.addItem ("HH:MM:SS", 1);
    timeFormatBox.addItem ("Decimal hours", 2);
    theme::styleCombo (timeFormatBox);
    timeFormatBox.onChange = [this]
    {
        if (timeFormatBox.getSelectedId() > 0)
            proc.settings().setProperty (ids::timeFormat, timeFormatBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (timeFormatBox);

    fillSessionTypes (defaultTypeBox);
    theme::styleCombo (defaultTypeBox);
    defaultTypeBox.onChange = [this]
    {
        if (defaultTypeBox.getSelectedId() > 0)
            proc.settings().setProperty (ids::defaultType, defaultTypeBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (defaultTypeBox);

    defaultBillableToggle.setColour (juce::ToggleButton::textColourId, theme::text);
    defaultBillableToggle.setColour (juce::ToggleButton::tickColourId, theme::green);
    defaultBillableToggle.onClick = [this]
    { proc.settings().setProperty (ids::defaultBillable, defaultBillableToggle.getToggleState(), nullptr); };
    addAndMakeVisible (defaultBillableToggle);

    folderLabel.setFont (theme::ui (11.0f));
    folderLabel.setColour (juce::Label::textColourId, theme::dim);
    folderLabel.setText ("Exports save to: " + ClockerProcessor::clockerFolder().getFullPathName(),
                         juce::dontSendNotification);
    addAndMakeVisible (folderLabel);

    refresh();
}

void SettingsTab::refresh()
{
    auto set = proc.settings();
    if (! defaultRateEd.hasKeyboardFocus (true))
        defaultRateEd.setText (juce::String ((double) set.getProperty (ids::defaultRate, 100.0), 2), false);
    roundingBox.setSelectedId ((int) set.getProperty (ids::rounding, 0) + 1, juce::dontSendNotification);
    timeFormatBox.setSelectedId ((int) set.getProperty (ids::timeFormat, 0) + 1, juce::dontSendNotification);
    defaultTypeBox.setSelectedId ((int) set.getProperty (ids::defaultType, 4) + 1, juce::dontSendNotification);
    defaultBillableToggle.setToggleState ((bool) set.getProperty (ids::defaultBillable, true),
                                          juce::dontSendNotification);
}

void SettingsTab::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void SettingsTab::resized()
{
    auto r = getLocalBounds().reduced (18);
    auto col = r.removeFromLeft (juce::jmin (360, r.getWidth()));
    auto place = [&col] (juce::Label& cap, juce::Component& comp)
    {
        cap.setBounds (col.removeFromTop (16));
        comp.setBounds (col.removeFromTop (28));
        col.removeFromTop (12);
    };
    place (caps[0], defaultRateEd);
    place (caps[1], roundingBox);
    place (caps[2], timeFormatBox);
    place (caps[3], defaultTypeBox);
    defaultBillableToggle.setBounds (col.removeFromTop (26));
    col.removeFromTop (16);
    folderLabel.setBounds (col.removeFromTop (16));
}

//==============================================================================
// EDITOR
//==============================================================================
ClockerEditor::ClockerEditor (ClockerProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_detective47_dust1200_BLUE_png,
                                            BinaryData::logo_detective47_dust1200_BLUE_pngSize);

    titleLabel.setText ("SOUND DETECTIVE  //  CLOCKER", juce::dontSendNotification);
    titleLabel.setFont (theme::ui (16.0f, true));
    titleLabel.setColour (juce::Label::textColourId, theme::amber);
    addAndMakeVisible (titleLabel);

    statusChip.setFont (theme::mono (13.0f, true));
    statusChip.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusChip);

    tabs.setTabBarDepth (30);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, theme::bg);
    tabs.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    tabs.addTab ("Clock",            theme::bg, new ClockTab (proc),    true);
    tabs.addTab ("Time Log",         theme::bg, new TimeLogTab (proc),  true);
    tabs.addTab ("Client / Project", theme::bg, new ClientTab (proc),   true);
    tabs.addTab ("Profitability",    theme::bg, new ProfitTab (proc),   true);
    tabs.addTab ("Export",           theme::bg, new ExportTab (proc),   true);
    tabs.addTab ("Settings",         theme::bg, new SettingsTab (proc), true);
    addAndMakeVisible (tabs);

    proc.addChangeListener (this);
    startTimer (1000);
    timerCallback();

    setResizable (true, true);
    setResizeLimits (740, 520, 1400, 1000);
    setSize (820, 560);
}

ClockerEditor::~ClockerEditor()
{
    proc.removeChangeListener (this);
}

void ClockerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* t = dynamic_cast<ClockerTab*> (tabs.getTabContentComponent (i)))
            if (t->isVisible())
                t->refresh();
}

void ClockerEditor::timerCallback()
{
    switch (proc.clockState())
    {
        case ClockerProcessor::ClockState::running:
            statusChip.setText ("ON THE CASE  " + formatClock (proc.elapsedMs()), juce::dontSendNotification);
            statusChip.setColour (juce::Label::textColourId, theme::green);
            break;
        case ClockerProcessor::ClockState::paused:
            statusChip.setText ("PAUSED  " + formatClock (proc.elapsedMs()), juce::dontSendNotification);
            statusChip.setColour (juce::Label::textColourId, theme::amber);
            break;
        default:
            statusChip.setText ("OFF THE CLOCK", juce::dontSendNotification);
            statusChip.setColour (juce::Label::textColourId, theme::dim);
            break;
    }
}

void ClockerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto header = getLocalBounds().removeFromTop (48);
    g.setColour (theme::panel);
    g.fillRect (header);
    g.setColour (theme::amber.withAlpha (0.3f));
    g.fillRect (header.removeFromBottom (1));

    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (10.0f, 6.0f, 36.0f, 36.0f),
                     juce::RectanglePlacement::centred);
}

void ClockerEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (48);
    header.removeFromLeft (54);
    statusChip.setBounds (header.removeFromRight (230).reduced (8, 12));
    titleLabel.setBounds (header.reduced (0, 12));
    tabs.setBounds (r);
}
