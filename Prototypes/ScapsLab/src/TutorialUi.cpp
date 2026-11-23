#include "Panel.hpp"
#include "Core/Presentation/CommandMenu.hpp"
#include "Core/App/Application.hpp"
#include "Core/Presentation/EditorLayer.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Io.hpp"
#include <imgui_internal.h>
#include "Core/Presentation/Panels/HostedWindows.hpp"
#include <fstream>

namespace SST::ScapsLab {
namespace {
class MenuAction final : public ICommand {
    Workflow &w;
    std::function<void()> action;
public:
    MenuAction(Workflow &w,std::function<void()> action):w(w),action(std::move(action)) {}
    std::string Description() const override {return "ScapsLab menu action";}
    Result<void> Execute(CommandContext &) override {
        try {action();return {};}
        catch(const std::exception &e) {w.status=e.what();return StructuredError{ErrorCategory::Validation,Severity::Error,"Menu action failed",e.what(),"Check application status","ScapsLab","menu.failed"};}
    }
};
class TutorialUi final : public Layer {
    Workflow &w;
    Ref<WindowWorkspace> windows;
    std::vector<Ref<Panel>> panels;
    std::string focusedStep;
    HostedWindows tutorialWindows,layerWindows;
    std::map<std::string,Ref<Panel>> layerPanels; // extra pinned Layers editors, keyed by layer ID
    bool about=false,clear=false,reportOpen=true,lessons=false;
    Json savedRecent;
    const Json catalog=TutorialCatalog();
    // Basic mode: a lesson is listed only if every panel it targets is shown (hidden lessons stay in code).
    void SaveExperimental() {
        w.settings["experimental_panels"]=ExperimentalPanels();w.SaveSettings();
    }
    bool LessonShown(const std::string &id) const {
        for(const auto &step:TutorialDefinition(id).at("steps")) {
            const auto window=step.at("target").at("window").get<std::string>();
            for(std::size_t i=0;i<panels.size();++i) if(panels[i]->GetType()==window && !PanelShown(int(i))) return false;
        }
        return true;
    }
public:
    TutorialUi(Application &app,Workflow &w,std::vector<Ref<Panel>> panels):Layer("ScapsLab tutorials"),w(w),panels(std::move(panels)) {
        auto &editor=app.GetEditorLayer();windows=editor.GetWindows();
        editor.UseStandardMenu("Simulate");
        SetCommandMenuHook([this](const std::string &root,bool open){this->w.MenuHook(root,open);});
        if(const auto config=app.GetConfigManager().lock()) {
            const Path layout{app.GetConfig().layout.imGuiIniPath};
            const auto userDirectory=layout.Empty()?config->GetPaths().userConfigDirectory:layout.parent_path();
            w.tutorialProgressPath=(userDirectory/Path{"scapslab-tutorials.json"}).Utf8();
            std::string source,error;
            if(Io::Load(w.tutorialProgressPath,source,error)) {
                try {
                    const auto value=Json::parse(source);
                    if(!value.is_object())throw std::invalid_argument("Expected an object");
                    for(const auto &lesson:catalog) {
                        const auto id=lesson.at("tutorial_id").get<std::string>();
                        if(!value.contains(id))continue;
                        const auto &entry=value.at(id);
                        if(!entry.is_object() || !entry.at("status").is_string() || !entry.at("step_index").is_number_unsigned()
                            || entry.at("step_index").get<std::size_t>()>=lesson.at("step_count").get<std::size_t>())throw std::invalid_argument("Invalid tutorial progress");
                    }
                    w.tutorialProgress=value;
                }
                catch(const std::exception &e) {w.status=std::string("Cannot load tutorial progress: ")+e.what();}
            }
        }
        auto menu=[&](const std::string &id,const std::string &path,std::function<void()> action,const std::string &shortcut="",std::function<bool()> enabled={},std::function<bool()> checked={}) {
            CommandMeta meta{CommandID{id},path.empty()?id:path.substr(path.find('/')+1),"ScapsLab",path};
            if(path.empty())meta.flags=CommandFlags::HiddenFromPalette;
            meta.menuPath=path;meta.shortcut=shortcut;meta.enabled=std::move(enabled);meta.checked=std::move(checked);
            if(!w.commands->Register(std::move(meta),[&w,action=std::move(action)](CommandContext &){return CreateUnique<MenuAction>(w,action);}))
                throw std::runtime_error("Cannot register menu: "+id);
        };
        auto idle=[&w]{return !w.Busy();};
        auto operation=[&](const char *op,std::function<bool()> enabled,const char *path="") { // menu path and shortcut come from the typed command's Info (Commands/Commands.hpp); only `enabled` is app state
            const CommandID id{std::string("scapslab.")+op};const auto meta=w.commands->GetMeta(id);
            if(!meta || !w.commands->SetMenu(id,*path?path:meta.Value().menuPath,meta.Value().shortcut,std::move(enabled))) throw std::runtime_error("Cannot configure menu");
        };
        menu("scapslab.file.new","File/New",[&w]{w.fileAction="new";},"Ctrl+N",idle);
        (void)w.commands->SetMenu(CommandID{"app.layout.reset"},"");
        menu("scapslab.view.plot_settings","View/Plot settings",[&w]{w.plotPreferences=true;});
        menu("scapslab.view.reset","View/Reset layout",[&w]{w.resetLayout=true;});
        for(const auto &[action,label,shortcut]:{std::tuple{"open","Open...","Ctrl+O"},std::tuple{"save","Save","Ctrl+S"},std::tuple{"save_as","Save As...","Ctrl+Shift+S"},
            std::tuple{"export_results","Export results...",""},std::tuple{"import_def","Import SCAPS .def...",""},std::tuple{"export_def","Export SCAPS .def...",""}})
            menu(std::string("scapslab.file.")+action,std::string("File/")+label,[&w,action]{w.fileAction=action;},shortcut,idle);
        menu("scapslab.file.include_results","File/Include results in project",[&w]{w.includeResults=!w.includeResults;},"",idle,[&w]{return w.includeResults;});
        menu("scapslab.file.recover","File/Recover autosave...",[&w]{w.fileAction="recover";},"",[&w]{return !w.Busy() && w.recoveryAvailable;});
        menu("scapslab.file.exit","File/Exit",[&w]{w.fileAction="exit";});
        for(int i=0;i<10;++i) menu("scapslab.recent."+std::to_string(i),"",[&w,i]{if(i<int(w.recent.size()))w.openRequest=w.recent[i];},"",[&w,i]{return !w.Busy() && i<int(w.recent.size());});
        if(std::filesystem::exists(ExamplesDirectory())) for(const auto &entry:std::filesystem::directory_iterator(ExamplesDirectory())) if(entry.path().extension()==".scapslab") {
            const auto path=entry.path();const std::string stem=path.stem().string();menu("scapslab.example."+stem,"File/Examples/"+stem+(!ExperimentalPanels() && stem=="05-capacitance-ready"?" (needs experimental panels)":""),[&w,path]{w.openRequest=path.string();},"",idle);
        }
        operation("undo",[&w]{return w.CanUndo();}); // synchronous: not gated by running calculations
        operation("redo",[&w]{return w.CanRedo();});
        menu("scapslab.history.clear","Edit/Clear history...",[this]{clear=true;});
        operation("run",idle);if(ExperimentalPanels())operation("run_batch",idle,"Simulate/Run batch");
        operation("cancel",[&w]{return w.AnyActive();});
        menu("scapslab.view.experimental","View/Show experimental panels (applies after restart)",[this]{ExperimentalPanels()=!ExperimentalPanels();SaveExperimental();this->w.status="Experimental panels "+std::string(ExperimentalPanels()?"on":"off")+": restart ScapsLab to apply";},"",{},[]{return ExperimentalPanels();});
        for(const auto &panel:this->panels) if(panel->GetId()) menu("scapslab.view."+std::to_string(panel->GetId()),"View/"+panel->GetTitle(),[panel]{panel->SetVisible(!panel->IsVisible());},"",{},[panel]{return panel->IsVisible();});
        WindowId top=0;for(const auto &p:this->panels)top=std::max(top,p->GetId());
        for(WindowId i=1;i<=top;++i) if(auto panel=editor.FindPanel(i).lock();panel && panel->GetTitle()=="Settings") {
            menu("scapslab.preferences","Tools/Preferences...",[panel]{panel->SetVisible(true);});break;
        }
        menu("scapslab.about","Help/About ScapsLab",[this]{about=true;});
        menu("scapslab.lessons","Tutorials/Lessons...",[this]{lessons=true;},"",[&w]{return !w.Busy() && !w.tutorial.Active();});
        for(const auto &lesson:catalog) { // automated self-check, for developers only
            const std::string id=lesson.at("tutorial_id"),title=lesson.at("title");
            menu("scapslab.lesson."+id+".verify","Help/Developer/Verify tutorials/"+title,[&w,id]{w.TutorialRequest({{"operation","tutorial_start"},{"tutorial_id",id},{"mode","verify"}});},"",[&w]{return !w.Busy() && !w.tutorial.Active();});
        }
        operation("tutorial_stop",[&w]{return w.tutorial.Active();});
    }
    // One extra Layers editor per pinned layer; it follows the layer by ID and disappears with it.
    void DrawLayerWindows() {
        layerWindows.BeginFrame();
        for(auto it=w.pinnedLayers.begin();it!=w.pinnedLayers.end();) {
            std::string name;
            for(const auto &d:w.project.devices) if(d->id==it->first && d->Document().is_object() && d->Document().contains("layers"))
                for(const auto &l:d->Document().at("layers")) if(l.value("scapslab_id",std::string{})==it->second) name=l.value("name",std::string("layer"));
            if(name.empty()) {layerPanels.erase(it->second);it=w.pinnedLayers.erase(it);continue;} // device or layer deleted
            auto &panel=layerPanels[it->second];
            if(!panel) {panel=CreateRef<Panel>(&w,1);panel->PinLayer(it->first,it->second);}
            bool open=true;
            ImGui::SetNextWindowSize(ImVec2(420,520),ImGuiCond_FirstUseEver);
            layerWindows.Render("Layer: "+name+"###sst-layer-"+it->second,open,0,[&]{panel->RenderContent();},true);
            if(!open) {layerPanels.erase(it->second);it=w.pinnedLayers.erase(it);continue;}
            ++it;
        }
        layerWindows.EndFrame();
    }
    void OnImGuiRender() override {
        tutorialWindows.BeginFrame();
        if(savedRecent!=w.recent) {
            for(int i=0;i<10;++i) {
                const std::string path=i<int(w.recent.size())?"File/Recent/"+std::to_string(i+1)+" "+std::filesystem::path(w.recent[i].get<std::string>()).filename().string():"";
                (void)w.commands->SetMenu(CommandID{"scapslab.recent."+std::to_string(i)},path,"",[this,i]{return !w.Busy() && i<int(w.recent.size());});
            }
            savedRecent=w.recent;
        }
        if(lessons) {
            ImGui::SetNextWindowSize(ImVec2(520,0),ImGuiCond_Appearing);
            if(ImGui::Begin("Tutorials###sst-lessons",&lessons,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Pick a lesson. ScapsLab highlights each control; you do the step yourself. Lessons add a tutorial device and never replace your project.");
                ImGui::Separator();
                for(const auto &lesson:catalog) {
                    const std::string id=lesson.at("tutorial_id"),title=lesson.at("title");
                    if(!LessonShown(id)) continue;
                    const auto progress=w.tutorialProgress.value(id,Json::object());
                    const auto steps=lesson.at("step_count").get<std::size_t>();
                    ImGui::PushID(id.c_str());
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(title.c_str());
                    ImGui::TextDisabled("%d min, %zu steps%s",lesson.value("minutes",0),steps,progress.value("status",std::string{})=="passed"?", completed":"");
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x-90);
                    ImGui::TextWrapped("%s",lesson.value("description",std::string{}).c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::SameLine(ImGui::GetContentRegionMax().x-70);
                    if(ImGui::Button("Start",ImVec2(70,0)) && !w.Busy() && !w.tutorial.Active()) {
                        (void)w.Dispatch({{"operation","tutorial_start"},{"tutorial_id",id},{"mode","learn"}});lessons=false;
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
            ImGui::End();
        }
        if(const auto *step=w.tutorial.Step()) {
            reportOpen=true;
            const auto key=w.tutorial.Report().at("tutorial_id").get<std::string>()+"/"+step->at("id").get<std::string>();
            if(focusedStep!=key) {
                for(const auto &panel:panels) if(panel->GetType()==step->at("target").at("window").get<std::string>()) {
                    (void)windows->Execute({WindowAction::Activate,panel->GetId()});
                    ImGui::SetWindowCollapsed(panel->ImGuiName().c_str(),false);
                    const auto item=step->at("target").value("item",std::string{});
                    if(item=="qe" || item=="light-iv")w.project.ui["optical_tab"]=item=="qe"?2:1;
                }
                focusedStep=key;
            }
            if(w.tutorialOverlay.Draw(w.tutorial,&tutorialWindows))(void)w.Dispatch({{"operation","tutorial_stop"}});
        } else {
            focusedStep.clear();
            const auto report=w.tutorial.Report();
            if(reportOpen && (report.at("status")=="failed" || report.at("status")=="passed")) {
                tutorialWindows.Render("Tutorial report###sst-tutorial-report",reportOpen,0,[&] {ImGui::TextUnformatted(report.at("status")=="passed"?(report.at("mode")=="learn"?"Lesson complete.":"Tutorial PASS"):"Tutorial FAIL");if(report.at("mode")!="learn")for(const auto &step:report.at("steps")) {ImGui::Text("%s: %s",step.at("step_id").get<std::string>().c_str(),step.at("status").get<std::string>().c_str());if(!step.at("error").is_null())ImGui::TextWrapped("%s",step.at("error").dump().c_str());}},true);
            }
        }
        tutorialWindows.EndFrame();
        DrawLayerWindows();
        if(about) {if(ImGui::Begin("About ScapsLab",&about,ImGuiWindowFlags_AlwaysAutoResize))ImGui::TextUnformatted("ScapsLab\nSemiconductor Studio\nDevice simulation with scaps_core");ImGui::End();}
        if(clear) {if(ImGui::Begin("Clear history",&clear))DrawHistory(w,true);ImGui::End();}
    }
};
}
void InstallTutorialUi(Application &app,Workflow &w,const std::vector<Ref<Panel>> &panels) {
    app.InsertLayerBefore(LayerId::ImGui,CreateUnique<TutorialUi>(app,w,panels));
}
}
