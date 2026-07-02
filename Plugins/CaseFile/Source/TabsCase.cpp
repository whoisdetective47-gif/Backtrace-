#include "PluginEditor.h"

using namespace casefile;

//==============================================================================
// EVIDENCE TAB
//==============================================================================
EvidenceTab::EvidenceTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[4] = { "EVIDENCE ON FILE", "EXHIBIT NAME", "REFERENCE ROLE", "NOTES" };
    for (int i = 0; i < 4; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleButton (importBtn, theme::approve);
    importBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Import audio evidence (current mix / references)",
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                juce::StringArray paths;
                for (const auto& f : fc.getResults())
                    paths.add (f.getFullPathName());
                importFiles (paths);
            });
    };
    addAndMakeVisible (importBtn);

    theme::styleButton (removeBtn, theme::stamp);
    removeBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.evidence(), t); refresh(); }
    };
    addAndMakeVisible (removeBtn);

    theme::styleButton (analyzeBtn, theme::brass);
    analyzeBtn.onClick = [this]
    {
        auto t = selected();
        if (! t.isValid()) return;
        t.setProperty (ids::analyzed, false, nullptr);
        statusLabel.setText (proc.analyzeEvidenceItem (t)
                                 ? "Analyzed " + t.getProperty (ids::name).toString() + "."
                                 : "Could not read the audio file — has it moved?",
                             juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (analyzeBtn);

    theme::styleButton (analyzeAllBtn, theme::brass);
    analyzeAllBtn.onClick = [this]
    {
        const int n = proc.analyzeAllEvidence();
        statusLabel.setText ("Analyzed " + juce::String (n) + " file(s).",
                             juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (analyzeAllBtn);

    list.setRowHeight (26);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    theme::styleEditor (nameEd);
    addAndMakeVisible (nameEd);
    nameEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::name, nameEd.getText(), nullptr); list.repaint(); }
    };

    for (int i = 0; i < evidenceRoles().size(); ++i)
        roleBox.addItem (evidenceRoles()[i], i + 1);
    theme::styleCombo (roleBox);
    roleBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && roleBox.getSelectedId() > 0)
        { t.setProperty (ids::role, roleBox.getSelectedId() - 1, nullptr); list.repaint(); }
    };
    addAndMakeVisible (roleBox);

    theme::styleEditor (notesEd, true);
    theme::bindText (notesEd, ids::notes, item);
    addAndMakeVisible (notesEd);

    infoLabel.setFont (theme::mono (12.0f));
    infoLabel.setColour (juce::Label::textColourId, theme::ink);
    infoLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (infoLabel);

    hintLabel.setFont (theme::type (12.0f));
    hintLabel.setColour (juce::Label::textColourId, theme::inkDim);
    hintLabel.setText ("Drop audio files anywhere on this tab, or hit IMPORT AUDIO. "
                       "Tag one file as Current Mix and at least one as a reference, "
                       "then INVESTIGATE on the Analysis tab.",
                       juce::dontSendNotification);
    addAndMakeVisible (hintLabel);

    statusLabel.setFont (theme::type (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (statusLabel);

    refresh();
}

bool EvidenceTab::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;ogg;caf"))
            return true;
    return false;
}

void EvidenceTab::filesDropped (const juce::StringArray& files, int, int)
{
    importFiles (files);
}

void EvidenceTab::importFiles (const juce::StringArray& paths)
{
    int added = 0;
    for (const auto& path : paths)
    {
        juce::File f (path);
        if (! f.existsAsFile()
              || ! f.hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;ogg;caf"))
            continue;
        // first exhibit defaults to Current Mix, the rest to Main Sonic Target
        const int role = proc.evidence().getNumChildren() == 0 ? 0 : 1;
        proc.addEvidence (f, role);
        ++added;
    }
    if (added > 0)
    {
        statusLabel.setText ("Filed " + juce::String (added) + " exhibit(s). "
                             "Check the role tags, then analyze.",
                             juce::dontSendNotification);
        list.updateContent();
        list.selectRow (proc.evidence().getNumChildren() - 1);
    }
    refresh();
}

int EvidenceTab::getNumRows() { return proc.evidence().getNumChildren(); }

juce::ValueTree EvidenceTab::selected() const
{
    return proc.evidence().getChild (list.getSelectedRow());
}

void EvidenceTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto t = proc.evidence().getChild (row);
    if (! t.isValid()) return;
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }

    const bool isCurrent = (int) t.getProperty (ids::role, 1) == 0;
    g.setColour (isCurrent ? theme::stamp : theme::ink);
    g.setFont (theme::type (12.5f, sel || isCurrent));
    g.drawText (t.getProperty (ids::name).toString(), 8, 0, w / 2 - 8, h,
                juce::Justification::centredLeft);

    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.5f));
    juce::String meta = evidenceRoles()[safeIndex (t.getProperty (ids::role), evidenceRoles())];
    meta << ((bool) t.getProperty (ids::analyzed, false) ? "  •  analyzed" : "  •  pending");
    if (! juce::File (t.getProperty (ids::path).toString()).existsAsFile())
        meta << "  •  FILE MISSING";
    g.drawText (meta, w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
}

void EvidenceTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    nameEd.setEnabled (has);
    notesEd.setEnabled (has);
    roleBox.setEnabled (has);
    removeBtn.setEnabled (has);
    analyzeBtn.setEnabled (has);

    theme::setIfChanged (nameEd,  has ? t.getProperty (ids::name).toString() : juce::String());
    theme::setIfChanged (notesEd, has ? t.getProperty (ids::notes).toString() : juce::String());
    roleBox.setSelectedId (has ? safeIndex (t.getProperty (ids::role), evidenceRoles()) + 1 : 0,
                           juce::dontSendNotification);

    juce::String info;
    if (has)
    {
        const juce::File f (t.getProperty (ids::path).toString());
        info << "File:  " << f.getFullPathName() << "\n";
        if (! f.existsAsFile())
            info << "\n!! FILE NOT FOUND — re-import or restore the file.\n";
        else if ((bool) t.getProperty (ids::analyzed, false))
        {
            const auto r = CaseFileProcessor::resultFromTree (t);
            info << "Length: " << juce::String (r.lengthSec, 1) << " s    "
                 << juce::String (r.sampleRate / 1000.0, 1) << " kHz    "
                 << (r.channels == 1 ? "mono" : "stereo");
            if (r.bitDepth > 0) info << "    " << r.bitDepth << "-bit";
            info << "\n";
            info << "Peak " << juce::String (r.peakDb, 1) << " dB    RMS "
                 << juce::String (r.rmsDb, 1) << " dB    Crest "
                 << juce::String (r.crestDb, 1) << " dB\n";
            info << "Width " << juce::String (r.widthPct, 0) << "%    Low width "
                 << juce::String (r.lowWidthPct, 0) << "%    Corr "
                 << juce::String (r.corr, 2);
        }
        else
            info << "Not analyzed yet — ANALYZE SELECTED or ANALYZE ALL.";
    }
    infoLabel.setText (info, juce::dontSendNotification);
}

void EvidenceTab::refresh()
{
    list.updateContent();
    list.repaint();
    refreshDetail();
}

void EvidenceTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (30);
    importBtn.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (6);
    removeBtn.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (14);
    analyzeBtn.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    analyzeAllBtn.setBounds (top.removeFromLeft (120));
    r.removeFromTop (4);
    hintLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromBottom (16));

    caps[0].setBounds (r.removeFromTop (14));
    list.setBounds (r.removeFromTop (juce::jmax (100, r.getHeight() * 42 / 100)));
    r.removeFromTop (10);

    auto colA = r.removeFromLeft ((r.getWidth() - 14) / 2);
    r.removeFromLeft (14);
    auto colB = r;

    placeField (colA, caps[1], nameEd);
    placeField (colA, caps[2], roleBox);
    infoLabel.setBounds (colA.withTrimmedBottom (2));

    caps[3].setBounds (colB.removeFromTop (14));
    notesEd.setBounds (colB.withTrimmedBottom (2));
}

//==============================================================================
// ANALYSIS TAB
//==============================================================================
AnalysisTab::AnalysisTab (CaseFileProcessor& p) : CaseTab (p)
{
    theme::styleEditor (view, true);
    view.setReadOnly (true);
    view.setFont (theme::mono (12.5f));
    addAndMakeVisible (view);

    theme::styleButton (investigateBtn, theme::stamp);
    investigateBtn.onClick = [this]
    {
        const int n = proc.investigate();
        if (n < 0)
            statusLabel.setText ("Need an analyzed Current Mix plus at least one reference "
                                 "— check the Evidence tab.", juce::dontSendNotification);
        else if (n == 0)
            statusLabel.setText ("Investigation complete — no new suspects. "
                                 "The mix tracks its references closely.",
                                 juce::dontSendNotification);
        else
            statusLabel.setText (juce::String (n) + " suspect(s) identified — "
                                 "see the Suspects tab.", juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (investigateBtn);

    theme::styleButton (refreshBtn, theme::brass);
    refreshBtn.onClick = [this] { refresh(); };
    addAndMakeVisible (refreshBtn);

    statusLabel.setFont (theme::type (12.0f, true));
    statusLabel.setColour (juce::Label::textColourId, theme::brass);
    addAndMakeVisible (statusLabel);
}

void AnalysisTab::refresh()
{
    view.setText (proc.buildAnalysisSummary(), false);
}

void AnalysisTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    auto top = r.removeFromTop (30);
    investigateBtn.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    refreshBtn.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (12);
    statusLabel.setBounds (top);
    r.removeFromTop (8);
    view.setBounds (r);
}

//==============================================================================
// SUSPECTS TAB
//==============================================================================
SuspectsTab::SuspectsTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[6] = { "SUSPECT", "RANGE / MEASUREMENT", "SEVERITY",
                             "POSSIBLE SOURCES", "WHY IT MATTERS", "SUGGESTED ACTIONS" };
    for (int i = 0; i < 6; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        SuspectDef d;
        d.title = "New Suspect";
        d.severity = 1;
        proc.addSuspect (d, true);
        proc.notify();
        list.updateContent();
        list.selectRow (proc.suspects().getNumChildren() - 1);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.suspects(), t); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    theme::styleButton (solveBtn, theme::approve);
    solveBtn.onClick = [this]
    {
        auto t = selected();
        if (! t.isValid()) return;
        t.setProperty (ids::solved, ! (bool) t.getProperty (ids::solved, false), nullptr);
        refresh();
    };
    addAndMakeVisible (solveBtn);

    theme::styleButton (toChecklistBtn, theme::brass);
    toChecklistBtn.onClick = [this]
    {
        auto t = selected();
        if (! t.isValid()) return;
        proc.suspectToChecklistItem (t);
        statusLabel.setText ("Checklist item created: Investigate: "
                             + t.getProperty (ids::title).toString(),
                             juce::dontSendNotification);
    };
    addAndMakeVisible (toChecklistBtn);

    list.setRowHeight (28);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    theme::styleEditor (titleEd);
    addAndMakeVisible (titleEd);
    titleEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::title, titleEd.getText(), nullptr); list.repaint(); }
    };
    theme::styleEditor (rangeEd);
    theme::bindText (rangeEd, ids::range, item);
    addAndMakeVisible (rangeEd);
    theme::styleEditor (sourcesEd);
    theme::bindText (sourcesEd, ids::sources, item);
    addAndMakeVisible (sourcesEd);
    theme::styleEditor (whyEd, true);
    theme::bindText (whyEd, ids::why, item);
    addAndMakeVisible (whyEd);
    theme::styleEditor (actionsEd, true);
    theme::bindText (actionsEd, ids::actions, item);
    addAndMakeVisible (actionsEd);

    for (int i = 0; i < severityNames().size(); ++i)
        severityBox.addItem (severityNames()[i], i + 1);
    theme::styleCombo (severityBox);
    severityBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && severityBox.getSelectedId() > 0)
        { t.setProperty (ids::severity, severityBox.getSelectedId() - 1, nullptr); list.repaint(); }
    };
    addAndMakeVisible (severityBox);

    statusLabel.setFont (theme::type (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (statusLabel);

    refresh();
}

int SuspectsTab::getNumRows() { return proc.suspects().getNumChildren(); }

juce::ValueTree SuspectsTab::selected() const
{
    return proc.suspects().getChild (list.getSelectedRow());
}

void SuspectsTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto t = proc.suspects().getChild (row);
    if (! t.isValid()) return;
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }

    const bool solved = t.getProperty (ids::solved, false);
    const int sev = safeIndex (t.getProperty (ids::severity), severityNames());

    // severity light — brass / amber / stamp-red
    const juce::Colour sevCol = solved ? theme::approve
                              : sev == 2 ? theme::stamp
                              : sev == 1 ? theme::amber.darker (0.1f) : theme::brass;
    g.setColour (sevCol);
    g.fillEllipse (9.0f, h / 2.0f - 5.0f, 10.0f, 10.0f);

    g.setColour (solved ? theme::inkDim : theme::ink);
    juce::Font f = theme::type (13.0f, sel);
    if (solved) f = f.withStyle (juce::Font::italic);
    g.setFont (f);
    g.drawText (t.getProperty (ids::title).toString(), 28, 0, w * 55 / 100 - 28, h,
                juce::Justification::centredLeft);

    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.0f));
    juce::String meta;
    if (solved) meta << "SOLVED  •  ";
    if ((bool) t.getProperty (ids::custom, false)) meta << "manual  •  ";
    meta << severityNames()[sev] << "  •  " << t.getProperty (ids::range).toString();
    g.drawText (meta, w * 55 / 100, 0, w * 45 / 100 - 8, h, juce::Justification::centredRight);
}

void SuspectsTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    for (auto* e : { &titleEd, &rangeEd, &sourcesEd, &whyEd, &actionsEd })
        e->setEnabled (has);
    severityBox.setEnabled (has);
    deleteBtn.setEnabled (has);
    solveBtn.setEnabled (has);
    toChecklistBtn.setEnabled (has);
    solveBtn.setButtonText (has && (bool) t.getProperty (ids::solved, false)
                                ? "REOPEN" : "MARK SOLVED");

    auto v = [&] (const juce::Identifier& prop)
    { return has ? t.getProperty (prop).toString() : juce::String(); };
    theme::setIfChanged (titleEd,   v (ids::title));
    theme::setIfChanged (rangeEd,   v (ids::range));
    theme::setIfChanged (sourcesEd, v (ids::sources));
    theme::setIfChanged (whyEd,     v (ids::why));
    theme::setIfChanged (actionsEd, v (ids::actions));
    severityBox.setSelectedId (has ? safeIndex (t.getProperty (ids::severity), severityNames()) + 1 : 0,
                               juce::dontSendNotification);
}

void SuspectsTab::refresh()
{
    list.updateContent();
    list.repaint();
    refreshDetail();
}

void SuspectsTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (30);
    addBtn.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (6);
    deleteBtn.setBounds (top.removeFromLeft (86));
    top.removeFromLeft (14);
    solveBtn.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (6);
    toChecklistBtn.setBounds (top.removeFromLeft (160));

    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromBottom (16));

    list.setBounds (r.removeFromTop (juce::jmax (110, r.getHeight() * 38 / 100)));
    r.removeFromTop (10);

    auto colA = r.removeFromLeft ((r.getWidth() - 14) / 2);
    r.removeFromLeft (14);
    auto colB = r;

    placeField (colA, caps[0], titleEd);
    {
        auto row = colA.removeFromTop (48);
        auto c1 = row.removeFromLeft (row.getWidth() * 62 / 100);
        row.removeFromLeft (10);
        placeField (c1,  caps[1], rangeEd);
        placeField (row, caps[2], severityBox);
    }
    placeField (colA, caps[3], sourcesEd);
    caps[4].setBounds (colA.removeFromTop (14));
    whyEd.setBounds (colA.withTrimmedBottom (2));

    caps[5].setBounds (colB.removeFromTop (14));
    actionsEd.setBounds (colB.withTrimmedBottom (2));
}

//==============================================================================
// REPORT TAB
//==============================================================================
ReportTab::ReportTab (CaseFileProcessor& p) : CaseTab (p)
{
    logo = theme::detectiveLogo();

    theme::caption (deliveryCap, "FINAL DELIVERY NOTES (PRINTS WITH THE REPORT)");
    addAndMakeVisible (deliveryCap);
    theme::styleEditor (deliveryEd, true);
    theme::bindText (deliveryEd, ids::finalDeliveryNotes, [this] { return proc.report(); });
    addAndMakeVisible (deliveryEd);

    formatBox.addItem ("Text Report", 1);
    formatBox.addItem ("Markdown", 2);
    formatBox.addItem ("JSON Case Packet (AI-ready)", 3);
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
        static const char* exts[] = { ".txt", ".md", ".json" };
        auto folder = CaseFileProcessor::caseFileFolder();
        folder.createDirectory();
        auto file = folder.getChildFile (proc.exportBaseName()
                                         + exts[juce::jlimit (0, 2, formatBox.getSelectedId() - 1)]);
        if (file.replaceWithText (buildCurrent()))
        {
            savedLabel.setText ("Case filed: " + file.getFullPathName(), juce::dontSendNotification);
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
    folderLabel.setText ("Case archive: " + CaseFileProcessor::caseFileFolder().getFullPathName(),
                         juce::dontSendNotification);
    addAndMakeVisible (folderLabel);
}

void ReportTab::paint (juce::Graphics& g)
{
    theme::paintPaper (g, getLocalBounds());
    theme::drawWatermark (g, logo, getLocalBounds());
}

juce::String ReportTab::buildCurrent() const
{
    switch (formatBox.getSelectedId())
    {
        case 2:  return proc.buildReport (true);
        case 3:  return proc.buildJSON();
        default: return proc.buildReport (false);
    }
}

void ReportTab::refresh()
{
    preview.setText (buildCurrent(), false);
    theme::setIfChanged (deliveryEd, proc.report().getProperty (ids::finalDeliveryNotes));
}

void ReportTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    deliveryCap.setBounds (r.removeFromTop (14));
    deliveryEd.setBounds (r.removeFromTop (54));
    r.removeFromTop (8);

    auto top = r.removeFromTop (30);
    formatBox.setBounds (top.removeFromLeft (230));
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
