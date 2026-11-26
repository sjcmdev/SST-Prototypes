#include "Core/dspch.hpp"
#include "Core/Presentation/ProjectRecoveryLayer.hpp"
#include "Core/App/Application.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/App/ApplicationInternals.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Presentation/EditorLayer.hpp"
#include "Core/Diagnostics/OperationOutcome.hpp"
#include "PlotLabWorkspace.hpp"
#include <imgui.h>
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <fstream>

namespace SST
{
    using namespace Prototypes::PlotLab;
    namespace
    {
        bool smokeTest = false;
        bool columnValuesPreview = false;
        bool dataFilterPreview = false;
        struct FileRequested : BusEvent
        {
            FileAction action = FileAction::New;
            Path path;
        };
        class FileCommand final : public ICommand
        {
          public:
            FileCommand(Ref<EventBus> bus, FileAction action, Path path = {}) : bus(std::move(bus)), action(action), path(std::move(path)) {}
            Result<void> Execute(CommandContext &) override
            {
                FileRequested request;
                request.action = action; request.path = path;
                bus->Queue(request);
                return {};
            }
            std::string Description() const override
            {
                return "PlotLab file operation";
            }

          private:
            Ref<EventBus> bus;
            FileAction action;
            Path path;
        };
    } // namespace
    void SetupApplication(Application &application)
    {
        const auto &runtime = application.GetRuntimeServices();
        auto doc =
            CreateRef<Document>(runtime.commandRegistry.lock(), runtime.undoStack.lock(), runtime.eventBus.lock(), runtime.jobSystem.lock());
        auto root = runtime.paths.installRoot.Native().parent_path() / "python";
        if (!std::filesystem::is_directory(root))
            root = SST_PLOTLAB_PYTHON_ROOT;
        if(PythonInterpreter::IsRuntimeInitialized()) InstallWorksheetPython(doc.get(),root);
        auto state = CreateRef<Session>(doc, runtime.jobSystem.lock(), root);
        state->selected = doc->project->root();
        auto safe = [state](auto action)
        {
            try
            {
                action();
            }
            catch (const std::exception &e)
            {
                state->status = e.what();
            }
        };
        application.GetIOLayer().RegisterHandler<CoreEvents::ProjectSaveRequested>([state, safe](const auto &)
                                                                                   { safe([&] { state->Save(); }); });
        application.GetIOLayer().RegisterHandler<FileRequested>([state, safe](const auto &e)
                                                                { safe([&] { state->File(e.action, e.path); }); });
        application.SetCloseGuard(
            [state]
            {
                if (state->exporting)
                {
                    state->status = "Wait for the export to finish before closing.";
                    return false;
                }
                if (state->allowClose || !state->document->Dirty())
                    return true;
                state->pending = FileAction::Close;
                return false;
            });
        auto registry = runtime.commandRegistry.lock();
        auto bus = runtime.eventBus.lock();
        auto open = registry->Register({CommandID{"app.open"}, "Open project / data", "PlotLab"},
            [bus](CommandContext &context) -> Unique<ICommand> {
                const auto *path = context.TryGet<Path>("path");
                return CreateUnique<FileCommand>(bus, FileAction::Open, path ? *path : Path{});
            });
        if (!open) throw std::runtime_error(open.Error().technicalDetails);
        for (const auto &[id, label, action, key] :
             {std::tuple{"plotlab.new", "New project", FileAction::New, KeyCode::N},
              std::tuple{"plotlab.open", "Open project", FileAction::Open, KeyCode::O}})
        {
            auto result = registry->Register({CommandID{id}, label, "PlotLab"}, [bus, action](CommandContext &)
                                             { return CreateUnique<FileCommand>(bus, action); });
            if (!result)
                throw std::runtime_error(result.Error().technicalDetails);
            auto bound = runtime.keymapResolver.lock()->RegisterBinding({id, {key, KeyModifiers::Ctrl}, CommandID{id}});
            if (!bound)
                throw std::runtime_error(bound.Error().technicalDetails);
        }
        if (smokeTest)
        {
            auto table = pl::CsvImporter{}.read("X,Y\n1,2\n2,4\n3,6\n");
            for (int column = 2; column < 70; ++column)
            {
                table.names.push_back("C" + std::to_string(column));
                pl::Column extra;
                extra.values = std::vector<double>{1, 2, 3};
                table.columns.push_back(std::move(extra));
            }
            doc->Execute(pl::importTable(*doc->project, doc->project->root(), "Smoke", table));
            const auto book = doc->project->children(doc->project->root()).front();
            const auto sheet = doc->project->children(book).front();
            const auto cols = doc->project->children(sheet);
            doc->AddGraph(doc->project->root(), sheet, cols[0], cols[1]);
            for (const char *family : {"DejaVu Serif", "DejaVu Sans Mono"}) {
                const auto graph = doc->AddGraph(doc->project->root(), sheet, cols[0], cols[1]);
                auto object = doc->project->object(graph);
                auto &settings = std::get<pl::Graph>(object.data);
                settings.fontFamily = family;
                settings.fontSizePt = 10;
                if (settings.fontFamily == "DejaVu Serif")
                    settings.fontFile = Path::FromResolved(root.parent_path() / "install/app/assets/fonts/DejaVuSerif.ttf").Utf8();
                doc->Execute(doc->project->change("Smoke font", {object}));
            }
            doc->undo->MarkClean();
            state->RefreshWindows();
        }
        // The preview flag opens the fixture's derived B column for CLI screenshot validation.
        auto& editor = application.GetEditorLayer();
        const auto panelId = editor.RegisterPanel<WorkspacePanel>(state,columnValuesPreview?pl::Id{6}:pl::Id{0},dataFilterPreview?1:0);
        auto panel = std::dynamic_pointer_cast<WorkspacePanel>(editor.FindPanel(panelId).lock());
        panel->RegisterCommands(runtime.keymapResolver.lock());
        editor.GetWindows()->SetDefaultLayout([panel = WeakRef<WorkspacePanel>(panel)] { if (auto p = panel.lock()) p->ResetLayout(); });
        editor.RegisterMenuRenderer("plotlab", [panel](EditorServices&) { panel->RenderMenus(); });
        editor.RegisterMenuRenderer("help", [panel](EditorServices&) { panel->RenderMenus(true); });
        application.InsertLayerBefore(LayerId::ImGui, CreateUnique<WorkspaceLayer>(state, smokeTest));
        application.InsertLayerBefore(LayerId::ImGui, CreateUnique<ProjectRecoveryLayer>(runtime.configDirectory/"recovery",runtime.jobSystem.lock(),
            [doc,previous=std::tuple{doc->project.get(),std::uint64_t{0},decltype(doc->scriptDrafts){},decltype(doc->scriptParameterDrafts){},decltype(doc->pythonHistory){}},revision=std::uint64_t{0}](ProjectRecoveryPlan& recovery) mutable {
                if (!doc->Dirty()) return;
                const auto& [project,engineRevision,drafts,parameters,history]=previous;
                if (project!=doc->project.get() || engineRevision!=doc->project->revision() || drafts!=doc->scriptDrafts || parameters!=doc->scriptParameterDrafts || history!=doc->pythonHistory) {
                    ++revision; previous={doc->project.get(),doc->project->revision(),doc->scriptDrafts,doc->scriptParameterDrafts,doc->pythonHistory};
                }
                recovery.Save(1,revision,Path::FromResolved(doc->path),".plotlab",[&] {auto snapshot=doc->RecoverySnapshot();return [snapshot=std::move(snapshot)](const Path& target){snapshot->save(target.Native());};});
            },
            [doc,state](const RecoveryCandidate& candidate) {
                if(doc->Dirty()) throw std::runtime_error("Save the current project before restoring another copy.");
                doc->Open(candidate.snapshot.Native()); doc->path.clear(); doc->recovered=true; state->RefreshWindows();
                state->status="Recovered a project copy. Use Save As after checking it.";
            }));
    }
} // namespace SST
int main(int argc, char **argv)
{
    std::vector<char*> arguments{argv[0]};
    const char* worksheetScript=nullptr;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--worksheet-script") {
            if(worksheetScript || i+1>=argc || !*argv[i+1] || argv[i+1][0]=='-') {
                std::cerr<<"--worksheet-script requires one script path. Use --help.\n"; return 2;
            }
            worksheetScript=argv[++i];
        }
        else if(arg=="--smoke-test") SST::smokeTest=true;
        else if(arg=="--data-filter-preview") SST::dataFilterPreview=true;
        else if(arg=="--column-values-preview") SST::columnValuesPreview=true;
        else arguments.push_back(argv[i]);
    }
    arguments.push_back(nullptr);
    if(worksheetScript && arguments.size()!=2) { std::cerr<<"--worksheet-script cannot be combined with other options.\n"; return 2; }
    const int sharedArgc=static_cast<int>(arguments.size()-1);
    if(const auto early=SST::HandleEarlyApplicationArguments(sharedArgc,arguments.data())) return *early;
    if(worksheetScript) {
        try { SST::InitializeLocalDiagnostics(); }
        catch(const std::exception& error) { std::cerr<<"Local crash diagnostics unavailable: "<<error.what()<<'\n'; }
        struct DiagnosticsCleanup { ~DiagnosticsCleanup() { SST::FinishLocalDiagnostics(); } } diagnosticsCleanup;
        SST::ApplicationDetail::InstallCrashFallbackHandlers();
#if DS_PYTHON_CAPI_AVAILABLE
            SST::PythonInterpreter interpreter;
            if(!interpreter.Start()) return SST::CliExitCode(SST::MakeOperationError(SST::ErrorCategory::Capability));
            auto pythonRoot=std::filesystem::absolute(argv[0]).parent_path()/"python";
            if(!std::filesystem::is_directory(pythonRoot)) pythonRoot=SST_PLOTLAB_PYTHON_ROOT;
            SST::Prototypes::PlotLab::InstallWorksheetPython(nullptr,pythonRoot);
            std::ifstream in(worksheetScript); if(!in) { std::cerr<<"Cannot read worksheet script. Check the path and permissions.\n"; return SST::CliExitCode(SST::MakeOperationError(SST::ErrorCategory::IO)); }
            const std::string code(std::istreambuf_iterator<char>(in),{});
            auto gil=SST::PythonInterpreter::AcquireRuntimeGil();
            const int result=PyRun_SimpleString(code.c_str());
            SST::PythonInterpreter::ReleaseRuntimeGil(gil);
            return result==0?0:SST::CliExitCode(SST::MakeOperationError(SST::ErrorCategory::Internal));
#else
            return SST::CliExitCode(SST::MakeOperationError(SST::ErrorCategory::Capability));
#endif
    }
    return SST::RunApplication(sharedArgc, arguments.data(), SST::SetupApplication);
}
