#include "PluginEditor.h"
#include "BinaryData.h"

using namespace casefile;

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

void theme::styleToggle (juce::ToggleButton& t)
{
    t.setColour (juce::ToggleButton::textColourId, ink);
    t.setColour (juce::ToggleButton::tickColourId, stamp);
    t.setColour (juce::ToggleButton::tickDisabledColourId, inkDim);
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

void theme::drawWatermark (juce::Graphics& g, const juce::Image& logo,
                           juce::Rectangle<int> area)
{
    if (! logo.isValid()) return;
    const float size = (float) juce::jmin (area.getWidth(), area.getHeight()) * 0.55f;
    juce::Rectangle<float> r ((float) area.getRight() - size - 18.0f,
                              (float) area.getBottom() - size - 12.0f, size, size);
    g.setOpacity (0.07f);   // detective silhouette as a subtle stamp, never in the way
    g.drawImage (logo, r, juce::RectanglePlacement::centred);
    g.setOpacity (1.0f);
}

juce::Image theme::detectiveLogo()
{
    return juce::ImageCache::getFromMemory (BinaryData::logo_detective47_dust1200_BLUE_png,
                                            BinaryData::logo_detective47_dust1200_BLUE_pngSize);
}

void theme::bindText (juce::TextEditor& ed, const juce::Identifier& prop,
                      std::function<juce::ValueTree()> target)
{
    ed.onTextChange = [&ed, prop, target = std::move (target)]
    {
        auto t = target();
        if (t.isValid())
            t.setProperty (prop, ed.getText(), nullptr);
    };
}

void theme::setIfChanged (juce::TextEditor& ed, const juce::String& v)
{
    if (! ed.hasKeyboardFocus (true) && ed.getText() != v)
        ed.setText (v, false);
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
    return theme::type (12.5f, true);
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
    g.setFont (theme::type (11.5f, front));
    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().withTrimmedTop (3),
                juce::Justification::centred);
}

int CaseFileLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    return theme::type (11.5f, true).getStringWidth (b.getButtonText().toUpperCase()) + 24;
}

//==============================================================================
// BRIEF TAB
//==============================================================================
void BriefTab::Content::paint (juce::Graphics& g)
{
    theme::paintPaper (g, getLocalBounds());
    theme::drawWatermark (g, logo, getLocalBounds());
}

BriefTab::BriefTab (CaseFileProcessor& p) : CaseTab (p)
{
    content.logo = theme::detectiveLogo();

    const char* names[18] = {
        "SONG TITLE", "ARTIST", "PRODUCER", "MIXER", "GENRE", "TEMPO", "KEY",
        "SAMPLE RATE", "BIT DEPTH", "MIX STAGE", "DEADLINE",
        "MAIN REFERENCE TRACKS", "EMOTIONAL TARGET", "MAIN MIX GOAL",
        "BIGGEST PROBLEM RIGHT NOW", "CLIENT NOTES", "DELIVERY REQUIREMENTS",
        "CASE INTERVIEW — ANSWER UNDER EACH QUESTION" };
    for (int i = 0; i < 18; ++i)
    {
        theme::caption (caps[i], names[i]);
        content.addAndMakeVisible (caps[i]);
    }

    auto briefTree = [this] { return proc.brief(); };
    auto single = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed);
        theme::bindText (ed, prop, briefTree);
        content.addAndMakeVisible (ed);
    };
    auto multi = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed, true);
        theme::bindText (ed, prop, briefTree);
        content.addAndMakeVisible (ed);
    };

    single (titleEd,    ids::songTitle);
    single (artistEd,   ids::artist);
    single (producerEd, ids::producer);
    single (mixerEd,    ids::mixer);
    single (genreEd,    ids::genre);
    single (tempoEd,    ids::tempo);
    single (keyEd,      ids::songKey);
    single (srEd,       ids::sampleRateTxt);
    single (bdEd,       ids::bitDepthTxt);
    single (deadlineEd, ids::deadline);
    single (emotionalEd, ids::emotionalTarget);
    single (goalEd,      ids::mixGoal);
    multi (refsEd,        ids::mainRefs);
    multi (problemEd,     ids::biggestProblem);
    multi (clientNotesEd, ids::clientNotes);
    multi (deliveryEd,    ids::deliveryReqs);
    multi (interviewEd,   ids::interview);

    for (int i = 0; i < mixStages().size(); ++i)
        stageBox.addItem (mixStages()[i], i + 1);
    theme::styleCombo (stageBox);
    stageBox.onChange = [this]
    {
        if (stageBox.getSelectedId() > 0)
            proc.brief().setProperty (ids::mixStage, stageBox.getSelectedId() - 1, nullptr);
    };
    content.addAndMakeVisible (stageBox);

    content.layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (22, 18);
        auto left = r.removeFromLeft ((r.getWidth() - 24) / 2);
        r.removeFromLeft (24);
        auto right = r;

        placeField (left, caps[0], titleEd);
        placeField (left, caps[1], artistEd);
        placeField (left, caps[2], producerEd);
        placeField (left, caps[3], mixerEd);
        placeField (left, caps[4], genreEd);

        auto row = left.removeFromTop (48);
        auto c1 = row.removeFromLeft ((row.getWidth() - 12) / 2);
        row.removeFromLeft (12);
        placeField (c1,  caps[5], tempoEd);
        placeField (row, caps[6], keyEd);

        row = left.removeFromTop (48);
        c1 = row.removeFromLeft ((row.getWidth() - 12) / 2);
        row.removeFromLeft (12);
        placeField (c1,  caps[7], srEd);
        placeField (row, caps[8], bdEd);

        placeField (left, caps[9],  stageBox);
        placeField (left, caps[10], deadlineEd);
        placeField (left, caps[12], emotionalEd);
        placeField (left, caps[13], goalEd);

        caps[11].setBounds (right.removeFromTop (14));
        refsEd.setBounds (right.removeFromTop (54));
        right.removeFromTop (8);
        caps[14].setBounds (right.removeFromTop (14));
        problemEd.setBounds (right.removeFromTop (54));
        right.removeFromTop (8);
        caps[15].setBounds (right.removeFromTop (14));
        clientNotesEd.setBounds (right.removeFromTop (70));
        right.removeFromTop (8);
        caps[16].setBounds (right.removeFromTop (14));
        deliveryEd.setBounds (right.removeFromTop (70));
        right.removeFromTop (8);
        caps[17].setBounds (right.removeFromTop (14));
        interviewEd.setBounds (right.withTrimmedBottom (4));
    };

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    refresh();
}

void BriefTab::paint (juce::Graphics& g) { g.fillAll (theme::paper); }

void BriefTab::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = viewport.getMaximumVisibleWidth();
    content.setSize (w, juce::jmax (640, viewport.getMaximumVisibleHeight()));
}

void BriefTab::refresh()
{
    auto b = proc.brief();
    theme::setIfChanged (titleEd,    b.getProperty (ids::songTitle));
    theme::setIfChanged (artistEd,   b.getProperty (ids::artist));
    theme::setIfChanged (producerEd, b.getProperty (ids::producer));
    theme::setIfChanged (mixerEd,    b.getProperty (ids::mixer));
    theme::setIfChanged (genreEd,    b.getProperty (ids::genre));
    theme::setIfChanged (tempoEd,    b.getProperty (ids::tempo));
    theme::setIfChanged (keyEd,      b.getProperty (ids::songKey));
    theme::setIfChanged (srEd,       b.getProperty (ids::sampleRateTxt));
    theme::setIfChanged (bdEd,       b.getProperty (ids::bitDepthTxt));
    theme::setIfChanged (deadlineEd, b.getProperty (ids::deadline));
    theme::setIfChanged (refsEd,     b.getProperty (ids::mainRefs));
    theme::setIfChanged (emotionalEd, b.getProperty (ids::emotionalTarget));
    theme::setIfChanged (goalEd,      b.getProperty (ids::mixGoal));
    theme::setIfChanged (problemEd,   b.getProperty (ids::biggestProblem));
    theme::setIfChanged (clientNotesEd, b.getProperty (ids::clientNotes));
    theme::setIfChanged (deliveryEd,    b.getProperty (ids::deliveryReqs));
    theme::setIfChanged (interviewEd,   b.getProperty (ids::interview));
    stageBox.setSelectedId (safeIndex (b.getProperty (ids::mixStage), mixStages()) + 1,
                            juce::dontSendNotification);
}

//==============================================================================
// SONG MAP TAB
//==============================================================================
SongMapTab::SongMapTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[19] = {
        "KEY", "SCALE / MODE", "TEMPO", "TIME SIG", "MAIN HOOK", "LOW-END RELATIONSHIP",
        "CHORD PROGRESSION (FULL SONG / PER SECTION)", "SONG NOTES",
        "SECTION MAP", "SECTION NAME", "START TIME", "END TIME", "START BAR", "END BAR",
        "ENERGY", "SECTION CHORDS", "MIX GOAL", "PRODUCTION GOAL", "SECTION NOTES" };
    for (int i = 0; i < 19; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    auto mapTree = [this] { return proc.songMap(); };
    auto secTree = [this] { return selected(); };

    auto single = [&] (juce::TextEditor& ed, const juce::Identifier& prop, bool section)
    {
        theme::styleEditor (ed);
        theme::bindText (ed, prop, section ? (std::function<juce::ValueTree()>) secTree
                                           : (std::function<juce::ValueTree()>) mapTree);
        addAndMakeVisible (ed);
    };
    auto multi = [&] (juce::TextEditor& ed, const juce::Identifier& prop, bool section)
    {
        theme::styleEditor (ed, true);
        theme::bindText (ed, prop, section ? (std::function<juce::ValueTree()>) secTree
                                           : (std::function<juce::ValueTree()>) mapTree);
        addAndMakeVisible (ed);
    };

    single (keyEd,    ids::songKey,   false);
    single (scaleEd,  ids::scaleMode, false);
    single (tempoEd,  ids::tempo,     false);
    single (sigEd,    ids::timeSig,   false);
    single (hookEd,   ids::mainHook,  false);
    single (lowEndEd, ids::lowEndRel, false);
    multi (chordsEd,    ids::chordProgression, false);
    multi (songNotesEd, ids::songNotes,        false);

    single (secNameEd,   ids::name,      true);
    single (startTimeEd, ids::startTime, true);
    single (endTimeEd,   ids::endTime,   true);
    single (startBarEd,  ids::startBar,  true);
    single (endBarEd,    ids::endBar,    true);
    single (secChordsEd, ids::chords,    true);
    single (secMixGoalEd, ids::sectionMixGoal, true);
    single (secProdGoalEd, ids::prodGoal, true);
    multi (secNotesEd, ids::notes, true);

    secNameEd.onTextChange = [this]
    {
        auto s = selected();
        if (s.isValid()) { s.setProperty (ids::name, secNameEd.getText(), nullptr); list.repaint(); }
    };
    startTimeEd.setTextToShowWhenEmpty ("1:02", theme::inkDim);
    endTimeEd.setTextToShowWhenEmpty ("1:31", theme::inkDim);
    startBarEd.setTextToShowWhenEmpty ("33", theme::inkDim);
    endBarEd.setTextToShowWhenEmpty ("48", theme::inkDim);

    for (int i = 0; i < energyLevels().size(); ++i)
        energyBox.addItem (energyLevels()[i], i + 1);
    theme::styleCombo (energyBox);
    energyBox.onChange = [this]
    {
        auto s = selected();
        if (s.isValid() && energyBox.getSelectedId() > 0)
        { s.setProperty (ids::energy, energyBox.getSelectedId() - 1, nullptr); list.repaint(); }
    };
    addAndMakeVisible (energyBox);

    for (int i = 0; i < sectionPresets().size(); ++i)
        presetBox.addItem (sectionPresets()[i], i + 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (presetBox);
    addAndMakeVisible (presetBox);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        auto name = sectionPresets()[juce::jmax (0, presetBox.getSelectedId() - 1)];
        proc.addSection (name == "Custom Section" ? "Section" : name);
        list.updateContent();
        list.selectRow (proc.sections().getNumChildren() - 1);
        // step the preset combo forward so Intro → Verse 1 → ... flows naturally
        presetBox.setSelectedId (juce::jmin (presetBox.getSelectedId() + 1,
                                             sectionPresets().size()),
                                 juce::dontSendNotification);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto s = selected();
        if (s.isValid()) { proc.removeChild (proc.sections(), s); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    theme::styleButton (copyStartBtn, theme::brass);
    copyStartBtn.onClick = [this]
    {
        auto s = selected();
        if (! s.isValid()) return;
        auto txt = s.getProperty (ids::startTime).toString().trim();
        const auto bar = s.getProperty (ids::startBar).toString().trim();
        if (txt.isEmpty()) txt = bar.isNotEmpty() ? "bar " + bar : juce::String();
        if (txt.isNotEmpty()) juce::SystemClipboard::copyTextToClipboard (txt);
    };
    addAndMakeVisible (copyStartBtn);

    // VST3 gives plugins no standard way to move the host transport — the data
    // structure is ready, the buttons ship disabled until a safe path exists.
    for (auto* b : { &goToBtn, &loopBtn })
    {
        theme::styleButton (*b, theme::inkDim.withAlpha (1.0f));
        b->setEnabled (false);
        b->setTooltip ("DAW transport control is not available to VST3 plugins yet — coming later.");
        addAndMakeVisible (b);
    }

    list.setRowHeight (24);
    addAndMakeVisible (list);

    refresh();
}

int SongMapTab::getNumRows() { return proc.sections().getNumChildren(); }

juce::ValueTree SongMapTab::selected() const
{
    return proc.sections().getChild (list.getSelectedRow());
}

void SongMapTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto s = proc.sections().getChild (row);
    if (! s.isValid()) return;
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }

    g.setColour (theme::ink);
    g.setFont (theme::type (12.5f, sel));
    g.drawText (s.getProperty (ids::name).toString(), 8, 0, w / 3 - 8, h,
                juce::Justification::centredLeft);

    juce::String meta;
    const auto t0 = s.getProperty (ids::startTime).toString().trim();
    const auto t1 = s.getProperty (ids::endTime).toString().trim();
    const auto b0 = s.getProperty (ids::startBar).toString().trim();
    const auto b1 = s.getProperty (ids::endBar).toString().trim();
    if (t0.isNotEmpty() || t1.isNotEmpty()) meta << t0 << " - " << t1 << "   ";
    if (b0.isNotEmpty() || b1.isNotEmpty()) meta << "bars " << b0 << "-" << b1 << "   ";
    meta << energyLevels()[safeIndex (s.getProperty (ids::energy), energyLevels())];
    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.5f));
    g.drawText (meta, w / 3, 0, w * 2 / 3 - 8, h, juce::Justification::centredRight);
}

void SongMapTab::refreshDetail()
{
    auto s = selected();
    const bool has = s.isValid();
    for (auto* e : { &secNameEd, &startTimeEd, &endTimeEd, &startBarEd, &endBarEd,
                     &secChordsEd, &secMixGoalEd, &secProdGoalEd, &secNotesEd })
        e->setEnabled (has);
    energyBox.setEnabled (has);
    copyStartBtn.setEnabled (has);

    theme::setIfChanged (secNameEd,   has ? s.getProperty (ids::name).toString() : juce::String());
    theme::setIfChanged (startTimeEd, has ? s.getProperty (ids::startTime).toString() : juce::String());
    theme::setIfChanged (endTimeEd,   has ? s.getProperty (ids::endTime).toString() : juce::String());
    theme::setIfChanged (startBarEd,  has ? s.getProperty (ids::startBar).toString() : juce::String());
    theme::setIfChanged (endBarEd,    has ? s.getProperty (ids::endBar).toString() : juce::String());
    theme::setIfChanged (secChordsEd, has ? s.getProperty (ids::chords).toString() : juce::String());
    theme::setIfChanged (secMixGoalEd, has ? s.getProperty (ids::sectionMixGoal).toString() : juce::String());
    theme::setIfChanged (secProdGoalEd, has ? s.getProperty (ids::prodGoal).toString() : juce::String());
    theme::setIfChanged (secNotesEd,  has ? s.getProperty (ids::notes).toString() : juce::String());
    energyBox.setSelectedId (has ? safeIndex (s.getProperty (ids::energy), energyLevels()) + 1 : 0,
                             juce::dontSendNotification);
}

void SongMapTab::refresh()
{
    auto m = proc.songMap();
    theme::setIfChanged (keyEd,    m.getProperty (ids::songKey));
    theme::setIfChanged (scaleEd,  m.getProperty (ids::scaleMode));
    theme::setIfChanged (tempoEd,  m.getProperty (ids::tempo));
    theme::setIfChanged (sigEd,    m.getProperty (ids::timeSig));
    theme::setIfChanged (hookEd,   m.getProperty (ids::mainHook));
    theme::setIfChanged (lowEndEd, m.getProperty (ids::lowEndRel));
    theme::setIfChanged (chordsEd, m.getProperty (ids::chordProgression));
    theme::setIfChanged (songNotesEd, m.getProperty (ids::songNotes));
    list.updateContent();
    list.repaint();
    refreshDetail();
}

void SongMapTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    // song-level strip: key / scale / tempo / sig / hook / low-end
    auto row = r.removeFromTop (44);
    const int small = 86;
    auto cell = [&row] (int width) { auto c = row.removeFromLeft (width); row.removeFromLeft (10); return c; };
    { auto c = cell (small);      placeField (c, caps[0], keyEd); }
    { auto c = cell (small + 20); placeField (c, caps[1], scaleEd); }
    { auto c = cell (small);      placeField (c, caps[2], tempoEd); }
    { auto c = cell (small);      placeField (c, caps[3], sigEd); }
    const int hookW = (row.getWidth() - 10) / 2;
    { auto c = cell (hookW); placeField (c, caps[4], hookEd); }
    { auto c = row;          placeField (c, caps[5], lowEndEd); }
    r.removeFromTop (6);

    // left column: chords + song notes
    auto left = r.removeFromLeft (juce::jmax (220, r.getWidth() * 32 / 100));
    r.removeFromLeft (16);
    caps[6].setBounds (left.removeFromTop (14));
    chordsEd.setBounds (left.removeFromTop ((left.getHeight() - 30) / 2));
    left.removeFromTop (8);
    caps[7].setBounds (left.removeFromTop (14));
    songNotesEd.setBounds (left);

    // right: section map
    auto controls = r.removeFromTop (28);
    caps[8].setBounds (controls.removeFromLeft (90));
    presetBox.setBounds (controls.removeFromLeft (140));
    controls.removeFromLeft (8);
    addBtn.setBounds (controls.removeFromLeft (110));
    controls.removeFromLeft (8);
    deleteBtn.setBounds (controls.removeFromLeft (86));
    r.removeFromTop (6);

    list.setBounds (r.removeFromTop (juce::jmax (72, r.getHeight() * 24 / 100)));
    r.removeFromTop (10);

    auto detail = r;
    auto colA = detail.removeFromLeft ((detail.getWidth() - 14) / 2);
    detail.removeFromLeft (14);
    auto colB = detail;

    placeField (colA, caps[9], secNameEd);
    {
        auto tr = colA.removeFromTop (48);
        auto t1 = tr.removeFromLeft ((tr.getWidth() - 10) / 2);
        tr.removeFromLeft (10);
        placeField (t1, caps[10], startTimeEd);
        placeField (tr, caps[11], endTimeEd);
    }
    {
        auto br = colA.removeFromTop (48);
        auto b1 = br.removeFromLeft ((br.getWidth() - 10) / 2);
        br.removeFromLeft (10);
        placeField (b1, caps[12], startBarEd);
        placeField (br, caps[13], endBarEd);
    }
    {
        auto er = colA.removeFromTop (48);
        auto e1 = er.removeFromLeft ((er.getWidth() - 10) / 2);
        er.removeFromLeft (10);
        placeField (e1, caps[14], energyBox);
        er.removeFromTop (14);
        copyStartBtn.setBounds (er.removeFromTop (26));
    }
    caps[18].setBounds (colA.removeFromTop (14));
    secNotesEd.setBounds (colA.withTrimmedBottom (2));

    placeField (colB, caps[15], secChordsEd);
    placeField (colB, caps[16], secMixGoalEd);
    placeField (colB, caps[17], secProdGoalEd);
    auto futureRow = colB.removeFromTop (26);
    goToBtn.setBounds (futureRow.removeFromLeft ((futureRow.getWidth() - 8) / 2));
    futureRow.removeFromLeft (8);
    loopBtn.setBounds (futureRow);
}

//==============================================================================
// CHECKLIST TAB
//==============================================================================
ChecklistTab::ChecklistTab (CaseFileProcessor& p) : CaseTab (p)
{
    theme::caption (filterCap, "SHOW");
    theme::caption (newCap, "NEW ITEM");
    theme::caption (sectionCap, "SECTION (OPTIONAL)");
    for (auto* c : { &filterCap, &newCap, &sectionCap }) addAndMakeVisible (c);

    filterBox.addItem ("All", 1);
    for (int i = 0; i < checklistGroups().size(); ++i)
        filterBox.addItem (checklistGroups()[i], i + 2);
    filterBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (filterBox);
    filterBox.onChange = [this] { list.deselectAllRows(); list.updateContent(); list.repaint(); };
    addAndMakeVisible (filterBox);

    for (int i = 0; i < checklistGroups().size(); ++i)
        groupBox.addItem (checklistGroups()[i], i + 1);
    groupBox.setSelectedId (4, juce::dontSendNotification);   // Custom
    theme::styleCombo (groupBox);
    addAndMakeVisible (groupBox);

    theme::styleEditor (newItemEd);
    newItemEd.setTextToShowWhenEmpty ("e.g. Check 200-350 Hz buildup on bass/piano", theme::inkDim);
    addAndMakeVisible (newItemEd);
    theme::styleEditor (sectionEd);
    sectionEd.setTextToShowWhenEmpty ("Chorus 1", theme::inkDim);
    addAndMakeVisible (sectionEd);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        const auto text = newItemEd.getText().trim();
        if (text.isEmpty()) return;
        proc.addChecklistItem (text, juce::jmax (0, groupBox.getSelectedId() - 1),
                               sectionEd.getText().trim(), false);
        newItemEd.clear();
        list.updateContent();
        list.repaint();
        refresh();
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto items = visibleItems();
        const int row = list.getSelectedRow();
        if (row >= 0 && row < items.size())
        {
            proc.removeChild (proc.checklist(), items[row]);
            list.updateContent();
            refresh();
        }
    };
    addAndMakeVisible (deleteBtn);

    auto move = [this] (int delta)
    {
        auto items = visibleItems();
        const int row = list.getSelectedRow();
        if (row < 0 || row >= items.size()) return;
        auto parent = proc.checklist();
        const int idx = parent.indexOf (items[row]);
        const int to  = idx + delta;
        if (idx < 0 || to < 0 || to >= parent.getNumChildren()) return;
        parent.moveChild (idx, to, nullptr);
        list.updateContent();
        list.selectRow (juce::jlimit (0, getNumRows() - 1, row + delta));
        list.repaint();
    };
    theme::styleButton (upBtn,   theme::brass);
    theme::styleButton (downBtn, theme::brass);
    upBtn.onClick   = [move] { move (-1); };
    downBtn.onClick = [move] { move (+1); };
    addAndMakeVisible (upBtn);
    addAndMakeVisible (downBtn);

    progressLabel.setFont (theme::type (12.5f, true));
    progressLabel.setColour (juce::Label::textColourId, theme::brass);
    progressLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (progressLabel);

    list.setRowHeight (26);
    addAndMakeVisible (list);

    refresh();
}

juce::Array<juce::ValueTree> ChecklistTab::visibleItems() const
{
    juce::Array<juce::ValueTree> out;
    const int filter = filterBox.getSelectedId() - 2;   // -1 = All
    for (auto c : proc.checklist())
        if (filter < 0 || (int) c.getProperty (ids::group, 3) == filter)
            out.add (c);
    return out;
}

int ChecklistTab::getNumRows() { return visibleItems().size(); }

void ChecklistTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto items = visibleItems();
    if (row >= items.size()) return;
    auto c = items[row];
    const bool done = c.getProperty (ids::done, false);

    if (sel) { g.setColour (theme::manila.withAlpha (0.45f)); g.fillRect (0, 0, w, h); }

    // stamp-style checkbox
    juce::Rectangle<float> box (8.0f, h / 2.0f - 7.0f, 14.0f, 14.0f);
    g.setColour (done ? theme::approve : theme::inkDim);
    g.drawRect (box, 1.4f);
    if (done)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
        tick.lineTo (box.getCentreX() - 1.0f, box.getBottom() - 3.5f);
        tick.lineTo (box.getRight() - 2.0f, box.getY() + 2.5f);
        g.strokePath (tick, juce::PathStrokeType (2.0f));
    }

    auto text = c.getProperty (ids::text).toString();
    g.setColour (done ? theme::inkDim : theme::ink);
    juce::Font f = theme::type (13.0f);
    if (done) f = f.withStyle (juce::Font::italic);
    g.setFont (f);

    juce::String tags;
    const auto sec = c.getProperty (ids::section).toString().trim();
    if (sec.isNotEmpty()) tags << "[" << sec << "] ";
    if ((bool) c.getProperty (ids::fromSuspect, false)) tags << "[suspect] ";
    tags << checklistGroups()[safeIndex (c.getProperty (ids::group), checklistGroups())];

    const int tagW = 200;
    g.drawText (text, 30, 0, w - tagW - 38, h, juce::Justification::centredLeft);
    g.setColour (theme::brass.withAlpha (0.8f));
    g.setFont (theme::type (10.5f));
    g.drawText (tags, w - tagW - 6, 0, tagW, h, juce::Justification::centredRight);

    if (done)   // strike the cleared line, detective style
    {
        g.setColour (theme::inkDim.withAlpha (0.5f));
        const float tw = juce::jmin ((float) w - tagW - 38.0f,
                                     theme::type (13.0f).getStringWidthFloat (text));
        g.drawLine (30.0f, h / 2.0f, 30.0f + tw, h / 2.0f, 1.2f);
    }
}

void ChecklistTab::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    auto items = visibleItems();
    if (row < 0 || row >= items.size()) return;
    auto c = items[row];
    c.setProperty (ids::done, ! (bool) c.getProperty (ids::done, false), nullptr);
    list.repaint();
    refresh();
}

void ChecklistTab::refresh()
{
    int done = 0, total = 0;
    for (auto c : proc.checklist())
    {
        ++total;
        if ((bool) c.getProperty (ids::done, false)) ++done;
    }
    progressLabel.setText (juce::String (done) + " / " + juce::String (total) + " CLEARED",
                           juce::dontSendNotification);
    list.updateContent();
    list.repaint();
}

void ChecklistTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (44);
    auto f = top.removeFromLeft (170);
    placeField (f, filterCap, filterBox);
    top.removeFromLeft (10);
    progressLabel.setBounds (top.removeFromRight (200).withTrimmedTop (14));
    auto btns = top.withTrimmedTop (14);
    upBtn.setBounds (btns.removeFromLeft (56).withHeight (26));
    btns.removeFromLeft (6);
    downBtn.setBounds (btns.removeFromLeft (56).withHeight (26));
    btns.removeFromLeft (6);
    deleteBtn.setBounds (btns.removeFromLeft (86).withHeight (26));

    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (44);
    list.setBounds (r.withTrimmedBottom (8));

    auto n = bottom.removeFromLeft (juce::jmax (200, bottom.getWidth() - 380));
    placeField (n, newCap, newItemEd);
    bottom.removeFromLeft (10);
    auto sec = bottom.removeFromLeft (140);
    placeField (sec, sectionCap, sectionEd);
    bottom.removeFromLeft (10);
    groupBox.setBounds (bottom.removeFromLeft (130).withTrimmedTop (14).withHeight (26));
    bottom.removeFromLeft (10);
    addBtn.setBounds (bottom.withTrimmedTop (14).withHeight (26));
}

//==============================================================================
// EDITOR
//==============================================================================
CaseFileEditor::CaseFileEditor (CaseFileProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    logo = theme::detectiveLogo();

    tabs.setTabBarDepth (34);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, theme::desk);
    tabs.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    tabs.addTab ("Brief",     theme::paper, new BriefTab (proc),     true);
    tabs.addTab ("Evidence",  theme::paper, new EvidenceTab (proc),  true);
    tabs.addTab ("Analysis",  theme::paper, new AnalysisTab (proc),  true);
    tabs.addTab ("Suspects",  theme::paper, new SuspectsTab (proc),  true);
    tabs.addTab ("Checklist", theme::paper, new ChecklistTab (proc), true);
    tabs.addTab ("Plugins",   theme::paper, new PluginLibTab (proc), true);
    tabs.addTab ("Hardware",  theme::paper, new HardwareTab (proc),  true);
    tabs.addTab ("Song Map",  theme::paper, new SongMapTab (proc),   true);
    tabs.addTab ("Chains",    theme::paper, new ChainsTab (proc),    true);
    tabs.addTab ("Versions",  theme::paper, new VersionsTab (proc),  true);
    tabs.addTab ("Report",    theme::paper, new ReportTab (proc),    true);
    addAndMakeVisible (tabs);

    proc.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (980, 640, 1720, 1180);
    setSize (1120, 720);
}

CaseFileEditor::~CaseFileEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void CaseFileEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* t = dynamic_cast<CaseTab*> (tabs.getTabContentComponent (i)))
            if (t->isVisible())
                t->refresh();
    repaint (0, 0, getWidth(), 52);
}

void CaseFileEditor::paint (juce::Graphics& g)
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
    g.drawText ("SOUND DETECTIVE  //  CASE FILE",
                header.withTrimmedLeft (62).withTrimmedRight (250),
                juce::Justification::centredLeft);

    juce::String sub ("OPEN THE CASE. FIND THE SUSPECTS. CLOSE THE MIX.");
    const auto song   = proc.brief().getProperty (ids::songTitle).toString().trim();
    const auto artist = proc.brief().getProperty (ids::artist).toString().trim();
    if (song.isNotEmpty())
    {
        sub = "CASE: " + song.toUpperCase();
        if (artist.isNotEmpty()) sub << "  •  " << artist.toUpperCase();
    }
    g.setColour (theme::amber.withAlpha (0.55f));
    g.setFont (theme::type (9.5f));
    g.drawText (sub, header.withTrimmedLeft (63).withTrimmedTop (30),
                juce::Justification::topLeft);

    theme::drawStamp (g, "CASE NO. " + proc.state.getProperty (ids::caseNumber).toString(),
                      theme::stamp.brighter (0.35f),
                      juce::Rectangle<float> ((float) getWidth() - 216.0f, 11.0f, 200.0f, 30.0f),
                      -2.5f);
}

void CaseFileEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (52);
    tabs.setBounds (r);
}
