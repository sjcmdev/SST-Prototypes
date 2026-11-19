#include "Core/dspch.hpp"
#include "Core/App/Application.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <GLFW/glfw3.h>
#include "Core/Presentation/EditorLayer.hpp"
#include "Panel.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdlib>
#include <thread>
namespace {std::string startupFile;}
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
            if(panels[0]->GetWorkflow()->focusLayers) {auto *w=panels[0]->GetWorkflow();ImGui::SetWindowFocus(panels[w->Current().selectedInterface>=0?12:1]->ImGuiName().c_str());w->focusLayers=false;}
            if(focus) {ImGui::SetWindowFocus(panels[1]->ImGuiName().c_str());ImGui::SetWindowFocus(panels[5]->ImGuiName().c_str());focus=false;}return;} done=true;
        const auto &ini=ImGui::GetCurrentContext()->SettingsIniData;
        if(!force && ini.size()>0 && std::string(ini.c_str()).find("[Window][sst-window-"+std::to_string(panels[15]->GetId())+"]")!=std::string::npos) return;
        const auto *vp=ImGui::GetMainViewport(); const auto root=vp->ID;
        ImGui::DockBuilderRemoveNode(root); ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(root,vp->WorkPos); ImGui::DockBuilderSetNodeSize(root,vp->WorkSize);
        ImGuiID center=root,left,right,topLeft,bottomLeft,topRight,bottomRight,iv,devices;
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.24f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.25f,&right,&center);
        ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,0.42f,&devices,&bottomLeft);
        ImGui::DockBuilderSplitNode(bottomLeft,ImGuiDir_Up,0.25f,&topLeft,&bottomLeft);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,0.62f,&topRight,&bottomRight);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.38f,&iv,&center);
        for(std::size_t i=0;i<panels.size();++i) {
            auto &panel=panels[i]; auto state=panel->GetState();
            state.visible=true;state.mode=WindowMode::Normal;
            state.dockId=i==15?devices:i==0?topLeft:i==1 || i==2 || i==11 || i==12 || i==13?bottomLeft:i==3?bottomRight:i==4?topRight:i==10?iv:center;
            panel->ApplyState(state); ImGui::DockBuilderDockWindow(panel->ImGuiName().c_str(),state.dockId);
        }
        ImGui::DockBuilderFinish(root); focus=true;
    }
};
class ScapsCloseGuard final : public Layer {
    Application &app;ScapsLab::Workflow &w;
    bool requested=false,saving=false,closing=false;
    std::string opening;
    Path recentPath;
    ScapsLab::Json savedRecent;
    bool Save(bool saveAs=false) {
        try {
            auto target=w.path;
            if(saveAs || target.empty()) {const auto file=SaveFileDialog({"ScapsLab project","scapslab"},"Untitled.scapslab");if(!file)return false;target=file->Utf8();}
            if(std::filesystem::path(target).extension()!=".scapslab") target+=".scapslab";
            const auto response=w.Dispatch({{"operation","project_save"},{"path",target}});return response.at("ok").get<bool>();
        } catch(const std::exception &e) {w.status=e.what();return false;}
    }
    void Proceed() {
        if(closing) {w.dirty=false;app.Shutdown();}
        else if(!opening.empty()) {(void)w.Dispatch({{"operation","project_load"},{"path",opening}});opening.clear();}
    }
public:
    ScapsCloseGuard(Application &app,ScapsLab::Workflow &w):Layer("ScapsLab file actions"),app(app),w(w) {
        app.SetCloseGuard([this] {if(!this->w.dirty && !this->w.Busy())return true;closing=true;requested=true;return false;});
        if(const auto config=app.GetConfigManager().lock()) recentPath=config->GetConfigDirectory()/Path{"scapslab-recent.json"};
        std::string text,error;if(TextFileIO::Load(recentPath,text,error)) {try {const auto items=ScapsLab::Json::parse(text);if(items.is_array() && items.size()<=10 && std::all_of(items.begin(),items.end(),[](const auto &p){return p.is_string();})) w.recent=items;}catch(...) {}}
        savedRecent=w.recent;
    }
    void OnImGuiRender() override {
        w.interacting=ImGui::IsAnyItemActive();
        if(auto *window=glfwGetCurrentContext()) glfwSetWindowTitle(window,(w.project.name+(w.dirty?" *":"")+" - ScapsLab").c_str());
        if(w.recent!=savedRecent) {std::string error;if(TextFileIO::Save(recentPath,w.recent.dump(2),error))savedRecent=w.recent;else w.status=error;}
        if(!ImGui::GetIO().WantTextInput) {
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) (void)w.Dispatch({{"operation","undo"}});
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) (void)w.Dispatch({{"operation","redo"}});
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) w.fileAction="save";
            if(ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) w.fileAction="open";
        }
        if(!w.fileAction.empty() && !w.Busy()) {
            const auto action=std::exchange(w.fileAction,std::string{});
            if(action=="save" || action=="save_as") (void)Save(action=="save_as");
            else if(action=="open") {try {if(const auto file=OpenFileDialog({"ScapsLab / device","scapslab,yaml,yml"})) w.openRequest=file->Utf8();}catch(const std::exception &e){w.status=e.what();}}
        }
        if(!w.openRequest.empty() && !w.Busy()) {opening=std::exchange(w.openRequest,std::string{});closing=false;if(w.dirty)requested=true;else Proceed();}
        if(requested) {ImGui::OpenPopup("Unsaved ScapsLab project");requested=false;}
        if(ImGui::BeginPopupModal("Unsaved ScapsLab project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(closing?"Save changes before closing?":"Save changes before opening another project?");
            ImGui::TextWrapped("%s",w.status.c_str());
            if(w.Busy()) ImGui::TextUnformatted("Wait for the active operation or cancel it in Devices / Run.");
            ImGui::BeginDisabled(w.Busy() || saving);
            if(ImGui::Button("Save and continue")) saving=Save();
            ImGui::SameLine();if(ImGui::Button("Discard")) {Proceed();ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Keep editing")) {saving=false;opening.clear();closing=false;ImGui::CloseCurrentPopup();}
            if(saving && !w.Busy()) {saving=false;if(w.response.value("ok",false) && !w.dirty) {Proceed();ImGui::CloseCurrentPopup();}}
            ImGui::EndPopup();
        }
    }
};
}
void SetupApplication(Application &app) {
    using namespace ScapsLab;
    RegisterPython();
    auto state=CreateUnique<Workflow>(app.GetJobSystemHandle().lock(),app.GetCommandRegistryHandle().lock()); auto *w=state.get();
    w->Current().document=ReadProject(SCAPSLAB_PROJECT); w->Current().name=w->Current().document.at("case_id");w->path="";
    if(!startupFile.empty()) {(void)w->Dispatch({{"operation","project_load"},{"path",startupFile}});while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
    const char *gallery=std::getenv("SCAPSLAB_GALLERY_PANEL");
    w->autoRecalculateBand=true;
    ImGui::GetIO().ConfigDebugHighlightIdConflicts=true;
    if(gallery && std::atoi(gallery)>=11 && std::atoi(gallery)<=14) {
        auto draft=w->Current().document;
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
        w->Current().selectedInterface=0;
    }
    if(gallery && startupFile.empty()) {
        (void)w->Dispatch({{"operation",std::atoi(gallery)>=11?"band":"run"},{"voltage_V",0.05}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    const bool single=gallery && std::string(gallery)!="default";
    if(gallery && startupFile.empty() && (!single || (std::atoi(gallery)>=6 && std::atoi(gallery)<10))) {
        for(const auto *kind:{"cv","cf"}) {
            (void)w->Dispatch({{"operation",kind},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
                {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->Current().document.at("temperature_K")}});
            while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        }
    }
    if(std::getenv("SCAPSLAB_GALLERY_DIRTY")) {
        (void)w->Dispatch({{"operation","project_edit"},{"name",w->project.name+" (edited)"}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    auto &editor=app.GetEditorLayer();
    std::vector<std::filesystem::path> examples;if(std::filesystem::exists(ExamplesDirectory())) for(const auto &entry:std::filesystem::directory_iterator(ExamplesDirectory())) if(entry.path().extension()==".scapslab") examples.push_back(entry.path());std::sort(examples.begin(),examples.end());
    const bool galleryMenu=std::getenv("SCAPSLAB_GALLERY_MENU")!=nullptr;
    editor.RegisterMenuRenderer("scapslab.file",[w,examples,galleryMenu](EditorServices &) {
        if(galleryMenu && ImGui::GetFrameCount()>5) ImGui::OpenPopup("File");
        if(ImGui::BeginMenu("File")) {
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::MenuItem("Open...","Ctrl+O")) w->fileAction="open";
            if(ImGui::MenuItem("Save","Ctrl+S")) w->fileAction="save";
            if(ImGui::MenuItem("Save As...")) w->fileAction="save_as";
            if(ImGui::BeginMenu("Recent files")) {for(const auto &path:w->recent) if(ImGui::MenuItem(path.get<std::string>().c_str())) w->openRequest=path;ImGui::EndMenu();}
            if(galleryMenu && ImGui::GetFrameCount()>5) ImGui::OpenPopup("Examples");
            if(ImGui::BeginMenu("Examples")) {for(const auto &path:examples) if(ImGui::MenuItem(path.stem().string().c_str())) w->openRequest=path.string();ImGui::EndMenu();}
            ImGui::EndDisabled();ImGui::EndMenu();
        }
    });
    std::vector<Ref<Panel>> panels;
    for(int v=0;v<16;++v) if(!single || std::atoi(gallery)==v) {
        auto panel=CreateRef<Panel>(w,v,single); panels.push_back(panel); const auto id=editor.RegisterWindow(panel);
        for(PanelId i=1;i<id;++i) if(auto existing=editor.FindPanel(i).lock();existing && existing->GetTitle()=="Logging Panel") existing->SetVisible(false);
    }
    if(!single) app.InsertLayerBefore(LayerId::ImGui,CreateUnique<DefaultLayout>(panels,gallery!=nullptr));
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<ScapsCloseGuard>(app,*w));
    app.InsertLayerBefore(LayerId::ImGui,std::move(state));
}
}



int main(int argc,char **argv) {
    for(int i=1;i<argc;++i) {const std::filesystem::path p=argv[i];if(p.extension()==".scapslab" || p.extension()==".yaml" || p.extension()==".yml") {startupFile=std::filesystem::absolute(p).string();break;}}
    auto app=SST::Application::Create(argc,argv,SST::SetupApplication);return app.Run();
}
