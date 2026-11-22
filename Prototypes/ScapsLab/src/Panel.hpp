#pragma once
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include <cmath>
#include <algorithm>
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
// Slider maths (UI-free, unit-tested). Position t in [0,1]: linear, or logarithmic (decades) for log sliders.
struct SliderMap {
    double lo=0,hi=1;bool log=false;
    double ToT(double v) const {const double t=log?(std::log(std::max(v,lo))-std::log(lo))/(std::log(hi)-std::log(lo)):(v-lo)/(hi-lo);return std::clamp(t,0.,1.);}
    double FromT(double t) const {t=std::clamp(t,0.,1.);return log?lo*std::exp(t*(std::log(hi)-std::log(lo))):lo+t*(hi-lo);}
    double Step(double v,double steps) const {return log?std::clamp(v*std::pow(10.,.01*steps),lo,hi):std::clamp(v+.01*steps*(hi-lo),lo,hi);} // arrow/wheel: 1% of range, log: 0.01 decade
};
inline double MantissaExponentValue(double m,double e,const SliderMap &r) {return std::clamp(m*std::pow(10.,e),r.lo,r.hi);}
inline void SplitMantissaExponent(double v,double &m,int &e) {e=int(std::floor(std::log10(v)));m=v/std::pow(10.,e);if(m>=10){m/=10;++e;}else if(m<1){m*=10;--e;}}
// One slider drag. Native (ImGui) value until a fine modifier is used; then relative to the cursor and the value at the moment the speed changed (no jump).
struct SliderDrag {
    double start=0,anchorT=0,scale=1;float anchorX=0;bool relative=false;
    void Begin(double value) {start=value;relative=false;scale=1;}
    double Update(const SliderMap &m,double native,double before,float mouseX,float widthPx,double wantedScale) {
        if(!relative && wantedScale==1) return native;
        if(!relative || wantedScale!=scale) {relative=true;scale=wantedScale;anchorT=m.ToT(before);anchorX=mouseX;return before;}
        return m.FromT(anchorT+double(mouseX-anchorX)*scale/std::max(1.f,widthPx));
    }
};
inline double SliderScale(bool shift,bool ctrl,bool lock) {return shift&&ctrl?.01:(shift||lock)?.1:1.;} // Shift = 10x slower, Ctrl+Shift = 100x, F toggle = Shift
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
    WeakRef<const BatchResult> batchPlotSource;
    std::vector<BatchCurve> batchPlotCurves;
    char parameterSearch[128]{};
    int recorderPlot=0, opticalTab=-1, grTab=-1;
    bool Defects(Json &draft,bool interface);
    bool Structure(Json &draft);
    void StructureEdit();
    void Preview(bool interface);
    int selectedDefect=0;
    void Report(const Json &response);                                  // failed command: status + response
    template<class T> void Do(const T &command) {Report(w->Exec(command));} // typed command (Commands/Commands.hpp)
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

