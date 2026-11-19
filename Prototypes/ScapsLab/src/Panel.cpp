#include "Panel.hpp"
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <implot.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui_internal.h>
namespace SST::ScapsLab {
static const char *names[]={"Project","Layers","Contacts","Solver","Run","Results","C-V","Mott-Schottky","C-f","G-omega-f","I-V"};
Panel::Panel(Workflow *w,int v,bool gallery):EditorWindow(std::string("scapslab.")+names[v],names[v],true),w(w),view(v),gallery(gallery) {
    auto s=GetState(); s.x=gallery?25.f:10.f+float(v%3)*415.f; s.y=gallery?50.f:45.f+float(v/3)*335.f;
    if(gallery) s.mode=WindowMode::Maximized;
    s.width=gallery?1100.f:405.f; s.height=gallery?650.f:325.f; ApplyState(s);
    acRequest={{"operation",v<8?"cv":"cf"},{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.05},{"frequency_Hz",1e4},
        {"voltage_V",0.0},{"start_Hz",10.0},{"stop_Hz",1e7},{"points",61},{"temperature_K",w->document.value("temperature_K",300.0)}};
    std::snprintf(file,sizeof(file),"%s",w->path.c_str());
}
void Panel::Send(const Json &r) { const auto response=w->Dispatch(r); if(!response.at("ok").get<bool>()) {w->status=response.at("error").at("message");w->response=response;} }
namespace {
std::uint64_t gesture=0;
void TrackGesture() {if(ImGui::IsItemActivated()) ++gesture;}
bool Toggle(const char *label,bool &value) {sst::ui::widgets::BeginLabelledRow(label);const bool changed=ImGui::Checkbox("##value",&value);TrackGesture();ImGui::EndTable();return changed;}
void TextInput(const char *label,char *text,std::size_t size) {
    sst::ui::widgets::BeginLabelledRow(label);ImGui::InputText("##value",text,size,ImGuiInputTextFlags_AutoSelectAll);ImGui::EndTable();
}
}
static bool Number(const char *label,Json &j,const char *key,double scale=1) {
    const auto &n=j.at(key); if(!n.is_number()) return false;
    const std::string k=key; double value=n.get<double>()*scale;
    const bool logarithmic=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3" || k=="Sn_m_s" || k=="Sp_m_s" ||
        k=="residual_tolerance" || k=="adaptive_target_relative_J_error" || k=="frequency_Hz" || k=="start_Hz" || k=="stop_Hz";
    sst::ui::widgets::BeginLabelledRow(label); bool changed=false;
    if(logarithmic) {
        const bool density=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3";
        const bool zeroAllowed=k=="Nd_m3" || k=="Na_m3" || k=="Sn_m_s" || k=="Sp_m_s";
        bool zero=value==0;
        if(zeroAllowed) {ImGui::TextUnformatted("Zero");ImGui::SameLine();changed=ImGui::Checkbox("##zero",&zero);TrackGesture();}
        int exponent=value>0?int(std::floor(std::log10(value))):(density?14:0);
        float mantissa=value>0?float(value/std::pow(10.,exponent)):1.f;
        ImGui::BeginDisabled(zero);
        ImGui::SetNextItemWidth(std::max(40.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.64f));
        changed|=ImGui::SliderFloat("##mantissa",&mantissa,1.f,9.99f,"%.2f x 10^");TrackGesture();ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);changed|=ImGui::SliderInt("##exponent",&exponent,density?0:-16,density?23:12);TrackGesture();
        ImGui::EndDisabled();
        if(zero) ImGui::TextDisabled("Zero value: clear Zero to edit mantissa/exponent.");
        if(changed) value=zero?0:double(mantissa)*std::pow(10.,exponent);
    } else if(k=="points_per_layer" || k=="points" || k=="max_newton_iterations") {
        int v=int(value);const int minimum=k=="points_per_layer"?3:k=="points"?2:1;
        changed=ImGui::SliderInt("##value",&v,minimum,k=="max_newton_iterations"?1000:2000);TrackGesture();value=v;
    } else {
        double lo=0.0,hi=10.0;
        if(k=="thickness_m") {lo=0.01;hi=50;}
        else if(k=="eps_r") {lo=1;hi=40;}
        else if(k=="Eg_eV") {lo=0.1;hi=5;}
        else if(k=="chi_eV" || k=="phi_m_eV") {lo=1;hi=7;}
        else if(k=="mu_n_m2_V_s" || k=="mu_p_m2_V_s") {lo=0.1;hi=5000;}
        else if(k=="temperature_K") {lo=100;hi=600;}
        else if(k=="step_V") {lo=0.001;hi=0.2;}
        else {lo=-5;hi=5;}
        changed=ImGui::SliderScalar("##value",ImGuiDataType_Double,&value,&lo,&hi,"%.5g");TrackGesture();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+click to enter an exact value. Values are validated before applying.");
    ImGui::EndTable();
    if(changed) j[key]=(k=="points_per_layer" || k=="points" || k=="max_newton_iterations")?Json(std::int64_t(value/scale)):Json(value/scale);
    return changed;
}
static bool Choice(const char *label,Json &j,const char *key,std::initializer_list<std::pair<const char*,const char*>> items) {
    const auto current=j.at(key).get<std::string>(); bool changed=false;
    const char *preview=current.c_str(); for(const auto &[name,value]:items) if(current==value) preview=name;
    sst::ui::widgets::BeginLabelledRow(label);
    if(ImGui::BeginCombo("##value",preview)) {for(const auto &[name,value]:items) if(ImGui::Selectable(name,current==value)) {j[key]=value;changed=true;++gesture;} ImGui::EndCombo();} ImGui::EndTable();return changed;
}
void Panel::RenderContent() {
    if(!placed && !gallery) {auto s=GetState();const auto *vp=ImGui::GetMainViewport();s.x+=vp->Pos.x;s.y+=vp->Pos.y;ApplyState(s);placed=true;}
    ImGui::TextDisabled("SCAPSLAB / %s",names[view]);
    if(view==4 || view==5 || (w->response.is_object() && !w->response.value("ok",true))) ImGui::TextWrapped("%s",w->status.c_str()); ImGui::Separator();
    if(view==5 || view==10) {Results();return;}
    if(view>=6 && view<10) {AC();return;}
    const bool replacing=w->pending && (w->pending->request.at("operation")=="load" || w->pending->request.at("operation")=="layer");
    ImGui::BeginDisabled(replacing);
    if(view==0) {
        TextInput("YAML path",file,sizeof(file));
        if(ImGui::Button("Undo")) Send({{"operation","undo"}});ImGui::SameLine();
        if(ImGui::Button("Redo")) Send({{"operation","redo"}});
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
            ImGui::TextDisabled("Ctrl+click sliders for exact values. Ctrl+A selects all text.");
        }
    } else if(w->document.is_object()) {
        auto draft=w->document;
        if(!w->edits.empty()) draft=w->edits.back().at("document");
        else if(w->pending && w->pending->request.value("operation",std::string{})=="edit" && w->pending->request.contains("document")) draft=w->pending->request.at("document");
        bool changed=false;
        if(view==1) {
            auto &layers=draft["layers"]; auto &selected=w->selectedLayer; selected=std::clamp(selected,0,int(layers.size())-1);
            sst::ui::widgets::BeginLabelledRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {
                for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i); if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),selected==i)) selected=i; ImGui::PopID();} ImGui::EndCombo();
            }
            ImGui::EndTable();
            auto &l=layers[selected]; const auto id=l.at("scapslab_id");
            if(ImGui::Button("Add")) Send({{"operation","layer"},{"action","add"}}); ImGui::SameLine();
            if(ImGui::Button("Remove")) Send({{"operation","layer"},{"action","remove"},{"layer_id",id}}); ImGui::SameLine();
            if(ImGui::Button("Up") && selected>0) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected-1}}); ImGui::SameLine();
            if(ImGui::Button("Down") && selected+1<int(layers.size())) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected+1}});
            ImGui::TextDisabled("Stable ID: %s",id.get<std::string>().c_str());
            char name[256]; std::snprintf(name,sizeof(name),"%s",l.at("name").get<std::string>().c_str());
            sst::ui::widgets::BeginLabelledRow("Name");if(ImGui::InputText("##name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {l["name"]=name;changed=true;++gesture;}ImGui::EndTable();
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
                sst::ui::widgets::BeginLabelledRow("Boundary mode");if(ImGui::Combo("##mode",&mode,"Legacy default\0Work function\0Flat band\0")) {if(mode==0)c.erase("flat_band");else c["flat_band"]=mode==2;changed=true;++gesture;}ImGui::EndTable();
                for(const auto *key:{"Sn_m_s","Sp_m_s"}) {
                    ImGui::PushID(key); bool infinite=c[key].is_string();
                    if(Toggle(key==std::string("Sn_m_s")?"Infinite Sn [m/s]":"Infinite Sp [m/s]",infinite)) {c[key]=infinite?Json(".inf"):Json(1e5);changed=true;}
                    
                    if(!infinite) changed|=Number("Surface velocity [m/s]",c,key);
                    else ImGui::TextDisabled("S is infinite: clear the toggle to edit velocity [m/s]."); ImGui::PopID();
                }
                ImGui::PopID();
            }
            const auto &computed=w->bandResult.empty()?w->result:w->bandResult;
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
            changed|=Number("Step [V]",draft["voltage"],"step_V"); changed|=Number("Temperature [K]",draft,"temperature_K");
            ImGui::TextWrapped("Applied voltage: left minus right. Signed J follows the scaps_core terminal-current convention.");
            bool automatic=w->autoRecalculate;
            if(Toggle("Auto-recalculate",automatic)) Send({{"operation","auto_recalculate"},{"enabled",automatic}});
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::Button("Calculate I-V")) Send({{"operation","run"}});
            ImGui::EndDisabled();
            bool bandAutomatic=w->autoRecalculateBand;
            if(Toggle("Auto-recalculate band",bandAutomatic)) Send({{"operation","auto_recalculate"},{"enabled",w->autoRecalculate},{"band_enabled",bandAutomatic}});
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::Button("Recalculate band")) Send({{"operation","band"},{"voltage_V",w->biasVoltage_V}});
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
void Panel::Results() {
    const bool bandOnly=view==5 && w->result.empty() && !w->bandResult.empty();
    const auto &result=bandOnly?w->bandResult:w->result;
    const auto &layers=w->document.at("layers");
    double total=0;for(const auto &l:layers) total+=l.at("thickness_m").get<double>();
    if(view==5) {
        const float width=ImGui::GetContentRegionAvail().x; const auto start=ImGui::GetCursorScreenPos();
        double offset=0;
        for(int i=0;i<int(layers.size());++i) {
            const auto &l=layers[i];const float part=float(l.at("thickness_m").get<double>()/total)*width;
            ImGui::SetCursorScreenPos(ImVec2(start.x+float(offset/total)*width,start.y));ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button,w->selectedLayer==i?ImVec4(.3f,.5f,.65f,1):ImVec4(.16f+.08f*(i%3),.32f+.08f*(i%2),.46f,1));
            if(ImGui::Button("##layer-stack",ImVec2(std::max(1.f,part-1.f),30))) {w->selectedLayer=i;w->focusLayers=true;}
            if(part>80) ImGui::GetWindowDrawList()->AddText(ImVec2(start.x+float(offset/total)*width+6,start.y+7),IM_COL32_WHITE,l.at("name").get<std::string>().c_str());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %.5g um. Click to select layer.",l.at("name").get<std::string>().c_str(),l.at("thickness_m").get<double>()*1e6);
            ImGui::PopStyleColor();ImGui::PopID();offset+=l.at("thickness_m").get<double>();
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x,start.y+36));
    }
    if(!result.contains("points") || result["points"].empty()) {ImGui::TextUnformatted("Calculate I-V to view results.");return;}
    const auto &points=result.at("points");int bias=bandOnly?0:w->selectedBias;bias=std::clamp(bias,0,int(points.size())-1);
    sst::ui::widgets::BeginLabelledRow("Bias point");ImGui::SliderInt("##bias",&bias,0,int(points.size())-1);ImGui::EndTable();if(!bandOnly)w->selectedBias=bias;
    const auto &band=w->bandResult;
    const bool preview=view==5 && !band.empty() && band.at("points")[0].at("voltage_V")==points[bias].at("voltage_V");
    const auto &p=preview?band.at("points")[0]:points[bias];
    const auto &metrics=preview?band:result;
    ImGui::Text("%.2f ms | %d mesh nodes",metrics.value("compute_time_ms",0.0),metrics.value("mesh_node_count",0));
    w->biasVoltage_V=p.at("voltage_V");
    ImGui::Text("V = %.5g V | %s | %d iterations",p.at("voltage_V").get<double>(),p.at("status").get<std::string>().c_str(),p.at("iterations").get<int>());
    if(view==10) {
        Toggle("Log |J| (off: signed J)",absoluteLog);
        std::vector<double> v,j; int failed=0;
        for(const auto &pt:points) {
            v.push_back(pt.at("voltage_V"));const bool valid=pt.at("status")=="Converged" && pt.at("J_A_m2").is_number();
            if(!valid) ++failed;
            const double current=valid?pt.at("J_A_m2").get<double>():NAN;
            j.push_back(absoluteLog?(current==0?NAN:std::abs(current)):current);
        }
        ImGui::Text("%d / %d converged; failed points masked",int(points.size())-failed,int(points.size()));
        if(ImGui::CollapsingHeader("Per-point convergence")) {
            for(const auto &pt:points) ImGui::Text("%s | %.5g V | %s",pt.at("bias_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("status").get<std::string>().c_str());
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
            w->selectedBias=bias;w->biasVoltage_V=v[bias];
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
    ImGui::BeginDisabled(w->Busy());
    ImGui::SetNextItemWidth(130); Number("Temperature [K]",acRequest,"temperature_K");
    if(ImGui::CollapsingHeader("Sweep settings",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(cv) {
            Number("Start [V]",acRequest,"start_V"); Number("Stop [V]",acRequest,"stop_V"); Number("Step [V]",acRequest,"step_V"); Number("Frequency [Hz]",acRequest,"frequency_Hz");
        } else {
            Number("Bias [V]",acRequest,"voltage_V"); Number("Start [Hz]",acRequest,"start_Hz"); Number("Stop [Hz]",acRequest,"stop_Hz"); Number("Log points",acRequest,"points");
        }
    }
    if(ImGui::Button(cv?"Run C-V":"Run C-f")) Send(acRequest);
    ImGui::SameLine(); ImGui::SetNextItemWidth(220); TextInput("CSV path",csvFile,sizeof(csvFile)); ImGui::SameLine();
    if(ImGui::Button("Export CSV")) Send({{"operation","export_ac"},{"kind",kind},{"path",csvFile}});
    ImGui::EndDisabled();
    if(w->Busy()) {
        ImGui::TextDisabled("AC controls disabled while a job is active; wait or Cancel.");
        const auto job=w->jobs->GetJob(w->active); if(job) {ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0); ImGui::TextWrapped("%s",job->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
    }
    if(!w->acResults.contains(kind)) {ImGui::TextWrapped("Run the sweep to view AC results. Voltage: left minus right.");return;}
    const auto &data=w->acResults.at(kind); const auto &points=data.at("points");
    const auto &first=points.front();
    ImGui::Text("Result: %.6g K | %s = %.6g %s | %d points",first.at("temperature_K").get<double>(),cv?"f":"V",first.at(cv?"frequency_Hz":"voltage_V").get<double>(),cv?"Hz":"V",int(points.size()));
    std::vector<double> x,y,g; int failed=0;
    const char *key=view==7?"inverse_C2_F_minus2":view==9?"G_over_omega_F":"capacitance_F";
    for(const auto &pt:points) {
        const bool valid=pt.at("status")=="Converged"; if(!valid) ++failed;
        x.push_back(pt.at(cv?"voltage_V":"frequency_Hz")); y.push_back(valid && pt.at(key).is_number()?pt.at(key).get<double>():NAN);
        g.push_back(valid && pt.at("G_over_omega_F").is_number()?pt.at("G_over_omega_F").get<double>():NAN);
    }
    if(failed) ImGui::TextWrapped("%d points did not converge; gaps are retained in plots and CSV.",failed);
    if(ImGui::CollapsingHeader("Per-point convergence"))
        for(const auto &pt:points) ImGui::Text("%s | %.5g V | %.5g Hz | %s",pt.at("point_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("frequency_Hz").get<double>(),pt.at("status").get<std::string>().c_str());
    const char *ylabel=view==7?"1/C^2 [F^-2]":view==9?"G/omega [F]":"C [F]";
    if(ImPlot::BeginPlot(names[view],ImVec2(-1,-1))) {
        ImPlot::SetupAxes(cv?"Voltage [V]":"Frequency [Hz]",ylabel,ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
        if(view==6 || view==8) ImPlot::SetupAxis(ImAxis_Y2,"G/omega [F]",ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_Y1,view==8?"%.8g":"%.3g");
        if(!cv) ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        ImPlot::PlotLine(ylabel,x.data(),y.data(),int(x.size()));
        if(view==6 || view==8) {ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlot::PlotLine("G/omega [F]",x.data(),g.data(),int(x.size()));}
        ImPlot::EndPlot();
    }
}
}





