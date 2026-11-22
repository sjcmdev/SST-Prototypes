#include "Core/dspch.hpp"
#include "Core/App/Application.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <GLFW/glfw3.h>
#include "Core/Presentation/EditorLayer.hpp"
#include "Panel.hpp"
#include "PlotUi.hpp"
#include "Io.hpp"
#include "Core/IO/IOLayer.hpp"
#include <imgui.h>
#include <implot.h>
#include <imgui_internal.h>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <iostream>
#include <thread>
namespace {std::string startupFile;}
namespace SST {
namespace {
class DefaultLayout final : public Layer {
    std::vector<Ref<ScapsLab::Panel>> panels;
    bool done=false, focus=false, force;
    std::string layoutKey=ScapsLab::ExperimentalPanels()?"full1":"basic2";
public:
    DefaultLayout(std::vector<Ref<ScapsLab::Panel>> panels,bool force):Layer("ScapsLab layout"),panels(std::move(panels)),force(force) {}
    void OnImGuiRender() override {
        // The deferred Core settings payload must load before the default layout is built.
        if(ImGui::GetFrameCount()<3) return;
        if(done) {
            if(auto *fw=panels[0]->GetWorkflow();fw->focusPanel>=0) {ImGui::SetWindowFocus(panels[fw->focusPanel]->ImGuiName().c_str());fw->focusPanel=-1;}
            auto *w=panels[0]->GetWorkflow();
            if(w->focusLayers) {ImGui::SetWindowFocus(panels[w->Current().selectedInterface>=0?12:1]->ImGuiName().c_str());w->focusLayers=false;}
            if(w->focusView>=0 && w->focusView<int(panels.size()) && !ScapsLab::PanelShown(w->focusView)) w->focusView=-1;
            if(w->focusView>=0 && w->focusView<int(panels.size())) {auto &p=panels[w->focusView];p->SetVisible(true);ImGui::SetWindowFocus(p->ImGuiName().c_str());w->focusView=-1;}
            if(w->resetLayout) {w->plotSession.ResetRanges();w->plotSession.settings.layout.clear();w->plotSession.settings.panels.clear();w->projectDirty=true;w->RefreshDirty();w->resetLayout=false;(void)w->commands->Execute(CommandID{"app.layout.reset"});force=true;done=false;}
            else if(focus) {ImGui::SetWindowFocus(panels[1]->ImGuiName().c_str());ImGui::SetWindowFocus(panels[5]->ImGuiName().c_str());focus=false;}return;} done=true;
        // SettingsIniData is only filled by a save, so it is empty right after the loaded layout; serialize the live settings instead.
        const std::string ini=ImGui::SaveIniSettingsToMemory();
        // Live windows always get a section, so require a saved dock assignment for a known panel.
        const auto section=ini.find("[Window][sst-window-"+std::to_string(panels[0]->GetId())+"]");
        if(!force && !panels[0]->GetWorkflow()->plotSession.settings.layout.empty()) return;
        if(auto *w=panels[0]->GetWorkflow();!layoutKey.empty() && !w->settingsPath.empty()) {
            // Rebuild once when the panel set (or the default layout) changed since the saved layout was made.
            if(w->settings.value("layout_key",std::string{})!=layoutKey) {force=true;w->settings["layout_key"]=layoutKey;w->SaveSettings();}
        }
        if(!force && ScapsLab::ValidDockLayout(ini) && section!=std::string::npos && ini.substr(section,ini.find("\n[",section+1)-section).find("DockId=")!=std::string::npos) return;
        const auto *vp=ImGui::GetMainViewport(); const auto root=vp->ID;
        ImGui::DockBuilderRemoveNode(root); ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(root,vp->WorkPos); ImGui::DockBuilderSetNodeSize(root,vp->WorkSize);
        ImGuiID center=root,left,right,bottomLeft,topRight,bottomRight,iv,tree;
        if(!ScapsLab::ExperimentalPanels()) {
            // Basic layout: tree + editor tabs | I-V over band diagram | Run.
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.22f,&left,&center);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.17f,&right,&center);
            ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,0.40f,&tree,&bottomLeft);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Up,0.50f,&iv,&center);
            for(std::size_t i=0;i<panels.size();++i) {
                if(!ScapsLab::PanelShown(int(i))) continue;
                auto &panel=panels[i]; auto state=panel->GetState();
                state.visible=true;state.mode=WindowMode::Normal;
                state.dockId=i==0?tree:i==4?right:i==10?iv:i==5||i==16||i==17||i==18||i==22?center:bottomLeft;
                panel->ApplyState(state); ImGui::DockBuilderDockWindow(panel->ImGuiName().c_str(),state.dockId);
            }
            ImGui::DockBuilderFinish(root); focus=true; return;
        }
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.24f,&left,&center);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.25f,&right,&center);
        ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,0.55f,&tree,&bottomLeft);
        ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,0.62f,&topRight,&bottomRight);
        ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.38f,&iv,&center);
        for(std::size_t i=0;i<panels.size();++i) {
            auto &panel=panels[i]; auto state=panel->GetState();
            state.visible=true;state.mode=WindowMode::Normal;
            state.dockId=i==0?tree:i==15 || i==1 || i==2 || i==11 || i==12 || i==13 || i==20 || i==21?bottomLeft:i==3?bottomRight:i==4?topRight:i==10?iv:center;
            panel->ApplyState(state); ImGui::DockBuilderDockWindow(panel->ImGuiName().c_str(),state.dockId);
        }
        ImGui::DockBuilderFinish(root); focus=true;
    }
};
class ScapsCloseGuard final : public Layer {
    Application &app;ScapsLab::Workflow &w;
    bool requested=false,saving=false,closing=false,creating=false;
    std::vector<Ref<ScapsLab::Panel>> panels;
    ScapsLab::PlotSettings loadedSettings,observedSettings,persistedSettings;
    unsigned appliedEpoch=~0u;
    std::chrono::steady_clock::time_point changedAt=std::chrono::steady_clock::now(),capturedAt{};
    void CaptureSession() {
        auto &s=w.plotSession.settings;
        for(const auto &panel:panels)if(panel->GetId())s.panels[panel->GetTitle()]=panel->IsVisible();
        ScapsLab::CaptureDockLayout(s,closing);
        capturedAt=std::chrono::steady_clock::now();
    }
    void PersistSession(bool force=false) {
        const auto now=std::chrono::steady_clock::now();
        for(const auto &panel:panels)if(panel->GetId())w.plotSession.settings.panels[panel->GetTitle()]=panel->IsVisible();
        // Exit flushes the last good frame; it must never sample a dismantled dock tree.
        if(!force && now-capturedAt>std::chrono::seconds(1))CaptureSession();
        const auto &s=w.plotSession.settings;
        if(s!=observedSettings){observedSettings=s;changedAt=now;}
        if(s!=persistedSettings && (force || now-changedAt>std::chrono::milliseconds(750))){w.settings["session"]=s.Write();w.SaveSettings();persistedSettings=s;}
    }
    void ApplySession() {
        auto &s=w.plotSession.settings;
        s.Read(w.project.ui);w.plotSession.runtime.clear();w.plotSession.epoch=w.epoch;appliedEpoch=w.epoch;loadedSettings=s;
        ScapsLab::ApplyDockLayout(s);
        for(auto &panel:panels)if(const auto it=s.panels.find(panel->GetTitle());it!=s.panels.end())panel->SetVisible(it->second);
        observedSettings=s;changedAt=std::chrono::steady_clock::now();
    }
    std::string opening;
    std::string settingsPlot;
    bool exporting=false, exportAll=false;
    int exportFormat=0, selectedRun=0;
    char exportPath[2048]="scapslab-results.csv";
    bool Save(bool saveAs=false) {
        try {
            auto target=w.path;
            if(saveAs || target.empty()) {const auto file=SaveFileDialog({"ScapsLab project","scapslab"},"Untitled.scapslab");if(!file)return false;target=file->Utf8();}
            if(std::filesystem::path(target).extension()!=".scapslab") target+=".scapslab";
            CaptureSession();w.plotSession.settings.WriteChanges(w.project.ui,loadedSettings);PersistSession(true);
            app.GetEventBusRef()->PublishNew<ScapsLab::ProjectSaveRequested>(target,w.includeResults);return w.ioResult.value("ok",false);
        } catch(const std::exception &e) {w.status=e.what();return false;}
    }
    void Proceed() {
        if(!w.recoveryPath.empty() && opening!=w.recoveryPath) w.DropRecovery(w.path);
        if(closing) {PersistSession(true);w.dirty=false;app.Shutdown();}
        else if(creating) {(void)w.Dispatch({{"operation","project_new"}});creating=false;}
        else if(!opening.empty()) {app.GetEventBusRef()->PublishNew<ScapsLab::ProjectOpenRequested>(opening);opening.clear();}
    }
public:
    ScapsCloseGuard(Application &app,ScapsLab::Workflow &w,std::vector<Ref<ScapsLab::Panel>> panels):Layer("ScapsLab file actions"),app(app),w(w),panels(std::move(panels)) {
        if(!std::getenv("SCAPSLAB_GALLERY_EXPORT") && !std::getenv("SCAPSLAB_NO_IDLE_THROTTLE")) app.SetIdleWaitProvider([this]()->double { // idle: sleep until input; job running: ~20 fps
            if(ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput || this->w.tutorial.Active() || this->requested || this->exporting) return 0.;
            return this->w.Pending() || this->w.warmJob ? 0.05 : 0.5;});
        app.SetCloseGuard([this] {if(!this->w.dirty && !this->w.Pending()){PersistSession(true);return true;}creating=false;closing=true;requested=true;return false;});
        persistedSettings=w.plotSession.settings;
        if(const auto config=app.GetConfigManager().lock()) w.SetRecentPath((config->GetConfigDirectory()/Path{"scapslab-recent.json"}).Utf8()); // async read/write; Workflow::OnUpdate persists changes
    }
    void OnUpdate(float) override {
        if(ImGui::GetFrameCount()>=3 && appliedEpoch!=w.epoch)ApplySession();
    }
    void OnImGuiRender() override {
        w.interacting=ImGui::IsAnyItemActive();w.dragging=std::exchange(w.dragSeen,false); // dragSeen: a slider was held last frame
        if(ImGui::GetFrameCount()<3)return; // apply after Core's deferred window settings
        if(std::getenv("SCAPSLAB_PERF")) { // frame-time log: avg/max over 2 s windows
            static double sum=0,mx=0;static int n=0;static auto last=std::chrono::steady_clock::now();
            const double ms=ImGui::GetIO().DeltaTime*1000.;sum+=ms;mx=std::max(mx,ms);++n;
            if(std::chrono::steady_clock::now()-last>std::chrono::seconds(2)) {std::fprintf(stderr,"[perf] frames %d avg %.2f ms max %.2f ms | %s\n",n,sum/n,mx,w.MemoryReport().c_str());sum=mx=0;n=0;last=std::chrono::steady_clock::now();}
        }
        if(appliedEpoch!=w.epoch)return;
        PersistSession();
        if(w.plotPreferences){
            ImGui::SetNextWindowSize(ImVec2(470,360),ImGuiCond_FirstUseEver);
            if(ImGui::Begin("Plot settings",&w.plotPreferences) && ImGui::BeginTable("plot_preferences",2)){
                ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Auto-fit margin [%]");ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);
                if(ImGui::SliderFloat("##auto_fit_margin",&w.plotSession.settings.margin,0,30,"%.0f %%")){++w.plotSession.revision;w.projectDirty=true;w.RefreshDirty();}
                auto &session=w.plotSession;
                const auto row=[](const char *label){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);};
                row("Plot");if(ImGui::BeginCombo("##plot",settingsPlot.empty()?"Select a plot":settingsPlot.c_str())){for(const auto &[id,v]:session.settings.plots)if(ImGui::Selectable(id.c_str(),id==settingsPlot))settingsPlot=id;ImGui::EndCombo();}
                if(!settingsPlot.empty()){
                    auto &v=session.settings.plots[settingsPlot];const auto &r=session.runtime[settingsPlot];bool changed=false;
                    const char *axes[]={"Log X1","Log X2","Log X3","Log Y1","Log Y2","Log Y3"};
                    for(int i=0;i<6;++i)if(r.enabled[i]){row(axes[i]);ImGui::PushID(i);bool log=v.log[i].value_or(r.logBefore[i]);if(ImGui::Checkbox("##log",&log)){v.log[i]=log;changed=true;}ImGui::PopID();}
                    row("Legend shown");bool shown=v.legend.value_or(r.legendShown);if(ImGui::Checkbox("##legend",&shown)){v.legend=shown;changed=true;}
                    row("Legend position");const int locations[]={ImPlotLocation_NorthWest,ImPlotLocation_North,ImPlotLocation_NorthEast,ImPlotLocation_West,ImPlotLocation_Center,ImPlotLocation_East,ImPlotLocation_SouthWest,ImPlotLocation_South,ImPlotLocation_SouthEast};
                    int location=0;for(int i=0;i<9;++i)if(locations[i]==v.legendLocation.value_or(r.legendLocation))location=i;
                    if(ImGui::Combo("##legend_position",&location,"Top left\0Top\0Top right\0Left\0Center\0Right\0Bottom left\0Bottom\0Bottom right\0")){v.legendLocation=locations[location];changed=true;}
                    if(changed){++session.revision;w.projectDirty=true;w.RefreshDirty();}
                }
                ImGui::EndTable();
            }ImGui::End();
        }
        if(auto *window=glfwGetCurrentContext()) glfwSetWindowTitle(window,(w.project.name+(w.dirty?" *":"")+" - ScapsLab").c_str());
        if(!ImGui::GetIO().WantTextInput) {
            if(ImGui::IsKeyPressed(ImGuiKey_F,false) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt) w.fineLock=!w.fineLock; // F = fine slider mode (touchpads)
            for(const auto &[chord,id]:{
                std::pair{ImGuiMod_Ctrl | ImGuiKey_Z,"scapslab.undo"},std::pair{ImGuiMod_Ctrl | ImGuiKey_Y,"scapslab.redo"},
                std::pair{ImGuiMod_Ctrl | ImGuiKey_N,"scapslab.file.new"}, // Ctrl+S: Core's global binding -> CoreEvents::ProjectSaveRequested -> Workflow::RegisterIo
                std::pair{ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,"scapslab.file.save_as"},std::pair{ImGuiMod_Ctrl | ImGuiKey_O,"scapslab.file.open"}})
                if(ImGui::Shortcut(chord) && w.commands->CanExecute(CommandID{id}))(void)w.commands->Execute(CommandID{id});
        }
        if((!w.openRequest.empty() || !w.fileAction.empty()) && w.Pending()) w.status="Project action queued; waiting for the current calculation or pending edits";
        if(!w.fileAction.empty() && !w.Pending()) {
            const auto action=std::exchange(w.fileAction,std::string{});
            if(action=="new") {creating=true;closing=false;if(w.dirty)requested=true;else Proceed();}
            else if(action=="exit") {if(!w.dirty){PersistSession(true);app.Shutdown();}else {creating=false;closing=true;requested=true;}}
            else if(action=="export_results") exporting=true;
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
                ImGui::TextWrapped("CSV/JSON include point status, provenance and effective settings. Plots: PNG/SVG/PDF via PlotLab (one file per run).");
                ImGui::BeginDisabled(w.Busy() || runs.empty());
                if(ImGui::Button("Export")) {std::vector<std::string> ids;if(!exportAll)ids.push_back(runs[selectedRun].at("run_id").get<std::string>());app.GetEventBusRef()->PublishNew<ScapsLab::ProjectExportRequested>(exportPath,formats[exportFormat],exportAll,ids);}
                ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Close"))exporting=false;
                ImGui::TextWrapped("%s",w.status.c_str());
            }ImGui::End();
        }
        if(!w.openRequest.empty() && !w.Pending()) {opening=std::exchange(w.openRequest,std::string{});creating=false;closing=false;if(w.dirty)requested=true;else Proceed();}
        if(requested) {ImGui::OpenPopup("Unsaved ScapsLab project");requested=false;}
        if(ImGui::BeginPopupModal("Unsaved ScapsLab project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(closing?"Save changes before closing?":creating?"Save changes before creating a new project?":"Save changes before opening another project?");
            ImGui::TextWrapped("%s",w.status.c_str());
            if(w.Saving()) ImGui::TextUnformatted("Saving the project, please wait...");
            else if(closing && !w.dirty && !w.Busy() && !saving) {Proceed();ImGui::CloseCurrentPopup();} // save finished while the close popup was up
            if(w.Busy()) ImGui::TextUnformatted("Wait for the active operation or cancel it in Devices / Run.");
            ImGui::BeginDisabled(w.Busy() || saving);
            if(ImGui::Button("Save and continue")) saving=Save();
            ImGui::SameLine();if(ImGui::Button("Discard")) {Proceed();ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Keep editing")) {saving=false;opening.clear();creating=false;closing=false;ImGui::CloseCurrentPopup();}
            if(saving && !w.Pending()) {saving=false;if(w.response.value("ok",false) && !w.dirty) {Proceed();ImGui::CloseCurrentPopup();}}
            ImGui::EndPopup();
        }
    }
};
}
void SetupApplication(Application &app) {
    using namespace ScapsLab;
    RegisterPython();
    std::string startupSettings;
    if(const auto config=app.GetConfigManager().lock()) {
        UiSettingsPath()=(config->GetConfigDirectory()/Path{"scapslab-ui.json"}).Utf8();
        startupSettings=UiSettingsPath(); // read below, once the Workflow exists (start-up read, before the first frame)
    }
    if(std::getenv("SCAPSLAB_EXPERIMENTAL")) ExperimentalPanels()=std::string(std::getenv("SCAPSLAB_EXPERIMENTAL"))=="1";
    if(auto jobs=app.GetJobSystemHandle().lock()) {
        const auto hardware=std::thread::hardware_concurrency();
        if(hardware>1 && jobs->GetThreadCount()<hardware-1)(void)jobs->SetThreadCount(hardware-1);
    }
    auto state=CreateUnique<Workflow>(app.GetJobSystemHandle().lock(),app.GetCommandRegistryHandle().lock(),app.GetEventBusRef(),app.GetNotifierRef()); auto *w=state.get();
    w->RegisterIo(app.GetIOLayer());
    if(!startupSettings.empty()) {
        w->settingsPath=startupSettings;std::string text,error;
        if(Io::Load(startupSettings,text,error)) {try {w->settings=Json::parse(text);}catch(...) {}}
        if(!w->settings.is_object()) w->settings=Json::object();
        ExperimentalPanels()=w->settings.value("experimental_panels",false);
        if(w->settings.contains("session"))w->plotSession.settings.Read(w->settings["session"]);
    }
    w->Current().document=NewDeviceDocument(); w->Current().name=w->Current().document.at("case_id");w->path="";
    if(!startupFile.empty()) {(void)w->Dispatch({{"operation","project_load"},{"path",startupFile}});while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
    const char *gallery=std::getenv("SCAPSLAB_GALLERY_PANEL");
    w->lightResponse=true;w->viewIdleSeconds=5; // S4: Json views of idle results are freed (typed results stay)
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
        while(w->Pending()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        w->Current().selectedInterface=0;
    }
    if(gallery && (std::atoi(gallery)>=19 || std::getenv("SCAPSLAB_GALLERY_PN")) && startupFile.empty()) {
        (void)w->Dispatch({{"operation","edit"},{"document",ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string())}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)==22 && startupFile.empty()) {
        auto draft=w->Current().document;draft["external"]["series_ohm_m2"]=1e-5;draft["external"]["shunt_ohm_m2"]=1.;
        for(int i=0;i<2;++i) {auto defect=NewDefect();defect["scapslab_id"]="gr-defect-"+std::to_string(i);defect["Nt_m3"]=1e17;draft["layers"][0]["defects"].push_back(defect);}
        for(auto &layer:draft["layers"]) layer["recombination"]={{"radiative_cm3_s",1e-14},{"auger_n_cm6_s",2.8e-31},{"auger_p_cm6_s",1e-31}};
        auto sheet=NewDefect(true);sheet["id"]="gr-interface";sheet["Nt_m2"]=1e10;draft["interface_defects"].push_back(sheet);
        (void)w->Dispatch({{"operation","edit"},{"document",draft}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        w->Current().selectedBias=10;
    }
    if(gallery && startupFile.empty()) {
        (void)w->Dispatch({{"operation",std::atoi(gallery)>=11 && std::atoi(gallery)<19?"band":"run"},{"voltage_V",0.05}});
        while(w->Pending()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)==19 && std::getenv("SCAPSLAB_OPTICAL_TAB") && std::string(std::getenv("SCAPSLAB_OPTICAL_TAB"))=="2") {
        (void)w->Dispatch({{"operation","edit"},{"patch",{{"external",{{"series_ohm_m2",1e-5},{"shunt_ohm_m2",1.}}}}}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        (void)w->Dispatch({{"operation","qe"},{"start_nm",300.},{"stop_nm",1500.},{"points",61}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    if(gallery && std::atoi(gallery)>=16 && std::atoi(gallery)<=18) {
        auto settings=BatchDefaults();settings["axes"][0]["count"]=5;
        (void)w->Dispatch({{"operation","batch_settings"},{"spec",settings}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        (void)w->Dispatch({{"operation","run_batch"}});
        while(w->Pending()){w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    const bool single=gallery && std::string(gallery)!="default";
    if(gallery && startupFile.empty() && (!single || (std::atoi(gallery)>=6 && std::atoi(gallery)<10))) {
        for(const auto *kind:{"cv","cf"}) {
            (void)w->Dispatch({{"operation",kind},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
                {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->Current().document.at("temperature_K")}});
            while(w->Pending()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        }
    }
    if(std::getenv("SCAPSLAB_GALLERY_DIRTY")) {
        (void)w->Dispatch({{"operation","project_edit"},{"name",w->project.name+" (edited)"}});
        while(w->Pending()) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    }
    auto &editor=app.GetEditorLayer();
    std::vector<Ref<Panel>> panels;
    for(int v=0;v<23;++v) if(!single || std::atoi(gallery)==v) {
        auto panel=CreateRef<Panel>(w,v,single); panels.push_back(panel);
        if(!single && !PanelShown(v)) continue; // hidden in basic mode: kept only so panel indices stay valid, never registered or docked
        const auto id=editor.RegisterWindow(panel);
        for(PanelId i=1;i<id;++i) if(auto existing=editor.FindPanel(i).lock();existing && existing->GetTitle()=="Logging Panel") existing->SetVisible(false);
    }
    if(!single) app.InsertLayerBefore(LayerId::ImGui,CreateUnique<DefaultLayout>(panels,gallery!=nullptr));
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<ScapsCloseGuard>(app,*w,panels));
    InstallTutorialUi(app,*w,panels);
    app.InsertLayerBefore(LayerId::ImGui,std::move(state));
}
}



int main(int argc,char **argv) {
    for(int i=1;i<argc;++i) if(std::string(argv[i])=="--tutorial-verify") {
        try {
            if(i+1>=argc || std::string(argv[i+1]).starts_with("--")) throw std::invalid_argument("--tutorial-verify requires a tutorial ID");
            const std::string id=argv[i+1];
            std::string reportPath="tutorial-report.json";
            for(int j=1;j<argc;++j) if(std::string(argv[j])=="--tutorial-report") {if(j+1>=argc)throw std::invalid_argument("--tutorial-report requires a JSON path");reportPath=argv[j+1];}
            const auto report=SST::ScapsLab::VerifyTutorial(id);
            std::string error;
            if(!SST::TextFileIO::Save(SST::Path{reportPath},report.dump(2),error)) throw std::runtime_error(error);
            std::cout<<report.dump(2)<<std::endl;return report.value("ok",false)?0:1;
        } catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 2;}
    }
    for(int i=1;i<argc;++i) {const std::filesystem::path p=argv[i];if(p.extension()==".scapslab" || p.extension()==".yaml" || p.extension()==".yml") {startupFile=std::filesystem::absolute(p).string();break;}}
    auto app=SST::Application::Create(argc,argv,SST::SetupApplication);return app.Run();
}
