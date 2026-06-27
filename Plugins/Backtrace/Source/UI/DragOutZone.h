#pragma once
#include <JuceHeader.h>

// =============================================================================
//  DragOutZone — drag the processed result out to the DAW or desktop (Phase 6).
//
//  On drag, provideFile() renders the current reversed/processed selection to a
//  WAV and the OS-level file drag begins; the host (or Finder) copies it on drop.
// =============================================================================
class DragOutZone : public juce::Component
{
public:
    std::function<juce::File()> provideFile;   // renders + returns the file to drag

    void paint(juce::Graphics& g) override
    {
        auto rf = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff14171a));
        g.fillRoundedRectangle(rf, 5.0f);
        g.setColour(juce::Colour(0x55ffffff));
        g.drawRoundedRectangle(rf.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(juce::Colour(0xcc7ad1ff));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("Drag to DAW", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDrag(const juce::MouseEvent&) override
    {
        if (dragging || provideFile == nullptr) return;

        auto f = provideFile();
        if (! f.existsAsFile()) return;

        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            { f.getFullPathName() }, false, this,
            [safe = juce::Component::SafePointer<DragOutZone>(this)]
            {
                if (safe != nullptr) safe->dragging = false;
            });
    }

private:
    bool dragging = false;
};
