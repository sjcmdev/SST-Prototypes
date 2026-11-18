#include "Core/dspch.hpp"
#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "CapLabCommands.hpp"
#include <imgui.h>
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
        if (ImGui::BeginMenu("CapLab")) {
            for (const auto &[label, id] : {
                std::pair{"Import lab AS...", "caplab.preview"}, {"Open project...", "caplab.open"},
                {"Save project", "project.save"}, {"Export active plot PNG...", "caplab.export"}})
                if (ImGui::MenuItem(label)) (void)registry->Execute(CommandID{id});
            ImGui::EndMenu();
        }
    });
    editor.RegisterPanel<CapLabPrototypePanel>(state, registry);
    application.InsertLayerBefore(LayerId::ImGui, std::move(workflow));
}
}
