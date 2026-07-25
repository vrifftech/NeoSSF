#include "core/AppModel.hpp"
#include "core/CoreTypes.hpp"
#include "wx_ui.hpp"
#include "NeoGameDirectoryMenu.hpp"
#include "NeoDocumentTabs.hpp"
#include "NeoSettings.hpp"
#include "NeoViewState.hpp"
#include "neossf_icon.xpm"
#include "TabularData.hpp"
#include "core/SSFJson.hpp"
#include "core/SsfPatcher.hpp"

#include <wx/aui/auibook.h>
#include <wx/clipbrd.h>
#include <wx/config.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/grid.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/valnum.h>
#include <wx/wx.h>
#include <wx/version.h>

#include <cstddef>
#include <memory>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace neossf;

constexpr const char* kAppName = "NeoSSF";
constexpr const char* kSsfWildcard = "Soundset files (*.ssf)|*.ssf|All files (*.*)|*.*";
constexpr const char* kTlkWildcard = "Talk table files (*.tlk)|*.tlk|All files (*.*)|*.*";
constexpr const char* kCsvTableWildcard = "CSV files (*.csv)|*.csv";
constexpr const char* kTsvTableWildcard = "TSV files (*.tsv)|*.tsv";
constexpr const char* kXmlTableWildcard = "XML files (*.xml)|*.xml|All files (*.*)|*.*";
constexpr const char* kJsonTableWildcard = "JSON files (*.json)|*.json|All files (*.*)|*.*";

const char* tableWildcardForFormat(neotabular::Format format) {
    switch (format) {
    case neotabular::Format::Csv: return kCsvTableWildcard;
    case neotabular::Format::Tsv: return kTsvTableWildcard;
    case neotabular::Format::Xml: return kXmlTableWildcard;
    case neotabular::Format::Json: return kJsonTableWildcard;
    }
    return kCsvTableWildcard;
}

std::string exportExtensionForFormat(neotabular::Format format) {
    switch (format) {
    case neotabular::Format::Csv: return "csv";
    case neotabular::Format::Tsv: return "tsv";
    case neotabular::Format::Xml: return "xml";
    case neotabular::Format::Json: return "json";
    }
    return "txt";
}

std::string exportDefaultFilename(const std::filesystem::path& source,
                                  neotabular::Format format,
                                  const std::string& fallbackStem) {
    std::string stem = source.empty() ? fallbackStem : source.stem().string();
    if (stem.empty()) stem = fallbackStem.empty() ? std::string("export") : fallbackStem;
    return stem + "." + exportExtensionForFormat(format);
}

constexpr int kGridColumns = 6;

std::string ssfColumnLabel(std::size_t column) {
    static const std::vector<std::string> labels = {"#", "Slot", "StrRef", "Sound ResRef", "TLK Sound", "Text"};
    return column < labels.size() ? labels[column] : ("Column " + std::to_string(column));
}

enum : int {
    ID_OpenSsf = wxID_HIGHEST + 1,
    ID_NewSsf,
    ID_LoadTlk,
    ID_Save,
    ID_SaveAs,
    ID_CloseTab,
    ID_CloseOtherTabs,
    ID_NextTab,
    ID_PreviousTab,
    ID_DocumentTabs,
    ID_Modify,
    ID_AddTlkEntry,
    ID_CopyCells,
    ID_PasteCells,
    ID_Filter,
    ID_ClearFilter,
    ID_FilterColumn,
    ID_ClearColumnFilter,
    ID_ClearAllFilters,
    ID_MoveColumnLeft,
    ID_MoveColumnRight,
    ID_ResetColumnOrder,
    ID_ResetRowOrder,
    ID_ImportCsv,
    ID_ImportTsv,
    ID_ImportXml,
    ID_ImportJson,
    ID_ExportCsv,
    ID_ExportTsv,
    ID_ExportXml,
    ID_ExportJson,
    ID_ExportPatcherPackage,
    ID_DarkMode,
    ID_FontIncrease,
    ID_FontDecrease,
    ID_FontReset,
    ID_Grid
};

constexpr int kRecentFileBaseId = wxID_HIGHEST + 1000;
constexpr int kClearRecentFilesId = kRecentFileBaseId + neosettings::kMaxRecentFiles;

std::string slotText(UInt32 value) {
    return value == kUnsetStrRef ? std::string("-1") : std::to_string(value);
}

std::string pathText(const std::filesystem::path& path) {
    return path.empty() ? std::string{} : path.string();
}

std::optional<std::filesystem::path> readCachedTlkPath() {
    return neosettings::AppSettings(kAppName).lastTlkPath();
}

void writeCachedTlkPath(const std::filesystem::path& path) {
    neosettings::AppSettings(kAppName).setLastTlkPath(path);
}


std::string readTextFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to open text file: " + file.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to open text file for writing: " + file.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("Unable to write text file: " + file.string());
}


class AddTlkEntryDialog final : public wxDialog {
public:
    AddTlkEntryDialog(wxWindow* parent, const std::string& slotLabel)
        : wxDialog(parent, wxID_ANY, "Add TLK Entry", wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* intro = new wxStaticText(this, wxID_ANY,
                                      wxui::toWx("Create a new dialog.tlk entry and assign its new StrRef to: " + slotLabel));
        root->Add(intro, 0, wxEXPAND | wxALL, 10);

        auto* form = new wxFlexGridSizer(2, 8, 8);
        form->AddGrowableCol(1, 1);
        form->Add(new wxStaticText(this, wxID_ANY, "Sound ResRef:"), 0, wxALIGN_CENTER_VERTICAL);
        resref_ = new wxTextCtrl(this, wxID_ANY);
        resref_->SetMaxLength(16);
        form->Add(resref_, 1, wxEXPAND);
        form->Add(new wxStaticText(this, wxID_ANY, "Text:"), 0, wxALIGN_TOP | wxTOP, 4);
        text_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_RICH2);
        form->Add(text_, 1, wxEXPAND);
        root->Add(form, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* buttons = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
        if (buttons) {
            root->Add(buttons, 0, wxEXPAND | wxALL, 10);
        }
        SetSizer(root);
        SetMinSize(FromDIP(wxSize(460, 280)));
        SetInitialSize(FromDIP(wxSize(560, 380)));
    }

    std::string entryText() const { return wxui::toStd(text_->GetValue()); }
    std::string resref() const { return wxui::toStd(resref_->GetValue()); }

private:
    wxTextCtrl* resref_ = nullptr;
    wxTextCtrl* text_ = nullptr;
};

class NeoSSFFrame final : public wxFrame {
public:
    NeoSSFFrame()
        : wxFrame(nullptr, wxID_ANY, "NeoSSF v1.1.0 (SSF file editor)", wxDefaultPosition, wxDefaultSize) {
        setApplicationIcon();
        buildMenus();
        buildMainWindow();
        wxui::createStatusBar(*this, 2);
        darkMode_ = wxui::readDarkMode(kAppName);
        fontScale_ = settings_.fontScale();
        fontScaleWheelFilter_.attach(this, [this](int steps) { changeFontScaleSteps(steps); });
        neoview::bindFontScaleDpiRefresh(this, [this]() { applyFontScale(); });
        applyDarkMode();
        createDocumentTab(false);
        tryLoadCachedTlk();
        SetMinSize(FromDIP(wxSize(760, 520)));
        SetInitialSize(FromDIP(wxSize(980, 700)));
        settings_.restoreWindowPlacement(*this);
        refreshAll();
    }

    void openStartupSsf(const std::filesystem::path& path) {
        if (path.empty()) return;
        try {
            openSsfPath(path);
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

private:

    struct DocumentTab {
        std::unique_ptr<AppModel> model = std::make_unique<AppModel>();
        neoview::DocumentViewState viewState;
        std::string tlkAutoLoadWarning;
        std::string untitledName = "Untitled SSF";
        bool dirty = false;
        wxWindow* tabPage = nullptr;
    };

    bool hasActiveDocument() const {
        return activeDocumentIndex_ != neotabs::npos && activeDocumentIndex_ < documents_.size();
    }

    DocumentTab& activeDocument() { return documents_.at(activeDocumentIndex_); }
    const DocumentTab& activeDocument() const { return documents_.at(activeDocumentIndex_); }
    AppModel& model() { return *activeDocument().model; }
    const AppModel& model() const { return *activeDocument().model; }
    neoview::DocumentViewState& viewState() { return activeDocument().viewState; }
    const neoview::DocumentViewState& viewState() const { return activeDocument().viewState; }
    std::string& tlkAutoLoadWarning() { return activeDocument().tlkAutoLoadWarning; }
    const std::string& tlkAutoLoadWarning() const { return activeDocument().tlkAutoLoadWarning; }

    bool tabDirty(const DocumentTab& tab) const {
        return tab.dirty || (tab.model && tab.model->tlkModified());
    }

    std::string tabDisplayName(const DocumentTab& tab) const {
        return neotabs::displayNameForPath(tab.model ? tab.model->ssfFile() : std::filesystem::path{}, tab.untitledName);
    }

    void updateActiveTabTitle() {
        if (!hasActiveDocument()) return;
        neotabs::setTabLabel(documentTabs_, activeDocument().tabPage, tabDisplayName(activeDocument()), tabDirty(activeDocument()));
    }

    void markDocumentDirty() {
        if (!hasActiveDocument()) return;
        activeDocument().dirty = true;
        updateActiveTabTitle();
    }

    void markDocumentClean() {
        if (!hasActiveDocument()) return;
        activeDocument().dirty = false;
        if (activeDocument().model) activeDocument().model->setTlkModified(false);
        updateActiveTabTitle();
    }

    void selectDocumentTab(std::size_t index) {
        if (documentTabs_ == nullptr || index >= documents_.size()) return;
        tabSwitchInProgress_ = true;
        const bool selected = neotabs::changeSelectionToPage(documentTabs_, documents_[index].tabPage);
        tabSwitchInProgress_ = false;
        if (!selected) return;
        activeDocumentIndex_ = index;
        refreshAll();
    }

    void createDocumentTab(bool markDirty, bool select = true) {
        DocumentTab tab;
        tab.model = std::make_unique<AppModel>();
        tab.model->newSsf();
        tab.dirty = markDirty;
        tab.viewState.resetForNewDocument();
        tab.viewState.selectedLogicalRow = 0;
        const std::size_t previousActiveIndex = activeDocumentIndex_;
        documents_.push_back(std::move(tab));
        const std::size_t index = documents_.size() - 1;

        tabSwitchInProgress_ = true;
        wxWindow* const page = neotabs::addTabPage(
            documentTabs_, tabDisplayName(documents_.back()), tabDirty(documents_.back()), select);
        if (page != nullptr) documents_.back().tabPage = page;
        tabSwitchInProgress_ = false;

        if (page == nullptr) {
            documents_.pop_back();
            activeDocumentIndex_ = previousActiveIndex;
            throw std::runtime_error("Unable to create a document tab.");
        }

        if (select) {
            activeDocumentIndex_ = index;
            tabSwitchInProgress_ = true;
            neotabs::changeSelectionToPage(documentTabs_, page);
            tabSwitchInProgress_ = false;
            refreshAll();
        }
    }

    bool activeTabIsReusableForOpen() const {
        return hasActiveDocument() && documents_.size() == 1 && !tabDirty(activeDocument()) && model().ssfFile().empty();
    }

    void ensureDocumentTabForOpen() {
        if (!hasActiveDocument()) { createDocumentTab(false); return; }
        if (!activeTabIsReusableForOpen()) createDocumentTab(false);
    }

    bool confirmCloseDocumentTab(std::size_t index) {
        if (index >= documents_.size()) return true;
        if (!tabDirty(documents_[index])) return true;
        return wxui::confirm(this, "Close tab", neotabs::closePromptText(tabDisplayName(documents_[index])));
    }

    bool closeDocumentTab(std::size_t index) {
        if (index >= documents_.size() || !confirmCloseDocumentTab(index)) return false;

        wxWindow* const page = documents_[index].tabPage;
        tabSwitchInProgress_ = true;
        const bool deleted = neotabs::deleteTabPage(documentTabs_, page);
        tabSwitchInProgress_ = false;
        if (!deleted) return false;

        documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
        if (documents_.empty()) {
            activeDocumentIndex_ = neotabs::npos;
            createDocumentTab(false);
            return true;
        }

        std::size_t selectedIndex = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (selectedIndex == neotabs::npos) selectedIndex = std::min(index, documents_.size() - 1);
        selectDocumentTab(selectedIndex);
        return true;
    }

    bool confirmCloseAllTabs() {
        for (std::size_t i = 0; i < documents_.size(); ++i) {
            if (!confirmCloseDocumentTab(i)) return false;
        }
        return true;
    }

    void onDocumentTabChanged(wxAuiNotebookEvent& event) {
        if (tabSwitchInProgress_) { event.Skip(); return; }
        const int selection = event.GetSelection();
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::pageForIndex(documentTabs_, selection));
        if (index != neotabs::npos) selectDocumentTab(index);
        event.Skip();
    }

    void onDocumentTabCloseRequested(wxAuiNotebookEvent& event) {
        event.Veto();
        const int selection = event.GetSelection();
        if (selection < 0) return;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::pageForIndex(documentTabs_, selection));
        if (index != neotabs::npos) closeDocumentTab(index);
    }

    void setApplicationIcon() {
        wxIconBundle bundle;
#if defined(__WXMSW__)
        wxIcon windowsIcon("neossf", wxBITMAP_TYPE_ICO_RESOURCE);
        if (windowsIcon.IsOk()) {
            bundle.AddIcon(windowsIcon);
        }
#endif
        wxIcon fallbackIcon(neossf_icon_xpm);
        if (fallbackIcon.IsOk()) {
            bundle.AddIcon(fallbackIcon);
        }
        if (bundle.GetIconCount() > 0) {
            SetIcons(bundle);
        }
    }

    std::unique_ptr<neogames::OpenGameDirectoryMenu> gameDirectoryMenu_;

    void buildMenus() {
        auto* file = new wxMenu;
        file->Append(ID_LoadTlk, "Load optional &dialog.tlk...");
        file->AppendSeparator();
        file->Append(ID_NewSsf, "&New SSF");
        file->Append(ID_OpenSsf, "&Open SSF...");
        recentFilesMenu_ = new wxMenu;
        rebuildRecentFilesMenu();
        file->AppendSubMenu(recentFilesMenu_, "Open &Recent");
        file->Append(ID_Save, "&Save");
        file->Append(ID_SaveAs, "Save &As...");
        file->AppendSeparator();
        file->Append(ID_CloseTab, "&Close Tab\tCtrl-W");
        file->Append(ID_CloseOtherTabs, "Close &Other Tabs");
        file->Append(ID_NextTab, "Next Tab\tCtrl-Tab");
        file->Append(ID_PreviousTab, "Previous Tab\tCtrl-Shift-Tab");
        gameDirectoryMenu_ = neogames::appendOpenGameDirectoryMenu(*this, *file);
        file->AppendSeparator();
        file->Append(wxID_EXIT, "E&xit");

        auto* import = new wxMenu;
        import->Append(ID_ImportCsv, "Import &CSV...");
        import->Append(ID_ImportTsv, "Import &TSV...");
        import->Append(ID_ImportXml, "Import &XML...");
        import->Append(ID_ImportJson, "Import &JSON...");

        auto* exportMenu = new wxMenu;
        exportMenu->Append(ID_ExportCsv, "Export as &CSV...");
        exportMenu->Append(ID_ExportTsv, "Export as &TSV...");
        exportMenu->Append(ID_ExportXml, "Export as &XML...");
        exportMenu->Append(ID_ExportJson, "Export as &JSON...");
        exportMenu->AppendSeparator();
        exportMenu->Append(ID_ExportPatcherPackage, "Export TSLPatcher/HoloPatcher SSF + TLK &Package...");

        auto* edit = new wxMenu;
        edit->Append(ID_CopyCells, "&Copy Cells	Ctrl-C");
        edit->Append(ID_PasteCells, "&Paste Cells	Ctrl-V");
        edit->AppendSeparator();
        edit->Append(ID_Filter, "&Filter/Search...	Ctrl-F");
        edit->Append(ID_FilterColumn, "Filter Selected &Column...");
        edit->Append(ID_ClearColumnFilter, "Clear Filter on Selected Column");
        edit->Append(ID_ClearAllFilters, "Clear &All Filters");
        edit->AppendSeparator();
        edit->Append(ID_Modify, "&Modify selected StrRef");
        edit->Append(ID_AddTlkEntry, "&Add TLK Entry...");

        auto* view = new wxMenu;
        darkModeItem_ = view->AppendCheckItem(ID_DarkMode, "&Dark Mode");
        view->AppendSeparator();
        view->Append(ID_FontIncrease, "Increase Font Size\tCtrl++");
        view->Append(ID_FontDecrease, "Decrease Font Size\tCtrl+-");
        view->Append(ID_FontReset, "Reset Font Size\tCtrl+0");
        view->AppendSeparator();
        view->Append(ID_MoveColumnLeft, "Move Selected Column Left");
        view->Append(ID_MoveColumnRight, "Move Selected Column Right");
        view->Append(ID_ResetColumnOrder, "Reset Column Order");
        view->Append(ID_ResetRowOrder, "Reset Row Order");

        auto* help = new wxMenu;
        help->Append(wxID_ABOUT, "&About");

        auto* bar = new wxMenuBar;
        bar->Append(file, "&File");
        bar->Append(import, "&Import");
        bar->Append(exportMenu, "&Export");
        bar->Append(edit, "&Edit");
        bar->Append(view, "&View");
        bar->Append(help, "&Help");
        SetMenuBar(bar);

        Bind(wxEVT_MENU, &NeoSSFFrame::onLoadTlk, this, ID_LoadTlk);
        Bind(wxEVT_MENU, &NeoSSFFrame::onNewSsf, this, ID_NewSsf);
        Bind(wxEVT_MENU, &NeoSSFFrame::onOpenSsf, this, ID_OpenSsf);
        Bind(wxEVT_MENU, &NeoSSFFrame::onOpenRecent, this, kRecentFileBaseId, kRecentFileBaseId + neosettings::kMaxRecentFiles - 1);
        Bind(wxEVT_MENU, &NeoSSFFrame::onClearRecentFiles, this, kClearRecentFilesId);
        Bind(wxEVT_MENU, &NeoSSFFrame::onSave, this, ID_Save);
        Bind(wxEVT_MENU, &NeoSSFFrame::onSaveAs, this, ID_SaveAs);
        Bind(wxEVT_MENU, &NeoSSFFrame::onCloseTab, this, ID_CloseTab);
        Bind(wxEVT_MENU, &NeoSSFFrame::onCloseOtherTabs, this, ID_CloseOtherTabs);
        Bind(wxEVT_MENU, &NeoSSFFrame::onNextTab, this, ID_NextTab);
        Bind(wxEVT_MENU, &NeoSSFFrame::onPreviousTab, this, ID_PreviousTab);
        Bind(wxEVT_MENU, &NeoSSFFrame::onModify, this, ID_Modify);
        Bind(wxEVT_MENU, &NeoSSFFrame::onAddTlkEntry, this, ID_AddTlkEntry);
        Bind(wxEVT_MENU, &NeoSSFFrame::onCopyCells, this, ID_CopyCells);
        Bind(wxEVT_MENU, &NeoSSFFrame::onPasteCells, this, ID_PasteCells);
        Bind(wxEVT_MENU, &NeoSSFFrame::onFilterPrompt, this, ID_Filter);
        Bind(wxEVT_MENU, &NeoSSFFrame::onClearFilter, this, ID_ClearFilter);
        Bind(wxEVT_MENU, &NeoSSFFrame::onFilterSelectedColumn, this, ID_FilterColumn);
        Bind(wxEVT_MENU, &NeoSSFFrame::onClearSelectedColumnFilter, this, ID_ClearColumnFilter);
        Bind(wxEVT_MENU, &NeoSSFFrame::onClearAllFilters, this, ID_ClearAllFilters);
        Bind(wxEVT_MENU, &NeoSSFFrame::onMoveColumnLeft, this, ID_MoveColumnLeft);
        Bind(wxEVT_MENU, &NeoSSFFrame::onMoveColumnRight, this, ID_MoveColumnRight);
        Bind(wxEVT_MENU, &NeoSSFFrame::onResetColumnOrder, this, ID_ResetColumnOrder);
        Bind(wxEVT_MENU, &NeoSSFFrame::onResetRowOrder, this, ID_ResetRowOrder);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Csv); }, ID_ImportCsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Tsv); }, ID_ImportTsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Xml); }, ID_ImportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onImport(neotabular::Format::Json); }, ID_ImportJson);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Csv); }, ID_ExportCsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Tsv); }, ID_ExportTsv);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Xml); }, ID_ExportXml);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { onExport(neotabular::Format::Json); }, ID_ExportJson);
        Bind(wxEVT_MENU, &NeoSSFFrame::onExportPatcherPackage, this, ID_ExportPatcherPackage);
        Bind(wxEVT_MENU, &NeoSSFFrame::onToggleDarkMode, this, ID_DarkMode);
        Bind(wxEVT_MENU, &NeoSSFFrame::onIncreaseFontScale, this, ID_FontIncrease);
        Bind(wxEVT_MENU, &NeoSSFFrame::onDecreaseFontScale, this, ID_FontDecrease);
        Bind(wxEVT_MENU, &NeoSSFFrame::onResetFontScale, this, ID_FontReset);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            wxui::showMessage(this, "About NeoSSF", "NeoSSF v1.1.0\nNative wxWidgets soundset editor\n\nA special thanks to everyone in the KOTOR modding community that has contributed their work, knowledge, and creativity to making tools, mods, and guides over the last 20+ years");
        }, wxID_ABOUT);
        Bind(wxEVT_CLOSE_WINDOW, &NeoSSFFrame::onClose, this);
    }

    void rebuildRecentFilesMenu() {
        if (recentFilesMenu_ != nullptr) {
            neosettings::populateRecentFilesMenu(*recentFilesMenu_, settings_, kRecentFileBaseId, kClearRecentFilesId);
        }
    }

    void rememberRecentFile(const std::filesystem::path& path) {
        settings_.addRecentFile(path);
        rebuildRecentFilesMenu();
    }

    void tryLoadResolvedTlkForPath(const std::filesystem::path& path) {
        if (model().tlkLoaded()) return;
        const auto tlk = neogames::resolver().bestTlkForPath(path);
        if (!tlk || tlk->empty()) return;
        try {
            model().loadTlk(*tlk);
            writeCachedTlkPath(*tlk);
            tlkAutoLoadWarning().clear();
        } catch (const std::exception& ex) {
            tlkAutoLoadWarning() = std::string("Unable to auto-load resolved TLK: ") + ex.what();
        }
    }

    void openSsfPath(const std::filesystem::path& path) {
        if (path.empty()) return;
        ensureDocumentTabForOpen();
        model().loadSsf(path);
        markDocumentClean();
        viewState().resetForNewDocument();
        viewState().selectedLogicalRow = 0;
        setFilterTerm({});
        tryLoadResolvedTlkForPath(path);
        rememberRecentFile(path);
        neogames::resolver().inferFromOpenedPath(path);
        refreshAll();
    }

    void onOpenRecent(wxCommandEvent& event) {
        const int index = event.GetId() - kRecentFileBaseId;
        const auto files = settings_.recentFiles();
        if (index < 0 || static_cast<std::size_t>(index) >= files.size()) return;
        try {
            if (!std::filesystem::exists(files[static_cast<std::size_t>(index)])) {
                settings_.removeRecentFile(files[static_cast<std::size_t>(index)]);
                rebuildRecentFilesMenu();
                throw std::runtime_error("Recent file no longer exists: " + files[static_cast<std::size_t>(index)].string());
            }
            openSsfPath(files[static_cast<std::size_t>(index)]);
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onClearRecentFiles(wxCommandEvent&) {
        settings_.clearRecentFiles();
        rebuildRecentFilesMenu();
    }

    void buildMainWindow() {
        auto* panel = new wxPanel(this);
        auto* root = new wxBoxSizer(wxVERTICAL);

        documentTabs_ = new wxAuiNotebook(panel, ID_DocumentTabs, wxDefaultPosition, wxDefaultSize,
                                          wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_SCROLL_BUTTONS);
        root->Add(documentTabs_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
        neotabs::configureDocumentTabStrip(documentTabs_);

        auto* pathBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Files");

        auto* tlkRow = new wxBoxSizer(wxHORIZONTAL);
        tlkRow->Add(new wxStaticText(panel, wxID_ANY, "dialog.tlk:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        tlkPath_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        tlkRow->Add(tlkPath_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        tlkRow->Add(new wxButton(panel, ID_LoadTlk, "Load optional TLK..."), 0);
        pathBox->Add(tlkRow, 0, wxEXPAND | wxALL, FromDIP(8));

        auto* ssfRow = new wxBoxSizer(wxHORIZONTAL);
        ssfRow->Add(new wxStaticText(panel, wxID_ANY, "Soundset SSF:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        ssfPath_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        ssfRow->Add(ssfPath_, 1, wxEXPAND | wxRIGHT, FromDIP(6));
        ssfRow->Add(new wxButton(panel, ID_NewSsf, "New"), 0, wxRIGHT, FromDIP(4));
        ssfRow->Add(new wxButton(panel, ID_OpenSsf, "Open..."), 0, wxRIGHT, FromDIP(4));
        ssfRow->Add(new wxButton(panel, ID_Save, "Save"), 0, wxRIGHT, FromDIP(4));
        ssfRow->Add(new wxButton(panel, ID_SaveAs, "Save As..."), 0);
        pathBox->Add(ssfRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

        auto* filterRow = new wxBoxSizer(wxHORIZONTAL);
        filterRow->Add(new wxStaticText(panel, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        filterText_ = new wxTextCtrl(panel, wxID_ANY);
        filterRow->Add(filterText_, 1, wxEXPAND | wxRIGHT, FromDIP(4));
        filterRow->Add(new wxButton(panel, ID_ClearFilter, "Clear"), 0);
        pathBox->Add(filterRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

        root->Add(pathBox, 0, wxEXPAND | wxALL, FromDIP(8));

        auto* gridBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Soundset entries");
        grid_ = new wxGrid(panel, ID_Grid);
        grid_->CreateGrid(0, kGridColumns);
        grid_->SetColLabelValue(0, "#");
        grid_->SetColLabelValue(1, "Slot");
        grid_->SetColLabelValue(2, "StrRef");
        grid_->SetColLabelValue(3, "Sound ResRef");
        grid_->SetColLabelValue(4, "TLK Sound");
        grid_->SetColLabelValue(5, "Text");
        grid_->SetColSize(0, 45);
        grid_->SetColSize(1, 150);
        grid_->SetColSize(2, 95);
        grid_->SetColSize(3, 170);
        grid_->SetColSize(4, 150);
        grid_->SetColSize(5, 430);
        grid_->SetRowLabelSize(0);
        grid_->EnableEditing(false);
        grid_->EnableDragGridSize(false);
        grid_->SetSelectionMode(wxGrid::wxGridSelectRows);
        gridBox->Add(grid_, 1, wxEXPAND | wxALL, 6);
        root->Add(gridBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* editorBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Selected entry");
        auto* selectedGrid = new wxFlexGridSizer(2, 8, 6);
        selectedGrid->AddGrowableCol(1, 1);

        selectedGrid->Add(new wxStaticText(panel, wxID_ANY, "Entry:"), 0, wxALIGN_CENTER_VERTICAL);
        selectedLabel_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        selectedGrid->Add(selectedLabel_, 1, wxEXPAND);

        selectedGrid->Add(new wxStaticText(panel, wxID_ANY, "StrRef:"), 0, wxALIGN_CENTER_VERTICAL);
        strRef_ = new wxTextCtrl(panel, wxID_ANY);
        selectedGrid->Add(strRef_, 0, wxEXPAND);

        selectedGrid->Add(new wxStaticText(panel, wxID_ANY, "Sound ResRef:"), 0, wxALIGN_CENTER_VERTICAL);
        directSound_ = new wxTextCtrl(panel, wxID_ANY);
        directSound_->SetMaxLength(32);
        selectedGrid->Add(directSound_, 1, wxEXPAND);

        selectedGrid->Add(new wxStaticText(panel, wxID_ANY, "TLK sound:"), 0, wxALIGN_CENTER_VERTICAL);
        selectedSound_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        selectedGrid->Add(selectedSound_, 1, wxEXPAND);

        selectedGrid->Add(new wxStaticText(panel, wxID_ANY, "TLK text:"), 0, wxALIGN_TOP | wxTOP, 3);
        selectedText_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        selectedGrid->Add(selectedText_, 1, wxEXPAND);
        editorBox->Add(selectedGrid, 1, wxEXPAND | wxALL, 8);

        auto* editButtons = new wxBoxSizer(wxHORIZONTAL);
        editButtons->AddStretchSpacer();
        editButtons->Add(new wxButton(panel, ID_Modify, "Apply Entry"), 0, wxRIGHT, 6);
        editButtons->Add(new wxButton(panel, ID_AddTlkEntry, "Add TLK Entry..."), 0);
        editorBox->Add(editButtons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
        root->Add(editorBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        panel->SetSizer(root);

        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_LoadTlk);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_NewSsf);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_OpenSsf);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_Save);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_SaveAs);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_Modify);
        Bind(wxEVT_BUTTON, &NeoSSFFrame::dispatchButton, this, ID_AddTlkEntry);
        if (filterText_) filterText_->Bind(wxEVT_TEXT, &NeoSSFFrame::onFilterText, this);
        grid_->Bind(wxEVT_GRID_SELECT_CELL, &NeoSSFFrame::onGridSelected, this);
        grid_->Bind(wxEVT_GRID_LABEL_LEFT_CLICK, &NeoSSFFrame::onGridLabelClicked, this);
        grid_->Bind(wxEVT_GRID_LABEL_RIGHT_CLICK, &NeoSSFFrame::onGridLabelRightClick, this);
        documentTabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &NeoSSFFrame::onDocumentTabChanged, this);
        documentTabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &NeoSSFFrame::onDocumentTabCloseRequested, this);
    }

    void dispatchButton(wxCommandEvent& event) {
        wxCommandEvent menuEvent(wxEVT_MENU, event.GetId());
        ProcessWindowEvent(menuEvent);
    }

    void onGridSelected(wxGridEvent& event) {
        selectVisibleRow(event.GetRow());
        event.Skip();
    }

    void onGridLabelClicked(wxGridEvent& event) {
        if (event.GetRow() >= 0) {
            selectVisibleRow(event.GetRow());
        }
        event.Skip();
    }

    bool saveModifiedTlkIfNeeded() {
        if (!model().tlkModified()) {
            return true;
        }
        if (!wxui::confirm(this, "Save TLK", "TLK data was modified. Save dialog.tlk before saving the SSF?")) {
            return false;
        }
        std::filesystem::path target = model().tlkFile();
        if (target.empty()) {
            const auto chosen = wxui::chooseSaveFile(this, "Save dialog.tlk", kTlkWildcard, "dialog.tlk");
            if (!chosen) {
                return false;
            }
            target = *chosen;
        }
        model().saveTlk(target);
        writeCachedTlkPath(target);
        tlkAutoLoadWarning().clear();
        return true;
    }

    bool rowPassesCurrentFilters(const neotabular::Table& table, std::size_t row) const {
        if (row >= table.rows.size()) return false;
        if (!viewState().filterTerm.empty() && !neotabular::rowMatches(table, table.rows[row], viewState().filterTerm)) {
            return false;
        }
        return neoview::rowPassesColumnFilters(viewState(), [&](std::size_t logicalColumn) {
            return logicalColumn < table.rows[row].size() ? table.rows[row][logicalColumn] : std::string();
        });
    }

    neotabular::Table filteredSsfTable() const {
        neotabular::Table table = model().toTable(true);
        if (!neoview::hasAnyFilter(viewState())) return table;
        std::vector<std::vector<std::string>> rows;
        rows.reserve(table.rows.size());
        for (std::size_t i = 0; i < table.rows.size(); ++i) {
            if (rowPassesCurrentFilters(table, i)) rows.push_back(table.rows[i]);
        }
        table.rows = std::move(rows);
        ensureSsfTableMetadataRow(table, model().ssfFormat());
        return table;
    }

    void rebuildVisibleRows() {
        neoview::removeColumnFiltersOutsideRange(viewState(), kGridColumns);
        const auto table = model().toTable(true);
        std::vector<std::size_t> visibleRows;
        visibleRows.reserve(model().entryCount());
        for (std::size_t i = 0; i < model().entryCount(); ++i) {
            if (rowPassesCurrentFilters(table, i)) {
                visibleRows.push_back(i);
            }
        }
        neoview::setRowsFromLogicalRows(viewState(), std::move(visibleRows));
    }

    int visibleRowForActual(std::size_t actualRow) const {
        return neoview::visualRowForLogical(viewState(), actualRow);
    }

    std::size_t actualRowForVisible(int visibleRow) const {
        try {
            return neoview::logicalRowForVisual(viewState(), visibleRow);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Selected row is outside the current filtered view.");
        }
    }

    std::size_t actualColumnForVisible(int visibleColumn) const {
        try {
            return neoview::logicalColumnForVisual(viewState(), visibleColumn);
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Selected column is outside the current view.");
        }
    }

    std::string ssfCellText(std::size_t i, std::size_t logicalColumn) const {
        const UInt32 value = model().entryValue(i);
        std::string text;
        std::string tlkSound;
        if (model().tlkLoaded() && value != kUnsetStrRef) {
            try {
                const auto entry = model().getTlkString(value);
                text = entry.text;
                tlkSound = entry.sound;
            } catch (...) {
                text = "Bad StrRef";
                tlkSound = "N/A";
            }
        }
        switch (logicalColumn) {
        case 0: return std::to_string(i + 1);
        case 1: return model().entryLabel(i);
        case 2: return slotText(value);
        case 3: return model().soundFile(i);
        case 4: return tlkSound;
        case 5: return text;
        default: return {};
        }
    }

    void refreshAll() {
        refreshPaths();
        refreshGrid();
        if (viewState().visualToLogicalRows.empty()) {
            viewState().selectedLogicalRow = -1;
            loadSelectedDetails();
        } else if (viewState().selectedLogicalRow < 0 || visibleRowForActual(static_cast<std::size_t>(viewState().selectedLogicalRow)) < 0) {
            selectRow(static_cast<int>(viewState().visualToLogicalRows.front()));
        } else {
            selectRow(viewState().selectedLogicalRow);
        }
        updateStatus();
    }

    void refreshPaths() {
        tlkPath_->SetValue(wxui::toWx(pathText(model().tlkFile())));
        ssfPath_->SetValue(wxui::toWx(pathText(model().ssfFile())));
    }

    void refreshGrid() {
        rebuildVisibleRows();
        neoview::ensureIdentityColumns(viewState(), kGridColumns);
        const int wantedRows = static_cast<int>(viewState().visualToLogicalRows.size());
        const int currentRows = grid_->GetNumberRows();
        if (currentRows < wantedRows) {
            grid_->AppendRows(wantedRows - currentRows);
        } else if (currentRows > wantedRows) {
            grid_->DeleteRows(wantedRows, currentRows - wantedRows);
        }

        const int wantedCols = static_cast<int>(viewState().visualToLogicalColumns.size());
        for (int visualCol = 0; visualCol < wantedCols; ++visualCol) {
            const std::size_t logicalColumn = actualColumnForVisible(visualCol);
            std::string label = ssfColumnLabel(logicalColumn);
            if (neoview::findColumnFilter(viewState(), logicalColumn) != nullptr) label += " *";
            grid_->SetColLabelValue(visualCol, wxui::toWx(label));
        }
        for (std::size_t visible = 0; visible < viewState().visualToLogicalRows.size(); ++visible) {
            const std::size_t i = viewState().visualToLogicalRows[visible];
            const int row = static_cast<int>(visible);
            for (int visualCol = 0; visualCol < wantedCols; ++visualCol) {
                const std::size_t logicalColumn = actualColumnForVisible(visualCol);
                grid_->SetCellValue(row, visualCol, wxui::toWx(ssfCellText(i, logicalColumn)));
                grid_->SetReadOnly(row, visualCol, true);
            }
        }
        wxui::applyTheme(grid_, darkMode_);
    }

    void selectVisibleRow(int visibleRow) {
        if (visibleRow < 0 || visibleRow >= static_cast<int>(viewState().visualToLogicalRows.size())) return;
        selectRow(static_cast<int>(viewState().visualToLogicalRows[static_cast<std::size_t>(visibleRow)]));
    }

    void selectRow(int row) {
        if (row < 0 || row >= static_cast<int>(model().entryCount())) {
            return;
        }
        viewState().selectedLogicalRow = row;
        const int visibleRow = visibleRowForActual(static_cast<std::size_t>(row));
        if (visibleRow >= 0) {
            grid_->ClearSelection();
            grid_->SelectRow(visibleRow);
            grid_->MakeCellVisible(visibleRow, 0);
        }
        loadSelectedDetails();
        updateStatus();
    }

    void loadSelectedDetails() {
        if (viewState().selectedLogicalRow < 0) {
            selectedLabel_->Clear();
            strRef_->Clear();
            directSound_->Clear();
            selectedSound_->Clear();
            selectedText_->Clear();
            return;
        }

        const auto index = static_cast<std::size_t>(viewState().selectedLogicalRow);
        const UInt32 value = model().entryValue(index);
        selectedLabel_->SetValue(wxui::toWx(std::to_string(index + 1) + ". " + model().entryLabel(index)));
        strRef_->SetValue(wxui::toWx(slotText(value)));
        directSound_->SetValue(wxui::toWx(model().soundFile(index)));

        std::string sound;
        std::string text;
        if (model().tlkLoaded() && value != kUnsetStrRef) {
            try {
                const auto entry = model().getTlkString(value);
                sound = entry.sound;
                text = entry.text;
            } catch (...) {
                sound = "N/A";
                text = "Bad StrRef";
            }
        }
        selectedSound_->SetValue(wxui::toWx(sound));
        selectedText_->SetValue(wxui::toWx(text));
    }

    void updateStatus() {
        updateActiveTabTitle();
        const std::string ssf = model().ssfFile().empty() ? "No SSF" : model().ssfFile().filename().string();
        const std::string tlk = model().tlkLoaded() ? (model().tlkFile().filename().string() + (model().tlkModified() ? " modified" : " loaded"))
                                                   : (tlkAutoLoadWarning().empty() ? std::string("No TLK") : tlkAutoLoadWarning());
        std::string detail = ssf + " - " + ssfFormatName(model().ssfFormat()) + " - " + std::to_string(viewState().visualToLogicalRows.size()) + "/" + std::to_string(model().entryCount()) + " rows";
        const std::string columnFilters = neoview::columnFilterSummary(viewState());
        if (!columnFilters.empty()) detail += "; filters: " + columnFilters;
        wxui::setStatusText(*this, wxui::toWx(detail), 0);
        wxui::setStatusText(*this, wxui::toWx(tlk), 1);
    }


    void applyDarkMode() {
        if (darkModeItem_ != nullptr) {
            darkModeItem_->Check(darkMode_);
        }
        wxui::applyTheme(this, darkMode_);
        if (grid_ != nullptr) {
            wxui::applyGridTheme(*grid_, darkMode_);
        }
        applyFontScale();
    }

    void applyFontScale() {
        neoview::applyFontScale(this, fontScale_);
    }

    void changeFontScaleSteps(int steps) {
        const double next = neoview::steppedFontScale(fontScale_, steps);
        if (neoview::fontScalePercent(next) == neoview::fontScalePercent(fontScale_)) return;
        fontScale_ = next;
        settings_.setFontScale(fontScale_);
        applyFontScale();
    }

    void setFilterTerm(std::string term) {
        viewState().filterTerm = std::move(term);
        if (filterText_ && wxui::toStd(filterText_->GetValue()) != viewState().filterTerm) {
            filterText_->ChangeValue(wxui::toWx(viewState().filterTerm));
        }
        refreshAll();
    }

    void onFilterText(wxCommandEvent&) {
        viewState().filterTerm = filterText_ ? wxui::toStd(filterText_->GetValue()) : std::string();
        refreshAll();
    }

    void onFilterPrompt(wxCommandEvent&) {
        const auto term = wxui::promptText(this, "Filter/Search", "Search term:", viewState().filterTerm);
        if (term) setFilterTerm(*term);
    }

    void clearAllFiltersAndRefresh() {
        neoview::clearAllFilters(viewState());
        if (filterText_ && !filterText_->GetValue().empty()) filterText_->ChangeValue(wxString{});
        refreshAll();
    }

    int selectedVisualColumn() const {
        if (contextVisualColumn_ >= 0) return contextVisualColumn_;
        return grid_ ? grid_->GetGridCursorCol() : -1;
    }

    void promptColumnFilterForVisualColumn(int visualColumn) {
        const std::size_t logicalColumn = actualColumnForVisible(visualColumn);
        const auto* existing = neoview::findColumnFilter(viewState(), logicalColumn);
        const std::string label = ssfColumnLabel(logicalColumn);
        const auto term = wxui::promptText(this, "Filter Column", "Show rows where column '" + label + "' contains:", existing ? existing->term : std::string());
        if (!term) return;
        neoview::setColumnFilter(viewState(), neoview::ColumnFilter{logicalColumn, label, *term, neoview::TextFilterMode::Contains, true});
        refreshAll();
    }

    void onClearFilter(wxCommandEvent&) {
        clearAllFiltersAndRefresh();
    }

    void onFilterSelectedColumn(wxCommandEvent&) {
        try {
            promptColumnFilterForVisualColumn(selectedVisualColumn());
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
        contextVisualColumn_ = -1;
    }

    void onClearSelectedColumnFilter(wxCommandEvent&) {
        try {
            neoview::clearColumnFilter(viewState(), actualColumnForVisible(selectedVisualColumn()));
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
        contextVisualColumn_ = -1;
    }

    void onClearAllFilters(wxCommandEvent&) {
        clearAllFiltersAndRefresh();
    }

    void onMoveColumnLeft(wxCommandEvent&) {
        const int visual = selectedVisualColumn();
        if (neoview::moveVisualColumn(viewState(), visual, visual - 1)) refreshAll();
    }

    void onMoveColumnRight(wxCommandEvent&) {
        const int visual = selectedVisualColumn();
        if (neoview::moveVisualColumn(viewState(), visual, visual + 1)) refreshAll();
    }

    void onResetColumnOrder(wxCommandEvent&) {
        neoview::setIdentityColumns(viewState(), kGridColumns);
        refreshAll();
    }

    void onResetRowOrder(wxCommandEvent&) {
        rebuildVisibleRows();
        refreshAll();
    }

    void onGridLabelRightClick(wxGridEvent& event) {
        if (event.GetCol() >= 0) {
            contextVisualColumn_ = event.GetCol();
            wxMenu menu;
            menu.Append(ID_FilterColumn, "Filter This Column...");
            menu.Append(ID_ClearColumnFilter, "Clear Filter on This Column");
            menu.AppendSeparator();
            menu.Append(ID_MoveColumnLeft, "Move Column Left");
            menu.Append(ID_MoveColumnRight, "Move Column Right");
            menu.Append(ID_ResetColumnOrder, "Reset Column Order");
            PopupMenu(&menu);
            return;
        }
        event.Skip();
    }

    void onImport(neotabular::Format format) {
        try {
            const auto file = wxui::chooseOpenFile(this, "Import " + neotabular::formatName(format), tableWildcardForFormat(format));
            if (!file) return;
            if (format == neotabular::Format::Xml) {
                model().importXml(readTextFile(*file));
            } else if (format == neotabular::Format::Json) {
                model().importXml(ssfJsonToXml(readTextFile(*file)));
            } else {
                model().importSsfTable(neotabular::readTable(*file, format));
            }
            viewState().resetForNewDocument();
            if (filterText_) filterText_->ChangeValue("");
            markDocumentDirty();
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onExport(neotabular::Format format) {
        try {
            const auto file = wxui::chooseSaveFile(this, "Export " + neotabular::formatName(format), tableWildcardForFormat(format),
                                                  exportDefaultFilename(model().ssfFile(), format, "soundset"));
            if (!file) return;
            if (format == neotabular::Format::Xml || format == neotabular::Format::Json) {
                if (neoview::hasAnyFilter(viewState())) {
                    wxui::showError(this, std::runtime_error("Filtering semantic SSF XML/JSON export is not supported. Use CSV/TSV for filtered row output."));
                    return;
                }
                const std::string xml = model().toXml(false);
                writeTextFile(*file, format == neotabular::Format::Json ? ssfXmlToJson(xml) : xml);
            } else {
                auto table = filteredSsfTable();
                neotabular::writeTable(table, *file, format);
            }
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onExportPatcherPackage(wxCommandEvent&) {
        try {
            if (!hasActiveDocument()) {
                throw std::runtime_error("Open or create an SSF document first.");
            }
            if (model().ssfFormat() != SSFFormat::KotOR_V11) {
                throw std::runtime_error(
                    "TSLPatcher/HoloPatcher SSFList output is available only for KotOR SSF V1.1 files. "
                    "NWN and NWN2 SSFs store direct sound ResRefs and require whole-file distribution.");
            }

            const auto originalSsfPath = wxui::chooseOpenFile(
                this,
                "Select the clean original SSF used as the comparison baseline",
                kSsfWildcard);
            if (!originalSsfPath) return;

            AppModel originalSsf;
            originalSsf.loadSsf(*originalSsfPath);

            const std::string defaultPatchName = originalSsfPath->filename().string();
            const auto patchName = wxui::promptText(
                this,
                "SSF Target Filename",
                "Filename TSLPatcher/HoloPatcher should modify in the user's Override folder:",
                defaultPatchName);
            if (!patchName) return;

            const auto outputDirectory = wxui::chooseDirectory(
                this,
                "Choose the tslpatchdata output folder");
            if (!outputDirectory) return;

            SsfTlkPatcherOptions options;
            options.patchFilename = *patchName;
            options.modifiedTlkPath = model().tlkLoaded()
                ? model().tlkFile()
                : std::filesystem::path{};

            const TalkTable* activeTlk = model().tlkLoaded() ? &model().tlkData() : nullptr;
            const UInt32 baselineTlkCount = model().tlkLoaded()
                ? model().tlkBaselineCount()
                : 0u;

            auto result = diffSsfAndTlkForPatcher(
                originalSsf,
                model(),
                activeTlk,
                baselineTlkCount,
                options,
                *originalSsfPath);
            neotsl::throwIfUnsupported(result.project);

            if (!result.hasPatchableChanges()) {
                wxui::showMessage(
                    this,
                    "No Patcher Changes",
                    "No patchable SSF changes or TLK entries added since this tab loaded its TLK were detected.");
                return;
            }

            std::vector<std::filesystem::path> generatedFiles{*outputDirectory / "changes.ini"};
            if (result.hasSsfChanges()) {
                generatedFiles.push_back(*outputDirectory / result.options.patchFilename);
            }
            if (result.hasAppendTable()) {
                generatedFiles.push_back(*outputDirectory / result.options.appendFilename);
            }

            std::vector<std::string> existingNames;
            for (const auto& generatedFile : generatedFiles) {
                std::error_code ec;
                if (std::filesystem::exists(generatedFile, ec) && !ec) {
                    existingNames.push_back(generatedFile.filename().string());
                }
            }
            if (!existingNames.empty()) {
                std::ostringstream message;
                message << "The selected folder already contains generated package files:\n\n";
                for (const auto& name : existingNames) message << "  " << name << '\n';
                message << "\nOverwrite these files?";
                if (!wxui::confirm(this, "Overwrite SSF/TLK Patcher Package", message.str())) return;
            }

            writeSsfTlkPatcherPackage(result, *outputDirectory);

            std::ostringstream summary;
            summary << "Wrote a complete TSLPatcher/HoloPatcher package to:\n"
                    << outputDirectory->string() << "\n\n"
                    << "Changed SSF slots: " << result.changedSsfSlots << '\n'
                    << "Dynamic SSF StrRef assignments: " << result.dynamicSsfAssignments << '\n'
                    << "Fixed SSF StrRef assignments: " << result.fixedSsfAssignments << '\n'
                    << "Entries written to append.tlk: " << result.appendedTlkEntries;
            if (result.hasAppendTable()) {
                summary << "\n\nTLK entries added since this tab loaded its TLK were placed in append.tlk. "
                           "SSF slots that reference them use dynamic StrRefN tokens, so they remain compatible "
                           "with other mods that append to dialog.tlk.";
            }
            summary << "\n\nExisting TLK-row edits are not exported by NeoSSF; use NeoTLK's "
                       "HoloPatcher package export for those replacements.";
            wxui::showMessage(this, "Patcher Package Generated", summary.str());
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onCopyCells(wxCommandEvent&) {
        if (!grid_ || !wxTheClipboard->Open()) return;
        int top = grid_->GetGridCursorRow();
        int left = grid_->GetGridCursorCol();
        int bottom = top;
        int right = left;
        const wxGridCellCoordsArray blockTop = grid_->GetSelectionBlockTopLeft();
        const wxGridCellCoordsArray blockBottom = grid_->GetSelectionBlockBottomRight();
        if (!blockTop.IsEmpty() && !blockBottom.IsEmpty()) {
            top = blockTop[0].GetRow(); left = blockTop[0].GetCol();
            bottom = blockBottom[0].GetRow(); right = blockBottom[0].GetCol();
        } else if (!grid_->GetSelectedRows().IsEmpty()) {
            top = grid_->GetSelectedRows()[0]; bottom = top; left = 0; right = kGridColumns - 1;
        }
        neotabular::Table copied;
        if (top >= 0 && left >= 0 && bottom >= top && right >= left) {
            for (int r = top; r <= bottom; ++r) {
                std::vector<std::string> row;
                for (int c = left; c <= right; ++c) row.push_back(wxui::toStd(grid_->GetCellValue(r, c)));
                copied.rows.push_back(std::move(row));
            }
        }
        wxTheClipboard->SetData(new wxTextDataObject(wxui::toWx(neotabular::serializeDelimited(copied, '\t'))));
        wxTheClipboard->Close();
    }

    void onPasteCells(wxCommandEvent&) {
        if (!grid_ || !wxTheClipboard->Open()) return;
        if (!wxTheClipboard->IsSupported(wxDF_TEXT)) { wxTheClipboard->Close(); return; }
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        wxTheClipboard->Close();
        try {
            const auto pasted = neotabular::parseDelimited(wxui::toStd(data.GetText()), '\t');
            const int startRow = grid_->GetGridCursorRow();
            const int startCol = grid_->GetGridCursorCol();
            for (std::size_t r = 0; r < pasted.rows.size(); ++r) {
                const int gridRow = startRow + static_cast<int>(r);
                if (gridRow < 0 || gridRow >= static_cast<int>(viewState().visualToLogicalRows.size())) continue;
                const auto actual = actualRowForVisible(gridRow);
                for (std::size_t c = 0; c < pasted.rows[r].size(); ++c) {
                    const int col = startCol + static_cast<int>(c);
                    const std::size_t logicalColumn = actualColumnForVisible(col);
                    if (logicalColumn == 2) model().modifySlot(actual + 1, pasted.rows[r][c]);
                    else if (logicalColumn == 3) model().modifySoundFile(actual + 1, pasted.rows[r][c]);
                    markDocumentDirty();
                }
            }
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
            refreshAll();
        }
    }

    void onToggleDarkMode(wxCommandEvent& event) {
        darkMode_ = event.IsChecked();
        wxui::writeDarkMode(kAppName, darkMode_);
        applyDarkMode();
    }

    void onIncreaseFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        changeFontScaleSteps(1);
    }
    void onDecreaseFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        changeFontScaleSteps(-1);
    }
    void onResetFontScale(wxCommandEvent&) {
        fontScaleWheelFilter_.reset();
        fontScale_ = neoview::kDefaultFontScale;
        settings_.setFontScale(fontScale_);
        applyFontScale();
    }



    void tryLoadCachedTlk() {
        if (model().tlkLoaded()) return;
        const auto cached = readCachedTlkPath();
        if (!cached || cached->empty()) return;
        try {
            if (!std::filesystem::exists(*cached)) {
                tlkAutoLoadWarning() = "Cached TLK not found: " + cached->string();
                return;
            }
            model().loadTlk(*cached);
            tlkAutoLoadWarning().clear();
        } catch (const std::exception& ex) {
            tlkAutoLoadWarning() = std::string("Unable to auto-load cached TLK: ") + ex.what();
        }
    }

    void onLoadTlk(wxCommandEvent&) {
        try {
            const auto file = wxui::chooseOpenFile(this, "Load optional dialog.tlk for preview text", kTlkWildcard);
            if (!file) return;
            model().loadTlk(*file);
            writeCachedTlkPath(*file);
            tlkAutoLoadWarning().clear();
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onOpenSsf(wxCommandEvent&) {
        try {
            const auto file = wxui::chooseOpenFile(this, "Open SSF", kSsfWildcard);
            if (!file) return;
            openSsfPath(*file);
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onNewSsf(wxCommandEvent&) {
        try {
            createDocumentTab(true);
            model().newSsf();
            viewState().selectedLogicalRow = 0;
            setFilterTerm({});
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onSave(wxCommandEvent&) {
        try {
            if (!saveModifiedTlkIfNeeded()) {
                return;
            }
            if (model().ssfFile().empty() || model().ssfFile().filename() == "new.ssf") {
                wxCommandEvent dummy(wxEVT_MENU, ID_SaveAs);
                onSaveAs(dummy);
                return;
            }
            model().saveSsf(model().ssfFile());
            markDocumentClean();
            rememberRecentFile(model().ssfFile());
            neogames::resolver().inferFromOpenedPath(model().ssfFile());
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onSaveAs(wxCommandEvent&) {
        try {
            if (!saveModifiedTlkIfNeeded()) {
                return;
            }
            const std::string defaultName = model().ssfFile().empty() ? "new.ssf" : model().ssfFile().filename().string();
            const auto file = wxui::chooseSaveFile(this, "Save SSF as", kSsfWildcard, defaultName);
            if (!file) return;
            model().saveSsf(*file);
            model().setSsfFile(*file);
            markDocumentClean();
            rememberRecentFile(*file);
            neogames::resolver().inferFromOpenedPath(*file);
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onModify(wxCommandEvent&) {
        try {
            if (viewState().selectedLogicalRow < 0) {
                throw std::runtime_error("Select a soundset row first.");
            }
            const std::size_t oneBased = static_cast<std::size_t>(viewState().selectedLogicalRow) + 1;
            model().modifySlot(oneBased, wxui::toStd(strRef_->GetValue()));
            model().modifySoundFile(oneBased, wxui::toStd(directSound_->GetValue()));
            markDocumentDirty();
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onAddTlkEntry(wxCommandEvent&) {
        try {
            if (viewState().selectedLogicalRow < 0) {
                throw std::runtime_error("Select a soundset row first.");
            }
            if (!model().tlkLoaded()) {
                throw std::runtime_error("Creating a new TLK entry requires loading a TLK first. Editing SSF StrRefs and direct sound ResRefs does not.");
            }
            const std::size_t row = static_cast<std::size_t>(viewState().selectedLogicalRow);
            AddTlkEntryDialog dialog(this, model().entryLabel(row));
            wxui::applyTheme(&dialog, darkMode_);
            if (dialog.ShowModal() != wxID_OK) {
                return;
            }
            model().addTlkEntryAndAssign(row + 1, dialog.entryText(), dialog.resref());
            markDocumentDirty();
            refreshAll();
        } catch (const std::exception& ex) {
            wxui::showError(this, ex);
        }
    }

    void onCloseTab(wxCommandEvent&) { closeDocumentTab(activeDocumentIndex_); }

    void onCloseOtherTabs(wxCommandEvent&) {
        if (!hasActiveDocument()) return;
        for (std::size_t i = documents_.size(); i-- > 0;) {
            if (i != activeDocumentIndex_ && !closeDocumentTab(i)) return;
        }
    }

    void onNextTab(wxCommandEvent&) {
        if (documentTabs_ == nullptr || documentTabs_->GetPageCount() < 2) return;
        tabSwitchInProgress_ = true;
        documentTabs_->AdvanceSelection(true);
        tabSwitchInProgress_ = false;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (index != neotabs::npos) selectDocumentTab(index);
    }

    void onPreviousTab(wxCommandEvent&) {
        if (documentTabs_ == nullptr || documentTabs_->GetPageCount() < 2) return;
        tabSwitchInProgress_ = true;
        documentTabs_->AdvanceSelection(false);
        tabSwitchInProgress_ = false;
        const std::size_t index = neotabs::findDocumentIndexForPage(
            documents_, neotabs::currentPage(documentTabs_));
        if (index != neotabs::npos) selectDocumentTab(index);
    }

    void onClose(wxCloseEvent& event) {
        if (event.CanVeto() && !confirmCloseAllTabs()) {
            event.Veto();
            return;
        }
        settings_.saveWindowPlacement(*this);
        event.Skip();
    }

    neosettings::AppSettings settings_{kAppName};
    wxMenu* recentFilesMenu_ = nullptr;
    wxMenuItem* darkModeItem_ = nullptr;
    wxTextCtrl* tlkPath_ = nullptr;
    wxTextCtrl* ssfPath_ = nullptr;
    wxTextCtrl* filterText_ = nullptr;
    wxGrid* grid_ = nullptr;
    int contextVisualColumn_ = -1;
    wxTextCtrl* selectedLabel_ = nullptr;
    wxTextCtrl* strRef_ = nullptr;
    wxTextCtrl* directSound_ = nullptr;
    wxTextCtrl* selectedSound_ = nullptr;
    wxTextCtrl* selectedText_ = nullptr;
    wxAuiNotebook* documentTabs_ = nullptr;
    std::vector<DocumentTab> documents_;
    std::size_t activeDocumentIndex_ = neotabs::npos;
    bool tabSwitchInProgress_ = false;
    neoview::FontScaleWheelFilter fontScaleWheelFilter_;
    double fontScale_ = neoview::kDefaultFontScale;
    bool darkMode_ = false;
};

class NeoSSFApp final : public wxApp {
public:
    bool OnInit() override {
#if wxCHECK_VERSION(3, 3, 0)
        SetAppearance(Appearance::System);
#endif
        auto* frame = new NeoSSFFrame;
        frame->Show(true);
        if (argc > 1) {
            frame->CallAfter([frame, arg = wxString(argv[1])]() {
                frame->openStartupSsf(std::filesystem::path(wxui::toStd(arg)));
            });
        }
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(NeoSSFApp);
