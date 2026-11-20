#include "Core/dspch.hpp"
#include "App/Application.hpp"

#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/IO/RecentFiles.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "CapLabCommands.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <cstdlib>
namespace SST {
namespace {
class CapLabFileActions final : public Layer {
public:
    CapLabFileActions(Application &app, Prototypes::CapLab::CapLabWorkflow &state)
        : Layer("CapLab file actions"), app(app), state(state) {
        app.SetCloseGuard([this] { return this->state.RequestClose(true); });
        if (const auto config=app.GetConfigManager().lock()) recentPath=config->GetConfigDirectory()/Path{"caplab-recent.json"};
        std::string text,error;
        if (TextFileIO::Load(recentPath,text,error)) {
            try {
                const auto files=Prototypes::CapLab::Json::parse(text).at("recent_projects").get<std::vector<std::string>>();
                for (auto it=files.rbegin();it!=files.rend();++it) TouchRecent(state.recent,Path{*it});
            } catch (const std::exception &) { state.status="Recent projects file could not be read."; }
        }
        savedRecent=state.recent;
        profilePath=recentPath.parent_path().Join("caplab-import-profiles.json");
        if (TextFileIO::Load(profilePath,text,error)) {
            try {
                auto checked=state.Document().project;
                checked.importProfiles=sst::plotlab::importProfilesFromJson(text); Validate(checked);
                state.Document().project.importProfiles=std::move(checked.importProfiles);
            }
            catch (const std::exception &e) {state.status=e.what();}
        }
    }
    void OnImGuiRender() override {
        if (state.profileRevision!=savedProfileRevision) {
            const auto profiles=sst::plotlab::importProfilesJson(state.Document().project.importProfiles);
            std::string error;
            if (TextFileIO::Save(profilePath,profiles,error)) savedProfileRevision=state.profileRevision;
            else state.status=error;
        }
        if (state.recent!=savedRecent) {
            Prototypes::CapLab::Json files=Prototypes::CapLab::Json::array(); for (const auto &path:state.recent) files.push_back(path.Utf8());
            std::string error;
            if (TextFileIO::Save(recentPath,Prototypes::CapLab::Json{{"recent_projects",files}}.dump(2),error)) savedRecent=state.recent;
            else state.status=error;
        }
        if (state.closeReady) { state.closeReady=false; state.bus->Queue(CoreEvents::ShutdownRequested{}); return; }
        if (state.closeRequested && !ImGui::IsPopupOpen("Unsaved CapLab project")) ImGui::OpenPopup("Unsaved CapLab project");
        if (!ImGui::BeginPopupModal("Unsaved CapLab project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextUnformatted(state.closeApplication ? "Save changes before closing CapLab?" : "Save changes before closing this project?");
        const auto &path=state.Document().path;
        ImGui::TextWrapped("%s",path ? path->Utf8().c_str() : "Untitled project");
        if (!state.status.empty()) ImGui::TextWrapped("%s",state.status.c_str());
        if (state.activeJob) {
            ImGui::TextUnformatted("Wait for the active job or cancel it before closing.");
            if (ImGui::Button("Cancel job")) (void)state.commands->Execute(CommandID{"caplab.cancel"});
        }
        auto decide=[&](const char *decision, const Path &target=Path{}) {
            CommandContext context;
            context.Set("request",Prototypes::CapLab::Json{{"operation","close_decision"},{"decision",decision},{"path",target.Utf8()}});
            const auto result=state.commands->Execute(CommandID{"caplab.workspace"},std::move(context));
            if (!result) state.status=result.Error().technicalDetails;
        };
        ImGui::BeginDisabled(state.activeJob!=0);
        if (ImGui::Button("Save")) {
            try {
                auto target=state.Document().path;
                if (!target) target=SaveFileDialog({"CapLab project","json"},"project.caplab.json");
                if (target) decide("save",*target);
            } catch (const std::exception &e) { state.status=e.what(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Discard")) decide("discard");
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Cancel")) decide("cancel");
        if (!state.closeRequested) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
private:
    Application &app;
    Prototypes::CapLab::CapLabWorkflow &state;
    Path recentPath;
    Path profilePath;
    std::uint64_t savedProfileRevision=0;
    std::vector<Path> savedRecent;
};
}
void SetupApplication(Application &application) {
    using namespace Prototypes::CapLab;
    RegisterPrototypePythonModule();
    const auto &services = application.GetRuntimeServices();
    auto workflow = CreateUnique<CapLabWorkflow>(services.jobSystem.lock(), services.eventBus.lock(), services.undoStack.lock());
    auto *state = workflow.get();
    application.GetIOLayer().RegisterHandler<DocumentRequested>([state](const auto &e) { state->Start(e.operation, e.target, e.sources, e.recursive); });
    application.GetIOLayer().RegisterHandler<CoreEvents::ProjectSaveRequested>([state](const auto &) { state->Start(Operation::Save); });
    auto registry = services.commandRegistry.lock();
    RegisterCommands(registry, services.eventBus.lock(), *state);
    const auto bound = services.keymapResolver.lock()->RegisterBinding({"caplab.open",
        {KeyCode::O, KeyModifiers::Ctrl}, CommandID{"caplab.open"}});
    if (!bound) throw std::runtime_error("Cannot register CapLab shortcut.");
    auto &editor = application.GetEditorLayer();
    ImGui::GetIO().ConfigDebugHighlightIdConflicts=true;
    auto resetDocking=CreateRef<bool>(false);
    (void)registry->AddObserver([resetDocking](const CommandExecutionEvent &event) {
        if (event.id.value=="app.layout.reset" && event.state==CommandExecutionState::Succeeded) *resetDocking=true;
    });
    editor.RegisterMenuRenderer("caplab", [registry,state,&editor,resetDocking,windows=editor.GetWindows(),initialized=false](EditorServices &) mutable {
        if (!initialized) {
            initialized=true;
            // Keep existing app and Core window IDs when adding module windows.
            for (auto view:{CapLabPrototypePanel::View::AS,CapLabPrototypePanel::View::Walter,CapLabPrototypePanel::View::IS})
                editor.RegisterPanel<CapLabPrototypePanel>(state,registry,view);
            windows->RemoveSettingsHandler();
            windows->InstallSettingsHandler();
            const auto *viewport=ImGui::GetMainViewport();
            if (const auto *dock=ImGui::DockBuilderGetNode(viewport->ID); dock && dock->LastFrameAlive!=GImGui->FrameCount)
                ImGui::DockSpaceOverViewport(viewport->ID,viewport,ImGuiDockNodeFlags_PassthruCentralNode);
            DS_LOG_DEBUG("CapLab restored window layout: {}",windows->Save());
            for (const auto &window:windows->Windows()) {
                const auto dockId=window->GetState().dockId;
                if (dockId) ImGui::DockBuilderDockWindow(window->ImGuiName().c_str(),dockId);
            }
            if (const auto *logging=ImGui::FindWindowSettingsByID(ImHashStr("Logging Panel")); logging && logging->DockId)
                ImGui::DockBuilderDockWindow("Logging Panel",logging->DockId);
            ImGui::DockBuilderFinish(ImGui::GetMainViewport()->ID);
            for (const auto &window:windows->Windows()) if (window->IsVisible() && window->GetState().mode==WindowMode::Normal) {
                (void)windows->Execute({WindowAction::Activate,window->GetId()}); break;
            }
        }
            const auto entries=windows->Windows();
            const auto added=std::find_if(entries.begin(),entries.end(),[](const auto &w) {return w->GetType()=="caplab.as";});
            if (*resetDocking || (added!=entries.end() && !ImGui::FindWindowSettingsByID(ImHashStr((*added)->ImGuiName().c_str())))) {
                *resetDocking=false;
                const auto *viewport=ImGui::GetMainViewport();
                const auto dock=viewport->ID;
                ImGui::DockBuilderRemoveNode(dock); ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock,viewport->WorkSize);
                ImGuiID center=dock;
                const auto bottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.18f,nullptr,&center);
                const auto left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.25f,nullptr,&center);
                ImGuiID right=ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.43f,nullptr,&center);
                ImGui::DockBuilderDockWindow("Logging Panel",bottom);
                for (const auto &w:entries) {
                    const auto &type=w->GetType();
                    if (type=="caplab.import") continue;
                    auto layout=w->GetState();
                    if (type=="caplab.analysis" && layout.shortName=="Analysis / results") layout.shortName=layout.longName="CV / DLCP";
                    const auto target=type=="caplab.workspace" ? left : type=="caplab.plot" ? center : right;
                    layout.dockId=target;
                    if (!type.starts_with("caplab.")) layout.visible=false;
                    w->ApplyState(layout);
                    if (layout.visible) ImGui::DockBuilderDockWindow(w->ImGuiName().c_str(),target);
                }
                ImGui::DockBuilderFinish(dock);
            }
        static bool gallery = std::getenv("SST_PLOT_GALLERY") != nullptr;
        if (ImGui::BeginMenu("CapLab")) {
            ImGui::MenuItem("3D and heatmap gallery", nullptr, &gallery);
            ImGui::BeginDisabled(state->activeJob!=0 || state->closeRequested);
            for (const auto &[label, id] : {
                std::pair{"New project", "caplab.new"}, {"Open project...", "caplab.open"},
                {"Save project", "project.save"}, {"Save project as...", "caplab.save_as"}, {"Close project", "caplab.close"},
                {"Import lab AS...", "caplab.preview"}, {"Export CSV / XLSX / PNG / SVG / PDF...", "caplab.export"}})
                if (ImGui::MenuItem(label)) (void)registry->Execute(CommandID{id});
            if (ImGui::BeginMenu("Recent projects",!state->recent.empty())) {
                for (const auto &path:state->recent) {
                    ImGui::PushID(path.Utf8().c_str());
                    if (ImGui::MenuItem(path.Utf8().c_str())) { CommandContext context; context.Set("path",path); (void)registry->Execute(CommandID{"caplab.open"},std::move(context)); }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (gallery) sst::ui::plot::ShowPlotGallery();
    });
    for (auto view : {CapLabPrototypePanel::View::Project, CapLabPrototypePanel::View::Plot,
                      CapLabPrototypePanel::View::CV, CapLabPrototypePanel::View::Import})
        editor.RegisterPanel<CapLabPrototypePanel>(state, registry, view);
    application.InsertLayerBefore(LayerId::ImGui, std::move(workflow));
    application.InsertLayerBefore(LayerId::ImGui, CreateUnique<CapLabFileActions>(application,*state));
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
