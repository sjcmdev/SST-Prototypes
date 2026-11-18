#include "Core/App/Application.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Presentation/EditorLayer.hpp"
#include "Core/dspch.hpp"
#include "PlotLabWorkspace.hpp"
#include <imgui.h>

namespace SST
{
    using namespace Prototypes::PlotLab;
    namespace
    {
        bool smokeTest = false;
        struct FileRequested : BusEvent
        {
            FileAction action = FileAction::New;
        };
        class FileCommand final : public ICommand
        {
          public:
            FileCommand(Ref<EventBus> bus, FileAction action) : bus(std::move(bus)), action(action) {}
            Result<void> Execute(CommandContext &) override
            {
                FileRequested request;
                request.action = action;
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
        };
    } // namespace
    void SetupApplication(Application &application)
    {
        const auto &runtime = application.GetRuntimeServices();
        auto doc =
            CreateRef<Document>(runtime.commandRegistry.lock(), runtime.undoStack.lock(), runtime.eventBus.lock());
        auto root = runtime.paths.installRoot.Native().parent_path() / "python";
        if (!std::filesystem::is_directory(root))
            root = SST_PLOTLAB_PYTHON_ROOT;
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
                                                                { safe([&] { state->File(e.action); }); });
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
        application.GetEditorLayer().RegisterMenuRenderer("plotlab",
                                                          [state, safe](EditorServices &)
                                                          {
                                                              if (ImGui::BeginMenu("PlotLab"))
                                                              {
                                                                  if (ImGui::MenuItem("New", "Ctrl+N"))
                                                                      safe([&] { state->File(FileAction::New); });
                                                                  if (ImGui::MenuItem("Open", "Ctrl+O"))
                                                                      safe([&] { state->File(FileAction::Open); });
                                                                  if (ImGui::MenuItem("Save", "Ctrl+S"))
                                                                      safe([&] { state->Save(); });
                                                                  if (ImGui::MenuItem("Save as"))
                                                                      safe([&] { state->Save(true); });
                                                                  ImGui::EndMenu();
                                                              }
                                                          });
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
        application.GetEditorLayer().RegisterPanel<WorkspacePanel>(state);
        application.InsertLayerBefore(LayerId::ImGui, CreateUnique<WorkspaceLayer>(state, smokeTest));
    }
} // namespace SST
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test")
            SST::smokeTest = true;
    auto app = SST::Application::Create(argc, argv, SST::SetupApplication);
    return app.Run();
}
