#include "Core/dspch.hpp"
#include "Core/App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Presentation/EditorLayer.hpp"
#include "Panel.hpp"
#include <imgui.h>
#include <cstdlib>
#include <thread>
namespace SST {
namespace {
class ScapsCloseGuard final : public Layer {
    Application &app;
    ScapsLab::Workflow &w;
    bool requested=false, saving=false;
public:
    ScapsCloseGuard(Application &app,ScapsLab::Workflow &w):Layer("ScapsLab close guard"),app(app),w(w) {
        app.SetCloseGuard([this] {if(!this->w.dirty)return true;requested=true;return false;});
    }
    void OnImGuiRender() override {
        if(requested) {ImGui::OpenPopup("Unsaved ScapsLab project");requested=false;}
        if(ImGui::BeginPopupModal("Unsaved ScapsLab project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your changes before closing?");
            ImGui::TextWrapped("%s",w.status.c_str());
            ImGui::BeginDisabled(w.Busy());
            if(ImGui::Button("Save and close")) {const auto r=w.Dispatch({{"operation","save"},{"path",w.path}});saving=r.at("ok");}
            ImGui::SameLine();if(ImGui::Button("Discard")) {w.dirty=false;app.Shutdown();}
            ImGui::SameLine();if(ImGui::Button("Keep editing")) ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();ImGui::EndPopup();
        }
        if(saving && !w.Busy()) {saving=false;if(!w.dirty)app.Shutdown();}
    }
};
}
void SetupApplication(Application &app) {
    using namespace ScapsLab;
    RegisterPython();
    auto state=CreateUnique<Workflow>(app.GetJobSystemHandle().lock(),app.GetCommandRegistryHandle().lock()); auto *w=state.get();
    w->document=ReadProject(SCAPSLAB_PROJECT); w->path=(std::filesystem::current_path()/"device_project.yaml").string();
    const char *gallery=std::getenv("SCAPSLAB_GALLERY_PANEL");
    if(gallery) {
        (void)w->Dispatch({{"operation","run"}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    auto &editor=app.GetEditorLayer();
    for(int v=0;v<6;++v) if(!gallery || std::atoi(gallery)==v) {
        auto panel=CreateRef<Panel>(w,v,gallery!=nullptr); const auto id=editor.RegisterWindow(panel);
        for(PanelId i=1;i<id;++i) if(auto existing=editor.FindPanel(i).lock();existing && existing->GetTitle()=="Logging Panel") existing->SetVisible(false);
    }
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<ScapsCloseGuard>(app,*w));
    app.InsertLayerBefore(LayerId::ImGui,std::move(state));
}
}


