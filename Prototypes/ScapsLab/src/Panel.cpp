#include "Panel.hpp"
#include <imgui.h>
#include <scaps_core/grading.hpp>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <implot.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <tuple>
#include <imgui_internal.h>
#include "Core/Platform/NativeFileDialog.hpp"
namespace SST::ScapsLab {
static const char *names[]={"Project","Layers","Contacts","Solver","Run","Results","C-V","Mott-Schottky","C-f","G-omega-f","I-V","Defects","Interfaces","Rs-Rsh","Structure","Devices","Batch","Recorder","Batch overlay","Illumination","Absorption","Grading","Generation-Recombination"};
Panel::Panel(Workflow *w,int v,bool gallery):EditorWindow(std::string("scapslab.")+names[v],names[v],true),w(w),view(v),gallery(gallery) {
    auto s=GetState(); s.x=gallery?25.f:10.f+float(v%3)*415.f; s.y=gallery?50.f:45.f+float(v/3)*335.f;
    if(gallery) s.mode=WindowMode::Maximized;
    s.width=gallery?1100.f:405.f; s.height=gallery?650.f:325.f; ApplyState(s);
    acRequest={{"operation",v<8?"cv":"cf"},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
        {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->Current().document.value("temperature_K",300.0)}};
    absoluteLog=w->project.ui.value("iv_absolute_log",true);
    if(w->Current().acResults.contains("qe") && w->Current().acResults["qe"].contains("request")) qeRequest.update(w->Current().acResults["qe"]["request"],true);
    std::snprintf(file,sizeof(file),"%s",w->path.c_str());
}
void Panel::Send(const Json &r) { const auto response=w->Dispatch(r); if(!response.at("ok").get<bool>()) {w->status=response.at("error").at("message");w->response=response;} }
namespace {
void BeginRow(const char *label) {ImGui::PushID(label);sst::ui::widgets::BeginLabelledRow(label);}
void EndRow() {sst::ui::widgets::EndLabelledRow();ImGui::PopID();}
std::uint64_t gesture=0;
void TrackGesture() {if(ImGui::IsItemActivated()) ++gesture;}
bool Toggle(const char *label,bool &value) {BeginRow(label);const bool changed=ImGui::Checkbox("##value",&value);TrackGesture();EndRow();return changed;}
void TextInput(const char *label,char *text,std::size_t size) {
    BeginRow(label);ImGui::InputText("##value",text,size,ImGuiInputTextFlags_AutoSelectAll);EndRow();
}
}
static bool Number(const char *label,Json &j,const char *key,double scale=1) {
    const auto &n=j.at(key); if(!n.is_number()) return false;
    const std::string k=key; double value=n.get<double>()*scale;
    const bool logarithmic=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3" || k=="Sn_m_s" || k=="Sp_m_s" ||
        k=="residual_tolerance" || k=="adaptive_target_relative_J_error" || k=="frequency_Hz" || k=="start_Hz" || k=="stop_Hz";
    BeginRow(label); bool changed=false;
    if(logarithmic || k=="Nt_m3" || k=="Nt_m2" || k=="peak_density_m2_eV" || k.find("sigma_")!=std::string::npos || k=="area_m2" || k=="series_ohm_m2" || k=="shunt_ohm_m2" || k=="probe_photon_flux_m2_s" || k=="probe_power_W_m2" || k.find("_m1")!=std::string::npos || k=="intensity_multiplier") {
        const bool density=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3";
        const bool zeroAllowed=k=="Nd_m3" || k=="Na_m3" || k=="Sn_m_s" || k=="Sp_m_s" || k=="series_ohm_m2" || k=="Nt_m3" || k=="Nt_m2" || k=="peak_density_m2_eV" || k.find("sigma_")!=std::string::npos || k.find("_m1")!=std::string::npos || k=="intensity_multiplier";
        bool zero=value==0;
        if(zeroAllowed) {ImGui::TextUnformatted("Zero");ImGui::SameLine();changed=ImGui::Checkbox("##zero",&zero);TrackGesture();ImGui::SameLine();}
        int exponent=value>0?int(std::floor(std::log10(value))):(density?14:0);
        float mantissa=value>0?float(value/std::pow(10.,exponent)):1.f;
        ImGui::BeginDisabled(zero);
        ImGui::SetNextItemWidth(std::max(40.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.64f));
        changed|=ImGui::SliderFloat("##mantissa",&mantissa,1.f,9.99f,"mantissa %.2f");TrackGesture();if(ImGui::IsItemHovered()) ImGui::SetTooltip("Mantissa: Ctrl+click for exact typing.");ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);changed|=ImGui::SliderInt("##exponent",&exponent,density?0:-30,density?23:20,"10^%d");TrackGesture();
        ImGui::EndDisabled();
        if(zero) ImGui::TextWrapped("Clear Zero to edit.");
        if(changed) value=zero?0:double(mantissa)*std::pow(10.,exponent);
    } else if(k=="points_per_layer" || k=="points" || k=="max_newton_iterations") {
        int v=int(value);const int minimum=k=="points_per_layer"?3:k=="points"?2:1;
        ImGui::SetNextItemWidth(-1);
        changed=ImGui::SliderInt("##value",&v,minimum,k=="max_newton_iterations"?1000:2000,"%d");TrackGesture();value=v;
    } else {
        double lo=0.0,hi=10.0;
        if(k=="thickness_m") {lo=0.01;hi=50;}
        else if(k=="eps_r") {lo=1;hi=40;}
        else if(k=="Eg_eV") {lo=0.1;hi=5;}
        else if(k=="chi_eV" || k=="phi_m_eV") {lo=1;hi=7;}
        else if(k=="mu_n_m2_V_s" || k=="mu_p_m2_V_s") {lo=0.1;hi=5000;}
        else if(k=="temperature_K") {lo=100;hi=600;}
        else if(k=="step_V") {lo=0.001;hi=0.2;}
        else if(k=="wavelength_nm" || k=="start_nm" || k=="stop_nm") {lo=100;hi=5000;}
        else if(k=="monochromatic_power_W_m2") {lo=0;hi=2000;}
        else if(k=="urbach_energy_eV") {lo=.001;hi=.2;}
        else if(k=="front_transmission" || k=="back_reflection" || k=="front_internal_reflection") {lo=0;hi=1;}
        else if(k=="characteristic_energy_eV") {lo=0.001;hi=1;}
        else if(k=="gaussian_width" || k=="tail_width") {lo=1;hi=20;}
        else {lo=-5;hi=5;}
        ImGui::SetNextItemWidth(-1);
        changed=ImGui::SliderScalar("##value",ImGuiDataType_Double,&value,&lo,&hi,"%.4g");TrackGesture();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+click to enter an exact value. Values are validated before applying.");
    EndRow();
    if(changed) j[key]=(k=="points_per_layer" || k=="points" || k=="max_newton_iterations")?Json(std::int64_t(value/scale)):Json(value/scale);
    return changed;
}
static bool Choice(const char *label,Json &j,const char *key,std::initializer_list<std::pair<const char*,const char*>> items) {
    const auto current=j.at(key).get<std::string>(); bool changed=false;
    const char *preview=current.c_str(); for(const auto &[name,value]:items) if(current==value) preview=name;
    BeginRow(label);
    if(ImGui::BeginCombo("##value",preview)) {for(const auto &[name,value]:items) if(ImGui::Selectable(name,current==value)) {j[key]=value;changed=true;++gesture;} ImGui::EndCombo();} EndRow();return changed;
}
void Panel::RenderContent() {
    if(!placed && !gallery) {auto s=GetState();const auto *vp=ImGui::GetMainViewport();s.x+=vp->Pos.x;s.y+=vp->Pos.y;ApplyState(s);placed=true;}
    ImGui::TextDisabled("SCAPSLAB / %s",names[view]);
    if(view==4 || view==5 || (w->response.is_object() && !w->response.value("ok",true))) ImGui::TextWrapped("%s",w->status.c_str()); ImGui::Separator();
    if(w->Current().document.contains("scaps_native")) for(const auto &warning:w->Current().document.at("scaps_native").value("warnings",Json::array())) ImGui::TextWrapped("SCAPS import warning: %s",warning.get<std::string>().c_str());
    if(view==16) {Batch();return;}
    if(view==17) {Recorder();return;}
    if(view==18) {BatchPlots();return;}
    if(view==15) {Devices();return;}
    if(view==10) {Compare();return;}
    if(view==5) {Results();return;}
    if(view==22) {GenerationRecombination();return;}
    if(view>=6 && view<10) {AC();return;}
    const bool replacing=w->pending && (w->pending->request.at("operation")=="project_load" || w->pending->request.at("operation")=="load" || w->pending->request.at("operation")=="layer");
    ImGui::BeginDisabled(replacing);
    if(view==0) {
        char name[256];std::snprintf(name,sizeof(name),"%s",w->project.name.c_str());
        BeginRow("Project name");if(ImGui::InputText("##name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) Send({{"operation","project_edit"},{"name",name}});EndRow();
        ImGui::TextWrapped("%s",w->path.c_str());
        if(w->dirty) ImGui::TextColored(ImVec4(1,.7f,.3f,1),"Unsaved changes *");
        if(ImGui::Button("Open")) w->fileAction="open";ImGui::SameLine();
        if(ImGui::Button("Save")) w->fileAction="save";ImGui::SameLine();
        if(ImGui::Button("Save As")) w->fileAction="save_as";
        ImGui::TextWrapped("%s",w->status.c_str());
        if(ImGui::Button("Undo")) Send({{"operation","undo"}});ImGui::SameLine();
        if(ImGui::Button("Redo")) Send({{"operation","redo"}});
        ImGui::Text("%d devices | active: %s",int(w->project.devices.size()),w->Current().name.c_str());
        if(!w->Current().document.is_null()) {
            ImGui::Text("Case: %s",w->Current().document.at("case_id").get<std::string>().c_str());
            ImGui::Text("Layers: %d",int(w->Current().document.at("layers").size()));
            ImGui::TextWrapped("Full YAML mapping is retained, including defects, interfaces, illumination and external elements. Layer IDs persist in scapslab_id.");
            ImGui::TextDisabled("Ctrl+click sliders for exact values. Ctrl+A selects all text.");
        }
    } else if(w->Current().document.is_object()) {
        auto draft=w->Current().document;
        if(!w->edits.empty()) draft=w->edits.back().at("document");
        else if(w->pending && w->pendingDevice==w->project.active_device && w->pending->request.value("operation",std::string{})=="edit" && w->pending->request.contains("document")) draft=w->pending->request.at("document");
        bool changed=false;
        if(view==19) changed|=Illumination(draft);
        else if(view==20) changed|=Absorption(draft);
        else if(view==21) changed|=Grading(draft);
        else if(view==11 || view==12) {
            const bool wide=ImGui::GetContentRegionAvail().x>750;
            if(wide) {ImGui::BeginTable("Editor and preview",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            changed|=Defects(draft,view==12);
            if(wide) ImGui::TableNextColumn();
            Preview(view==12);
            if(wide) ImGui::EndTable();
        }
        else if(view==13) {
            auto &e=draft["external"];
            ImGui::TextWrapped("R_area = R x area. The solver stores Ohm m^2; controls show Ohm cm^2. Rs = 0 is allowed.");
            changed|=Number("Device area [cm^2]",e,"area_m2",1e4);
            changed|=Number("Rs area [Ohm cm^2]",e,"series_ohm_m2",1e4);
            bool open=e["shunt_ohm_m2"].is_string();
            if(Toggle("Rsh infinite (open)",open)) {e["shunt_ohm_m2"]=open?Json(".inf"):Json(1.0);changed=true;}
            if(!open) changed|=Number("Rsh area [Ohm cm^2]",e,"shunt_ohm_m2",1e4);
            const double area=e.at("area_m2");
            ImGui::Text("Rs = %.6g Ohm",e.at("series_ohm_m2").get<double>()/area);
            if(open) ImGui::TextUnformatted("Rsh = infinite Ohm");else ImGui::Text("Rsh = %.6g Ohm",e.at("shunt_ohm_m2").get<double>()/area);
        } else if(view==14) changed|=Structure(draft);
        else if(view==1) {
            auto &layers=draft["layers"]; auto &selected=w->Current().selectedLayer; selected=std::clamp(selected,0,int(layers.size())-1);
            BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {
                for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i); if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),selected==i)) selected=i; ImGui::PopID();} ImGui::EndCombo();
            }
            EndRow();
            auto &l=layers[selected]; const auto id=l.at("scapslab_id");
            if(ImGui::Button("Add")) Send({{"operation","layer"},{"action","add"}}); ImGui::SameLine();
            if(ImGui::Button("Remove")) Send({{"operation","layer"},{"action","remove"},{"layer_id",id}}); ImGui::SameLine();
            if(ImGui::Button("Up") && selected>0) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected-1}}); ImGui::SameLine();
            if(ImGui::Button("Down") && selected+1<int(layers.size())) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected+1}});
            ImGui::TextDisabled("Stable ID: %s",id.get<std::string>().c_str());
            char name[256]; std::snprintf(name,sizeof(name),"%s",l.at("name").get<std::string>().c_str());
            BeginRow("Name");if(ImGui::InputText("##name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {l["name"]=name;changed=true;++gesture;}EndRow();
            changed|=Number("Thickness [µm]",l,"thickness_m",1e6);
            auto &m=l["material"];
            changed|=Number("Relative permittivity",m,"eps_r"); changed|=Number("Band gap [eV]",m,"Eg_eV"); changed|=Number("Affinity [eV]",m,"chi_eV");
            changed|=Number("Nc [cm^-3]",m,"Nc_ref_m3",1e-6); changed|=Number("Nv [cm^-3]",m,"Nv_ref_m3",1e-6);
            changed|=Number("Electron mobility [cm^2/(V s)]",m,"mu_n_m2_V_s",1e4); changed|=Number("Hole mobility [cm^2/(V s)]",m,"mu_p_m2_V_s",1e4);
            changed|=Number("Donors [cm^-3]",l["doping"],"Nd_m3",1e-6); changed|=Number("Acceptors [cm^-3]",l["doping"],"Na_m3",1e-6);
        } else if(view==2) {
            for(const auto *side:{"left","right"}) {
                ImGui::PushID(side); ImGui::SeparatorText(side); auto &c=draft["contacts"][side];
                const bool flat=c.value("flat_band",false);
                ImGui::BeginDisabled(flat);changed|=Number("Work function [eV]",c,"phi_m_eV");ImGui::EndDisabled();
                if(flat) ImGui::TextWrapped("Work function is derived from the adjacent material in Flat band mode. Select Work function to edit it.");
                int mode=c.contains("flat_band")?(c["flat_band"].get<bool>()?2:1):0;
                BeginRow("Boundary mode");if(ImGui::Combo("##mode",&mode,"Legacy default\0Work function\0Flat band\0")) {if(mode==0)c.erase("flat_band");else c["flat_band"]=mode==2;changed=true;++gesture;}EndRow();
                for(const auto *key:{"Sn_m_s","Sp_m_s"}) {
                    ImGui::PushID(key); bool infinite=c[key].is_string();
                    if(Toggle(key==std::string("Sn_m_s")?"Infinite Sn [m/s]":"Infinite Sp [m/s]",infinite)) {c[key]=infinite?Json(".inf"):Json(1e5);changed=true;}
                    
                    if(!infinite) changed|=Number("Surface velocity [m/s]",c,key);
                    else ImGui::TextDisabled("S is infinite: clear the toggle to edit velocity [m/s]."); ImGui::PopID();
                }
                ImGui::PopID();
            }
            const auto &computed=w->Current().bandResult.empty()?w->Current().result:w->Current().bandResult;
            if(computed.contains("contact_phi_eV") && ImGui::CollapsingHeader("Contact view",ImGuiTreeNodeFlags_DefaultOpen)) {
                double total=0;for(const auto &layer:draft.at("layers")) total+=layer.at("thickness_m").get<double>()*1e6;
                const double x[]={0,total},phi[]={computed.at("contact_phi_eV")[0],computed.at("contact_phi_eV")[1]};
                ImGui::Text("Effective phi: left %.3f eV | right %.3f eV",phi[0],phi[1]);
                if(ImPlot::BeginPlot("Contact work functions",ImVec2(-1,180))) {
                    ImPlot::SetupAxes("Position [um]","phi [eV]");
                    ImPlot::PlotLine("Contacts",x,phi,2,{ImPlotProp_Marker,ImPlotMarker_Square,ImPlotProp_MarkerSize,6.f});ImPlot::EndPlot();
                }
            }
        } else if(view==3) {
            changed|=Choice("Constants",draft,"constants_mode",{{"CODATA","codata"},{"SCAPS compatible","scaps_compat"}});
            changed|=Choice("Mobility µ(T)",draft,"mobility_temperature_model",{{"Constant","constant"},{"SqrtT","sqrt_T"},{"Arora (Caughey-Thomas T)","caughey_thomas_T"}});
            ImGui::TextWrapped("Arora uses the sourced caughey_thomas material parameters from the project; missing parameters are reported by the core.");
            auto &mesh=draft["mesh"];
            changed|=Choice("Mesh level",mesh,"adaptivity_level",{{"0","0"},{"1","1"},{"2","2"},{"3","3"},{"A","A"}});
            changed|=Number("Target relative J error",mesh,"adaptive_target_relative_J_error");
            changed|=Number("Points per layer",mesh,"points_per_layer");
            changed|=Number("Newton residual tolerance",draft["solver"],"residual_tolerance");
            changed|=Number("Newton iteration limit",draft["solver"],"max_newton_iterations");
        } else if(view==4) {
            changed|=Number("Start [V]",draft["voltage"],"start_V"); changed|=Number("Stop [V]",draft["voltage"],"stop_V");
            changed|=Number("Step [V]",draft["voltage"],"step_V"); changed|=Number("T [K]",draft,"temperature_K");
            ImGui::TextWrapped("Applied voltage: left minus right. Signed J follows the scaps_core terminal-current convention.");
            bool automatic=w->autoRecalculate;
            if(Toggle("Auto I-V",automatic)) Send({{"operation","auto_recalculate"},{"enabled",automatic}});
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::Button("Calculate I-V")) Send({{"operation","run"}});
            ImGui::EndDisabled();
            bool bandAutomatic=w->autoRecalculateBand;
            if(Toggle("Auto band",bandAutomatic)) Send({{"operation","auto_recalculate"},{"enabled",w->autoRecalculate},{"band_enabled",bandAutomatic}});
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::Button("Recalculate band")) Send({{"operation","band"},{"voltage_V",w->Current().biasVoltage_V}});
            ImGui::EndDisabled();
            if(w->Busy()) ImGui::TextDisabled("Calculation active: wait or Cancel. Parameter edits cancel an outdated sweep.");
        }
        if(changed) Send({{"operation","edit"},{"document",draft},{"gesture_id",std::to_string(gesture)}});
    }
    ImGui::EndDisabled();
    if(replacing) ImGui::TextWrapped("Project structure is changing; editing resumes when the operation finishes.");
    if(view==4 && w->active) {
        const auto j=w->jobs->GetJob(w->active); if(j) {ImGui::ProgressBar(j->totalWork>0?j->completedWork/j->totalWork:0);ImGui::TextWrapped("%s",j->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
        ImGui::TextWrapped("Cancellation takes effect after mesh preparation or the current Newton solve.");
    }
}
namespace {
bool FileField(const char *label,Json &j,const char *key) {
    char text[2048];std::snprintf(text,sizeof(text),"%s",j.value(key,std::string{}).c_str());
    BeginRow(label);const bool changed=ImGui::InputText("##path",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);EndRow();
    if(changed) {j[key]=text;++gesture;}return changed;
}
void PlotJson(const char *title,const char *xLabel,const char *yLabel,const Json &x,const Json &y,bool log=false,float height=190) {
    auto xs=x.get<std::vector<double>>(),ys=y.get<std::vector<double>>();
    if(log) for(auto &v:ys) if(v<=0) v=NAN;
    if(ImPlot::BeginPlot(title,ImVec2(-1,height))) {ImPlot::SetupAxes(xLabel,yLabel,ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);if(log) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);ImPlot::PlotLine(title,xs.data(),ys.data(),int(xs.size()));ImPlot::EndPlot();}
}
}
bool Panel::Illumination(Json &draft) {
    bool changed=false;auto &light=draft["illumination"];
    if(!w->Busy() && w->Current().actions.contains("qe"))qeRequest.update(w->Current().actions.at("qe"),true);
    const auto previousQe=qeRequest;
    bool enabled=light.value("enabled",false);if(Toggle("Light enabled",enabled)) {light["enabled"]=enabled;changed=true;}
    const char *galleryTab=gallery?std::getenv("SCAPSLAB_OPTICAL_TAB"):nullptr;
    auto tabFlags=[&](int tab){return galleryTab && std::atoi(galleryTab)==tab?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;};
    if(ImGui::BeginTabBar("Optical views")) {
        if(ImGui::BeginTabItem("Spectrum / G(x)",nullptr,tabFlags(0))) {
            const bool wide=ImGui::GetContentRegionAvail().x>750;
            if(wide) {ImGui::BeginTable("Spectrum and generation",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            int source=light.value("mode",std::string{})=="monochromatic"?2:light.value("scapslab_spectrum",std::string{})=="AM1.5G"?0:1;
            BeginRow("Spectrum");if(ImGui::Combo("##source",&source,"AM1.5G\0File\0Monochromatic\0")) {
                std::string spectrumPath;bool apply=true;
                if(source==1) {try {const auto path=OpenFileDialog({"SCAPS spectrum","spe"});if(path)spectrumPath=path->Utf8();else apply=false;}catch(const std::exception &e){w->status=e.what();apply=false;}}
                if(apply) {light["mode"]=source==2?"monochromatic":"spectrum";light["scapslab_spectrum"]=source==0?"AM1.5G":"custom";light["spectrum_file"]="";light["spectrum_nm_W_m2"]=source==0?AM15Spectrum():Json::array();if(source==1)light["spectrum_file"]=spectrumPath;changed=true;++gesture;}
            }EndRow();
            if(light.value("mode",std::string{})!="spectrum" && light.value("mode",std::string{})!="monochromatic") ImGui::TextWrapped("Imported mode: %s. Select an optical source to replace it.",light.at("mode").get<std::string>().c_str());
            if(source==1) {if(FileField("Spectrum file [.spe]",light,"spectrum_file")) {light["mode"]="spectrum";light["scapslab_spectrum"]="custom";light["spectrum_nm_W_m2"]=Json::array();changed=true;}ImGui::TextWrapped("SCAPS spectrum: wavelength [nm], power per bin [W/m^2]. Press Enter to apply paths.");}
            if(source==2) {changed|=Number("Wavelength [nm]",light,"wavelength_nm");changed|=Number("Power [W/m^2]",light,"monochromatic_power_W_m2");}
            changed|=Number("Intensity multiplier",light,"intensity_multiplier");
            bool right=light.value("from_right",false);if(Toggle("Illuminate from right",right)) {light["from_right"]=right;changed=true;}
            changed|=Number("Front transmission",light,"front_transmission");changed|=Number("Back reflection",light,"back_reflection");changed|=Number("Internal reflection",light,"front_internal_reflection");
            changed|=FileField("Front filter file",light,"front_filter_file");changed|=FileField("Back filter file",light,"back_filter_file");
            if(wide) ImGui::TableNextColumn();
            const auto &r=w->Current().bandResult.contains("optics")?w->Current().bandResult:w->Current().result;
            if(r.contains("optics")) {const auto &o=r.at("optics");ImGui::Text("Incident power %.6g W/m^2",o.at("incident_power_W_m2").get<double>());PlotJson("G(x)","x [m]","G [m^-3 s^-1]",o.at("x_m"),o.at("generation_m3_s"),false,330);}
            else ImGui::TextWrapped("Generation preview updates after automatic band calculation.");
            if(wide) ImGui::EndTable();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Light I-V",nullptr,tabFlags(1))) {
            ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Calculate light I-V")) Send({{"operation","run"}});ImGui::EndDisabled();
            const auto &r=w->Current().result;
            if(r.contains("photovoltaic")) {const auto &pv=r.at("photovoltaic");ImGui::TextWrapped("PV: %s",pv.at("status").get<std::string>().c_str());if(!pv.value("message",std::string{}).empty()) ImGui::TextWrapped("%s",pv.at("message").get<std::string>().c_str());
                if(ImGui::BeginTable("PV metrics",2,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    for(const auto &[key,label,scale]:{std::tuple{"Voc_V","Voc [V]",1.},std::tuple{"Jsc_A_m2","Jsc [mA/cm^2]",.1},std::tuple{"FF_fraction","FF [%]",100.},std::tuple{"efficiency_fraction","Efficiency [%]",100.}}) {
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();
                        if(pv.at(key).is_number())ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",pv.at(key).get<double>()*scale);else ImGui::TextUnformatted("unavailable");
                    }ImGui::EndTable();
                }
            }
            if(r.contains("points")) {
                const bool reset=ImGui::Button("Reset PV view");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Default range: -1.2 Jsc to +Jsc. Scroll to zoom; drag to pan.");
                std::vector<double>x,y;for(const auto &p:r.at("points")) {x.push_back(p.at("voltage_V"));y.push_back(p.value("valid",false) && p.at("J_A_m2").is_number()?p.at("J_A_m2").get<double>()*.1:NAN);}
                const auto pv=r.value("photovoltaic",Json::object());
                const bool valid=pv.value("status",std::string{})=="converged";
                const double jsc=valid && pv.at("Jsc_A_m2").is_number()?std::abs(pv.at("Jsc_A_m2").get<double>())*.1:0.;
                if(ImPlot::BeginPlot("Light I-V",ImVec2(-1,std::max(280.f,ImGui::GetContentRegionAvail().y-30)))) {
                    ImPlot::SetupAxes("Voltage [V]","J [mA/cm^2]");ImPlot::SetupLegend(ImPlotLocation_NorthWest,ImPlotLegendFlags_Horizontal);
                    if(x.size()>1 && x.front()!=x.back())ImPlot::SetupAxisLimits(ImAxis_X1,*std::min_element(x.begin(),x.end()),*std::max_element(x.begin(),x.end()),reset?ImGuiCond_Always:ImGuiCond_Once);
                    if(jsc>0)ImPlot::SetupAxisLimits(ImAxis_Y1,-1.2*jsc,jsc,reset?ImGuiCond_Always:ImGuiCond_Once);
                    ImPlot::PlotLine("Light I-V",x.data(),y.data(),int(x.size()));
                    if(valid) {
                        const double zero=0.,shortCircuit=-jsc;
                        if(pv.at("Voc_V").is_number()) {const double voc=pv.at("Voc_V");ImPlot::PlotScatter("Voc",&voc,&zero,1);}
                        if(jsc>0)ImPlot::PlotScatter("Jsc",&zero,&shortCircuit,1);
                        if(pv.contains("Vmpp_V") && pv.at("Vmpp_V").is_number() && pv.at("Jmpp_A_m2").is_number()) {const double v=pv.at("Vmpp_V"),j=pv.at("Jmpp_A_m2").get<double>()*.1;ImPlot::PlotScatter("MPP",&v,&j,1);}
                    }ImPlot::EndPlot();
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("QE(lambda)",nullptr,tabFlags(2))) {
            const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("QE controls and plot",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            Number("Start wavelength [nm]",qeRequest,"start_nm");Number("Stop wavelength [nm]",qeRequest,"stop_nm");Number("Wavelength samples",qeRequest,"points");
            Choice("Probe mode",qeRequest,"probe_mode",{{"Constant photons","constant_flux"},{"Constant power","constant_power"}});
            Number(qeRequest["probe_mode"]=="constant_flux"?"Photon flux [m^-2 s^-1]":"Probe power [W/m^2]",qeRequest,qeRequest["probe_mode"]=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2");
            ImGui::TextWrapped("Dark differential EQE at 0 V; current device temperature. Full spectrum coverage is required for integrated Jsc.");
            ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Calculate QE")) Send(qeRequest);ImGui::EndDisabled();
            if(wide) ImGui::TableNextColumn();
            if(w->Current().acResults.contains("qe")) {const auto &q=w->Current().acResults.at("qe");ImGui::TextWrapped("Effective: %s | T %.6g K | 0 V | %d samples",q.at("probe_mode").get<std::string>().c_str(),q.at("temperature_K").get<double>(),int(q.at("points").size()));Json x=Json::array(),y=Json::array();for(const auto &p:q.at("points")) {x.push_back(p.at("wavelength_nm"));y.push_back(p.at("EQE").is_number()?p.at("EQE"):Json(NAN));}
                ImGui::TextWrapped("Integrated Jsc: %s A/m^2 | Light Jsc: %s A/m^2",q.at("integrated_Jsc_A_m2").dump().c_str(),q.at("light_Jsc_A_m2").dump().c_str());ImGui::TextWrapped("%s",q.at("integration_note").get<std::string>().c_str());PlotJson("QE","Wavelength [nm]","EQE [fraction]",x,y,false,300);
            }if(wide) ImGui::EndTable();ImGui::EndTabItem();
        }ImGui::EndTabBar();
    }
    if(previousQe!=qeRequest && !w->Busy()) {auto actions=w->Current().actions;actions["qe"]=qeRequest;Send({{"operation","action_settings"},{"actions",actions}});}
    ImGui::TextWrapped("%s",w->status.c_str());return changed;
}
bool Panel::Absorption(Json &draft) {
    auto &selected=w->Current().selectedLayer;auto &layers=draft["layers"];selected=std::clamp(selected,0,int(layers.size())-1);
    BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i);if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),i==selected)) selected=i;ImGui::PopID();}ImGui::EndCombo();}EndRow();
    const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("Absorption editor and plot",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
    auto &a=layers[selected]["absorption"];const auto before=a;bool changed=Choice("Absorption model",a,"model",{{"None","none"},{"Square root","sqrt"},{"SCAPS model","scaps"},{"File","file"}});
    if(changed && a["model"]=="file" && a.value("file",std::string{}).empty()) {try {const auto path=OpenFileDialog({"SCAPS absorption","abs"});if(path)a["file"]=path->Utf8();else {a=before;changed=false;}}catch(const std::exception &e){w->status=e.what();a=before;changed=false;}}
    if(a["model"]=="file") changed|=FileField("Absorption file",a,"file");
    if(a["model"]=="sqrt") {changed|=Number("A [m^-1 eV^1/2]",a,"A_m1_eV_half");changed|=Number("B [m^-1 eV^-1/2]",a,"B_m1_eV_minus_half");}
    if(a["model"]=="scaps") {changed|=Number("Background [m^-1]",a,"background_m1");changed|=Number("Step [m^-1]",a,"step_m1");changed|=Number("Sqrt alpha [m^-1]",a,"sqrt_alpha_m1");changed|=Number("Sqrt beta [m^-1]",a,"sqrt_beta_m1");}
    bool tail=a.value("urbach_enabled",false);if(Toggle("Urbach tail",tail)) {a["urbach_enabled"]=tail;changed=true;}if(tail) changed|=Number("Urbach E_U [eV]",a,"urbach_energy_eV");
    ImGui::TextWrapped("Eg-joint Urbach extension. A pure square-root model has alpha(Eg)=0 and therefore no tail; use a nonzero step/background or a file.");
    if(wide) ImGui::TableNextColumn();
    const auto &r=w->Current().bandResult.contains("optics")?w->Current().bandResult:w->Current().result;
    if(r.contains("optics")) for(const auto &curve:r.at("optics").at("absorption")) if(curve.at("layer_id")==layers[selected].at("scapslab_id")) {
        auto x=curve.at("wavelength_nm").get<std::vector<double>>(),y=curve.at("alpha_m1").get<std::vector<double>>(),base=curve.at("without_tail_m1").get<std::vector<double>>();for(auto &v:y) if(v<=0) v=NAN;for(auto &v:base) if(v<=0) v=NAN;
        if(ImPlot::BeginPlot("Absorption preview",ImVec2(-1,300))) {ImPlot::SetupAxes("Wavelength [nm]","alpha [m^-1]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);ImPlot::PlotLine("With tail",x.data(),y.data(),int(x.size()));ImPlot::PlotLine("Without tail",x.data(),base.data(),int(x.size()));ImPlot::EndPlot();}
    }if(wide) ImGui::EndTable();return changed;
}
bool Panel::Grading(Json &draft) {
    bool changed=false;auto &layers=draft["layers"];auto &selected=w->Current().selectedLayer;selected=std::clamp(selected,0,int(layers.size())-1);
    BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i);if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),i==selected)) selected=i;ImGui::PopID();}ImGui::EndCombo();}EndRow();
    const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("Grading controls and profile",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
    auto &l=layers[selected];BeginRow("Parameter (SI / eV)");if(ImGui::BeginCombo("##parameter",gradingParameter.c_str())) {for(const auto *key:{"Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s"}) if(ImGui::Selectable(key,gradingParameter==key)) gradingParameter=key;ImGui::EndCombo();}EndRow();
    bool enabled=l.contains("grading") && l["grading"].contains(gradingParameter);
    if(Toggle("Grade parameter",enabled)) {if(enabled) {const auto &obj=(gradingParameter=="Nd_m3" || gradingParameter=="Na_m3")?l.at("doping"):l.at("material");const double value=obj.at(gradingParameter);l["grading"][gradingParameter]={{"law","linear"},{"left",value},{"right",value},{"bowing",0.}};}else {l["grading"].erase(gradingParameter);if(l["grading"].empty()) l.erase("grading");}changed=true;}
    if(enabled) {
        auto &g=l["grading"][gradingParameter];changed|=Choice("Profile law",g,"law",{{"Uniform","uniform"},{"Linear","linear"},{"Parabolic","parabolic"},{"Logarithmic","logarithmic"},{"Exponential","exponential"}});
        const bool density=gradingParameter.find("_m3")!=std::string::npos;
        const bool mobility=gradingParameter.find("_m2_V_s")!=std::string::npos;
        const std::string unit=density?"cm^-3":mobility?"cm^2/(V s)":gradingParameter=="eps_r"?"dimensionless":"eV";
        for(const auto *end:{"left","right"}) {Json value={{gradingParameter,g.value(end,0.)}};ImGui::PushID(end);if(Number((std::string(end==std::string("left")?"Left endpoint [":"Right endpoint [")+unit+"]").c_str(),value,gradingParameter.c_str(),density?1e-6:mobility?1e4:1.)) {g[end]=value.at(gradingParameter);changed=true;}ImGui::PopID();}
        if(g["law"]=="parabolic") {if(!g.contains("bowing"))g["bowing"]=0.;changed|=Number("Bowing (parameter units)",g,"bowing");}
        if(wide) ImGui::TableNextColumn();
        try {scaps_core::Profile profile;profile.law=scaps_core::grading_law_from_string(g.at("law"));profile.left=g.at("left");profile.right=g.at("right");profile.bowing=g.value("bowing",0.);scaps_core::validate_profile(profile);
            std::vector<double>x,y;for(int i=0;i<=100;++i) {x.push_back(l.at("thickness_m").get<double>()*i/100.);y.push_back(scaps_core::evaluate_profile(profile,i/100.));}PlotJson("Parameter profile","Local x [m]",gradingParameter.c_str(),x,y);
        }catch(const std::exception &e){ImGui::TextWrapped("%s",e.what());}
    }if(wide) ImGui::EndTable();changed|=Structure(draft);return changed;
}
bool Panel::Defects(Json &draft,bool sheet) {
    bool changed=false;auto &layers=draft["layers"];
    if(sheet && layers.size()<2) {ImGui::TextWrapped("Add a second layer to create a heterointerface.");return false;}
    auto &selected=sheet?w->Current().selectedInterface:w->Current().selectedLayer;
    selected=std::clamp(selected,0,int(layers.size())-(sheet?2:1));
    BeginRow(sheet?"Interface":"Layer");
    const auto name=layers[selected].at("name").get<std::string>()+(sheet?" / "+layers[selected+1].at("name").get<std::string>():"");
    if(ImGui::BeginCombo("##selection",name.c_str())) {
        for(int i=0;i<int(layers.size())-(sheet?1:0);++i) {
            ImGui::PushID(i);const auto label=layers[i].at("name").get<std::string>()+(sheet?" / "+layers[i+1].at("name").get<std::string>():"");
            if(ImGui::Selectable(label.c_str(),selected==i)) {selected=i;selectedDefect=0;}ImGui::PopID();
        }ImGui::EndCombo();
    }EndRow();
    auto &defects=sheet?draft["interface_defects"]:layers[selected]["defects"];
    if(!defects.is_array()) defects=Json::array();
    if(ImGui::Button("Add defect")) {
        auto d=NewDefect(sheet);unsigned n=1;std::set<std::string> ids;
        if(sheet) for(const auto &v:defects) ids.insert(v.at("id"));
        else for(const auto &l:layers) for(const auto &v:l["defects"]) ids.insert(v.at("scapslab_id"));
        const std::string prefix=sheet?"interface-defect-":"defect-";
        while(ids.contains(prefix+std::to_string(n))) ++n;
        d[sheet?"id":"scapslab_id"]=prefix+std::to_string(n);if(sheet) d["left_layer_index"]=selected;
        defects.push_back(d);selectedDefect=int(defects.size())-1;changed=true;++gesture;
    }
    std::vector<int> indices;for(int i=0;i<int(defects.size());++i) if(!sheet || defects[i].at("left_layer_index")==selected) indices.push_back(i);
    if(indices.empty()) {ImGui::TextUnformatted("No defects on this object.");return changed;}
    if(std::find(indices.begin(),indices.end(),selectedDefect)==indices.end()) selectedDefect=indices.front();
    BeginRow("Defect ID");
    if(ImGui::BeginCombo("##defect",defects[selectedDefect].at(sheet?"id":"scapslab_id").get<std::string>().c_str())) {
        for(int i:indices) if(ImGui::Selectable(defects[i].at(sheet?"id":"scapslab_id").get<std::string>().c_str(),i==selectedDefect)) selectedDefect=i;
        ImGui::EndCombo();
    }EndRow();
    ImGui::Text("%d defects on this %s",int(indices.size()),sheet?"interface":"layer");
    if(ImGui::Button("Remove selected defect")) {defects.erase(defects.begin()+selectedDefect);++gesture;return true;}
    auto &d=defects[selectedDefect];
    // Materialize editor defaults without replacing imported values or unknown fields.
    auto defaults=NewDefect(sheet);defaults.update(d,true);d=std::move(defaults);
    if(!sheet) {
        bool amphoteric=d.value("amphoteric",false);
        if(Toggle("Amphoteric (+/0/-)",amphoteric)) {d["amphoteric"]=amphoteric;if(amphoteric)d["energy_distribution"]="single";changed=true;}
        if(amphoteric) {
            bool correlated=d.value("correlated",true);if(Toggle("Correlated transitions",correlated)) {d["correlated"]=correlated;changed=true;}
            changed|=Number("Second Et offset [eV]",d,"second_energy_offset_eV");
            changed|=Number("Second sigma n [cm^2]",d,"second_sigma_n_m2",1e4);
            changed|=Number("Second sigma p [cm^2]",d,"second_sigma_p_m2",1e4);
        }
        if(amphoteric) ImGui::TextWrapped("Amphoteric model: two discrete transitions.");
        ImGui::BeginDisabled(amphoteric);
        changed|=Choice("Distribution",d,"energy_distribution",{{"Discrete","single"},{"Uniform","uniform"},{"Gaussian","gaussian"},{"CB tail","cb_tail"},{"VB tail","vb_tail"}});
        ImGui::EndDisabled();
        changed|=Choice("Energy reference",d,"energy_reference",{{"Ec - offset","relative_to_Ec"},{"Ev + offset","relative_to_Ev"},{"Ei + offset","relative_to_Ei"}});
        changed|=Number("Integrated Nt [cm^-3]",d,"Nt_m3",1e-6);
        changed|=Number("sigma n [cm^2]",d,"sigma_n_m2",1e4);changed|=Number("sigma p [cm^2]",d,"sigma_p_m2",1e4);
        changed|=Choice("Normalization",d,"distribution_normalization",{{"SCAPS analytic integral","scaps"},{"In-gap continuum integral","normalized_continuum"}});
    } else {
        auto previous=d;
        const bool shapeChanged=Choice("Distribution",d,"energy_distribution",{{"Discrete","single"},{"Uniform","uniform"},{"Gaussian","gaussian"}});
        changed|=shapeChanged;
        const char *refs[]={"3: higher Ev + offset","4: lower Ec - offset","5: left Ei + offset","6: right Ei + offset","7: left Ev + offset","8: right Ev + offset","9: overlap midgap + offset"};
        int ref=d.at("energy_reference").get<int>()-3;BeginRow("Energy reference");
        if(ImGui::Combo("##reference",&ref,refs,7)) {d["energy_reference"]=ref+3;changed=true;++gesture;}EndRow();
        if(d.at("energy_distribution")=="single") ImGui::TextWrapped("Discrete states use integrated sheet Nt; peak density applies to distributions.");
        ImGui::BeginDisabled(d.at("energy_distribution")=="single");
        const bool conventionChanged=Choice("Sheet density convention",d,"density_convention",{{"Integrated sheet","integrated_sheet"},{"SCAPS peak / eV","scaps_peak"}});
        ImGui::EndDisabled();
        if(shapeChanged || conventionChanged) {ChangeSheetDistribution(previous,d.at("energy_distribution"),d.at("density_convention"));d=std::move(previous);changed=true;}
        if(d.at("energy_distribution")=="single" || d.at("density_convention")=="integrated_sheet") {
            changed|=Number("Sheet Nt [cm^-2]",d,"Nt_m2",1e-4);
        } else {changed|=Number("Peak Dit [cm^-2/eV]",d,"peak_density_m2_eV",1e-4);}
        changed|=Number("Left sigma n [cm^2]",d,"sigma_n_left_m2",1e4);changed|=Number("Left sigma p [cm^2]",d,"sigma_p_left_m2",1e4);
        changed|=Number("Right sigma n [cm^2]",d,"sigma_n_right_m2",1e4);changed|=Number("Right sigma p [cm^2]",d,"sigma_p_right_m2",1e4);
    }
    ImGui::BeginDisabled(!sheet && d.value("amphoteric",false));
    changed|=Choice("Charge type",d,"charge_type",{{"Neutral","neutral"},{"Donor (+/0)","donor_like"},{"Acceptor (0/-)","acceptor_like"}});
    ImGui::EndDisabled();
    changed|=Number("Et offset [eV]",d,"energy_offset_eV");
    if(d.at("energy_distribution")!="single") {
        changed|=Number("Characteristic energy [eV]",d,"characteristic_energy_eV");
        if(d.at("energy_distribution")=="gaussian") changed|=Number("Gaussian span [Echar]",d,"gaussian_width");
        if(d.at("energy_distribution")=="cb_tail" || d.at("energy_distribution")=="vb_tail") changed|=Number("Tail span [Echar]",d,"tail_width");
        ImGui::TextWrapped("Gaussian: exp(-((E-Et)/Echar)^2), sigma = Echar/sqrt(2). Uniform width = Echar. Tails decay from the selected reference offset.");
    }
    return changed;
}
void Panel::Preview(bool sheet) {
    if(w->Current().bandResult.empty() && w->Current().result.empty()) {ImGui::TextWrapped("Preview waiting for auto-recalculate band. The previous model's occupancy is cleared on every edit.");return;}
    const auto &point=PointAtBias(w->Current().result,w->Current().bandResult,w->Current().selectedBias);
    ImGui::TextWrapped("Occupancy at %.5g V, %.5g K | %s | %s",point.at("voltage_V").get<double>(),w->Current().document.at("temperature_K").get<double>(),sheet?"interface sheet":"layer midpoint",point.value("validity_status",std::string("unknown")).c_str());
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,ImVec2(.03f,.08f));
    if(!ImPlot::BeginPlot("Nt(E) and occupancy",ImVec2(-1,std::max(300.f,ImGui::GetContentRegionAvail().y-8)))) {ImPlot::PopStyleVar();return;}
    ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);
    ImPlot::SetupAxes("E - Ev [eV]",sheet?"Dit [cm^-2/eV]":"Nt [cm^-3/eV]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxis(ImAxis_Y2,"Occupation fraction",ImPlotAxisFlags_AuxDefault);ImPlot::SetupAxisLimits(ImAxis_Y2,-.05,1.05,ImGuiCond_Always);
    bool edgesDrawn=false;
    for(const auto &c:point.at("defect_previews")) {
        if(c.at("sheet").get<bool>()!=sheet) continue;
        if(sheet?c.at("left_layer_index")!=w->Current().selectedInterface:c.at("layer_id")!=w->Current().document.at("layers")[w->Current().selectedLayer].at("scapslab_id")) continue;
        const auto id=c.at("id").get<std::string>();auto x=c.at("energy_eV").get<std::vector<double>>(),y=c.at(sheet?"density_m2_eV":"density_m3_eV").get<std::vector<double>>();
        for(auto &v:y) v*=sheet?1e-4:1e-6;
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);
        if(c.at("single").get<bool>()) {const double e=c.at("level_energy_eV");ImPlot::PlotInfLines((id+" (discrete Nt)").c_str(),&e,1);if(c.contains("second_level_energy_eV")) {const double e2=c.at("second_level_energy_eV");ImPlot::PlotInfLines((id+" second transition").c_str(),&e2,1);}}
        else ImPlot::PlotLine(id.c_str(),x.data(),y.data(),int(x.size()));
        if(!edgesDrawn) {const auto edges=c.at("band_edges_eV").get<std::vector<double>>();for(int e=0;e<int(edges.size());++e) {const char *labels[]={"EvL","EcL","EvR","EcR"};ImPlot::PlotInfLines(sheet?labels[e]:e==0?"Ev":"Ec",&edges[e],1,ImPlotSpec(ImPlotProp_LineColor,ImVec4(.9f,.85f,.4f,1),ImPlotProp_LineWeight,2));ImPlot::TagX(edges[e],ImVec4(.5f,.45f,.15f,1),"%s",sheet?labels[e]:e==0?"Ev":"Ec");}edgesDrawn=true;}
        if(point.at("status")=="Converged") {
            x=c.at("occupation_energy_eV").get<std::vector<double>>();y.clear();for(const auto &v:c.at("occupation")) y.push_back(v.is_number()?v.get<double>():NAN);
            ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlotSpec occupationStyle(ImPlotProp_LineWeight,2,ImPlotProp_Marker,ImPlotMarker_None);ImPlot::PlotLine((id+(c.at("amphoteric").get<bool>() && c.at("correlated").get<bool>()?" P-":" f")).c_str(),x.data(),y.data(),int(x.size()),occupationStyle);
        }
    }ImPlot::EndPlot();ImPlot::PopStyleVar();
}
bool Panel::Structure(Json &draft) {
    auto &layers=draft["layers"];double total=0,emin=0,emax=-1e10;
    for(const auto &l:layers) {total+=l.at("thickness_m").get<double>();const auto &m=l.at("material");emin=std::min(emin,-m.at("chi_eV").get<double>()-m.at("Eg_eV").get<double>());emax=std::max(emax,-m.at("chi_eV").get<double>());}
    ImGui::TextWrapped("Structure to scale | total %.5g um. Click layer/interface; drag a layer's right edge to change thickness.",total*1e6);
    const bool compact=view==22;
    const auto origin=ImGui::GetCursorScreenPos();const float width=std::max(1.f,ImGui::GetContentRegionAvail().x),height=compact?64.f:view==14?std::max(190.f,ImGui::GetContentRegionAvail().y-12):190.f;
    auto *draw=ImGui::GetWindowDrawList();double offset=0;bool changed=false;
    auto y=[&](double e){return origin.y+16+float((emax-e)/(emax-emin))*std::max(78.f,height-120);};
    for(int i=0;i<int(layers.size());++i) {
        auto &l=layers[i];const auto &m=l.at("material");const float x1=origin.x+float(offset/total)*width;offset+=l.at("thickness_m").get<double>();const float x2=origin.x+float(offset/total)*width;
        draw->AddRectFilled(ImVec2(x1,origin.y),ImVec2(x2,origin.y+height),w->Current().selectedInterface<0 && w->Current().selectedLayer==i?IM_COL32(44,86,116,255):IM_COL32(25+15*(i%3),48,67,255));
        const double ec=-m.at("chi_eV").get<double>(),ev=ec-m.at("Eg_eV").get<double>();
        if(!compact) {draw->AddLine(ImVec2(x1,y(ec)),ImVec2(x2,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(ev)),ImVec2(x2,y(ev)),IM_COL32(255,173,110,255),2);
        if(i>0) {const auto &a=layers[i-1].at("material");const double prevEc=-a.at("chi_eV").get<double>();draw->AddLine(ImVec2(x1,y(prevEc)),ImVec2(x1,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(prevEc-a.at("Eg_eV").get<double>())),ImVec2(x1,y(ev)),IM_COL32(255,173,110,255),2);}
        }
        draw->PushClipRect(ImVec2(x1+2,origin.y),ImVec2(x2-2,origin.y+height),true);
        draw->AddText(ImVec2(x1+5,origin.y+(compact?8:height-90)),IM_COL32_WHITE,l.at("name").get<std::string>().c_str());
        char thickness[80];std::snprintf(thickness,sizeof(thickness),"%.4g um",l.at("thickness_m").get<double>()*1e6);
        if(x2-x1>ImGui::CalcTextSize(thickness).x+10) draw->AddText(ImVec2(x1+5,origin.y+(compact?32:height-50)),IM_COL32_WHITE,thickness);
        char parameters[100];std::snprintf(parameters,sizeof(parameters),"chi %.3g eV | Eg %.3g eV",-ec,ec-ev);
        if(!compact && x2-x1>ImGui::CalcTextSize(parameters).x+10) draw->AddText(ImVec2(x1+5,origin.y+height-70),IM_COL32(205,220,235,255),parameters);
        if(!compact && l.contains("grading") && !l["grading"].empty()) {
            const auto label=std::string("Graded: ")+l["grading"].begin().key();draw->AddText(ImVec2(x1+5,origin.y+height-35),IM_COL32(140,235,170,255),label.c_str());
            const auto &g=l["grading"].begin().value();if(g.contains("left") && g.contains("right") && (g.value("law",std::string{})=="linear" || g.value("law",std::string{})=="uniform" || g.value("law",std::string{})=="parabolic" || g.value("law",std::string{})=="logarithmic" || g.value("law",std::string{})=="exponential")) {
                scaps_core::Profile profile;profile.law=scaps_core::grading_law_from_string(g.value("law",std::string("linear")));profile.left=g["left"];profile.right=g["right"];profile.bowing=g.value("bowing",0.);
                profile.background=g.value("background",0.);profile.length_left=g.value("length_left",1.);profile.length_right=g.value("length_right",1.);
                std::vector<double> samples;for(int n=0;n<=40;++n) samples.push_back(scaps_core::evaluate_profile(profile,n/40.));
                const auto [lo,hi]=std::minmax_element(samples.begin(),samples.end());const double low=*lo,span=std::max(1e-30,*hi-low);
                for(int n=1;n<=40;++n) {auto at=[&](int k){return ImVec2(x1+(x2-x1)*k/40.f,origin.y+height-8-float((samples[k]-low)/span)*18);};draw->AddLine(at(n-1),at(n),IM_COL32(140,235,170,255));}
            }
        }
        draw->PopClipRect();
        ImGui::PushID(i);ImGui::SetCursorScreenPos(ImVec2(x1,origin.y));ImGui::InvisibleButton("##select-layer",ImVec2(std::max(1.f,x2-x1-7),height));
        if(ImGui::IsItemClicked()) {w->Current().selectedLayer=i;w->Current().selectedInterface=-1;w->focusLayers=true;}
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s | %.6g um | chi %.4g eV | Eg %.4g eV",l.at("name").get<std::string>().c_str(),l.at("thickness_m").get<double>()*1e6,m.at("chi_eV").get<double>(),m.at("Eg_eV").get<double>());
        ImGui::SetCursorScreenPos(ImVec2(x2-7,origin.y));ImGui::InvisibleButton("##thickness-interface",ImVec2(7,height));TrackGesture();
        if(ImGui::IsItemClicked()) {w->Current().selectedInterface=i+1<int(layers.size())?i:-1;w->Current().selectedLayer=i;w->focusLayers=true;}
        if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {l["thickness_m"]=std::max(1e-9,l.at("thickness_m").get<double>()+ImGui::GetIO().MouseDelta.x*total/width);changed|=ImGui::GetIO().MouseDelta.x!=0;}
        if(ImGui::IsItemHovered()) {ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);ImGui::SetTooltip("Drag: thickness [um]. Click: interface after %s",l.at("name").get<std::string>().c_str());}
        if(i+1<int(layers.size())) {
            const auto &right=layers[i+1].at("material");char offsets[100];
            const double deltaEc=m.at("chi_eV").get<double>()-right.at("chi_eV").get<double>();
            std::snprintf(offsets,sizeof(offsets),"dEc %.3g eV / dEv %.3g eV",deltaEc,deltaEc+m.at("Eg_eV").get<double>()-right.at("Eg_eV").get<double>());
            if(!compact && x2-origin.x>ImGui::CalcTextSize(offsets).x+14) draw->AddText(ImVec2(x2-ImGui::CalcTextSize(offsets).x-14,origin.y+height-24),IM_COL32(230,210,140,255),offsets);
            int count=0;for(const auto &d:draft.value("interface_defects",Json::array())) if(d.at("left_layer_index")==i) ++count;
            draw->AddLine(ImVec2(x2-3,origin.y),ImVec2(x2-3,origin.y+height),IM_COL32(235,196,90,255),2);
            const auto badge=std::to_string(count);draw->AddCircleFilled(ImVec2(x2-13,origin.y+12),9,IM_COL32(120,85,22,255));draw->AddText(ImVec2(x2-13-ImGui::CalcTextSize(badge.c_str()).x*.5f,origin.y+5),IM_COL32_WHITE,badge.c_str());
        }ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x,origin.y+height+6));ImGui::Dummy(ImVec2(width,1));
    return changed;
}
void Panel::Results() {
    const bool bandOnly=view==5 && w->Current().result.empty() && !w->Current().bandResult.empty();
    const auto &result=bandOnly?w->Current().bandResult:w->Current().result;
    const auto &layers=w->Current().document.at("layers");
    double total=0;for(const auto &l:layers) total+=l.at("thickness_m").get<double>();
    if(view==5) {auto draft=w->Current().document;if(Structure(draft)) Send({{"operation","edit"},{"document",draft},{"gesture_id",std::to_string(gesture)}});}
    if(!result.contains("points") || result["points"].empty()) {ImGui::TextUnformatted("Calculate I-V to view results.");return;}
    const auto &points=result.at("points");int bias=bandOnly?0:w->Current().selectedBias;bias=std::clamp(bias,0,int(points.size())-1);
    BeginRow("Bias point");ImGui::SliderInt("##bias",&bias,0,int(points.size())-1);EndRow();if(!bandOnly)w->Current().selectedBias=bias;
    const auto &band=w->Current().bandResult;
    const auto &p=view==5?PointAtBias(w->Current().result,band,bias):points[bias];
    const bool preview=view==5 && !band.empty() && &p==&band.at("points")[0];
    const auto &metrics=preview?band:result;
    ImGui::Text("%.2f ms | %d mesh nodes",metrics.value("compute_time_ms",0.0),metrics.value("mesh_node_count",0));
    w->Current().biasVoltage_V=p.at("voltage_V");
    ImGui::TextWrapped("Terminal %.5g V | internal %.5g V | %s | %d iterations",p.at("terminal_voltage_V").get<double>(),p.at("internal_voltage_V").get<double>(),p.at("status").get<std::string>().c_str(),p.at("iterations").get<int>());
    if(view==10) {
        Toggle("Log |J|",absoluteLog);
        std::vector<double> v,j; int failed=0;
        for(const auto &pt:points) {
            v.push_back(pt.at("voltage_V"));const bool valid=pt.value("valid",false) && pt.at("J_A_m2").is_number();
            if(!valid) ++failed;
            const double current=valid?pt.at("J_A_m2").get<double>():NAN;
            j.push_back(absoluteLog?(current==0?NAN:std::abs(current)):current);
        }
        ImGui::TextWrapped("Curve validity: %s | %d / %d valid points; invalid points masked",result.value("validity_status",std::string("unknown")).c_str(),int(points.size())-failed,int(points.size()));
        if(ImGui::CollapsingHeader("Per-point convergence")) {
            for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %s | %s",pt.at("bias_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("status").get<std::string>().c_str(),pt.at("validity_status").get<std::string>().c_str());
        }
        if(ImPlot::BeginPlot("I-V",ImVec2(-1,-1))) {
            ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
            if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
            ImPlot::PlotLine("I-V",v.data(),j.data(),int(v.size()));
            ImPlot::PlotScatter("Selected bias",&v[bias],&j[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
            double selectedVoltage=v[bias];
            if(ImPlot::DragLineX(1,&selectedVoltage,ImVec4(1,.7f,.3f,1))) {
                bias=int(std::min_element(v.begin(),v.end(),[&](double a,double b){return std::abs(a-selectedVoltage)<std::abs(b-selectedVoltage);})-v.begin());
            }
            w->Current().selectedBias=bias;w->Current().biasVoltage_V=v[bias];
            ImPlot::EndPlot();
        }
        return;
    }
    const double current=p.at("J_A_m2").is_number()?std::abs(p.at("J_A_m2").get<double>()):0;
    if(current>0) ImGui::Text("Residual %.3e | relative dJ %.3e",p.at("residual").get<double>(),p.at("conservation_error_A_m2").get<double>()/current);
    if(p.at("status")!="Converged" || p.at("J_A_m2").is_null()) {ImGui::TextWrapped("This bias did not converge; profiles are masked. Select a converged bias.");return;}
    auto x=p.at("x_m").get<std::vector<double>>();for(auto &value:x)value*=1e6;
    if(x.empty()) return;
    const float h=std::max(120.f,(ImGui::GetContentRegionAvail().y-10)/2);
    if(ImPlot::BeginPlot("Bands / contacts",ImVec2(-1,h))) {
        ImPlot::SetupAxes("Position [um]","Energy [eV]");ImPlot::SetupLegend(ImPlotLocation_NorthEast,ImPlotLegendFlags_Horizontal);
        const double end=total*1e6,metal=end*.08;
        ImPlot::SetupAxisLimits(ImAxis_X1,-metal,end+metal,ImGuiCond_Always);
        for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) {const auto y=p.at(key).get<std::vector<double>>();if(y.size()==x.size())ImPlot::PlotLine(key,x.data(),y.data(),int(x.size()));}
        const auto limits=ImPlot::GetPlotLimits();auto *draw=ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for(int side=0;side<2;++side) {
            const double lo=side?end:-metal,hi=side?end+metal:0;
            const auto a=ImPlot::PlotToPixels(lo,limits.Y.Max),b=ImPlot::PlotToPixels(hi,limits.Y.Min);
            draw->AddRectFilled(a,b,IM_COL32(255,176,66,38));
            const double phi=result.at("contact_phi_eV")[side];
            const double chi=layers[side?layers.size()-1:0].at("material").at("chi_eV");
            const auto ec=p.at("Ec_eV").get<std::vector<double>>();
            if(!ec.empty()) {
                const double ef=(side?ec.back():ec.front())+chi-phi;const double xx[]={lo,hi},yy[]={ef,ef};
                ImPlot::PlotLine(side?"EFm right":"EFm left",xx,yy,2);
                ImPlot::Annotation((lo+hi)/2,ef,ImVec4(1,.8f,.4f,1),ImVec2(side?-90.f:5.f,-28.f),true,"EFm / phi %.3f eV",phi);
            }
        }
        double boundary=0;
        for(std::size_t i=0;i+1<layers.size();++i) {
            boundary+=layers[i].at("thickness_m").get<double>()*1e6;
            draw->AddLine(ImPlot::PlotToPixels(boundary,limits.Y.Min),ImPlot::PlotToPixels(boundary,limits.Y.Max),IM_COL32(210,210,220,130));
        }
        ImPlot::PopPlotClipRect();ImPlot::EndPlot();
    }
    if(ImPlot::BeginPlot("Carriers",ImVec2(-1,h))) {
        ImPlot::SetupAxes("Position [um]","Density [cm^-3]");ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y1,[](double value,char *buffer,int size,void*) {return std::snprintf(buffer,size,"1e%.0f",std::log10(value));});
        for(const auto *key:{"n_m3","p_m3"}) {auto y=p.at(key).get<std::vector<double>>();for(auto &value:y)value*=1e-6;if(y.size()==x.size())ImPlot::PlotLine(key==std::string("n_m3")?"n":"p",x.data(),y.data(),int(x.size()));}
        ImPlot::EndPlot();
    }
}
void Panel::AC() {
    const bool cv=view<8; const char *kind=cv?"cv":"cf";
    if(!w->Busy() && w->Current().actions.contains(kind)) acRequest.update(w->Current().actions.at(kind),true);
    const auto previousRequest=acRequest;
    ImGui::BeginDisabled(w->Busy());
    ImGui::SetNextItemWidth(130); Number("T [K]",acRequest,"temperature_K");
    if(ImGui::CollapsingHeader("Sweep settings",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(cv) {
            Number("Start [V]",acRequest,"start_V"); Number("Stop [V]",acRequest,"stop_V"); Number("Step [V]",acRequest,"step_V"); Number("Frequency [Hz]",acRequest,"frequency_Hz");
        } else {
            Number("Bias [V]",acRequest,"voltage_V"); Number("Start [Hz]",acRequest,"start_Hz"); Number("Stop [Hz]",acRequest,"stop_Hz"); Number("Log points",acRequest,"points");
        }
    }
    if(previousRequest!=acRequest && !w->Busy()) {auto actions=w->Current().actions;actions[kind]=acRequest;Send({{"operation","action_settings"},{"actions",actions}});}
    if(ImGui::Button(cv?"Run C-V":"Run C-f")) Send(acRequest);
    ImGui::SameLine(); ImGui::SetNextItemWidth(220); TextInput("CSV path",csvFile,sizeof(csvFile)); ImGui::SameLine();
    if(ImGui::Button("Export CSV")) Send({{"operation","export_ac"},{"kind",kind},{"path",csvFile}});
    ImGui::EndDisabled();
    if(w->Busy()) {
        ImGui::TextDisabled("AC controls disabled while a job is active; wait or Cancel.");
        const auto job=w->jobs->GetJob(w->active); if(job) {ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0); ImGui::TextWrapped("%s",job->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
    }
    const Json *snapshot=nullptr;
    if(w->Current().acResults.contains(kind)) snapshot=&w->Current().acResults.at(kind);
    else for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->acResults.contains(kind)) {snapshot=&d->acResults.at(kind);break;}
    if(!snapshot) {ImGui::TextWrapped("Run the sweep to view AC results. Voltage: left minus right.");return;}
    const auto &data=*snapshot;const auto &points=data.at("points");
    ImGui::TextWrapped("Curve validity: %s (1%% conservation gate)",data.value("validity_status",std::string("unknown")).c_str());
    const auto &first=points.front();
    ImGui::Text("Result: %.6g K | %s = %.6g %s | %d points",first.at("temperature_K").get<double>(),cv?"f":"V",first.at(cv?"frequency_Hz":"voltage_V").get<double>(),cv?"Hz":"V",int(points.size()));
    std::vector<double> x,y,g; int failed=0;
    const char *key=view==7?"inverse_C2_F_minus2":view==9?"G_over_omega_F":"capacitance_F";
    for(const auto &pt:points) {
        const bool valid=pt.value("valid",false); if(!valid) ++failed;
        x.push_back(pt.at(cv?"voltage_V":"frequency_Hz")); y.push_back(valid && pt.at(key).is_number()?pt.at(key).get<double>():NAN);
        g.push_back(valid && pt.at("G_over_omega_F").is_number()?pt.at("G_over_omega_F").get<double>():NAN);
    }
    if(failed) ImGui::TextWrapped("%d invalid points (convergence/current conservation); raw CSV rows retain validity flags.",failed);
    if(ImGui::CollapsingHeader("Per-point convergence"))
        for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %.5g Hz | %s | %s",pt.at("point_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("frequency_Hz").get<double>(),pt.at("status").get<std::string>().c_str(),pt.at("validity_status").get<std::string>().c_str());
    const char *ylabel=view==7?"1/C^2 [F^-2]":view==9?"G/omega [F]":"C [F]";
    if(ImPlot::BeginPlot(names[view],ImVec2(-1,-1))) {
        ImPlot::SetupAxes(cv?"Voltage [V]":"Frequency [Hz]",ylabel,ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        if(view==6 || view==8) ImPlot::SetupAxis(ImAxis_Y2,"G/omega [F]",ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_Y1,view==8?"%.8g":"%.3g");
        if(!cv) ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->acResults.contains(kind)) {
            std::vector<double> xx,yy,gg;for(const auto &pt:d->acResults.at(kind).at("points")) {xx.push_back(pt.at(cv?"voltage_V":"frequency_Hz"));yy.push_back(pt.value("valid",false) && pt.at(key).is_number()?pt.at(key).get<double>():NAN);gg.push_back(pt.value("valid",false) && pt.at("G_over_omega_F").is_number()?pt.at("G_over_omega_F").get<double>():NAN);}
            const auto c=d->colour.get<std::vector<float>>();const auto label=d->name+"##"+d->id;
            ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);ImPlot::PlotLine(label.c_str(),xx.data(),yy.data(),int(xx.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],c[3])});
            if(view==6 || view==8) {ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlot::PlotLine((d->name+" G/omega##"+d->id).c_str(),xx.data(),gg.data(),int(xx.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],c[3])});}
        }
        ImPlot::EndPlot();
    }
}
void Panel::Devices() {
    ImGui::BeginDisabled(w->Busy());
    if(ImGui::Button("New (template)")) Send({{"operation","device_add"}});ImGui::SameLine();
    if(ImGui::Button("Duplicate")) Send({{"operation","device_duplicate"}});ImGui::SameLine();
    ImGui::BeginDisabled(w->project.devices.size()==1);
    if(ImGui::Button("Delete")) Send({{"operation","device_remove"}});ImGui::EndDisabled();
    if(ImGui::Button("Import YAML")) {try {if(const auto path=OpenFileDialog({"Device YAML","yaml,yml"})) Send({{"operation","device_add"},{"path",path->Utf8()}});}catch(const std::exception &e){w->status=e.what();}}
    ImGui::SameLine();if(ImGui::Button("Undo delete")) {w->preferProjectUndo=true;Send({{"operation","undo"}});}
    ImGui::TextWrapped("Import YAML here, or use File / Import SCAPS .def. Unsupported items are reported as warnings.");
    ImGui::EndDisabled();
    if(ImGui::BeginTable("Device list",3,ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Device",ImGuiTableColumnFlags_WidthStretch,1.8f);ImGui::TableSetupColumn("Status");ImGui::TableSetupColumn("Compare",ImGuiTableColumnFlags_WidthFixed,75);ImGui::TableHeadersRow();
        for(const auto &d:w->project.devices) {
            ImGui::PushID(d->id.c_str());ImGui::TableNextRow();ImGui::TableNextColumn();
            const auto c=d->colour.get<std::vector<float>>();ImGui::ColorButton("##colour",ImVec4(c[0],c[1],c[2],c[3]),ImGuiColorEditFlags_NoTooltip,ImVec2(12,12));ImGui::SameLine();
            if(ImGui::Selectable((d->name+(d->dirty?" *":"")+"##select").c_str(),w->project.active_device==d->id)) Send({{"operation","device_select"},{"device_id",d->id}});
            if(ImGui::BeginDragDropSource()) {ImGui::SetDragDropPayload("SCAPSLAB_DEVICE",d->id.c_str(),d->id.size()+1);ImGui::TextUnformatted(d->name.c_str());ImGui::EndDragDropSource();}
            if(ImGui::BeginDragDropTarget()) {if(const auto *payload=ImGui::AcceptDragDropPayload("SCAPSLAB_DEVICE")) Send({{"operation","device_move"},{"device_id",static_cast<const char*>(payload->Data)},{"index",int(&d-&w->project.devices[0])}});ImGui::EndDragDropTarget();}
            ImGui::TableNextColumn();ImGui::TextUnformatted(d->status.c_str());ImGui::TableNextColumn();
            bool compare=w->project.compare_set.contains(d->id);ImGui::TextUnformatted("Show");ImGui::SameLine();ImGui::BeginDisabled(w->Busy());if(ImGui::Checkbox("##compare",&compare)) Send({{"operation","device_update"},{"device_id",d->id},{"compare",compare}});ImGui::EndDisabled();
            ImGui::PopID();
        }ImGui::EndTable();
    }
    ImGui::BeginDisabled(w->Busy());
    char name[256];std::snprintf(name,sizeof(name),"%s",w->Current().name.c_str());BeginRow("Rename active");
    if(ImGui::InputText("##rename",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) Send({{"operation","device_update"},{"name",name}});EndRow();
    BeginRow("Colour");auto c=w->Current().colour.get<std::vector<float>>();if(ImGui::ColorEdit4("##value",c.data(),ImGuiColorEditFlags_NoInputs)) Send({{"operation","device_update"},{"colour",c}});EndRow();
    ImGui::TextWrapped("%s",w->Current().description.c_str());
    if(ImGui::Button("Run all checked")) Send({{"operation","run_all"}});ImGui::EndDisabled();
    if(w->Busy()) {
        if(w->batchTotal) {ImGui::ProgressBar(float(w->batchCompleted)/float(w->batchTotal));ImGui::Text("Devices: %d / %d",int(w->batchCompleted),int(w->batchTotal));}
        const auto job=w->jobs->GetJob(w->active);if(job) {ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0);ImGui::TextWrapped("%s",job->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
    }
}
void Panel::Compare() {
    if(Toggle("Log |J|",absoluteLog)) {auto ui=w->project.ui;ui["iv_absolute_log"]=absoluteLog;Send({{"operation","project_edit"},{"ui",ui}});}
    ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Run all checked")) Send({{"operation","run_all"}});ImGui::EndDisabled();
    bool any=false;for(const auto &d:w->project.devices) any|=w->project.compare_set.contains(d->id) && !d->result.empty();
    if(!any) {ImGui::TextWrapped("Check devices in Devices, then Run all checked to compare their I-V curves.");return;}
    bool light=false;for(const auto &d:w->project.devices) light|=w->project.compare_set.contains(d->id) && d->result.contains("photovoltaic");
    if(light && ImGui::BeginTable("Solar metrics",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        for(const auto *label:{"Device","Voc [V]","Jsc [mA/cm^2]","FF [%]","Efficiency [%]"}) ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->result.contains("photovoltaic")) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(d->name.c_str());
            for(const auto *key:{"Voc_V","Jsc_A_m2","FF_fraction","efficiency_fraction"}) {ImGui::TableNextColumn();const auto &value=d->result.at("photovoltaic").at(key);if(value.is_number()) ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",value.get<double>()*(std::string(key)=="Jsc_A_m2"?.1:std::string(key)=="Voc_V"?1:100));else ImGui::TextUnformatted("--");}
        }ImGui::EndTable();
    }
    if(ImPlot::BeginPlot("I-V compare",ImVec2(-1,-1))) {
        ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && !d->result.empty()) {
            std::vector<double> x,y;for(const auto &pt:d->result.at("points")) {x.push_back(pt.at("voltage_V"));double j=pt.value("valid",false) && pt.at("J_A_m2").is_number()?pt.at("J_A_m2").get<double>():NAN;y.push_back(absoluteLog?(j==0?NAN:std::abs(j)):j);}
            const auto c=d->colour.get<std::vector<float>>();const auto label=d->name+"##"+d->id;
            ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],c[3])});
            if(d->id==w->project.active_device && !x.empty()) {
                int bias=std::clamp(d->selectedBias,0,int(x.size())-1);double voltage=x[bias];
                ImPlot::PlotScatter("Selected bias",&x[bias],&y[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
                if(ImPlot::DragLineX(1,&voltage,ImVec4(c[0],c[1],c[2],c[3]))) {bias=int(std::min_element(x.begin(),x.end(),[&](double a,double b){return std::abs(a-voltage)<std::abs(b-voltage);})-x.begin());}
                d->selectedBias=bias;d->biasVoltage_V=x[bias];
            }
        }ImPlot::EndPlot();
    }
}

}






namespace SST::ScapsLab {
namespace {
std::string MemberLabel(const Json &row,const Json &spec) {
    std::string label=row.at("id").get<std::string>();
    for(const auto &a:spec.at("axes")) {const auto path=a.at("path").get<std::string>();char value[80];std::snprintf(value,sizeof(value)," = %.6g %s",row.at("parameters").at(path).get<double>(),a.value("unit",std::string("1")).c_str());label+=" | "+path+value;}
    return label;
}
ImVec4 MemberColour(const Json &row,const Json &table) {
    const auto path=table.at("spec").at("axes")[0].at("path").get<std::string>();double lo=INFINITY,hi=-INFINITY;
    for(const auto &r:table.at("rows")) {const double v=r.at("parameters").at(path);lo=std::min(lo,v);hi=std::max(hi,v);}
    const double v=row.at("parameters").at(path);return ImPlot::SampleColormap(float(hi>lo?(v-lo)/(hi-lo):.5),ImPlotColormap_Viridis);
}
bool AxisNumber(const char *label,Json &axis,const char *key,const Json &parameter) {
    const auto path=parameter.at("path").get<std::string>();const auto name=path.substr(path.find_last_of('.')==std::string::npos?0:path.find_last_of('.')+1);
    Json value={{name,axis.at(key)}};const bool changed=Number(label,value,name.c_str());if(changed)axis[key]=value[name];return changed;
}
}
void Panel::Batch() {
    auto &d=w->Current();auto spec=d.batchSpec;bool changed=false;const auto parameters=BatchParameters(d.document);
    ImGui::TextWrapped("%s | independent members; failed points remain in the worksheet. Ctrl+click for exact values.",d.name.c_str());
    ImGui::BeginDisabled(w->Busy());
    BeginRow("Dimensions / mode");int mode=spec.at("axes").size()==1?0:spec.value("mode",std::string("nested"))=="together"?2:1;
    if(ImGui::Combo("##mode",&mode,"1-D\0 2-D (nested)\0Together\0")) {changed=true;spec["mode"]=mode==2?"together":"nested";if(mode==0)spec["axes"].erase(spec["axes"].begin()+1,spec["axes"].end());else if(spec["axes"].size()==1)spec["axes"].push_back({{"parameter_id",parameters[0].at("id")},{"values",Json::array({parameters[0].at("value"),parameters[0].at("value"),parameters[0].at("value")})}});}EndRow();
    TextInput("Search project tree",parameterSearch,sizeof(parameterSearch));
    for(std::size_t i=0;i<spec["axes"].size();++i) {
        ImGui::PushID(int(i));auto &axis=spec["axes"][i];auto selected=std::find_if(parameters.begin(),parameters.end(),[&](const Json &p){return p.at("id")==axis.at("parameter_id");});
        ImGui::SeparatorText(i==0?"Axis 1":"Axis 2");BeginRow("Project parameter");
        const auto title=selected==parameters.end()?std::string("Select parameter"):selected->at("label").get<std::string>()+" ["+selected->at("unit").get<std::string>()+"]";
        if(ImGui::BeginCombo("##parameter",title.c_str(),ImGuiComboFlags_HeightLargest)) {
            std::string group;
            for(const auto &p:parameters) {const auto label=p.at("label").get<std::string>();if(parameterSearch[0] && label.find(parameterSearch)==std::string::npos)continue;
                const auto root=label.substr(0,label.find(" / "));if(group!=root) {ImGui::SeparatorText(root.c_str());group=root;}
                if(ImGui::Selectable((label+" ["+p.at("unit").get<std::string>()+"]##"+p.at("id").get<std::string>()).c_str(),selected!=parameters.end() && p.at("id")==selected->at("id"))) {axis={{"parameter_id",p.at("id")},{"values",Json::array({p.at("value")})}};changed=true;}
            }ImGui::EndCombo();
        }EndRow();
        selected=std::find_if(parameters.begin(),parameters.end(),[&](const Json &p){return p.at("id")==axis.at("parameter_id");});
        if(selected!=parameters.end()) {
            BeginRow("Values");int spacing=axis.contains("values")?0:axis.value("spacing",std::string("linear"))=="log"?2:1;
            if(ImGui::Combo("##spacing",&spacing,"List\0Linear\0Log\0")) {changed=true;const auto v=selected->at("value");axis={{"parameter_id",selected->at("id")}};if(spacing==0)axis["values"]=Json::array({v});else {axis["start"]=v;axis["stop"]=v;axis["count"]=3;axis["spacing"]=spacing==2?"log":"linear";}}EndRow();
            if(spacing==0) {
                BeginRow("List length");int count=int(axis["values"].size());if(ImGui::SliderInt("##count",&count,1,100)) {while(axis["values"].size()<std::size_t(count))axis["values"].push_back(selected->at("value"));while(axis["values"].size()>std::size_t(count))axis["values"].erase(axis["values"].end()-1);changed=true;}EndRow();
                for(std::size_t j=0;j<axis["values"].size();++j) {ImGui::PushID(int(j));Json v={{"value",axis["values"][j]}};const auto label="Value "+std::to_string(j+1)+" ["+selected->at("unit").get<std::string>()+"]";if(AxisNumber(label.c_str(),v,"value",*selected)) {axis["values"][j]=v["value"];changed=true;}ImGui::PopID();}
            } else {changed|=AxisNumber(("Start ["+selected->at("unit").get<std::string>()+"]").c_str(),axis,"start",*selected);changed|=AxisNumber(("Stop ["+selected->at("unit").get<std::string>()+"]").c_str(),axis,"stop",*selected);BeginRow("Member count");int count=axis.at("count");if(ImGui::SliderInt("##count",&count,1,100)) {axis["count"]=count;changed=true;}EndRow();}
        }ImGui::PopID();
    }
    if(changed) Send({{"operation","batch_settings"},{"spec",spec}});
    Json preview;std::string error;
    try {auto configured=spec;configured["columns"]=d.recorderColumns;preview=BatchPreview(d.document,configured);}catch(const std::exception &e){error=e.what();}
    if(!error.empty())ImGui::TextWrapped("Configuration: %s",error.c_str());
    ImGui::BeginDisabled(!error.empty());if(ImGui::Button("Run batch"))Send({{"operation","run_batch"}});ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(!w->active);if(ImGui::Button("Stop"))Send({{"operation","cancel"}});ImGui::EndDisabled();
    if(w->active) {const auto job=w->jobs->GetJob(w->active);if(job) {ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0);ImGui::TextWrapped("%s",job->currentStage.c_str());}}
    ImGui::TextWrapped("%s",w->status.c_str());
    if(!preview.empty()) {
        ImGui::TextWrapped("Preview: %zu members x %zu biases x (DC + %zu AC frequencies) = %zu samples. Polarity: left minus right.",preview.at("member_count").get<std::size_t>(),preview.at("bias_count").get<std::size_t>(),preview.at("frequency_count").get<std::size_t>(),preview.at("sample_count").get<std::size_t>());
        if(!d.batchResult.empty())ImGui::Text("Estimated time: %.3g s (previous mean/member)",d.batchResult.value("elapsed_s",0.)/std::max(std::size_t(1),d.batchResult.at("rows").size())*preview.at("member_count").get<double>());else ImGui::TextUnformatted("Estimated time: unavailable before first run");
        const auto &biases=preview.at("voltages_V");
        ImGui::Text("Bias: %.5g ... %.5g V | DC + %zu AC frequencies",biases.front().get<double>(),biases.back().get<double>(),preview.at("frequency_count").get<std::size_t>());
        if(ImGui::TreeNode("Bias / frequency samples")) {ImGui::BeginChild("Samples",ImVec2(0,100),ImGuiChildFlags_Borders);for(const auto &v:biases) ImGui::Text("%.7g V",v.get<double>());ImGui::TextWrapped("AC frequencies [Hz]: %s",preview.at("frequencies_Hz").dump().c_str());ImGui::EndChild();ImGui::TreePop();}
        if(ImGui::BeginChild("Combinations",ImVec2(0,140),ImGuiChildFlags_Borders)) for(std::size_t i=0;i<preview.at("combinations").size();++i)ImGui::TextWrapped("%zu: %s",i+1,preview.at("combinations")[i].dump().c_str());ImGui::EndChild();
    }
}
void Panel::Recorder() {
    auto &d=w->Current();auto columns=d.recorderColumns;auto spec=d.batchSpec;bool changed=false;
    ImGui::TextWrapped("Configure before or after batch. Re-extract uses retained curves without solving. Solar metrics and illuminated batches are unavailable in this engine (pending st6); missing values stay flagged.");
    ImGui::BeginDisabled(w->Busy());
    ImGui::SetNextItemOpen(d.batchResult.empty(),ImGuiCond_Once);
    if(ImGui::TreeNode("Quantities and sampling")) {
    for(const char *q:{"Voc","Jsc","FF","eta","J","C","G","Vbi"}) {bool enabled=std::any_of(columns.begin(),columns.end(),[&](const Json &c){return c.at("quantity")==q;});if(Toggle(q,enabled)) {changed=true;columns.erase(std::remove_if(columns.begin(),columns.end(),[&](const Json &c){return c.at("quantity")==q;}),columns.end());if(enabled) {Json c={{"id",q},{"quantity",q}};if(std::string(q)=="J" || std::string(q)=="C" || std::string(q)=="G")c["voltage_V"]=0.;if(std::string(q)=="C" || std::string(q)=="G" || std::string(q)=="Vbi")c["frequency_Hz"]=1e4;columns.push_back(c);}}}
    for(auto &c:columns) {ImGui::PushID(c.at("id").get<std::string>().c_str());if(c.contains("voltage_V"))changed|=Number((c.at("quantity").get<std::string>()+" @ bias [V]").c_str(),c,"voltage_V");if(c.contains("frequency_Hz"))changed|=Number((c.at("quantity").get<std::string>()+" @ f [Hz]").c_str(),c,"frequency_Hz");ImGui::PopID();}
    auto frequencies=Json::array();for(const auto &c:columns)if(c.contains("frequency_Hz") && std::find(frequencies.begin(),frequencies.end(),c["frequency_Hz"])==frequencies.end())frequencies.push_back(c["frequency_Hz"]);spec["frequencies_Hz"]=frequencies;
    if(changed)Send({{"operation","batch_settings"},{"spec",spec},{"columns",columns}});
    ImGui::TreePop();}
    ImGui::BeginDisabled(d.batchResult.empty());if(ImGui::Button("Re-extract Recorder"))Send({{"operation","record_batch"}});ImGui::SameLine();
    if(ImGui::Button("Export all CSV...")) {if(const auto target=SaveFileDialog({"Recorder CSV","csv"},"scapslab-batch.csv"))Send({{"operation","export_batch"},{"path",target->Utf8()}});}ImGui::SameLine();
    if(ImGui::Button("Send to PlotLab...")) {if(const auto target=SaveFileDialog({"PlotLab workbook","plotlab"},"scapslab-batch.plotlab"))Send({{"operation","send_to_plotlab"},{"path",target->Utf8()}});}ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::TextWrapped("%s",w->status.c_str());
    if(d.batchResult.empty()) {ImGui::TextUnformatted("Run a batch to populate the worksheet.");return;}
    const auto &table=d.batchResult;const auto &rows=table.at("rows");
    if(ImGui::BeginTable("Recorder worksheet",int(table.at("columns").size()+4+table.at("spec").at("axes").size()),ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg,ImVec2(0,200))) {
        for(const char *h:{"Member","Status / error","Time [s]","Attempt"})ImGui::TableSetupColumn(h);
        for(const auto &a:table.at("spec").at("axes"))ImGui::TableSetupColumn((a.at("path").get<std::string>()+" ["+a.at("unit").get<std::string>()+"]").c_str());
        for(const auto &c:table.at("columns"))ImGui::TableSetupColumn((c.at("id").get<std::string>()+" ["+c.at("unit").get<std::string>()+"]").c_str());ImGui::TableSetupScrollFreeze(1,1);ImGui::TableHeadersRow();
        for(std::size_t i=0;i<rows.size();++i) {const auto &r=rows[i];ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableNextColumn();if(ImGui::Selectable(r.at("id").get<std::string>().c_str(),d.selectedMember==int(i)))Send({{"operation","batch_select"},{"index",i}});ImGui::TableNextColumn();ImGui::TextWrapped("%s %s %s",r.at("status").get<std::string>().c_str(),r.value("error_type",std::string{}).c_str(),r.value("message",std::string{}).c_str());ImGui::TableNextColumn();ImGui::Text("%.3g",r.at("elapsed_s").get<double>());ImGui::TableNextColumn();ImGui::Text("%d",r.value("attempt",1));for(const auto &a:table.at("spec").at("axes")) {ImGui::TableNextColumn();ImGui::Text("%.6g",r.at("parameters").at(a.at("path").get<std::string>()).get<double>());}for(const auto &c:table.at("columns")) {ImGui::TableNextColumn();const auto id=c.at("id").get<std::string>();const auto &v=r.at("values").at(id);if(v.is_number())ImGui::Text("%.6g",v.get<double>());else ImGui::TextUnformatted("--");ImGui::TextWrapped("%s",r.at("statuses").at(id).get<std::string>().c_str());}ImGui::PopID();}ImGui::EndTable();
    }
    if(table.at("columns").empty())return;recorderPlot=std::clamp(recorderPlot,0,int(table.at("columns").size())-1);BeginRow("Plot scalar");if(ImGui::BeginCombo("##quantity",table.at("columns")[recorderPlot].at("id").get<std::string>().c_str())) {for(int i=0;i<int(table.at("columns").size());++i)if(ImGui::Selectable(table.at("columns")[i].at("id").get<std::string>().c_str(),i==recorderPlot))recorderPlot=i;ImGui::EndCombo();}EndRow();
    const auto &c=table.at("columns")[recorderPlot];const auto &axes=table.at("spec").at("axes");const auto path=axes[0].at("path").get<std::string>();
    if(ImPlot::BeginPlot("Recorder vs parameter",ImVec2(-1,std::max(220.f,ImGui::GetContentRegionAvail().y-5)))) {ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);ImPlot::SetupAxes((path+" ["+axes[0].at("unit").get<std::string>()+"]").c_str(),(c.at("id").get<std::string>()+" ["+c.at("unit").get<std::string>()+"]").c_str(),ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);if(axes[0].value("spacing",std::string{})=="log")ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        std::map<std::string,std::vector<std::pair<double,double>>> groups;for(const auto &r:rows) {std::string label=c.at("id");if(axes.size()==2) {const auto p=axes[1].at("path").get<std::string>();label=p+" = "+r.at("parameters").at(p).dump()+" "+axes[1].at("unit").get<std::string>();}const auto &v=r.at("values").at(c.at("id").get<std::string>());groups[label].push_back({r.at("parameters").at(path).get<double>(),v.is_number() && r.at("statuses").at(c.at("id").get<std::string>())=="ok"?v.get<double>():NAN});}
        for(auto &[label,points]:groups) {std::sort(points.begin(),points.end(),[](auto a,auto b){return a.first<b.first;});std::vector<double>x,y;for(auto [a,b]:points){x.push_back(a);y.push_back(b);}ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),ImPlotSpec(ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,3));}ImPlot::EndPlot();}
}
void Panel::BatchPlots() {
    auto &d=w->Current();if(d.batchResult.empty()) {ImGui::TextUnformatted("Run a batch to overlay its curves.");return;}
    const auto &table=d.batchResult;const auto &rows=table.at("rows");d.selectedMember=std::clamp(d.selectedMember,0,int(rows.size())-1);
    BeginRow("Selected member");if(ImGui::BeginCombo("##member",MemberLabel(rows[d.selectedMember],table.at("spec")).c_str())) {for(int i=0;i<int(rows.size());++i)if(ImGui::Selectable((MemberLabel(rows[i],table.at("spec"))+" | "+rows[i].at("status").get<std::string>()).c_str(),i==d.selectedMember))Send({{"operation","batch_select"},{"index",i}});ImGui::EndCombo();}EndRow();
    const auto &selected=rows[d.selectedMember];ImGui::TextWrapped("%s | %s",selected.at("status").get<std::string>().c_str(),selected.value("message",std::string{}).c_str());
    ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Retry selected"))Send({{"operation","retry_batch"},{"member_id",selected.at("id")}});ImGui::SameLine();if(ImGui::Button("Export selected CSV..."))if(const auto target=SaveFileDialog({"Recorder CSV","csv"},"scapslab-member.csv"))Send({{"operation","export_batch"},{"member_id",selected.at("id")},{"path",target->Utf8()}});ImGui::EndDisabled();
    ImGui::TextWrapped("Colour: %s [%s]. Click near a curve sample to select. Failed members remain listed.",table.at("spec").at("axes")[0].at("path").get<std::string>().c_str(),table.at("spec").at("axes")[0].at("unit").get<std::string>().c_str());
    if(ImPlot::BeginPlot("Batch I-V overlay",ImVec2(-1,-1))) {ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);ImPlot::SetupAxes("Voltage [V] (left minus right)","J [A/m^2]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);double nearest=64;int hit=-1;
        for(int i=0;i<int(rows.size());++i) {const auto &r=rows[i];if(!r.contains("curves"))continue;std::vector<double>x,y;for(const auto &p:r.at("curves").at("iv")) {const double v=p.at("voltage_V");const double j=p.at("J_Am2").is_number() && p.at("status")=="Converged"?p.at("J_Am2").get<double>():NAN;x.push_back(v);y.push_back(j);if(std::isfinite(j) && ImPlot::IsPlotHovered()) {const auto pixel=ImPlot::PlotToPixels(v,j);const auto mouse=ImGui::GetMousePos();const double dist=(pixel.x-mouse.x)*(pixel.x-mouse.x)+(pixel.y-mouse.y)*(pixel.y-mouse.y);if(dist<nearest){nearest=dist;hit=i;}}}ImPlot::PlotLine(MemberLabel(r,table.at("spec")).c_str(),x.data(),y.data(),int(x.size()),ImPlotSpec(ImPlotProp_LineColor,MemberColour(r,table),ImPlotProp_LineWeight,i==d.selectedMember?3.f:2.f));}
        if(hit>=0 && ImGui::IsMouseClicked(0) && !w->Busy())Send({{"operation","batch_select"},{"index",hit}});ImPlot::EndPlot();}
}
}

namespace SST::ScapsLab {
void Panel::GenerationRecombination() {
    auto &device=w->Current();
    auto draft=device.document;if(Structure(draft)) Send({{"operation","edit"},{"document",draft},{"gesture_id",std::to_string(gesture)}});
    if(Toggle("Follow I-V working point",grFollow) && !grFollow) {
        if(device.result.contains("points") && !device.result.at("points").empty()) device.biasVoltage_V=device.result.at("points")[std::clamp(device.selectedBias,0,int(device.result.at("points").size())-1)].at("voltage_V");
        grBias["voltage_V"]=device.biasVoltage_V;device.needsBand=true;device.changedAt=std::chrono::steady_clock::now();
    }
    if(grFollow && device.result.contains("points") && !device.result.at("points").empty()) {
        BeginRow("I-V point");ImGui::SliderInt("##point",&device.selectedBias,0,int(device.result.at("points").size())-1);EndRow();
    }
    if(!grFollow) {
        grBias["voltage_V"]=device.biasVoltage_V;
        const bool changed=Number("Working voltage [V]",grBias,"voltage_V");
        if(changed) {device.biasVoltage_V=grBias.at("voltage_V");device.needsBand=true;device.changedAt=std::chrono::steady_clock::now();}
        if(!w->Busy() && device.needsBand && !ImGui::IsAnyItemActive() && std::chrono::steady_clock::now()-device.changedAt>=std::chrono::milliseconds(250))
            Send({{"operation","band"},{"voltage_V",device.biasVoltage_V}});
        const bool hasVoc=device.result.contains("photovoltaic") && device.result["photovoltaic"].at("Voc_V").is_number();
        ImGui::BeginDisabled(w->Busy() || !hasVoc);
        if(ImGui::Button("Voc")) {grBias["voltage_V"]=device.result["photovoltaic"]["Voc_V"];device.biasVoltage_V=grBias.at("voltage_V");Send({{"operation","band"},{"voltage_V",device.biasVoltage_V}});}
        ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Requires an idle job and a valid refined Voc from a light I-V sweep.");
        ImGui::SameLine();
        const bool hasVmp=device.result.contains("points") && std::any_of(device.result.at("points").begin(),device.result.at("points").end(),[](const Json &p){return p.value("valid",false) && p.at("J_A_m2").is_number();});
        ImGui::BeginDisabled(w->Busy() || !hasVmp);
        if(ImGui::Button("Vmp")) {
            double best=-1;for(const auto &p:device.result.at("points")) if(p.value("valid",false) && p.at("J_A_m2").is_number()) {
                const double power=-p.at("voltage_V").get<double>()*p.at("J_A_m2").get<double>();
                if(power>best) {best=power;grBias["voltage_V"]=p.at("voltage_V");}
            }
            device.biasVoltage_V=grBias.at("voltage_V");Send({{"operation","band"},{"voltage_V",device.biasVoltage_V}});
        }
        ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Requires an idle job and valid I-V points. Selects the sampled point with maximum delivered power.");
    }
    const auto &snapshot=grFollow?device.result:device.bandResult;
    if(!snapshot.contains("points") || snapshot.at("points").empty()) {ImGui::TextWrapped("Calculate I-V or select a working voltage. %s",w->status.c_str());return;}
    const int index=grFollow?std::clamp(device.selectedBias,0,int(snapshot.at("points").size())-1):0;
    const auto &point=snapshot.at("points")[index];
    if(!point.contains("generation_recombination")) {ImGui::TextUnformatted("Recalculate this snapshot to obtain G/R.");return;}
    const auto &gr=point.at("generation_recombination");
    ImGui::TextWrapped("%s | terminal %.6g V | internal %.6g V | %.5g K | solver %s | profile validity %s",point.at("bias_id").get<std::string>().c_str(),point.at("terminal_voltage_V").get<double>(),point.at("internal_voltage_V").get<double>(),snapshot.at("project_snapshot").at("temperature_K").get<double>(),point.at("status").get<std::string>().c_str(),point.at("validity_status").get<std::string>().c_str());
    ImGui::TextWrapped("%s | illumination %s | %s",device.id.c_str(),snapshot.at("project_snapshot").value("illumination",Json::object()).value("enabled",false)?"on":"off",w->status.c_str());
    ImGui::TextWrapped("Terminal budget: %s | relative error %.5g",gr.value("terminal_budget_valid",false)?"valid <= 1e-6":"NOT VALID",gr.at("terminal_budget_relative_error").is_number()?gr.at("terminal_budget_relative_error").get<double>():NAN);
    ImGui::BeginDisabled(w->Busy());
    for(const auto *format:{"csv","json"}) {
        if(ImGui::Button((std::string("Export G/R ")+format).c_str())) {
            try {if(const auto path=SaveFileDialog({"G/R results",format},(std::string("generation-recombination.")+format).c_str())) Send({{"operation","export_gr"},{"path",path->Utf8()},{"format",format},{"source",grFollow?"iv":"band"},{"point_index",index}});}
            catch(const std::exception &e){w->status=e.what();}
        }ImGui::SameLine();
    }ImGui::NewLine();ImGui::EndDisabled();
    const bool valid=point.value("valid",false);
    if(!ImGui::BeginTabBar("G/R views"))return;
    const bool budgetShot=gallery && std::getenv("SCAPSLAB_GR_TAB") && std::string(std::getenv("SCAPSLAB_GR_TAB"))=="1";
    if(ImGui::BeginTabItem("Profiles",nullptr,gallery && !budgetShot?ImGuiTabItemFlags_SetSelected:0)) {
    bool showGeneration=!grHidden.contains("generation");
    if(ImGui::CollapsingHeader("Visible mechanisms")) {if(Toggle("Generation",showGeneration)) {if(showGeneration)grHidden.erase("generation");else grHidden.insert("generation");}
    for(const auto *mechanism:{"srh_bulk","interface","radiative","auger"}) {bool show=!grHidden.contains(mechanism);if(Toggle(mechanism,show)){if(show)grHidden.erase(mechanism);else grHidden.insert(mechanism);}}
    for(const auto &c:gr.at("channels")) ImGui::TextWrapped("%s / %s: %s",c.at("mechanism").get<std::string>().c_str(),c.at("id").get<std::string>().c_str(),c.at("validity_status").get<std::string>().c_str());
    }
    if(!valid) ImGui::TextWrapped("Invalid working point: profiles are masked; raw values remain available in export.");
    ImGui::TextWrapped("Log plot shows positive rates only; signed rates and integrals are retained in export. Sheets use the right axis [m^-2 s^-1].");
    if(ImPlot::BeginPlot("G(x), R(x)",ImVec2(-1,280))) {
        ImPlot::SetupAxes("Position [um]","Bulk rate [m^-3 s^-1]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxis(ImAxis_Y2,"Sheet rate [m^-2 s^-1]",ImPlotAxisFlags_AuxDefault|ImPlotAxisFlags_AutoFit);ImPlot::SetupAxisScale(ImAxis_Y2,ImPlotScale_Log10);
        auto plot=[&](const std::string &name,const Json &xx,const Json &yy,bool sheet,bool curveValid) {
            auto x=xx.get<std::vector<double>>(),y=yy.get<std::vector<double>>();if(x.size()!=y.size())return;
            for(auto &v:x)v*=1e6;for(auto &v:y)if(!curveValid || !std::isfinite(v) || v<=0)v=NAN;
            ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);
            if(sheet)ImPlot::PlotScatter(name.c_str(),x.data(),y.data(),int(x.size()));else ImPlot::PlotLine(name.c_str(),x.data(),y.data(),int(x.size()));
        };
        if(showGeneration)plot("G",gr.at("x_m"),gr.at("generation_m3_s"),false,valid);
        for(const auto &c:gr.at("channels")) if(!grHidden.contains(c.at("mechanism").get<std::string>())) {
            const bool sheet=!c.at("R_sheet_m2_s").empty();plot(c.at("mechanism").get<std::string>()+" / "+c.at("id").get<std::string>(),c.at("x_m"),c.at(sheet?"R_sheet_m2_s":"R_m3_s"),sheet,c.value("valid",false));
        }
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);double boundary=0;
        for(const auto &l:snapshot.at("project_snapshot").at("layers")) {boundary+=l.at("thickness_m").get<double>()*1e6;ImPlot::PlotInfLines((l.at("scapslab_id").get<std::string>()+" boundary").c_str(),&boundary,1);}
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Integrated currents",nullptr,budgetShot?ImGuiTabItemFlags_SetSelected:0)) {
    ImGui::TextWrapped("q integral G = %.8g A/m^2 | q integral R = %.8g A/m^2 | net = %.8g A/m^2",gr.at("generation_current_A_m2").get<double>(),gr.at("recombination_current_A_m2").get<double>(),gr.at("net_generation_current_A_m2").get<double>());
    ImGui::TextWrapped("Terminal budget (extracted -J): %s | relative error %.5g. Contacts/injection and shunts can contribute.",gr.value("terminal_budget_valid",false)?"valid <= 1e-6":"NOT VALID",gr.at("terminal_budget_relative_error").is_number()?gr.at("terminal_budget_relative_error").get<double>():NAN);
    const float tableHeight=std::clamp(ImGui::GetContentRegionAvail().y-210.f,80.f,150.f);
    if(ImGui::BeginTable("Integrated losses",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,tableHeight))) {
        for(const auto *name:{"Layer / defect ID","Mechanism","J_R [A/m^2]","Profile validity"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
        for(const auto &c:gr.at("channels")) for(const auto &l:c.at("by_layer")) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s / %s",l.at("layer_id").get<std::string>().c_str(),c.at("id").get<std::string>().c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(c.at("mechanism").get<std::string>().c_str());ImGui::TableNextColumn();ImGui::Text("%.9g",l.at("current_A_m2").get<double>());ImGui::TableNextColumn();ImGui::TextUnformatted(c.at("validity_status").get<std::string>().c_str());
        }
        for(const auto &l:gr.at("layers")) {ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(l.at("layer_id").get<std::string>().c_str());ImGui::TableNextColumn();ImGui::TextUnformatted("TOTAL");ImGui::TableNextColumn();ImGui::Text("%.9g",l.at("recombination_current_A_m2").get<double>());}
        ImGui::EndTable();
    }
    ImGui::TextUnformatted("Loss breakdown | signed A/m^2 | sheets assigned to left layer");
    if(ImPlot::BeginPlot("Integrated loss breakdown",ImVec2(-1,150))) {
        ImPlot::SetupAxes("Mechanism index","J_R [A/m^2]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(ImPlotLocation_North,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        int i=0;for(const auto &c:gr.at("channels")) {const double x=i++,y=valid?c.at("integrated_current_A_m2").get<double>():NAN;ImPlot::PlotBars((c.at("mechanism").get<std::string>()+" / "+c.at("id").get<std::string>()).c_str(),&x,&y,1,.7);}
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
}
