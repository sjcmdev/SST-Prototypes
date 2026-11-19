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
        // The deferred Core settings payload must load before the default layout is built.
        if(ImGui::GetFrameCount()<3) return;
        if(done) {
            if(panels[0]->GetWorkflow()->focusLayers) {auto *w=panels[0]->GetWorkflow();ImGui::SetWindowFocus(panels[w->selectedInterface>=0?12:1]->ImGuiName().c_str());w->focusLayers=false;}
            if(focus) {ImGui::SetWindowFocus(panels[1]->ImGuiName().c_str());ImGui::SetWindowFocus(panels[5]->ImGuiName().c_str());focus=false;}return;} done=true;
        const auto &ini=ImGui::GetCurrentContext()->SettingsIniData;
        if(!force && ini.size()>0 && std::string(ini.c_str()).find("[Window][sst-window-"+std::to_string(panels[14]->GetId())+"]")!=std::string::npos) return;
        const auto *vp=ImGui::GetMainViewport(); const auto root=vp->ID;
        ImGui::DockBuilderRemoveNode(root); ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(root,vp->WorkPos); ImGui::DockBuilderSetNodeSize(root,vp->WorkSize);
        ImGuiID center=root,left,right,topLeft,bottomLeft,topRight,bottomRight,iv;
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.24f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.25f,&right,&center);
        ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,0.30f,&topLeft,&bottomLeft);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,0.62f,&topRight,&bottomRight);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.38f,&iv,&center);
        for(std::size_t i=0;i<panels.size();++i) {
            auto &panel=panels[i]; auto state=panel->GetState();
            state.visible=true;state.mode=WindowMode::Normal;
            state.dockId=i==0?topLeft:i==1 || i==2 || i==11 || i==12 || i==13?bottomLeft:i==3?bottomRight:i==4?topRight:i==10?iv:center;
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
        w.interacting=ImGui::IsAnyItemActive();
        if(!ImGui::GetIO().WantTextInput) {
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) (void)w.Dispatch({{"operation","undo"}});
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) (void)w.Dispatch({{"operation","redo"}});
        }
        if(requested) {ImGui::OpenPopup("Unsaved ScapsLab project");requested=false;}
        if(ImGui::BeginPopupModal("Unsaved ScapsLab project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your changes before closing?");
            ImGui::TextWrapped("%s",w.status.c_str());
            if(w.Busy()) ImGui::TextUnformatted("Wait for the active operation, or cancel it in Run, before closing.");
            ImGui::BeginDisabled(w.Busy());
            if(ImGui::Button("Save and close")) {const auto r=w.Dispatch({{"operation","save"},{"path",w.path}});saving=r.at("ok");}
            ImGui::SameLine();if(ImGui::Button("Discard")) {w.dirty=false;app.Shutdown();}
            ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::Button("Keep editing")) {saving=false;ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
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
    w->autoRecalculateBand=true;
    ImGui::GetIO().ConfigDebugHighlightIdConflicts=true;
    if(gallery && std::atoi(gallery)>=11) {
        auto draft=w->document;
        draft["layers"][0]["defects"]=Json::array();
        for(const auto *shape:{"gaussian","cb_tail","vb_tail"}) {
            auto d=NewDefect();d["scapslab_id"]=std::string("preview-")+shape;d["energy_distribution"]=shape;d["Nt_m3"]=1e17;
            if(std::string(shape)=="cb_tail") d["energy_reference"]="relative_to_Ec";
            if(std::string(shape)=="vb_tail") d["energy_reference"]="relative_to_Ev";
            draft["layers"][0]["defects"].push_back(d);
        }
        auto second=draft["layers"][0];second["scapslab_id"]="layer-preview-2";second["name"]="Window";second["thickness_m"]=2e-7;
        second["defects"]=Json::array();second["material"]["chi_eV"]=4.1;second["material"]["Eg_eV"]=1.4;draft["layers"].push_back(second);
        auto sheet=NewDefect(true);sheet["id"]="interface-preview";sheet["energy_distribution"]="gaussian";sheet["energy_offset_eV"]=0.5;sheet["Nt_m2"]=1e11;
        draft["interface_defects"]=Json::array({sheet});draft["external"]["area_m2"]=1e-4;draft["external"]["series_ohm_m2"]=1e-5;draft["external"]["shunt_ohm_m2"]=1.;
        (void)w->Dispatch({{"operation","edit"},{"document",draft}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        w->selectedInterface=0;
    }
    if(gallery) {
        (void)w->Dispatch({{"operation",std::atoi(gallery)>=11?"band":"run"},{"voltage_V",0.05}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    const bool single=gallery && std::string(gallery)!="default";
    if(gallery && (!single || (std::atoi(gallery)>=6 && std::atoi(gallery)<10))) {
        for(const auto *kind:{"cv","cf"}) {
            (void)w->Dispatch({{"operation",kind},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
                {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->document.at("temperature_K")}});
            while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        }
    }
    auto &editor=app.GetEditorLayer(); std::vector<Ref<Panel>> panels;
    for(int v=0;v<15;++v) if(!single || std::atoi(gallery)==v) {
        auto panel=CreateRef<Panel>(w,v,single); panels.push_back(panel); const auto id=editor.RegisterWindow(panel);
        for(PanelId i=1;i<id;++i) if(auto existing=editor.FindPanel(i).lock();existing && existing->GetTitle()=="Logging Panel") existing->SetVisible(false);
    }
    if(!single) app.InsertLayerBefore(LayerId::ImGui,CreateUnique<DefaultLayout>(panels,gallery!=nullptr));
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<ScapsCloseGuard>(app,*w));
    app.InsertLayerBefore(LayerId::ImGui,std::move(state));
}
}


