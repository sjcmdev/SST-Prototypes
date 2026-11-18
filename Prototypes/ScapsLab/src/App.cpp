#include "Core/dspch.hpp"
#include "Core/App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Presentation/EditorLayer.hpp"
#include "Panel.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdlib>
#include <thread>
namespace SST {
namespace {
class DefaultLayout final : public Layer {
    std::vector<Ref<ScapsLab::Panel>> panels;
    bool done=false, focus=false, force;
public:
    DefaultLayout(std::vector<Ref<ScapsLab::Panel>> panels,bool force):Layer("ScapsLab layout"),panels(std::move(panels)),force(force) {}
    void OnImGuiRender() override {
        if(done) {if(focus) {ImGui::SetWindowFocus(panels[5]->ImGuiName().c_str());focus=false;}return;} done=true;
        const auto &ini=ImGui::GetCurrentContext()->SettingsIniData;
        if(!force && ini.size()>0 && std::string(ini.c_str()).find("[Window][sst-window-"+std::to_string(panels[5]->GetId())+"]")!=std::string::npos) return;
        const auto *vp=ImGui::GetMainViewport(); const auto root=vp->ID;
        ImGui::DockBuilderRemoveNode(root); ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(root,vp->WorkPos); ImGui::DockBuilderSetNodeSize(root,vp->WorkSize);
        ImGuiID center=root,left,right,topLeft,bottomLeft,topRight,bottomRight;
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.24f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.29f,&right,&center);
        ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,0.30f,&topLeft,&bottomLeft);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,0.45f,&topRight,&bottomRight);
        for(std::size_t i=0;i<panels.size();++i) {
            auto &panel=panels[i]; auto state=panel->GetState();
            state.dockId=i==0?topLeft:i==1 || i==2?bottomLeft:i==3?bottomRight:i==4?topRight:center;
            panel->ApplyState(state); ImGui::DockBuilderDockWindow(panel->ImGuiName().c_str(),state.dockId);
        }
        ImGui::DockBuilderFinish(root); focus=true;
    }
};
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
    const bool single=gallery && std::string(gallery)!="default";
    if(gallery) {
        for(const auto *kind:{"cv","cf"}) {
            (void)w->Dispatch({{"operation",kind},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
                {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->document.at("temperature_K")}});
            while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        }
    }
    auto &editor=app.GetEditorLayer(); std::vector<Ref<Panel>> panels;
    for(int v=0;v<10;++v) if(!single || std::atoi(gallery)==v) {
        auto panel=CreateRef<Panel>(w,v,single); panels.push_back(panel); const auto id=editor.RegisterWindow(panel);
        for(PanelId i=1;i<id;++i) if(auto existing=editor.FindPanel(i).lock();existing && existing->GetTitle()=="Logging Panel") existing->SetVisible(false);
    }
    if(!single) app.InsertLayerBefore(LayerId::ImGui,CreateUnique<DefaultLayout>(panels,gallery!=nullptr));
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<ScapsCloseGuard>(app,*w));
    app.InsertLayerBefore(LayerId::ImGui,std::move(state));
}
}


