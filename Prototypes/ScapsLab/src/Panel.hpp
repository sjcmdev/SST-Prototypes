#pragma once
#include "Workflow.hpp"
#include "Domain/ParameterBounds.hpp"
#include "Commands/Commands.hpp"
#include "Library/LibraryUi.hpp"
#include <cmath>
#include <algorithm>
#include "Core/Presentation/Panels/EditorWindow.hpp"
namespace SST {class Application;}
namespace SST::ScapsLab {
Bounds DisplayBounds(std::string_view key,double nativeScale=1);
bool DrawBoundValue(const char* id,double& value,std::string_view key,double nativeScale=1,const void* address=nullptr,const char* label="",bool showUnit=false);
void DrawParameterBoundsPreferences(Workflow&);
void DrawHistory(Workflow &workflow,bool global=false);
int PlotCacheBuildCount();   // test hooks (S3): number of plot-cache (re)builds so far, and the sum of all finite cached values
double PlotCacheSum();
void SetPlotDataHook(void (*hook)(bool)); // called with true/false around plot-data cache lookups/drawing (allocation-count test)
void OpenLayerWindow(Workflow *w,const std::string &device,const std::string &layer); // adds an extra pinned "Layer: <name>" editor (drawn by the tutorial/UI layer)
// Basic mode (owner 2026-11-21): only these panels (indices into Panel.cpp names[]) are created/docked/listed in View. The rest stay in code.
inline constexpr int kBasicPanels[]={0,1,2,3,4,5,10,11,12,13,16,17,18,22,23,24,25}; // hidden panels remain available in experimental mode
inline bool &ExperimentalPanels() {static bool on=false;return on;} // View > Show experimental panels; persisted in app settings, applied at start
inline std::string &UiSettingsPath() {static std::string path;return path;}
inline bool PanelShown(int view) {if(ExperimentalPanels())return true;for(const int p:kBasicPanels)if(p==view)return true;return false;}
// Slider maths (UI-free, unit-tested). Position t in [0,1]: linear, or logarithmic (decades) for log sliders.
struct SliderMap {
    double lo=0,hi=1;bool log=false;double step=0;
    double ToT(double v) const {const double t=log?(std::log(std::max(v,lo))-std::log(lo))/(std::log(hi)-std::log(lo)):(v-lo)/(hi-lo);return std::clamp(t,0.,1.);}
    double FromT(double t) const {t=std::clamp(t,0.,1.);return log?lo*std::exp(t*(std::log(hi)-std::log(lo))):lo+t*(hi-lo);}
    double Step(double v,double steps) const {return log?std::clamp(v*std::pow(10.,.01*steps),lo,hi):std::clamp(v+steps*(step>0?step:.01*(hi-lo)),lo,hi);} // arrow/wheel: 1% of range, log: 0.01 decade
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
struct SweepControlState {
    SweepEditor editor;
    std::string customText,error,parameterKey;
    bool showUnit=false;
};
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
    Library::Editor libraryEditor;
    int view;
    std::string pinDevice,pinLayer;
    std::string contactStatus;
    unsigned contactTakeoverId=0;
    std::string contactTakeoverSide;
    bool absoluteLog=false, placed=false, gallery=false;
    char file[1024]{}, csvFile[1024]="scapslab-ac.csv";
    bool RunActions(ProjectDraft&);
    int selectedAction=1;
    WeakRef<const RunPlan> actionSource;
    std::array<SweepControlState,kActionCount> actionEditors;
    SweepControlState dlcpAmplitudes;
    WeakRef<const RunPlan> dlcpSource;
    WeakRef<const AcResult> dlcpPlotSource;
    Ref<const DlcpProfilePoint> dlcpLastLive;
    std::string dlcpDevice;
    std::vector<Ref<const DlcpProfilePoint>> dlcpRows;
    bool DlcpControls(RunAction&,const ProjectDraft&);
    void DLCP(Ref<const AcResult> stored={});
    int dlcpSelected=0;
    bool DltsControls(RunAction&,const ProjectDraft&);
    void DLTS(Ref<const AcResult> stored={});
    int dltsAction=6,dltsSelected=0;
    void AC();
    bool Illumination(ProjectDraft &draft);
    bool Absorption(ProjectDraft &draft);
    bool OpticalFile(ProjectDraft&,std::string& file,scaps_core::OpticalTable& table,const char* extension,const char* label);
    bool Grading(ProjectDraft &draft);
    bool ProfileEditor(scaps_core::Profile&,double thickness_m,const std::string& parameter,const scaps_core::Profile* composition=nullptr,bool diffusion=false);
    std::string gradingParameter="Eg_eV";
    void Batch();
    void Recorder();
    void BatchPlots();
    void ResultsBrowser();
    int resultAction=1,resultTab=0;
    WeakRef<const BatchResult> batchPlotSource;
    std::vector<BatchCurve> batchPlotCurves;
    char parameterSearch[128]{};
    WeakRef<const BatchDraft> sweepSource;
    std::vector<SweepControlState> sweepEditors;
    int recorderPlot=0, opticalTab=-1, grTab=-1;
    bool Defects(ProjectDraft &draft,bool interface);
    bool Structure(ProjectDraft &draft);
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
