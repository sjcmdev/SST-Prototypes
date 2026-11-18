#include "Core/dspch.hpp"
#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/__PROTOTYPE_NAME__PrototypePanel.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include <imgui.h>

namespace SST
{
    using namespace Prototypes::__PROTOTYPE_NAME__;
    class DocumentCommand final : public ICommand
    {
    public:
        DocumentCommand(Ref<EventBus> bus, Operation operation) : bus(std::move(bus)), operation(operation) {}
        Result<void> Execute(CommandContext &) override
        {
            DocumentRequested request;
            request.operation = operation;
            bus->Queue(request);
            return {};
        }
        std::string Description() const override { return "Prototype document operation"; }
    private:
        Ref<EventBus> bus;
        Operation operation;
    };

    void SetupApplication(Application &application)
    {
        RegisterPrototypePythonModule();
        const auto &services = application.GetRuntimeServices();
        auto workflow = CreateUnique<PrototypeWorkflow>(services.jobSystem.lock());
        auto *state = workflow.get();
        application.GetIOLayer().RegisterHandler<DocumentRequested>([state](const auto &event) { state->Start(event.operation); });
        application.GetIOLayer().RegisterHandler<CoreEvents::ProjectSaveRequested>([state](const auto &) { state->Start(Operation::Save); });
        auto registry = services.commandRegistry.lock();
        auto bus = services.eventBus.lock();
        for (const auto &[id, name, operation, key] : {
            std::tuple{"prototype.evaluate", "Evaluate", Operation::Evaluate, KeyCode::F5},
            std::tuple{"prototype.open", "Open document", Operation::Open, KeyCode::O}})
        {
            const auto registered = registry->Register({CommandID{id}, name, "Prototype"},
                [bus, operation](CommandContext &) { return CreateUnique<DocumentCommand>(bus, operation); });
            if (!registered) throw std::runtime_error("Cannot register prototype command.");
            const auto bound = services.keymapResolver.lock()->RegisterBinding({id,
                {key, operation == Operation::Open ? KeyModifiers::Ctrl : KeyModifiers::None}, CommandID{id}});
            if (!bound) throw std::runtime_error("Cannot register prototype shortcut.");
        }
        auto &editor = application.GetEditorLayer();
        auto windows = editor.GetWindows();
        state->captureWindowLayout = [windows] { return windows->Save(); };
        state->restoreWindowLayout = [windows](const std::string &layout) { return bool(windows->Load(layout)); };
        editor.RegisterMenuRenderer("prototype", [registry](EditorServices &)
        {
            if (ImGui::BeginMenu("Document"))
            {
                for (const auto &[label, shortcut, id] : {
                    std::tuple{"Evaluate", "F5", "prototype.evaluate"},
                    std::tuple{"Open", "Ctrl+O", "prototype.open"},
                    std::tuple{"Save", "Ctrl+S", "project.save"}})
                    if (ImGui::MenuItem(label, shortcut)) (void)registry->Execute(CommandID{id});
                ImGui::EndMenu();
            }
        });
        const auto settingsPath = services.configDirectory.Join("prototype_settings.txt");
        std::string settings, error;
        if (FileSystem::Exists(settingsPath.Native()))
        {
            if (TextFileIO::Load(settingsPath, settings, error) && (settings == "0" || settings == "1"))
                state->scientificNotation = settings == "1";
            else state->status = "Cannot load prototype settings: " + error;
        }
        editor.RegisterSettingsTab({"prototype", "Prototype", [state, settingsPath]
        {
            ImGui::Checkbox("Scientific notation", &state->scientificNotation);
            if (ImGui::Button("Save prototype settings"))
            {
                std::string error;
                state->status = TextFileIO::Save(settingsPath, state->scientificNotation ? "1" : "0", error)
                    ? "Settings saved." : error;
            }
            ImGui::TextWrapped("%s", state->status.c_str());
        }});
        // The application owns this layer for the entire editor lifetime.
        editor.RegisterPanel<__PROTOTYPE_NAME__PrototypePanel>(state);
        application.InsertLayerBefore(LayerId::ImGui, std::move(workflow));
    }
}
