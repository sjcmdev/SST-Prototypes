#pragma once
#include "Workflow.hpp"
#include "Core/Presentation/Panels/EditorWindow.hpp"
namespace SST {class Application;}
namespace SST::ScapsLab {
void DrawHistory(Workflow &workflow,bool global=false);
class Panel final : public EditorWindow {
public:
    Panel(Workflow *workflow, int view, bool gallery=false);
    void RenderContent() override;
    bool IsDirty() const override {return view==0 && w->dirty;}
    Workflow *GetWorkflow() const {return w;}
    void SelectGradingParameter(const std::string &key) {gradingParameter=key;} // what the user does before a Grading step
private:
    Workflow *w;
    int view;
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
    void Preview(bool interface);
    int selectedDefect=0;
    void Send(const Json &request);
    void Results();
    void GenerationRecombination();
    bool grFollow=true;
    Json grBias={{"voltage_V",0.0}};
    std::set<std::string> grHidden;
    void Devices();
    void Compare();
};
void InstallTutorialUi(Application &app,Workflow &w,const std::vector<Ref<Panel>> &panels);
}

