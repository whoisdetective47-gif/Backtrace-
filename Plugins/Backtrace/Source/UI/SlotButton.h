#pragma once
#include <JuceHeader.h>

// =============================================================================
//  SlotButton — a Vault slot that is both a selector (click) and a drag source.
//
//  A short click still selects the slot (TextButton behaviour). A drag past a
//  small threshold instead starts an external file drag of the slot's audio, so
//  the user can pull a slot straight onto the DAW timeline. Empty slots provide
//  no file, so they are not draggable.
// =============================================================================
class SlotButton : public juce::TextButton,
                   public juce::FileDragAndDropTarget
{
public:
    std::function<juce::File()> provideDragFile;        // returns the slot's WAV (or {} if empty)
    std::function<void(const juce::StringArray&)> onFilesDropped;   // drop audio → import into this slot
    std::function<void()> onContextMenu;                // right-click → Discard Evidence menu

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onContextMenu) onContextMenu(); return; }   // don't select on right-click
        juce::TextButton::mouseDown(e);
    }

    static bool isAudioFile(const juce::String& f)
    {
        return f.endsWithIgnoreCase(".wav")  || f.endsWithIgnoreCase(".aif")
            || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac")
            || f.endsWithIgnoreCase(".mp3")  || f.endsWithIgnoreCase(".m4a")
            || f.endsWithIgnoreCase(".ogg")  || f.endsWithIgnoreCase(".caf");
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile(f)) return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setState(juce::Button::buttonOver); }
    void fileDragExit (const juce::StringArray&)            override { setState(juce::Button::buttonNormal); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        setState(juce::Button::buttonNormal);
        if (onFilesDropped) onFilesDropped(files);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging || provideDragFile == nullptr) { juce::TextButton::mouseDrag(e); return; }
        if (e.getDistanceFromDragStart() < 8) return;   // let small moves stay a click

        auto f = provideDragFile();
        if (! f.existsAsFile()) return;                 // empty slot → not draggable

        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles(
            { f.getFullPathName() }, false, this,
            [safe = juce::Component::SafePointer<SlotButton>(this)] { if (safe != nullptr) safe->dragging = false; });
    }

private:
    bool dragging = false;
};
