// GUI probe for the "can't type in the small boxes" bug. Builds the real
// ClockerEditor, then for every TextEditor in each tab checks:
//   1. hit-test — is the editor actually the component under its own centre,
//      or is something invisible sitting on top of it?
//   2. focus — can it grab keyboard focus (needs a desktop peer)?
//   3. typing — does a simulated keypress insert text?
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void findEditors (juce::Component* c, juce::Array<juce::TextEditor*>& out)
{
    for (auto* ch : c->getChildren())
    {
        if (auto* te = dynamic_cast<juce::TextEditor*> (ch))
            out.add (te);
        else
            findEditors (ch, out);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int problems = 0;

    ClockerProcessor proc;
    proc.addManualEntry (75 * 60000LL, true, 4, "probe entry");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    bool haveDesktop = true;
    ed->setVisible (true);
    ed->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    if (ed->getPeer() == nullptr) haveDesktop = false;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

    juce::TabbedComponent* tabs = nullptr;
    for (auto* ch : ed->getChildren())
        if ((tabs = dynamic_cast<juce::TabbedComponent*> (ch)) != nullptr)
            break;
    if (tabs == nullptr) { std::cout << "FATAL: no TabbedComponent found\n"; return 1; }

    for (int tabIdx = 0; tabIdx < tabs->getNumTabs(); ++tabIdx)
    {
        tabs->setCurrentTabIndex (tabIdx, false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
        auto* content = tabs->getTabContentComponent (tabIdx);
        if (content == nullptr) continue;

        // select a Time Log row so its edit panel is populated
        for (auto* ch : content->getChildren())
            if (auto* lb = dynamic_cast<juce::ListBox*> (ch))
            {
                lb->selectRow (0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            }

        juce::Array<juce::TextEditor*> eds;
        findEditors (content, eds);
        std::cout << "\n=== tab " << tabIdx << " (" << tabs->getTabNames()[tabIdx]
                  << ") — " << eds.size() << " text editors ===\n";

        int i = 0;
        for (auto* te : eds)
        {
            auto centre = content->getLocalPoint (te, te->getLocalBounds().getCentre());
            auto* hit = content->getComponentAt (centre.x, centre.y);
            const bool hitOk = (hit == te || (hit != nullptr && te->isParentOf (hit)));

            bool focusOk = true;
            if (haveDesktop && ! te->isReadOnly())
            {
                te->grabKeyboardFocus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
                focusOk = te->hasKeyboardFocus (true);
            }

            juce::String typed = "-";
            if (! te->isReadOnly())
            {
                auto before = te->getText();
                te->keyPressed (juce::KeyPress ('5', juce::ModifierKeys(), '5'));
                typed = (te->getText() != before) ? "yes" : "NO";
            }

            const bool bad = ! hitOk || ! focusOk || typed == "NO";
            if (bad) ++problems;
            std::cout << (bad ? "PROBLEM " : "ok      ")
                      << "editor#" << i++
                      << " bounds=" << te->getBounds().toString()
                      << " hit=" << (hitOk ? juce::String ("self")
                                           : (hit != nullptr ? juce::String (typeid (*hit).name())
                                                             : juce::String ("NOTHING")))
                      << " focus=" << (focusOk ? "yes" : "NO")
                      << " typing=" << typed
                      << (te->isReadOnly() ? " [read-only]" : "")
                      << "\n";
        }
    }

    std::cout << "\n" << (problems == 0 ? "PROBE CLEAN — bug is not in component config/geometry"
                                        : juce::String (problems) + " PROBLEM(S) FOUND")
              << " (desktop peer: " << (haveDesktop ? "yes" : "no") << ")\n";
    return 0;
}
