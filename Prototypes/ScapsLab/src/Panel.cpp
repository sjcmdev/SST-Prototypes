#include "Panel.hpp"
#include "Domain/ParameterBounds.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include <numbers>
#include "PlotUi.hpp"
#include <imgui.h>
#include <scaps_core/grading.hpp>
#include <scaps_core/illumination.hpp>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <implot.h>
#include <implot_internal.h>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <tuple>
#include <map>
#include <imgui_internal.h>
#include "Core/Platform/NativeFileDialog.hpp"
#include "Vendor/ImGuiNotify/win32Example/fonts/IconsFontAwesome6.h"
namespace SST {void PerfAdd(const std::string &name, double ms);}
namespace SST::ScapsLab {
static const char *names[]={"Project","Layers","Contacts","Solver","Run","Band diagram & profiles","C-V","Mott-Schottky","C-f","G-omega-f","I-V","Defects","Interfaces","Rs-Rsh","Structure","History","Batch","Recorder","Batch overlay","Illumination","Absorption","Grading","Generation-Recombination"};
Panel::Panel(Workflow *w,int v,bool gallery):EditorWindow(std::string("scapslab.")+names[v],v==18?"Results":names[v],true),w(w),view(v),gallery(gallery) {
    auto s=GetState(); s.x=gallery?25.f:10.f+float(v%3)*415.f; s.y=gallery?50.f:45.f+float(v/3)*335.f;
    if(gallery) s.mode=WindowMode::Maximized;
    s.width=gallery?1100.f:v==4?620.f:405.f; s.height=gallery?650.f:v==4?650.f:325.f; ApplyState(s);
    absoluteLog=w->plotSession.settings.ivAbsoluteLog;
    std::snprintf(file,sizeof(file),"%s",w->path.c_str());
}
void Panel::Report(const Json &response) { if(!response.at("ok").get<bool>()) {w->status=response.at("error").at("message");w->response=response;} }
void OpenLayerWindow(Workflow *w,const std::string &device,const std::string &layer) {
    const std::pair<std::string,std::string> key{device,layer};
    if(!layer.empty() && std::find(w->pinnedLayers.begin(),w->pinnedLayers.end(),key)==w->pinnedLayers.end()) w->pinnedLayers.push_back(key);
}
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
namespace {
// New defect with a unique id; bulk default = SCAPS-like single neutral level at mid-gap (Ei), Nt 1e14 cm^-3, sigma 1e-15 cm^2.
int AddDefectToDraft(ProjectDraft& draft,int layer,bool sheet) {return AddDraftDefect(draft,layer,sheet);}
}
namespace {
Workflow *numericWorkflow=nullptr;
ProjectDraft *numericDraft=nullptr;
bool BoundIndex(const char* id,int& value,const char* key,int last,const char* format="%d") {
    const auto bounds=BoundsTable()->at(key);const auto [softMin,softMax]=bounds.SliderRange(value);
    const int lo=int(std::max(bounds.hardMin,softMin)),hi=int(std::min({bounds.hardMax,softMax,double(last)}));
    if(hi<lo){ImGui::TextDisabled("No selectable index in the configured bounds");return false;}
    int edited=value;const bool changed=ImGui::SliderInt(id,&edited,lo,hi,format);
    if(changed && (!bounds.Contains(edited) || edited>last)) {
        const auto message=bounds.Error(key,edited)+"; available maximum "+std::to_string(last);
        if(numericWorkflow)numericWorkflow->status=message;ImGui::TextColored({1,.45f,.25f,1},"%s",message.c_str());return false;
    }
    if(changed)value=edited;return changed;
}
void InitSweepControl(SweepControlState& state,SweepEditor editor,std::string key={}) {
    if(key.empty())key=state.parameterKey;
    if(key.empty())key=editor.displayUnit=="Hz"?"frequency_Hz":editor.displayUnit=="nm"?"wavelength_nm":"voltage_V";
    state={};state.editor=std::move(editor);state.parameterKey=std::move(key);
    for(double value:state.editor.range.custom){char text[32];std::snprintf(text,sizeof(text),"%.17g",value);if(!state.customText.empty())state.customText+="; ";state.customText+=text;}
}
bool SweepNumber(SweepControlState& state,const char* id,double& displayValue,bool step=false) {
    const auto& editor=state.editor;
    const auto key=step?(editor.component==SweepComponent::Exponent?std::string("sweep.exponent_step"):editor.range.logarithmic?std::string("sweep.decades"):
        "sweep.step:"+(editor.component==SweepComponent::Mantissa?std::string("sweep.mantissa"):state.parameterKey)):
        editor.component==SweepComponent::Mantissa?std::string("sweep.mantissa"):editor.component==SweepComponent::Exponent?"sweep.exponent:"+state.parameterKey:state.parameterKey;
    const double scale=editor.component==SweepComponent::FullValue && !(step && editor.range.logarithmic)?editor.displayScale:1;
    double native=displayValue/scale;const bool changed=DrawBoundValue(id,native,key,scale);
    if(changed)displayValue=native*scale;return changed;
}
bool SweepPoints(const char* id,int& points,const char* key="sweep.points") {
    double value=points;const bool changed=DrawBoundValue(id,value,key);if(changed)points=int(value);return changed;
}
bool SweepRangeControl(SweepControlState& state,bool compact=false) {
    auto& editor=state.editor;auto& range=editor.range;bool changed=false,listChanged=false;
    if(compact) {
        int input=range.sampling==SweepSampling::List?1:0,sampling=range.sampling==SweepSampling::Step?1:0,scale=range.logarithmic?1:0;
        const auto& style=ImGui::GetStyle();
        const std::string quantityLabel=std::string(editor.displayUnit=="Hz"?"Frequency":editor.displayUnit=="nm"?"Wavelength":"Voltage")+" ["+editor.displayUnit+"]";
        const float quantity=ImGui::CalcTextSize(quantityLabel.c_str()).x;
        const float number=std::max(ImGui::CalcTextSize("-1.234e+100").x+style.FramePadding.x*2,ImGui::CalcTextSize("Step [decades]").x);
        const float combo=ImGui::CalcTextSize("Custom list").x+ImGui::GetFrameHeight()+style.FramePadding.x*2;
        const float available=ImGui::GetContentRegionAvail().x;
        const bool split=available<quantity+number*3+combo*3+style.CellPadding.x*14;
        const auto begin=[&](const char* id,bool modes) {
            const int columns=modes?3:split?4:7;
            const float minimum=modes?combo*3:quantity+number*3+(split?0:combo*3);
            const float inner=minimum+style.CellPadding.x*2*columns;
            const bool scroll=available<inner;
            if(!ImGui::BeginTable(id,columns,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_NoKeepColumnsVisible|(scroll?ImGuiTableFlags_ScrollX:0),ImVec2(0,scroll?ImGui::GetFrameHeight()*2+style.CellPadding.y*4+style.ScrollbarSize:0),scroll?inner:0))return false;
            if(!modes) {
                ImGui::TableSetupColumn("Quantity",ImGuiTableColumnFlags_WidthFixed,quantity);
                for(const auto* title:{"Min","Max",sampling?(range.logarithmic?"Step [decades]":"Step"):"Points"})ImGui::TableSetupColumn(title,ImGuiTableColumnFlags_WidthFixed,std::max(number,ImGui::CalcTextSize(title).x));
            }
            if(modes || !split)for(const auto* title:{"Scale","Sampling","Input"})ImGui::TableSetupColumn(title,ImGuiTableColumnFlags_WidthFixed,combo);
            ImGui::TableHeadersRow();ImGui::TableNextRow();return true;
        };
        const bool opened=begin("Action range",false);
        if(opened) {
            ImGui::TableNextColumn();ImGui::Text("%s [%s]",editor.displayUnit=="Hz"?"Frequency":editor.displayUnit=="nm"?"Wavelength":"Voltage",editor.displayUnit.c_str());
            const auto drag=[&](const char* id,double& value,bool step=false){return SweepNumber(state,id,value,step);};
            ImGui::TableNextColumn();ImGui::BeginDisabled(input!=0);changed|=drag("##minimum",range.minimum);
            ImGui::TableNextColumn();changed|=drag("##maximum",range.maximum);
            ImGui::TableNextColumn();if(sampling)changed|=drag("##step",range.step,true);else {ImGui::SetNextItemWidth(-1);changed|=SweepPoints("##points",range.points);}ImGui::EndDisabled();
            if(split)ImGui::EndTable();
        }
        if((!split && opened) || (split && begin("Action modes",true))) {
            ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##scale",&scale,"Linear\0Log\0")){range.logarithmic=scale==1;changed=true;}
            ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##sampling",&sampling,"Points\0Step\0")){range.sampling=sampling?SweepSampling::Step:SweepSampling::Points;input=0;changed=true;}
            ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##input",&input,"Range\0Custom list\0")){changed=true;if(input){try{range.custom=SweepValues(range);}catch(const std::exception&){range.custom={range.minimum};}range.sampling=SweepSampling::List;const auto saved=editor;InitSweepControl(state,saved);}else {range.sampling=sampling?SweepSampling::Step:SweepSampling::Points;if(!range.custom.empty()){range.minimum=range.custom.front();range.maximum=range.custom.back();range.points=int(range.custom.size());}}}
            ImGui::EndTable();
        }
        if(input && sst::ui::widgets::MultilineText(("Values ["+editor.displayUnit+"]").c_str(),&state.customText,ImVec2(-1,65))){changed=true;listChanged=true;}
        if(changed){state.error.clear();if(listChanged)try{range.custom=ParseSweepList(state.customText);}catch(const std::exception& e){state.error=e.what();}}
        return changed;
    }
    BeginRow("Input");int input=range.sampling==SweepSampling::List?1:0;
    if(ImGui::Combo("##input",&input,"Range\0Custom list\0")) {
        changed=true;
        if(input==1){try{range.custom=SweepValues(range);}catch(const std::exception&){range.custom={range.minimum};}range.sampling=SweepSampling::List;const auto saved=editor;InitSweepControl(state,saved);}
        else {range.sampling=SweepSampling::Points;if(!range.custom.empty()){range.minimum=range.custom.front();range.maximum=range.custom.back();range.points=int(range.custom.size());}}
    }EndRow();
    const bool exponent=editor.component==SweepComponent::Exponent;
    if(!input) {
        if(!exponent){BeginRow("Scale");int scale=range.logarithmic?1:0;if(ImGui::Combo("##scale",&scale,"Linear\0Log\0")){range.logarithmic=scale==1;changed=true;}EndRow();}
        const auto drag=[&](const std::string& label,double& value,bool=false){BeginRow(label.c_str());const bool edit=SweepNumber(state,"##value",value,&value==&range.step);EndRow();return edit;};
        const auto unit=exponent?std::string("decades"):editor.component==SweepComponent::Mantissa?std::string("1"):editor.displayUnit;
        changed|=drag("Minimum ["+unit+"]",range.minimum,exponent);
        changed|=drag("Maximum ["+unit+"]",range.maximum,exponent);
        BeginRow("Sampling");int sampling=range.sampling==SweepSampling::Step?1:0;
        if(ImGui::Combo("##sampling",&sampling,"Points\0Step\0")){range.sampling=sampling?SweepSampling::Step:SweepSampling::Points;changed=true;}EndRow();
        if(sampling)changed|=drag(range.logarithmic || exponent?"Step [decades]":"Step ["+unit+"]",range.step,exponent);
        else {BeginRow("Points");if(range.minimum==range.maximum)ImGui::TextUnformatted("1");else if(SweepPoints("##points",range.points))changed=true;EndRow();}
    } else if(sst::ui::widgets::MultilineText(("Values ["+(exponent?std::string("decades"):editor.component==SweepComponent::Mantissa?std::string("1"):editor.displayUnit)+"]").c_str(),&state.customText,ImVec2(-1,65))) {
        changed=true;listChanged=true;
    }
    if(changed) {
        state.error.clear();
        if(listChanged)try{range.custom=ParseSweepList(state.customText);}catch(const std::exception& e){state.error=e.what();}
    }
    if(editor.component==SweepComponent::Mantissa)ImGui::Text("Mantissa; fixed exponent = %.17g [%s]",editor.fixed,editor.displayUnit.c_str());
    if(exponent)ImGui::Text("Exponent; fixed mantissa = %.17g [%s]",editor.fixed,editor.displayUnit.c_str());
    return changed;
}
void SweepPreview(const BatchAxis& axis,const SweepControlState& state) {
    if(!state.error.empty()){ImGui::TextWrapped("%s",state.error.c_str());return;}
    const auto values=BatchAxisValues(axis);const auto& editor=state.editor;
    std::string preview;
    for(std::size_t i=0;i<values.size();++i){if(i==10 && values.size()>12){preview+=" ... ";i=values.size()-1;}char text[40];std::snprintf(text,sizeof(text),"%.9g",values[i]*editor.displayScale);if(!preview.empty())preview+="; ";preview+=text;}
    ImGui::TextWrapped("Preview [%s]: %s | %zu members",editor.displayUnit.c_str(),preview.c_str(),values.size());
    if(editor.range.sampling==SweepSampling::Step && values.size()>1) {
        const auto samples=SweepValues(editor.range);const double last=editor.range.logarithmic?std::log10(samples.back())-std::log10(samples[samples.size()-2]):samples.back()-samples[samples.size()-2];
        if(std::abs(last-editor.range.step)>1e-10*editor.range.step)ImGui::Text("Last step: %.9g %s (maximum included)",last,editor.range.logarithmic || editor.component==SweepComponent::Exponent?"decades":editor.component==SweepComponent::Mantissa?"":editor.displayUnit.c_str());
    }
}
void SweepMenu(const void* address,const char *key,double scale=1,const char *title="",SweepComponent component=SweepComponent::FullValue) {
    if(!numericWorkflow || !numericDraft || !address)return;
    const char* popup=component==SweepComponent::Mantissa?"Sweep mantissa":component==SweepComponent::Exponent?"Sweep exponent":"Sweep full value";
    if(ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Menu) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F10))))ImGui::OpenPopup(popup);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420,0),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::BeginPopupContextItem(popup))return;
    const auto parameters=ProjectParameters(*numericDraft);const auto path=ProjectFieldPath(numericDraft->project,address);
    const auto found=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.path==path;});
    if(found!=parameters.end()) {
        static SweepControlState state;static BatchAxis axis;static Ref<const ProjectDraft> snapshot;static Ref<const BatchDraft> base;static Ref<const RecorderDraft> recorder;static Ref<const RunPlan> runPlan;
        auto& workflow=*numericWorkflow;
        const auto refresh=[&](SweepComponent part){snapshot=workflow.Current().draft;base=workflow.Current().batch;recorder=workflow.Current().recorder;runPlan=workflow.Current().runPlan;try{std::string unit=title;const auto left=unit.rfind('['),right=unit.rfind(']');unit=left!=unit.npos && right>left?unit.substr(left+1,right-left-1):found->unit;InitSweepControl(state,NewSweep(*found,part,scale,unit),found->path);axis=PrepareSweepAxis(*snapshot,found->id,state.editor);}catch(const std::exception& e){state={};state.error=e.what();}};
        if(ImGui::IsWindowAppearing())refresh(component);
        ImGui::Text("Quick batch: %s / %s",*title?title:found->label.c_str(),state.editor.component==SweepComponent::Mantissa?"Mantissa":state.editor.component==SweepComponent::Exponent?"Exponent":"Full value");ImGui::Separator();
        if(state.editor.component!=SweepComponent::FullValue || !state.error.empty())if(ImGui::Button("Sweep full value..."))refresh(SweepComponent::FullValue);
        if(SweepRangeControl(state) && state.error.empty())try{axis=PrepareSweepAxis(*snapshot,found->id,state.editor);}catch(const std::exception& e){state.error=e.what();}
        const bool stale=snapshot!=workflow.Current().draft || base!=workflow.Current().batch || recorder!=workflow.Current().recorder || runPlan!=workflow.Current().runPlan;
        if(stale){ImGui::TextWrapped("Project or batch settings changed; refresh before running.");if(ImGui::Button("Refresh snapshot"))refresh(state.editor.component);}
        SweepPreview(axis,state);
        ImGui::TextUnformatted("Sampling: included Run Actions");
        ImGui::Text("Recorder: %zu quantities",recorder?recorder->columns.size():0);
        ImGui::BeginDisabled(workflow.Busy() || stale || !state.error.empty());
        if(ImGui::Button("Run quick batch")) {auto quick=*base;quick.mode="nested";quick.axes={axis};(void)workflow.Exec(Cmd::QuickSweep::Make(CreateRef<const BatchDraft>(std::move(quick)),recorder,snapshot,runPlan));ImGui::CloseCurrentPopup();}
        if(ImGui::Button("Add to batch..."))ImGui::OpenPopup("Choose batch row");
        bool added=false;
        if(ImGui::BeginPopup("Choose batch row")) {
            for(std::size_t i=0;i<base->axes.size();++i)if(ImGui::Selectable(("Replace parameter "+std::to_string(i+1)+": "+base->axes[i].parameter_id).c_str())){auto draft=*base;draft.axes[i]=axis;(void)workflow.Exec(Cmd::SetBatchSettings::Make(CreateRef<const BatchDraft>(std::move(draft))));added=true;ImGui::CloseCurrentPopup();}
            if((base->mode=="linked" || base->mode=="together" || base->axes.size()<2) && ImGui::Selectable("Append parameter")){auto draft=*base;draft.axes.push_back(axis);(void)workflow.Exec(Cmd::SetBatchSettings::Make(CreateRef<const BatchDraft>(std::move(draft))));added=true;ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        if(added)ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
    }
    ImGui::EndPopup();
}

template<class S> bool Is(const Json &j,const S &s) {return j.is_string() && j.template get_ref<const std::string &>()==s;} // Json string == s without building a temporary Json (no allocation)
// Typed result of a history run; a run whose profiles were dropped is served by the live result when it is the same calculation.
Ref<const ResultBase> HistoryTyped(const DeviceState &device,const Json &run) {
    const auto live=(Is(run.at("kind"),"band")?device.bandResult:device.result).typed();
    if(!run.value("profiles_stored",true) && live && live->Provenance()==run.at("provenance"))return live;
    return device.RunResult(run.at("run_id").get_ref<const std::string &>());
}
// True when the selected run is an I-V/band run that is not the live result (so the live curve and other runs are drawn dimmed).
bool SelectedOffLive(const DeviceState &device,const std::string &kind) {
    if(device.selectedRun.empty())return false;
    for(const auto &run:device.history) if(Is(run.at("run_id"),device.selectedRun)) {
        if(!Is(run.at("kind"),kind) && !(kind=="iv" && Is(run.at("kind"),"band")))return false;
        const auto live=(Is(run.at("kind"),"band")?device.bandResult:device.result).typed();
        return !live || live->Provenance().is_null() || live->Provenance()!=run.at("provenance");
    }
    return false;
}
// Live-curve line style: dimmed with the device colour while another run is selected.
ImPlotSpec LiveStyle(const DeviceState &device,float weight) {
    if(!SelectedOffLive(device,"iv"))return {ImPlotProp_LineWeight,weight};
    const auto c=device.colour.get<std::vector<float>>();return {ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],.3f),ImPlotProp_LineWeight,weight};
}
bool ShowRun(const Json &run) {return run.value("pinned",false) || Is(run.at("kind"),"batch");}
// Colour scheme for all plots versus x (one table; Ec/n blue, Ev/p red, G green, one colour per recombination mechanism).
ImVec4 XColour(const std::string &key,int shade=0) {
    static const std::pair<const char*,ImVec4> table[]={
        {"Ec_eV",{.35f,.62f,1.f,1.f}},{"EFn_eV",{.35f,.62f,1.f,1.f}},{"n_m3",{.35f,.62f,1.f,1.f}},
        {"Ev_eV",{1.f,.38f,.38f,1.f}},{"EFp_eV",{1.f,.38f,.38f,1.f}},{"p_m3",{1.f,.38f,.38f,1.f}},
        {"generation_m3_s",{.30f,.80f,.35f,1.f}},{"G",{.30f,.80f,.35f,1.f}},
        {"srh_bulk",{.75f,.50f,.95f,1.f}},{"interface",{1.f,.65f,.15f,1.f}},{"radiative",{.20f,.80f,.80f,1.f}},{"auger",{.95f,.45f,.75f,1.f}}};
    ImVec4 c{.8f,.8f,.8f,1.f};
    for(const auto &[k,v]:table) if(key==k) c=v;
    const float t=std::min(.6f,.25f*shade);  // lighter tint for extra channels of the same mechanism
    return {c.x+(1-c.x)*t,c.y+(1-c.y)*t,c.z+(1-c.z)*t,1.f};
}
// Dashed polyline drawn on the plot draw list (ImPlot has no dashed style). Call between PushPlotClipRect/PopPlotClipRect.
void DashedLine(const std::vector<double> &x,const std::vector<double> &y,ImVec4 c,float dash=7.f,float gap=4.f) {
    auto *draw=ImPlot::GetPlotDrawList();const ImU32 col=ImGui::ColorConvertFloat4ToU32(c);float acc=0;
    for(std::size_t i=0;i+1<x.size() && i+1<y.size();++i) {
        const ImVec2 a=ImPlot::PlotToPixels(x[i],y[i]),b=ImPlot::PlotToPixels(x[i+1],y[i+1]);
        const float len=std::hypot(b.x-a.x,b.y-a.y);if(!(len>0))continue;
        for(float t=0;t<len;) {
            const float period=dash+gap,pos=std::fmod(acc+t,period);
            const float seg=pos<dash?std::min(dash-pos,len-t):std::min(period-pos,len-t);
            if(pos<dash)draw->AddLine({a.x+(b.x-a.x)*t/len,a.y+(b.y-a.y)*t/len},{a.x+(b.x-a.x)*(t+seg)/len,a.y+(b.y-a.y)*(t+seg)/len},col,1.8f);
            t+=seg;
        }
        acc=std::fmod(acc+len,dash+gap);
    }
}
// Layer type from net doping Nd-Na (graded doping: base value; ponytail: grading ignored, upgrade via ProfileSamples mean).
// Shaded band + boundary line + name per layer; x in um. Call inside a plot after the data.
int LayerType(const Json &l) {const double net=l.value("doping",Json::object()).value("Nd_m3",0.)-l.value("doping",Json::object()).value("Na_m3",0.);return std::abs(net)<1e15?0:net>0?1:-1;} // 1 = n, -1 = p, 0 = i
// Opaque strip colour per layer type, same hue as the translucent regions in the Bands/Carriers plots: n blue, p red, i grey.
int LayerType(const scaps_core::ProjectLayer& l) {const double net=l.Nd_m3-l.Na_m3;return std::abs(net)<1e15?0:net>0?1:-1;}
ImU32 LayerStripColour(const scaps_core::ProjectLayer& l) {const int t=LayerType(l);return t>0?IM_COL32(40,75,130,255):t<0?IM_COL32(130,48,52,255):IM_COL32(78,78,84,255);}
ImU32 LayerStripColour(const Json &l) {const int t=LayerType(l);return t>0?IM_COL32(40,75,130,255):t<0?IM_COL32(130,48,52,255):IM_COL32(78,78,84,255);}
void DrawLayerRegions(const Json &layers) {
    const auto limits=ImPlot::GetPlotLimits();auto *draw=ImPlot::GetPlotDrawList();ImPlot::PushPlotClipRect();
    double x0=0;
    for(std::size_t i=0;i<layers.size();++i) {
        const auto &l=layers[i];const double x1=x0+l.at("thickness_m").get<double>()*1e6;
        const int type=LayerType(l);
        const ImU32 fill=type==0?IM_COL32(160,160,160,24):type>0?IM_COL32(90,150,255,28):IM_COL32(255,90,90,28);
        const auto a=ImPlot::PlotToPixels(x0,limits.Y.Max),b=ImPlot::PlotToPixels(x1,limits.Y.Min);
        draw->AddRectFilled(a,b,fill);
        if(i+1<layers.size())draw->AddLine({b.x,a.y},{b.x,b.y},IM_COL32(210,210,220,150));
        const std::string name=l.value("name",std::string{});
        if(!name.empty())draw->AddText({a.x+4,a.y+2},IM_COL32(220,220,230,200),name.c_str());
        x0=x1;
    }
    ImPlot::PopPlotClipRect();
}
// Live and stored are selected independently of pinning overlays.
void ShotBar(Workflow &w,const std::string &kind) {
    ImGui::PushID("shotbar");
    auto &device=w.Current();const std::string deviceId=device.id;const auto history=device.history;
    const auto actionKind=kind=="band"?ActionKind::Band:kind=="cv"?ActionKind::Cv:kind=="cf"?ActionKind::Cf:kind=="qe"?ActionKind::Qe:ActionKind::Iv;
    if(!device.selectedRun.empty()) {
        ImGui::TextUnformatted("Viewing stored run");ImGui::SameLine();
        if(ImGui::SmallButton("Back to Live"))(void)w.Exec(Cmd::SelectRun::Make(deviceId,""));ImGui::SameLine();
        ImGui::TextUnformatted("Pin view");ImGui::SameLine();ImGui::Checkbox("##pin-view",&device.pinStoredView);
    }
    const char* recalc=actionKind==ActionKind::Band?"Recalculate band":actionKind==ActionKind::Cv?"Recalculate C-V":actionKind==ActionKind::Cf?"Recalculate C-f":actionKind==ActionKind::Qe?"Recalculate QE":"Recalculate I-V";
    ImGui::BeginDisabled(w.Busy());if(ImGui::Button(recalc))(void)w.Exec(Cmd::RecalculateLive::Make(actionKind));ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(w.Busy());bool shot=ImGui::Button("Capture live");ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Store the current exact live result (Ctrl+P). Single shot executes included Actions in Run.");
    if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_P,ImGuiInputFlags_RouteFocused))shot=true;
    if(shot && !w.Busy())(void)w.Exec(Cmd::CaptureLive::Make(actionKind));
    std::vector<const Json *> shots;
    for(const auto &run:history)if(run.at("pinned")==true && (run.at("kind")==kind || (kind=="iv" && run.at("kind")=="band")))shots.push_back(&run);
    ImGui::SameLine();
    if(ImGui::Button(("Shots ("+std::to_string(shots.size())+")###shots").c_str()))ImGui::OpenPopup("shotlist");
    if(ImGui::BeginPopup("shotlist")) {
        static std::string editId,editText;
        if(shots.empty())ImGui::TextDisabled("No pinned runs. Capture live stores the current exact result; Pin keeps its overlay.");
        for(const auto *shotRun:shots) {
            const auto &run=*shotRun;const auto id=run.at("run_id").get<std::string>();ImGui::PushID(id.c_str());
            bool visible=run.at("visible");
            if(ImGui::Checkbox("##v",&visible) && !w.Busy())(void)w.Exec(Cmd::UpdateRun::With({{"device_id",deviceId},{"run_id",id},{"visible",visible}}));
            ImGui::SameLine();const auto c=run.at("colour").get<std::vector<float>>();ImGui::ColorButton("##c",ImVec4(c[0],c[1],c[2],c[3]),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoPicker,ImVec2(12,12));
            ImGui::SameLine();char buf[256];std::snprintf(buf,sizeof(buf),"%s",(editId==id?editText:RunLabel(run)).c_str());
            ImGui::SetNextItemWidth(260);ImGui::InputText("##n",buf,sizeof(buf));
            if(ImGui::IsItemActive()){editId=id;editText=buf;}
            if(ImGui::IsItemDeactivatedAfterEdit() && !w.Busy())(void)w.Exec(Cmd::UpdateRun::With({{"device_id",deviceId},{"run_id",id},{"name",std::string(buf)}}));
            if(ImGui::IsItemDeactivated())editId.clear();
            ImGui::SameLine();if(ImGui::SmallButton("Delete") && !w.Busy())(void)w.Exec(Cmd::UpdateRun::With({{"device_id",deviceId},{"run_id",id},{"pinned",false}}));
            ImGui::PopID();
        }
        if(!shots.empty() && ImGui::Button("Clear all") && !w.Busy())(void)w.Exec(Cmd::UpdateRun::With({{"device_id",deviceId},{"unpin_kind",kind}}));
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
void RecalculateThisBias(Workflow& w) {
    if(w.Current().selectedRun.empty())(void)w.Exec(Cmd::RecalculateLive::Make(ActionKind::Band));
    else (void)w.Exec(Cmd::RecalculateStoredBias::With());
}
// Owner 2026-11-23: a bias without stored profiles must not blank the plots - draw the nearest stored profile
// under one fixed-height line that says so and offers the recalculation.
const IvPoint* NearestStoredProfiles(Workflow& w,const std::vector<IvPoint>& points,int index) {
    const IvPoint* best=nullptr;int distance=std::numeric_limits<int>::max();
    for(int i=0;i<int(points.size());++i) if(points[i].details && std::abs(i-index)<distance) {distance=std::abs(i-index);best=&points[i];}
    if(!best)return nullptr;
    ImGui::AlignTextToFramePadding();ImGui::TextDisabled("Not stored at %.4g V - showing %.4g V",points[std::clamp(index,0,int(points.size())-1)].voltage_V,best->voltage_V);
    ImGui::SameLine();ImGui::BeginDisabled(w.Busy());if(ImGui::SmallButton("Recalculate this bias"))RecalculateThisBias(w);ImGui::EndDisabled();
    return best;
}
bool MissingProfiles(Workflow& w,const IvPoint& point) {
    if(point.details)return false;
    ImGui::TextUnformatted("Profiles not stored at this bias");
    ImGui::BeginDisabled(w.Busy());
    if(ImGui::Button("Recalculate this bias"))RecalculateThisBias(w);
    ImGui::EndDisabled();return true;
}
// S3 plot caches: everything a plot draws per frame is read from here. Entries are keyed by Json node address / device and dropped when
// Workflow::plotGen changes (RunCompleted/HistoryChanged/SelectionChanged/DocumentEdited/... events), so draw paths do no JSON->vector
// conversion, no DocumentHash and no allocation once warm. The cache is only valid for the Workflow currently rendering (numericWorkflow).
// Test hook (S3): called with true/false around the plot-data paths (cache lookups and drawing cached lines) so a test can count allocations there.
void (*PlotDataHook)(bool)=nullptr;
struct PlotDataScope {PlotDataScope() {if(PlotDataHook)PlotDataHook(true);} ~PlotDataScope() {if(PlotDataHook)PlotDataHook(false);}};
struct Cols {std::vector<double> a,b,c;int n=0;std::size_t size=0;};
struct HLine {std::string name,head,tail,modelHash,runId;bool previous=false;std::vector<double> x,y;ImVec4 colour;float weight=1.5f;};
struct HEntry {std::uint64_t docGen=0;const DeviceState *dev=nullptr;std::string kind,xkey,ykey,selected;bool absolute=false;double scale=1.;int bias=0;bool profiles=false,gr=false,dashed=false;std::vector<HLine> lines;};
struct PlotCache {
    std::uint64_t gen=~0ull;int frame=-1;
    std::map<std::tuple<const void *,int,double>,Cols> cols;
    std::vector<HEntry> entries;
};
inline int &PlotCacheBuilds() {static int n=0;return n;} // test hook: number of cache (re)builds
PlotCache &Cache() {
    static PlotCache c;
    // Invalidate at a frame boundary only (or when another Workflow renders): an event published mid-frame (e.g. a legend click dispatching
    // history_update) must not free vectors the running draw code still references; it is picked up on the next frame.
    if(numericWorkflow && c.gen!=numericWorkflow->plotGen && (c.frame!=ImGui::GetFrameCount() || (c.gen>>32)!=(numericWorkflow->plotGen>>32))) {c.cols.clear();c.entries.clear();c.gen=numericWorkflow->plotGen;}
    c.frame=ImGui::GetFrameCount();
    return c;
}
template<class Node,class F> Cols &Derived(const Node &node,int tag,F &&build,double param=0.) {
    auto &cols=Cache().cols;const auto key=std::make_tuple(static_cast<const void*>(&node),tag,param);
    auto found=cols.find(key);
    if(found==cols.end() || found->second.size!=node.size()) {Cols c;build(c);c.size=node.size();++PlotCacheBuilds();found=cols.insert_or_assign(key,std::move(c)).first;}
    return found->second;
}
// Numeric array as doubles, scaled; mode 0 raw, 1 v<=0 -> NaN, 2 non-finite or v<=0 -> NaN, 3 all NaN (invalid curve).
const std::vector<double> &Vec(const Json &node,double scale=1.,int mode=0) {
    PlotDataScope scope;
    return Derived(node,mode,[&](Cols &c){
        c.a=node.get<std::vector<double>>();
        for(auto &v:c.a) {v*=scale;if(mode==1?v<=0:mode==2?(!std::isfinite(v) || v<=0):mode==3)v=NAN;}
    },scale).a;
}
const std::vector<double>& Vec(const std::vector<double>& node,double scale=1.,int mode=0) {
    PlotDataScope scope;
    return Derived(node,mode,[&](Cols& c){c.a=node;for(auto& v:c.a){v*=scale;if(mode==4)v=v==0?NAN:std::abs(v);else if(mode==1?v<=0:mode==2?(!std::isfinite(v) || v<=0):mode==3)v=NAN;}},scale).a;
}
const Cols& IvCols(const std::vector<IvPoint>& points,bool absolute,double scale=1.) {
    PlotDataScope scope;
    return Derived(points,100,[&](Cols& c){for(const auto& p:points){c.a.push_back(p.voltage_V);const bool valid=p.valid && std::isfinite(p.J_A_m2);if(!valid)++c.n;const double current=valid?p.J_A_m2*scale:NAN;c.b.push_back(absolute?(current==0?NAN:std::abs(current)):current);}},absolute?-scale:scale);
}
const Cols& IvCols(const IvResult& result,bool absolute) {
    if(result.curve.empty())return IvCols(result.points,absolute);
    PlotDataScope scope;return Derived(result.curve,101,[&](Cols& c){for(const auto& p:result.curve){c.a.push_back(p.voltage_V);const bool valid=ValidIvSample(p);if(!valid)++c.n;const double j=valid?p.J_Am2:NAN;c.b.push_back(absolute?(j==0?NAN:std::abs(j)):j);}},absolute?-1.:1.);
}
const Cols& AcCols(const std::vector<AcPoint>& points,int view) {
    PlotDataScope scope;
    return Derived(points,200+view,[&](Cols& c){for(const auto& p:points){if(!p.valid)++c.n;c.a.push_back(view<8?p.voltage_V:p.frequency_Hz);c.b.push_back(p.valid?(view==7?p.inverse_C2_F_minus2:view==9?p.G_over_omega_F:p.capacitance_F):NAN);c.c.push_back(p.valid?p.G_over_omega_F:NAN);}});
}
bool Archived(const DeviceState&,const ResultBase&,const std::string&){return false;}
// Live I-V curve (a=V, b=J or |J|, n=invalid points) and AC curve (a=x, b=y of this view, c=G/omega, n=invalid points) of a result's points array.
const Cols &IvCols(const Json &points,bool absolute) {
    PlotDataScope scope;
    return Derived(points,100,[&](Cols &c){
        for(const auto &pt:points) {
            c.a.push_back(pt.at("voltage_V"));const bool valid=pt.value("valid",false) && pt.at("J_A_m2").is_number();
            if(!valid) ++c.n;
            const double current=valid?pt.at("J_A_m2").get<double>():NAN;
            c.b.push_back(absolute?(current==0?NAN:std::abs(current)):current);
        }
    },absolute?1.:0.);
}
// Marked badge in the top-left corner of the current plot (slot 0,1,... stack downwards). Everything that is not an exact result carries one.
void PlotBadge(const char *text,ImU32 fill,ImU32 ink,int slot) {
    const ImVec2 origin=ImPlot::GetPlotPos(),size=ImGui::CalcTextSize(text);const float top=origin.y+8+float(slot)*(size.y+10);
    auto *list=ImPlot::GetPlotDrawList();list->AddRectFilled(ImVec2(origin.x+8,top),ImVec2(origin.x+20+size.x,top+6+size.y),fill,4.f);list->AddText(ImVec2(origin.x+14,top+3),ink,text);
}
// Live curve of the document before the last edit: dimmed (RAII around its PlotLine) and tagged "outdated" until the new exact result replaces it.
struct StaleDim {
    bool on;
    explicit StaleDim(const DeviceState &d):on(d.Stale()) {if(on)ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*.4f);}
    ~StaleDim() {if(on)ImGui::PopStyleVar();}
};
// Live slider-drag preview (DeviceState::previewV/J, never stored) and the growing exact run (liveV/J): dashed / orange curves; status stays above the plot; call between BeginPlot/EndPlot.
// Neither curve is a result: they are never saved, put in history, exported or read by a readout.
void ReferenceLine(Workflow& w,const std::string& name,const std::vector<double>& x,const std::vector<double>& y,ImVec4 colour) {
    if(w.liveChangeReference==3 || x.empty() || x.size()!=y.size())return;
    if(w.liveChangeReference==0 || w.liveChangeReference==2)colour.w*=.3f;
    const auto label=name+" (before)##change-reference";
    const bool dashed=w.liveChangeReference==1 || w.liveChangeReference==2;
    if(!dashed)ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,colour,ImPlotProp_LineWeight,2.f});
    else {ImPlot::PlotLine(label.c_str(),x.data(),y.data(),0,{ImPlotProp_LineColor,colour});if(const auto item=ImPlot::GetItem(label.c_str());item && item->Show){if(ImPlot::FitThisFrame())for(std::size_t i=0;i<x.size();++i)if(std::isfinite(x[i]) && std::isfinite(y[i]))ImPlot::FitPoint({x[i],y[i]});ImPlot::PushPlotClipRect();DashedLine(x,y,colour);ImPlot::PopPlotClipRect();}}
}
bool IvIsReference(const Workflow& w,const DeviceState& d) {return w.liveChangeReference!=3 && d.changeIv && d.changeIv==d.result.typed() && (!d.previewV.empty() || d.liveRunning);}
void IvReference(Workflow& w,const DeviceState& d,bool absolute,double scale=1.) {
    if(!d.changeIv)return;const auto& cols=IvCols(d.changeIv->points,absolute,scale);const auto c=d.colour.get<std::vector<float>>();ReferenceLine(w,d.name+" I-V",cols.a,cols.b,{c[0],c[1],c[2],c[3]});
}
void ProfileReference(Workflow& w,const char* key,double scale=1.) {
    const auto& d=w.Current();const auto result=d.changeBand?d.changeBand:d.changeIv;if(!result || result->points.empty())return;
    const auto& p=result->points[std::clamp(d.selectedBias,0,int(result->points.size())-1)];if(!p.valid)return;
    ReferenceLine(w,key,Vec(p.x_m.get(),1e6),Vec(PointColumn(p,key).get(),scale),XColour(key));
}
void IvStatus(const DeviceState& d) {
    if(d.liveRunning && !d.liveV.empty())ImGui::TextColored(ImVec4(1.f,.55f,.1f,1.f),"RUNNING (%zu points so far, exact result follows)",d.liveV.size());
    else if(!d.previewV.empty())ImGui::TextColored(ImVec4(1.f,.55f,.1f,1.f),"%s",d.previewCoarse?"PREVIEW (coarse, exact on release)":"PREVIEW (exact on release)");
    if(d.Stale())ImGui::TextDisabled("outdated: model edited");
}
void PreviewOverlay(const DeviceState &d,bool absolute,double scale=1.) {
    auto J=[&](double j){return absolute?(j==0?double(NAN):std::abs(j)*scale):j*scale;};
    if(d.liveRunning && !d.liveV.empty()) {
        std::vector<double> y;y.reserve(d.liveJ.size());for(double j:d.liveJ) y.push_back(J(j));
        ImPlot::PlotLine("running##live",d.liveV.data(),y.data(),int(y.size()),{ImPlotProp_LineColor,ImVec4(1.f,.55f,.1f,1.f),ImPlotProp_LineWeight,2.f});
    } else if(!d.previewV.empty()) {
        std::vector<double> x,y;const std::size_t n=d.previewV.size();
        auto Ji=[&](std::size_t i){return J(d.previewJ[i]);};
        for(std::size_t i=0;i<n;++i){x.push_back(d.previewV[i]);y.push_back(Ji(i));}
        ImPlot::PlotLine("preview##live",x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(1.f,.55f,.1f,1.f),ImPlotProp_LineWeight,2.f});
    }
}
const Cols &AcCols(const Json &points,int view) {
    PlotDataScope scope;
    const bool cv=view<8;const char *key=view==7?"inverse_C2_F_minus2":view==9?"G_over_omega_F":"capacitance_F";
    return Derived(points,200+view,[&](Cols &c){
        for(const auto &pt:points) {
            const bool valid=pt.value("valid",false);if(!valid) ++c.n;
            c.a.push_back(pt.at(cv?"voltage_V":"frequency_Hz"));c.b.push_back(valid && pt.at(key).is_number()?pt.at(key).get<double>():NAN);
            c.c.push_back(valid && pt.at("G_over_omega_F").is_number()?pt.at("G_over_omega_F").get<double>():NAN);
        }
    });
}
void HistoryLines(Workflow &w,const DeviceState &device,const std::string &kind,const char *xkey,const char *ykey,bool absolute=false,double scale=1.) {
    PlotDataScope scope;
    auto &entries=Cache().entries;HEntry *e=nullptr;
    for(auto &c:entries) if(c.dev==&device && !c.profiles && c.absolute==absolute && c.scale==scale && c.kind==kind && c.xkey==xkey && c.ykey==ykey && c.selected==device.selectedRun) {e=&c;break;}
    if(!e) {
        ++PlotCacheBuilds();e=&entries.emplace_back();e->docGen=w.docGen;e->dev=&device;e->kind=kind;e->xkey=xkey;e->ykey=ykey;e->selected=device.selectedRun;e->absolute=absolute;e->scale=scale;
        const bool dim=SelectedOffLive(device,kind); // a selected non-live run is highlighted, the rest dimmed
        std::string currentHash;bool hashed=false;
        for(const auto &run:device.history) {
            const bool chosen=dim && run.at("run_id")==device.selectedRun;
            if(!run.at("visible").get<bool>() || (!ShowRun(run) && !chosen) || (run.at("kind")!=kind && !(kind=="iv" && run.at("kind")=="batch")))continue;
            HLine l;
            const auto add=[&](double x,double value){l.x.push_back(x);l.y.push_back(absolute?(value==0?NAN:std::abs(value)):value*scale);};
            if(run.at("kind")=="batch") {
                const auto typed=HistoryTyped(device,run);
                if(const auto batch=dynamic_cast<const BatchResult *>(typed.get())) // curve columns straight from the packed table: the table's Json view is never built for a plot
                    for(const auto &curve:BatchIvCurves(*batch)) for(std::size_t i=0;i<curve.v.size();++i) add(curve.v[i],curve.converged[i]?curve.j[i]:double(NAN));
            } else { // typed points: no Json is built for history runs
                const auto typed=HistoryTyped(device,run);
                const auto each=[&](const auto &points){for(const auto &point:points){double x=0,y=0;if(!PointNumber(point,xkey,x))continue;add(x,point.valid && PointNumber(point,ykey,y)?y:NAN);}};
                if(const auto iv=dynamic_cast<const IvResult *>(typed.get()))each(iv->points);else if(const auto ac=dynamic_cast<const AcResult *>(typed.get()))each(ac->points);
            }
            if(l.x.empty())continue;
            if(!hashed) {currentHash=DocumentHash(device.Document());hashed=true;}
            const auto c=run.at("colour").get<std::vector<float>>();
            const bool selected=run.at("run_id")==device.selectedRun;
            l.colour=ImVec4(c[0],c[1],c[2],dim&&!selected?.3f:c[3]);l.weight=selected?4.f:1.5f;l.runId=run.at("run_id").get<std::string>();
            l.modelHash=run.at("model_hash").get<std::string>();l.previous=l.modelHash!=currentHash;
            l.head=device.name+" / "+RunLabel(run);l.tail=std::string(std::string(ykey)=="G_over_omega_F"?" G/omega":"")+"##"+l.runId+"/"+ykey;
            l.name=l.head+(l.previous?" (previous)":"")+l.tail;
            e->lines.push_back(std::move(l));
        }
    }
    if(e->docGen!=w.docGen) { // a document edit does not touch result data: only the "(previous)" tags can change, so no curve is rebuilt
        e->docGen=w.docGen;const auto hash=DocumentHash(device.Document());
        for(auto &l:e->lines) if((l.modelHash!=hash)!=l.previous) {l.previous=!l.previous;l.name=l.head+(l.previous?" (previous)":"")+l.tail;}
    }
    for(const auto &l:e->lines) {
        ImPlot::PlotLine(l.name.c_str(),l.x.data(),l.y.data(),int(l.x.size()),{ImPlotProp_LineColor,l.colour,ImPlotProp_LineWeight,l.weight});
        if(auto *item=ImPlot::GetItem(l.name.c_str());item && !item->Show && !w.Busy())(void)w.Exec(Cmd::UpdateRun::With({{"device_id",device.id},{"run_id",l.runId},{"visible",false}}));
    }
}
void HistoryProfiles(Workflow &w,const char *field,double scale=1.,bool gr=false,bool dashed=false) {
    PlotDataScope scope;
    const auto &device=w.Current();
    auto &entries=Cache().entries;HEntry *e=nullptr;
    for(auto &c:entries) if(c.dev==&device && c.profiles && c.gr==gr && c.dashed==dashed && c.scale==scale && c.bias==device.selectedBias && c.ykey==field && c.selected==device.selectedRun) {e=&c;break;}
    if(!e) {
        ++PlotCacheBuilds();e=&entries.emplace_back();e->dev=&device;e->profiles=true;e->gr=gr;e->dashed=dashed;e->scale=scale;e->bias=device.selectedBias;e->ykey=field;e->selected=device.selectedRun;
        for(const auto &run:device.history) {
            if(!run.at("visible").get<bool>() || !ShowRun(run) || run.at("run_id")==device.selectedRun || (run.at("kind")!="iv" && run.at("kind")!="band"))continue;
            const auto typed=HistoryTyped(device,run);const auto iv=dynamic_cast<const IvResult *>(typed.get());
            if(!iv || !(iv->has>>31&1u) || iv->points.empty())continue;
            const auto &point=iv->points[std::clamp(device.selectedBias,0,int(iv->points.size())-1)];
            if(point.verbatim || !point.valid)continue;
            HLine l;
            if(gr) {if(point.details){const auto& g=point.details->generation_recombination;l.x=g.x_m.get();if(field=="generation_m3_s")l.y=g.generation_m3_s.get();else continue;}else{const auto *g=ChildOf(point.rest,"generation_recombination");if(!g || !NumbersOf(*g,"x_m",l.x) || !NumbersOf(*g,field,l.y))continue;}}
            else if(!PointArray(point,"x_m",l.x) || !PointArray(point,field,l.y))continue;
            if(l.x.size()!=l.y.size())continue;
            for(auto &v:l.x)v*=1e6;for(auto &v:l.y){v*=scale;if(gr && v<=0)v=NAN;}
            const auto c=run.at("colour").get<std::vector<float>>();
            l.colour=ImVec4(c[0],c[1],c[2],c[3]); // the selected run is never in this list, so it keeps its run colour
            l.name=std::string(field)+" / "+RunLabel(run)+"##"+run.at("run_id").get<std::string>();
            e->lines.push_back(std::move(l));
        }
    }
    for(const auto &l:e->lines) {
        ImPlot::PlotLine(l.name.c_str(),l.x.data(),l.y.data(),int(l.x.size()),{ImPlotProp_LineColor,ImVec4(l.colour.x,l.colour.y,l.colour.z,dashed?.6f:1.f),ImPlotProp_LineWeight,1.f});
        if(dashed){ImPlot::PushPlotClipRect();DashedLine(l.x,l.y,l.colour);ImPlot::PopPlotClipRect();}
    }
}
bool BeginSavedPlot(Workflow &w,const char *title,ImVec2 size) {return BeginSavedPlot(w.plotSession,title,size);}
void EndSavedPlot(Workflow &w,const char *title) {if(EndSavedPlot(w.plotSession,title)){w.projectDirty=true;w.RefreshDirty();}}

}
int PlotCacheBuildCount() {return PlotCacheBuilds();}
void SetPlotDataHook(void (*hook)(bool)) {PlotDataHook=hook;}
double PlotCacheSum() {
    double sum=0;const auto add=[&](const std::vector<double> &v){for(double d:v)if(std::isfinite(d))sum+=d;};
    for(const auto &[key,c]:Cache().cols){add(c.a);add(c.b);add(c.c);}
    for(const auto &e:Cache().entries)for(const auto &l:e.lines){add(l.x);add(l.y);}
    return sum;
}
void DrawHistory(Workflow &w,bool global) {
    ImGui::PushID(global?"global-history":"device-history");
    ImGui::BeginDisabled(w.Busy());
    if(ImGui::Button(global?"Clear unpinned (all devices)":"Clear unpinned (active device)")) (void)w.Exec(Cmd::ClearHistory::With({{"scope",global?"all":"device"}}));
    ImGui::SameLine();if(ImGui::Button(global?"Clear including pinned...":"Clear active including pinned..."))ImGui::OpenPopup("Confirm clear");
    if(ImGui::BeginPopupModal("Confirm clear",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(global?"Clear all runs from every device, including pinned?":"Clear all runs from the active device, including pinned?");
        if(ImGui::Button("Clear all")){(void)w.Exec(Cmd::ClearHistory::With({{"scope",global?"all":"device"},{"include_pinned",true}}));ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    int retention=w.project.ui.value("history_retention",5);
    BeginRow("Legacy runs retained");if(SweepPoints("##retention",retention,"history_retention")) {auto ui=w.project.ui;ui["history_retention"]=retention;(void)w.Exec(Cmd::EditProject::With({{"ui",ui}}));}EndRow();
    // Commands replace device state; draw from a snapshot while dispatching edits.
    const auto devices=w.project.devices;
    for(const auto &device:devices) {
        ImGui::PushID(device->id.c_str());
        if(ImGui::TreeNodeEx("device",ImGuiTreeNodeFlags_DefaultOpen,"%s (%zu)",device->name.c_str(),device->history.size())) {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(4,2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(3,2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(4,2));
            if(ImGui::BeginTable("runs",7,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Colour",ImGuiTableColumnFlags_WidthFixed,38);
                ImGui::TableSetupColumn("Visible",ImGuiTableColumnFlags_WidthFixed,45);
                ImGui::TableSetupColumn("Pin",ImGuiTableColumnFlags_WidthFixed,38);
                ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Technique",ImGuiTableColumnFlags_WidthFixed,72);
                ImGui::TableSetupColumn("Time",ImGuiTableColumnFlags_WidthFixed,78);
                ImGui::TableSetupColumn("Info",ImGuiTableColumnFlags_WidthFixed,28);
                ImGui::TableHeadersRow();
                static ImGuiID rename=0;static bool focusRename=false;static char name[1024]{};
                const auto history=device->history;
                const auto row=[&](const Json &run) {
                    ImGui::PushID(run.at("run_id").get<std::string>().c_str());
                    ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                    Json update={{"operation","history_update"},{"device_id",device->id},{"run_id",run.at("run_id")}};bool changed=false;
                    auto colour=run.at("colour").get<std::vector<float>>();
                    const float size=ImGui::GetFrameHeight();
                    if(ImGui::ColorButton("##swatch",ImVec4(colour[0],colour[1],colour[2],colour[3]),0,ImVec2(size,size)))ImGui::OpenPopup("Colour");
                    if(ImGui::BeginPopup("Colour")) {
                        ImGui::SetNextItemWidth(180);
                        if(ImGui::ColorPicker4("##picker",colour.data(),ImGuiColorEditFlags_NoSidePreview|ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar)){update["colour"]=colour;changed=true;}
                        ImGui::EndPopup();
                    }
                    ImGui::TableSetColumnIndex(1);bool visible=run.at("visible");
                    if(ImGui::Checkbox("##visible",&visible)){update["visible"]=visible;changed=true;}
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Visible in plots");
                    ImGui::TableSetColumnIndex(2);const bool pin=run.at("pinned");
                    const auto pos=ImGui::GetCursorScreenPos();
                    if(ImGui::InvisibleButton("##pin",ImVec2(size,size),ImGuiButtonFlags_EnableNav)){update["pinned"]=!pin;changed=true;}
                    if(ImGui::IsItemHovered())ImGui::SetTooltip(pin?"Unpin run":"Keep / Pin run");
                    if(run.at("run_id")==device->selectedRun)w.tutorialOverlay.Item("scapslab.History","keep");
                    auto *draw=ImGui::GetWindowDrawList();const auto ink=ImGui::GetColorU32(pin?ImGuiCol_CheckMark:ImGuiCol_TextDisabled);
                    const float x=pos.x+size*.5f,y=pos.y+size*.25f;
                    draw->AddRectFilled(ImVec2(x-3,y),ImVec2(x+3,y+6),ink);draw->AddLine(ImVec2(x-5,y+6),ImVec2(x+5,y+6),ink,2);draw->AddLine(ImVec2(x,y+6),ImVec2(x,y+13),ink,1.5f);
                    ImGui::RenderNavCursor(ImGui::GetCurrentContext()->LastItemData.Rect,ImGui::GetItemID());
                    ImGui::TableSetColumnIndex(3);
                    const auto label=RunLabel(run);
                    const auto editId=ImGui::GetID("rename");
                    if(rename==editId) {
                        ImGui::SetNextItemWidth(-1);
                        if(focusRename){ImGui::SetKeyboardFocusHere();focusRename=false;}
                        const bool entered=ImGui::InputText("##rename",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue);
                        if(ImGui::IsKeyPressed(ImGuiKey_Escape))rename=0;
                        else if(entered || ImGui::IsItemDeactivatedAfterEdit()){update["name"]=name;changed=true;rename=0;}
                        else if(ImGui::IsItemDeactivated())rename=0;
                    } else {
                        const bool clicked=ImGui::Selectable((label+"###label").c_str(),run.at("run_id")==device->selectedRun,ImGuiSelectableFlags_AllowDoubleClick,ImVec2(0,size));
                        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\nDouble-click to rename",label.c_str());
                        if(clicked) {
                            if(ImGui::IsMouseDoubleClicked(0)){rename=editId;focusRename=true;std::snprintf(name,sizeof(name),"%s",label.c_str());}
                            else (void)w.Exec(Cmd::SelectRun::With({{"device_id",device->id},{"run_id",run.at("run_id")}}));
                        }
                    }
                    ImGui::TableSetColumnIndex(4);ImGui::TextUnformatted(run.at("kind").get<std::string>().c_str());
                    ImGui::TableSetColumnIndex(5);
                    const std::time_t seconds=run.at("time_unix_ms").get<std::int64_t>()/1000;std::tm local{};
                    localtime_s(&local,&seconds);char time[32];std::strftime(time,sizeof(time),"%H:%M:%S",&local);ImGui::TextUnformatted(time);
                    if(ImGui::IsItemHovered()){std::strftime(time,sizeof(time),"%Y-%m-%d %H:%M:%S",&local);ImGui::SetTooltip("%s (local)\nElapsed: %.3g s",time,run.at("elapsed_s").get<double>());}
                    ImGui::TableSetColumnIndex(6);
                    if(!run.value("profiles_stored",true)){ImGui::TextDisabled("(i)");if(ImGui::IsItemHovered())ImGui::SetTooltip("Profiles not stored. Recalculate this model to view spatial profiles.");}
                    if(changed && !w.Busy())(void)w.Exec(Cmd::UpdateRun::FromJson(update));
                    ImGui::PopID();
                };
                std::set<std::string> groups;
                for(const auto &run:history) {
                    const auto group=run.value("group_id",std::string{});
                    if(group.empty()){row(run);continue;}
                    if(!groups.insert(group).second)continue;
                    ImGui::PushID(group.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(3);
                    const bool open=ImGui::TreeNodeEx("sweep",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_NoTreePushOnOpen,"Sweep");
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",group.c_str());
                    ImGui::SameLine();const auto start=ImGui::GetCursorScreenPos();
                    const auto total=std::count_if(history.begin(),history.end(),[&](const Json &member){return member.value("group_id",std::string{})==group;});
                    const float step=90.f/float(total);int count=0;
                    for(const auto &member:history)if(member.value("group_id",std::string{})==group) {
                        const auto c=member.at("colour").get<std::vector<float>>();
                        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x+count*step,start.y+3),ImVec2(start.x+(count+1)*step,start.y+14),ImGui::ColorConvertFloat4ToU32(ImVec4(c[0],c[1],c[2],c[3])));++count;
                    }
                    ImGui::Dummy(ImVec2(90,17));ImGui::SameLine();ImGui::TextDisabled("%d runs",count);
                    if(open)for(const auto &member:history)if(member.value("group_id",std::string{})==group)row(member);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar(3);ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();ImGui::PopID();
}
// Slider with live-drag features: relative Shift / Ctrl+Shift / F fine mode, Esc revert, drag tooltip, arrows and Ctrl+wheel steps. Returns true when value changed.
static bool Slide(const char *id,double &value,const SliderMap &m,const char *format) {
    static ImGuiID dragId=0;static SliderDrag drag;
    const double before=value;double v=value;auto &io=ImGui::GetIO();
    const bool nativeChanged=ImGui::SliderScalar(id,ImGuiDataType_Double,&v,&m.lo,&m.hi,format,m.log?ImGuiSliderFlags_Logarithmic:ImGuiSliderFlags_None);
    const ImGuiID item=ImGui::GetItemID();const bool typing=ImGui::TempInputIsActive(item),active=ImGui::IsItemActive() && !typing && ImGui::IsMouseDown(0);
    const bool lock=numericWorkflow && numericWorkflow->fineLock;
    bool changed=nativeChanged;value=v;    if(active) {
        if(numericWorkflow) numericWorkflow->dragSeen=true;
        if(dragId!=item) {dragId=item;drag.Begin(before);}
        const float width=ImGui::GetItemRectSize().x-ImGui::GetStyle().GrabMinSize;
        value=drag.Update(m,v,before,io.MousePos.x,width,SliderScale(io.KeyShift,io.KeyCtrl,lock));changed=value!=before;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {value=drag.start;changed=value!=before;ImGui::ClearActiveID();dragId=0;ImGui::SetTooltip("Cancelled");}
        else {
            char a[48],b[48];std::snprintf(a,sizeof(a),"%.4g",drag.start);std::snprintf(b,sizeof(b),"%.4g",value);
            if(m.log) ImGui::SetTooltip("%s -> %s  (x%.3g)%s",a,b,drag.start>0?value/drag.start:0.,drag.relative?"  [fine]":"");
            else ImGui::SetTooltip("%s -> %s  (%+.3g)%s",a,b,value-drag.start,drag.relative?"  [fine]":"");
        }
    } else {
        if(dragId==item) dragId=0;
        if(!typing && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) {
            const double fine=io.KeyShift?.1:1.;double steps=0;
            if(ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)) steps+=1;
            if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)) steps-=1;
            if(ImGui::IsItemHovered() && io.KeyCtrl && io.MouseWheel!=0) steps+=io.MouseWheel;
            if(steps!=0) {value=m.Step(value,steps*fine);changed=value!=before;}
        }
    }
    return changed;
}
Bounds DisplayBounds(std::string_view key,double scale) {
    if(key.starts_with("sweep.step:") || key.starts_with("sweep.exponent:")) {
        const bool exponent=key.starts_with("sweep.exponent:");const auto property=DisplayBounds(key.substr(key.find(':')+1),scale);
        auto bounds=BoundsTable()->at(exponent?"sweep.exponent":"sweep.step");
        if(exponent) {
            bounds.hardMin=property.hardMin>0?std::ceil(std::log10(property.hardMin)):bounds.hardMin;
            bounds.hardMax=std::floor(std::log10(property.hardMax));
            bounds.softMin=std::floor(std::log10(std::max(property.softMin,property.step)));
            bounds.softMax=std::floor(std::log10(property.softMax));
        }else {
            bounds.hardMax=property.hardMax-property.hardMin;
            bounds.softMin=property.step;bounds.softMax=std::max(property.step*2,property.softMax-property.softMin);
            bounds.unit=property.unit;bounds.format=property.format;bounds.integer=property.integer;
            if(property.integer){bounds.hardMin=property.step;bounds.step=property.step;}
        }
        return bounds;
    }
    const auto canonical=ParameterBoundsKey(key);auto bounds=BoundsTable()->at(canonical);
    const double factor=scale/ParameterToSI(canonical);
    const double uiScale=bounds.displayScale*ParameterToSI(canonical);
    if(std::abs(scale-uiScale)<=1e-12*uiScale)bounds.unit=bounds.displayUnit;
    bounds.hardMin*=factor;bounds.hardMax*=factor;bounds.softMin*=factor;bounds.softMax*=factor;bounds.step*=factor;
    return bounds;
}
bool DrawBoundValue(const char* id,double& native,std::string_view key,double scale,const void* address,const char* label) {
    const auto bounds=DisplayBounds(key,scale);const double before=native;double value=native*scale;
    const auto [lo,hi]=bounds.SliderRange(value);const bool logarithmic=bounds.scale==ParameterScale::Log;
    bool changed=false;const bool scoped=std::string_view(id)!="##number";if(scoped)ImGui::PushID(id);ImGui::BeginGroup();
    if(logarithmic) {
        bool zero=value==0;
        if(bounds.hardMin==0) {ImGui::TextUnformatted("Zero");ImGui::SameLine();changed=ImGui::Checkbox("##zero",&zero);TrackGesture();ImGui::SameLine();}
        double v=value>0?value:bounds.softMin,m=1;int e=0;SplitMantissaExponent(v,m,e);
        const auto [emin,emax]=bounds.ExponentRange(v);const auto mantissa=BoundsTable()->at("sweep.mantissa");
        ImGui::BeginDisabled(zero);const float gap=ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(40.f,(ImGui::GetContentRegionAvail().x-gap)*.58f));
        if(Slide("##mantissa",m,{mantissa.softMin,mantissa.softMax,false,bounds.step/std::pow(10.,e)},zero?"0":bounds.format.c_str())) {
            changed=true;v=(m<1 || m>=10)?m:m*std::pow(10.,e);
        }
        TrackGesture();SweepMenu(address,std::string(key).c_str(),scale,label,SweepComponent::Mantissa);ImGui::SameLine();
        double exponent=e;ImGui::SetNextItemWidth(-1);
        if(ImGui::SliderScalar("##exponent",ImGuiDataType_Double,&exponent,&emin,&emax,zero?"0":"10^%.0f")) {
            changed=true;v=m*std::pow(10.,exponent);
        }
        TrackGesture();SweepMenu(address,std::string(key).c_str(),scale,label,SweepComponent::Exponent);ImGui::EndDisabled();
        if(changed)value=zero?0:v;
    }else {
        ImGui::SetNextItemWidth(-1);
        changed=Slide("##value",value,{lo,hi,false,bounds.step},bounds.format.c_str());TrackGesture();
        SweepMenu(address,std::string(key).c_str(),scale,label);
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s | hard [%.6g, %.6g], typical [%.6g, %.6g]\nCtrl+click: exact entry (log: full value in mantissa if outside [1,10)).\nShift / Ctrl+Shift: fine; F: lock; Esc: cancel.\n%s\n%s",label,bounds.hardMin,bounds.hardMax,bounds.softMin,bounds.softMax,bounds.source.c_str(),bounds.note.c_str());
    if(changed && !bounds.Contains(value)) {
        const auto message=bounds.Error(key,value);
        if(numericWorkflow)numericWorkflow->status=message;
        ImGui::TextColored({1,.45f,.25f,1},"%s",message.c_str());changed=false;value=before*scale;
    }
    else if(!bounds.Contains(value))ImGui::TextColored({1,.45f,.25f,1},"outside hard limits: %s",bounds.Error(key,value).c_str());
    if(!bounds.Typical(value) && bounds.Contains(value))ImGui::TextColored({1,.72f,.25f,1},"outside typical range");
    ImGui::EndGroup();if(scoped)ImGui::PopID();if(changed)native=value/scale;return changed;
}
void DrawParameterBoundsPreferences(Workflow& w) {
    ImGui::TextWrapped("Hard limits reject edits; typical ranges guide sliders. Values outside the typical range remain editable within hard limits. Units are fixed by the model.");
    ImGui::BeginDisabled(w.boundsFileBusy);
    if(ImGui::Button("Open user file"))w.AsyncIo("Open parameter bounds",[]{
        auto error=EnsureUserBoundsFile();if(!error.empty())return error;
        const auto windows=std::getenv("WINDIR");if(!windows)return std::string("Windows directory is unavailable");
        Platform::ProcessRunOptions options;options.executable=Path{std::filesystem::path(windows)/"System32/notepad.exe"};options.arguments={UserBoundsFile()};
        const auto result=Platform::LaunchDetached(options);return result?std::string{}:result.Error().technicalDetails;
    });
    ImGui::SameLine();if(ImGui::Button("Reload"))(void)w.Exec(Cmd::BoundsFile::Make(BoundsFileAction::Reload));
    ImGui::SameLine();if(ImGui::Button("Reset to defaults"))(void)w.Exec(Cmd::BoundsFile::Make(BoundsFileAction::Reset));ImGui::EndDisabled();
    ImGui::TextDisabled("scapslab/parameter_bounds.yaml");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",UserBoundsFile().c_str());
    if(!w.boundsFileDiagnostic.empty())ImGui::TextWrapped("%s",w.boundsFileDiagnostic.c_str());
    if(ImGui::BeginTable("Parameter bounds",7,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,ImVec2(0,ImGui::GetContentRegionAvail().y),1080)) {
        for(const auto* title:{"Parameter","UI unit","Hard limits","Typical slider range","Scale","Step / format","Origin"})ImGui::TableSetupColumn(title);
        ImGui::TableSetupScrollFreeze(1,1);ImGui::TableHeadersRow();const auto table=BoundsTable();
        for(const auto& [key,b]:*table) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(key.c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("SI: %s\n%s\n%s",b.unit.c_str(),b.source.c_str(),b.note.c_str());
            ImGui::TableNextColumn();ImGui::TextUnformatted(b.displayUnit.c_str());
            ImGui::TableNextColumn();ImGui::Text("%.5g .. %.5g%s",b.hardMin*b.displayScale,b.hardMax*b.displayScale,b.allowInfinity?"; +inf":"");
            ImGui::TableNextColumn();ImGui::Text("%.5g .. %.5g",b.softMin*b.displayScale,b.softMax*b.displayScale);
            ImGui::TableNextColumn();ImGui::TextUnformatted(b.scale==ParameterScale::Log?"log":"linear");
            ImGui::TableNextColumn();ImGui::Text("%.4g / %s",b.step*b.displayScale,b.format.c_str());
            ImGui::TableNextColumn();ImGui::TextUnformatted(b.userOverride?"user":"default");
        }
        ImGui::EndTable();
    }
}
static bool NumberControl(const char *label,double &native,const char *key,double scale,const void* address) {
    BeginRow(label);const bool changed=DrawBoundValue("##number",native,key,scale,address,label);EndRow();return changed;
}
template<class T> requires std::is_arithmetic_v<T>
static bool Number(const char* label,T& native,const char* key,double scale=1) {double value=double(native);const bool changed=NumberControl(label,value,key,scale,&native);if(changed)native=static_cast<T>(value);return changed;}
static bool Number(const char* label,Json& object,const char* key,double scale=1) {if(!object.at(key).is_number())return false;double value=object.at(key);const bool changed=NumberControl(label,value,key,scale,nullptr);if(changed)object[key]=(std::string_view(key)=="points_per_layer" || std::string_view(key)=="points" || std::string_view(key)=="max_newton_iterations")?Json(std::int64_t(value)):Json(value);return changed;}
static bool Choice(const char* label,std::string& current,const char*,std::initializer_list<std::pair<const char*,const char*>> items) {
    bool changed=false;const char* preview=current.c_str();for(const auto& [name,value]:items)if(current==value)preview=name;
    BeginRow(label);if(ImGui::BeginCombo("##value",preview)){for(const auto& [name,value]:items)if(ImGui::Selectable(name,current==value)){current=value;changed=true;++gesture;}ImGui::EndCombo();}EndRow();return changed;
}
template<class T> requires std::is_enum_v<T>
static bool Choice(const char* label,T& current,const char*,std::initializer_list<std::pair<const char*,T>> items) {
    bool changed=false;const char* preview="";for(const auto& [name,value]:items)if(current==value)preview=name;
    BeginRow(label);if(ImGui::BeginCombo("##value",preview)){for(const auto& [name,value]:items)if(ImGui::Selectable(name,current==value)){current=value;changed=true;++gesture;}ImGui::EndCombo();}EndRow();return changed;
}
static bool Choice(const char *label,Json &j,const char *key,std::initializer_list<std::pair<const char*,const char*>> items) {
    const auto current=j.at(key).get<std::string>(); bool changed=false;
    const char *preview=current.c_str(); for(const auto &[name,value]:items) if(current==value) preview=name;
    BeginRow(label);
    if(ImGui::BeginCombo("##value",preview)) {for(const auto &[name,value]:items) if(ImGui::Selectable(name,current==value)) {j[key]=value;changed=true;++gesture;} ImGui::EndCombo();} EndRow();return changed;
}
bool Panel::RunActions(ProjectDraft& draft) {
    const auto source=w->Current().runPlan;auto plan=CreateRef<RunPlan>(*source);bool changed=false;
    plan->actions[1].onEdit |=w->autoRecalculate;plan->actions[0].onEdit |=w->autoRecalculateBand;
    const char* labels[]={"Band / profiles","I-V","C-V","C-f","QE"};
    const bool conditions=Number("Temperature [K]",draft.project.temperature_K,"temperature_K");w->tutorialOverlay.Item("scapslab.Run","temperature");
    ImGui::TextWrapped("Voltage: left minus right. Include selects Single shot; On edit updates Live without storing Results.");
    if(actionSource.lock()!=source) {
        for(std::size_t i=0;i<plan->actions.size();++i) {
            SweepEditor editor;editor.displayUnit=i==3?"Hz":i==4?"nm":"V";
            if(plan->actions[i].range)editor.range=*plan->actions[i].range;
            else {const auto& v=draft.project.voltage;editor.range={SweepSampling::Step,false,v.start_V,v.stop_V,std::abs(v.step_V),2};if(v.start_V>v.stop_V){editor.range.sampling=SweepSampling::List;editor.range.custom=ActionSamples(plan->actions[i],draft.project);}}
            InitSweepControl(actionEditors[i],std::move(editor),i==3?"frequency_Hz":i==4?"wavelength_nm":"voltage_V");
        }
        actionSource=source;
    }
    const auto& style=ImGui::GetStyle();
    const float numeric=std::max(110.f,ImGui::CalcTextSize("-0.5000 V").x+style.FramePadding.x*2);
    const float actionWidth=ImGui::CalcTextSize("Band / profiles").x;
    const float checkWidth=ImGui::CalcTextSize("Include").x;
    const float scaleWidth=ImGui::CalcTextSize("Log").x+ImGui::GetFrameHeight()+style.FramePadding.x*2;
    const float moreWidth=ImGui::CalcTextSize("More").x+style.FramePadding.x*2;
    const float minimum=actionWidth+2*checkWidth+4*numeric+scaleWidth+moreWidth+18*style.CellPadding.x;
    const bool scroll=ImGui::GetContentRegionAvail().x<minimum;bool more=false;
    if(ImGui::BeginTable("Run Actions",9,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_NoKeepColumnsVisible|(scroll?ImGuiTableFlags_ScrollX:0),ImVec2(0,scroll?ImGui::GetFrameHeightWithSpacing()*6+style.ScrollbarSize+style.CellPadding.y*12:0),scroll?minimum:0)) {
        ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);
        for(const auto* title:{"Include","On edit"})ImGui::TableSetupColumn(title,ImGuiTableColumnFlags_WidthFixed,checkWidth);
        for(const auto* title:{"From","To","Step / Points"})ImGui::TableSetupColumn(title,ImGuiTableColumnFlags_WidthFixed,numeric);
        ImGui::TableSetupColumn("Scale",ImGuiTableColumnFlags_WidthFixed,scaleWidth);
        ImGui::TableSetupColumn("At",ImGuiTableColumnFlags_WidthFixed,numeric);
        ImGui::TableSetupColumn("More",ImGuiTableColumnFlags_WidthFixed,moreWidth);
        ImGui::TableHeadersRow();
        for(int i:{1,2,3,4,0}) {
            auto& a=plan->actions[i];auto& state=actionEditors[i];auto& range=state.editor.range;
            ImGui::PushID(ActionId(a.kind));ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(labels[i]);
            ImGui::TableNextColumn();changed|=ImGui::Checkbox("##include",&a.include);
            ImGui::TableNextColumn();changed|=ImGui::Checkbox("##on-edit",&a.onEdit);
            const auto cell=[&](const char* id,double& value,const char* unit,bool=false) {
                return DrawBoundValue(id,value,std::string_view(unit)=="Hz"?"frequency_Hz":std::string_view(unit)=="nm"?"wavelength_nm":std::string_view(unit)=="dec"?"sweep.decades":"voltage_V");
            };
            ImGui::TableNextColumn();bool rangeChanged=false;
            if(i==0)changed|=cell("##bias",a.voltage_V,"V");
            else {
                ImGui::BeginDisabled(range.sampling==SweepSampling::List);
                rangeChanged|=SweepNumber(state,"##from",range.minimum);
                ImGui::TableNextColumn();rangeChanged|=SweepNumber(state,"##to",range.maximum);
                ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);
                if(range.sampling==SweepSampling::Step)rangeChanged|=SweepNumber(state,"##step",range.step,true);
                else rangeChanged|=SweepPoints("##points",range.points);
                ImGui::EndDisabled();
            }
            ImGui::TableSetColumnIndex(6);
            if(i!=0){int scale=range.logarithmic;ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##scale",&scale,"Lin\0Log\0")){range.logarithmic=scale;rangeChanged=true;}}
            ImGui::TableNextColumn();
            if(i==2){ImGui::TextUnformatted("f");ImGui::SameLine();changed|=cell("##frequency",a.frequency_Hz,"Hz",true);}
            if(i==3){ImGui::TextUnformatted("Bias");ImGui::SameLine();changed|=cell("##voltage",a.voltage_V,"V");}
            if(rangeChanged){a.range=range;state.error.clear();changed=true;}
            ImGui::TableNextColumn();if(ImGui::Button("...")){selectedAction=i;more=true;}
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(more)ImGui::OpenPopup("Action settings");
    ImGui::SetNextWindowSize(ImVec2(600,0),ImGuiCond_Appearing);
    if(ImGui::BeginPopup("Action settings")) {
    ImGui::SeparatorText(labels[selectedAction]);auto& a=plan->actions[selectedAction];ImGui::PushID(ActionId(a.kind));
    bool ownTemperature=a.temperature_K>0;
    if(ImGui::BeginTable("Action temperature",2)) {
        ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Override temperature");ImGui::SameLine();
        if(ImGui::Checkbox("##override",&ownTemperature)){a.temperature_K=ownTemperature?draft.project.temperature_K:0;changed=true;}
        ImGui::TableNextColumn();ImGui::BeginDisabled(!ownTemperature);double temperature=ownTemperature?a.temperature_K:draft.project.temperature_K;if(Number("T [K]",temperature,"temperature_K") && ownTemperature){a.temperature_K=temperature;changed=true;}ImGui::EndDisabled();ImGui::EndTable();
    }
    if(a.kind==ActionKind::Band || a.kind==ActionKind::Cf)changed|=Number("Bias [V]",a.voltage_V,"voltage_V");
    if(a.kind!=ActionKind::Band) {
        auto& state=actionEditors[selectedAction];
        if(SweepRangeControl(state,true)) {a.range=state.editor.range;changed=true;}
        if(!state.error.empty())ImGui::TextWrapped("%s",state.error.c_str());
        else try {const auto values=SweepValues(state.editor.range);ImGui::Text("Range [%s]: %zu samples; first %.9g, last %.9g",state.editor.displayUnit.c_str(),values.size(),values.front(),values.back());}catch(const std::exception& e){ImGui::TextWrapped("%s",e.what());}
    }
    if(a.kind==ActionKind::Cv)changed|=Number("Frequency [Hz]",a.frequency_Hz,"frequency_Hz");
    if(a.kind==ActionKind::Qe) {
        changed|=Toggle("Constant photon flux",a.constantFlux);
        if(a.constantFlux)changed|=Number("Photon flux [m^-2 s^-1]",a.photon_flux_m2_s,"probe_photon_flux_m2_s");
        else changed|=Number("Probe power [W/m^2]",a.probe_power_W_m2,"probe_power_W_m2");
    }
    ImGui::PopID();ImGui::EndPopup();
    }
    std::string error;try {ValidateRunPlan(*plan,draft);for(const auto& state:actionEditors)if(!state.error.empty())throw std::invalid_argument(state.error);}catch(const std::exception& e){error=e.what();}
    if(changed && error.empty()) {Do(Cmd::SetRunPlan::Make(plan));actionSource=plan;}
    else if(changed) { // Include may be empty while editing; invalid range text stays local until corrected.
        try {for(const auto& next:plan->actions)if(next.range)(void)SweepValues(*next.range);Do(Cmd::SetRunPlan::Make(plan));actionSource=plan;}catch(const std::exception&){}
    }
    if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
    ImGui::BeginDisabled(w->Busy() || !error.empty());
    if(ImGui::Button("Single shot"))Do(Cmd::SingleShot::Make(plan));w->tutorialOverlay.Item("scapslab.Run","run");
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Results"))w->focusView=18;
    ImGui::SameLine();ImGui::TextUnformatted("Pause after Action");ImGui::SameLine();changed|=ImGui::Checkbox("##pause-action",&plan->pauseAfterAction);
    ImGui::SameLine();ImGui::TextUnformatted("Member");ImGui::SameLine();changed|=ImGui::Checkbox("##pause-member",&plan->pauseAfterMember);
    if(changed && (plan->pauseAfterAction!=source->pauseAfterAction || plan->pauseAfterMember!=source->pauseAfterMember))Do(Cmd::SetRunPlan::Make(plan));
    if(const auto op=w->ActiveOp();op && op->paused) {if(ImGui::Button("Resume"))Do(Cmd::ResumeRun::With());}
    return conditions;
}
void Panel::RenderContent() {
    if(w->plotSession.epoch!=w->epoch){w->plotSession.settings.Read(w->project.ui);w->plotSession.runtime.clear();w->plotSession.epoch=w->epoch;}
    struct PerfScope {const char *n;std::chrono::steady_clock::time_point t=std::chrono::steady_clock::now();~PerfScope(){static const bool on=std::getenv("SCAPSLAB_PERF")!=nullptr;if(on)SST::PerfAdd(std::string("panel ")+n,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}} perfScope{names[view]};
    w->tutorialOverlay.Window(std::string("scapslab.")+names[view]);
    w->WatchPlots();
    numericWorkflow=w;numericDraft=nullptr;
    struct ResetNumeric {~ResetNumeric(){numericDraft=nullptr;numericWorkflow=nullptr;}} resetNumeric;
    if(!placed && !gallery) {auto s=GetState();const auto *vp=ImGui::GetMainViewport();s.x+=vp->Pos.x;s.y+=vp->Pos.y;ApplyState(s);placed=true;}
    ImGui::TextDisabled("SCAPSLAB / %s",names[view]);
    if(w->fineLock || ImGui::GetIO().KeyShift) {ImGui::SameLine();ImGui::TextColored(ImVec4(1.f,.75f,.2f,1.f),"[FINE%s]",w->fineLock?" lock, F":"");}
    {static const std::map<int,const char*> subtitles={{5,"Spatial profiles (bands, carriers) vs. position x at the selected bias point"},{6,"Capacitance vs. DC bias at fixed frequency"},{7,"1/C^2 vs. DC bias (built-in potential, doping)"},{8,"Capacitance and G/omega vs. frequency at fixed bias"},{9,"G/omega vs. frequency at fixed bias"},{10,"J-V curve of the checked devices; marker = selected bias point"},{22,"Generation and recombination rates vs. position x at the selected bias point"}};
     if(const auto it=subtitles.find(view);it!=subtitles.end()) { // compact header: help text is a tooltip on (?), status stays on the same grey line
         ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",it->second);
         if(view==5 && !w->status.empty()){ImGui::SameLine();ImGui::TextDisabled("| %s",w->status.c_str());}}}
    if(view==4 || (view!=5 && w->response.is_object() && !w->response.value("ok",true))) ImGui::TextWrapped("%s",w->status.c_str());
    if(view==5 || view==10 || view==22) {} else ImGui::Separator();
    if(w->Current().Document().contains("scaps_native")) for(const auto &warning:w->Current().Document().at("scaps_native").value("warnings",Json::array())) ImGui::TextWrapped("SCAPS import warning: %s",warning.get<std::string>().c_str());
    if(!pinLayer.empty() && w->project.active_device!=pinDevice) { // extra "Layer: <name>" window of a device that is not active
        ImGui::TextWrapped("This layer belongs to another device.");
        if(ImGui::Button("Activate that device")) Do(Cmd::SelectDevice::With({{"device_id",pinDevice}}));
        return;
    }
    if((view==5 || view==10 || view==15 || view==16 || view==17 || view==18 || view==22 || (view>=6 && view<10)) && !w->ViewsReady()) {ImGui::TextDisabled("Preparing results...");return;} // Json views are built on a worker, never on the UI thread
    if(view==16) {Batch();return;}
    if(view==17) {Recorder();return;}
    if(view==18) {ResultsBrowser();return;}
    if(view==0) {Devices();return;}
    if(view==15) {DrawHistory(*w);return;}
    if(view==10) {Compare();return;}
    if(view==5) {Results();return;}
    if(view==22) {GenerationRecombination();return;}
    if(view>=6 && view<10) {AC();return;}
    if(view==19)ShotBar(*w,"qe");
    const bool replacing=w->ReplacingProject();
    ImGui::BeginDisabled(replacing);
    if(w->Current().draft) {
        auto draft=*(w->EditDraft()?w->EditDraft():w->Current().draft.get());
        numericDraft=&draft;
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
            auto &e=draft.project.external;
            ImGui::TextWrapped("R_area = R x area. The solver stores Ohm m^2; controls show Ohm cm^2. Rs = 0 is allowed.");
            changed|=Number("Device area [cm^2]",e.area_m2,"area_m2",1e4);
            changed|=Number("Rs area [Ohm cm^2]",e.series_ohm_m2,"series_ohm_m2",1e4);
            bool open=std::isinf(e.shunt_ohm_m2);
            if(Toggle("Rsh infinite (open)",open)) {e.shunt_ohm_m2=open?INFINITY:1.;changed=true;}
            if(!open) changed|=Number("Rsh area [Ohm cm^2]",e.shunt_ohm_m2,"shunt_ohm_m2",1e4);
            const double area=e.area_m2;
            ImGui::Text("Rs = %.6g Ohm",e.series_ohm_m2/area);
            if(open) ImGui::TextUnformatted("Rsh = infinite Ohm");else ImGui::Text("Rsh = %.6g Ohm",e.shunt_ohm_m2/area);
        } else if(view==14) changed|=Structure(draft);
        else if(view==1) {
            auto &layers=draft.project.layers; int pinned=0; auto &selected=pinLayer.empty()?w->Current().selectedLayer:pinned; // pinned window: layer found by stable ID, independent of the tree selection
            if(!pinLayer.empty()) for(int i=0;i<int(layers.size());++i) if(draft.layers[i].id==pinLayer) pinned=i;
            selected=std::clamp(selected,0,int(layers.size())-1);
            if(pinLayer.empty()) {BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].name.c_str())) {
                for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i); if(ImGui::Selectable(layers[i].name.c_str(),selected==i)) selected=i; ImGui::PopID();} ImGui::EndCombo();
            }
            EndRow();}
            auto &l=layers[selected]; const auto& id=draft.layers[selected].id;
            if(ImGui::Button("Add")) Do(Cmd::EditLayer::With({{"action","add"}})); if(pinLayer.empty()) w->tutorialOverlay.Item("scapslab.Layers","add-layer"); ImGui::SameLine();
            if(ImGui::Button("Remove")) Do(Cmd::EditLayer::With({{"action","remove"},{"layer_id",id}})); ImGui::SameLine();
            if(ImGui::Button("Up") && selected>0) Do(Cmd::EditLayer::With({{"action","move"},{"layer_id",id},{"index",selected-1}})); ImGui::SameLine();
            if(ImGui::Button("Down") && selected+1<int(layers.size())) Do(Cmd::EditLayer::With({{"action","move"},{"layer_id",id},{"index",selected+1}}));
            if(pinLayer.empty()) {ImGui::SameLine();if(ImGui::Button("Open in new window")) OpenLayerWindow(w,w->project.active_device,id);if(ImGui::IsItemHovered())ImGui::SetTooltip("Edit this layer in a separate window, side by side with other layers");}
            ImGui::TextDisabled("Stable ID: %s",id.c_str());
            {   // Defects / interface shortcuts: edit through the Defects and Interfaces panels.
                static std::string picked;const bool last=selected+1>=int(layers.size());
                if(ImGui::Button("Add defect")){w->pendingDefect=AddDraftDefect(draft,selected,false);w->pendingSheet=false;w->Current().selectedInterface=-1;w->focusPanel=11;picked=draft.layers[selected].defects.back().id;changed=true;++gesture;}
                ImGui::SameLine();ImGui::BeginDisabled(last);
                if(ImGui::Button("Add interface")){w->pendingDefect=AddDraftDefect(draft,selected,true);w->pendingSheet=true;w->Current().selectedInterface=selected;w->focusPanel=12;picked=draft.project.interface_defects.back().id;changed=true;++gesture;}
                ImGui::EndDisabled();if(last&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("The last layer has no next layer to form an interface with.");
                ImGui::SameLine();
                if(ImGui::Button("Remove##defect")&&!picked.empty()) {
                    const auto& ids=draft.layers[selected].defects;auto& sheets=draft.project.interface_defects;
                    const auto bulk=std::find_if(ids.begin(),ids.end(),[&](const auto& d){return d.id==picked;});
                    if(bulk!=ids.end()){RemoveDraftDefect(draft,selected,int(bulk-ids.begin()),false);changed=true;++gesture;picked.clear();}
                    else if(const auto at=std::find_if(sheets.begin(),sheets.end(),[&](const auto& d){return d.id==picked;});at!=sheets.end()){RemoveDraftDefect(draft,selected,int(at-sheets.begin()),true);changed=true;++gesture;picked.clear();}
                }
                const auto& bulk=draft.project.layers[selected].defects;
                for(int i=0;i<int(bulk.size());++i) {
                    const auto& d=bulk[i];const auto& idv=draft.layers[selected].defects[i].id;
                    const char* type=d.amphoteric?"amphoteric":d.charge_type==scaps_core::TrapChargeType::DonorLike?"donor":d.charge_type==scaps_core::TrapChargeType::AcceptorLike?"acceptor":"neutral";
                    char row[160];std::snprintf(row,sizeof(row),"%s | Et %+.3g eV | Nt %.3g cm^-3##d%d",type,d.energy_offset_eV,d.Nt_m3*1e-6,i);
                    if(ImGui::Selectable(row,picked==idv)){picked=idv;w->pendingDefect=i;w->pendingSheet=false;w->Current().selectedInterface=-1;w->focusPanel=11;}
                }
                for(int i=0;i<int(draft.project.interface_defects.size());++i) {
                    const auto& d=draft.project.interface_defects[i];if(d.left_layer_index!=selected)continue;const auto& idv=d.id;
                    char row[160];std::snprintf(row,sizeof(row),"interface | Et %+.3g eV | Nt %.3g cm^-2##i%d",d.energy_offset_eV,d.Nt_m2*1e-4,i);
                    if(ImGui::Selectable(row,picked==idv)){picked=idv;w->pendingDefect=i;w->pendingSheet=true;w->Current().selectedInterface=selected;w->focusPanel=12;}
                }
            }
            static int materialSelection=0;BeginRow("Material selection");ImGui::Combo("##material_selection",&materialSelection,"Both A/B\0Pure A\0Pure B\0");EndRow();
            const char *selection=materialSelection==1?"A":materialSelection==2?"B":"both";
            BeginRow("SCAPS material [.material]");
            const bool materialLoad=ImGui::Button("Load...");w->tutorialOverlay.Item("scapslab.Layers","material-load");if(materialLoad)try {if(const auto path=OpenFileDialog({"SCAPS material","material"}))Do(Cmd::LoadMaterial::With({{"layer_id",id},{"selection",selection},{"path",path->Utf8()}}));}catch(const std::exception &e){w->status=e.what();}
            ImGui::SameLine();const bool materialSave=ImGui::Button("Save...");w->tutorialOverlay.Item("scapslab.Layers","material-save");if(materialSave)try {if(const auto path=SaveFileDialog({"SCAPS material","material"},"layer.material"))Do(Cmd::SaveMaterial::With({{"layer_id",id},{"selection",selection},{"path",path->Utf8()}}));}catch(const std::exception &e){w->status=e.what();}EndRow();
            char name[256]; std::snprintf(name,sizeof(name),"%s",l.name.c_str());
            BeginRow("Name");if(ImGui::InputText("##name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {l.name=name;changed=true;++gesture;}EndRow();
            changed|=Number("Thickness [µm]",l.thickness_m,"thickness_m",1e6);
            auto &m=l.material;
            changed|=Number("Relative permittivity",m.eps_r,"eps_r"); changed|=Number("Band gap [eV]",m.Eg_eV,"Eg_eV"); changed|=Number("Affinity [eV]",m.chi_eV,"chi_eV");
            changed|=Number("Nc [cm^-3]",m.Nc_ref_m3,"Nc_ref_m3",1e-6); changed|=Number("Nv [cm^-3]",m.Nv_ref_m3,"Nv_ref_m3",1e-6);
            changed|=Number("Electron mobility [cm^2/(V s)]",m.mu_n_m2Vs,"mu_n_m2_V_s",1e4); changed|=Number("Hole mobility [cm^2/(V s)]",m.mu_p_m2Vs,"mu_p_m2_V_s",1e4);
            changed|=Number("Donors [cm^-3]",l.Nd_m3,"Nd_m3",1e-6); changed|=Number("Acceptors [cm^-3]",l.Na_m3,"Na_m3",1e-6);
        } else if(view==2) {
            for(const auto *side:{"left","right"}) {
                ImGui::PushID(side); ImGui::SeparatorText(side); auto &c=std::string_view(side)=="left"?draft.project.left_contact:draft.project.right_contact;
                bool flat=c.flat_band.value_or(false);
                if(Toggle("Flat band (ohmic, automatic phi_m)",flat)) {c.flat_band=flat;changed=true;++gesture;}
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Checked (SCAPS 'flat band'): phi_m follows the adjacent semiconductor, the contact is ohmic, no band bending.\nUnchecked: phi_m is used as entered; a metal on the wrong side of the semiconductor work function makes a depleting (Schottky-like) contact.");
                ImGui::BeginDisabled(flat);changed|=Number("Work function phi_m [eV]",c.phi_m_eV,"phi_m_eV");ImGui::EndDisabled();
                if(flat) ImGui::TextDisabled("phi_m is derived from the adjacent layer (flat band).");
                else {
                    // Documented formula (same non-degenerate model as scaps_core semiconductor_work_function_eV; Nc/Nv at reference T, full ionisation):
                    // ni = sqrt(Nc Nv) exp(-Eg/2kT); n0 = (net+sqrt(net^2+4ni^2))/2 for net=Nd-Na>=0 (else n0=ni^2/p0); phi_s = chi - kT ln(n0/Nc).
                    // n-type barrier = phi_m - phi_s, p-type barrier = phi_s - phi_m; barrier > 0 means depleting.
                    const auto &layer=draft.project.layers[std::string(side)=="left"?0:draft.project.layers.size()-1];const auto &m=layer.material;
                    const double kT=8.617333262e-5*draft.project.temperature_K,Nc=m.Nc_ref_m3,Nv=m.Nv_ref_m3,Eg=m.Eg_eV;
                    const double net=layer.Nd_m3-layer.Na_m3;
                    const double ni=std::sqrt(Nc*Nv)*std::exp(-Eg/(2*kT)),root=std::sqrt(net*net+4*ni*ni),n0=net>=0?(net+root)/2:ni*ni/((-net+root)/2);
                    const double phiS=m.chi_eV-kT*std::log(n0/Nc),barrier=net>=0?c.phi_m_eV-phiS:phiS-c.phi_m_eV;
                    if(barrier>1e-3) ImGui::TextColored(ImVec4(1.f,.7f,.3f,1.f),"semiconductor phi_s = %.3f eV -> barrier %.3f eV (depleting, Schottky-like)",phiS,barrier);
                    else ImGui::TextColored(ImVec4(.5f,.9f,.5f,1.f),"semiconductor phi_s = %.3f eV -> barrier %.3f eV (ohmic, no depletion)",phiS,barrier);
                    if(!c.flat_band.has_value()) ImGui::TextDisabled("flat_band not set: engine default (ohmic only when phi_m is on the ohmic side of phi_s).");
                }
                for(const auto *key:{"Sn_m_s","Sp_m_s"}) {
                    ImGui::PushID(key); auto& velocity=std::string_view(key)=="Sn_m_s"?c.Sn_mps:c.Sp_mps;bool infinite=std::isinf(velocity);
                    if(Toggle(key==std::string("Sn_m_s")?"Infinite Sn [m/s]":"Infinite Sp [m/s]",infinite)) {velocity=infinite?INFINITY:1e5;changed=true;}
                    
                    if(!infinite) changed|=Number("Surface velocity [m/s]",velocity,key);
                    else ImGui::TextDisabled("S is infinite: clear the toggle to edit velocity [m/s]."); ImGui::PopID();
                }
                ImGui::PopID();
            }
            const auto &computed=(w->Current().bandResult.empty()?w->Current().Iv():w->Current().Band())->meta.prim;
            if(computed.contains("contact_phi_eV") && ImGui::CollapsingHeader("Contact view",ImGuiTreeNodeFlags_DefaultOpen)) {
                double total=0;for(const auto &layer:draft.project.layers) total+=layer.thickness_m*1e6;
                const double x[]={0,total},phi[]={computed.at("contact_phi_eV")[0],computed.at("contact_phi_eV")[1]};
                ImGui::Text("Effective phi: left %.3f eV | right %.3f eV",phi[0],phi[1]);
                if(BeginSavedPlot(*w,"Contact work functions",ImVec2(-1,180))) {
                    ImPlot::SetupAxes("Position [um]","phi [eV]");SetupSavedPlot(w->plotSession,"Contact work functions");
                    ImPlot::PlotLine("Contacts",x,phi,2,{ImPlotProp_Marker,ImPlotMarker_Square,ImPlotProp_MarkerSize,6.f});EndSavedPlot(*w,"Contact work functions");
                }
            }
        } else if(view==3) {
            changed|=Choice<scaps_core::ConstantsMode>("Constants",draft.project.constants_mode,"constants_mode",{{"CODATA",scaps_core::ConstantsMode::CODATA},{"SCAPS compatible",scaps_core::ConstantsMode::ScapsCompat}});
            changed|=Choice<scaps_core::MobilityTemperatureModel>("Mobility µ(T)",draft.project.mobility_temperature_model,"mobility_temperature_model",{{"Constant",scaps_core::MobilityTemperatureModel::Constant},{"SqrtT",scaps_core::MobilityTemperatureModel::SqrtT},{"Arora (Caughey-Thomas T)",scaps_core::MobilityTemperatureModel::CaugheyThomasT}});
            ImGui::TextWrapped("Arora uses the sourced caughey_thomas material parameters from the project; missing parameters are reported by the core.");
            auto &mesh=draft.project.mesh;
            changed|=Choice("Mesh level",mesh.adaptivity_level,"adaptivity_level",{{"0","0"},{"1","1"},{"2","2"},{"3","3"},{"A","A"}});
            changed|=Number("Target relative J error",mesh.adaptive_target_relative_J_error,"adaptive_target_relative_J_error");
            changed|=Number("Points per layer",mesh.points_per_layer,"points_per_layer");
            changed|=Number("Newton residual tolerance",draft.project.solver.residual_tolerance,"residual_tolerance");
            changed|=Number("Newton iteration limit",draft.project.solver.max_newton_iterations,"max_newton_iterations");
            ImGui::SeparatorText("Distributed defects");
            const auto distribution=[&](auto& defect,const std::string& label,const std::string& id) {
                if(defect.energy_distribution=="single")return;
                ImGui::PushID(id.c_str());ImGui::TextUnformatted(label.c_str());
                changed|=Number("Distribution levels",defect.energy_levels,"energy_levels");
                changed|=Choice("Quadrature",defect.energy_quadrature,"energy_quadrature",{{"SCAPS trapezoid","scaps_trapezoid"},{"Gauss-Legendre","gauss_legendre"}});ImGui::PopID();
            };
            for(std::size_t l=0;l<draft.project.layers.size();++l){ImGui::PushID(draft.layers[l].id.c_str());for(std::size_t d=0;d<draft.project.layers[l].defects.size();++d)distribution(draft.project.layers[l].defects[d],draft.project.layers[l].name+" / "+draft.project.layers[l].defects[d].label,draft.layers[l].defects[d].id);ImGui::PopID();}
            ImGui::PushID("interfaces");for(auto& defect:draft.project.interface_defects)distribution(defect,"Interface / "+defect.id,defect.id);ImGui::PopID();
            const scaps_core::ProjectDefect defaults;ImGui::TextDisabled("New defects: %d levels / %s",defaults.energy_levels,defaults.energy_quadrature=="scaps_trapezoid"?"SCAPS trapezoid":"Gauss-Legendre");
        } else if(view==4) {
            changed|=RunActions(draft);
        }
        if(changed)Do(Cmd::EditDocument::Make(CreateRef<const ProjectDraft>(std::move(draft)),std::to_string(gesture)));
    }
    ImGui::EndDisabled();
    if(replacing) ImGui::TextWrapped("Project structure is changing; editing resumes when the operation finishes.");
    if(view==4 && w->ActiveJob()) {
        const auto j=w->jobs->GetJob(w->ActiveJob()); if(j) {ImGui::ProgressBar(j->totalWork>0?j->completedWork/j->totalWork:0);ImGui::TextWrapped("%s",j->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Do(Cmd::Cancel::With());
        ImGui::TextWrapped("Stop preserves completed Actions. Preview cancellation is checked between Newton iterations.");
    }
}
namespace {
bool FileField(const char *label,Json &j,const char *key) {
    char text[2048];std::snprintf(text,sizeof(text),"%s",j.value(key,std::string{}).c_str());
    BeginRow(label);const bool changed=ImGui::InputText("##path",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);EndRow();
    if(changed) {j[key]=text;++gesture;}return changed;
}
template<class X,class Y> void PlotJson(Workflow &w,const char *title,const char *xLabel,const char *yLabel,const X &x,const Y &y,bool log=false,float height=190,bool persistent=false) {
    // persistent: x/y live inside a stored result, so their vectors come from the plot cache; otherwise (per-frame temporaries) convert here
    std::vector<double> xl,yl;if(!persistent) {if constexpr(std::is_same_v<X,Json>)xl=x.template get<std::vector<double>>();else xl=x;if constexpr(std::is_same_v<Y,Json>)yl=y.template get<std::vector<double>>();else yl=y;if(log) for(auto &v:yl) if(v<=0) v=NAN;}
    const auto &xs=persistent?Vec(x):xl,&ys=persistent?Vec(y,1.,log?1:0):yl;
    if(BeginSavedPlot(w,title,ImVec2(-1,height))) {ImPlot::SetupAxes(xLabel,yLabel,ImPlotAxisFlags_None,ImPlotAxisFlags_None);if(log) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);SetupSavedPlot(w.plotSession,title);ImPlot::PlotLine(title,xs.data(),ys.data(),int(xs.size()));EndSavedPlot(w,title);}
}
}
bool Panel::OpticalFile(ProjectDraft& draft,std::string& file,scaps_core::OpticalTable& table,const char* extension,const char* label) {
    const auto field=ProjectFieldPath(draft.project,&file);const auto dot=field.rfind('.');const auto key=field.substr(dot+1);
    std::string pointer="/";for(char c:field.substr(0,dot)){if(c=='.' || c=='[')pointer+='/';else if(c!=']')pointer+=c;}pointer+="/scapslab_"+key;
    ImGui::PushID(field.c_str());const auto previous=file;bool changed=false;
    char text[1024];std::snprintf(text,sizeof(text),"%s",file.c_str());BeginRow(label);if(ImGui::InputText("##value",text,sizeof(text),ImGuiInputTextFlags_EnterReturnsTrue)){file=text;changed=true;}EndRow();
    BeginRow("Select file");if(ImGui::Button("Browse..."))try{if(const auto path=OpenFileDialog({label,extension})){file=path->Utf8();changed=true;}}catch(const std::exception& e){w->status=e.what();}
    w->tutorialOverlay.Item(std::string(extension)=="abs"?"scapslab.Absorption":std::string(extension)=="opt"?"scapslab.Defects":"scapslab.Illumination",key);EndRow();
    if(changed)try {
        const std::string kind=extension;
        auto points=kind=="gen"?scaps_core::read_scaps_generation(file):kind=="spe"?scaps_core::read_scaps_spectrum(file):kind=="ftr"?scaps_core::read_scaps_filter(file):scaps_core::read_scaps_absorption(file);
        draft.extensions[pointer]=file;table=std::move(points);file.clear();
    }catch(const std::exception& e){w->status=std::string(label)+": "+e.what();file=previous;changed=false;}
    if(!table.empty()) {
        const auto source=draft.extensions.find(pointer);const auto name=source!=draft.extensions.end() && source->second.is_string()?source->second.get<std::string>():std::string("Embedded table");
        ImGui::TextWrapped("%s | %d points",name.c_str(),int(table.size()));
        std::vector<double> x,y;const std::string kind=extension;for(const auto& [at,value]:table){x.push_back(at*(kind=="gen"?1e9:1.));y.push_back(value*(kind=="gen"?1e-6:1.));}
        PlotJson(*w,label,kind=="gen"?"x [nm]":"Wavelength [nm]",kind=="gen"?"G [cm^-3 s^-1]":kind=="spe"?"Power [W/m^2]":kind=="abs"?"alpha [m^-1]":kind=="ftr"?"Fraction [1]":"Cross section [m^2]",x,y,false,180);
        if(kind=="gen" && ImGui::BeginPopupContextItem("Export generation")) {
            ImGui::BeginDisabled(w->Busy());for(const auto* format:{"csv","grd"})if(ImGui::MenuItem((std::string("Export .")+format).c_str()))try{if(const auto path=SaveFileDialog({"Generation profile",format},(std::string("generation.")+format).c_str()))Do(Cmd::ExportProfile::With({{"parameter_id","generation_m3_s"},{"path",path->Utf8()}}));}catch(const std::exception& e){w->status=e.what();}ImGui::EndDisabled();ImGui::EndPopup();
        }
    }
    ImGui::PopID();return changed;
}
bool Panel::Illumination(ProjectDraft &draft) {
    bool changed=false;auto &light=draft.project.illumination;
    bool enabled=light.enabled;if(Toggle("Light enabled",enabled)) {light.enabled=enabled;changed=true;}
    const char *galleryTab=gallery?std::getenv("SCAPSLAB_OPTICAL_TAB"):nullptr;
    const int savedTab=w->plotSession.settings.opticalTab;const bool restoreTab=opticalTab!=savedTab;
    int tutorialTab=-1;if(const auto *step=w->tutorial.Step();step && step->at("target").value("window",std::string{})=="scapslab.Illumination"){const auto item=step->at("target").value("item",std::string{});tutorialTab=item=="light-iv"?1:item=="qe"?2:-1;} // the coach must point at a visible button
    auto tabFlags=[&](int tab){return (galleryTab && std::atoi(galleryTab)==tab) || (restoreTab && savedTab==tab) || tutorialTab==tab?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;};
    auto selectTab=[&](int tab){opticalTab=tab;w->plotSession.settings.opticalTab=tab;};
    if(ImGui::BeginTabBar("Optical views")) {
        if(ImGui::BeginTabItem("Spectrum / G(x)",nullptr,tabFlags(0))) {selectTab(0);
            const bool wide=ImGui::GetContentRegionAvail().x>750;
            if(wide) {ImGui::BeginTable("Spectrum and generation",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            int source=light.mode=="generation_file"?3:light.mode=="uniform"?4:light.mode=="monochromatic"?2:(draft.extensions.contains("/illumination/scapslab_spectrum")?draft.extensions.at("/illumination/scapslab_spectrum").get<std::string>():std::string{})=="AM1.5G"?0:1;
            BeginRow("Spectrum");const bool sourceChanged=ImGui::Combo("##source",&source,"AM1.5G\0Spectrum from file\0Monochromatic\0Generation profile from file\0Uniform generation\0");w->tutorialOverlay.Item("scapslab.Illumination","source");if(sourceChanged) {
                std::string spectrumPath;bool apply=true;
                if(source==1 || source==3) {try {const auto path=OpenFileDialog({source==3?"SCAPS generation":"SCAPS spectrum",source==3?"gen":"spe"});if(path)spectrumPath=path->Utf8();else apply=false;}catch(const std::exception &e){w->status=e.what();apply=false;}}
                if(apply) {light.mode=source==3?"generation_file":source==4?"uniform":source==2?"monochromatic":"spectrum";draft.extensions["/illumination/scapslab_spectrum"]=source==0?"AM1.5G":"custom";light.spectrum_file="";light.spectrum_nm_W_m2=source==0?AM15SpectrumValues():scaps_core::OpticalTable{};if(source==1)light.spectrum_file=spectrumPath;if(source==3){light.generation_file=spectrumPath;light.generation_m_m3s.clear();}changed=true;++gesture;}
            }EndRow();
            if(source==1) {if(OpticalFile(draft,light.spectrum_file,light.spectrum_nm_W_m2,"spe","Spectrum file [.spe]")) {light.mode="spectrum";draft.extensions["/illumination/scapslab_spectrum"]="custom";changed=true;}}
            if(source==3)changed|=OpticalFile(draft,light.generation_file,light.generation_m_m3s,"gen","Generation profile [.gen]");
            if(source==4)changed|=Number("Uniform G [m^-3 s^-1]",light.uniform_generation_m3s,"uniform_generation_m3_s");
            if(source==2) {changed|=Number("Wavelength [nm]",light.wavelength_nm,"wavelength_nm");changed|=Number("Power [W/m^2]",light.monochromatic_power_W_m2,"monochromatic_power_W_m2");}
            changed|=Number("Intensity multiplier",light.intensity_multiplier,"intensity_multiplier");
            bool right=light.from_right;if(Toggle("Illuminate from right",right)) {light.from_right=right;changed=true;}
            changed|=Number("Front transmission",light.front_transmission,"front_transmission");changed|=Number("Back reflection",light.back_reflection,"back_reflection");changed|=Number("Internal reflection",light.front_internal_reflection,"front_internal_reflection");
            changed|=OpticalFile(draft,light.front_filter_file,light.front_filter_nm_fraction,"ftr","Front transmission [.ftr]");changed|=OpticalFile(draft,light.back_filter_file,light.back_filter_nm_fraction,"ftr","Back reflection [.ftr]");
            if(wide) ImGui::TableNextColumn();
            const auto &r=*(w->Current().Band()->optics?w->Current().Band():w->Current().Iv());
            if(r.optics) {const auto &o=*r.optics;ImGui::Text("Incident power %.6g W/m^2",o.incident_power_W_m2);PlotJson(*w,"G(x)","x [m]","G [m^-3 s^-1]",o.x_m.get(),o.generation_m3_s.get(),false,330,true);}
            else ImGui::TextWrapped("Generation preview updates after automatic band calculation.");
            if(wide) ImGui::EndTable();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Light I-V",nullptr,tabFlags(1))) {selectTab(1);
            if(ImGui::Button("Run settings"))w->focusView=4;ImGui::SameLine();ShotBar(*w,"iv");ImGui::TextDisabled("Illuminated J-V curve with Voc, Jsc and MPP markers");
            const auto &r=*w->Current().Iv();
            const auto &live=w->Current();const bool estimating=live.liveRunning && live.pvEstimate.valid;
            if(estimating) { // photovoltaic metrics read off the sweep so far: tagged, replaced by the exact refinement of the same job
                ImGui::TextColored(ImVec4(1.f,.6f,.15f,1.f),"PV metrics: ESTIMATE from the solved sweep (exact refinement running)");
                if(ImGui::BeginTable("PV estimate",2,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    const auto &e=live.pvEstimate;
                    for(const auto &[label,value,format]:{std::tuple{"Voc [V] (estimate)",e.Voc_V,"%.4f"},std::tuple{"Jsc [mA/cm^2] (estimate)",std::abs(e.Jsc_A_m2)*.1,"%.4g"},std::tuple{"FF [%] (estimate)",e.FF*100.,"%.4g"},std::tuple{"Efficiency [%] (estimate)",e.eta*100.,"%.4g"}}) {
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("%s",label);ImGui::TableNextColumn();
                        if(std::isfinite(value))ImGui::TextDisabled(format,value);else ImGui::TextDisabled("unavailable");
                    }ImGui::EndTable();
                }
            } else if(live.Stale() && bool(r.photovoltaic)) ImGui::TextDisabled("PV metrics below are outdated (model edited); recalculating replaces them.");
            if(!estimating && bool(r.photovoltaic)) {const auto &pv=*r.photovoltaic;ImGui::TextWrapped("PV: %s",scaps_core::to_string(pv.status));if(!pv.message.empty()) ImGui::TextWrapped("%s",pv.message.c_str());
                if(ImGui::BeginTable("PV metrics",2,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    for(const auto &[key,label,scale]:{std::tuple{"Voc_V","Voc [V]",1.},std::tuple{"Jsc_A_m2","Jsc [mA/cm^2]",.1},std::tuple{"FF_fraction","FF [%]",100.},std::tuple{"efficiency_fraction","Efficiency [%]",100.}}) {
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();
                        const double value=std::string_view(key)=="Voc_V"?-pv.Voc_V:std::string_view(key)=="Jsc_A_m2"?-pv.Jsc_Am2:std::string_view(key)=="FF_fraction"?pv.FF:pv.eta;if(std::isfinite(value))ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",value*scale);else ImGui::TextUnformatted("unavailable");
                    }ImGui::EndTable();
                }
            }
            if(!r.points.empty() || live.liveRunning) {
                const bool reset=ImGui::Button("Reset PV view");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Default range: -1.2 Jsc to +Jsc. Scroll to zoom; drag to pan.");
                const auto& cols=IvCols(r.points,false,.1);const auto& x=cols.a;const auto& y=cols.b;
                const bool valid=r.photovoltaic && r.photovoltaic->status==scaps_core::PhotovoltaicStatus::Converged;
                const double jsc=valid && std::isfinite(r.photovoltaic->Jsc_Am2)?std::abs(r.photovoltaic->Jsc_Am2)*.1:0.;
                IvStatus(w->Current());
    if(BeginSavedPlot(*w,"Light I-V",ImVec2(-1,std::max(280.f,ImGui::GetContentRegionAvail().y-30)))) {
                    ImPlot::SetupAxes("Voltage [V]","J [mA/cm^2]");ImPlot::SetupLegend(ImPlotLocation_NorthWest,ImPlotLegendFlags_Horizontal);
                    if(reset){auto &v=w->plotSession.settings.plots["Light I-V"];v.manual=false;v.ranges={};}
                    if(x.size()>1 && x.front()!=x.back())FitSavedAxis(w->plotSession,"Light I-V",ImAxis_X1,*std::min_element(x.begin(),x.end()),*std::max_element(x.begin(),x.end()));
                    if(jsc>0)FitSavedAxis(w->plotSession,"Light I-V",ImAxis_Y1,-1.2*jsc,jsc);
                    SetupSavedPlot(w->plotSession,"Light I-V");
                    HistoryLines(*w,w->Current(),"iv","voltage_V","J_A_m2",false,.1);
                    if(!IvIsReference(*w,live)){StaleDim dim(live);ImPlot::PlotLine("Light I-V",x.data(),y.data(),int(x.size()),{ImPlotProp_LineWeight,2.5f});}
                    IvReference(*w,live,false,.1);PreviewOverlay(live,false,.1);
                    if(estimating) { // markers of the estimate: hollow-style, labelled as such
                        const auto &e=live.pvEstimate;const double zero=0.,sc=-std::abs(e.Jsc_A_m2)*.1,mj=e.Jmpp_A_m2*.1;
                        if(std::isfinite(e.Voc_V))ImPlot::PlotScatter("Voc (estimate)",&e.Voc_V,&zero,1);
                        if(std::isfinite(sc))ImPlot::PlotScatter("Jsc (estimate)",&zero,&sc,1);
                        if(std::isfinite(e.Vmpp_V) && std::isfinite(mj))ImPlot::PlotScatter("MPP (estimate)",&e.Vmpp_V,&mj,1);
                    }
                    if(valid && !estimating) {
                        const double zero=0.,shortCircuit=-jsc;
                        if(std::isfinite(r.photovoltaic->Voc_V)) {const double voc=-r.photovoltaic->Voc_V;ImPlot::PlotScatter("Voc",&voc,&zero,1);}
                        if(jsc>0)ImPlot::PlotScatter("Jsc",&zero,&shortCircuit,1);
                        if(std::isfinite(r.photovoltaic->Vmpp_V) && std::isfinite(r.photovoltaic->Jmpp_Am2)) {const double v=-r.photovoltaic->Vmpp_V,j=r.photovoltaic->Jmpp_Am2*.1;ImPlot::PlotScatter("MPP",&v,&j,1);}
                    }EndSavedPlot(*w,"Light I-V");
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("QE(lambda)",nullptr,tabFlags(2))) {selectTab(2);
            const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("QE controls and plot",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            ImGui::TextWrapped("QE range and probe settings are in Run. Dark differential EQE uses the Action temperature at 0 V.");
            if(ImGui::Button("Run settings"))w->focusView=4;
            if(wide) ImGui::TableNextColumn();
            if(const auto result=w->Current().acResults.get("qe")) {const auto& q=result->meta.prim;const auto& points=result->points;
                ImGui::TextWrapped("Effective: %s | T %.6g K | 0 V | %d samples",q.at("probe_mode").get<std::string>().c_str(),q.at("temperature_K").get<double>(),int(points.size()));
                ImGui::TextWrapped("Integrated Jsc: %s A/m^2 | Light Jsc: %s A/m^2",q.at("integrated_Jsc_A_m2").dump().c_str(),q.at("light_Jsc_A_m2").dump().c_str());ImGui::TextWrapped("%s",q.at("integration_note").get<std::string>().c_str());
                if(BeginSavedPlot(*w,"QE history",ImVec2(-1,300))) {ImPlot::SetupAxes("Wavelength [nm]","EQE [fraction]");SetupSavedPlot(w->plotSession,"QE history");HistoryLines(*w,w->Current(),"qe","wavelength_nm","EQE");const auto& data=Derived(points,620,[&](Cols& c){for(const auto& p:points){c.a.push_back(p.wavelength_nm);c.b.push_back(p.EQE);}});if(const auto it=w->Current().changeAc.find("qe");it!=w->Current().changeAc.end()){const auto& before=Derived(it->second->points,620,[&](Cols& c){for(const auto& p:it->second->points){c.a.push_back(p.wavelength_nm);c.b.push_back(p.valid?p.EQE:NAN);}});ReferenceLine(*w,"EQE",before.a,before.b,ImPlot::GetColormapColor(0));}if(w->liveChangeReference==3 || !w->Current().changeAc.contains("qe") || w->Current().changeAc.at("qe")!=result)ImPlot::PlotLine("EQE",data.a.data(),data.b.data(),int(data.a.size()),{ImPlotProp_LineWeight,2.5f});EndSavedPlot(*w,"QE history");}
                if(ImGui::BeginTable("QE sample voltages",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,120))) {
                    for(const auto *label:{"Wavelength [nm]","Terminal [V]","Internal [V]"}) ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
                    for(const auto &p:points) {ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%.6g",p.wavelength_nm);ImGui::TableNextColumn();ImGui::Text("%.6g",p.terminal_voltage_V);ImGui::TableNextColumn();if(std::isfinite(p.internal_voltage_V))ImGui::Text("%.9g",p.internal_voltage_V);else ImGui::TextUnformatted("unavailable");}
                    ImGui::EndTable();
                }
            }if(wide) ImGui::EndTable();ImGui::EndTabItem();
        }ImGui::EndTabBar();
    }
    ImGui::TextWrapped("%s",w->status.c_str());return changed;
}
bool Panel::Absorption(ProjectDraft &draft) {
    auto &selected=w->Current().selectedLayer;auto &layers=draft.project.layers;selected=std::clamp(selected,0,int(layers.size())-1);
    BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].name.c_str())) {for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i);if(ImGui::Selectable(layers[i].name.c_str(),i==selected)) selected=i;ImGui::PopID();}ImGui::EndCombo();}EndRow();
    const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("Absorption editor and plot",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
    auto &a=layers[selected].absorption;const auto before=a;bool changed=Choice("Absorption model",a.model,"model",{{"None","none"},{"Square root","sqrt"},{"SCAPS model","scaps"},{"File","file"}});w->tutorialOverlay.Item("scapslab.Absorption","model");
    if(changed && a.model=="file" && a.file.empty()) {try {const auto path=OpenFileDialog({"SCAPS absorption","abs"});if(path)a.file=path->Utf8();else {a=before;changed=false;}}catch(const std::exception &e){w->status=e.what();a=before;changed=false;}}
    if(a.model=="file") changed|=OpticalFile(draft,a.file,a.table_nm_m1,"abs","Absorption file [.abs]");
    if(a.model=="sqrt") {changed|=Number("A [m^-1 eV^-1/2]",a.A_m1_eV_half,"A_m1_eV_half");changed|=Number("B [m^-1 eV^1/2]",a.B_m1_eV_minus_half,"B_m1_eV_minus_half");}
    if(a.model=="scaps") {changed|=Number("Background [m^-1]",a.background_m1,"background_m1");changed|=Number("Step [m^-1]",a.step_m1,"step_m1");changed|=Number("Sqrt alpha [m^-1]",a.sqrt_alpha_m1,"sqrt_alpha_m1");changed|=Number("Sqrt beta [m^-1]",a.sqrt_beta_m1,"sqrt_beta_m1");}
    bool mixing=bool(a.composition);
    if(Toggle("Composition absorption A/B",mixing)) {
        if(!mixing){a.composition.reset();changed=true;}
        else try {
            const auto fileA=OpenFileDialog({"Pure A absorption","abs"});
            if(fileA)if(const auto fileB=OpenFileDialog({"Pure B absorption","abs"})) {
                const double gap=layers[selected].material.Eg_eV;
                a.composition.emplace();auto& mixed=*a.composition;mixed.table_a_nm_m1=scaps_core::read_scaps_absorption(fileA->Utf8());mixed.table_b_nm_m1=scaps_core::read_scaps_absorption(fileB->Utf8());mixed.Eg_a_eV=mixed.Eg_b_eV=gap;mixed.uv_a_nm=mixed.uv_b_nm=200.;
                changed=true;
            }
        }catch(const std::exception &e){w->status=e.what();}
    }
    if(bool(a.composition)) {
        auto &c=*a.composition;
        changed|=OpticalFile(draft,c.file_a,c.table_a_nm_m1,"abs","Pure A absorption [.abs]");
        changed|=OpticalFile(draft,c.file_b,c.table_b_nm_m1,"abs","Pure B absorption [.abs]");
        changed|=Number("Pure A gap [eV]",c.Eg_a_eV,"Eg_a_eV");changed|=Number("Pure B gap [eV]",c.Eg_b_eV,"Eg_b_eV");
        changed|=Number("Pure A UV limit [nm]",c.uv_a_nm,"uv_a_nm");changed|=Number("Pure B UV limit [nm]",c.uv_b_nm,"uv_b_nm");
    }
    bool tail=a.urbach_enabled;if(Toggle("Urbach tail",tail)) {a.urbach_enabled=tail;changed=true;}if(tail) changed|=Number("Urbach E_U [eV]",a.urbach_energy_eV,"urbach_energy_eV");
    ImGui::TextWrapped("Eg-joint Urbach extension. A pure square-root model has alpha(Eg)=0 and therefore no tail; use a nonzero step/background or a file.");
    if(wide) ImGui::TableNextColumn();
    const auto &r=*(w->Current().Band()->optics?w->Current().Band():w->Current().Iv());
    if(r.optics) for(const auto &curve:r.optics->absorption) if(curve.layer_id==draft.layers[selected].id) {
        const auto &x=Vec(curve.wavelength_nm.get()),&y=Vec(curve.alpha_m1.get(),1.,1),&base=Vec(curve.without_tail_m1.get(),1.,1);
        if(BeginSavedPlot(*w,"Absorption preview",ImVec2(-1,300))) {ImPlot::SetupAxes("Wavelength [nm]","alpha [m^-1]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);SetupSavedPlot(w->plotSession,"Absorption preview");ImPlot::PlotLine("With tail",x.data(),y.data(),int(x.size()));ImPlot::PlotLine("Without tail",x.data(),base.data(),int(x.size()));EndSavedPlot(*w,"Absorption preview");}
    }if(wide) ImGui::EndTable();return changed;
}
bool Panel::ProfileEditor(scaps_core::Profile& g,double thickness,const std::string& parameter,const scaps_core::Profile* composition,bool diffusion) {
    using namespace scaps_core;
    ImGui::PushID(parameter.c_str());bool changed=false;const bool density=parameter.find("_m3")!=std::string::npos,mobility=parameter.find("_m2_V_s")!=std::string::npos;
    const double scale=density?1e-6:mobility?1e4:1.;const std::string unit=density?"cm^-3":mobility?"cm^2/(V s)":parameter=="composition" || parameter=="eps_r"?"1":parameter.find("vth_")==0?"m/s":"eV";
    const auto old=g;auto law=std::string(grading_law_name(g.law));
    if(Choice("Profile law",law,"law",{{"Uniform","uniform"},{"Linear","linear"},{"Parabolic","parabolic"},{"Two parabolas","parabolic2"},{"Logarithmic","logarithmic"},{"Exponential","exponential"},{"Beta","beta"},{"Power","power"},{"Effective medium","effective_medium"},{"Gaussian","gaussian"},{"From file","table"}})){g.law=grading_law_from_string(law);changed=true;}
    w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","law");
    if(diffusion){BeginRow("Diffusion law");const char* owner=parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading";if(ImGui::Button("Gauss")){g.law=GradingLaw::GaussDiffusion;changed=true;}w->tutorialOverlay.Item(owner,"diffusion-gauss");ImGui::SameLine();if(ImGui::Button("erfc")){g.law=GradingLaw::ErfcDiffusion;changed=true;}w->tutorialOverlay.Item(owner,"diffusion-erfc");EndRow();}
    auto load=[&] {
        try{if(const auto file=OpenFileDialog({"SCAPS grading","grd"})) {
            auto loaded=ReadProfileFile(file->Utf8(),density);g=std::move(loaded.profile);
            if(!w->path.empty()){std::error_code ec;const auto relative=std::filesystem::relative(file->Native(),std::filesystem::path(w->path).parent_path(),ec);if(!ec)g.source_file=relative.generic_string();}
            if(numericDraft){ResetProfileFields(*numericDraft,g,{"interpolation_logarithmic","position_relative","shared_range","composition_dependent"});const auto field=ProjectFieldPath(numericDraft->project,&g);std::string pointer="/";for(char c:field){if(c=='.' || c=='[')pointer+='/';else if(c!=']')pointer+=c;}numericDraft->extensions[pointer+"/scapslab_warnings"]=loaded.warnings;}
            changed=true;
        }else if(old.law!=GradingLaw::Table){g=old;changed=false;}}catch(const std::exception& e){w->status=e.what();g=old;changed=false;}
    };
    if(changed && g.law==GradingLaw::Table && g.coordinate.empty())load();
    if(g.law==GradingLaw::Table) {
        BeginRow("Grading file [.grd]");if(ImGui::Button("Browse..."))load();w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","load-profile");EndRow();
        ImGui::TextWrapped("%s | %d points",g.source_file.empty()?"Embedded table":g.source_file.c_str(),int(g.coordinate.size()));changed|=Toggle("Logarithmic interpolation",numericDraft?ProfileParameter(*numericDraft,g,"interpolation_logarithmic",false):g.interpolation_logarithmic);
    }else {
        auto number=[&](const char* key,const char* label,double fallback,double factor=1.){const std::string field=key;const std::string valueKey=field=="background" || field=="bowing" || (field=="left" && g.law==GradingLaw::Gaussian)?"grading."+parameter+"."+(field=="left"?"amplitude":field):field=="left" || field=="right" || field=="middle"?parameter:field=="diffusion_depth_m" || field=="projected_range_m" || field=="dose_m2" || field=="surface_density_m3"?field:"grading."+field;ImGui::PushID(key);changed|=Number(label,ProfileParameter(*numericDraft,g,key,fallback),valueKey.c_str(),factor);ImGui::PopID();};
        number("left",("Left / amplitude ["+unit+"]").c_str(),0.,scale);number("right",("Right ["+unit+"]").c_str(),0.,scale);
        const auto law=g.law;
        if(law==GradingLaw::Parabolic)number("bowing",("Bowing ["+unit+"]").c_str(),0.,scale);
        if(law==GradingLaw::Parabolic2){number("midpoint","Middle coordinate [x/d or y]",.5);number("middle",("Middle value ["+unit+"]").c_str(),g.left,scale);}
        if(law==GradingLaw::Exponential){number("background",("Background ["+unit+"]").c_str(),0.,scale);number("length_left","Left length [x/d or y]",1.);number("length_right","Right length [x/d or y]",1.);}
        if(law==GradingLaw::Beta){number("beta_a","Beta a [1]",1.);number("beta_b","Beta b [1]",1.);}
        if(law==GradingLaw::Power)number("power","Power [1]",1.);
        if(law==GradingLaw::Gaussian){number("background",("Background ["+unit+"]").c_str(),0.,scale);number("gaussian_center","Centre [x/d or y]",.5);number("gaussian_width","Sigma [x/d or y]",.1);}
        if(law==GradingLaw::GaussDiffusion || law==GradingLaw::ErfcDiffusion){number("diffusion_depth_m","Signed diffusion depth [um]",-1e-6,1e6);if(law==GradingLaw::GaussDiffusion){number("projected_range_m","Projected range [um]",0.,1e6);number("dose_m2","Dose [cm^-2]",1e14,1e-4);}else number("surface_density_m3","Surface density [cm^-3]",1e20,1e-6);}
    }
    if(parameter=="Nd_m3" || parameter=="Na_m3" || parameter=="Nt_m3")changed|=Toggle("Depends on composition y",numericDraft?ProfileParameter(*numericDraft,g,"composition_dependent",false):g.composition_dependent);
    try {
        const auto& draft=numericDraft?*numericDraft:*w->Current().draft;const auto& layerId=draft.layers[w->Current().selectedLayer].id;
        auto display=ResolveSharedProfile(g,draft,layerId,parameter);if(parameter!="composition" && parameter!="Nd_m3" && parameter!="Na_m3" && parameter!="Nt_m3"){display.composition_dependent=true;ImGui::TextDisabled("Material P(y): x is mapped through composition y(x).");}
        auto c=composition?std::optional<Profile>(ResolveSharedProfile(*composition,draft,layerId,"composition")):std::nullopt;auto samples=ProfileSamples(display,thickness,c?&*c:nullptr);
        const auto field=ProjectFieldPath(draft.project,&g);std::string pointer="/";for(char ch:field){if(ch=='.' || ch=='[')pointer+='/';else if(ch!=']')pointer+=ch;}
        if(const auto it=draft.extensions.find(pointer+"/scapslab_warnings");it!=draft.extensions.end())for(const auto& warning:it->second)ImGui::TextWrapped("File warning: %s",warning.get<std::string>().c_str());
        auto x=std::move(samples.x_m),y=std::move(samples.value_SI);for(auto& v:x)v*=1e9;for(auto& v:y)v*=scale;
        PlotJson(*w,"Profile preview","Local x [nm]",("Value ["+unit+"]").c_str(),x,y,density,220);w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","export-profile");
        if(ImGui::BeginPopupContextItem("Export profile")){ImGui::BeginDisabled(w->Busy());for(const auto* format:{"csv","grd"})if(ImGui::MenuItem((std::string("Export .")+format).c_str()))try{if(const auto path=SaveFileDialog({"Profile",format},(std::string("profile.")+format).c_str())){Json request={{"operation","export_profile"},{"layer_id",layerId},{"parameter_id",parameter},{"path",path->Utf8()}};if(parameter=="Nt_m3")request["defect_id"]=draft.layers[w->Current().selectedLayer].defects[selectedDefect].id;Do(Cmd::ExportProfile::FromJson(request));}}catch(const std::exception& e){w->status=e.what();}ImGui::EndDisabled();ImGui::EndPopup();}
        ImGui::TextDisabled("Right-click preview to export CSV / .grd (SI values).");
    }catch(const std::exception& e){ImGui::TextWrapped("Profile error: %s",e.what());}
    ImGui::PopID();return changed;
}
bool Panel::Grading(ProjectDraft& draft) {
    bool changed=false;auto& layers=draft.project.layers;auto& selected=w->Current().selectedLayer;selected=std::clamp(selected,0,int(layers.size())-1);
    BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].name.c_str())){for(int i=0;i<int(layers.size());++i){ImGui::PushID(i);if(ImGui::Selectable(layers[i].name.c_str(),i==selected))selected=i;ImGui::PopID();}ImGui::EndCombo();}EndRow();
    auto& l=layers[selected];BeginRow("Parameter (SI / eV)");const bool parameterOpen=ImGui::BeginCombo("##parameter",gradingParameter.c_str());w->tutorialOverlay.Item("scapslab.Grading","parameter");if(parameterOpen){for(const auto* key:{"composition","Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"})if(ImGui::Selectable(key,gradingParameter==key))gradingParameter=key;ImGui::EndCombo();}EndRow();
    bool enabled=l.grading && GradingProfile(*l.grading,gradingParameter).has_value();
    if(Toggle("Grade parameter",enabled)) {
        if(enabled){if(!l.grading)l.grading.emplace();auto& profile=GradingProfile(*l.grading,gradingParameter);profile.emplace();profile->law=scaps_core::GradingLaw::Linear;const auto path="layers["+std::to_string(selected)+"]"+(gradingParameter=="Nd_m3" || gradingParameter=="Na_m3"?".doping.":".material.")+gradingParameter;const double value=gradingParameter=="composition"?.5:ProjectParameter(draft.project,path);ResetProfileFields(draft,*profile);ProfileParameter(draft,*profile,"left",value);ProfileParameter(draft,*profile,"right",value);}
        else {GradingProfile(*l.grading,gradingParameter).reset();bool any=false;for(const auto* key:{"composition","Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"})any|=GradingProfile(*l.grading,key).has_value();if(!any)l.grading.reset();}
        changed=true;
    }
    if(enabled){const auto& composition=l.grading->composition;changed|=ProfileEditor(*GradingProfile(*l.grading,gradingParameter),l.thickness_m,gradingParameter,composition?&*composition:nullptr,gradingParameter=="Nd_m3" || gradingParameter=="Na_m3");
        if(gradingParameter=="composition") {ImGui::TextUnformatted("Derived material parameters (left / right, SI / eV)");const auto prefix="layers["+std::to_string(selected)+"].material.";for(const auto& [path,value]:ProjectNumbers(draft.project))if(path.starts_with(prefix) && path.find('.',prefix.size())==path.npos){const auto key=path.substr(prefix.size());try{double left=value,right=value;const auto& profile=GradingProfile(*l.grading,key);if(profile){auto p=*profile;p.composition_dependent=true;const auto samples=ProfileSamples(p,l.thickness_m,&*composition);left=samples.value_SI.front();right=samples.value_SI.back();}BeginRow(key.c_str());ImGui::Text("%.6g / %.6g",left,right);EndRow();}catch(const std::exception& e){ImGui::TextWrapped("%s",e.what());}}}
    }
    changed|=Structure(draft);return changed;
}
namespace {
void ChangeSheetDistributionTyped(scaps_core::ProjectInterfaceDefect& d,const std::string& shape,const std::string& convention) {
    const auto width=[&](const std::string& kind){return d.characteristic_energy_eV*(kind=="gaussian"?std::sqrt(std::numbers::pi)*std::erf(d.gaussian_width*.5):1);};
    const double total=d.energy_distribution=="single" || d.density_convention=="integrated_sheet"?d.Nt_m2:d.peak_density_m2_eV*width(d.energy_distribution);
    d.energy_distribution=shape;d.density_convention=convention;const bool integrated=shape=="single" || convention=="integrated_sheet";d.Nt_m2=integrated?total:0.;d.peak_density_m2_eV=integrated?0.:total/width(shape);
}
}
bool Panel::Defects(ProjectDraft &draft,bool sheet) {
    bool changed=false;auto &layers=draft.project.layers;
    if(sheet && layers.size()<2) {ImGui::TextWrapped("Add a second layer to create a heterointerface.");return false;}
    auto &selected=sheet?w->Current().selectedInterface:w->Current().selectedLayer;
    selected=std::clamp(selected,0,int(layers.size())-(sheet?2:1));
    BeginRow(sheet?"Interface":"Layer");
    const auto name=layers[selected].name+(sheet?" / "+layers[selected+1].name:"");
    if(ImGui::BeginCombo("##selection",name.c_str())) {
        for(int i=0;i<int(layers.size())-(sheet?1:0);++i) {
            ImGui::PushID(i);const auto label=layers[i].name+(sheet?" / "+layers[i+1].name:"");
            if(ImGui::Selectable(label.c_str(),selected==i)) {selected=i;selectedDefect=0;}ImGui::PopID();
        }ImGui::EndCombo();
    }EndRow();
    auto render=[&]<class D>(std::vector<D>& defects) {
    constexpr bool sheet=std::is_same_v<D,scaps_core::ProjectInterfaceDefect>;
    if(w->pendingDefect>=0 && w->pendingSheet==sheet){selectedDefect=w->pendingDefect;w->pendingDefect=-1;}
    if(ImGui::Button("Add defect")){selectedDefect=AddDraftDefect(draft,selected,sheet);changed=true;++gesture;}
    const auto idOf=[&](int i)->const std::string& {if constexpr(sheet)return defects[i].id;else return draft.layers[selected].defects[i].id;};
    std::vector<int> indices;for(int i=0;i<int(defects.size());++i) {if constexpr(sheet){if(defects[i].left_layer_index==selected)indices.push_back(i);}else indices.push_back(i);}
    if(indices.empty()) {ImGui::TextUnformatted("No defects on this object.");return changed;}
    if(std::find(indices.begin(),indices.end(),selectedDefect)==indices.end()) selectedDefect=indices.front();
    BeginRow("Defect ID");
    if(ImGui::BeginCombo("##defect",idOf(selectedDefect).c_str())) {
        for(int i:indices) if(ImGui::Selectable(idOf(i).c_str(),i==selectedDefect)) selectedDefect=i;
        ImGui::EndCombo();
    }EndRow();
    ImGui::Text("%d defects on this %s",int(indices.size()),sheet?"interface":"layer");
    if(ImGui::Button("Remove selected defect")) {RemoveDraftDefect(draft,selected,selectedDefect,sheet);++gesture;return true;}
    auto &d=defects[selectedDefect];
    if constexpr(!sheet) {
        bool amphoteric=d.amphoteric;
        if(Toggle("Amphoteric (+/0/-)",amphoteric)) {d.amphoteric=amphoteric;if(amphoteric)d.energy_distribution="single";changed=true;}
        if(amphoteric) {
            bool correlated=d.correlated;if(Toggle("Correlated transitions",correlated)) {d.correlated=correlated;changed=true;}
            changed|=Number("Second Et offset [eV]",d.second_energy_offset_eV,"second_energy_offset_eV");
            changed|=Number("Second sigma n [cm^2]",d.second_sigma_n_m2,"second_sigma_n_m2",1e4);
            changed|=Number("Second sigma p [cm^2]",d.second_sigma_p_m2,"second_sigma_p_m2",1e4);
        }
        if(amphoteric) ImGui::TextWrapped("Amphoteric model: two discrete transitions.");
        ImGui::BeginDisabled(amphoteric);
        changed|=Choice("Distribution",d.energy_distribution,"energy_distribution",{{"Discrete","single"},{"Uniform","uniform"},{"Gaussian","gaussian"},{"CB tail","cb_tail"},{"VB tail","vb_tail"}});
        ImGui::EndDisabled();
        changed|=Choice<scaps_core::DefectEnergyReference>("Energy reference",d.energy_reference,"energy_reference",{{"Ec - offset",scaps_core::DefectEnergyReference::RelativeToEc},{"Ev + offset",scaps_core::DefectEnergyReference::RelativeToEv},{"Ei + offset",scaps_core::DefectEnergyReference::RelativeToEi}});
        changed|=Number("Integrated Nt [cm^-3]",d.Nt_m3,"Nt_m3",1e-6);
        bool spatial=d.density_profile_m3.has_value();if(Toggle("Spatial Nt(x)",spatial)){if(spatial){d.density_profile_m3.emplace();ResetProfileFields(draft,*d.density_profile_m3);ProfileParameter(draft,*d.density_profile_m3,"left",d.Nt_m3);ProfileParameter(draft,*d.density_profile_m3,"right",d.Nt_m3);}else d.density_profile_m3.reset();changed=true;}
        const bool opticalOpen=ImGui::CollapsingHeader("Optical capture / IPV");w->tutorialOverlay.Item("scapslab.Defects","optical-capture");
        if(opticalOpen) {
            auto &opt=d.optical;bool enabled=opt.enabled;if(Toggle("Optical capture enabled",enabled)){opt.enabled=enabled;changed=true;}
            changed|=OpticalFile(draft,opt.electron_file,opt.electron_table_nm_m2,"opt","Electron capture [.opt]");
            changed|=OpticalFile(draft,opt.hole_file,opt.hole_table_nm_m2,"opt","Hole capture [.opt]");
        }
        if(spatial)changed|=ProfileEditor(*d.density_profile_m3,layers[selected].thickness_m,"Nt_m3",layers[selected].grading && layers[selected].grading->composition?&*layers[selected].grading->composition:nullptr,true);
        changed|=Number("sigma n [cm^2]",d.sigma_n_m2,"sigma_n_m2",1e4);changed|=Number("sigma p [cm^2]",d.sigma_p_m2,"sigma_p_m2",1e4);
        changed|=Choice("Normalization",d.distribution_normalization,"distribution_normalization",{{"SCAPS analytic integral","scaps"},{"In-gap continuum integral","normalized_continuum"}});
    } else {
        auto previous=d;
        const bool shapeChanged=Choice("Distribution",d.energy_distribution,"energy_distribution",{{"Discrete","single"},{"Uniform","uniform"},{"Gaussian","gaussian"}});
        changed|=shapeChanged;
        const char *refs[]={"3: higher Ev + offset","4: lower Ec - offset","5: left Ei + offset","6: right Ei + offset","7: left Ev + offset","8: right Ev + offset","9: overlap midgap + offset"};
        int ref=d.energy_reference-3;BeginRow("Energy reference");
        if(ImGui::Combo("##reference",&ref,refs,7)) {d.energy_reference=ref+3;changed=true;++gesture;}EndRow();
        if(d.energy_distribution=="single") ImGui::TextWrapped("Discrete states use integrated sheet Nt; peak density applies to distributions.");
        ImGui::BeginDisabled(d.energy_distribution=="single");
        const bool conventionChanged=Choice("Sheet density convention",d.density_convention,"density_convention",{{"Integrated sheet","integrated_sheet"},{"SCAPS peak / eV","scaps_peak"}});
        ImGui::EndDisabled();
        if(shapeChanged || conventionChanged) {ChangeSheetDistributionTyped(previous,d.energy_distribution,d.density_convention);d=std::move(previous);changed=true;}
        if(d.energy_distribution=="single" || d.density_convention=="integrated_sheet") {
            changed|=Number("Sheet Nt [cm^-2]",d.Nt_m2,"Nt_m2",1e-4);
        } else {changed|=Number("Peak Dit [cm^-2/eV]",d.peak_density_m2_eV,"peak_density_m2_eV",1e-4);}
        changed|=Number("Left sigma n [cm^2]",d.sigma_n_left_m2,"sigma_n_left_m2",1e4);changed|=Number("Left sigma p [cm^2]",d.sigma_p_left_m2,"sigma_p_left_m2",1e4);
        changed|=Number("Right sigma n [cm^2]",d.sigma_n_right_m2,"sigma_n_right_m2",1e4);changed|=Number("Right sigma p [cm^2]",d.sigma_p_right_m2,"sigma_p_right_m2",1e4);
    }
    bool amphoteric=false;if constexpr(!sheet)amphoteric=d.amphoteric;ImGui::BeginDisabled(amphoteric);
    changed|=Choice<scaps_core::TrapChargeType>("Charge type",d.charge_type,"charge_type",{{"Neutral",scaps_core::TrapChargeType::Neutral},{"Donor (+/0)",scaps_core::TrapChargeType::DonorLike},{"Acceptor (0/-)",scaps_core::TrapChargeType::AcceptorLike}});
    ImGui::EndDisabled();
    changed|=Number("Et offset [eV]",d.energy_offset_eV,"energy_offset_eV");
    if(d.energy_distribution!="single") {
        changed|=Number("Characteristic energy [eV]",d.characteristic_energy_eV,"characteristic_energy_eV");
        if(d.energy_distribution=="gaussian") changed|=Number("Gaussian span [Echar]",d.gaussian_width,"gaussian_width");
        if(d.energy_distribution=="cb_tail" || d.energy_distribution=="vb_tail") changed|=Number("Tail span [Echar]",d.tail_width,"tail_width");
        ImGui::TextWrapped("Gaussian: exp(-((E-Et)/Echar)^2), sigma = Echar/sqrt(2). Uniform width = Echar. Tails decay from the selected reference offset.");
    }
    return changed;
    };
    if(sheet)return render(draft.project.interface_defects);return render(layers[selected].defects);
}
void Panel::Preview(bool sheet) {
    if(w->Current().bandResult.empty() && w->Current().result.empty()) {ImGui::TextWrapped("Preview waiting for auto-recalculate band. The previous model's occupancy is cleared on every edit.");return;}
    const auto &point=PointAtBias(*w->Current().Iv(),*w->Current().Band(),w->Current().selectedBias);
    ImGui::TextWrapped("Occupancy at %.5g V, %.5g K | %s | %s",point.voltage_V,w->Current().Document().at("temperature_K").get<double>(),sheet?"interface sheet":"layer midpoint",point.validity_status.c_str());
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,ImVec2(w->plotSession.settings.margin*.02f,w->plotSession.settings.margin*.02f));
    if(!BeginSavedPlot(*w,"Nt(E) and occupancy",ImVec2(-1,std::max(300.f,ImGui::GetContentRegionAvail().y-8)))) {ImPlot::PopStyleVar();return;}
    ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);
    ImPlot::SetupAxes("E - Ev [eV]",sheet?"Dit [cm^-2/eV]":"Nt [cm^-3/eV]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y2,"Occupation fraction",ImPlotAxisFlags_AuxDefault);FitSavedAxis(w->plotSession,"Nt(E) and occupancy",ImAxis_Y2,0,1);SetupSavedPlot(w->plotSession,"Nt(E) and occupancy");
    if(!point.details){EndSavedPlot(*w,"Nt(E) and occupancy");ImPlot::PopStyleVar();return;}
    bool edgesDrawn=false;
    for(const auto &c:point.details->defect_previews) {
        if(c.sheet!=sheet) continue;
        if(sheet?c.left_layer_index!=w->Current().selectedInterface:c.layer_id!=w->Current().Document().at("layers")[w->Current().selectedLayer].at("scapslab_id").get<std::string>()) continue;
        const auto id=c.id;const auto *xp=&Vec(c.energy_eV),*yp=&Vec(c.density,sheet?1e-4:1e-6);
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);
        if(c.single) {const double e=c.level_energy_eV;ImPlot::PlotInfLines((id+" (discrete Nt)").c_str(),&e,1);if(bool(c.second_level_energy_eV)) {const double e2=*c.second_level_energy_eV;ImPlot::PlotInfLines((id+" second transition").c_str(),&e2,1);}}
        else ImPlot::PlotLine(id.c_str(),xp->data(),yp->data(),int(xp->size()));
        if(!edgesDrawn) {const auto &edges=Vec(c.band_edges_eV);for(int e=0;e<int(edges.size());++e) {const char *labels[]={"EvL","EcL","EvR","EcR"};ImPlot::PlotInfLines(sheet?labels[e]:e==0?"Ev":"Ec",&edges[e],1,ImPlotSpec(ImPlotProp_LineColor,ImVec4(.9f,.85f,.4f,1),ImPlotProp_LineWeight,2));ImPlot::TagX(edges[e],ImVec4(.5f,.45f,.15f,1),"%s",sheet?labels[e]:e==0?"Ev":"Ec");}edgesDrawn=true;}
        if(point.status=="Converged") {
            xp=&Vec(c.occupation_energy_eV);yp=&Vec(c.occupation);
            ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlotSpec occupationStyle(ImPlotProp_LineWeight,2,ImPlotProp_Marker,ImPlotMarker_None);ImPlot::PlotLine((id+(c.amphoteric && c.correlated?" P-":" f")).c_str(),xp->data(),yp->data(),int(xp->size()),occupationStyle);
        }
    }EndSavedPlot(*w,"Nt(E) and occupancy");ImPlot::PopStyleVar();
}
bool Panel::Structure(ProjectDraft &draft) {
    auto &layers=draft.project.layers;double total=0,emin=0,emax=-1e10;
    for(const auto &l:layers) {total+=l.thickness_m;const auto &m=l.material;emin=std::min(emin,-m.chi_eV-m.Eg_eV);emax=std::max(emax,-m.chi_eV);}
    const bool compact=view==22 || view==5;
    if(!compact) ImGui::TextWrapped("Structure to scale | total %.5g um. Click layer/interface; drag a layer's right edge to change thickness.",total*1e6);
    const auto origin=ImGui::GetCursorScreenPos();const float width=std::max(1.f,ImGui::GetContentRegionAvail().x),height=compact?40.f:view==14?std::max(190.f,ImGui::GetContentRegionAvail().y-12):190.f;
    auto *draw=ImGui::GetWindowDrawList();double offset=0;bool changed=false;
    auto y=[&](double e){return origin.y+16+float((emax-e)/(emax-emin))*std::max(78.f,height-120);};
    for(int i=0;i<int(layers.size());++i) {
        auto &l=layers[i];const auto &m=l.material;const float x1=origin.x+float(offset/total)*width;offset+=l.thickness_m;const float x2=origin.x+float(offset/total)*width;
        draw->AddRectFilled(ImVec2(x1,origin.y),ImVec2(x2,origin.y+height),LayerStripColour(l));
        if(w->Current().selectedInterface<0 && w->Current().selectedLayer==i) draw->AddRect(ImVec2(x1+1,origin.y+1),ImVec2(x2-1,origin.y+height-1),IM_COL32(255,190,70,255),0,0,2.f); // selection = border, never a different fill
        const double ec=-m.chi_eV,ev=ec-m.Eg_eV;
        if(!compact) {draw->AddLine(ImVec2(x1,y(ec)),ImVec2(x2,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(ev)),ImVec2(x2,y(ev)),IM_COL32(255,173,110,255),2);
        if(i>0) {const auto &a=layers[i-1].material;const double prevEc=-a.chi_eV;draw->AddLine(ImVec2(x1,y(prevEc)),ImVec2(x1,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(prevEc-a.Eg_eV)),ImVec2(x1,y(ev)),IM_COL32(255,173,110,255),2);}
        }
        draw->PushClipRect(ImVec2(x1+2,origin.y),ImVec2(x2-2,origin.y+height),true);
        draw->AddText(ImVec2(x1+5,origin.y+(compact?4:height-90)),IM_COL32_WHITE,l.name.c_str());
        char thickness[80];std::snprintf(thickness,sizeof(thickness),"%.4g um",l.thickness_m*1e6);
        if(x2-x1>ImGui::CalcTextSize(thickness).x+10) draw->AddText(ImVec2(x1+5,origin.y+(compact?20:height-50)),IM_COL32_WHITE,thickness);
        char parameters[100];std::snprintf(parameters,sizeof(parameters),"chi %.3g eV | Eg %.3g eV",-ec,ec-ev);
        if(!compact && x2-x1>ImGui::CalcTextSize(parameters).x+10) draw->AddText(ImVec2(x1+5,origin.y+height-70),IM_COL32(205,220,235,255),parameters);
        if(!compact && l.grading) {
            const scaps_core::Profile* profile=nullptr;std::string key;
            for(const auto* name:{"Eg_eV","Na_m3","Nc_ref_m3","Nd_m3","Nv_ref_m3","chi_eV","composition","eps_r","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"})if(const auto& g=GradingProfile(*l.grading,name);g){profile=&*g;key=name;break;}
            if(profile){const auto label="Graded: "+key;draw->AddText(ImVec2(x1+5,origin.y+height-35),IM_COL32(140,235,170,255),label.c_str());
                using scaps_core::GradingLaw;const auto law=profile->law;
                if(law==GradingLaw::Linear || law==GradingLaw::Uniform || law==GradingLaw::Parabolic || law==GradingLaw::Logarithmic || law==GradingLaw::Exponential) {
                    std::vector<double> samples;for(int n=0;n<=40;++n)samples.push_back(scaps_core::evaluate_profile(*profile,n/40.));const auto [lo,hi]=std::minmax_element(samples.begin(),samples.end());const double low=*lo,span=std::max(1e-30,*hi-low);
                    for(int n=1;n<=40;++n){const auto at=[&](int k){return ImVec2(x1+(x2-x1)*k/40.f,origin.y+height-8-float((samples[k]-low)/span)*18);};draw->AddLine(at(n-1),at(n),IM_COL32(140,235,170,255));}
                }
            }
        }
        draw->PopClipRect();
        ImGui::PushID(i);ImGui::SetCursorScreenPos(ImVec2(x1,origin.y));ImGui::InvisibleButton("##select-layer",ImVec2(std::max(1.f,x2-x1-7),height));
        if(ImGui::IsItemClicked()) {w->Current().selectedLayer=i;w->Current().selectedInterface=-1;w->focusLayers=true;}
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s) | %.6g um | chi %.4g eV | Eg %.4g eV\nClick: select layer. Drag right edge: thickness (total %.5g um).",l.name.c_str(),LayerType(l)>0?"n":LayerType(l)<0?"p":"i",l.thickness_m*1e6,m.chi_eV,m.Eg_eV,total*1e6);
        ImGui::SetCursorScreenPos(ImVec2(x2-7,origin.y));ImGui::InvisibleButton("##thickness-interface",ImVec2(7,height));TrackGesture();
        if(ImGui::IsItemClicked()) {w->Current().selectedInterface=i+1<int(layers.size())?i:-1;w->Current().selectedLayer=i;w->focusLayers=true;}
        if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {const auto bounds=BoundsTable()->at("thickness_m");const double next=l.thickness_m+ImGui::GetIO().MouseDelta.x*total/width;if(bounds.Contains(next)){l.thickness_m=next;changed|=ImGui::GetIO().MouseDelta.x!=0;}else w->status=bounds.Error("thickness_m",next);}
        if(ImGui::IsItemHovered()) {ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);ImGui::SetTooltip("Drag: thickness [um]. Click: interface after %s",l.name.c_str());}
        if(i+1<int(layers.size())) {
            const auto &right=layers[i+1].material;char offsets[100];
            const double deltaEc=m.chi_eV-right.chi_eV;
            std::snprintf(offsets,sizeof(offsets),"dEc %.3g eV / dEv %.3g eV",deltaEc,deltaEc+m.Eg_eV-right.Eg_eV);
            if(!compact && x2-origin.x>ImGui::CalcTextSize(offsets).x+14) draw->AddText(ImVec2(x2-ImGui::CalcTextSize(offsets).x-14,origin.y+height-24),IM_COL32(230,210,140,255),offsets);
            int count=0;for(const auto &d:draft.project.interface_defects) if(d.left_layer_index==i) ++count;
            draw->AddLine(ImVec2(x2-3,origin.y),ImVec2(x2-3,origin.y+height),IM_COL32(235,196,90,255),2);
            const auto badge=std::to_string(count);draw->AddCircleFilled(ImVec2(x2-13,origin.y+12),9,IM_COL32(120,85,22,255));draw->AddText(ImVec2(x2-13-ImGui::CalcTextSize(badge.c_str()).x*.5f,origin.y+5),IM_COL32_WHITE,badge.c_str());
        }ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x,origin.y+height+6));ImGui::Dummy(ImVec2(width,1));
    return changed;
}
// Layer-edge drag: edit a local preview while the mouse is down, send ONE edit (one undo step, one recalculation) on release.
void Panel::StructureEdit() {
    static Ref<const ProjectDraft> dragDraft;static std::string dragDevice;auto& device=w->Current();
    if(dragDraft && dragDevice!=device.id)dragDraft.reset();if(!device.draft)return;
    const bool held=ImGui::IsMouseDown(ImGuiMouseButton_Left);auto draft=*(dragDraft?dragDraft:device.draft);const bool changed=Structure(draft);
    if(changed && held){dragDraft=CreateRef<const ProjectDraft>(std::move(draft));dragDevice=device.id;return;}
    if(changed){dragDraft.reset();Do(Cmd::EditDocument::Make(CreateRef<const ProjectDraft>(std::move(draft)),std::to_string(gesture)));return;}
    if(dragDraft && !held){auto next=std::move(dragDraft);if(!CompareDrafts(*device.draft,*next).Empty())Do(Cmd::EditDocument::Make(std::move(next),std::to_string(gesture)));}
}
void Panel::Results() {
    const Json *selected=nullptr;
    // A live band/profile job of the current model (changed Eg, Nd, ...) replaces the outdated run's profiles until the exact I-V run lands.
    // Owner 2026-11-23 (no blinking): while dragging or while a newer band job is pending, keep showing the last live band
    // instead of falling back to the outdated exact run for a frame and back again.
    const bool bandPending=w->dragging || w->Current().needsBand || w->Busy();
    const bool liveBand=view==5 && w->Current().selectedRun.empty() && !w->Current().bandResult.empty() && (w->Current().BandFresh() || bandPending);
    const auto bandBadge=[&]{
        const auto &d=w->Current();
        if(liveBand && d.result.empty()==false && d.Stale()) PlotBadge("LIVE band (exact I-V follows)",IM_COL32(255,140,25,230),IM_COL32(0,0,0,255),0);
        else if(view==5 && !liveBand && !d.BandFresh() && (d.Stale() || d.needsBand)) PlotBadge("outdated: model edited",IM_COL32(95,95,105,210),IM_COL32(235,235,240,255),0);
    };
    if(!liveBand) for(const auto &run:w->Current().history) if(run.at("run_id")==w->Current().selectedRun && (run.at("kind")=="iv" || run.at("kind")=="band")) {
        selected=&run;
    }
    const bool bandOnly=view==5 && w->Current().result.empty() && !w->Current().bandResult.empty();
    const auto resultRef=selected?std::static_pointer_cast<const IvResult>(HistoryTyped(w->Current(),*selected)):bandOnly?w->Current().Band():w->Current().Iv();const auto& result=*resultRef;
    const auto profileKind=selected?selected->at("kind").get<std::string>():bandOnly?"band":"iv";
    const auto &layers=(selected?selected->at("document"):w->Current().Document()).at("layers");
    double total=0;for(const auto &l:layers) total+=l.at("thickness_m").get<double>();
    if(view==5) StructureEdit();
    ShotBar(*w,view==5?"band":"iv");
    if(result.points.empty()) {
        // Owner 2026-11-23: the changing curve must be visible while dragging even before the first exact I-V exists.
        const auto& d=w->Current();
        if(view==10 && (!d.previewV.empty() || (d.liveRunning && !d.liveV.empty()))) {
            const bool absoluteLog=w->plotSession.settings.ivAbsoluteLog;
            if(BeginSavedPlot(*w,"I-V",ImVec2(-1,-1))) {
                ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
                if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
                SetupSavedPlot(w->plotSession,"I-V");PreviewOverlay(d,absoluteLog);EndSavedPlot(*w,"I-V");
            }
            return;
        }
        ImGui::TextUnformatted("Calculate I-V to view results.");return;
    }
    const auto &points=result.points;int bias=bandOnly?0:w->Current().selectedBias;bias=std::clamp(bias,0,int(points.size())-1);
    { // one row: Bias point [slider showing V] | Single shot | Shots
        char biasFormat[64];std::snprintf(biasFormat,sizeof(biasFormat),"%.4g V  (#%%d)",points[bias].voltage_V);
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Bias point");ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x*.5f));if(BoundIndex("##bias",bias,"selection.bias_index",int(points.size())-1,biasFormat) && !bandOnly)Do(Cmd::SelectBias::Make(bias));
    }
    ImGui::TextDisabled("%s",selected?"STORED result (frozen snapshot)":"LIVE result (store with Capture live)");
    const auto bandRef=w->Current().ProfileBand();const auto &band=*bandRef;
    // Owner 2026-11-23: changing the bias must not blank/re-layout the panel. While the selected bias' details load
    // (SelectBias requests them), keep drawing the last band point that has details under a fixed-height status line.
    const IvPoint* drawn=&(view==5?PointAtBias(result,band,bias):points[bias]);
    if(view==5) {
        if(drawn->details) ImGui::TextDisabled("Details at %.4g V",points[bias].voltage_V);
        else if(!band.empty() && band.points[0].details) {drawn=&band.points[0];ImGui::TextDisabled("Loading details at %.4g V ...",points[bias].voltage_V);}
        else if(const auto* nearest=NearestStoredProfiles(*w,points,bias)) drawn=nearest;
        else if(MissingProfiles(*w,*drawn)) return;
    }
    const auto &p=*drawn;
    const bool preview=view==5 && !band.empty() && &p==&band.points[0];
    const auto before=w->Current().changeBand?w->Current().changeBand:w->Current().changeIv;
    const bool profileIsReference=!selected && w->liveChangeReference!=3 && before && std::any_of(before->points.begin(),before->points.end(),[&](const auto& point){return &point==&p;});
    const auto &metrics=(preview?band:result).meta.prim;
    w->Current().biasVoltage_V=points[bias].voltage_V; // the selected bias, even while the previous point is still drawn
    {   // one small grey status line
        const double jAbs=std::isfinite(p.J_A_m2)?std::abs(p.J_A_m2):0;char residual[80]="";
        if(jAbs>0 && std::isfinite(p.residual)) std::snprintf(residual,sizeof(residual)," | residual %.2e | dJ/J %.1e",p.residual,p.conservation_error_A_m2/jAbs);
        ImGui::TextDisabled("%.0f ms | %d nodes | terminal %.5g V | internal %.5g V | %s | %d it%s",metrics.value("compute_time_ms",0.0),metrics.value("mesh_node_count",0),p.terminal_voltage_V,p.internal_voltage_V,p.status.c_str(),int(p.iterations),residual);
    }
    if(view==10) {
        absoluteLog=w->plotSession.settings.ivAbsoluteLog;if(Toggle("Log |J|",absoluteLog)){w->plotSession.settings.ivAbsoluteLog=absoluteLog;w->plotSession.settings.plots["I-V"].log[ImAxis_Y1]=absoluteLog;w->projectDirty=true;w->RefreshDirty();}
        const auto &ivc=IvCols(points,absoluteLog);const auto &v=ivc.a,&j=ivc.b;const int failed=ivc.n;
        ImGui::TextWrapped("Curve validity: %s | %d / %d valid points; invalid points masked",result.meta.prim.value("validity_status",std::string("unknown")).c_str(),int(points.size())-failed,int(points.size()));
        if(ImGui::CollapsingHeader("Per-point convergence")) {
            for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %s | %s",pt.bias_id.c_str(),pt.voltage_V,pt.status.c_str(),pt.validity_status.c_str());
        }
        IvStatus(w->Current());
    if(BeginSavedPlot(*w,"I-V",ImVec2(-1,-1))) {
            ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
            if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
            SetupSavedPlot(w->plotSession,"I-V");HistoryLines(*w,w->Current(),"iv","voltage_V","J_A_m2",absoluteLog);if(!Archived(w->Current(),result,"iv") && !IvIsReference(*w,w->Current())){StaleDim dim(w->Current());ImPlot::PlotLine("I-V",v.data(),j.data(),int(v.size()),LiveStyle(w->Current(),2.f));}
            IvReference(*w,w->Current(),absoluteLog);PreviewOverlay(w->Current(),absoluteLog);ImPlot::PlotScatter("Selected bias",&v[bias],&j[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
            double selectedVoltage=v[bias];
            if(ImPlot::DragLineX(1,&selectedVoltage,ImVec4(1,.7f,.3f,1))) {
                bias=int(std::min_element(v.begin(),v.end(),[&](double a,double b){return std::abs(a-selectedVoltage)<std::abs(b-selectedVoltage);})-v.begin());
            }
            if(bias!=w->Current().selectedBias)Do(Cmd::SelectBias::Make(bias));
            EndSavedPlot(*w,"I-V");
        }
        return;
    }
    if(p.status!="Converged" || !std::isfinite(p.J_A_m2)) {ImGui::TextWrapped("This bias did not converge; profiles are masked. Select a converged bias.");return;}
    const auto &x=Vec(p.x_m.get(),1e6);
    if(x.empty()) return;
    bool contactAnnotations=w->plotSession.settings.contactAnnotations.value_or(false);
    if(Toggle("Contact annotations",contactAnnotations)){w->plotSession.settings.contactAnnotations=contactAnnotations;w->projectDirty=true;w->RefreshDirty();}
    // Bands and Carriers share the free height; the splitter fraction is kept in project.ui["band_split"].
    const float splitterH=8.f,availH=std::max(160.f,ImGui::GetContentRegionAvail().y-splitterH-2*ImGui::GetStyle().ItemSpacing.y);
    float split=std::clamp(w->plotSession.settings.bandSplit,.15f,.85f);
    const float h=std::max(60.f,availH*split),h2=std::max(60.f,availH-h);
    const auto &contactDoc=(selected?selected->at("document"):w->Current().Document()).at("contacts");
    if(BeginSavedPlot(*w,"Bands / contacts",ImVec2(-1,h))) {
        ImPlot::SetupAxes("Position [um]","Energy [eV]");ImPlot::SetupLegend(ImPlotLocation_East,ImPlotLegendFlags_Outside);
        const double end=total*1e6,metal=end*.04;
        FitSavedAxis(w->plotSession,"Bands / contacts",ImAxis_X1,-metal,end+metal);
        {double lo=1e300,hi=-1e300;for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) for(double v:Vec(PointColumn(p,key).get())) if(std::isfinite(v)){lo=std::min(lo,v);hi=std::max(hi,v);}
        if(hi>lo){const double m=.05*(hi-lo);FitSavedAxis(w->plotSession,"Bands / contacts",ImAxis_Y1,lo-m,hi+m);}}
        SetupSavedPlot(w->plotSession,"Bands / contacts");
        for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) {const bool dashed=key[1]=='F';HistoryProfiles(*w,key,1.,false,dashed);if(!selected)ProfileReference(*w,key);if(profileIsReference || Archived(w->Current(),result,profileKind))continue;const auto &y=Vec(PointColumn(p,key).get());if(y.size()==x.size()){const auto c=XColour(key);ImPlot::PlotLine(key,x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(c.x,c.y,c.z,dashed?.6f:1.f),ImPlotProp_LineWeight,2.f});if(dashed){ImPlot::PushPlotClipRect();DashedLine(x,y,c);ImPlot::PopPlotClipRect();}}}
        const auto limits=ImPlot::GetPlotLimits();auto *draw=ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for(int side=0;side<2;++side) {
            const double lo=side?end:-metal,hi=side?end+metal:0;
            const auto a=ImPlot::PlotToPixels(lo,limits.Y.Max),b=ImPlot::PlotToPixels(hi,limits.Y.Min);
            draw->AddRectFilled(a,b,IM_COL32(255,176,66,38));
            const double phi=result.meta.prim.at("contact_phi_eV")[side];
            const double chi=layers[side?layers.size()-1:0].at("material").at("chi_eV");
            const auto &ec=Vec(p.Ec_eV.get());
            if(!ec.empty()) {
                const double ef=(side?ec.back():ec.front())+chi-phi;const double xx[]={lo,hi},yy[]={ef,ef};
                const bool flatContact=contactDoc.at(side?"right":"left").value("flat_band",false);
                ImPlot::PlotLine(side?"Right contact E_F###Right contact Fermi level (phi_m)":"Left contact E_F###Left contact Fermi level (phi_m)",xx,yy,2);
                if(contactAnnotations)ImPlot::Annotation((lo+hi)/2,ef,ImVec4(1,.8f,.4f,1),ImVec2(side?-90.f:5.f,-28.f),true,"%s contact phi_m = %.3f eV%s",side?"Right":"Left",phi,flatContact?" (flat band)":"");
            }
        }
        ImPlot::PopPlotClipRect();DrawLayerRegions(layers);bandBadge();EndSavedPlot(*w,"Bands / contacts");
    }
    ImGui::InvisibleButton("##band-split",ImVec2(-1,splitterH));
    if(ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if(ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y!=0) w->plotSession.settings.bandSplit=std::clamp(split+ImGui::GetIO().MouseDelta.y/availH,.15f,.85f);
    if(ImGui::IsItemDeactivated()) {w->projectDirty=true;w->RefreshDirty();}
    {const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const float cy=(a.y+b.y)*.5f;ImGui::GetWindowDrawList()->AddLine({a.x+4,cy},{b.x-4,cy},ImGui::IsItemHovered()||ImGui::IsItemActive()?IM_COL32(255,190,70,255):IM_COL32(110,110,120,200),2.f);}
    if(BeginSavedPlot(*w,"Carriers",ImVec2(-1,h2))) {
        ImPlot::SetupAxes("Position [um]","Density [cm^-3]");ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y1,[](double value,char *buffer,int size,void*) {return std::snprintf(buffer,size,"1e%.0f",std::log10(value));});
        ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);FitSavedAxis(w->plotSession,"Carriers",ImAxis_X1,0,total*1e6);


        SetupSavedPlot(w->plotSession,"Carriers");
        for(const auto *key:{"n_m3","p_m3"}) {HistoryProfiles(*w,key,1e-6);if(!selected)ProfileReference(*w,key,1e-6);if(profileIsReference || Archived(w->Current(),result,profileKind))continue;const auto &y=Vec(PointColumn(p,key).get(),1e-6);if(y.size()==x.size())ImPlot::PlotLine(key==std::string("n_m3")?"n":"p",x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,XColour(key),ImPlotProp_LineWeight,2.f});}
        DrawLayerRegions(layers);bandBadge();EndSavedPlot(*w,"Carriers");
    }
}
void Panel::AC() {
    const bool cv=view<8; const char *kind=cv?"cv":"cf";
    ShotBar(*w,kind);
    ImGui::TextUnformatted("Action ranges and working conditions: Run settings");
    if(ImGui::Button("Run settings"))w->focusView=4;
    ImGui::BeginDisabled(w->Busy());
    ImGui::SameLine(); ImGui::SetNextItemWidth(220); TextInput("CSV path",csvFile,sizeof(csvFile)); ImGui::SameLine();
    if(ImGui::Button("Export CSV")) Do(Cmd::ExportAc::With({{"kind",kind},{"path",csvFile}}));
    ImGui::EndDisabled();
    if(w->Busy()) {
        ImGui::TextDisabled("AC controls disabled while a job is active; wait or Cancel.");
        const auto job=w->jobs->GetJob(w->ActiveJob()); if(job) {ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0); ImGui::TextWrapped("%s",job->currentStage.c_str());}
        if(ImGui::Button("Cancel")) Do(Cmd::Cancel::With());
    }
    Ref<const AcResult> snapshot;
    if(w->Current().acResults.contains(kind)) snapshot=w->Current().acResults.get(kind);
    else for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->acResults.contains(kind)) {snapshot=d->acResults.get(kind);break;}
    if(!snapshot) {ImGui::TextWrapped("Run the sweep to view AC results. Voltage: left minus right.");return;}
    const auto &data=*snapshot;const auto &points=data.points;
    ImGui::TextWrapped("Curve validity: %s (1%% conservation gate)",data.meta.prim.value("validity_status",std::string("unknown")).c_str());
    const auto &first=points.front();
    ImGui::Text("Result: %.6g K | %s = %.6g %s | %d points",first.temperature_K,cv?"f":"V",(cv?first.frequency_Hz:first.voltage_V),cv?"Hz":"V",int(points.size()));
    const int failed=AcCols(points,view).n;
    const char *key=view==7?"inverse_C2_F_minus2":view==9?"G_over_omega_F":"capacitance_F";
    if(failed) ImGui::TextWrapped("%d invalid points (convergence/current conservation); raw CSV rows retain validity flags.",failed);
    if(ImGui::CollapsingHeader("Per-point convergence"))
        for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %.5g Hz | %s | %s",pt.point_id.c_str(),pt.voltage_V,pt.frequency_Hz,pt.status.c_str(),pt.validity_status.c_str());
    const char *ylabel=view==7?"1/C^2 [F^-2]":view==9?"G/omega [F]":"C [F]";
    if(BeginSavedPlot(*w,names[view],ImVec2(-1,-1))) {
        ImPlot::SetupAxes(cv?"Voltage [V]":"Frequency [Hz]",ylabel,ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        if(view==6 || view==8) ImPlot::SetupAxis(ImAxis_Y2,"G/omega [F]",ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y1,view==8?"%.8g":"%.3g");
        if(!cv) ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        SetupSavedPlot(w->plotSession,names[view]);
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->acResults.contains(kind)) {
            if(!d->history.empty()) {
                ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);HistoryLines(*w,*d,kind,cv?"voltage_V":"frequency_Hz",key);
                if(view==6 || view==8){ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);HistoryLines(*w,*d,kind,cv?"voltage_V":"frequency_Hz","G_over_omega_F");}if(Archived(*d,*d->acResults.get(kind),kind))continue;
            }
            if(const auto it=d->changeAc.find(kind);it!=d->changeAc.end()){const auto& before=AcCols(it->second->points,view);const auto colour=d->colour.get<std::vector<float>>();ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);ReferenceLine(*w,d->name,before.a,before.b,{colour[0],colour[1],colour[2],colour[3]});if(view==6 || view==8){ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ReferenceLine(*w,d->name+" G/omega",before.a,before.c,{colour[0],colour[1],colour[2],colour[3]});}}
            if(w->liveChangeReference!=3 && d->changeAc.contains(kind) && d->changeAc.at(kind)==d->acResults.get(kind))continue;
            const auto &ac=AcCols(d->acResults.get(kind)->points,view);const auto &xx=ac.a,&yy=ac.b,&gg=ac.c;
            const auto c=d->colour.get<std::vector<float>>();const auto label=d->name+"##"+d->id;
            ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);ImPlot::PlotLine(label.c_str(),xx.data(),yy.data(),int(xx.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],c[3]),ImPlotProp_LineWeight,2.5f});
            if(view==6 || view==8) {ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlot::PlotLine((d->name+" G/omega##"+d->id).c_str(),xx.data(),gg.data(),int(xx.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],c[3]),ImPlotProp_LineWeight,2.5f});}
        }
        EndSavedPlot(*w,names[view]);
    }
}
void Panel::Devices() {
    // Project tree: Project > Devices > device > (Layers, Interfaces, Contacts, Illumination, Results). One line per item.
    ImGui::BeginDisabled(w->Busy());
    if(ImGui::Button(ICON_FA_PLUS " New device")) Do(Cmd::AddDevice::With());w->tutorialOverlay.Item("scapslab.Project","new");ImGui::SameLine();
    if(ImGui::Button(ICON_FA_FILE_IMPORT " Import device")) {try {if(const auto path=OpenFileDialog({"Device YAML","yaml,yml"})) Do(Cmd::AddDevice::With({{"path",path->Utf8()}}));}catch(const std::exception &e){w->status=e.what();}}
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Import a device YAML (or use File / Import SCAPS .def). Unsupported items are reported as warnings.");
    ImGui::EndDisabled();
    if(ImGui::Button("Undo")) Do(Cmd::Undo::With());w->tutorialOverlay.Item("scapslab.Project","undo"); // S9: synchronous, works while calculating
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (also restores a deleted device)");
    ImGui::SameLine();if(ImGui::Button("Redo")) Do(Cmd::Redo::With());w->tutorialOverlay.Item("scapslab.Project","redo");
    ImGui::SameLine();if(ImGui::Button(ICON_FA_PLAY " Run all checked") && !w->Busy()) Do(Cmd::RunAll::With());
    ImGui::SameLine();ImGui::TextDisabled("Compare: show in plots");
    if(w->Busy()) {
        const auto job=w->jobs->GetJob(w->ActiveJob());
        if(w->batchTotal) ImGui::ProgressBar(float(w->batchCompleted)/float(w->batchTotal),ImVec2(-90,0),(std::to_string(w->batchCompleted)+" / "+std::to_string(w->batchTotal)+" devices").c_str());
        else ImGui::ProgressBar(job && job->totalWork>0?float(job->completedWork/job->totalWork):0.f,ImVec2(-90,0),job?job->currentStage.c_str():"");
        ImGui::SameLine();if(ImGui::Button("Cancel")) Do(Cmd::Cancel::With());
    } else if(w->dirty || !w->status.empty()) ImGui::TextDisabled("%s%s",w->dirty?"* unsaved  ":"",w->status.c_str());
    ImGui::Separator();
    const auto select=[&](const DeviceState &d) {if(w->project.active_device!=d.id) Do(Cmd::SelectDevice::With({{"device_id",d.id}}));};
    const auto go=[&](int target) {if(PanelShown(target))w->focusView=target;}; // hidden panels: no jump
    // Tree node with room for inline decoration; returns {open, clicked (not the arrow)}.
    const auto node=[&](const char *id,ImGuiTreeNodeFlags flags,bool selected) {
        const bool open=ImGui::TreeNodeEx(id,flags|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_AllowOverlap|ImGuiTreeNodeFlags_OpenOnArrow|(selected?ImGuiTreeNodeFlags_Selected:0));
        return std::pair{open,ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()};
    };
    const auto text=[&](const std::string &label,const std::string &dim={}) {ImGui::SameLine(0,4);ImGui::TextUnformatted(label.c_str());if(!dim.empty()){ImGui::SameLine(0,8);ImGui::TextDisabled("%s",dim.c_str());}};
    static std::string renameId,inlineId;static char renameBuf[256]{};static bool openRename=false,focusInline=false; // OpenPopup must run at the modal's own ID scope, not inside the tree's PushID
    const auto devices=w->project.devices; // commands replace device state: iterate a snapshot
    if(ImGui::TreeNodeEx("##project",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_OpenOnArrow)) {
        text(ICON_FA_FOLDER_OPEN " "+w->project.name+(w->dirty?" *":""));
        if(ImGui::BeginPopupContextItem("project-menu")) {
            if(ImGui::MenuItem("Rename project...")) {renameId="";std::snprintf(renameBuf,sizeof(renameBuf),"%s",w->project.name.c_str());ImGui::CloseCurrentPopup();openRename=true;}
            if(ImGui::MenuItem("Save")) w->fileAction="save";
            if(ImGui::MenuItem("Open...")) w->fileAction="open";
            ImGui::EndPopup();
        }
        if(ImGui::TreeNodeEx("##devices",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_OpenOnArrow)) {
            text("Devices ("+std::to_string(devices.size())+")");
            for(const auto &dp:devices) {
                const auto &d=*dp;const bool active=w->project.active_device==d.id;
                ImGui::PushID(d.id.c_str());
                const float rowRight=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x,box=ImGui::GetFrameHeight();
                const auto [open,clicked]=node("##device",ImGuiTreeNodeFlags_DefaultOpen,active);
                if(clicked) {select(d);go(14);}
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nStatus: %s",d.name.c_str(),d.status.c_str());
                if(ImGui::BeginDragDropSource()) {ImGui::SetDragDropPayload("SCAPSLAB_DEVICE",d.id.c_str(),d.id.size()+1);ImGui::TextUnformatted(d.name.c_str());ImGui::EndDragDropSource();}
                if(ImGui::BeginDragDropTarget()) {if(const auto *payload=ImGui::AcceptDragDropPayload("SCAPSLAB_DEVICE")) Do(Cmd::MoveDevice::With({{"device_id",static_cast<const char*>(payload->Data)},{"index",int(&dp-&devices[0])}}));ImGui::EndDragDropTarget();}
                if(ImGui::BeginPopupContextItem("device-menu")) {
                    select(d);
                    if(ImGui::MenuItem("Rename...")) {renameId=d.id;std::snprintf(renameBuf,sizeof(renameBuf),"%s",d.name.c_str());ImGui::CloseCurrentPopup();openRename=true;}
                    if(ImGui::MenuItem("Duplicate")) Do(Cmd::DuplicateDevice::With({{"device_id",d.id}}));
                    if(ImGui::MenuItem("Delete",nullptr,false,devices.size()>1)) Do(Cmd::RemoveDevice::With({{"device_id",d.id}}));
                    ImGui::Separator();
                    if(ImGui::MenuItem("Add layer")) {Do(Cmd::EditLayer::With({{"action","add"}}));go(1);}
                    if(ImGui::MenuItem("Add defect")) {AddDefectTo(false,w->Current().selectedLayer);go(11);}
                    if(ImGui::MenuItem("Add interface defect")) {AddDefectTo(true,w->Current().selectedLayer);go(12);}
                    ImGui::Separator();
                    if(ImGui::MenuItem("Export SCAPS .def...")) w->fileAction="export_def";
                    ImGui::EndPopup();
                }
                const auto c=d.colour.get<std::vector<float>>();bool compare=w->project.compare_set.contains(d.id);
                ImGui::SameLine(0,4);ImGui::ColorButton("##colour",ImVec4(c[0],c[1],c[2],c[3]),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoPicker,ImVec2(box*.6f,box*.6f));
                ImGui::SameLine(0,4);
                if(inlineId==d.id) { // inline rename (double-click or F2): Enter/click away commits, Esc cancels
                    if(focusInline){ImGui::SetKeyboardFocusHere();focusInline=false;}
                    ImGui::SetNextItemWidth(std::max(80.f,rowRight-ImGui::GetCursorPosX()-box*2));
                    const bool entered=ImGui::InputText("##inline",renameBuf,sizeof(renameBuf),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
                    if(ImGui::IsKeyPressed(ImGuiKey_Escape)) inlineId.clear();
                    else if(entered || ImGui::IsItemDeactivated()) {if(renameBuf[0] && d.name!=renameBuf) Do(Cmd::UpdateDevice::With({{"device_id",d.id},{"name",renameBuf}}));inlineId.clear();}
                } else {
                    if(compare) ImGui::TextUnformatted((d.name+(d.dirty?" *":"")).c_str());else ImGui::TextDisabled("%s",(d.name+(d.dirty?" *":"")).c_str());
                    const bool dbl=ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                    const bool f2=active && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2);
                    if(dbl || f2) {inlineId=d.id;focusInline=true;std::snprintf(renameBuf,sizeof(renameBuf),"%s",d.name.c_str());}
                }
                if(w->LaneJob(d.id)) {
                    const auto job=w->jobs->GetJob(w->LaneJob(d.id));ImGui::SameLine(0,6);
                    ImGui::ProgressBar(job && job->totalWork>0?float(job->completedWork/job->totalWork):0.f,ImVec2(48,box*.35f),"");
                }
                ImGui::SameLine();ImGui::SetCursorPosX(rowRight-box);
                ImGui::BeginDisabled(w->Busy());if(ImGui::Checkbox("##compare",&compare)) Do(Cmd::UpdateDevice::With({{"device_id",d.id},{"compare",compare}}));ImGui::EndDisabled();
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Compare: show in plots");
                if(open) {
                    const auto &doc=d.Document();
                    const auto layers=doc.is_object() && doc.contains("layers")?doc.at("layers"):Json::array();
                    const auto sheets=doc.is_object()?doc.value("interface_defects",Json::array()):Json::array();
                    if(ImGui::TreeNodeEx("##layers",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_OpenOnArrow)) {
                        text(ICON_FA_LAYER_GROUP " Structure ("+std::to_string(layers.size())+" layers)");
                        if(PanelShown(2)) { // contacts are the first and last items of the stack
                            const auto contact=[&](const char *label,const char *id) {
                                ImGui::PushID(id);const auto [co,cc]=node("##contact",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,false);(void)co;
                                if(cc) {select(d);go(2);}
                                text(label);ImGui::PopID();
                            };
                            contact(ICON_FA_PLUG " Left contact (front)","left");
                        }
                        for(int i=0;i<int(layers.size());++i) {
                            const auto &l=layers[i];const auto &defects=l.value("defects",Json::array());ImGui::PushID(i);
                            const auto [lopen,lclick]=node("##layer",defects.empty()?ImGuiTreeNodeFlags_Leaf:ImGuiTreeNodeFlags_None,active && d.selectedLayer==i && d.selectedInterface<0);
                            if(lclick) {select(d);w->Current().selectedLayer=i;w->Current().selectedInterface=-1;go(1);}
                            { // drag a layer onto another row of the same device: reorder through the layer "move" operation (stable IDs kept)
                                const std::string payloadText=d.id+"\n"+l.value("scapslab_id",std::string{});
                                if(ImGui::BeginDragDropSource()) {ImGui::SetDragDropPayload("SCAPSLAB_LAYER",payloadText.c_str(),payloadText.size()+1);ImGui::TextUnformatted(l.value("name",std::string("layer")).c_str());ImGui::EndDragDropSource();}
                                if(ImGui::BeginDragDropTarget()) {
                                    if(const auto *payload=ImGui::AcceptDragDropPayload("SCAPSLAB_LAYER",ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                                        const std::string text=static_cast<const char*>(payload->Data);const auto nl=text.find('\n');
                                        const std::string from=text.substr(nl+1);int fromIndex=-1;
                                        for(int k=0;k<int(layers.size());++k) if(layers[k].value("scapslab_id",std::string{})==from) fromIndex=k;
                                        if(text.substr(0,nl)==d.id && fromIndex>=0 && fromIndex!=i) {
                                            const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();const float y=fromIndex>i?min.y:max.y; // lands above the row when moving up, below when moving down
                                            ImGui::GetForegroundDrawList()->AddLine(ImVec2(min.x,y),ImVec2(max.x,y),ImGui::GetColorU32(ImGuiCol_NavHighlight),2.f);
                                            if(payload->IsDelivery()) {select(d);Do(Cmd::EditLayer::With({{"action","move"},{"layer_id",from},{"index",i}}));}
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                            }
                            if(ImGui::BeginPopupContextItem("layer-menu")) {
                                select(d);w->Current().selectedLayer=i;
                                if(ImGui::MenuItem("Add defect")) {AddDefectTo(false,i);go(11);}
                                if(ImGui::MenuItem("Add layer")) {Do(Cmd::EditLayer::With({{"action","add"}}));go(1);}
                                if(ImGui::MenuItem("Add interface to next layer",nullptr,false,i+1<int(layers.size()))) {AddDefectTo(true,i);go(12);}
                                if(ImGui::MenuItem("Open in new window")) OpenLayerWindow(w,d.id,l.value("scapslab_id",std::string{}));
                                ImGui::EndPopup();
                            }
                            char thickness[48];std::snprintf(thickness,sizeof(thickness),"%.3g um",l.value("thickness_m",0.)*1e6);
                            text(l.value("name",std::string("layer")),thickness);
                            if(lopen) {
                                for(int k=0;k<int(defects.size());++k) {
                                    ImGui::PushID(k);const auto [dopen,dclick]=node("##defect",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,false);(void)dopen;
                                    if(dclick) {select(d);w->Current().selectedLayer=i;w->Current().selectedInterface=-1;selectedDefect=k;go(11);}
                                    text(ICON_FA_CIRCLE_DOT " "+defects[k].value("scapslab_id",std::string("defect")));ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                            std::vector<int> sheetIdx;for(int k=0;k<int(sheets.size());++k) if(sheets[k].value("left_layer_index",-1)==i) sheetIdx.push_back(k);
                            if(i+1<int(layers.size()) && !sheetIdx.empty()) { // interface shown inline only when it has defects; not draggable, but a drop here maps to the layer move index
                                ImGui::PushID(1000+i);
                                const auto [iopen,iclick]=node("##interface",ImGuiTreeNodeFlags_DefaultOpen,active && d.selectedInterface==i);
                                if(iclick) {select(d);w->Current().selectedInterface=i;w->Current().selectedLayer=i;go(12);}
                                if(ImGui::BeginDragDropTarget()) {
                                    if(const auto *payload=ImGui::AcceptDragDropPayload("SCAPSLAB_LAYER",ImGuiDragDropFlags_AcceptBeforeDelivery|ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                                        const std::string text=static_cast<const char*>(payload->Data);const auto nl=text.find('\n');
                                        const std::string from=text.substr(nl+1);int fromIndex=-1;
                                        for(int k=0;k<int(layers.size());++k) if(layers[k].value("scapslab_id",std::string{})==from) fromIndex=k;
                                        const int target=fromIndex<=i?i:i+1;
                                        if(text.substr(0,nl)==d.id && fromIndex>=0 && fromIndex!=target) {
                                            const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
                                            ImGui::GetForegroundDrawList()->AddLine(ImVec2(min.x,max.y),ImVec2(max.x,max.y),ImGui::GetColorU32(ImGuiCol_NavHighlight),2.f);
                                            if(payload->IsDelivery()) {select(d);Do(Cmd::EditLayer::With({{"action","move"},{"layer_id",from},{"index",target}}));}
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                if(ImGui::BeginPopupContextItem("interface-menu")) {
                                    select(d);w->Current().selectedInterface=i;w->Current().selectedLayer=i;
                                    if(ImGui::MenuItem("Add interface defect")) {AddDefectTo(true,i);go(12);}
                                    if(ImGui::MenuItem("Remove defects")) {auto draft=CreateRef<ProjectDraft>(*d.draft);auto &arr=draft->project.interface_defects;for(int k=int(arr.size())-1;k>=0;--k)if(arr[k].left_layer_index==i)RemoveDraftDefect(*draft,i,k,true);Do(Cmd::EditDocument::Make(std::move(draft)));}
                                    ImGui::EndPopup();
                                }
                                text(ICON_FA_ARROWS_LEFT_RIGHT " Interface "+std::to_string(i+1)+" ("+layers[i].value("name",std::string{})+" / "+layers[i+1].value("name",std::string{})+")",std::to_string(sheetIdx.size())+" defect(s)");
                                if(iopen) {
                                    for(const int k:sheetIdx) {
                                        ImGui::PushID(k);const auto [dopen,dclick]=node("##idefect",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,false);(void)dopen;
                                        if(dclick) {select(d);w->Current().selectedInterface=i;w->Current().selectedLayer=i;selectedDefect=k;go(12);}
                                        text(ICON_FA_CIRCLE_DOT " "+sheets[k].value("id",std::string("defect")));ImGui::PopID();
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                        }
                        if(PanelShown(2)) {ImGui::PushID("right");const auto [co,cc]=node("##contact",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,false);(void)co;if(cc) {select(d);go(2);}text(ICON_FA_PLUG " Right contact (back)");ImGui::PopID();}
                        ImGui::TreePop();
                    }
                    for(const auto &[label,target]:{std::pair<const char*,int>{ICON_FA_SUN " Illumination",19}}) {
                        if(!PanelShown(target)) continue;
                        ImGui::PushID(target);const auto [lopen,lclick]=node("##leaf",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,false);(void)lopen;
                        if(lclick) {select(d);go(target);}
                        text(label);ImGui::PopID();
                    }
                    const auto &history=d.history;
                    if(ImGui::TreeNodeEx("##results",ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_OpenOnArrow|(active?ImGuiTreeNodeFlags_DefaultOpen:0))) {
                        text(ICON_FA_CHART_LINE " Results ("+std::to_string(history.size())+")");
                        static std::set<std::string> multi;static std::string anchor; // Ctrl/Shift multi-selection of runs
                        std::vector<Json> deferred; // history edits run after the list (they may replace the history under our references)
                        for(int r=int(history.size())-1;r>=0;--r) {
                            const auto &run=history[r];const auto id=run.at("run_id").get<std::string>();
                            ImGui::PushID(id.c_str());
                            const auto [ropen,rclick]=node("##run",ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen,active && (d.selectedRun==id || multi.contains(id)));(void)ropen;
                            const std::string kind=run.value("kind",std::string{});
                            const bool hiddenKind=!PanelShown(kind=="iv"?10:kind=="cv"?6:kind=="cf"?8:kind=="qe"?19:kind=="band"?14:kind=="batch"?16:5);
                            if(hiddenKind && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("panel disabled in basic mode");
                            if(rclick && !hiddenKind) {
                                const auto &io=ImGui::GetIO();
                                if(io.KeyShift && !anchor.empty()) {
                                    int a=-1;for(int k=0;k<int(history.size());++k)if(history[k].at("run_id")==anchor)a=k;
                                    if(a>=0)for(int k=std::min(a,r);k<=std::max(a,r);++k)multi.insert(history[k].at("run_id").get<std::string>());else multi.insert(id);
                                } else if(io.KeyCtrl) {if(!multi.erase(id))multi.insert(id);anchor=id;}
                                else {multi={id};anchor=id;}
                                deferred.push_back({{"operation","history_select"},{"device_id",d.id},{"run_id",id}});
                                if(!io.KeyShift && !io.KeyCtrl)go(kind=="iv"?10:kind=="cv"?6:kind=="cf"?8:kind=="qe"?19:kind=="band"?14:5);
                            }
                            if(ImGui::BeginPopupContextItem("runmenu")) {
                                if(!multi.contains(id)){multi={id};anchor=id;}
                                const std::vector<std::string> picked(multi.begin(),multi.end());const std::string count=" ("+std::to_string(picked.size())+")";
                                const auto each=[&](const Json &change){for(const auto &x:picked){Json u=change;u["operation"]="history_update";u["device_id"]=d.id;u["run_id"]=x;deferred.push_back(u);}};
                                if(ImGui::MenuItem(("Show in plots"+count).c_str()))each({{"visible",true}});
                                if(ImGui::MenuItem(("Hide in plots"+count).c_str()))each({{"visible",false}});
                                if(ImGui::MenuItem(("Pin"+count).c_str()))each({{"pinned",true}});
                                if(ImGui::MenuItem(("Unpin"+count).c_str()))each({{"pinned",false}});
                                if(ImGui::MenuItem(("Compare (overlay)"+count).c_str()))each({{"pinned",true},{"visible",true}});
                                if(ImGui::MenuItem(("Export CSV..."+count).c_str())) try {if(const auto path=SaveFileDialog({"Results CSV","csv"},"results.csv"))deferred.push_back({{"operation","export_results"},{"path",path->Utf8()},{"format","csv"},{"run_ids",picked}});}catch(const std::exception &e){w->status=e.what();}
                                ImGui::Separator();
                                if(ImGui::MenuItem(("Delete"+count).c_str())){deferred.push_back({{"operation","history_clear"},{"device_id",d.id},{"run_ids",picked}});multi.clear();}
                                ImGui::EndPopup();
                            }
                            const auto rc=run.at("colour").get<std::vector<float>>();
                            ImGui::SameLine(0,4);ImGui::ColorButton("##rc",ImVec4(rc[0],rc[1],rc[2],rc[3]),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoPicker,ImVec2(box*.5f,box*.5f));
                            const std::time_t seconds=run.value("time_unix_ms",std::int64_t(0))/1000;std::tm local{};localtime_s(&local,&seconds);char time[32];std::strftime(time,sizeof(time),"%H:%M:%S",&local);
                            ImGui::BeginDisabled(hiddenKind);text(RunLabel(run),time);ImGui::EndDisabled();ImGui::PopID();
                        }
                        for(const auto &request:deferred)(void)w->Dispatch(request);
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    if(openRename) {openRename=false;ImGui::OpenPopup("Rename##dialog");}
    if(ImGui::BeginPopupModal("Rename##dialog",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(renameId.empty()?"Rename project":"Rename device");
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool entered=ImGui::InputText("##name",renameBuf,sizeof(renameBuf),ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if(ImGui::Button("OK") || entered) {
            if(renameId.empty()) Do(Cmd::EditProject::With({{"name",renameBuf}}));else Do(Cmd::UpdateDevice::With({{"device_id",renameId},{"name",renameBuf}}));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void Panel::AddDefectTo(bool sheet,int layer) {
    auto draft=CreateRef<ProjectDraft>(*(w->EditDraft()?w->EditDraft():w->Current().draft.get()));
    w->pendingDefect=AddDraftDefect(*draft,layer,sheet);w->pendingSheet=sheet;
    Do(Cmd::EditDocument::Make(std::move(draft)));w->Current().selectedLayer=layer;w->Current().selectedInterface=sheet?layer:-1;
}
void Panel::Compare() {
    absoluteLog=w->plotSession.settings.plots["I-V compare"].log[ImAxis_Y1].value_or(w->plotSession.settings.ivAbsoluteLog);
    static bool historyOpen=false; // one compact row: Run history toggle | Log |J| | Run all checked | Single shot | Shots
    if(ImGui::Button(historyOpen?"Run history  v":"Run history  >"))historyOpen=!historyOpen;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Log |J|");ImGui::SameLine();
    if(ImGui::Checkbox("##iv_absolute_log",&absoluteLog)){w->plotSession.settings.ivAbsoluteLog=absoluteLog;w->plotSession.settings.plots["I-V compare"].log[ImAxis_Y1]=absoluteLog;w->projectDirty=true;w->RefreshDirty();}
    ImGui::SameLine();ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Run all checked")) Do(Cmd::RunAll::With());ImGui::EndDisabled();ShotBar(*w,"iv");
    if(historyOpen) DrawHistory(*w);
    bool any=false;for(const auto &d:w->project.devices) any|=(w->project.compare_set.contains(d->id) || d->id==w->project.active_device) && (!d->result.empty() || !d->history.empty() || d->liveRunning);
    if(!any) {ImGui::TextWrapped("Tick 'Compare' on devices in the Project tree to overlay them.");return;}
    bool light=false;for(const auto &d:w->project.devices) light|=w->project.compare_set.contains(d->id) && bool(d->Iv()->photovoltaic);
    if(light && ImGui::BeginTable("Solar metrics",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        for(const auto *label:{"Device","Voc [V]","Jsc [mA/cm^2]","FF [%]","Efficiency [%]"}) ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && bool(d->Iv()->photovoltaic)) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(d->name.c_str());
            for(const auto *key:{"Voc_V","Jsc_A_m2","FF_fraction","efficiency_fraction"}) {ImGui::TableNextColumn();const auto& pv=*d->Iv()->photovoltaic;const double value=std::string_view(key)=="Voc_V"?-pv.Voc_V:std::string_view(key)=="Jsc_A_m2"?-pv.Jsc_Am2:std::string_view(key)=="FF_fraction"?pv.FF:pv.eta;if(std::isfinite(value)) ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",value*(std::string(key)=="Jsc_A_m2"?.1:std::string(key)=="Voc_V"?1:100));else ImGui::TextUnformatted("--");}
        }ImGui::EndTable();
    }
    IvStatus(w->Current());
    if(BeginSavedPlot(*w,"I-V compare",ImVec2(-1,-1))) {
        ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        SetupSavedPlot(w->plotSession,"I-V compare");
        for(const auto &d:w->project.devices) if((w->project.compare_set.contains(d->id) || d->id==w->project.active_device) && (!d->result.empty() || !d->history.empty() || d->liveRunning)) {
            const auto shown=d->id==w->project.active_device?d->ProfileSweep():d->Iv();
            if(shown->points.empty()){HistoryLines(*w,*d,"iv","voltage_V","J_A_m2",absoluteLog);if(d->id==w->project.active_device)PreviewOverlay(*d,absoluteLog);continue;}
            const auto &ivc=IvCols(shown->points,absoluteLog);const auto &x=ivc.a,&y=ivc.b;

            if(!d->history.empty())HistoryLines(*w,*d,"iv","voltage_V","J_A_m2",absoluteLog);
            const float c[4]={d->colour[0].get<float>(),d->colour[1].get<float>(),d->colour[2].get<float>(),d->colour[3].get<float>()};
            static std::string label;label=d->name;label+="##";label+=d->id; // reused buffer: no per-frame allocation
            if(!d->result.empty() && !Archived(*d,*d->Iv(),"iv") && !IvIsReference(*w,*d)){const auto& live=IvCols(d->Iv()->points,absoluteLog);StaleDim dim(*d);ImPlot::PlotLine(label.c_str(),live.a.data(),live.b.data(),int(live.a.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],SelectedOffLive(*d,"iv")?.3f:c[3]),ImPlotProp_LineWeight,2.5f});}
            IvReference(*w,*d,absoluteLog);if(d->id==w->project.active_device) PreviewOverlay(*d,absoluteLog);
            if(d->id==w->project.active_device && !x.empty()) {
                int bias=std::clamp(d->selectedBias,0,int(x.size())-1);double voltage=x[bias];
                ImPlot::PlotScatter("Selected bias",&x[bias],&y[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
                const auto nearest=[&](double at){return int(std::min_element(x.begin(),x.end(),[&](double a,double b){return std::abs(a-at)<std::abs(b-at);})-x.begin());};
                if(ImPlot::DragLineX(1,&voltage,ImVec4(c[0],c[1],c[2],c[3]))) bias=nearest(voltage);
                else if(ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(0) && ImGui::GetIO().MouseDragMaxDistanceSqr[0]<16.f) bias=nearest(ImPlot::GetPlotMousePos().x); // click selects the nearest bias point
                if(bias!=d->selectedBias)Do(Cmd::SelectBias::Make(bias));
            }
        }EndSavedPlot(*w,"I-V compare");
    }
}

}






namespace SST::ScapsLab {
namespace {
double ParameterNumber(const scaps_core::BatchRow& row,const std::string& path) {
    return std::visit([](const auto& value)->double {if constexpr(std::is_arithmetic_v<std::decay_t<decltype(value)>>)return double(value);else return NAN;},row.parameters.at(path));
}
std::string MemberLabel(const scaps_core::BatchRow &row,const Json &spec) {
    std::string label=row.id;
    for(const auto &a:spec.at("axes")) {const auto path=a.at("path").get<std::string>();label+=" | "+ParameterLabel(path,a.value("unit",std::string("1")),ParameterNumber(row,path));}
    return label;
}
}
void Panel::Batch() {
    auto& d=w->Current();if(!d.draft)return;const bool legacy=!d.runPlanEdited && d.batch->source && !d.actions.value("iv",Json::object()).contains("include") && !w->path.empty();auto spec=*d.batch;bool changed=false;const auto parameters=ProjectParameters(*d.draft);
    ImGui::TextWrapped("%s | Uses included Run Actions and their own ranges. Ctrl+click for exact values.",d.name.c_str());ImGui::BeginDisabled(w->Busy());
    if(legacy){ImGui::TextUnformatted("Legacy batch sampling (preserved)");if(ImGui::Button("Use included Run Actions"))Do(Cmd::SetRunPlan::Make(d.runPlan));}
    changed|=Toggle("Store curves",spec.include_curves);changed|=Toggle("Store profiles (selected bias)",spec.include_profiles);
    if(!w->Busy() && (sweepSource.lock()!=d.batch || sweepEditors.size()!=spec.axes.size())) {
        sweepEditors.clear();
        for(const auto& axis:spec.axes){const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==axis.parameter_id || (axis.parameter_id.empty() && p.path==axis.path);});SweepControlState state;if(p!=parameters.end())InitSweepControl(state,EditSweep(axis,*p),p->path);sweepEditors.push_back(std::move(state));}
        sweepSource=d.batch;
    }
    BeginRow("Dimensions / mode");int mode=spec.mode=="together" || spec.mode=="linked"?2:spec.axes.size()==1?0:1;
    if(ImGui::Combo("##mode",&mode,"1-D\0Nested 2-D\0Linked\0")) {
        if(mode!=2 && spec.axes.size()>2)w->status="Remove extra linked parameters before selecting 1-D / Nested 2-D";
        else {changed=true;spec.mode=mode==2?"linked":"nested";if(mode==0){spec.axes.resize(1);sweepEditors.resize(1);}else if(mode==1 && spec.axes.size()==1){const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id!=spec.axes[0].parameter_id;});if(p!=parameters.end()){BatchAxis axis;axis.parameter_id=p->id;axis.values=std::vector<double>{p->value};spec.axes.push_back(axis);SweepControlState state;InitSweepControl(state,EditSweep(axis,*p),p->path);sweepEditors.push_back(std::move(state));}}}
    }EndRow();
    TextInput("Search project tree",parameterSearch,sizeof(parameterSearch));
    std::optional<std::size_t> remove;
    for(std::size_t i=0;i<spec.axes.size() && i<sweepEditors.size();++i) {
        ImGui::PushID(int(i));auto& axis=spec.axes[i];auto& state=sweepEditors[i];auto selected=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==axis.parameter_id || (axis.parameter_id.empty() && p.path==axis.path);});ImGui::SeparatorText(("Parameter "+std::to_string(i+1)).c_str());BeginRow("Project parameter");
        const auto title=selected==parameters.end()?std::string("Unresolved parameter: ")+axis.parameter_id:selected->label+" ["+selected->unit+"]";
        if(ImGui::BeginCombo("##parameter",title.c_str(),ImGuiComboFlags_HeightLargest)) {
            std::string group;for(const auto& p:parameters){const auto& label=p.label;if(parameterSearch[0] && label.find(parameterSearch)==std::string::npos)continue;const auto root=label.substr(0,label.find(" / "));if(group!=root){ImGui::SeparatorText(root.c_str());group=root;}
                if(ImGui::Selectable((label+" ["+p.unit+"]##"+p.id).c_str(),selected!=parameters.end() && p.id==selected->id)){axis=BatchAxis{};axis.parameter_id=p.id;axis.values=std::vector<double>{p.value};InitSweepControl(state,EditSweep(axis,p),p.path);changed=true;}}
            ImGui::EndCombo();
        }EndRow();
        selected=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==axis.parameter_id || (axis.parameter_id.empty() && p.path==axis.path);});
        if(selected!=parameters.end()) {
            if(SweepRangeControl(state) && state.error.empty())try{auto next=PrepareSweepAxis(*d.draft,selected->id,state.editor);axis=std::move(next);changed=true;}catch(const std::exception& e){state.error=e.what();}
            SweepPreview(axis,state);
        }
        if(spec.axes.size()>1 && ImGui::Button("Remove parameter"))remove=i;
        ImGui::PopID();
    }
    if(remove){spec.axes.erase(spec.axes.begin()+*remove);sweepEditors.erase(sweepEditors.begin()+*remove);changed=true;}
    if((spec.mode=="linked" || spec.mode=="together") && ImGui::Button("Add linked parameter")) {
        const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return std::none_of(spec.axes.begin(),spec.axes.end(),[&](const auto& axis){return axis.parameter_id==p.id;});});
        if(p!=parameters.end()){BatchAxis axis;axis.parameter_id=p->id;axis.values=std::vector<double>(BatchAxisValues(spec.axes.front()).size(),p->value);spec.axes.push_back(axis);SweepControlState state;InitSweepControl(state,EditSweep(axis,*p),p->path);sweepEditors.push_back(std::move(state));changed=true;}
    }
    if(changed){auto next=CreateRef<const BatchDraft>(spec);sweepSource=next;Do(Cmd::SetBatchSettings::Make(next));}
    std::optional<BatchPlan> preview;std::string error;try{preview=PrepareBatch(*d.draft,spec,*d.recorder);}catch(const std::exception& e){error=e.what();}if(!error.empty())ImGui::TextWrapped("Configuration: %s",error.c_str());
    const bool invalidEditor=std::any_of(sweepEditors.begin(),sweepEditors.end(),[](const auto& state){return !state.error.empty();});
    ImGui::BeginDisabled(!error.empty() || invalidEditor);if(ImGui::Button(legacy?"Run legacy sampling":"Run batch")){if(!legacy)Do(Cmd::SetRunPlan::Make(d.runPlan));Do(Cmd::RunBatch::With());}ImGui::EndDisabled();ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!w->AnyActive());if(ImGui::Button("Stop"))Do(Cmd::Cancel::With());ImGui::EndDisabled();
    if(w->ActiveJob()){const auto job=w->jobs->GetJob(w->ActiveJob());if(job){ImGui::ProgressBar(job->totalWork>0?job->completedWork/job->totalWork:0);ImGui::TextWrapped("%s",job->currentStage.c_str());}}ImGui::TextWrapped("%s",w->status.c_str());
    if(preview && !legacy){
        std::size_t samples=0;for(const auto& action:d.runPlan->actions)if(action.include){const auto n=ActionSamples(action,d.draft->project).size();samples+=n;ImGui::Text("%s: %zu samples / member",ActionId(action.kind),n);}
        ImGui::Text("Preview: %zu members, %zu Action samples",preview->combinations.size(),preview->combinations.size()*samples);
    }
    if(preview && legacy){const auto& p=*preview;const auto& biases=p.engine.voltages_V;const auto& frequencies=p.engine.frequencies_Hz;
        ImGui::TextWrapped("Preview: %zu members x %zu biases x (DC + %zu AC frequencies) = %zu samples. Polarity: left minus right.",p.combinations.size(),biases.size(),frequencies.size(),p.SampleCount());
        if(!d.batchResult.empty())ImGui::Text("Estimated time: %.3g s (previous mean/member)",d.batchResult.typed()->Schema().value("elapsed_s",0.)/std::max(std::size_t(1),d.batchResult.typed()->rows.size())*p.combinations.size());else ImGui::TextUnformatted("Estimated time: unavailable before first run");
        ImGui::Text("Bias: %.5g ... %.5g V | DC + %zu AC frequencies",biases.front(),biases.back(),frequencies.size());
        if(ImGui::TreeNode("Bias / frequency samples")){ImGui::BeginChild("Samples",ImVec2(0,100),ImGuiChildFlags_Borders);for(double v:biases)ImGui::Text("%.7g V",v);std::string values="[";for(double f:frequencies){if(values.size()>1)values+=", ";char text[64];std::snprintf(text,sizeof(text),"%.17g",f);values+=text;}values+=']';ImGui::TextWrapped("AC frequencies [Hz]: %s",values.c_str());ImGui::EndChild();ImGui::TreePop();}
        if(ImGui::BeginChild("Combinations",ImVec2(0,140),ImGuiChildFlags_Borders))for(std::size_t i=0;i<p.combinations.size();++i){std::string label=std::to_string(i+1)+": ";for(const auto& [path,value]:p.combinations[i]){char text[64];std::snprintf(text,sizeof(text),"%.17g",value);label+=path+" = "+text+"; ";}ImGui::TextWrapped("%s",label.c_str());}ImGui::EndChild();
    }
}
void Panel::Recorder() {
    auto& d=w->Current();auto recorder=*d.recorder;auto& columns=recorder.columns;auto spec=*d.batch;bool changed=false;
    ImGui::TextWrapped("Extract from stored data uses retained samples. Missing samples remain flagged. Calculate missing creates a new explicit run on the original snapshot.");ImGui::BeginDisabled(w->Busy());ImGui::SetNextItemOpen(d.batchResult.empty(),ImGuiCond_Once);
    if(ImGui::TreeNode("Quantities and sampling")) {
        for(const char* q:{"Voc","Jsc","FF","eta","J","C","G","Vbi"}){bool enabled=std::any_of(columns.begin(),columns.end(),[&](const auto& c){return c.quantity==q;});if(Toggle(q,enabled)){changed=true;std::erase_if(columns,[&](const auto& c){return c.quantity==q;});if(enabled){scaps_core::RecorderColumn c;c.id=c.quantity=q;if(std::string_view(q)=="J" || std::string_view(q)=="C" || std::string_view(q)=="G")c.voltage_V=0;if(std::string_view(q)=="C" || std::string_view(q)=="G" || std::string_view(q)=="Vbi")c.frequency_Hz=1e4;columns.push_back(std::move(c));}}}
        static int newQuantity=0;const char* quantities[]={"J","C","G","profile","Voc","Jsc","FF","eta","Vbi","Pmax","Vmp"};
        BeginRow("Add quantity");ImGui::Combo("##new-quantity",&newQuantity,"J\0C\0G\0profile\0Voc\0Jsc\0FF\0eta\0Vbi\0Pmax\0Vmp\0");EndRow();
        if(ImGui::Button("Add column")) {
            scaps_core::RecorderColumn c;c.quantity=quantities[newQuantity];int suffix=int(columns.size()+1);do{c.id=c.quantity+"_"+std::to_string(suffix++);}while(std::any_of(columns.begin(),columns.end(),[&](const auto& other){return other.id==c.id;}));
            if(c.quantity=="J" || c.quantity=="C" || c.quantity=="G" || c.quantity=="profile")c.voltage_V=0;
            if(c.quantity=="C" || c.quantity=="G" || c.quantity=="Vbi")c.frequency_Hz=1e4;
            if(c.quantity=="profile"){c.field="n_m3";c.x_m=0;}columns.push_back(std::move(c));changed=true;
        }
        std::optional<std::size_t> removeColumn;
        for(std::size_t i=0;i<columns.size();++i) {
            auto& c=columns[i];ImGui::PushID(int(i));ImGui::SeparatorText(c.quantity.c_str());char id[256];std::snprintf(id,sizeof(id),"%s",c.id.c_str());BeginRow("Column ID");if(ImGui::InputText("##column-id",id,sizeof(id))){c.id=id;changed=true;}EndRow();
            ImGui::TextDisabled("Source: %s",c.quantity=="C" || c.quantity=="G" || c.quantity=="Vbi"?"C-V / explicit AC sample":c.quantity=="profile"?"Band / explicit profile sample":"I-V");
            if(std::isfinite(c.voltage_V))changed|=Number((c.quantity+" @ bias [V]").c_str(),c.voltage_V,"voltage_V");if(std::isfinite(c.frequency_Hz))changed|=Number((c.quantity+" @ f [Hz]").c_str(),c.frequency_Hz,"frequency_Hz");
            if(c.quantity=="profile"){char field[80];std::snprintf(field,sizeof(field),"%s",c.field.c_str());BeginRow("Profile field");if(ImGui::InputText("##field",field,sizeof(field))){c.field=field;changed=true;}EndRow();changed|=Number("Position [m]",c.x_m,"x_m");}
            if(ImGui::Button("Remove column"))removeColumn=i;ImGui::PopID();
        }
        if(removeColumn){columns.erase(columns.begin()+*removeColumn);changed=true;}
        std::vector<double> frequencies;for(const auto& c:columns)if(std::isfinite(c.frequency_Hz) && std::find(frequencies.begin(),frequencies.end(),c.frequency_Hz)==frequencies.end())frequencies.push_back(c.frequency_Hz);spec.frequencies_Hz=std::move(frequencies);
        if(changed)Do(Cmd::SetBatchSettings::Make(CreateRef<const BatchDraft>(std::move(spec)),CreateRef<const RecorderDraft>(std::move(recorder))));ImGui::TreePop();
    }
    ImGui::BeginDisabled(d.batchResult.empty() && d.selectedRun.empty());if(ImGui::Button("Extract from stored data"))Do(Cmd::RecordBatch::With());ImGui::SameLine();
    if(ImGui::Button("Calculate missing..."))ImGui::OpenPopup("Calculate missing plan");
    if(ImGui::BeginPopup("Calculate missing plan")) {
        ImGui::TextWrapped("Run the original included Actions on the original snapshot, then acquire requested J/profile biases, C/G bias-frequency pairs and photovoltaic metrics; Vbi uses the C-V range. Creates a linked Results root. Current editor values are ignored.");
        for(const auto& c:columns)ImGui::Text("%s [%s]: %s, V=%.7g, f=%.7g Hz",c.id.c_str(),c.unit.c_str(),c.quantity.c_str(),c.voltage_V,c.frequency_Hz);
        if(ImGui::Button("Run on original snapshot")){Do(Cmd::RecordBatch::With({{"calculate_missing",true}}));ImGui::CloseCurrentPopup();}ImGui::EndPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Export all CSV...")) {if(const auto target=SaveFileDialog({"Recorder CSV","csv"},"scapslab-batch.csv"))Do(Cmd::ExportBatch::With({{"path",target->Utf8()}}));}ImGui::SameLine();
    if(ImGui::Button("Send to PlotLab...")) {if(const auto target=SaveFileDialog({"PlotLab workbook","plotlab"},"scapslab-batch.plotlab"))Do(Cmd::SendToPlotLab::With({{"path",target->Utf8()}}));}ImGui::EndDisabled();ImGui::EndDisabled();
    ImGui::TextWrapped("%s",w->status.c_str());
    if(d.batchResult.empty()) {ImGui::TextUnformatted("Run a batch to populate the worksheet.");return;}
    if(d.batchResult.typed()->runPlan) {
        const auto& table=*d.batchResult.typed();
        if(ImGui::BeginTable("Extracted Recorder",int(table.recorderDraft->columns.size()+1),ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,220))) {
            ImGui::TableSetupColumn("Member");for(const auto& c:table.recorderDraft->columns)ImGui::TableSetupColumn((c.id+" ["+c.unit+"]").c_str());ImGui::TableHeadersRow();
            for(const auto& row:table.rows){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(row->id.c_str());for(const auto& c:table.recorderDraft->columns){ImGui::TableNextColumn();const auto found=row->values.find(c.id);if(found!=row->values.end()){if(std::isfinite(found->second.value))ImGui::Text("%.7g",found->second.value);else ImGui::TextUnformatted("--");ImGui::TextDisabled("%s",found->second.status.c_str());}}}ImGui::EndTable();
        }
        return;
    }
    const auto &table=d.batchResult.typed()->Schema();const auto &rows=d.batchResult.typed()->rows;
    if(ImGui::BeginTable("Recorder worksheet",int(table.at("columns").size()+4+table.at("spec").at("axes").size()),ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg,ImVec2(0,200))) {
        for(const char *h:{"Member","Status / error","Time [s]","Attempt"})ImGui::TableSetupColumn(h);
        for(const auto &a:table.at("spec").at("axes"))ImGui::TableSetupColumn((a.at("path").get<std::string>()+" ["+a.at("unit").get<std::string>()+"]").c_str());
        for(const auto &c:table.at("columns"))ImGui::TableSetupColumn((c.at("id").get<std::string>()+" ["+c.at("unit").get<std::string>()+"]").c_str());ImGui::TableSetupScrollFreeze(1,1);ImGui::TableHeadersRow();
        for(std::size_t i=0;i<rows.size();++i) {const auto &r=*rows[i];ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableNextColumn();if(ImGui::Selectable(r.id.c_str(),d.selectedMember==int(i)))Do(Cmd::SelectBatchMember::With({{"index",i}}));ImGui::TableNextColumn();ImGui::TextWrapped("%s %s %s",r.status.c_str(),r.error_type.c_str(),r.message.c_str());ImGui::TableNextColumn();ImGui::Text("%.3g",r.elapsed_s);ImGui::TableNextColumn();ImGui::Text("%d",r.attempt);for(const auto &a:table.at("spec").at("axes")) {ImGui::TableNextColumn();ImGui::Text("%.6g",ParameterNumber(r,a.at("path").get<std::string>()));}for(const auto &c:table.at("columns")) {ImGui::TableNextColumn();const auto id=c.at("id").get<std::string>();const auto &v=r.values.at(id);if(std::isfinite(v.value))ImGui::Text("%.6g",v.value);else ImGui::TextUnformatted("--");ImGui::TextWrapped("%s",v.status.c_str());}ImGui::PopID();}ImGui::EndTable();
    }
    if(table.at("columns").empty())return;recorderPlot=w->plotSession.settings.recorderPlot;recorderPlot=std::clamp(recorderPlot,0,int(table.at("columns").size())-1);BeginRow("Plot scalar");if(ImGui::BeginCombo("##quantity",table.at("columns")[recorderPlot].at("id").get<std::string>().c_str())) {for(int i=0;i<int(table.at("columns").size());++i)if(ImGui::Selectable(table.at("columns")[i].at("id").get<std::string>().c_str(),i==recorderPlot))recorderPlot=i;ImGui::EndCombo();}EndRow();
    w->plotSession.settings.recorderPlot=recorderPlot;
    const auto &c=table.at("columns")[recorderPlot];const auto &axes=table.at("spec").at("axes");const auto path=axes[0].at("path").get<std::string>();
    const bool nested=table.at("spec").value("mode",std::string("nested"))=="nested" && axes.size()==2;
    std::map<double,std::vector<std::pair<double,double>>> groups;
    for(const auto &row:rows) {const auto &r=*row;const double group=nested?ParameterNumber(r,axes[1].at("path").get<std::string>()):0.;const auto &v=r.values.at(c.at("id").get<std::string>());groups[group].push_back({ParameterNumber(r,path),v.status=="ok"?v.value:NAN});}
    if(groups.empty())return;
    const bool compact=CompactBatchLegend(groups.size());const double lo=groups.begin()->first,hi=groups.rbegin()->first;
    if(compact && w->plotSession.settings.plots["Recorder vs parameter"].legend.value_or(true))BatchColourScale((axes[1].at("path").get<std::string>()+" ["+axes[1].at("unit").get<std::string>()+"]").c_str(),lo,hi);
    if(BeginSavedPlot(*w,"Recorder vs parameter",ImVec2(-1,std::max(220.f,ImGui::GetContentRegionAvail().y-5)))) {ImPlot::SetupLegend(ImPlotLocation_NorthWest);ImPlot::SetupAxes((path+" ["+axes[0].at("unit").get<std::string>()+"]").c_str(),(c.at("id").get<std::string>()+" ["+c.at("unit").get<std::string>()+"]").c_str(),ImPlotAxisFlags_None,ImPlotAxisFlags_None);if(axes[0].value("spacing",std::string{})=="log")ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        SetupSavedPlot(w->plotSession,"Recorder vs parameter",compact);
        for(auto &[group,points]:groups) {std::sort(points.begin(),points.end(),[](auto a,auto b){return a.first<b.first;});std::vector<double>x,y;for(auto [a,b]:points){x.push_back(a);y.push_back(b);}const std::string label=nested?ParameterLabel(axes[1].at("path").get<std::string>(),axes[1].at("unit"),group):c.at("id").get<std::string>();ImPlotSpec style(ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,3);if(compact)style.SetProp(ImPlotProp_LineColor,ImPlot::SampleColormap(float(hi>lo?(group-lo)/(hi-lo):.5),ImPlotColormap_Viridis));ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),style);}EndSavedPlot(*w,"Recorder vs parameter");}
}
namespace {
bool BatchAxes(Workflow& w,const char* id,bool iv) {
    auto& s=w.plotSession.settings;auto& v=s.plots[id];bool changed=false;
    const auto checkbox=[&](const char* label,bool& value){ImGui::TextUnformatted(label);ImGui::SameLine();const bool edit=ImGui::Checkbox((std::string("##")+label).c_str(),&value);ImGui::SameLine();return edit;};
    bool x=v.log[ImAxis_X1].value_or(false),y=v.log[ImAxis_Y1].value_or(false),absolute=s.batchAbsolute.value_or(false);
    if(checkbox("Log X",x)){v.log[ImAxis_X1]=x;changed=true;}if(checkbox("Log Y",y)){v.log[ImAxis_Y1]=y;changed=true;}
    if(iv && checkbox("Log |J|",absolute)){s.batchAbsolute=absolute;v.log[ImAxis_Y1]=absolute;changed=true;}ImGui::NewLine();
    if(changed){++w.plotSession.revision;w.projectDirty=true;w.RefreshDirty();}
    return absolute;
}
}
void Panel::ResultsBrowser() {
    auto& d=w->Current();const auto history=d.history.share();
    if(!d.liveMembers.empty() || (d.selectedRun.empty() && !d.batchResult.empty() && !d.batchResult.typed()->runPlan)){BatchPlots();return;}
    BeginRow("Stored Run");
    if(ImGui::BeginCombo("##stored-root",d.selectedRun.empty()?"Select an explicit Run":d.selectedRun.c_str())) {
        for(const auto& device:w->project.devices) {
            std::set<std::string> seen;
            for(const auto& entry:device->history) {
                const auto id=entry.at("run_id").get<std::string>(),group=entry.value("group_id",std::string{});
                if(!group.empty() && !seen.insert(group).second)continue;
                const auto label=device->name+" / "+(entry.value("legacy",false)?"Legacy runs / ":"")+RunLabel(entry)+(entry.value("legacy",false)&&!group.empty()?" (partial history)":"")+"##"+device->id+id;
                if(ImGui::Selectable(label.c_str(),device->id==d.id && id==d.selectedRun)) {Do(Cmd::SelectDevice::Make(device->id));Do(Cmd::SelectRun::Make(device->id,id));}
            }
        }
        ImGui::EndCombo();
    }EndRow();
    if(d.selectedRun.empty()) {ImGui::TextUnformatted("Results contains explicit Single shot, Quick batch, Batch and Capture live runs. Select a stored Run.");return;}
    const auto entry=std::find_if(history->begin(),history->end(),[&](const auto& r){return r.at("run_id")==d.selectedRun;});
    if(entry==history->end())return;
    const auto stored=d.RunResult(d.selectedRun);const auto table=std::dynamic_pointer_cast<const BatchResult>(stored);
    if(table && !table->runPlan){ImGui::TextUnformatted("Legacy batch results");BatchPlots();return;}
    if(!stored)return;
    ImGui::TextDisabled("STORED / frozen snapshot | %s",d.selectedRun.c_str());
    bool pin=entry->at("pinned");ImGui::BeginDisabled(w->Busy());
    if(Toggle("Pin Run",pin)) {
        const auto group=entry->value("group_id",std::string{});
        for(const auto& r:*history)if(r.at("run_id")==d.selectedRun || (!group.empty() && r.value("group_id",std::string{})==group))Do(Cmd::UpdateRun::With({{"device_id",d.id},{"run_id",r.at("run_id")},{"pinned",pin}}));
    }
    if(ImGui::Button("Delete Run")) {
        Json ids=Json::array();const auto group=entry->value("group_id",std::string{});for(const auto& r:*history)if(r.at("run_id")==d.selectedRun || (!group.empty() && r.value("group_id",std::string{})==group))ids.push_back(r.at("run_id"));
        Do(Cmd::ClearHistory::With({{"run_ids",ids}}));ImGui::EndDisabled();return;
    }
    ImGui::SameLine();if(ImGui::Button("Clear unpinned")){Do(Cmd::ClearHistory::With());ImGui::EndDisabled();return;}
    ImGui::EndDisabled();
    for(int i=0;i<5;++i)if(w->project.ui.value("results_action",std::string("iv"))==ActionId(ActionKind(i)))resultAction=i;
    BeginRow("Action");if(ImGui::Combo("##result-action",&resultAction,"Band / profiles\0I-V\0C-V\0C-f\0QE\0"))w->project.ui["results_action"]=ActionId(ActionKind(resultAction));EndRow();const auto kind=ActionKind(resultAction);
    const auto resolve=[&](const DeviceState& device,const std::string& id,int index,int action,int attempt)->Ref<const ResultBase> {
        auto source=device.RunResult(id);
        if(auto batch=std::dynamic_pointer_cast<const BatchResult>(source);batch && batch->runPlan) {
            if(index<0 || index>=int(batch->actionMembers.size()))return {};
            auto member=batch->Member(index,attempt);
            if(member)for(const auto& [k,result]:member->results)if(int(k)==action)return result;
            return {};
        }
        std::string group;for(const auto& r:device.history)if(r.at("run_id")==id)group=r.value("group_id",std::string{});
        for(const auto& r:device.history)if(r.at("kind")==ActionId(ActionKind(action)) && (r.at("run_id")==id || (!group.empty() && r.value("group_id",std::string{})==group)))return device.RunResult(r.at("run_id"));
        return {};
    };
    if(table) {
        if(table->rows.empty())return;d.selectedMember=std::clamp(d.selectedMember,0,int(table->rows.size())-1);
        if(ImGui::BeginTable("Result members",int(7+table->batchDraft->axes.size()+table->recorderDraft->columns.size()),ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,ImVec2(0,180))) {
            for(const char* h:{"Member","Compare","Status","Error","Time [s]","Attempt","Action"})ImGui::TableSetupColumn(h);
            for(const auto& axis:table->batchDraft->axes)ImGui::TableSetupColumn((axis.path+" ["+axis.unit+"]").c_str());for(const auto& c:table->recorderDraft->columns)ImGui::TableSetupColumn((c.id+" ["+c.unit+"]").c_str());ImGui::TableHeadersRow();
            ImGuiListClipper clipper;clipper.Begin(int(table->rows.size()));while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
                const auto displayed=table->Member(i,i==d.selectedMember?d.selectedAttempt:0);const auto& row=displayed && displayed->summary?*displayed->summary:*table->rows[i];ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableNextColumn();if(ImGui::Selectable(row.id.c_str(),d.selectedMember==i)){d.selectedMember=i;d.selectedAttempt=0;++w->plotGen;}
                ImGui::TableNextColumn();const auto key=std::make_tuple(d.id,d.selectedRun,i,resultAction,row.attempt);bool compare=w->resultCompare.contains(key);if(ImGui::Checkbox("##compare",&compare)){if(compare)w->resultCompare.insert(key);else w->resultCompare.erase(key);}
                ImGui::TableNextColumn();ImGui::TextUnformatted(row.status.c_str());ImGui::TableNextColumn();ImGui::TextWrapped("%s %s",row.error_type.c_str(),row.message.c_str());ImGui::TableNextColumn();ImGui::Text("%.4g",row.elapsed_s);ImGui::TableNextColumn();ImGui::Text("%d",row.attempt);ImGui::TableNextColumn();ImGui::TextUnformatted(ActionId(kind));
                for(const auto& axis:table->batchDraft->axes){ImGui::TableNextColumn();ImGui::Text("%.7g",ParameterNumber(row,axis.path));}
                for(const auto& c:table->recorderDraft->columns){ImGui::TableNextColumn();const auto found=row.values.find(c.id);if(found!=row.values.end()){if(std::isfinite(found->second.value))ImGui::Text("%.7g",found->second.value);else ImGui::TextUnformatted("--");ImGui::TextDisabled("%s",found->second.status.c_str());}}ImGui::PopID();
            }ImGui::EndTable();
        }
        const auto& row=*table->rows[d.selectedMember];BeginRow("Attempt");int attempt=d.selectedAttempt?d.selectedAttempt:row.attempt;if(BoundIndex("##attempt",attempt,"selection.attempt_index",row.attempt)){d.selectedAttempt=attempt;++w->plotGen;}EndRow();
        ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Retry selected member"))Do(Cmd::RetryBatchMember::With({{"member_id",row.id}}));ImGui::EndDisabled();
    } else {
        ImGui::TextUnformatted("Member: point_0 | attempt 1");d.selectedMember=0;
    }
    const int attempt=table?(d.selectedAttempt?d.selectedAttempt:table->rows[d.selectedMember]->attempt):1;
    const auto key=std::make_tuple(d.id,d.selectedRun,d.selectedMember,resultAction,attempt);bool compare=w->resultCompare.contains(key);
    if(Toggle("Compare selected Action / attempt",compare)){if(compare)w->resultCompare.insert(key);else w->resultCompare.erase(key);}
    const auto selected=resolve(d,d.selectedRun,d.selectedMember,resultAction,attempt);
    if(ImGui::BeginTabBar("Results views")) {
        if(ImGui::BeginTabItem("Curves")) {
            if(!selected)ImGui::TextUnformatted("Action not sampled or curves not stored. Use Recorder / Calculate missing for an explicit new run.");
            const bool absolute=BatchAxes(*w,"Stored Actions",kind==ActionKind::Iv || kind==ActionKind::Band);
            const auto draw=[&](const Ref<const ResultBase>& result,const std::string& label,const ImVec4& colour,float weight) {
                if(auto iv=std::dynamic_pointer_cast<const IvResult>(result)){const auto& c=IvCols(*iv,absolute);ImPlot::PlotLine(label.c_str(),c.a.data(),c.b.data(),int(c.a.size()),{ImPlotProp_LineColor,colour,ImPlotProp_LineWeight,weight});}
                else if(auto ac=std::dynamic_pointer_cast<const AcResult>(result)) {
                    if(kind==ActionKind::Qe){const auto& c=Derived(ac->points,405,[&](Cols& c){for(const auto& p:ac->points){c.a.push_back(p.wavelength_nm);c.b.push_back(p.valid?p.EQE:NAN);}});ImPlot::PlotLine(label.c_str(),c.a.data(),c.b.data(),int(c.a.size()),{ImPlotProp_LineColor,colour,ImPlotProp_LineWeight,weight});}
                    else {const auto& c=AcCols(ac->points,kind==ActionKind::Cf?8:6);ImPlot::PlotLine(label.c_str(),c.a.data(),c.b.data(),int(c.a.size()),{ImPlotProp_LineColor,colour,ImPlotProp_LineWeight,weight});}
                }
            };
            if(BeginSavedPlot(*w,"Stored Actions",ImVec2(-1,std::max(200.f,ImGui::GetContentRegionAvail().y)))) {
                ImPlot::SetupAxes(kind==ActionKind::Cf?"Frequency [Hz]":kind==ActionKind::Qe?"Wavelength [nm]":"Voltage [V]",kind==ActionKind::Cv || kind==ActionKind::Cf?"C [F]":kind==ActionKind::Qe?"EQE [1]":"J [A/m^2]");SetupSavedPlot(w->plotSession,"Stored Actions");
                for(const auto& [deviceId,runId,index,action,oldAttempt]:w->resultCompare)if(action==resultAction)for(const auto& device:w->project.devices)if(device->id==deviceId)if(const auto result=resolve(*device,runId,index,action,oldAttempt))draw(result,device->name+" / "+runId+" / "+std::to_string(index)+" / attempt "+std::to_string(oldAttempt),ImVec4(.5f,.6f,.8f,.7f),1.4f);
                if(selected)draw(selected,"Selected member / attempt "+std::to_string(attempt),ImVec4(1.f,.65f,.2f,1.f),3.f);EndSavedPlot(*w,"Stored Actions");
            }ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Scalar")) {
            if(table && !table->recorderDraft->columns.empty()) {
                recorderPlot=std::clamp(recorderPlot,0,int(table->recorderDraft->columns.size())-1);BeginRow("Quantity");if(ImGui::BeginCombo("##scalar",table->recorderDraft->columns[recorderPlot].id.c_str())){for(int i=0;i<int(table->recorderDraft->columns.size());++i)if(ImGui::Selectable(table->recorderDraft->columns[i].id.c_str(),i==recorderPlot))recorderPlot=i;ImGui::EndCombo();}EndRow();
                const auto& c=table->recorderDraft->columns[recorderPlot];const auto& axis=table->batchDraft->axes.front();const bool nested=table->batchDraft->mode=="nested" && table->batchDraft->axes.size()==2;
                std::set<double> groups{0};if(nested){groups.clear();for(const auto& row:table->rows)groups.insert(ParameterNumber(*row,table->batchDraft->axes[1].path));}
                if(BeginSavedPlot(*w,"Stored scalars",ImVec2(-1,-1))){ImPlot::SetupAxes((axis.path+" ["+axis.unit+"]").c_str(),(c.id+" ["+c.unit+"]").c_str());SetupSavedPlot(w->plotSession,"Stored scalars");for(double group:groups){const auto& values=Derived(table->rows,500+recorderPlot,[&](Cols& values){for(const auto& row:table->rows)if(!nested || ParameterNumber(*row,table->batchDraft->axes[1].path)==group){values.a.push_back(ParameterNumber(*row,axis.path));const auto found=row->values.find(c.id);values.b.push_back(found!=row->values.end() && found->second.status=="ok"?found->second.value:NAN);}},group);ImPlot::PlotScatter((c.id+" / "+std::to_string(group)).c_str(),values.a.data(),values.b.data(),int(values.a.size()));}EndSavedPlot(*w,"Stored scalars");}
            }else ImGui::TextUnformatted("Extract Recorder columns to view scalars.");ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Profiles")) {
            const auto iv=std::dynamic_pointer_cast<const IvResult>(selected);
            if(iv && !iv->points.empty()) {d.selectedBias=std::clamp(d.selectedBias,0,int(iv->points.size())-1);BeginRow("Bias index");BoundIndex("##profile-bias",d.selectedBias,"selection.bias_index",int(iv->points.size())-1);EndRow();const auto& p=iv->points[d.selectedBias];if(p.x_m.empty())ImGui::TextUnformatted("Profiles not stored at this bias. Calculate missing explicitly from Recorder.");else if(BeginSavedPlot(*w,"Stored profiles",ImVec2(-1,-1))){ImPlot::SetupAxes("Position [um]","Energy [eV]");SetupSavedPlot(w->plotSession,"Stored profiles");const auto& x=Vec(p.x_m.get(),1e6);for(const auto* field:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}){const auto& y=PointColumn(p,field).get();if(y.size()==x.size())ImPlot::PlotLine(field,x.data(),y.data(),int(y.size()));}EndSavedPlot(*w,"Stored profiles");}}
            else ImGui::TextUnformatted("Select Band or I-V with stored profiles.");ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Recorder")){Recorder();ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
}
void Panel::BatchPlots() {
    auto &d=w->Current();const bool absolute=BatchAxes(*w,d.liveMembers.empty()?"Batch I-V overlay":"Batch I-V running",true);
    if(!d.liveMembers.empty()) { // members finished so far in the running batch: curves only, marked, never stored (the table replaces them when the batch ends)
        ImGui::TextColored(ImVec4(1.f,.6f,.15f,1.f),"RUNNING: %d of %d members finished (partial curves, not saved; the table follows)",int(d.liveMembers.size()),int(d.liveMemberTotal));
        const bool compact=CompactBatchLegend(d.liveMemberTotal);
        const auto& running=d.liveBatch?*d.liveBatch:*d.batch;const auto &axis=running.axes[0];
        const auto samples=BatchAxisValues(axis);const int count=int(samples.size());
        const auto valueAt=[&](int i){return samples[i];};
        double lo=INFINITY,hi=-INFINITY;for(double v:samples){lo=std::min(lo,v);hi=std::max(hi,v);}
        const auto stride=running.mode=="nested"?std::max(std::size_t(1),d.liveMemberTotal/std::max(1,count)):std::size_t(1);
        const auto colourLabel=(axis.path.empty()?axis.parameter_id:axis.path)+(axis.unit.empty()?"":" ["+axis.unit+"]");
        if(compact && w->plotSession.settings.plots["Batch I-V running"].legend.value_or(true))BatchColourScale(colourLabel.c_str(),lo,hi);
        if(BeginSavedPlot(*w,"Batch I-V running",ImVec2(-1,-1))) {
            ImPlot::SetupLegend(ImPlotLocation_NorthWest);ImPlot::SetupAxes("Voltage [V] (left minus right)","J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);SetupSavedPlot(w->plotSession,"Batch I-V running",compact);
            for(const auto &m:d.liveMembers) {const auto name="member "+std::to_string(m.index+1)+" (running)##live"+std::to_string(m.index);const double v=valueAt(std::clamp(int(std::size_t(m.index)/stride),0,std::max(0,count-1)));const auto colour=ImPlot::SampleColormap(float(hi>lo?(v-lo)/(hi-lo):.5),ImPlotColormap_Viridis);ImPlotSpec style(ImPlotProp_LineWeight,m.index==d.selectedMember?3.f:1.5f,ImPlotProp_LineColor,colour);ImPlot::PlotLine(name.c_str(),m.v.get().data(),(absolute?Vec(m.j.get(),1.,4):m.j.get()).data(),int(std::min(m.v.size(),m.j.size())),style);}
            PlotBadge("RUNNING (partial)",IM_COL32(255,140,25,230),IM_COL32(0,0,0,255),0);
            EndSavedPlot(*w,"Batch I-V running");
        }
        return;
    }
    if(d.batchResult.empty()) {ImGui::TextUnformatted("Run a batch to overlay its curves.");return;}
    const auto result=d.batchResult.typed();const auto& table=result->Schema();const auto& rows=result->rows;
    if(rows.empty())return;
    d.selectedMember=std::clamp(d.selectedMember,0,int(rows.size())-1);
    BeginRow("Selected member");if(ImGui::BeginCombo("##member",MemberLabel(*rows[d.selectedMember],table.at("spec")).c_str())) {for(int i=0;i<int(rows.size());++i)if(ImGui::Selectable((MemberLabel(*rows[i],table.at("spec"))+" | "+rows[i]->status).c_str(),i==d.selectedMember))Do(Cmd::SelectBatchMember::With({{"index",i}}));ImGui::EndCombo();}EndRow();
    const auto &selected=*rows[d.selectedMember];ImGui::TextWrapped("%s | %s",selected.status.c_str(),selected.message.c_str());
    ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Retry selected"))Do(Cmd::RetryBatchMember::With({{"member_id",selected.id}}));ImGui::SameLine();if(ImGui::Button("Export selected CSV..."))if(const auto target=SaveFileDialog({"Recorder CSV","csv"},"scapslab-member.csv"))Do(Cmd::ExportBatch::With({{"member_id",selected.id},{"path",target->Utf8()}}));ImGui::EndDisabled();
    const auto path=table.at("spec").at("axes")[0].at("path").get<std::string>();
    ImGui::TextWrapped("Colour: %s [%s]. Click near a curve sample to select. Failed members remain listed.",path.c_str(),table.at("spec").at("axes")[0].at("unit").get<std::string>().c_str());
    if(batchPlotSource.lock()!=result){batchPlotSource=result;batchPlotCurves=BatchIvCurves(*result);}
    double lo=INFINITY,hi=-INFINITY;for(const auto& row:rows){const double v=ParameterNumber(*row,path);lo=std::min(lo,v);hi=std::max(hi,v);}
    const bool compact=CompactBatchLegend(rows.size());
    if(compact && w->plotSession.settings.plots["Batch I-V overlay"].legend.value_or(true))BatchColourScale((path+" ["+table.at("spec").at("axes")[0].at("unit").get<std::string>()+"]").c_str(),lo,hi);
    if(BeginSavedPlot(*w,"Batch I-V overlay",ImVec2(-1,-1))) {ImPlot::SetupLegend(ImPlotLocation_NorthWest);ImPlot::SetupAxes("Voltage [V] (left minus right)","J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);SetupSavedPlot(w->plotSession,"Batch I-V overlay",compact);double nearest=64;int hit=-1;
        for(int i=0;i<int(rows.size());++i) {
            const auto& r=*rows[i];const auto& curve=batchPlotCurves[i];
            if(ImPlot::IsPlotHovered())for(std::size_t j=0;j<curve.v.size();++j)if(std::isfinite(curve.j[j])) {const auto pixel=ImPlot::PlotToPixels(curve.v[j],absolute?std::abs(curve.j[j]):curve.j[j]);const auto mouse=ImGui::GetMousePos();const double dist=(pixel.x-mouse.x)*(pixel.x-mouse.x)+(pixel.y-mouse.y)*(pixel.y-mouse.y);if(dist<nearest){nearest=dist;hit=i;}}
            const double v=ParameterNumber(r,path);const auto colour=ImPlot::SampleColormap(float(hi>lo?(v-lo)/(hi-lo):.5),ImPlotColormap_Viridis);
            ImPlotSpec style(ImPlotProp_LineColor,colour,ImPlotProp_LineWeight,i==d.selectedMember?3.f:1.5f);ImPlot::PlotLine(MemberLabel(r,table.at("spec")).c_str(),curve.v.data(),(absolute?Vec(curve.j,1.,4):curve.j).data(),int(curve.v.size()),style);
        }
        if(hit>=0 && ImGui::IsMouseClicked(0) && !w->Busy())Do(Cmd::SelectBatchMember::With({{"index",hit}}));EndSavedPlot(*w,"Batch I-V overlay");}

}
}

namespace SST::ScapsLab {
void Panel::GenerationRecombination() {
    grFollow=w->plotSession.settings.grFollow;grHidden=w->plotSession.settings.grHidden;grBias=Json{{"voltage_V",w->plotSession.settings.grVoltage}};
    const int savedTab=w->plotSession.settings.grTab;const bool restoreTab=grTab!=savedTab;
    auto &device=w->Current();const auto ivRef=device.ProfileSweep();const auto& iv=*ivRef;
    StructureEdit();
    ShotBar(*w,"band");
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Follow I-V working point");ImGui::SameLine();
    const bool followToggled=ImGui::Checkbox("###Follow I-V working point",&grFollow);TrackGesture();
    if(followToggled && grFollow && !iv.points.empty())Do(Cmd::SelectBias::Make(std::clamp(device.selectedBias,0,int(iv.points.size())-1)));
    if(followToggled && !grFollow) {
        if(!iv.points.empty()) device.biasVoltage_V=iv.points[std::clamp(device.selectedBias,0,int(iv.points.size())-1)].voltage_V;
        grBias["voltage_V"]=device.biasVoltage_V;device.needsBand=true;device.changedAt=std::chrono::steady_clock::now();
    }
    if(grFollow && !iv.points.empty()) {
        ImGui::SameLine();ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x));
        {const auto &pts=iv.points;const int sel=std::clamp(device.selectedBias,0,int(pts.size())-1);char fmt[64];std::snprintf(fmt,sizeof(fmt),"%.4g V  (#%%d)",pts[sel].voltage_V);
        int bias=sel;if(BoundIndex("##point",bias,"selection.bias_index",int(pts.size())-1,fmt))Do(Cmd::SelectBias::Make(bias));}
    }
    if(!grFollow) {
        grBias["voltage_V"]=device.biasVoltage_V;
        const bool changed=Number("Working voltage [V]",grBias,"voltage_V");
        if(changed) {device.biasVoltage_V=grBias.at("voltage_V");device.needsBand=true;device.changedAt=std::chrono::steady_clock::now();}
        if(!w->Busy() && device.needsBand && !ImGui::IsAnyItemActive() && std::chrono::steady_clock::now()-device.changedAt>=std::chrono::milliseconds(250))
            Do(Cmd::RecalculateLive::Make(ActionKind::Band));
        const bool hasVoc=iv.photovoltaic && std::isfinite(iv.photovoltaic->Voc_V);
        ImGui::BeginDisabled(w->Busy() || !hasVoc);
        if(ImGui::Button("Voc")) {grBias["voltage_V"]=-iv.photovoltaic->Voc_V;device.biasVoltage_V=grBias.at("voltage_V");Do(Cmd::RecalculateLive::Make(ActionKind::Band));}
        ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Requires an idle job and a valid refined Voc from a light I-V sweep.");
        ImGui::SameLine();
        const bool hasVmp=!iv.points.empty() && std::any_of(iv.points.begin(),iv.points.end(),[](const IvPoint &p){return p.valid && std::isfinite(p.J_A_m2);});
        ImGui::BeginDisabled(w->Busy() || !hasVmp);
        if(ImGui::Button("Vmp")) {
            double best=-1;for(const auto &p:iv.points) if(p.valid && std::isfinite(p.J_A_m2)) {
                const double power=-p.voltage_V*p.J_A_m2;
                if(power>best) {best=power;grBias["voltage_V"]=p.voltage_V;}
            }
            device.biasVoltage_V=grBias.at("voltage_V");Do(Cmd::RecalculateLive::Make(ActionKind::Band));
        }
        ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Requires an idle job and valid I-V points. Selects the sampled point with maximum delivered power.");
    }
    w->plotSession.settings.grFollow=grFollow;w->plotSession.settings.grVoltage=grBias.at("voltage_V").get<double>();
    const double selectedVoltage=iv.points.empty()?NAN:iv.points[std::clamp(device.selectedBias,0,int(iv.points.size())-1)].voltage_V;
    const auto bandRef=device.ProfileBand();
    const bool selectedBand=grFollow && !bandRef->points.empty() && bandRef->points.front().voltage_V==selectedVoltage;
    const auto &snapshot=*(grFollow && !selectedBand?ivRef:bandRef);
    if(snapshot.points.empty()) {ImGui::TextWrapped("Calculate I-V or select a working voltage. %s",w->status.c_str());return;}
    const int index=grFollow && !selectedBand?std::clamp(device.selectedBias,0,int(snapshot.points.size())-1):0;
    const IvPoint* drawnGr=&snapshot.points[index];
    // Keep the last profiles on screen while the new bias loads (owner 2026-11-23: no blanking on bias change).
    if(drawnGr->details) ImGui::TextDisabled("Details at %.4g V",drawnGr->voltage_V);
    else if(!bandRef->points.empty() && bandRef->points.front().details) {drawnGr=&bandRef->points.front();ImGui::TextDisabled("Loading details at %.4g V ...",selectedVoltage);}
    else if(const auto* nearest=NearestStoredProfiles(*w,snapshot.points,index)) drawnGr=nearest;
    else if(MissingProfiles(*w,*drawnGr))return;
    const auto &point=*drawnGr;
    const auto &gr=point.details->generation_recombination;
    const auto before=device.changeBand?device.changeBand:device.changeIv;
    const bool grIsReference=w->liveChangeReference!=3 && before && std::any_of(before->points.begin(),before->points.end(),[&](const auto& p){return &p==&point;});
    if(ImGui::CollapsingHeader("Details")) {
    ImGui::TextWrapped("%s | terminal %.6g V | internal %.6g V | %.5g K | solver %s | profile validity %s",point.bias_id.c_str(),point.terminal_voltage_V,point.internal_voltage_V,snapshot.meta.prim.at("project_snapshot").at("temperature_K").get<double>(),point.status.c_str(),point.validity_status.c_str());
    ImGui::TextWrapped("%s | illumination %s | %s",device.id.c_str(),snapshot.meta.prim.at("project_snapshot").value("illumination",Json::object()).value("enabled",false)?"on":"off",w->status.c_str());
    ImGui::TextWrapped("Terminal budget: %s | relative error %.5g",gr.terminal_budget_valid?"valid <= 1e-6":"NOT VALID",gr.terminal_budget_relative_error);
    for(const auto &c:gr.channels) ImGui::TextWrapped("%s / %s: %s",c.mechanism.c_str(),c.id.c_str(),c.validity_status.c_str());
    ImGui::TextWrapped("Log plot shows positive rates only; signed rates and integrals are retained in export. Sheets use the right axis [m^-2 s^-1].");
    }
    const bool valid=point.valid;
    const bool budgetShot=gallery && std::getenv("SCAPSLAB_GR_TAB") && std::string(std::getenv("SCAPSLAB_GR_TAB"))=="1";
    const auto& style=ImGui::GetStyle();float toolbarWidth=16;
    for(const auto* label:{"Generation","srh_bulk","interface","radiative","auger"})toolbarWidth+=ImGui::CalcTextSize(label).x+ImGui::GetFrameHeight()+style.ItemInnerSpacing.x+style.ItemSpacing.x;
    for(const auto* label:{"Profiles","Integrated currents","Export G/R csv","Export G/R json"})toolbarWidth+=ImGui::CalcTextSize(label).x+2*style.FramePadding.x+2*style.ItemInnerSpacing.x;
    const float toolbarHeight=ImGui::GetFrameHeight()+2*style.ItemSpacing.y+(toolbarWidth>ImGui::GetContentRegionAvail().x?style.ScrollbarSize:0);
    ImGui::SetNextWindowContentSize({toolbarWidth,0});
    ImGui::BeginChild("G/R controls",ImVec2(0,toolbarHeight),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
    for(const auto* mechanism:{"generation","srh_bulk","interface","radiative","auger"}) {
        ImGui::PushID(mechanism);ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(std::string_view(mechanism)=="generation"?"Generation":mechanism);ImGui::SameLine(0,style.ItemInnerSpacing.x);
        bool show=!grHidden.contains(mechanism);if(ImGui::Checkbox("##visible",&show)){if(show)grHidden.erase(mechanism);else grHidden.insert(mechanism);}ImGui::PopID();ImGui::SameLine();
    }
    if(ImGui::BeginTabBar("G/R views")) {
        ImGui::BeginDisabled(w->Busy());
        for(const auto* format:{"csv","json"})if(ImGui::TabItemButton((std::string("Export G/R ")+format).c_str(),ImGuiTabItemFlags_Trailing)) {
            try {if(const auto path=SaveFileDialog({"G/R results",format},(std::string("generation-recombination.")+format).c_str()))Do(Cmd::ExportGr::With({{"path",path->Utf8()},{"format",format},{"source",grFollow?"iv":"band"},{"point_index",index}}));}
            catch(const std::exception& e){w->status=e.what();}
        }
        ImGui::EndDisabled();
        if(ImGui::BeginTabItem("Profiles",nullptr,(gallery && !budgetShot) || (restoreTab && savedTab==0)?ImGuiTabItemFlags_SetSelected:0)){grTab=0;ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Integrated currents",nullptr,budgetShot || (restoreTab && savedTab==1)?ImGuiTabItemFlags_SetSelected:0)){grTab=1;ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
    ImGui::EndChild();w->plotSession.settings.grTab=grTab;w->plotSession.settings.grHidden=grHidden;
    const bool showGeneration=!grHidden.contains("generation");
    if(grTab==0) {
    if(!valid) ImGui::TextWrapped("Invalid working point: profiles are masked; raw values remain available in export.");
    // Log Y fitting uses visible series in EndSavedPlot; enable the right axis only for sheet channels.
    double sheetMax=0;
    if(valid) {
        for(const auto &c:gr.channels) if(!grHidden.contains(c.mechanism) && c.valid) {
            for(double v:c.R_sheet_m2_s.get())if(std::isfinite(v))sheetMax=std::max(sheetMax,v);
        }
    }
    const double totalUm=[&]{double t=0;for(const auto &l:snapshot.meta.prim.at("project_snapshot").at("layers"))t+=l.at("thickness_m").get<double>()*1e6;return t;}();
    if(BeginSavedPlot(*w,"G(x), R(x)",ImVec2(-1,-1))) {
        ImPlot::SetupAxes("Position [um]","Bulk rate [m^-3 s^-1]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        FitSavedAxis(w->plotSession,"G(x), R(x)",ImAxis_X1,0,totalUm);
        if(sheetMax>0){ImPlot::SetupAxis(ImAxis_Y2,"Sheet rate [m^-2 s^-1]",ImPlotAxisFlags_AuxDefault);ImPlot::SetupAxisScale(ImAxis_Y2,ImPlotScale_Log10);}
        SetupSavedPlot(w->plotSession,"G(x), R(x)");
        auto plot=[&](const std::string &name,const Column &xx,const Column &yy,bool sheet,bool curveValid,int shade=0) {
            const auto &x=Vec(xx.get(),1e6),&y=Vec(yy.get(),1.,curveValid?2:3);if(x.size()!=y.size())return;
            if(sheet && sheetMax<=0)return;ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);
            const auto mech=name.substr(0,name.find(' '));const ImPlotSpec style(ImPlotProp_LineColor,XColour(mech,shade),ImPlotProp_LineWeight,2.f,ImPlotProp_MarkerFillColor,XColour(mech,shade),ImPlotProp_MarkerLineColor,XColour(mech,shade));
            if(sheet)ImPlot::PlotScatter(name.c_str(),x.data(),y.data(),int(x.size()),style);else ImPlot::PlotLine(name.c_str(),x.data(),y.data(),int(x.size()),style);
        };
        if(showGeneration)HistoryProfiles(*w,"generation_m3_s",1.,true);
        if(showGeneration && !grIsReference && !Archived(device,snapshot,grFollow?"iv":"band"))plot("G",gr.x_m,gr.generation_m3_s,false,valid);
        for(const auto &run:device.history) {
            if(!run.at("visible").get<bool>() || !ShowRun(run) || run.at("run_id")==device.selectedRun || (run.at("kind")!="iv" && run.at("kind")!="band"))continue;
            const auto history=std::dynamic_pointer_cast<const IvResult>(HistoryTyped(device,run));if(!history || history->points.empty())continue;const auto &points=history->points;
            const auto &p=points[std::clamp(device.selectedBias,0,int(points.size())-1)];
            if(!p.details)continue;
            const auto colour=run.at("colour").get<std::vector<float>>();const bool selectedRun=run.at("run_id")==device.selectedRun;std::map<std::string,int> shadeOf;
            for(const auto &channel:p.details->generation_recombination.channels) {
                if(grHidden.contains(channel.mechanism))continue;
                const bool sheet=!channel.R_sheet_m2_s.empty();
                const auto &x=Vec(channel.x_m.get(),1e6),&y=Vec((sheet?channel.R_sheet_m2_s:channel.R_m3_s).get(),1.,channel.valid?1:3);if(x.size()!=y.size())continue;
                const auto label=channel.mechanism+" / "+channel.id+" / "+RunLabel(run)+"##"+run.at("run_id").get<std::string>();
                if(sheet && sheetMax<=0)continue;ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);
                const auto mechName=channel.mechanism;const ImVec4 lc=selectedRun?XColour(mechName,shadeOf[mechName]++):ImVec4(colour[0],colour[1],colour[2],colour[3]);ImPlotSpec style(ImPlotProp_LineColor,lc,ImPlotProp_LineWeight,selectedRun?2.5f:1.f,ImPlotProp_MarkerFillColor,lc,ImPlotProp_MarkerLineColor,lc);
                if(sheet)ImPlot::PlotScatter(label.c_str(),x.data(),y.data(),int(x.size()),style);else ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),style);
            }
        }
        if(const auto before=device.changeBand?device.changeBand:device.changeIv;before && !before->points.empty()) {
            const auto& p=before->points[std::clamp(device.selectedBias,0,int(before->points.size())-1)];
            if(p.details && p.valid){const auto& g=p.details->generation_recombination;
                ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);if(showGeneration)ReferenceLine(*w,"G",Vec(g.x_m.get(),1e6),Vec(g.generation_m3_s.get(),1.,2),XColour("G"));std::map<std::string,int> shades;
                for(const auto& c:g.channels)if(!grHidden.contains(c.mechanism) && c.valid){const bool sheet=!c.R_sheet_m2_s.empty();if(sheet && sheetMax<=0)continue;ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);ReferenceLine(*w,c.mechanism+" / "+c.id,Vec(c.x_m.get(),1e6),Vec((sheet?c.R_sheet_m2_s:c.R_m3_s).get(),1.,2),XColour(c.mechanism,shades[c.mechanism]++));}
            }
        }
        std::map<std::string,int> curShade;
        for(const auto &c:gr.channels) if(!grIsReference && !Archived(device,snapshot,grFollow?"iv":"band") && !grHidden.contains(c.mechanism)) {
            const bool sheet=!c.R_sheet_m2_s.empty();plot(c.mechanism+" / "+c.id,c.x_m,(sheet?c.R_sheet_m2_s:c.R_m3_s),sheet,c.valid,curShade[c.mechanism]++);
        }
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);DrawLayerRegions(snapshot.meta.prim.at("project_snapshot").at("layers"));
        EndSavedPlot(*w,"G(x), R(x)");
    }
    }
    if(grTab==1) {
    ImGui::TextWrapped("q integral G = %.8g A/m^2 | q integral R = %.8g A/m^2 | net = %.8g A/m^2",gr.generation_current_A_m2,gr.recombination_current_A_m2,gr.net_generation_current_A_m2);
    ImGui::TextWrapped("Terminal budget: %s | relative error %.5g.",gr.terminal_budget_valid?"valid <= 1e-6":"NOT VALID",gr.terminal_budget_relative_error);
    if(gr.has_terminal_budget) {
        for(const auto &c:gr.contact_recombination) ImGui::Text("Contact %s: electron %.8g | hole %.8g A/m^2 (signed removal)",c.id.c_str(),c.electron_current_A_m2,c.hole_current_A_m2);
        ImGui::TextWrapped("Shunt %.8g | series %.8g | predicted terminal %.8g A/m^2",gr.shunt_current_A_m2,gr.series_current_A_m2,gr.predicted_terminal_current_A_m2);
        ImGui::TextWrapped("%s","J = G - R - right.electron - left.hole + J_shunt (native current sign)");
    } else ImGui::TextWrapped("Legacy bulk-only budget; recalculate to include contacts and external branches.");
    const float tableHeight=std::clamp(ImGui::GetContentRegionAvail().y-210.f,80.f,150.f);
    if(ImGui::BeginTable("Integrated losses",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,tableHeight))) {
        for(const auto *name:{"Layer / defect ID","Mechanism","J_R [A/m^2]","Profile validity"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
        for(const auto &c:gr.channels) for(const auto &l:c.by_layer) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s / %s",l.layer_id.c_str(),c.id.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(c.mechanism.c_str());ImGui::TableNextColumn();ImGui::Text("%.9g",l.current_A_m2);ImGui::TableNextColumn();ImGui::TextUnformatted(c.validity_status.c_str());
        }
        for(const auto &l:gr.layers) {ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(l.layer_id.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted("TOTAL");ImGui::TableNextColumn();ImGui::Text("%.9g",l.recombination_current_A_m2);}
        ImGui::EndTable();
    }
    ImGui::TextUnformatted("Loss breakdown | signed A/m^2 | sheets assigned to left layer");
    if(BeginSavedPlot(*w,"Integrated loss breakdown",ImVec2(-1,std::max(150.f,ImGui::GetContentRegionAvail().y-4)))) {
        ImPlot::SetupAxes("Mechanism index","J_R [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupLegend(ImPlotLocation_North,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        SetupSavedPlot(w->plotSession,"Integrated loss breakdown");
        const auto reference=device.changeBand?device.changeBand:device.changeIv;
        const GenerationRecombinationResult* old=nullptr;if(reference && !reference->points.empty()){const auto& p=reference->points[std::clamp(device.selectedBias,0,int(reference->points.size())-1)];if(p.details && p.valid)old=&p.details->generation_recombination;}
        int i=0;for(const auto& c:gr.channels){const double x=i,y=valid?c.integrated_current_A_m2:NAN;const auto colour=ImPlot::GetColormapColor(i++);if(!grHidden.contains(c.mechanism)){
            if(old){const auto found=std::find_if(old->channels.begin(),old->channels.end(),[&](const auto& channel){return channel.mechanism==c.mechanism && channel.id==c.id;});if(found!=old->channels.end()){const auto& line=Derived(found->by_layer,730,[&](Cols& cols){cols.a={x-.35,x+.35};cols.b={found->integrated_current_A_m2,found->integrated_current_A_m2};},x);ReferenceLine(*w,c.mechanism+" / "+c.id,line.a,line.b,colour);}}
            if(!grIsReference)ImPlot::PlotBars((c.mechanism+" / "+c.id).c_str(),&x,&y,1,.7,{ImPlotProp_FillColor,colour,ImPlotProp_LineColor,colour});
        }}
        EndSavedPlot(*w,"Integrated loss breakdown");
    }
    }
}
}
