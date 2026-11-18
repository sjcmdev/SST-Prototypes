#include "Panel.hpp"
#include <imgui.h>
#include <implot.h>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace SST::ScapsLab {
static const char *names[]={"Project","Layers","Contacts","Solver","Run","Results"};
Panel::Panel(Workflow *w,int v,bool gallery):EditorWindow(std::string("scapslab.")+names[v],names[v],true),w(w),view(v),gallery(gallery) {
    auto s=GetState(); s.x=gallery?25.f:10.f+float(v%3)*415.f; s.y=gallery?50.f:45.f+float(v/3)*335.f;
    if(gallery) s.mode=WindowMode::Maximized;
    s.width=gallery?1100.f:405.f; s.height=gallery?650.f:325.f; ApplyState(s);
    std::snprintf(file,sizeof(file),"%s",w->path.c_str());
}
void Panel::Send(const Json &r) { const auto response=w->Dispatch(r); if(!response.at("ok").get<bool>()) w->status=response.at("error").at("message"); }
static bool Number(const char *label,Json &j,const char *key,double scale=1) {
    const auto &n=j.at(key); double value=n.is_number()?n.get<double>()*scale:0;
    char text[96]; std::snprintf(text,sizeof(text),"%.9g",value);
    if(ImGui::InputText(label,text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) {
        try {std::size_t used;value=std::stod(text,&used);if(used!=std::strlen(text)) throw std::invalid_argument("number");
            j[key]=n.is_number_integer() && std::floor(value/scale)==value/scale?Json(std::int64_t(value/scale)):Json(value/scale);
        } catch(...) {j[key]=text;} return true;
    } return false;
}
static bool Choice(const char *label,Json &j,const char *key,std::initializer_list<std::pair<const char*,const char*>> items) {
    const auto current=j.at(key).get<std::string>(); bool changed=false;
    const char *preview=current.c_str(); for(const auto &[name,value]:items) if(current==value) preview=name;
    if(ImGui::BeginCombo(label,preview)) {for(const auto &[name,value]:items) if(ImGui::Selectable(name,current==value)) {j[key]=value;changed=true;} ImGui::EndCombo();} return changed;
}
void Panel::RenderContent() {
    if(!placed && !gallery) {auto s=GetState();const auto *vp=ImGui::GetMainViewport();s.x+=vp->Pos.x;s.y+=vp->Pos.y;ApplyState(s);placed=true;}
    ImGui::TextDisabled("SCAPSLAB / %s",names[view]);
    ImGui::TextWrapped("%s",w->status.c_str()); ImGui::Separator();
    if(view==5) {Results();return;}
    ImGui::BeginDisabled(w->Busy());
    if(view==0) {
        ImGui::InputText("YAML path",file,sizeof(file));
        if(w->dirty) ImGui::TextColored(ImVec4(1,.7f,.3f,1),"Unsaved changes");
        if(ImGui::Button("Load project")) { if(w->dirty) ImGui::OpenPopup("Discard unsaved changes?"); else Send({{"operation","load"},{"path",file}}); }
        if(ImGui::BeginPopupModal("Discard unsaved changes?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Loading replaces the current unsaved project.");
            if(ImGui::Button("Discard and load")) {Send({{"operation","load"},{"path",file}});ImGui::CloseCurrentPopup();} ImGui::SameLine();
            if(ImGui::Button("Keep editing")) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        ImGui::SameLine(); if(ImGui::Button("Save project")) Send({{"operation","save"},{"path",file}});
        if(!w->document.is_null()) {
            ImGui::Text("Case: %s",w->document.at("case_id").get<std::string>().c_str());
            ImGui::Text("Layers: %d",int(w->document.at("layers").size()));
            ImGui::TextWrapped("Full YAML mapping is retained, including defects, interfaces, illumination and external elements. Layer IDs persist in scapslab_id.");
            ImGui::TextDisabled("Numeric edits: press Enter to apply.");
        }
    } else if(w->document.is_object()) {
        auto draft=w->document; bool changed=false;
        if(view==1) {
            auto &layers=draft["layers"]; selected=std::clamp(selected,0,int(layers.size())-1);
            if(ImGui::BeginCombo("Layer",layers[selected].at("name").get<std::string>().c_str())) {
                for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i); if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),selected==i)) selected=i; ImGui::PopID();} ImGui::EndCombo();
            }
            auto &l=layers[selected]; const auto id=l.at("scapslab_id");
            if(ImGui::Button("Add")) Send({{"operation","layer"},{"action","add"}}); ImGui::SameLine();
            if(ImGui::Button("Remove")) Send({{"operation","layer"},{"action","remove"},{"layer_id",id}}); ImGui::SameLine();
            if(ImGui::Button("Up") && selected>0) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected-1}}); ImGui::SameLine();
            if(ImGui::Button("Down") && selected+1<int(layers.size())) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected+1}});
            ImGui::TextDisabled("Stable ID: %s",id.get<std::string>().c_str());
            char name[256]; std::snprintf(name,sizeof(name),"%s",l.at("name").get<std::string>().c_str());
            if(ImGui::InputText("Name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)) {l["name"]=name;changed=true;}
            changed|=Number("Thickness [µm]",l,"thickness_m",1e6);
            auto &m=l["material"];
            changed|=Number("Relative permittivity",m,"eps_r"); changed|=Number("Band gap [eV]",m,"Eg_eV"); changed|=Number("Affinity [eV]",m,"chi_eV");
            changed|=Number("Nc [cm^-3]",m,"Nc_ref_m3",1e-6); changed|=Number("Nv [cm^-3]",m,"Nv_ref_m3",1e-6);
            changed|=Number("Electron mobility [cm^2/(V s)]",m,"mu_n_m2_V_s",1e4); changed|=Number("Hole mobility [cm^2/(V s)]",m,"mu_p_m2_V_s",1e4);
            changed|=Number("Donors [cm^-3]",l["doping"],"Nd_m3",1e-6); changed|=Number("Acceptors [cm^-3]",l["doping"],"Na_m3",1e-6);
        } else if(view==2) {
            for(const auto *side:{"left","right"}) {
                ImGui::PushID(side); ImGui::SeparatorText(side); auto &c=draft["contacts"][side];
                changed|=Number("Work function [eV]",c,"phi_m_eV");
                int mode=c.contains("flat_band")?(c["flat_band"].get<bool>()?2:1):0;
                if(ImGui::Combo("Boundary mode",&mode,"Legacy default\0Work function\0Flat band\0")) {if(mode==0)c.erase("flat_band");else c["flat_band"]=mode==2;changed=true;}
                for(const auto *key:{"Sn_m_s","Sp_m_s"}) {
                    ImGui::PushID(key); bool infinite=c[key].is_string();
                    if(ImGui::Checkbox("Ideal (infinite S)",&infinite)) {c[key]=infinite?Json(".inf"):Json(1e5);changed=true;}
                    ImGui::SameLine(); ImGui::TextUnformatted(key);
                    if(!infinite) changed|=Number("Surface velocity [m/s]",c,key); ImGui::PopID();
                }
                ImGui::PopID();
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
            changed|=Number("Step [V]",draft["voltage"],"step_V"); changed|=Number("Temperature [K]",draft,"temperature_K");
            ImGui::TextWrapped("Applied voltage: left minus right. Signed J follows the scaps_core terminal-current convention.");
            if(ImGui::Button("Run I-V sweep")) Send({{"operation","run"}});
        }
        if(changed) Send({{"operation","edit"},{"document",draft}});
    }
    ImGui::EndDisabled();
    if(view==4 && w->active) {
        const auto j=w->jobs->GetJob(w->active); if(j) {ImGui::ProgressBar(j->totalWork>0?j->completedWork/j->totalWork:0);ImGui::TextWrapped("%s",j->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
        ImGui::TextWrapped("Cancellation takes effect after mesh preparation or the current Newton solve.");
    }
}
void Panel::Results() {
    if(!w->result.contains("points") || w->result["points"].empty()) {ImGui::TextUnformatted("Run a sweep to view results.");return;}
    const auto &points=w->result.at("points"); bias=std::clamp(bias,0,int(points.size())-1);
    ImGui::SliderInt("Bias point",&bias,0,int(points.size())-1); const auto &p=points[bias];
    ImGui::Text("V = %.5g V | %s | %d iterations",p.at("voltage_V").get<double>(),p.at("status").get<std::string>().c_str(),p.at("iterations").get<int>());
    ImGui::Text("Residual %.3e | conservation error %.3e A/m²",p.at("residual").get<double>(),p.at("conservation_error_A_m2").get<double>());
    ImGui::Checkbox("|J| logarithmic",&absoluteLog);
    if(p.at("J_A_m2").is_null()) {ImGui::TextWrapped("This bias did not converge; profiles are unavailable. Select a converged bias."); return;}
    const float h=std::max(95.f,(ImGui::GetContentRegionAvail().y-30.f)/3.f);
    auto x=p.at("x_m").get<std::vector<double>>(); for(auto &v:x)v*=1e6;
    if(ImPlot::BeginPlot("Bands",ImVec2(-1,h))) {ImPlot::SetupAxes("Position [µm]","Energy [eV]");ImPlot::SetupLegend(ImPlotLocation_NorthEast,ImPlotLegendFlags_Horizontal);
        for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) {const auto y=p.at(key).get<std::vector<double>>(); if(y.size()==x.size())ImPlot::PlotLine(key,x.data(),y.data(),int(x.size()));} ImPlot::EndPlot();}
    if(ImPlot::BeginPlot("Carriers",ImVec2(-1,h))) {ImPlot::SetupAxes("Position [µm]","Density [cm^-3]");ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        for(const auto *key:{"n_m3","p_m3"}) {auto y=p.at(key).get<std::vector<double>>();for(auto &v:y)v*=1e-6;if(y.size()==x.size())ImPlot::PlotLine(key==std::string("n_m3")?"n":"p",x.data(),y.data(),int(x.size()));}ImPlot::EndPlot();}
    std::vector<double> v,j; for(const auto &pt:points) {v.push_back(pt.at("voltage_V"));const double current=pt.at("J_A_m2").is_number()?pt.at("J_A_m2").get<double>():NAN;j.push_back(absoluteLog?std::abs(current):current);}
    if(ImPlot::BeginPlot("I-V",ImVec2(-1,h))) {ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m²]":"Signed J [A/m²]",ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        if(absoluteLog)ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);ImPlot::PlotLine("I-V",v.data(),j.data(),int(v.size()));ImPlot::EndPlot();}
}
}





