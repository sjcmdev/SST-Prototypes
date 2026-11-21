#pragma once
#include "Workflow.hpp"
#include "Core/Presentation/Panels/EditorWindow.hpp"
namespace SST {class Application;}
namespace SST::ScapsLab {
void DrawHistory(Workflow &workflow,bool global=false);
int PlotCacheBuildCount();   // test hooks (S3): number of plot-cache (re)builds so far, and the sum of all finite cached values
double PlotCacheSum();
void SetPlotDataHook(void (*hook)(bool)); // called with true/false around plot-data cache lookups/drawing (allocation-count test)
void OpenLayerWindow(Workflow *w,const std::string &device,const std::string &layer); // adds an extra pinned "Layer: <name>" editor (drawn by the tutorial/UI layer)
// Basic mode (owner 2026-11-21): only these panels (indices into Panel.cpp names[]) are created/docked/listed in View. The rest stay in code.
inline constexpr int kBasicPanels[]={0,1,2,3,4,5,10,11,12,13,16,17,18,22}; // all except Structure (14; strip lives in Band diagram), C-V, Mott-Schottky, C-f, G-omega-f, History, Illumination, Absorption, Grading
inline bool &ExperimentalPanels() {static bool on=false;return on;} // View > Show experimental panels; persisted in app settings, applied at start
inline std::string &UiSettingsPath() {static std::string path;return path;}
inline bool PanelShown(int view) {if(ExperimentalPanels())return true;for(const int p:kBasicPanels)if(p==view)return true;return false;}
class Panel final : public EditorWindow {
public:
    Panel(Workflow *workflow, int view, bool gallery=false);
    void RenderContent() override;
    bool IsDirty() const override {return view==0 && w->dirty;}
    Workflow *GetWorkflow() const {return w;}
    void PinLayer(const std::string &device,const std::string &layer) {pinDevice=device;pinLayer=layer;} // extra Layers window: edits this layer only
    void SelectGradingParameter(const std::string &key) {gradingParameter=key;} // what the user does before a Grading step
private:
    Workflow *w;
    int view;
    std::string pinDevice,pinLayer;
    bool absoluteLog=false, placed=false, gallery=false;
    char file[1024]{}, csvFile[1024]="scapslab-ac.csv";
    Json acRequest;
    void AC();
    bool Illumination(Json &draft);
    bool Absorption(Json &draft);
    bool OpticalFile(Json &object,const char *file,const char *table,const char *extension,const char *label);
    bool Grading(Json &draft);
    bool ProfileEditor(Json &profile,double thickness_m,const std::string &parameter,const Json &composition=Json::object(),bool diffusion=false);
    Json qeRequest={{"operation","qe"},{"start_nm",300.},{"stop_nm",4100.},{"points",61},{"probe_mode","constant_flux"},{"probe_photon_flux_m2_s",1e16},{"probe_power_W_m2",1e-3}};
    std::string gradingParameter="Eg_eV";
    void Batch();
    void Recorder();
    void BatchPlots();
    char parameterSearch[128]{};
    int recorderPlot=0, opticalTab=-1, grTab=-1;
    bool Defects(Json &draft,bool interface);
    bool Structure(Json &draft);
    void StructureEdit();
    void Preview(bool interface);
    int selectedDefect=0;
    void Send(const Json &request);
    void Results();
    void GenerationRecombination();
    bool grFollow=true;
    Json grBias={{"voltage_V",0.0}};
    std::set<std::string> grHidden;
    void Devices();
    void AddDefectTo(bool sheet,int layer);
    void Compare();
};
void InstallTutorialUi(Application &app,Workflow &w,const std::vector<Ref<Panel>> &panels);
}

