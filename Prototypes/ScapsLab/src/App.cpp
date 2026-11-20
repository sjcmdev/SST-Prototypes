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
            state.dockId=i==15?devices:i==0?topLeft:i==1 || i==2 || i==11 || i==12 || i==13 || i==20 || i==21?bottomLeft:i==3?bottomRight:i==4?topRight:i==10?iv:center;
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
    std::string appliedLayout;
    bool exporting=false, exportAll=false;
    int exportFormat=0, selectedRun=0;
    char exportPath[2048]="scapslab-results.csv";
    bool Save(bool saveAs=false) {
        try {
            auto target=w.path;
            if(saveAs || target.empty()) {const auto file=SaveFileDialog({"ScapsLab project","scapslab"},"Untitled.scapslab");if(!file)return false;target=file->Utf8();}
            if(std::filesystem::path(target).extension()!=".scapslab") target+=".scapslab";
            std::size_t iniSize=0;const auto *ini=ImGui::SaveIniSettingsToMemory(&iniSize);w.project.ui["imgui_ini"]=std::string(ini,iniSize);
            const auto response=w.Dispatch({{"operation","project_save"},{"path",target},{"include_results",w.includeResults}});return response.at("ok").get<bool>();
        } catch(const std::exception &e) {w.status=e.what();return false;}
    }
    void Proceed() {
        if(!w.recoveryPath.empty() && opening!=w.recoveryPath) {std::error_code ec;if(w.path.empty() || !std::filesystem::equivalent(w.path,w.recoveryPath,ec))std::filesystem::remove(w.recoveryPath,ec);}
        if(closing) {w.dirty=false;app.Shutdown();}
        else if(!opening.empty()) {(void)w.Dispatch({{"operation","project_load"},{"path",opening}});opening.clear();}
    }
public:
    ScapsCloseGuard(Application &app,ScapsLab::Workflow &w):Layer("ScapsLab file actions"),app(app),w(w) {
        app.SetCloseGuard([this] {if(!this->w.dirty && !this->w.Busy())return true;closing=true;requested=true;return false;});
        if(const auto config=app.GetConfigManager().lock()) recentPath=config->GetConfigDirectory()/Path{"scapslab-recent.json"};
        std::string text,error;if(TextFileIO::Load(recentPath,text,error)) {try {const auto items=ScapsLab::Json::parse(text);if(items.is_array() && items.size()<=10 && std::all_of(items.begin(),items.end(),[](const auto &p){return p.is_string();})) w.recent=items;}catch(...) {}}
        savedRecent=w.recent;
        if(!recentPath.Empty()) {w.recoveryPath=(recentPath.parent_path()/Path{"scapslab-recovery.scapslab"}).Utf8();if(std::filesystem::exists(w.recoveryPath))w.status="Autosave found: File > Recover autosave";}
    }
    void OnImGuiRender() override {
        w.interacting=ImGui::IsAnyItemActive();
        const auto layout=w.project.ui.value("imgui_ini",std::string{});
        if(!layout.empty() && layout!=appliedLayout && !w.Busy()) {ImGui::LoadIniSettingsFromMemory(layout.c_str(),layout.size());appliedLayout=layout;}
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
            if(action=="export_results") exporting=true;
            else if(action=="recover") w.openRequest=w.recoveryPath;
            else if(action=="save" || action=="save_as") (void)Save(action=="save_as");
            else if(action=="import_def" || action=="export_def") {try {const auto file=action=="import_def"?OpenFileDialog({"SCAPS definition","def"}):SaveFileDialog({"SCAPS definition","def"},"device.def");if(file)(void)w.Dispatch({{"operation",action},{"path",file->Utf8()}});}catch(const std::exception &e){w.status=e.what();}}
            else if(action=="open") {try {if(const auto file=OpenFileDialog({"ScapsLab / device","scapslab,yaml,yml"})) w.openRequest=file->Utf8();}catch(const std::exception &e){w.status=e.what();}}
        }
        if(std::getenv("SCAPSLAB_GALLERY_EXPORT")) exporting=true;
        if(exporting) {
            if(std::getenv("SCAPSLAB_GALLERY_EXPORT"))ImGui::SetNextWindowFocus();
            ImGui::SetNextWindowSize(ImVec2(720,400),ImGuiCond_FirstUseEver);
            if(ImGui::Begin("Export results",&exporting)) {
                const auto runs=w.Runs();
                const char *formats[]={"csv","json","png","svg","pdf"};
                ImGui::TextUnformatted("Saved snapshots only. Export does not calculate or change solver settings.");
                if(ImGui::BeginTable("export_fields",2,ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Label",0,.25f);ImGui::TableSetupColumn("Control",0,.75f);
                    auto row=[](const char *label){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(label);ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);};
                    row("Format");if(ImGui::Combo("##format",&exportFormat,"CSV\0JSON\0PNG\0SVG\0PDF\0")) {auto path=std::filesystem::path(exportPath).replace_extension(formats[exportFormat]);std::snprintf(exportPath,sizeof(exportPath),"%s",path.string().c_str());}
                    row("All saved runs");ImGui::Checkbox("##all",&exportAll);
                    row("Selected run");ImGui::BeginDisabled(exportAll || runs.empty());
                    selectedRun=std::clamp(selectedRun,0,std::max(0,int(runs.size())-1));
                    const std::string preview=runs.empty()?"No saved results":runs[selectedRun].at("run_id").get<std::string>();
                    if(ImGui::BeginCombo("##run",preview.c_str())) {for(int i=0;i<int(runs.size());++i) if(ImGui::Selectable(runs[i].at("run_id").get<std::string>().c_str(),i==selectedRun))selectedRun=i;ImGui::EndCombo();}ImGui::EndDisabled();
                    row("Destination");ImGui::InputText("##destination",exportPath,sizeof(exportPath),ImGuiInputTextFlags_AutoSelectAll);
                    ImGui::EndTable();
                }
                if(ImGui::Button("Browse...")) {try {if(const auto file=SaveFileDialog({"Results export",formats[exportFormat]},exportPath))std::snprintf(exportPath,sizeof(exportPath),"%s",file->Utf8().c_str());}catch(const std::exception &e){w.status=e.what();}}
                ImGui::TextWrapped("CSV/JSON include point status, provenance and effective settings. Batch/Recorder is one sheet. Plots: PNG/SVG/PDF via PlotLab (one file per run).");
                ImGui::BeginDisabled(w.Busy() || runs.empty());
                if(ImGui::Button("Export")) {ScapsLab::Json ids=ScapsLab::Json::array();if(!exportAll)ids.push_back(runs[selectedRun].at("run_id"));const auto r=w.Dispatch({{"operation","export_results"},{"path",exportPath},{"format",formats[exportFormat]},{"all",exportAll},{"run_ids",ids}});if(!r.value("ok",false))w.status=r.at("error").at("message");}
                ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Close"))exporting=false;
                ImGui::TextWrapped("%s",w.status.c_str());
            }ImGui::End();
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
    w->autoRecalculate=true;
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
    if(gallery && std::atoi(gallery)>=19 && startupFile.empty()) {
        (void)w->Dispatch({{"operation","edit"},{"document",ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string())}});
        while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)==22 && startupFile.empty()) {
        auto draft=w->Current().document;
        for(int i=0;i<2;++i) {auto defect=NewDefect();defect["scapslab_id"]="gr-defect-"+std::to_string(i);defect["Nt_m3"]=1e17;draft["layers"][0]["defects"].push_back(defect);}
        for(auto &layer:draft["layers"]) layer["recombination"]={{"radiative_cm3_s",1e-14},{"auger_n_cm6_s",2.8e-31},{"auger_p_cm6_s",1e-31}};
        auto sheet=NewDefect(true);sheet["id"]="gr-interface";sheet["Nt_m2"]=1e10;draft["interface_defects"].push_back(sheet);
        (void)w->Dispatch({{"operation","edit"},{"document",draft}});
        while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        w->Current().selectedBias=10;
    }
    if(gallery && startupFile.empty()) {
        (void)w->Dispatch({{"operation",std::atoi(gallery)>=11 && std::atoi(gallery)<19?"band":"run"},{"voltage_V",0.05}});
        while(w->Busy()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)==19 && std::getenv("SCAPSLAB_OPTICAL_TAB") && std::string(std::getenv("SCAPSLAB_OPTICAL_TAB"))=="2") {
        (void)w->Dispatch({{"operation","qe"},{"start_nm",300.},{"stop_nm",1500.},{"points",61}});
        while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)>=16 && std::atoi(gallery)<=18) {
        auto settings=BatchDefaults();settings["axes"][0]["count"]=5;
        (void)w->Dispatch({{"operation","batch_settings"},{"spec",settings}});
        while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        (void)w->Dispatch({{"operation","run_batch"}});
        while(w->Busy()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
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
            ImGui::MenuItem("Include results in project",nullptr,&w->includeResults);
            if(ImGui::MenuItem("Export results...")) w->fileAction="export_results";
            if(ImGui::MenuItem("Recover autosave...",nullptr,false,!w->recoveryPath.empty() && std::filesystem::exists(w->recoveryPath))) w->fileAction="recover";
            ImGui::Separator();
            if(ImGui::MenuItem("Import SCAPS .def...")) w->fileAction="import_def";
            if(ImGui::MenuItem("Export SCAPS .def...")) w->fileAction="export_def";
            if(ImGui::BeginMenu("Recent files")) {for(const auto &path:w->recent) if(ImGui::MenuItem(path.get<std::string>().c_str())) w->openRequest=path;ImGui::EndMenu();}
            
            if(ImGui::BeginMenu("Examples")) {for(const auto &path:examples) if(ImGui::MenuItem(path.stem().string().c_str())) w->openRequest=path.string();ImGui::EndMenu();}
            ImGui::EndDisabled();ImGui::EndMenu();
        }
    });
    editor.RegisterMenuRenderer("scapslab.edit",[w](EditorServices &) {
        if(ImGui::BeginMenu("Edit")) {ImGui::BeginDisabled(w->Busy());if(ImGui::MenuItem("Undo","Ctrl+Z"))(void)w->Dispatch({{"operation","undo"}});if(ImGui::MenuItem("Redo","Ctrl+Y"))(void)w->Dispatch({{"operation","redo"}});ImGui::EndDisabled();ImGui::EndMenu();}
        if(ImGui::BeginMenu("Run")) {ImGui::BeginDisabled(w->Busy());if(ImGui::MenuItem("Calculate I-V"))(void)w->Dispatch({{"operation","run"}});if(ImGui::MenuItem("Run batch"))(void)w->Dispatch({{"operation","run_batch"}});ImGui::EndDisabled();ImGui::BeginDisabled(!w->active);if(ImGui::MenuItem("Stop"))(void)w->Dispatch({{"operation","cancel"}});ImGui::EndDisabled();ImGui::EndMenu();}
    });
    std::vector<Ref<Panel>> panels;
    for(int v=0;v<23;++v) if(!single || std::atoi(gallery)==v) {
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
