#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
//  Vintage detective case-file theme: aged paper on a dark leather desk,
//  manila folder tabs, typewriter ink, rubber-stamp status.
//==============================================================================
namespace clocker::theme
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
    void caption     (juce::Label&, const juce::String& text);
    void paintPaper  (juce::Graphics&, juce::Rectangle<int> area);
    void drawStamp   (juce::Graphics&, const juce::String& text, juce::Colour,
                      juce::Rectangle<float> area, float angleDegrees);
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
struct ClockerTab : public juce::Component
{
    explicit ClockerTab (ClockerProcessor& p) : proc (p) {}
    virtual void refresh() {}
    void visibilityChanged() override { if (isVisible()) refresh(); }
    void paint (juce::Graphics& g) override { clocker::theme::paintPaper (g, getLocalBounds()); }
    ClockerProcessor& proc;
};

//==============================================================================
class ClockTab : public ClockerTab, private juce::Timer
{
public:
    explicit ClockTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { refresh(); }
    void addManualMinutes (juce::int64 minutes);
    juce::Image logo;
    juce::Label timerLabel, statusLabel, contextLabel;
    juce::Label typeCap, notesCap, manualCap, colonLabel;
    juce::TextButton punchInBtn { "PUNCH IN" }, pauseResumeBtn { "PAUSE" },
                     breakBtn { "BREAK" },      punchOutBtn { "PUNCH OUT" };
    juce::ToggleButton billableToggle { "Billable" };
    juce::ComboBox typeBox;
    juce::TextEditor notesEd;
    juce::TextEditor manualHoursEd, manualMinsEd;
    juce::TextButton manualAddBtn { "ADD" },
                     add15Btn { "+15M" }, add30Btn { "+30M" }, add60Btn { "+1H" };
};

//==============================================================================
class TimeLogTab : public ClockerTab, private juce::ListBoxModel
{
public:
    explicit TimeLogTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    juce::ValueTree entryForRow (int row) const;   // newest first
    void saveSelected();
    void deleteSelected();

    juce::ListBox list { "entries", this };
    juce::Label editCap, hoursCap, minsCap, notesCap2;
    juce::TextEditor hoursEd, minsEd, notesEd;
    juce::ToggleButton billableToggle { "Billable" };
    juce::ComboBox typeBox;
    juce::TextButton saveBtn { "SAVE" }, deleteBtn { "DELETE" };
};

//==============================================================================
class ClientTab : public ClockerTab
{
public:
    explicit ClientTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
private:
    void bindText   (juce::TextEditor&, const juce::Identifier&);
    void bindDouble (juce::TextEditor&, const juce::Identifier&);
    juce::Label caps[9];
    juce::TextEditor clientEd, projectEd, songEd, engineerEd,
                     rateEd, feeEd, billingNotesEd, invoiceNotesEd;
    juce::ComboBox projectTypeBox;
    juce::TextButton resetBtn { "CLOSE CASE / NEW PROJECT" };
};

//==============================================================================
class ProfitTab : public ClockerTab
{
public:
    explicit ProfitTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
private:
    juce::TextEditor view;
    juce::TextButton refreshBtn { "REFRESH" };
};

//==============================================================================
class ExportTab : public ClockerTab
{
public:
    explicit ExportTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
private:
    juce::String buildCurrent() const;
    juce::ComboBox formatBox;
    juce::TextEditor preview;
    juce::TextButton copyBtn { "COPY TO CLIPBOARD" }, saveBtn { "FILE THE REPORT" };
    juce::Label savedLabel, folderLabel;
};

//==============================================================================
class SettingsTab : public ClockerTab
{
public:
    explicit SettingsTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
private:
    juce::Label caps[4];
    juce::TextEditor defaultRateEd;
    juce::ComboBox roundingBox, timeFormatBox, defaultTypeBox;
    juce::ToggleButton defaultBillableToggle { "New entries default to Billable" };
    juce::Label folderLabel;
};

//==============================================================================
class ClockerEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit ClockerEditor (ClockerProcessor&);
    ~ClockerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    ClockerProcessor& proc;
    CaseFileLookAndFeel lnf;
    juce::Image logo;
    juce::String statusText;
    juce::Colour statusColour { clocker::theme::inkDim };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClockerEditor)
};
