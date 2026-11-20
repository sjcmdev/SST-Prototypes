#include "Core/dspch.hpp"
#include "App/Application.hpp"

#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "CapLabCommands.hpp"
#include <imgui.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <cstdlib>
namespace SST {
void SetupApplication(Application &application) {
    using namespace Prototypes::CapLab;
    RegisterPrototypePythonModule();
    const auto &services = application.GetRuntimeServices();
    auto workflow = CreateUnique<CapLabWorkflow>(services.jobSystem.lock(), services.eventBus.lock(), services.undoStack.lock());
    auto *state = workflow.get();
    application.GetIOLayer().RegisterHandler<DocumentRequested>([state](const auto &e) { state->Start(e.operation, e.target); });
    application.GetIOLayer().RegisterHandler<CoreEvents::ProjectSaveRequested>([state](const auto &) { state->Start(Operation::Save); });
    auto registry = services.commandRegistry.lock();
    RegisterCommands(registry, services.eventBus.lock(), *state);
    const auto bound = services.keymapResolver.lock()->RegisterBinding({"caplab.open",
        {KeyCode::O, KeyModifiers::Ctrl}, CommandID{"caplab.open"}});
    if (!bound) throw std::runtime_error("Cannot register CapLab shortcut.");
    auto &editor = application.GetEditorLayer();
    editor.RegisterMenuRenderer("caplab", [registry](EditorServices &) {
        static bool gallery = std::getenv("SST_PLOT_GALLERY") != nullptr;
        if (ImGui::BeginMenu("CapLab")) {
            ImGui::MenuItem("3D and heatmap gallery", nullptr, &gallery);
            for (const auto &[label, id] : {
                std::pair{"Import lab AS...", "caplab.preview"}, {"Open project...", "caplab.open"},
                {"Save project", "project.save"}, {"Export CSV / XLSX / PNG...", "caplab.export"}})
                if (ImGui::MenuItem(label)) (void)registry->Execute(CommandID{id});
            ImGui::EndMenu();
        }
        if (gallery) sst::ui::plot::ShowPlotGallery();
    });
    for (auto view : {CapLabPrototypePanel::View::Project, CapLabPrototypePanel::View::Plot,
                      CapLabPrototypePanel::View::Analysis, CapLabPrototypePanel::View::Import})
        editor.RegisterPanel<CapLabPrototypePanel>(state, registry, view);
    application.InsertLayerBefore(LayerId::ImGui, std::move(workflow));
}
}

int main(int argc, char** argv) {
    // Keep the shared --open interface and accept the tester's positional path.
    std::vector<char*> arguments(argv,argv+argc+1);
    char open[]="--open";
    if (argc>1 && argv[1][0]!='-') arguments.insert(arguments.begin()+1,open);
    auto app=SST::Application::Create(static_cast<int>(arguments.size()-1),arguments.data(),SST::SetupApplication);
    return app.Run();
}
