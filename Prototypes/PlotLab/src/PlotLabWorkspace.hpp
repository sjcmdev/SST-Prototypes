#pragma once
#include <sst/ui/Plot/Editor.hpp>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Layer.hpp"
#include "Core/Presentation/Panels/HostedWindows.hpp"
#include "PlotLabDocument.hpp"
#include "PlotLabExport.hpp"
#include <set>
#include <imgui.h>

namespace SST::Prototypes::PlotLab
{
    enum class FileAction
    {
        None,
        New,
        Open,
        Close
    };
    struct Session
    {
        Session(Ref<Document> document, Ref<JobSystem> jobs, std::filesystem::path pythonRoot)
            : document(std::move(document)), jobs(std::move(jobs)), pythonRoot(std::move(pythonRoot))
        {
        }
        Ref<Document> document;
        Ref<JobSystem> jobs;
        std::filesystem::path pythonRoot;
        std::set<pl::Id> sheets, graphs;
        pl::Id activate = 0;
        std::vector<std::pair<CommandID, Path>> uiRequests;
        std::vector<std::filesystem::path> recentFiles;
        std::map<pl::Id, plot::PlotViewState> views;
        std::map<pl::Id, plot::PlotEditorState> plotEditors;
        pl::Id selected = 0;
        std::string status = "Create a Workbook and Worksheet, or import CSV/TXT.";
        FileAction pending = FileAction::None;
        JobId exporting = 0;
        bool allowClose = false;
        bool Save(bool saveAs = false);
        void File(FileAction, Path path = {});
        Path pendingPath;
        Ref<OpenJob> opening;
        JobId openJob = 0;
        Ref<pl::Project> openingBase;
        std::uint64_t openingRevision = 0;
        void CompleteFile();
        void Show(pl::Id);
        void RefreshWindows();
    };
    class WorkspaceLayer final : public Layer
    {
      public:
        WorkspaceLayer(Ref<Session> session, bool smoke)
            : Layer("PlotLab workspace"), state(std::move(session)), smoke(smoke)
        {
        }
        void OnUpdate(float) override;

      private:
        Ref<Session> state;
        bool smoke;
        int frames = 0;
    };
    class WorkspacePanel final : public IPanel
    {
      public:
        explicit WorkspacePanel(Ref<Session> session, pl::Id previewColumn = 0, int dataPreview = 0)
            : IPanel("Project Explorer"), state(std::move(session)), previewColumn(previewColumn), previewData(dataPreview != 0), dataAction(dataPreview) { RegisterCommands(); }
        Ref<IPanel> Clone() const override
        {
            auto copy = CreateRef<WorkspacePanel>(state);
            copy->windows.UseInstanceIdentity();
            return copy;
        }
        void Render() override;
        // Component entry points also used by backend-free ImGui validation.
        void WorksheetContents(pl::Id);
        void GraphContents(pl::Id);
        void Dialogs();
        void ResetLayout() { SetVisible(true); layoutReady = false; resetLayoutRequested = true; }
        void RegisterCommands(Ref<KeymapResolver> keymap = {});
        void RenderMenus(bool helpOnly = false);

      private:
        friend struct WorkspaceTestAccess;
        HostedWindows windows;
        bool layoutReady = false, layoutRebuilt = false, resetLayoutRequested = false;
        ImGuiID explorerDock = 0, contentDock = 0;
        void PrepareLayout();
        void Activate(pl::Id);
        void ProcessCommands();
        void RunAction(const std::string&, const Path& = {});
        bool ActionEnabled(const std::string&) const;
        pl::Id CurrentSheet() const;
        bool keymapRegistered = false, helpDialog = false;
        Ref<Session> state;
        std::weak_ptr<pl::Project> viewProject;
        bool projectSeen = false;
        void SyncProject();
        pl::Id rename = 0, remove = 0, metadata = 0, exportGraph = 0;
        pl::Id previewColumn = 0;
        bool previewData = false;
        bool previewLibrary = false;
        pl::Id targetGraph = 0, sheetChoice = 0, xChoice = 0, errorChoice = 0;
        std::set<pl::Id> availableColumns, plottedSeries;
        int plotType = 2;
        void BeginGraphDialog(pl::Id graph = 0, pl::Id sheet = 0);
        void AddAvailablePlots();
        std::string name, units, longName, formula, comments, parameterName, cellText;
        int displayMode = 0, displayDigits = 6;
        int rowFrom = 1, rowTo = 0, recalculate = 1;
        std::string beforeScript;
        int role = 0, format = 0;
        bool graphDialog = false, importDialog = false;
        std::string importText, importName, importPath;
        pl::ImportOptions importOptions;
        std::optional<pl::Table> preview;
        struct MaskPreview { std::weak_ptr<pl::Project> owner; std::uint64_t revision=0; std::vector<double> x,y; };
        std::map<pl::Id,MaskPreview> maskPreviews;
        std::map<pl::Id, std::vector<std::pair<pl::Id, std::uint64_t>>> graphSources;
        struct SheetView {
            std::set<pl::Id> columns;
            pl::Id anchorColumn = 0, column = 0, anchorRow = 0, row = 0;
            float zoom = 1;
        };
        std::map<pl::Id, SheetView> sheetViews;
        std::map<pl::Id, pl::Id> activeSheets, renderedSheets;
        void NewWorkbook(pl::Id folder);
        void NewWorksheet(pl::Id book);
        pl::Id contextColumn = 0, operationsSheet = 0;
        bool valuesDialog = false;
        void SelectColumn(pl::Id, pl::Id, bool extend, bool toggle);
        void SelectCell(pl::Id, pl::Id, pl::Id, bool extend);
        void ColumnMenu(pl::Id);
        void HeaderMenu(pl::Id);
        void MaskSelection(pl::Id, bool);
        void MoveColumn(pl::Id, int);
        void FillSelection(pl::Id, bool);
        void CopySelection(pl::Id);
        void PasteSelection(pl::Id, const char*);
        void PlotSelection(pl::Id, plot::SeriesKind);
        double width = 80, height = 60;
        int dpi = 300;
        void Tree(pl::Id);
        void Worksheet(pl::Id);
        void DataOperations(pl::Id);
        int dataAction=0, filterKind=0, dataColumn=0, dataSecondColumn=0, everyN=2, topN=10, operationMode=0, maskFirst=0, maskEnd=1;
        bool descending=false, columnOnly=false, maskRows=false, confirmBreak=false, excludeFiltered=false;
        double filterMinimum=0,filterMaximum=10, replaceNumber=0, normalizationX=0;
        pl::Id joinSheet=0;
        std::vector<std::pair<pl::Id,bool>> nestedKeys;
        std::set<pl::Id> operationColumns;
        ImVec4 maskTint{1,.35f,.35f,.6f};
        std::string filterText, replacement, operationName="Result";
        void Graph(pl::Id);
        void EditColumn(pl::Id);
        void BeginImport();
        void PreviewImport();
        template <class F> void Attempt(F &&operation)
        {
            try
            {
                operation();
                state->status = "Ready.";
            }
            catch (const std::exception &e)
            {
                state->status = e.what();
            }
        }
    };
} // namespace SST::Prototypes::PlotLab
