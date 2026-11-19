#pragma once
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
        std::map<pl::Id, plot::PlotViewState> views;
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
        explicit WorkspacePanel(Ref<Session> session, pl::Id previewColumn = 0)
            : IPanel("Project Explorer"), state(std::move(session)), previewColumn(previewColumn) {}
        Ref<IPanel> Clone() const override
        {
            auto copy = CreateRef<WorkspacePanel>(state);
            copy->windows.UseInstanceIdentity();
            return copy;
        }
        void Render() override;

      private:
        HostedWindows windows;
        Ref<Session> state;
        pl::Id rename = 0, remove = 0, metadata = 0, exportGraph = 0;
        pl::Id previewColumn = 0;
        bool previewLibrary = false;
        pl::Id targetGraph = 0, sheetChoice = 0, xChoice = 0, yChoice = 0, errorChoice = 0;
        std::string name, units, longName, formula, comments, parameterName, cellText;
        int displayMode = 0, displayDigits = 6;
        int rowFrom = 1, rowTo = 0, recalculate = 1;
        std::string beforeScript;
        int role = 0, format = 0;
        bool textColumn = false, graphDialog = false, importDialog = false;
        std::string importText, importName, importPath;
        pl::ImportOptions importOptions;
        std::optional<pl::Table> preview;
        std::map<pl::Id, int> columnPages;
        std::map<std::string, ImFont *> graphFonts;
        std::map<pl::Id, std::string> fontErrors;
        double width = 80, height = 60;
        int dpi = 300;
        void Tree(pl::Id);
        void Worksheet(pl::Id);
        void Graph(pl::Id);
        void Dialogs();
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
