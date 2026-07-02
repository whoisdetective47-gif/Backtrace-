#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
//  Case File GUI — vintage detective case-folder theme, shared with Clocker:
//  aged paper on a dark leather desk, manila folder tabs, typewriter ink,
//  rubber-stamp accents, ghosted Detective 47 silhouette watermark.
//
//  Eleven folder tabs: Brief, Evidence, Analysis, Suspects, Checklist,
//  Plugins, Hardware, Song Map, Chains, Versions, Report.
//==============================================================================

namespace casefile::theme
{
    const juce::Colour desk    (0xff241b12);   // dark leather desk backdrop
    const juce::Colour leather (0xff2e2317);   // header strip
    const juce::Colour paper   (0xffe9dfc4);   // aged paper
    const juce::Colour field   (0xfff3ebd6);   // lighter paper for inputs
    const juce::Colour manila  (0xffd3bd8e);   // folder-tab manila
    const juce::Colour ink     (0xff33291c);   // typewriter ink
    const juce::Colour inkDim  (0x9033291c);
    const juce::Colour stamp   (0xffa8382a);   // rubber-stamp red
    const juce::Colour approve (0xff43703f);   // stamped green
    const juce::Colour brass   (0xff8a6d2f);   // brass/amber on paper
    const juce::Colour amber   (0xffc8a24a);   // title gold on leather

    juce::Font type (float size, bool bold = false);   // typewriter face
    juce::Font mono (float size, bool bold = false);

    void styleButton (juce::TextButton&, juce::Colour accent = ink);
    void styleEditor (juce::TextEditor&, bool multiline = false);
    void styleCombo  (juce::ComboBox&);
    void styleToggle (juce::ToggleButton&);
    void caption     (juce::Label&, const juce::String& text);
    void paintPaper  (juce::Graphics&, juce::Rectangle<int> area);
    void drawStamp   (juce::Graphics&, const juce::String& text, juce::Colour,
                      juce::Rectangle<float> area, float angleDegrees);
    void drawWatermark (juce::Graphics&, const juce::Image& logo,
                        juce::Rectangle<int> area);          // ghosted silhouette
    juce::Image detectiveLogo();

    // binds a TextEditor to a property on whatever tree `target` returns at
    // edit time — used by every list/detail form in the plugin
    void bindText (juce::TextEditor&, const juce::Identifier& prop,
                   std::function<juce::ValueTree()> target);
    void setIfChanged (juce::TextEditor&, const juce::String& value);
}

// caption above a field, fixed row height — the standard form row everywhere
inline void placeField (juce::Rectangle<int>& col, juce::Label& cap, juce::Component& comp,
                        int fieldHeight = 26, int gap = 8)
{
    cap.setBounds (col.removeFromTop (14));
    comp.setBounds (col.removeFromTop (fieldHeight));
    col.removeFromTop (gap);
}

//==============================================================================
struct CaseFileLookAndFeel : public juce::LookAndFeel_V4
{
    CaseFileLookAndFeel();
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int  getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
};

//==============================================================================
struct CaseTab : public juce::Component
{
    explicit CaseTab (CaseFileProcessor& p) : proc (p) {}
    virtual void refresh() {}
    void visibilityChanged() override { if (isVisible()) refresh(); }
    void paint (juce::Graphics& g) override { casefile::theme::paintPaper (g, getLocalBounds()); }
    CaseFileProcessor& proc;
};

//==============================================================================
//  BRIEF — the case brief: identity and intention of the song (scrolling form)
//==============================================================================
class BriefTab : public CaseTab
{
public:
    explicit BriefTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Content : public juce::Component
    {
        void paint (juce::Graphics&) override;   // paper + watermark
        juce::Image logo;
        std::function<void (juce::Rectangle<int>)> layout;
        void resized() override { if (layout) layout (getLocalBounds()); }
    };

    juce::Viewport viewport;
    Content content;

    static constexpr int numFields = 15;
    juce::Label caps[numFields + 3];
    juce::TextEditor titleEd, artistEd, producerEd, mixerEd, genreEd, tempoEd,
                     keyEd, srEd, bdEd, deadlineEd;
    juce::ComboBox stageBox;
    juce::TextEditor refsEd, emotionalEd, goalEd, problemEd, clientNotesEd,
                     deliveryEd, interviewEd;
};

//==============================================================================
//  SONG MAP / MUSICALITY — key/tempo/chords + section map with future
//  transport hooks (VST3 offers no host-transport control; buttons are
//  present but marked "coming later", copy-start works today)
//==============================================================================
class SongMapTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit SongMapTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[19];
    juce::TextEditor keyEd, scaleEd, tempoEd, sigEd, hookEd, lowEndEd;
    juce::TextEditor chordsEd, songNotesEd;

    juce::ListBox list { "sections", this };
    juce::ComboBox presetBox;
    juce::TextButton addBtn { "ADD SECTION" }, deleteBtn { "DELETE" };
    juce::TextEditor secNameEd, startTimeEd, endTimeEd, startBarEd, endBarEd,
                     secChordsEd, secMixGoalEd, secProdGoalEd, secNotesEd;
    juce::ComboBox energyBox;
    juce::TextButton copyStartBtn { "COPY START" },
                     goToBtn { "GO TO (COMING LATER)" },
                     loopBtn { "LOOP (COMING LATER)" };
};

//==============================================================================
//  CHECKLIST — standard production/mix/master lists + custom + section-linked
//==============================================================================
class ChecklistTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit ChecklistTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::Array<juce::ValueTree> visibleItems() const;

    juce::Label filterCap, newCap, sectionCap, progressLabel;
    juce::ComboBox filterBox, groupBox;
    juce::ListBox list { "checklist", this };
    juce::TextEditor newItemEd, sectionEd;
    juce::TextButton addBtn { "ADD" }, deleteBtn { "DELETE" },
                     upBtn { "UP" }, downBtn { "DOWN" };
};

//==============================================================================
//  PLUGIN LIBRARY — manual entry, search/filter, favorites, CSV/paste import
//==============================================================================
class PluginLibTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit PluginLibTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::Array<juce::ValueTree> visibleItems() const;
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[8], statusLabel;
    juce::TextEditor searchEd;
    juce::ComboBox filterBox;
    juce::ListBox list { "plugins", this };
    juce::TextButton addBtn { "ADD PLUGIN" }, deleteBtn { "DELETE" },
                     pasteBtn { "BULK PASTE" }, importBtn { "IMPORT CSV" },
                     exportBtn { "EXPORT CSV" };
    juce::TextEditor nameEd, companyEd, bestUseEd, notesEd;
    juce::ComboBox categoryBox, ownershipBox;
    juce::ToggleButton favToggle { "Favorite" }, cpuToggle { "CPU heavy" },
                       oftenToggle { "Used often" };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
//  HARDWARE LOCKER — manual outboard gear entries
//==============================================================================
class HardwareTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit HardwareTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::Array<juce::ValueTree> visibleItems() const;
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[10], statusLabel;
    juce::TextEditor searchEd;
    juce::ComboBox filterBox;
    juce::ListBox list { "hardware", this };
    juce::TextButton addBtn { "ADD GEAR" }, deleteBtn { "DELETE" },
                     exportBtn { "EXPORT CSV" };
    juce::TextEditor nameEd, brandEd, channelsEd, insertEd, favUseEd,
                     notesEd, recallEd;
    juce::ComboBox typeBox, stereoBox;
    juce::ToggleButton favToggle { "Favorite" };
};

//==============================================================================
//  EVIDENCE — import current mix + references, tag roles, file info
//==============================================================================
class EvidenceTab : public CaseTab, private juce::ListBoxModel,
                    public juce::FileDragAndDropTarget
{
public:
    explicit EvidenceTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::ValueTree selected() const;
    void refreshDetail();
    void importFiles (const juce::StringArray& paths);

    juce::Label caps[4], infoLabel, statusLabel, hintLabel;
    juce::ListBox list { "evidence", this };
    juce::TextButton importBtn { "IMPORT AUDIO" }, removeBtn { "REMOVE" },
                     analyzeBtn { "ANALYZE SELECTED" }, analyzeAllBtn { "ANALYZE ALL" };
    juce::TextEditor nameEd, notesEd;
    juce::ComboBox roleBox;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
//  ANALYSIS — measured comparison readout + INVESTIGATE
//==============================================================================
class AnalysisTab : public CaseTab
{
public:
    explicit AnalysisTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    juce::TextEditor view;
    juce::TextButton investigateBtn { "INVESTIGATE" }, refreshBtn { "REFRESH" };
    juce::Label statusLabel;
};

//==============================================================================
//  SUSPECTS — detective-style problem cards
//==============================================================================
class SuspectsTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit SuspectsTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[6], statusLabel;
    juce::ListBox list { "suspects", this };
    juce::TextButton addBtn { "ADD CUSTOM SUSPECT" }, deleteBtn { "DELETE" },
                     solveBtn { "MARK SOLVED" }, toChecklistBtn { "ADD TO CHECKLIST" };
    juce::TextEditor titleEd, rangeEd, sourcesEd, whyEd, actionsEd;
    juce::ComboBox severityBox;
};

//==============================================================================
//  CHAINS / RECALL — manual recall sheets per track template
//==============================================================================
class ChainsTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit ChainsTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[13];
    juce::ComboBox templateBox;
    juce::ListBox list { "chains", this };
    juce::TextButton addBtn { "ADD CHAIN" }, deleteBtn { "DELETE" };
    juce::TextEditor trackNameEd, micEd, preampEd, compEd, eqEd, deesserEd,
                     fxSendsEd, problemEd, planEd, outboardEd, pluginChainEd, notesEd;
};

//==============================================================================
//  VERSIONS — Mix 1 / Mix 2 / Final revision log
//==============================================================================
class VersionsTab : public CaseTab, private juce::ListBoxModel
{
public:
    explicit VersionsTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshDetail(); }
    juce::ValueTree selected() const;
    void refreshDetail();

    juce::Label caps[7], statusLabel;
    juce::ListBox list { "versions", this };
    juce::TextEditor newNameEd;
    juce::TextButton addBtn { "LOG VERSION" }, deleteBtn { "DELETE" },
                     snapshotBtn { "ATTACH ANALYSIS SNAPSHOT" };
    juce::TextEditor nameEd, printedEd, notesEd, feedbackEd, changesEd, problemsEd;
};

//==============================================================================
//  REPORT — the final case file, exported as text / markdown / JSON packet
//==============================================================================
class ReportTab : public CaseTab
{
public:
    explicit ReportTab (CaseFileProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String buildCurrent() const;
    juce::Image logo;
    juce::Label deliveryCap, savedLabel, folderLabel;
    juce::TextEditor deliveryEd, preview;
    juce::ComboBox formatBox;
    juce::TextButton copyBtn { "COPY TO CLIPBOARD" }, saveBtn { "FILE THE REPORT" };
};

//==============================================================================
class CaseFileEditor : public juce::AudioProcessorEditor,
                       private juce::ChangeListener
{
public:
    explicit CaseFileEditor (CaseFileProcessor&);
    ~CaseFileEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    CaseFileProcessor& proc;
    CaseFileLookAndFeel lnf;
    juce::Image logo;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaseFileEditor)
};
