#include "PluginEditor.h"
#include "BinaryData.h"

using namespace clocker;

//==============================================================================
// theme
//==============================================================================
juce::Font theme::type (float size, bool bold)
{
    // American Typewriter ships with macOS; JUCE falls back gracefully elsewhere
    return juce::Font (juce::FontOptions ("American Typewriter", size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font theme::mono (float size, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

void theme::styleButton (juce::TextButton& b, juce::Colour accent)
{
    b.setColour (juce::TextButton::buttonColourId,  field);
    b.setColour (juce::TextButton::textColourOffId, accent);   // LNF also borders with this
    b.setColour (juce::TextButton::textColourOnId,  accent);
}

void theme::styleEditor (juce::TextEditor& e, bool multiline)
{
    e.setColour (juce::TextEditor::backgroundColourId,     field);
    e.setColour (juce::TextEditor::textColourId,           ink);
    e.setColour (juce::TextEditor::outlineColourId,        ink.withAlpha (0.35f));
    e.setColour (juce::TextEditor::focusedOutlineColourId, stamp);
    e.setColour (juce::CaretComponent::caretColourId,      ink);
    e.setFont (type (13.5f));
    if (multiline)
    {
        e.setMultiLine (true);
        e.setReturnKeyStartsNewLine (true);
    }
}

void theme::styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, field);
    c.setColour (juce::ComboBox::textColourId,       ink);
    c.setColour (juce::ComboBox::outlineColourId,    ink.withAlpha (0.35f));
    c.setColour (juce::ComboBox::arrowColourId,      stamp);
}

void theme::caption (juce::Label& l, const juce::String& s)
{
    l.setText (s, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, brass);
    l.setFont (type (11.0f, true));
}

void theme::paintPaper (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (paper);
    g.fillRect (area);
    // subtle age speckle — fixed seed so it never flickers
    juce::Random rnd (47);
    g.setColour (ink.withAlpha (0.03f));
    for (int i = 0; i < 260; ++i)
        g.fillRect (area.getX() + rnd.nextInt (juce::jmax (1, area.getWidth())),
                    area.getY() + rnd.nextInt (juce::jmax (1, area.getHeight())),
                    rnd.nextInt (3) + 1, 1);
    // faded top edge, like the folder has seen some years
    g.setGradientFill (juce::ColourGradient (ink.withAlpha (0.08f),
                                             (float) area.getX(), (float) area.getY(),
                                             juce::Colours::transparentBlack,
                                             (float) area.getX(), (float) area.getY() + 14.0f, false));
    g.fillRect (area.removeFromTop (14));
}

void theme::drawStamp (juce::Graphics& g, const juce::String& text, juce::Colour c,
                       juce::Rectangle<float> area, float angleDegrees)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (angleDegrees),
                                                     area.getCentreX(), area.getCentreY()));
    g.setColour (c.withAlpha (0.9f));
    g.drawRoundedRectangle (area, 4.0f, 2.0f);
    g.drawRoundedRectangle (area.reduced (3.0f), 3.0f, 0.8f);
    g.setFont (type (13.0f, true));
    g.drawText (text, area, juce::Justification::centred);
}

//==============================================================================
// look & feel
//==============================================================================
CaseFileLookAndFeel::CaseFileLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId,            theme::field);
    setColour (juce::PopupMenu::textColourId,                  theme::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::manila);
    setColour (juce::PopupMenu::highlightedTextColourId,       theme::ink);
    setColour (juce::ScrollBar::thumbColourId,                 theme::manila.darker (0.2f));
    setColour (juce::ListBox::backgroundColourId,              theme::field);
    setColour (juce::TabbedComponent::backgroundColourId,      theme::desk);
    setColour (juce::TabbedComponent::outlineColourId,         juce::Colours::transparentBlack);
}

juce::Font CaseFileLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return theme::type (13.5f, true);
}

void CaseFileLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    auto base = b.findColour (juce::TextButton::buttonColourId);
    if (down) base = base.darker (0.12f);
    else if (over && b.isEnabled()) base = base.brighter (0.05f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    auto border = b.findColour (juce::TextButton::textColourOffId)
                      .withAlpha (b.isEnabled() ? 0.85f : 0.25f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 4.0f, 1.6f);
}

void CaseFileLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f, 0.0f).withTrimmedTop (3.0f);
    const bool front = b.isFrontTab();

    juce::Path tab;   // folder-tab: rounded top corners, open bottom
    tab.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 8.0f,
                             7.0f, 7.0f, true, true, false, false);
    g.setColour (front ? theme::paper : theme::manila.darker (over ? 0.02f : 0.14f));
    g.fillPath (tab);
    g.setColour (theme::ink.withAlpha (front ? 0.6f : 0.35f));
    g.strokePath (tab, juce::PathStrokeType (1.0f));

    g.setColour (front ? theme::ink : theme::ink.withAlpha (0.6f));
    g.setFont (theme::type (12.0f, front));
    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().withTrimmedTop (3),
                juce::Justification::centred);
}

int CaseFileLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    return theme::type (12.0f, true).getStringWidth (b.getButtonText().toUpperCase()) + 30;
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
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_detective47_dust1200_BLUE_png,
                                            BinaryData::logo_detective47_dust1200_BLUE_pngSize);

    timerLabel.setFont (theme::mono (60.0f, true));
    timerLabel.setColour (juce::Label::textColourId, theme::ink);
    timerLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (timerLabel);

    statusLabel.setFont (theme::type (15.0f, true));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    contextLabel.setFont (theme::type (12.0f));
    contextLabel.setColour (juce::Label::textColourId, theme::inkDim);
    contextLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (contextLabel);

    theme::styleButton (punchInBtn,     theme::approve);
    theme::styleButton (pauseResumeBtn, theme::brass);
    theme::styleButton (breakBtn,       theme::brass);
    theme::styleButton (punchOutBtn,    theme::stamp);
    punchInBtn.onClick  = [this] { proc.clockIn(); refresh(); };
    pauseResumeBtn.onClick = [this]
    {
        if (proc.clockState() == ClockerProcessor::ClockState::running) proc.pauseTimer();
        else                                                            proc.resumeTimer();
        refresh();
    };
    breakBtn.onClick = [this]
    {
        if (proc.isOnBreak()) proc.endBreak();
        else { proc.startBreak (notesEd.getText().trim()); notesEd.clear(); }
        refresh();
    };
    punchOutBtn.onClick = [this] { proc.clockOut (notesEd.getText().trim()); notesEd.clear(); refresh(); };
    for (auto* b : { &punchInBtn, &pauseResumeBtn, &breakBtn, &punchOutBtn })
        addAndMakeVisible (b);

    billableToggle.setColour (juce::ToggleButton::textColourId, theme::ink);
    billableToggle.setColour (juce::ToggleButton::tickColourId, theme::stamp);
    billableToggle.setColour (juce::ToggleButton::tickDisabledColourId, theme::inkDim);
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

    theme::caption (notesCap, "FIELD NOTES -- ATTACHED TO THIS ENTRY");
    addAndMakeVisible (notesCap);
    theme::styleEditor (notesEd, true);
    addAndMakeVisible (notesEd);

    theme::caption (manualCap, "LATE ENTRY -- ADD TIME BY HAND");
    addAndMakeVisible (manualCap);
    for (auto* e : { &manualHoursEd, &manualMinsEd })
    {
        theme::styleEditor (*e);
        e->setFont (theme::mono (15.0f));
        e->setInputRestrictions (3, "0123456789");
        e->setSelectAllWhenFocused (true);
        addAndMakeVisible (e);
    }
    manualHoursEd.setTextToShowWhenEmpty ("H", theme::inkDim);
    manualMinsEd.setTextToShowWhenEmpty ("M", theme::inkDim);
    manualHoursEd.setExplicitFocusOrder (1);
    manualMinsEd.setExplicitFocusOrder (2);

    colonLabel.setText (":", juce::dontSendNotification);
    colonLabel.setFont (theme::mono (18.0f, true));
    colonLabel.setColour (juce::Label::textColourId, theme::ink);
    colonLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (colonLabel);

    theme::styleButton (manualAddBtn, theme::ink);
    manualAddBtn.onClick = [this]
    {
        addManualMinutes ((juce::int64) manualHoursEd.getText().getIntValue() * 60
                          + manualMinsEd.getText().getIntValue());
        manualHoursEd.clear();
        manualMinsEd.clear();
    };
    addAndMakeVisible (manualAddBtn);

    add15Btn.onClick = [this] { addManualMinutes (15); };
    add30Btn.onClick = [this] { addManualMinutes (30); };
    add60Btn.onClick = [this] { addManualMinutes (60); };
    for (auto* b : { &add15Btn, &add30Btn, &add60Btn })
    {
        theme::styleButton (*b, theme::inkDim.withAlpha (1.0f));
        addAndMakeVisible (b);
    }

    refresh();
    startTimer (500);
}

void ClockTab::addManualMinutes (juce::int64 minutes)
{
    if (minutes <= 0) return;
    proc.addManualEntry (minutes * 60000, billableToggle.getToggleState(),
                         juce::jmax (0, typeBox.getSelectedId() - 1),
                         notesEd.getText().trim());
    notesEd.clear();
}

void ClockTab::refresh()
{
    const auto st = proc.clockState();
    const bool decimal = (int) proc.settings().getProperty (ids::timeFormat, 0) == 1;
    const auto ms = proc.elapsedMs();
    timerLabel.setText (decimal ? juce::String (ms / 3600000.0, 2) + " h" : formatClock (ms),
                        juce::dontSendNotification);

    if (st == ClockerProcessor::ClockState::running && proc.isOnBreak())
    {
        statusLabel.setText ("ON BREAK -- OFF THE RECORD", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, theme::brass);
    }
    else if (st == ClockerProcessor::ClockState::running)
    {
        statusLabel.setText ("ON THE CASE -- CLOCKED IN", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, theme::approve);
    }
    else if (st == ClockerProcessor::ClockState::paused)
    {
        statusLabel.setText ("PAUSED", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, theme::brass);
    }
    else
    {
        statusLabel.setText ("OFF THE CLOCK", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    }

    auto proj = proc.project();
    juce::String ctx ("CASE: ");
    auto client = proj.getProperty (ids::client).toString().trim();
    auto song   = proj.getProperty (ids::song).toString().trim();
    ctx << (client.isNotEmpty() ? client : juce::String ("UNASSIGNED"));
    if (song.isNotEmpty()) ctx << "  //  " << song;
    ctx << "  //  " << (proc.activeBillable() ? "BILLABLE" : "NON-BILLABLE")
        << "  //  " << sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, proc.activeType())];
    contextLabel.setText (ctx, juce::dontSendNotification);

    const bool idle = (st == ClockerProcessor::ClockState::idle);
    punchInBtn.setEnabled (idle);
    pauseResumeBtn.setEnabled (! idle && ! proc.isOnBreak());
    pauseResumeBtn.setButtonText (st == ClockerProcessor::ClockState::paused ? "RESUME" : "PAUSE");
    breakBtn.setEnabled (! idle);
    breakBtn.setButtonText (proc.isOnBreak() ? "BACK TO CASE" : "BREAK");
    punchOutBtn.setEnabled (! idle);

    if (billableToggle.getToggleState() != proc.activeBillable())
        billableToggle.setToggleState (proc.activeBillable(), juce::dontSendNotification);
    if (typeBox.getSelectedId() != proc.activeType() + 1)
        typeBox.setSelectedId (proc.activeType() + 1, juce::dontSendNotification);
}

void ClockTab::paint (juce::Graphics& g)
{
    theme::paintPaper (g, getLocalBounds());

    // ghosted Detective 47 badge behind everything, like a watermark
    if (logo.isValid())
    {
        const float sz = 240.0f;
        juce::Rectangle<float> wm ((float) getWidth() - sz - 24.0f,
                                   (float) getHeight() - sz - 12.0f, sz, sz);
        g.setOpacity (0.07f);
        g.drawImage (logo, wm, juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    // the timecard
    auto card = getLocalBounds().reduced (22).removeFromTop (168).toFloat();
    g.setColour (theme::field);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (theme::ink.withAlpha (0.55f));
    g.drawRoundedRectangle (card, 6.0f, 1.4f);
    const float dash[] = { 4.0f, 3.0f };
    g.drawDashedLine (juce::Line<float> (card.getX() + 8, card.getY() + 24,
                                         card.getRight() - 8, card.getY() + 24),
                      dash, 2, 0.8f);
    g.setFont (theme::type (11.0f, true));
    g.setColour (theme::brass);
    g.drawText ("OFFICIAL TIMECARD", card.reduced (12, 5).removeFromTop (16),
                juce::Justification::centredLeft);
    g.drawText (juce::Time::getCurrentTime().formatted ("%b %d %Y").toUpperCase(),
                card.reduced (12, 5).removeFromTop (16), juce::Justification::centredRight);
}

void ClockTab::resized()
{
    auto r = getLocalBounds().reduced (22);

    auto card = r.removeFromTop (168);
    card.removeFromTop (26);
    timerLabel.setBounds (card.removeFromTop (86));
    statusLabel.setBounds (card.removeFromTop (26));
    contextLabel.setBounds (card.removeFromTop (20));

    r.removeFromTop (14);
    auto btns = r.removeFromTop (44);
    const int bw = (btns.getWidth() - 3 * 10) / 4;
    punchInBtn.setBounds     (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    pauseResumeBtn.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    breakBtn.setBounds       (btns.removeFromLeft (bw)); btns.removeFromLeft (10);
    punchOutBtn.setBounds    (btns);

    r.removeFromTop (12);
    auto row = r.removeFromTop (26);
    billableToggle.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    typeCap.setBounds (row.removeFromLeft (94));
    typeBox.setBounds (row.removeFromLeft (200).withHeight (26));

    r.removeFromTop (10);
    notesCap.setBounds (r.removeFromTop (16));
    notesEd.setBounds (r.removeFromTop (54));

    r.removeFromTop (10);
    manualCap.setBounds (r.removeFromTop (16));
    r.removeFromTop (2);
    auto mrow = r.removeFromTop (30);
    manualHoursEd.setBounds (mrow.removeFromLeft (58));
    colonLabel.setBounds    (mrow.removeFromLeft (14));
    manualMinsEd.setBounds  (mrow.removeFromLeft (58));
    mrow.removeFromLeft (10);
    manualAddBtn.setBounds  (mrow.removeFromLeft (86));
    mrow.removeFromLeft (16);
    add15Btn.setBounds (mrow.removeFromLeft (62)); mrow.removeFromLeft (6);
    add30Btn.setBounds (mrow.removeFromLeft (62)); mrow.removeFromLeft (6);
    add60Btn.setBounds (mrow.removeFromLeft (56));
}

//==============================================================================
// TIME LOG TAB
//==============================================================================
TimeLogTab::TimeLogTab (ClockerProcessor& p) : ClockerTab (p)
{
    list.setColour (juce::ListBox::backgroundColourId, theme::field);
    list.setColour (juce::ListBox::outlineColourId, theme::ink.withAlpha (0.4f));
    list.setOutlineThickness (1);
    list.setRowHeight (24);
    addAndMakeVisible (list);

    theme::caption (editCap, "SELECTED ENTRY");
    theme::caption (hoursCap, "H");
    theme::caption (minsCap,  "M");
    theme::caption (notesCap2, "NOTES");
    for (auto* c : { &editCap, &hoursCap, &minsCap, &notesCap2 })
        addAndMakeVisible (c);

    for (auto* e : { &hoursEd, &minsEd })
    {
        theme::styleEditor (*e);
        e->setFont (theme::mono (14.0f));
        e->setInputRestrictions (3, "0123456789");
        e->setSelectAllWhenFocused (true);
        addAndMakeVisible (e);
    }
    theme::styleEditor (notesEd);
    addAndMakeVisible (notesEd);

    billableToggle.setColour (juce::ToggleButton::textColourId, theme::ink);
    billableToggle.setColour (juce::ToggleButton::tickColourId, theme::stamp);
    addAndMakeVisible (billableToggle);

    fillSessionTypes (typeBox);
    theme::styleCombo (typeBox);
    addAndMakeVisible (typeBox);

    theme::styleButton (saveBtn, theme::approve);
    saveBtn.onClick = [this] { saveSelected(); };
    addAndMakeVisible (saveBtn);
    theme::styleButton (deleteBtn, theme::stamp);
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

    if (selected)             g.fillAll (theme::manila.withAlpha (0.55f));
    else if ((row & 1) == 0)  g.fillAll (theme::ink.withAlpha (0.04f));
    g.setColour (theme::ink.withAlpha (0.12f));            // ledger baseline
    g.fillRect (0, h - 1, w, 1);

    const bool billable = e.getProperty (ids::billable);
    g.setFont (theme::type (12.0f));
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (8, 0);

    g.setColour (theme::inkDim);
    g.drawText (formatDate (e.getProperty (ids::start)), r.removeFromLeft (92), juce::Justification::centredLeft);
    g.setColour (theme::ink);
    g.drawText (sessionTypes()[juce::jlimit (0, sessionTypes().size() - 1, (int) e.getProperty (ids::type))],
                r.removeFromLeft (120), juce::Justification::centredLeft);
    g.setColour (billable ? theme::approve : theme::stamp);
    g.setFont (theme::type (11.0f, true));
    g.drawText (billable ? "BILL" : "N/B", r.removeFromLeft (42), juce::Justification::centredLeft);
    g.setColour (theme::ink);
    g.setFont (theme::mono (12.0f, true));
    g.drawText (formatDuration (e.getProperty (ids::durationMs)), r.removeFromLeft (72), juce::Justification::centredLeft);
    g.setColour (theme::inkDim);
    g.setFont (theme::type (12.0f));
    juce::String n = e.getProperty (ids::notes).toString().replace ("\n", " ");
    if ((bool) e.getProperty (ids::manual)) n = "[late entry] " + n;
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

void TimeLogTab::resized()
{
    auto r = getLocalBounds().reduced (22);
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

    theme::styleButton (resetBtn, theme::stamp);
    resetBtn.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
            "Close Case / New Project",
            "Clear all time entries and project details?\nFile the report (Export tab) first if you need it.",
            "Close Case", "Cancel", this,
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

void ClientTab::resized()
{
    auto r = getLocalBounds().reduced (22);
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

    theme::styleButton (refreshBtn, theme::brass);
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

    s << "\nDETECTIVE'S NOTES\n-----------------\n";
    if (t.totalMs <= 0)
        s << "No time logged yet. Punch in and the numbers will follow.\n";
    else
    {
        if (t.projectType == 1 && t.flatFee > 0.0)
            s << "This case has taken " << formatDuration (t.totalMs) << ". At a "
              << formatMoney (t.flatFee) << " flat rate, the effective hourly rate is "
              << formatMoney (t.effectiveRate) << "/hr.\n";
        const auto revMs = t.typeMs[6] + t.typeMs[7];  // Revision + Recall
        if (revMs > 0 && revMs * 100 / t.totalMs >= 15)
            s << "Revisions/recalls are " << juce::String ((int) (revMs * 100 / t.totalMs))
              << "% of total time. Consider revision limits or hourly revisions.\n";
        if (t.nonBillableMs > 0 && t.nonBillableMs * 100 / t.totalMs >= 20)
            s << juce::String ((int) (t.nonBillableMs * 100 / t.totalMs))
              << "% of tracked time is non-billable. Look at where those hours go.\n";
        if (t.typeMs[11] > 0)
            s << "Breaks: " << formatDuration (t.typeMs[11]) << " (tracked, not billed).\n";
    }

    view.setText (s, false);
}

void ProfitTab::resized()
{
    auto r = getLocalBounds().reduced (22);
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

    theme::styleButton (copyBtn, theme::brass);
    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (buildCurrent());
        savedLabel.setText ("Copied to clipboard.", juce::dontSendNotification);
    };
    addAndMakeVisible (copyBtn);

    theme::styleButton (saveBtn, theme::approve);
    saveBtn.onClick = [this]
    {
        static const char* exts[] = { ".txt", ".md", ".csv", ".json" };
        auto folder = ClockerProcessor::clockerFolder();
        folder.createDirectory();
        auto file = folder.getChildFile (proc.exportBaseName()
                                         + exts[juce::jlimit (0, 3, formatBox.getSelectedId() - 1)]);
        if (file.replaceWithText (buildCurrent()))
        {
            savedLabel.setText ("Filed: " + file.getFullPathName(), juce::dontSendNotification);
            file.revealToUser();
        }
        else
            savedLabel.setText ("Could not write file.", juce::dontSendNotification);
    };
    addAndMakeVisible (saveBtn);

    savedLabel.setFont (theme::type (11.0f));
    savedLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (savedLabel);

    folderLabel.setFont (theme::type (11.0f));
    folderLabel.setColour (juce::Label::textColourId, theme::inkDim);
    folderLabel.setText ("Case archive: " + ClockerProcessor::clockerFolder().getFullPathName(),
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

void ExportTab::resized()
{
    auto r = getLocalBounds().reduced (22);
    auto top = r.removeFromTop (30);
    formatBox.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (10);
    copyBtn.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (10);
    saveBtn.setBounds (top.removeFromLeft (170));
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
    const char* names[4] = { "DEFAULT HOURLY RATE ($/HR)", "BILLING ROUNDING",
                             "TIMER DISPLAY", "DEFAULT SESSION TYPE" };
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

    roundingBox.addItem ("Exact time",                  1);
    roundingBox.addItem ("Round to nearest 5 minutes",  2);
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

    defaultBillableToggle.setColour (juce::ToggleButton::textColourId, theme::ink);
    defaultBillableToggle.setColour (juce::ToggleButton::tickColourId, theme::stamp);
    defaultBillableToggle.onClick = [this]
    { proc.settings().setProperty (ids::defaultBillable, defaultBillableToggle.getToggleState(), nullptr); };
    addAndMakeVisible (defaultBillableToggle);

    folderLabel.setFont (theme::type (11.0f));
    folderLabel.setColour (juce::Label::textColourId, theme::inkDim);
    folderLabel.setText ("Reports file to: " + ClockerProcessor::clockerFolder().getFullPathName(),
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

void SettingsTab::resized()
{
    auto r = getLocalBounds().reduced (22);
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
    setLookAndFeel (&lnf);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_detective47_dust1200_BLUE_png,
                                            BinaryData::logo_detective47_dust1200_BLUE_pngSize);

    tabs.setTabBarDepth (34);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, theme::desk);
    tabs.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    tabs.addTab ("Clock",            theme::paper, new ClockTab (proc),    true);
    tabs.addTab ("Time Log",         theme::paper, new TimeLogTab (proc),  true);
    tabs.addTab ("Client / Project", theme::paper, new ClientTab (proc),   true);
    tabs.addTab ("Profitability",    theme::paper, new ProfitTab (proc),   true);
    tabs.addTab ("Export",           theme::paper, new ExportTab (proc),   true);
    tabs.addTab ("Settings",         theme::paper, new SettingsTab (proc), true);
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
    setLookAndFeel (nullptr);
}

void ClockerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* t = dynamic_cast<ClockerTab*> (tabs.getTabContentComponent (i)))
            if (t->isVisible())
                t->refresh();
    timerCallback();
}

void ClockerEditor::timerCallback()
{
    switch (proc.clockState())
    {
        case ClockerProcessor::ClockState::running:
            statusText   = (proc.isOnBreak() ? "ON BREAK  " : "ON THE CASE  ") + formatClock (proc.elapsedMs());
            statusColour = proc.isOnBreak() ? theme::amber : juce::Colour (0xff8fce8f);
            break;
        case ClockerProcessor::ClockState::paused:
            statusText   = "PAUSED  " + formatClock (proc.elapsedMs());
            statusColour = theme::amber;
            break;
        default:
            statusText   = "OFF THE CLOCK";
            statusColour = juce::Colours::white.withAlpha (0.45f);
            break;
    }
    repaint (0, 0, getWidth(), 52);
}

void ClockerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::desk);

    auto header = getLocalBounds().removeFromTop (52);
    g.setColour (theme::leather);
    g.fillRect (header);
    g.setColour (theme::amber.withAlpha (0.5f));
    g.fillRect (header.withTrimmedTop (header.getHeight() - 2));

    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (10.0f, 4.0f, 44.0f, 44.0f),
                     juce::RectanglePlacement::centred);

    g.setColour (theme::amber);
    g.setFont (theme::type (17.0f, true));
    g.drawText ("SOUND DETECTIVE  //  CLOCKER",
                header.withTrimmedLeft (62).withTrimmedRight (250),
                juce::Justification::centredLeft);
    g.setColour (theme::amber.withAlpha (0.55f));
    g.setFont (theme::type (9.5f));
    g.drawText ("CASE FILE NO. 47", header.withTrimmedLeft (63).withTrimmedTop (30),
                juce::Justification::topLeft);

    theme::drawStamp (g, statusText, statusColour,
                      juce::Rectangle<float> ((float) getWidth() - 236.0f, 11.0f, 220.0f, 30.0f),
                      -2.5f);
}

void ClockerEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (52);
    tabs.setBounds (r);
}
