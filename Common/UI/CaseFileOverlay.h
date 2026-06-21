#pragma once
#include <JuceHeader.h>
#include "SDColors.h"

// ===========================================================================
//  Detective 47 — Case File Overlay
//
//  First-launch walkthrough displayed as a classified case file document.
//  Shown once per installation; dismissed state stored in ApplicationProperties.
//  Reusable across all Detective 47 plugins — pass pluginCode e.g. "D47-001".
// ===========================================================================
class CaseFileOverlay : public juce::Component
{
public:
    CaseFileOverlay(const juce::String& pluginCode,
                    const juce::String& pluginName,
                    const std::vector<std::pair<juce::String, juce::String>>& pages)
        : code(pluginCode), name(pluginName), casePages(pages)
    {
        setInterceptsMouseClicks(true, true);

        btnNext.setButtonText("NEXT  >");
        btnNext.onClick = [this] { advance(); };
        btnNext.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFFD09020));
        btnNext.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xFF100C06));
        btnNext.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE0B030));
        addAndMakeVisible(btnNext);

        btnSkip.setButtonText("SKIP BRIEFING");
        btnSkip.onClick = [this] { dismiss(); };
        btnSkip.setColour(juce::TextButton::buttonColourId,  juce::Colour(0x00000000));
        btnSkip.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF6A5020));
        addAndMakeVisible(btnSkip);
    }

    // Call before adding to parent — returns false if already seen
    static bool shouldShow(const juce::String& pluginCode)
    {
        auto props = getProps();
        return props != nullptr && !props->getBoolValue("seen_" + pluginCode, false);
    }

    void paint(juce::Graphics& g) override
    {
        // ── Dark overlay behind document ──────────────────────────────────
        g.setColour(juce::Colour(0xCC050402));
        g.fillAll();

        // ── Document bounds ───────────────────────────────────────────────
        auto doc = getDocBounds();

        // Very slight rotation for that "tossed on desk" feel
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(
            -0.012f,
            (float)doc.getCentreX(), (float)doc.getCentreY()));

        // Paper shadow
        g.setColour(juce::Colour(0x70000000));
        g.fillRect(doc.translated(5, 6));

        // Manila paper body
        juce::ColourGradient paper(
            juce::Colour(0xFFD4B878), (float)doc.getX(),  (float)doc.getY(),
            juce::Colour(0xFFC4A860), (float)doc.getRight(), (float)doc.getBottom(), false);
        g.setGradientFill(paper);
        g.fillRect(doc);

        // Paper edge texture — subtle darker border
        g.setColour(juce::Colour(0xFF9A8040));
        g.drawRect(doc, 1);

        // Faint horizontal lines (like ruled paper)
        g.setColour(juce::Colour(0x18000000));
        for (int ly = doc.getY() + 90; ly < doc.getBottom() - 20; ly += 22)
            g.drawHorizontalLine(ly, (float)doc.getX() + 12, (float)doc.getRight() - 12);

        // ── CLASSIFIED stamp (red, diagonal) ─────────────────────────────
        {
            juce::Graphics::ScopedSaveState stamp(g);
            g.addTransform(juce::AffineTransform::rotation(
                -0.42f,
                (float)doc.getRight() - 80.0f, (float)doc.getY() + 60.0f));
            g.setColour(juce::Colour(0xBBCC2020));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
            g.drawText("CLASSIFIED",
                       doc.getRight() - 160, doc.getY() + 20, 160, 36,
                       juce::Justification::centred, false);
            g.setColour(juce::Colour(0x88CC2020));
            g.drawRect(doc.getRight() - 164, doc.getY() + 18, 168, 40, 2);
        }

        // ── Header band ───────────────────────────────────────────────────
        g.setColour(juce::Colour(0xFF2A2010));
        g.fillRect(doc.getX(), doc.getY(), doc.getWidth(), 54);

        // Case file label
        g.setColour(juce::Colour(0xFFD09020));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
        g.drawText("DETECTIVE 47  //  CASE FILE  //  " + code,
                   doc.getX() + 14, doc.getY() + 8, doc.getWidth() - 28, 12,
                   juce::Justification::centredLeft, false);

        // Plugin name in header
        g.setColour(juce::Colour(0xFFE8C840));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
        g.drawText(name,
                   doc.getX() + 14, doc.getY() + 22, doc.getWidth() - 28, 24,
                   juce::Justification::centredLeft, false);

        // ── Page number ───────────────────────────────────────────────────
        g.setColour(juce::Colour(0xFF6A5020));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
        g.drawText(juce::String(currentPage + 1) + " / " + juce::String(casePages.size()),
                   doc.getRight() - 60, doc.getY() + 36, 46, 12,
                   juce::Justification::centredRight, false);

        // ── Evidence heading ──────────────────────────────────────────────
        const auto& [heading, body] = casePages[(size_t)currentPage];

        g.setColour(juce::Colour(0xFF2A2010));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
        g.drawText("[ EVIDENCE " + juce::String(currentPage + 1) + " ]  " + heading,
                   doc.getX() + 14, doc.getY() + 66, doc.getWidth() - 28, 16,
                   juce::Justification::centredLeft, false);

        // Underline
        g.setColour(juce::Colour(0xFF9A8040));
        g.drawHorizontalLine(doc.getY() + 84,
                             (float)doc.getX() + 14, (float)doc.getRight() - 14);

        // ── Body text ─────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xFF1E1808));
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));

        juce::AttributedString as;
        as.setJustification(juce::Justification::topLeft);
        as.setWordWrap(juce::AttributedString::byWord);
        as.append(body,
                  juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain),
                  juce::Colour(0xFF1E1808));

        juce::TextLayout tl;
        tl.createLayout(as, (float)doc.getWidth() - 28.0f);
        tl.draw(g, juce::Rectangle<float>(
            (float)doc.getX() + 14.0f, (float)doc.getY() + 92.0f,
            (float)doc.getWidth() - 28.0f, (float)doc.getHeight() - 150.0f));

        // ── Progress dots ─────────────────────────────────────────────────
        {
            int total  = (int)casePages.size();
            float dotR = 5.0f, gap = 14.0f;
            float totalW = total * dotR * 2 + (total - 1) * (gap - dotR * 2);
            float startX = (float)doc.getCentreX() - totalW * 0.5f;
            float dotY   = (float)doc.getBottom() - 28.0f;

            for (int i = 0; i < total; ++i)
            {
                float dx = startX + i * gap;
                g.setColour(i == currentPage
                            ? juce::Colour(0xFFD09020)
                            : juce::Colour(0xFF9A8040));
                g.fillEllipse(dx, dotY, dotR * 2, dotR * 2);
            }
        }

        g.restoreState();
    }

    void resized() override
    {
        auto doc = getDocBounds();
        int bY = doc.getBottom() - 46;
        btnNext.setBounds(doc.getRight() - 130, bY, 116, 28);
        btnSkip.setBounds(doc.getX() + 10,      bY, 130, 28);
    }

    void mouseDown(const juce::MouseEvent&) override {} // absorb clicks

private:
    juce::String code, name;
    std::vector<std::pair<juce::String, juce::String>> casePages;
    int currentPage = 0;
    juce::TextButton btnNext, btnSkip;

    juce::Rectangle<int> getDocBounds() const
    {
        int dw = juce::jmin(getWidth()  - 60, 560);
        int dh = juce::jmin(getHeight() - 60, 400);
        return { (getWidth()  - dw) / 2,
                 (getHeight() - dh) / 2,
                 dw, dh };
    }

    void advance()
    {
        if (currentPage < (int)casePages.size() - 1)
        {
            ++currentPage;
            if (currentPage == (int)casePages.size() - 1)
                btnNext.setButtonText("CASE CLOSED");
            repaint();
        }
        else
        {
            dismiss();
        }
    }

    void dismiss()
    {
        if (auto props = getProps())
            props->setValue("seen_" + code, true);
        setVisible(false);
        if (auto* p = getParentComponent())
            p->removeChildComponent(this);
    }

    static juce::PropertiesFile* getProps()
    {
        static juce::ApplicationProperties appProps;
        if (appProps.getUserSettings() == nullptr)
        {
            juce::PropertiesFile::Options opts;
            opts.applicationName     = "Detective47Plugins";
            opts.filenameSuffix      = ".settings";
            opts.osxLibrarySubFolder = "Application Support";
            appProps.setStorageParameters(opts);
        }
        return appProps.getUserSettings();
    }
};
