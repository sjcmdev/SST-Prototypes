#pragma once
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Layer.hpp"
#include "Core/Presentation/Panels/IPanel.hpp"
#include "PlotLabDocument.hpp"
#include "PlotLabExport.hpp"
#include <set>

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
        void File(FileAction);
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
        explicit WorkspacePanel(Ref<Session> session) : IPanel("Project Explorer"), state(std::move(session)) {}
        Ref<IPanel> Clone() const override
        {
            return CreateRef<WorkspacePanel>(state);
        }
        void Render() override;

      private:
        Ref<Session> state;
        pl::Id rename = 0, remove = 0, metadata = 0, exportGraph = 0;
        pl::Id targetGraph = 0, sheetChoice = 0, xChoice = 0, yChoice = 0, errorChoice = 0;
        std::string name, units, longName, formula;
        int role = 0, format = 0;
        bool textColumn = false, graphDialog = false, importDialog = false;
        std::string importText, importName, importPath;
        pl::ImportOptions importOptions;
        std::optional<pl::Table> preview;
        std::map<pl::Id, int> columnPages;
        double width = 80, height = 60;
        int dpi = 300;
        void Tree(pl::Id);
        void Worksheet(pl::Id);
        void Graph(pl::Id);
        void Dialogs();
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
