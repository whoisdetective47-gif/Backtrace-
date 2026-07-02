#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace clocker::theme
{
    const juce::Colour bg      (0xff14171a);
    const juce::Colour panel   (0xff1b2026);
    const juce::Colour inset   (0xff101316);
    const juce::Colour amber   (0xffc8a24a);
    const juce::Colour blue    (0xff7ad1ff);
    const juce::Colour green   (0xff7fd18a);
    const juce::Colour red     (0xffd1837f);
    const juce::Colour text    (0xd9ffffff);
    const juce::Colour dim     (0x80ffffff);

    juce::Font mono (float size, bool bold = false);
    juce::Font ui   (float size, bool bold = false);
    void styleButton (juce::TextButton&, juce::Colour accent = amber);
    void styleEditor (juce::TextEditor&, bool multiline = false);
    void styleCombo  (juce::ComboBox&);
    void caption     (juce::Label&, const juce::String& text);
}

//==============================================================================
struct ClockerTab : public juce::Component
{
    explicit ClockerTab (ClockerProcessor& p) : proc (p) {}
    virtual void refresh() {}
    void visibilityChanged() override { if (isVisible()) refresh(); }
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
    juce::Label timerLabel, statusLabel, contextLabel;
    juce::Label billableCap, typeCap, notesCap, manualCap;
    juce::TextButton clockInBtn { "CLOCK IN" }, pauseBtn { "PAUSE" },
                     resumeBtn { "RESUME" },   clockOutBtn { "CLOCK OUT" };
    juce::ToggleButton billableToggle { "Billable" };
    juce::ComboBox typeBox;
    juce::TextEditor notesEd;
    juce::TextEditor manualHoursEd, manualMinsEd;
    juce::TextButton manualAddBtn { "ADD MANUAL TIME" };
};

//==============================================================================
class TimeLogTab : public ClockerTab, private juce::ListBoxModel
{
public:
    explicit TimeLogTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    juce::ValueTree entryForRow (int row) const;   // newest first
    void saveSelected();
    void deleteSelected();

    juce::ListBox list { "entries", this };
    juce::Label editCap, hoursCap, minsCap, typeCap2, notesCap2;
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
    void paint (juce::Graphics&) override;
private:
    void bindText   (juce::TextEditor&, const juce::Identifier&);
    void bindDouble (juce::TextEditor&, const juce::Identifier&);
    juce::Label caps[9];
    juce::TextEditor clientEd, projectEd, songEd, engineerEd,
                     rateEd, feeEd, billingNotesEd, invoiceNotesEd;
    juce::ComboBox projectTypeBox;
    juce::TextButton resetBtn { "NEW PROJECT / RESET" };
};

//==============================================================================
class ProfitTab : public ClockerTab
{
public:
    explicit ProfitTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
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
    void paint (juce::Graphics&) override;
private:
    juce::String buildCurrent() const;
    juce::ComboBox formatBox;
    juce::TextEditor preview;
    juce::TextButton copyBtn { "COPY TO CLIPBOARD" }, saveBtn { "SAVE TO CLOCKER FOLDER" };
    juce::Label savedLabel, folderLabel;
};

//==============================================================================
class SettingsTab : public ClockerTab
{
public:
    explicit SettingsTab (ClockerProcessor&);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::Label caps[6];
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
    juce::Image logo;
    juce::Label titleLabel, statusChip;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClockerEditor)
};
