#include "PluginEditor.h"

using namespace casefile;

//==============================================================================
// PLUGIN LIBRARY TAB
//==============================================================================
PluginLibTab::PluginLibTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[8] = { "SEARCH", "CATEGORY FILTER", "PLUGIN NAME",
                             "DEVELOPER / COMPANY", "CATEGORY", "OWNED / DEMO / TRIAL",
                             "BEST USE", "NOTES" };
    for (int i = 0; i < 8; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleEditor (searchEd);
    searchEd.setTextToShowWhenEmpty ("type to search...", theme::inkDim);
    searchEd.onTextChange = [this] { list.deselectAllRows(); list.updateContent(); list.repaint(); };
    addAndMakeVisible (searchEd);

    filterBox.addItem ("All categories", 1);
    filterBox.addItem ("Favorites only", 2);
    for (int i = 0; i < pluginCategories().size(); ++i)
        filterBox.addItem (pluginCategories()[i], i + 3);
    filterBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (filterBox);
    filterBox.onChange = [this] { list.deselectAllRows(); list.updateContent(); list.repaint(); };
    addAndMakeVisible (filterBox);

    list.setRowHeight (24);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    auto single = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed);
        theme::bindText (ed, prop, item);
        addAndMakeVisible (ed);
    };
    single (nameEd,    ids::name);
    single (companyEd, ids::company);
    single (bestUseEd, ids::bestUse);
    theme::styleEditor (notesEd, true);
    theme::bindText (notesEd, ids::notes, item);
    addAndMakeVisible (notesEd);
    nameEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::name, nameEd.getText(), nullptr); list.repaint(); }
    };

    for (int i = 0; i < pluginCategories().size(); ++i)
        categoryBox.addItem (pluginCategories()[i], i + 1);
    theme::styleCombo (categoryBox);
    categoryBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && categoryBox.getSelectedId() > 0)
        { t.setProperty (ids::category, categoryBox.getSelectedId() - 1, nullptr); list.repaint(); }
    };
    addAndMakeVisible (categoryBox);

    for (int i = 0; i < ownershipTypes().size(); ++i)
        ownershipBox.addItem (ownershipTypes()[i], i + 1);
    theme::styleCombo (ownershipBox);
    ownershipBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && ownershipBox.getSelectedId() > 0)
            t.setProperty (ids::ownership, ownershipBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (ownershipBox);

    auto toggle = [&] (juce::ToggleButton& t, const juce::Identifier& prop)
    {
        theme::styleToggle (t);
        t.onClick = [this, &t, prop]
        {
            auto tree = selected();
            if (tree.isValid()) { tree.setProperty (prop, t.getToggleState(), nullptr); list.repaint(); }
        };
        addAndMakeVisible (t);
    };
    toggle (favToggle,   ids::favorite);
    toggle (cpuToggle,   ids::cpuHeavy);
    toggle (oftenToggle, ids::usedOften);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        proc.bulkAddPlugins ("New Plugin");
        searchEd.clear();
        filterBox.setSelectedId (1, juce::dontSendNotification);
        list.updateContent();
        list.selectRow (getNumRows() - 1);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.pluginLib(), t); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    theme::styleButton (scanBtn, theme::stamp.brighter (0.1f));
    scanBtn.setTooltip ("Reads plugin names from your VST3/AU folders on disk. "
                        "No plugin code is loaded, so it can't crash or slow the DAW.");
    scanBtn.onClick = [this]
    {
        const int n = proc.scanPluginFolders();
        statusLabel.setText (n > 0 ? "Scan complete — " + juce::String (n)
                                       + " new plugins filed. Check categories and mark favorites."
                                   : "Scan complete — nothing new. The library already has everything on disk.",
                             juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (scanBtn);

    theme::styleButton (pasteBtn, theme::brass);
    pasteBtn.onClick = [this]
    {
        const int n = proc.bulkAddPlugins (juce::SystemClipboard::getTextFromClipboard());
        statusLabel.setText (n > 0 ? "Added " + juce::String (n) + " plugins from clipboard "
                                     "(one per line: Name, Company, Category)."
                                   : "Clipboard was empty — copy lines like: Pro-Q 3, FabFilter, EQ",
                             juce::dontSendNotification);
        refresh();
    };
    addAndMakeVisible (pasteBtn);

    theme::styleButton (importBtn, theme::brass);
    importBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import plugin list (CSV/TXT)",
                                                       juce::File(), "*.csv;*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                const int n = proc.importPluginCSV (f.loadFileAsString());
                statusLabel.setText ("Imported " + juce::String (n) + " plugins from "
                                     + f.getFileName(), juce::dontSendNotification);
                refresh();
            });
    };
    addAndMakeVisible (importBtn);

    theme::styleButton (exportBtn, theme::approve);
    exportBtn.onClick = [this]
    {
        auto folder = CaseFileProcessor::caseFileFolder();
        folder.createDirectory();
        auto f = folder.getChildFile ("PluginLibrary_"
                    + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".csv");
        if (f.replaceWithText (proc.pluginLibCSV()))
        {
            statusLabel.setText ("Exported: " + f.getFullPathName(), juce::dontSendNotification);
            f.revealToUser();
        }
        else
            statusLabel.setText ("Could not write file.", juce::dontSendNotification);
    };
    addAndMakeVisible (exportBtn);

    statusLabel.setFont (theme::type (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (statusLabel);

    theme::styleHint (emptyHint, "No plugins on file yet.\n"
                                 "SCAN FOLDERS reads your VST3/AU folders automatically — "
                                 "or ADD PLUGIN / BULK PASTE / IMPORT CSV.");
    addAndMakeVisible (emptyHint);

    refresh();
}

juce::Array<juce::ValueTree> PluginLibTab::visibleItems() const
{
    juce::Array<juce::ValueTree> out;
    const auto needle = searchEd.getText().trim();
    const int sel = filterBox.getSelectedId();
    for (auto t : proc.pluginLib())
    {
        if (sel == 2 && ! (bool) t.getProperty (ids::favorite)) continue;
        if (sel >= 3 && (int) t.getProperty (ids::category) != sel - 3) continue;
        if (needle.isNotEmpty()
              && ! t.getProperty (ids::name).toString().containsIgnoreCase (needle)
              && ! t.getProperty (ids::company).toString().containsIgnoreCase (needle))
            continue;
        out.add (t);
    }
    return out;
}

int PluginLibTab::getNumRows() { return visibleItems().size(); }

juce::ValueTree PluginLibTab::selected() const
{
    auto items = visibleItems();
    const int row = list.getSelectedRow();
    return row >= 0 && row < items.size() ? items[row] : juce::ValueTree();
}

void PluginLibTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto items = visibleItems();
    if (row >= items.size()) return;
    auto t = items[row];
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }

    g.setColour (theme::ink);
    g.setFont (theme::type (12.5f, sel));
    juce::String label = t.getProperty (ids::name).toString();
    if ((bool) t.getProperty (ids::favorite)) label = juce::String::fromUTF8 ("\xe2\x98\x85 ") + label;
    g.drawText (label, 8, 0, w / 2 - 8, h, juce::Justification::centredLeft);

    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.5f));
    juce::String meta = t.getProperty (ids::company).toString();
    if (meta.isNotEmpty()) meta << "  •  ";
    meta << pluginCategories()[safeIndex (t.getProperty (ids::category), pluginCategories())];
    if ((bool) t.getProperty (ids::cpuHeavy)) meta << "  •  CPU!";
    g.drawText (meta, w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
}

void PluginLibTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    for (auto* e : { &nameEd, &companyEd, &bestUseEd, &notesEd }) e->setEnabled (has);
    for (auto* b : { &favToggle, &cpuToggle, &oftenToggle }) b->setEnabled (has);
    categoryBox.setEnabled (has);
    ownershipBox.setEnabled (has);

    theme::setIfChanged (nameEd,    has ? t.getProperty (ids::name).toString() : juce::String());
    theme::setIfChanged (companyEd, has ? t.getProperty (ids::company).toString() : juce::String());
    theme::setIfChanged (bestUseEd, has ? t.getProperty (ids::bestUse).toString() : juce::String());
    theme::setIfChanged (notesEd,   has ? t.getProperty (ids::notes).toString() : juce::String());
    categoryBox.setSelectedId (has ? safeIndex (t.getProperty (ids::category), pluginCategories()) + 1 : 0,
                               juce::dontSendNotification);
    ownershipBox.setSelectedId (has ? safeIndex (t.getProperty (ids::ownership), ownershipTypes()) + 1 : 0,
                                juce::dontSendNotification);
    favToggle.setToggleState   (has && (bool) t.getProperty (ids::favorite),  juce::dontSendNotification);
    cpuToggle.setToggleState   (has && (bool) t.getProperty (ids::cpuHeavy),  juce::dontSendNotification);
    oftenToggle.setToggleState (has && (bool) t.getProperty (ids::usedOften), juce::dontSendNotification);
}

void PluginLibTab::refresh()
{
    list.updateContent();
    if (getNumRows() > 0 && list.getSelectedRow() < 0)
        list.selectRow (0);
    emptyHint.setVisible (getNumRows() == 0);
    list.repaint();
    refreshDetail();
}

void PluginLibTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (44);
    auto s = top.removeFromLeft (170);
    placeField (s, caps[0], searchEd);
    top.removeFromLeft (10);
    auto f = top.removeFromLeft (160);
    placeField (f, caps[1], filterBox);
    top.removeFromLeft (14);
    auto btns = top.withTrimmedTop (14).withHeight (26);
    addBtn.setBounds (btns.removeFromLeft (100));
    btns.removeFromLeft (6);
    deleteBtn.setBounds (btns.removeFromLeft (72));
    btns.removeFromLeft (6);
    scanBtn.setBounds (btns.removeFromLeft (112));
    btns.removeFromLeft (6);
    pasteBtn.setBounds (btns.removeFromLeft (96));
    btns.removeFromLeft (6);
    importBtn.setBounds (btns.removeFromLeft (96));
    btns.removeFromLeft (6);
    exportBtn.setBounds (btns.removeFromLeft (96));

    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromBottom (16));

    auto detail = r.removeFromRight (juce::jmax (260, r.getWidth() * 38 / 100));
    r.removeFromRight (14);
    list.setBounds (r);
    emptyHint.setBounds (r);

    placeField (detail, caps[2], nameEd);
    placeField (detail, caps[3], companyEd);
    {
        auto row = detail.removeFromTop (48);
        auto c1 = row.removeFromLeft ((row.getWidth() - 10) / 2);
        row.removeFromLeft (10);
        placeField (c1,  caps[4], categoryBox);
        placeField (row, caps[5], ownershipBox);
    }
    auto togglesRow = detail.removeFromTop (24);
    favToggle.setBounds (togglesRow.removeFromLeft (togglesRow.getWidth() / 3));
    cpuToggle.setBounds (togglesRow.removeFromLeft (togglesRow.getWidth() / 2));
    oftenToggle.setBounds (togglesRow);
    detail.removeFromTop (8);
    placeField (detail, caps[6], bestUseEd);
    caps[7].setBounds (detail.removeFromTop (14));
    notesEd.setBounds (detail.withTrimmedBottom (2));
}

//==============================================================================
// HARDWARE LOCKER TAB
//==============================================================================
HardwareTab::HardwareTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[11] = { "SEARCH", "TYPE FILTER", "GEAR NAME", "BRAND",
                              "GEAR TYPE", "MONO / STEREO", "CHANNELS",
                              "CONNECTION / INSERT PATH", "FAVORITE USE",
                              "NOTES + RECALL NOTES", "GEAR PHOTOS (RECALL)" };
    for (int i = 0; i < 11; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleEditor (searchEd);
    searchEd.setTextToShowWhenEmpty ("type to search...", theme::inkDim);
    searchEd.onTextChange = [this] { list.deselectAllRows(); list.updateContent(); list.repaint(); };
    addAndMakeVisible (searchEd);

    filterBox.addItem ("All types", 1);
    filterBox.addItem ("Favorites only", 2);
    for (int i = 0; i < hardwareTypes().size(); ++i)
        filterBox.addItem (hardwareTypes()[i], i + 3);
    filterBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (filterBox);
    filterBox.onChange = [this] { list.deselectAllRows(); list.updateContent(); list.repaint(); };
    addAndMakeVisible (filterBox);

    list.setRowHeight (24);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    auto single = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed);
        theme::bindText (ed, prop, item);
        addAndMakeVisible (ed);
    };
    single (nameEd,     ids::name);
    single (brandEd,    ids::brand);
    single (channelsEd, ids::numChannels);
    single (insertEd,   ids::insertPath);
    single (favUseEd,   ids::favoriteUse);
    theme::styleEditor (notesEd, true);
    theme::bindText (notesEd, ids::notes, item);
    addAndMakeVisible (notesEd);
    theme::styleEditor (recallEd, true);
    theme::bindText (recallEd, ids::recallNotes, item);
    addAndMakeVisible (recallEd);
    nameEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::name, nameEd.getText(), nullptr); list.repaint(); }
    };
    channelsEd.setInputRestrictions (3, "0123456789");

    for (int i = 0; i < hardwareTypes().size(); ++i)
        typeBox.addItem (hardwareTypes()[i], i + 1);
    theme::styleCombo (typeBox);
    typeBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && typeBox.getSelectedId() > 0)
        { t.setProperty (ids::gearType, typeBox.getSelectedId() - 1, nullptr); list.repaint(); }
    };
    addAndMakeVisible (typeBox);

    for (int i = 0; i < stereoMonoTypes().size(); ++i)
        stereoBox.addItem (stereoMonoTypes()[i], i + 1);
    theme::styleCombo (stereoBox);
    stereoBox.onChange = [this]
    {
        auto t = selected();
        if (t.isValid() && stereoBox.getSelectedId() > 0)
            t.setProperty (ids::stereoMono, stereoBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (stereoBox);

    theme::styleToggle (favToggle);
    favToggle.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::favorite, favToggle.getToggleState(), nullptr); list.repaint(); }
    };
    addAndMakeVisible (favToggle);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        auto t = proc.addChildTo (proc.hardware(), ids::HardwareItem);
        t.setProperty (ids::name, "New Gear", nullptr);
        t.setProperty (ids::gearType, hardwareTypes().size() - 1, nullptr);
        t.setProperty (ids::stereoMono, 0, nullptr);
        t.setProperty (ids::favorite, false, nullptr);
        for (auto* prop : { &ids::brand, &ids::numChannels, &ids::insertPath,
                            &ids::favoriteUse, &ids::notes, &ids::recallNotes,
                            &ids::maintenanceNotes })
            t.setProperty (*prop, "", nullptr);
        searchEd.clear();
        filterBox.setSelectedId (1, juce::dontSendNotification);
        list.updateContent();
        list.selectRow (getNumRows() - 1);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.hardware(), t); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    theme::styleButton (exportBtn, theme::approve);
    exportBtn.onClick = [this]
    {
        auto folder = CaseFileProcessor::caseFileFolder();
        folder.createDirectory();
        auto f = folder.getChildFile ("HardwareLocker_"
                    + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".csv");
        if (f.replaceWithText (proc.hardwareCSV()))
        {
            statusLabel.setText ("Exported: " + f.getFullPathName(), juce::dontSendNotification);
            f.revealToUser();
        }
        else
            statusLabel.setText ("Could not write file.", juce::dontSendNotification);
    };
    addAndMakeVisible (exportBtn);

    statusLabel.setFont (theme::type (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (statusLabel);

    theme::styleHint (emptyHint, "Locker's empty — hit ADD GEAR to file your first piece.\n"
                                 "Then attach photos of settings for recall.");
    addAndMakeVisible (emptyHint);

    // --- gear photo panel ---------------------------------------------------
    photoView.setImagePlacement (juce::RectanglePlacement::centred);
    addAndMakeVisible (photoView);

    photoCounter.setFont (theme::type (11.0f));
    photoCounter.setColour (juce::Label::textColourId, theme::inkDim);
    photoCounter.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (photoCounter);

    theme::styleButton (addPhotoBtn, theme::approve);
    addPhotoBtn.onClick = [this]
    {
        if (! selected().isValid()) return;
        photoChooser = std::make_unique<juce::FileChooser> (
            "Add gear photo(s)", juce::File(), "*.jpg;*.jpeg;*.png;*.heic;*.gif;*.bmp");
        photoChooser->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                auto t = selected();
                if (! t.isValid()) return;
                int n = 0;
                for (const auto& f : fc.getResults())
                    if (proc.addHardwarePhoto (t, f).isValid()) ++n;
                if (n > 0)
                {
                    photoIndex = t.getChildWithName (ids::Photos).getNumChildren() - 1;
                    statusLabel.setText ("Filed " + juce::String (n) + " photo(s) — archived copies in "
                                         + CaseFileProcessor::gearPhotosFolder().getFullPathName(),
                                         juce::dontSendNotification);
                }
                refreshPhoto();
            });
    };
    addAndMakeVisible (addPhotoBtn);

    theme::styleButton (removePhotoBtn, theme::stamp);
    removePhotoBtn.onClick = [this]
    {
        auto t = selected();
        auto photos = t.getChildWithName (ids::Photos);
        if (photos.isValid() && photoIndex < photos.getNumChildren())
        {
            photos.removeChild (photoIndex, nullptr);      // keeps the archived file
            photoIndex = juce::jmax (0, photoIndex - 1);
            refreshPhoto();
        }
    };
    addAndMakeVisible (removePhotoBtn);

    theme::styleButton (prevPhotoBtn, theme::brass);
    theme::styleButton (nextPhotoBtn, theme::brass);
    prevPhotoBtn.onClick = [this] { --photoIndex; refreshPhoto(); };
    nextPhotoBtn.onClick = [this] { ++photoIndex; refreshPhoto(); };
    addAndMakeVisible (prevPhotoBtn);
    addAndMakeVisible (nextPhotoBtn);

    refresh();
}

bool HardwareTab::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("jpg;jpeg;png;heic;gif;bmp"))
            return true;
    return false;
}

void HardwareTab::filesDropped (const juce::StringArray& files, int, int)
{
    auto t = selected();
    if (! t.isValid())
    {
        statusLabel.setText ("Select (or add) a piece of gear first, then drop its photo.",
                             juce::dontSendNotification);
        return;
    }
    int n = 0;
    for (const auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("jpg;jpeg;png;heic;gif;bmp")
              && proc.addHardwarePhoto (t, f).isValid())
            ++n;
    }
    if (n > 0)
    {
        photoIndex = t.getChildWithName (ids::Photos).getNumChildren() - 1;
        statusLabel.setText ("Filed " + juce::String (n) + " photo(s) for "
                             + t.getProperty (ids::name).toString() + ".",
                             juce::dontSendNotification);
    }
    refreshPhoto();
}

juce::Array<juce::ValueTree> HardwareTab::visibleItems() const
{
    juce::Array<juce::ValueTree> out;
    const auto needle = searchEd.getText().trim();
    const int sel = filterBox.getSelectedId();
    for (auto t : proc.hardware())
    {
        if (sel == 2 && ! (bool) t.getProperty (ids::favorite)) continue;
        if (sel >= 3 && (int) t.getProperty (ids::gearType) != sel - 3) continue;
        if (needle.isNotEmpty()
              && ! t.getProperty (ids::name).toString().containsIgnoreCase (needle)
              && ! t.getProperty (ids::brand).toString().containsIgnoreCase (needle))
            continue;
        out.add (t);
    }
    return out;
}

int HardwareTab::getNumRows() { return visibleItems().size(); }

juce::ValueTree HardwareTab::selected() const
{
    auto items = visibleItems();
    const int row = list.getSelectedRow();
    return row >= 0 && row < items.size() ? items[row] : juce::ValueTree();
}

void HardwareTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto items = visibleItems();
    if (row >= items.size()) return;
    auto t = items[row];
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }

    g.setColour (theme::ink);
    g.setFont (theme::type (12.5f, sel));
    juce::String label = t.getProperty (ids::name).toString();
    if ((bool) t.getProperty (ids::favorite)) label = juce::String::fromUTF8 ("\xe2\x98\x85 ") + label;
    g.drawText (label, 8, 0, w / 2 - 8, h, juce::Justification::centredLeft);

    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.5f));
    juce::String meta = t.getProperty (ids::brand).toString();
    if (meta.isNotEmpty()) meta << "  •  ";
    meta << hardwareTypes()[safeIndex (t.getProperty (ids::gearType), hardwareTypes())];
    g.drawText (meta, w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
}

void HardwareTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    for (auto* e : { &nameEd, &brandEd, &channelsEd, &insertEd, &favUseEd, &notesEd, &recallEd })
        e->setEnabled (has);
    favToggle.setEnabled (has);
    typeBox.setEnabled (has);
    stereoBox.setEnabled (has);

    theme::setIfChanged (nameEd,     has ? t.getProperty (ids::name).toString() : juce::String());
    theme::setIfChanged (brandEd,    has ? t.getProperty (ids::brand).toString() : juce::String());
    theme::setIfChanged (channelsEd, has ? t.getProperty (ids::numChannels).toString() : juce::String());
    theme::setIfChanged (insertEd,   has ? t.getProperty (ids::insertPath).toString() : juce::String());
    theme::setIfChanged (favUseEd,   has ? t.getProperty (ids::favoriteUse).toString() : juce::String());
    theme::setIfChanged (notesEd,    has ? t.getProperty (ids::notes).toString() : juce::String());
    theme::setIfChanged (recallEd,   has ? t.getProperty (ids::recallNotes).toString() : juce::String());
    typeBox.setSelectedId (has ? safeIndex (t.getProperty (ids::gearType), hardwareTypes()) + 1 : 0,
                           juce::dontSendNotification);
    stereoBox.setSelectedId (has ? safeIndex (t.getProperty (ids::stereoMono), stereoMonoTypes()) + 1 : 0,
                             juce::dontSendNotification);
    favToggle.setToggleState (has && (bool) t.getProperty (ids::favorite), juce::dontSendNotification);
    refreshPhoto();
}

void HardwareTab::refreshPhoto()
{
    auto t = selected();
    auto photos = t.isValid() ? t.getChildWithName (ids::Photos) : juce::ValueTree();
    const int count = photos.isValid() ? photos.getNumChildren() : 0;
    photoIndex = count > 0 ? juce::jlimit (0, count - 1, photoIndex) : 0;

    addPhotoBtn.setEnabled (t.isValid());
    removePhotoBtn.setEnabled (count > 0);
    prevPhotoBtn.setEnabled (photoIndex > 0);
    nextPhotoBtn.setEnabled (photoIndex < count - 1);

    juce::Image img;
    if (count > 0)
    {
        const juce::File f (photos.getChild (photoIndex).getProperty (ids::path).toString());
        if (f.existsAsFile())
            img = juce::ImageCache::getFromFile (f);
        photoCounter.setText (juce::String (photoIndex + 1) + " / " + juce::String (count)
                              + (img.isValid() ? "" : "  (file missing)"),
                              juce::dontSendNotification);
    }
    else
        photoCounter.setText (t.isValid() ? "No photos yet — ADD PHOTO or drop an image here."
                                          : "", juce::dontSendNotification);
    photoView.setImage (img);
}

void HardwareTab::refresh()
{
    list.updateContent();
    if (getNumRows() > 0 && list.getSelectedRow() < 0)
        list.selectRow (0);
    emptyHint.setVisible (getNumRows() == 0);
    list.repaint();
    refreshDetail();
}

void HardwareTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (44);
    auto s = top.removeFromLeft (juce::jmax (160, top.getWidth() / 4));
    placeField (s, caps[0], searchEd);
    top.removeFromLeft (10);
    auto f = top.removeFromLeft (170);
    placeField (f, caps[1], filterBox);
    top.removeFromLeft (14);
    auto btns = top.withTrimmedTop (14).withHeight (26);
    addBtn.setBounds (btns.removeFromLeft (100));
    btns.removeFromLeft (6);
    deleteBtn.setBounds (btns.removeFromLeft (78));
    btns.removeFromLeft (6);
    exportBtn.setBounds (btns.removeFromLeft (100));

    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromBottom (16));

    auto detail = r.removeFromRight (juce::jmax (280, r.getWidth() * 40 / 100));
    r.removeFromRight (14);

    // left column: gear list on top, recall-photo panel below it
    auto photoArea = r.removeFromBottom (juce::jmin (200, r.getHeight() / 2));
    r.removeFromBottom (6);
    list.setBounds (r);
    emptyHint.setBounds (r);

    caps[10].setBounds (photoArea.removeFromTop (14));
    auto photoBtns = photoArea.removeFromBottom (26);
    prevPhotoBtn.setBounds (photoBtns.removeFromLeft (34));
    photoBtns.removeFromLeft (4);
    nextPhotoBtn.setBounds (photoBtns.removeFromLeft (34));
    photoBtns.removeFromLeft (6);
    addPhotoBtn.setBounds (photoBtns.removeFromLeft (juce::jmax (90, photoBtns.getWidth() / 2 - 40)));
    photoBtns.removeFromLeft (6);
    removePhotoBtn.setBounds (photoBtns);
    photoCounter.setBounds (photoArea.removeFromBottom (16));
    photoView.setBounds (photoArea.reduced (0, 2));

    {
        auto row = detail.removeFromTop (48);
        auto c1 = row.removeFromLeft ((row.getWidth() - 10) / 2);
        row.removeFromLeft (10);
        placeField (c1,  caps[2], nameEd);
        placeField (row, caps[3], brandEd);
    }
    {
        auto row = detail.removeFromTop (48);
        auto c1 = row.removeFromLeft ((row.getWidth() - 10) / 2);
        row.removeFromLeft (10);
        placeField (c1,  caps[4], typeBox);
        placeField (row, caps[5], stereoBox);
    }
    {
        auto row = detail.removeFromTop (48);
        auto c1 = row.removeFromLeft (90);
        row.removeFromLeft (10);
        placeField (c1, caps[6], channelsEd);
        auto favCell = row.removeFromRight (100).withTrimmedTop (14).withHeight (26);
        favToggle.setBounds (favCell);
        placeField (row, caps[7], insertEd);
    }
    placeField (detail, caps[8], favUseEd);
    caps[9].setBounds (detail.removeFromTop (14));
    notesEd.setBounds (detail.removeFromTop (juce::jmax (50, (detail.getHeight() - 8) / 2)));
    detail.removeFromTop (6);
    recallEd.setBounds (detail.withTrimmedBottom (2));
}

//==============================================================================
// CHAINS / RECALL TAB
//==============================================================================
ChainsTab::ChainsTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[13] = { "RECALL SHEETS", "TRACK NAME", "MIC", "PREAMP",
                              "COMPRESSOR", "EQ", "DE-ESSER", "FX SENDS",
                              "MAIN PROBLEM", "CURRENT PLAN", "OUTBOARD CHAIN",
                              "PLUGIN CHAIN (IN ORDER)", "NOTES + REVISION HISTORY" };
    for (int i = 0; i < 13; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    for (int i = 0; i < chainTemplates().size(); ++i)
        templateBox.addItem (chainTemplates()[i], i + 1);
    templateBox.setSelectedId (1, juce::dontSendNotification);
    theme::styleCombo (templateBox);
    addAndMakeVisible (templateBox);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        proc.addChain (juce::jmax (0, templateBox.getSelectedId() - 1));
        list.updateContent();
        list.selectRow (proc.chains().getNumChildren() - 1);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.chains(), t); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    list.setRowHeight (24);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    auto single = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed);
        theme::bindText (ed, prop, item);
        addAndMakeVisible (ed);
    };
    single (trackNameEd, ids::trackName);
    single (micEd,       ids::mic);
    single (preampEd,    ids::preamp);
    single (compEd,      ids::compressor);
    single (eqEd,        ids::eq);
    single (deesserEd,   ids::deesser);
    single (fxSendsEd,   ids::fxSends);
    single (problemEd,   ids::mainProblem);
    single (planEd,      ids::plan);
    single (outboardEd,  ids::outboardChain);
    theme::styleEditor (pluginChainEd, true);
    theme::bindText (pluginChainEd, ids::pluginChain, item);
    addAndMakeVisible (pluginChainEd);
    theme::styleEditor (notesEd, true);
    theme::bindText (notesEd, ids::notes, item);
    addAndMakeVisible (notesEd);
    trackNameEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::trackName, trackNameEd.getText(), nullptr); list.repaint(); }
    };

    theme::styleHint (emptyHint, "No recall sheets yet —\npick a template and hit ADD CHAIN.");
    addAndMakeVisible (emptyHint);

    refresh();
}

int ChainsTab::getNumRows() { return proc.chains().getNumChildren(); }

juce::ValueTree ChainsTab::selected() const
{
    return proc.chains().getChild (list.getSelectedRow());
}

void ChainsTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto t = proc.chains().getChild (row);
    if (! t.isValid()) return;
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }
    g.setColour (theme::ink);
    g.setFont (theme::type (12.5f, sel));
    g.drawText (t.getProperty (ids::trackName).toString(), 8, 0, w - 130, h,
                juce::Justification::centredLeft);
    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.0f));
    g.drawText (chainTemplates()[safeIndex (t.getProperty (ids::trackType), chainTemplates())],
                w - 126, 0, 118, h, juce::Justification::centredRight);
}

void ChainsTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    for (auto* e : { &trackNameEd, &micEd, &preampEd, &compEd, &eqEd, &deesserEd,
                     &fxSendsEd, &problemEd, &planEd, &outboardEd, &pluginChainEd, &notesEd })
        e->setEnabled (has);

    auto v = [&] (const juce::Identifier& prop)
    { return has ? t.getProperty (prop).toString() : juce::String(); };
    theme::setIfChanged (trackNameEd, v (ids::trackName));
    theme::setIfChanged (micEd,       v (ids::mic));
    theme::setIfChanged (preampEd,    v (ids::preamp));
    theme::setIfChanged (compEd,      v (ids::compressor));
    theme::setIfChanged (eqEd,        v (ids::eq));
    theme::setIfChanged (deesserEd,   v (ids::deesser));
    theme::setIfChanged (fxSendsEd,   v (ids::fxSends));
    theme::setIfChanged (problemEd,   v (ids::mainProblem));
    theme::setIfChanged (planEd,      v (ids::plan));
    theme::setIfChanged (outboardEd,  v (ids::outboardChain));
    theme::setIfChanged (pluginChainEd, v (ids::pluginChain));
    theme::setIfChanged (notesEd,     v (ids::notes));
}

void ChainsTab::refresh()
{
    list.updateContent();
    if (getNumRows() > 0 && list.getSelectedRow() < 0)
        list.selectRow (0);
    emptyHint.setVisible (getNumRows() == 0);
    list.repaint();
    refreshDetail();
}

void ChainsTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (44);
    auto tpl = top.removeFromLeft (170);
    placeField (tpl, caps[0], templateBox);
    top.removeFromLeft (10);
    auto btns = top.withTrimmedTop (14).withHeight (26);
    addBtn.setBounds (btns.removeFromLeft (100));
    btns.removeFromLeft (6);
    deleteBtn.setBounds (btns.removeFromLeft (78));

    r.removeFromTop (6);
    auto listArea = r.removeFromLeft (juce::jmax (180, r.getWidth() * 24 / 100));
    r.removeFromLeft (14);
    list.setBounds (listArea);
    emptyHint.setBounds (listArea);

    auto colA = r.removeFromLeft ((r.getWidth() - 14) / 2);
    r.removeFromLeft (14);
    auto colB = r;

    placeField (colA, caps[1], trackNameEd);
    placeField (colA, caps[2], micEd);
    placeField (colA, caps[3], preampEd);
    placeField (colA, caps[4], compEd);
    placeField (colA, caps[5], eqEd);
    placeField (colA, caps[6], deesserEd);
    caps[11].setBounds (colA.removeFromTop (14));
    pluginChainEd.setBounds (colA.withTrimmedBottom (2));

    placeField (colB, caps[7],  fxSendsEd);
    placeField (colB, caps[8],  problemEd);
    placeField (colB, caps[9],  planEd);
    placeField (colB, caps[10], outboardEd);
    caps[12].setBounds (colB.removeFromTop (14));
    notesEd.setBounds (colB.withTrimmedBottom (2));
}

//==============================================================================
// VERSIONS TAB
//==============================================================================
VersionsTab::VersionsTab (CaseFileProcessor& p) : CaseTab (p)
{
    const char* names[7] = { "NEW VERSION NAME", "VERSION NAME", "PRINTED FILES",
                             "NOTES", "CLIENT FEEDBACK", "CHANGES MADE",
                             "PROBLEMS REMAINING" };
    for (int i = 0; i < 7; ++i)
    {
        theme::caption (caps[i], names[i]);
        addAndMakeVisible (caps[i]);
    }

    theme::styleEditor (newNameEd);
    newNameEd.setTextToShowWhenEmpty ("Mix 1", theme::inkDim);
    addAndMakeVisible (newNameEd);

    theme::styleButton (addBtn, theme::approve);
    addBtn.onClick = [this]
    {
        auto name = newNameEd.getText().trim();
        if (name.isEmpty())
            name = "Mix " + juce::String (proc.versions().getNumChildren() + 1);
        proc.addVersion (name);
        newNameEd.clear();
        list.updateContent();
        list.selectRow (proc.versions().getNumChildren() - 1);
    };
    addAndMakeVisible (addBtn);

    theme::styleButton (deleteBtn, theme::stamp);
    deleteBtn.onClick = [this]
    {
        auto t = selected();
        if (t.isValid()) { proc.removeChild (proc.versions(), t); refresh(); }
    };
    addAndMakeVisible (deleteBtn);

    theme::styleButton (snapshotBtn, theme::brass);
    snapshotBtn.onClick = [this]
    {
        auto t = selected();
        if (! t.isValid()) return;
        t.setProperty (ids::analysisSummary, proc.buildAnalysisSummary(), nullptr);
        statusLabel.setText ("Analysis snapshot attached to "
                             + t.getProperty (ids::name).toString()
                             + " — it prints with the report.", juce::dontSendNotification);
    };
    addAndMakeVisible (snapshotBtn);

    list.setRowHeight (24);
    addAndMakeVisible (list);

    auto item = [this] { return selected(); };
    theme::styleEditor (nameEd);
    theme::bindText (nameEd, ids::name, item);
    addAndMakeVisible (nameEd);
    nameEd.onTextChange = [this]
    {
        auto t = selected();
        if (t.isValid()) { t.setProperty (ids::name, nameEd.getText(), nullptr); list.repaint(); }
    };
    theme::styleEditor (printedEd);
    theme::bindText (printedEd, ids::printedFiles, item);
    printedEd.setTextToShowWhenEmpty ("Full mix, instrumental, TV mix, acapella...", theme::inkDim);
    addAndMakeVisible (printedEd);

    auto multi = [&] (juce::TextEditor& ed, const juce::Identifier& prop)
    {
        theme::styleEditor (ed, true);
        theme::bindText (ed, prop, item);
        addAndMakeVisible (ed);
    };
    multi (notesEd,    ids::notes);
    multi (feedbackEd, ids::clientFeedback);
    multi (changesEd,  ids::changes);
    multi (problemsEd, ids::problems);

    statusLabel.setFont (theme::type (11.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::inkDim);
    addAndMakeVisible (statusLabel);

    theme::styleHint (emptyHint, "No versions logged —\nLOG VERSION opens Mix 1.");
    addAndMakeVisible (emptyHint);

    refresh();
}

int VersionsTab::getNumRows() { return proc.versions().getNumChildren(); }

juce::ValueTree VersionsTab::selected() const
{
    return proc.versions().getChild (list.getSelectedRow());
}

void VersionsTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    auto t = proc.versions().getChild (row);
    if (! t.isValid()) return;
    if (sel) { g.setColour (theme::manila.withAlpha (0.6f)); g.fillRect (0, 0, w, h); }
    g.setColour (theme::ink);
    g.setFont (theme::type (12.5f, sel));
    g.drawText (t.getProperty (ids::name).toString(), 8, 0, w - 130, h,
                juce::Justification::centredLeft);
    g.setColour (theme::inkDim);
    g.setFont (theme::type (11.0f));
    g.drawText (juce::Time ((juce::int64) t.getProperty (ids::dateMs, 0)).formatted ("%b %d %H:%M"),
                w - 126, 0, 118, h, juce::Justification::centredRight);
}

void VersionsTab::refreshDetail()
{
    auto t = selected();
    const bool has = t.isValid();
    for (auto* e : { &nameEd, &printedEd, &notesEd, &feedbackEd, &changesEd, &problemsEd })
        e->setEnabled (has);
    snapshotBtn.setEnabled (has);

    auto v = [&] (const juce::Identifier& prop)
    { return has ? t.getProperty (prop).toString() : juce::String(); };
    theme::setIfChanged (nameEd,     v (ids::name));
    theme::setIfChanged (printedEd,  v (ids::printedFiles));
    theme::setIfChanged (notesEd,    v (ids::notes));
    theme::setIfChanged (feedbackEd, v (ids::clientFeedback));
    theme::setIfChanged (changesEd,  v (ids::changes));
    theme::setIfChanged (problemsEd, v (ids::problems));
}

void VersionsTab::refresh()
{
    list.updateContent();
    if (getNumRows() > 0 && list.getSelectedRow() < 0)
        list.selectRow (0);
    emptyHint.setVisible (getNumRows() == 0);
    list.repaint();
    refreshDetail();
}

void VersionsTab::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (44);
    auto n = top.removeFromLeft (180);
    placeField (n, caps[0], newNameEd);
    top.removeFromLeft (10);
    auto btns = top.withTrimmedTop (14).withHeight (26);
    addBtn.setBounds (btns.removeFromLeft (110));
    btns.removeFromLeft (6);
    deleteBtn.setBounds (btns.removeFromLeft (78));
    btns.removeFromLeft (6);
    snapshotBtn.setBounds (btns.removeFromLeft (200));

    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromBottom (16));

    auto listArea = r.removeFromLeft (juce::jmax (170, r.getWidth() * 22 / 100));
    r.removeFromLeft (14);
    list.setBounds (listArea);
    emptyHint.setBounds (listArea);

    {
        auto row = r.removeFromTop (48);
        auto c1 = row.removeFromLeft (juce::jmin (220, row.getWidth() / 3));
        row.removeFromLeft (10);
        placeField (c1,  caps[1], nameEd);
        placeField (row, caps[2], printedEd);
    }

    auto colA = r.removeFromLeft ((r.getWidth() - 14) / 2);
    r.removeFromLeft (14);
    auto colB = r;

    const int half = (colA.getHeight() - 44) / 2;
    caps[3].setBounds (colA.removeFromTop (14));
    notesEd.setBounds (colA.removeFromTop (half));
    colA.removeFromTop (8);
    caps[4].setBounds (colA.removeFromTop (14));
    feedbackEd.setBounds (colA.withTrimmedBottom (2));

    caps[5].setBounds (colB.removeFromTop (14));
    changesEd.setBounds (colB.removeFromTop (half));
    colB.removeFromTop (8);
    caps[6].setBounds (colB.removeFromTop (14));
    problemsEd.setBounds (colB.withTrimmedBottom (2));
}
