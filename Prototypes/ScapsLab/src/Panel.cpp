#include "Panel.hpp"
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
int AddDefectToDraft(Json &draft,int layer,bool sheet) {
    auto &layers=draft["layers"];auto &defects=sheet?draft["interface_defects"]:layers[layer]["defects"];
    if(!defects.is_array()) defects=Json::array();
    auto d=NewDefect(sheet);unsigned n=1;std::set<std::string> ids;
    if(sheet) for(const auto &v:defects) ids.insert(v.at("id"));
    else for(const auto &l:layers) for(const auto &v:l["defects"]) ids.insert(v.at("scapslab_id"));
    const std::string prefix=sheet?"interface-defect-":"defect-";
    while(ids.contains(prefix+std::to_string(n))) ++n;
    d[sheet?"id":"scapslab_id"]=prefix+std::to_string(n);
    if(sheet) d["left_layer_index"]=layer;else {d["Nt_m3"]=1e20;d["sigma_n_m2"]=1e-19;d["sigma_p_m2"]=1e-19;}
    defects.push_back(d);return int(defects.size())-1;
}
}
namespace {
Workflow *numericWorkflow=nullptr;
Json *numericDocument=nullptr;
void SweepMenu(Json &object,const char *key,double scale=1,const char *title="") {
    if(!numericWorkflow || !numericDocument)return;
    const ImVec2 itemMin=ImGui::GetItemRectMin(),itemMax=ImGui::GetItemRectMax();
    const auto *vp=ImGui::GetWindowViewport();const float popupWidth=380.f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(360,0),ImVec2(FLT_MAX,FLT_MAX));
    ImGui::SetNextWindowPos(ImVec2(std::clamp(itemMin.x,vp->WorkPos.x,std::max(vp->WorkPos.x,vp->WorkPos.x+vp->WorkSize.x-popupWidth)),std::clamp(itemMax.y,vp->WorkPos.y,std::max(vp->WorkPos.y,vp->WorkPos.y+vp->WorkSize.y-230.f))),ImGuiCond_Appearing);
    if(!ImGui::BeginPopupContextItem("Sweep"))return;
    const auto parameters=BatchParameters(*numericDocument);
    for(const auto &p:parameters) {
        const auto pointer=Json::json_pointer(p.at("pointer").get<std::string>());
        if(&numericDocument->at(pointer)!=&object.at(key))continue;
        // Range is edited in the control's display units (scale = display/native); the request stays native.
        ImGui::Text("Sweep: %s",*title?title:p.at("label").get<std::string>().c_str());ImGui::Separator();
        static double delta=1.;static int points=5;static bool logarithmic=false;
        if(ImGui::IsWindowAppearing())delta=std::max(1e-30,std::abs(p.at("value").get<double>())*.1*scale);
        const auto row=[](const char *label){ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::SameLine(150.f);ImGui::SetNextItemWidth(-1);};
        row("Range +/-");ImGui::DragScalar("##delta",ImGuiDataType_Double,&delta,float(std::max(1e-30,delta*.01)),nullptr,nullptr,"%.6g");
        row("Points");ImGui::SliderInt("##points",&points,2,100);
        row("Log spacing");ImGui::Checkbox("##log",&logarithmic);
        Json request={{"operation","quick_sweep"},{"parameter_id",p.at("id")},{"delta_native_units",delta/scale},{"points",points},{"spacing",logarithmic?"log":"linear"}};
        std::string error;try{(void)QuickSweep(*numericDocument,request);}catch(const std::exception &e){error=e.what();}
        if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
        ImGui::BeginDisabled(numericWorkflow->Busy() || !error.empty());
        if(ImGui::Button("Run sweep")){(void)numericWorkflow->Dispatch(request);ImGui::CloseCurrentPopup();}ImGui::EndDisabled();break;
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
const Json &HistoryResult(const DeviceState &device,const Json &run) {
    static const Json none=Json::object();
    const auto typed=HistoryTyped(device,run);
    return typed?typed->View():none; // the view is owned by the (shared) typed result, so the reference stays valid
}
bool Archived(const DeviceState &,const Json &,const std::string &) {return false;} // live curve is always drawn; history draws only pinned shots (and batch runs)
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
const Json *LiveRun(const DeviceState &d,const std::string &kind) {
    const Ref<const ResultBase> res=kind=="iv"?Ref<const ResultBase>(d.result.typed()):Ref<const ResultBase>(d.acResults.get(kind));
    if(!res || res->Provenance().is_null())return nullptr;
    for(auto it=d.history.rbegin();it!=d.history.rend();++it)if(Is(it->at("kind"),kind) && it->at("provenance")==res->Provenance())return &*it;
    return nullptr;
}
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
// Fit an axis to [lo,hi] whenever `key` changes; user zoom/pan is kept otherwise.
void FitWhenChanged(const std::string &plot,ImAxis axis,double key,double lo,double hi) {
    static std::map<std::string,double> last;auto &k=last[plot+std::to_string(int(axis))];
    ImPlot::SetupAxisLimits(axis,lo,hi,k!=key?ImPlotCond_Always:ImPlotCond_Once);k=key;
}
// Live vs pinned: toolbar with "Single shot" (Ctrl+P) and the "Shots" list of the active device.
void ShotBar(Workflow &w,const std::string &kind) {
    ImGui::PushID("shotbar");
    const auto &device=w.Current();const Json *live=LiveRun(device,kind);const std::string deviceId=device.id;const std::string liveId=live?live->at("run_id").get<std::string>():std::string{};const auto history=device.history;
    const bool livePinned=live && live->at("pinned")==true;
    ImGui::BeginDisabled(w.Busy() || !live || livePinned);
    bool shot=ImGui::Button("Single shot");
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Pin the current curve as a frozen overlay (Ctrl+P).\nEdits and sliders only replace the live curve.");
    if(ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_P,ImGuiInputFlags_RouteFocused))shot=true; // Ctrl+P = pin; Ctrl+Shift+S stays Save As
    if(shot && live && !livePinned && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",deviceId},{"run_id",liveId},{"pinned",true}});
    std::vector<const Json *> shots;
    for(const auto &run:history)if(run.at("pinned")==true && (run.at("kind")==kind || (kind=="iv" && run.at("kind")=="band")))shots.push_back(&run);
    ImGui::SameLine();
    if(ImGui::Button(("Shots ("+std::to_string(shots.size())+")###shots").c_str()))ImGui::OpenPopup("shotlist");
    if(ImGui::BeginPopup("shotlist")) {
        static std::string editId,editText;
        if(shots.empty())ImGui::TextDisabled("No shots yet. Press Single shot to pin the current curve.");
        for(const auto *shotRun:shots) {
            const auto &run=*shotRun;const auto id=run.at("run_id").get<std::string>();ImGui::PushID(id.c_str());
            bool visible=run.at("visible");
            if(ImGui::Checkbox("##v",&visible) && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",deviceId},{"run_id",id},{"visible",visible}});
            ImGui::SameLine();const auto c=run.at("colour").get<std::vector<float>>();ImGui::ColorButton("##c",ImVec4(c[0],c[1],c[2],c[3]),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoPicker,ImVec2(12,12));
            ImGui::SameLine();char buf[256];std::snprintf(buf,sizeof(buf),"%s",(editId==id?editText:RunLabel(run)).c_str());
            ImGui::SetNextItemWidth(260);ImGui::InputText("##n",buf,sizeof(buf));
            if(ImGui::IsItemActive()){editId=id;editText=buf;}
            if(ImGui::IsItemDeactivatedAfterEdit() && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",deviceId},{"run_id",id},{"name",std::string(buf)}});
            if(ImGui::IsItemDeactivated())editId.clear();
            ImGui::SameLine();if(ImGui::SmallButton("Delete") && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",deviceId},{"run_id",id},{"pinned",false}});
            ImGui::PopID();
        }
        if(!shots.empty() && ImGui::Button("Clear all") && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",deviceId},{"unpin_kind",kind}});
        ImGui::EndPopup();
    }
    ImGui::PopID();
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
    std::map<std::tuple<const Json *,int,double>,Cols> cols;
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
template<class F> Cols &Derived(const Json &node,int tag,F &&build,double param=0.) {
    auto &cols=Cache().cols;const auto key=std::make_tuple(&node,tag,param);
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
// Live slider-drag preview (DeviceState::previewV/J, never stored) and the growing exact run (liveV/J): dashed / orange curve + badge over the I-V plot; call between BeginPlot/EndPlot.
// Returns the next free badge slot. Neither curve is a result: they are never saved, put in history, exported or read by a readout.
int PreviewOverlay(const DeviceState &d,bool absolute,double scale=1.) {
    int slot=0;
    auto J=[&](double j){return absolute?(j==0?double(NAN):std::abs(j)*scale):j*scale;};
    if(d.liveRunning && !d.liveV.empty()) {
        std::vector<double> y;y.reserve(d.liveJ.size());for(double j:d.liveJ) y.push_back(J(j));
        ImPlot::PlotLine("running##live",d.liveV.data(),y.data(),int(y.size()),{ImPlotProp_LineColor,ImVec4(1.f,.55f,.1f,1.f),ImPlotProp_LineWeight,2.f});
        char text[64];std::snprintf(text,sizeof(text),"RUNNING (%d points so far, exact result follows)",int(y.size()));PlotBadge(text,IM_COL32(255,140,25,230),IM_COL32(0,0,0,255),slot++);
    } else if(!d.previewV.empty()) {
        std::vector<double> x,y;const std::size_t n=d.previewV.size();
        auto Ji=[&](std::size_t i){return J(d.previewJ[i]);};
        if(d.previewCoarse) {for(std::size_t i=0;i+1<n;i+=2) {x.push_back(d.previewV[i]);y.push_back(Ji(i));x.push_back(d.previewV[i+1]);y.push_back(Ji(i+1));x.push_back(d.previewV[i+1]);y.push_back(double(NAN));}} // dashes: every other segment
        else for(std::size_t i=0;i<n;++i) {x.push_back(d.previewV[i]);y.push_back(Ji(i));}
        ImPlot::PlotLine("preview##live",x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(1.f,.55f,.1f,1.f),ImPlotProp_LineWeight,2.f});
        PlotBadge(d.previewCoarse?"PREVIEW (coarse, exact on release)":"PREVIEW (exact on release)",IM_COL32(255,140,25,230),IM_COL32(0,0,0,255),slot++);
    }
    if(d.Stale()) PlotBadge("outdated: model edited",IM_COL32(95,95,105,210),IM_COL32(235,235,240,255),slot++);
    return slot;
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
            if(!hashed) {currentHash=DocumentHash(device.document);hashed=true;}
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
        e->docGen=w.docGen;const auto hash=DocumentHash(device.document);
        for(auto &l:e->lines) if((l.modelHash!=hash)!=l.previous) {l.previous=!l.previous;l.name=l.head+(l.previous?" (previous)":"")+l.tail;}
    }
    for(const auto &l:e->lines) {
        ImPlot::PlotLine(l.name.c_str(),l.x.data(),l.y.data(),int(l.x.size()),{ImPlotProp_LineColor,l.colour,ImPlotProp_LineWeight,l.weight});
        if(auto *item=ImPlot::GetItem(l.name.c_str());item && !item->Show && !w.Busy())(void)w.Dispatch({{"operation","history_update"},{"device_id",device.id},{"run_id",l.runId},{"visible",false}});
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
            if(gr) {const auto *g=ChildOf(point.rest,"generation_recombination");if(!g || !NumbersOf(*g,"x_m",l.x) || !NumbersOf(*g,field,l.y))continue;}
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
bool BeginSavedPlot(Workflow &w,const char *title,ImVec2 size) {
    if(const auto plots=w.project.ui.find("plots");plots!=w.project.ui.end() && plots->contains(title)) {const auto &v=plots->at(title);ImPlot::SetNextAxesLimits(v.at(0),v.at(1),v.at(2),v.at(3),ImPlotCond_Once);if(v.size()==6)ImPlot::SetNextAxisLimits(ImAxis_Y2,v.at(4),v.at(5),ImPlotCond_Once);}
    return ImPlot::BeginPlot(title,size);
}
void EndSavedPlot(Workflow &w,const char *title) {
    const auto limits=ImPlot::GetPlotLimits(ImAxis_X1,ImAxis_Y1);
    double state[6]={limits.X.Min,limits.X.Max,limits.Y.Min,limits.Y.Max};std::size_t n=4;
    if(ImPlot::GetCurrentPlot()->Axes[ImAxis_Y2].Enabled) {const auto secondary=ImPlot::GetPlotLimits(ImAxis_X1,ImAxis_Y2);state[4]=secondary.Y.Min;state[5]=secondary.Y.Max;n=6;}
    const Json *stored=nullptr; // compare in place: building a Json array every frame was a per-frame allocation in every plot
    if(const auto plots=w.project.ui.find("plots");plots!=w.project.ui.end()) if(const auto it=plots->find(title);it!=plots->end())stored=&*it;
    bool same=stored && stored->is_array() && stored->size()==n;
    for(std::size_t i=0;same && i<n;++i)same=(*stored)[i]==state[i];
    if(!same){auto saved=Json::array();for(std::size_t i=0;i<n;++i)saved.push_back(state[i]);w.project.ui["plots"][title]=std::move(saved);w.projectDirty=true;w.RefreshDirty();}ImPlot::EndPlot();
}
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
    if(ImGui::Button(global?"Clear unpinned (all devices)":"Clear unpinned (active device)")) (void)w.Dispatch({{"operation","history_clear"},{"scope",global?"all":"device"}});
    ImGui::SameLine();if(ImGui::Button(global?"Clear including pinned...":"Clear active including pinned..."))ImGui::OpenPopup("Confirm clear");
    if(ImGui::BeginPopupModal("Confirm clear",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(global?"Clear all runs from every device, including pinned?":"Clear all runs from the active device, including pinned?");
        if(ImGui::Button("Clear all")){(void)w.Dispatch({{"operation","history_clear"},{"scope",global?"all":"device"},{"include_pinned",true}});ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    int retention=w.project.ui.value("history_retention",5);
    BeginRow("Automatic runs retained");if(ImGui::SliderInt("##retention",&retention,1,100)) {auto ui=w.project.ui;ui["history_retention"]=retention;(void)w.Dispatch({{"operation","project_edit"},{"ui",ui}});}EndRow();
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
                            else (void)w.Dispatch({{"operation","history_select"},{"device_id",device->id},{"run_id",run.at("run_id")}});
                        }
                    }
                    ImGui::TableSetColumnIndex(4);ImGui::TextUnformatted(run.at("kind").get<std::string>().c_str());
                    ImGui::TableSetColumnIndex(5);
                    const std::time_t seconds=run.at("time_unix_ms").get<std::int64_t>()/1000;std::tm local{};
                    localtime_s(&local,&seconds);char time[32];std::strftime(time,sizeof(time),"%H:%M:%S",&local);ImGui::TextUnformatted(time);
                    if(ImGui::IsItemHovered()){std::strftime(time,sizeof(time),"%Y-%m-%d %H:%M:%S",&local);ImGui::SetTooltip("%s (local)\nElapsed: %.3g s",time,run.at("elapsed_s").get<double>());}
                    ImGui::TableSetColumnIndex(6);
                    if(!run.value("profiles_stored",true)){ImGui::TextDisabled("(i)");if(ImGui::IsItemHovered())ImGui::SetTooltip("Profiles not stored. Recalculate this model to view spatial profiles.");}
                    if(changed && !w.Busy())(void)w.Dispatch(update);
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
// Log slider range in display units (key-specific so a decade is a useful number of pixels).
static SliderMap LogMap(const std::string &k) {
    if(k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3") return {1,1e24,true};
    if(k=="Nt_m3") return {1,1e28,true};
    if(k=="Nt_m2" || k=="peak_density_m2_eV") return {1,1e22,true};
    if(k.find("sigma_")!=std::string::npos) return {1e-30,1e-10,true};
    if(k=="Sn_m_s" || k=="Sp_m_s") return {1e-6,1e8,true};
    if(k=="series_ohm_m2" || k=="area_m2") return {1e-6,1e4,true};
    if(k=="shunt_ohm_m2") return {1e-2,1e10,true};
    if(k=="thickness_m") return {1e-3,1e3,true};
    if(k.find("vth_")==0) return {1e2,1e8,true};
    if(k=="frequency_Hz" || k=="start_Hz" || k=="stop_Hz") return {1e-2,1e12,true};
    if(k=="residual_tolerance" || k=="adaptive_target_relative_J_error") return {1e-18,1,true};
    if(k=="uniform_generation_m3_s") return {1e-30,1e36,true};
    return {1e-30,1e21,true};
}
static bool Number(const char *label,Json &j,const char *key,double scale=1) {
    const auto &n=j.at(key); if(!n.is_number()) return false;
    const std::string k=key; double value=n.get<double>()*scale;
    const bool logarithmic=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3" || k=="Sn_m_s" || k=="Sp_m_s" ||
        k=="residual_tolerance" || k=="adaptive_target_relative_J_error" || k=="frequency_Hz" || k=="start_Hz" || k=="stop_Hz" || k=="dose_m2" || k=="surface_density_m3" || k=="uniform_generation_m3_s" || k.find("vth_")==0;
    BeginRow(label);ImGui::BeginGroup(); bool changed=false;
    if(logarithmic || k=="Nt_m3" || k=="Nt_m2" || k=="peak_density_m2_eV" || k.find("sigma_")!=std::string::npos || k=="area_m2" || k=="series_ohm_m2" || k=="shunt_ohm_m2" || k=="probe_photon_flux_m2_s" || k=="probe_power_W_m2" || k.find("_m1")!=std::string::npos || k=="intensity_multiplier" || k=="thickness_m") {
        const bool density=k=="Nd_m3" || k=="Na_m3" || k=="Nc_ref_m3" || k=="Nv_ref_m3";
        const bool zeroAllowed=k=="Nd_m3" || k=="Na_m3" || k=="Sn_m_s" || k=="Sp_m_s" || k=="series_ohm_m2" || k=="Nt_m3" || k=="Nt_m2" || k=="peak_density_m2_eV" || k.find("sigma_")!=std::string::npos || k.find("_m1")!=std::string::npos || k=="intensity_multiplier";
        bool zero=value==0;
        if(zeroAllowed) {ImGui::TextUnformatted("Zero");ImGui::SameLine();changed=ImGui::Checkbox("##zero",&zero);TrackGesture();ImGui::SameLine();}
        double v=value>0?value:(density?1e14:1.);
        ImGui::BeginDisabled(zero);
        ImGui::SetNextItemWidth(-1);
        changed|=Slide("##value",v,LogMap(k),zero?"0":"%.3g");TrackGesture();
        ImGui::EndDisabled();
        if(zero) ImGui::TextWrapped("Clear Zero to edit.");
        if(changed) value=zero?0:v;
    } else if(k=="points_per_layer" || k=="points" || k=="max_newton_iterations") {
        int v=int(value);const int minimum=k=="points_per_layer"?3:k=="points"?2:1;
        ImGui::SetNextItemWidth(-1);
        changed=ImGui::SliderInt("##value",&v,minimum,k=="max_newton_iterations"?1000:2000,"%d");TrackGesture();value=v;
    } else {
        double lo=0.0,hi=10.0;
        if(k=="thickness_m") {lo=0.01;hi=50;}
        else if(k=="eps_r") {lo=1;hi=40;}
        else if(k=="Eg_eV" || k=="Eg_a_eV" || k=="Eg_b_eV") {lo=0.1;hi=5;}
        else if(k=="chi_eV" || k=="phi_m_eV") {lo=1;hi=7;}
        else if(k=="mu_n_m2_V_s" || k=="mu_p_m2_V_s") {lo=0.1;hi=5000;}
        else if(k=="temperature_K") {lo=100;hi=600;}
        else if(k=="step_V") {lo=0.001;hi=0.2;}
        else if(k=="wavelength_nm" || k=="start_nm" || k=="stop_nm" || k=="uv_a_nm" || k=="uv_b_nm") {lo=100;hi=5000;}
        else if(k=="monochromatic_power_W_m2") {lo=0;hi=2000;}
        else if(k=="urbach_energy_eV") {lo=.001;hi=.2;}
        else if(k=="front_transmission" || k=="back_reflection" || k=="front_internal_reflection") {lo=0;hi=1;}
        else if(k=="characteristic_energy_eV") {lo=0.001;hi=1;}
        else if(k=="gaussian_width" || k=="tail_width") {lo=1;hi=20;}
        else {lo=-5;hi=5;}
        ImGui::SetNextItemWidth(-1);
        changed=Slide("##value",value,{lo,hi,false},"%.4g");TrackGesture();
    }
    if(ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Ctrl+click: exact value. Shift: 10x finer, Ctrl+Shift: 100x, F: fine toggle. Esc while dragging: cancel. Arrows / Ctrl+wheel: step (Shift: finer).");
    ImGui::EndGroup();SweepMenu(j,key,scale,label);EndRow();
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
    struct PerfScope {const char *n;std::chrono::steady_clock::time_point t=std::chrono::steady_clock::now();~PerfScope(){static const bool on=std::getenv("SCAPSLAB_PERF")!=nullptr;if(on)SST::PerfAdd(std::string("panel ")+n,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}} perfScope{names[view]};
    w->tutorialOverlay.Window(std::string("scapslab.")+names[view]);
    w->WatchPlots();
    numericWorkflow=w;numericDocument=nullptr;
    struct ResetNumeric {~ResetNumeric(){numericDocument=nullptr;numericWorkflow=nullptr;}} resetNumeric;
    if(!placed && !gallery) {auto s=GetState();const auto *vp=ImGui::GetMainViewport();s.x+=vp->Pos.x;s.y+=vp->Pos.y;ApplyState(s);placed=true;}
    ImGui::TextDisabled("SCAPSLAB / %s",names[view]);
    if(w->fineLock || ImGui::GetIO().KeyShift) {ImGui::SameLine();ImGui::TextColored(ImVec4(1.f,.75f,.2f,1.f),"[FINE%s]",w->fineLock?" lock, F":"");}
    {static const std::map<int,const char*> subtitles={{5,"Spatial profiles (bands, carriers) vs. position x at the selected bias point"},{6,"Capacitance vs. DC bias at fixed frequency"},{7,"1/C^2 vs. DC bias (built-in potential, doping)"},{8,"Capacitance and G/omega vs. frequency at fixed bias"},{9,"G/omega vs. frequency at fixed bias"},{10,"J-V curve of the checked devices; marker = selected bias point"},{22,"Generation and recombination rates vs. position x at the selected bias point"}};
     if(const auto it=subtitles.find(view);it!=subtitles.end()) { // compact header: help text is a tooltip on (?), status stays on the same grey line
         ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",it->second);
         if(view==5 && !w->status.empty()){ImGui::SameLine();ImGui::TextDisabled("| %s",w->status.c_str());}}}
    if(view==4 || (view!=5 && w->response.is_object() && !w->response.value("ok",true))) ImGui::TextWrapped("%s",w->status.c_str());
    if(view==5 || view==10 || view==22) {} else ImGui::Separator();
    if(w->Current().document.contains("scaps_native")) for(const auto &warning:w->Current().document.at("scaps_native").value("warnings",Json::array())) ImGui::TextWrapped("SCAPS import warning: %s",warning.get<std::string>().c_str());
    if(!pinLayer.empty() && w->project.active_device!=pinDevice) { // extra "Layer: <name>" window of a device that is not active
        ImGui::TextWrapped("This layer belongs to another device.");
        if(ImGui::Button("Activate that device")) Send({{"operation","device_select"},{"device_id",pinDevice}});
        return;
    }
    if((view==5 || view==10 || view==15 || view==16 || view==17 || view==18 || view==22 || (view>=6 && view<10)) && !w->ViewsReady()) {ImGui::TextDisabled("Preparing results...");return;} // Json views are built on a worker, never on the UI thread
    if(view==16) {Batch();return;}
    if(view==17) {Recorder();return;}
    if(view==18) {BatchPlots();return;}
    if(view==0) {Devices();return;}
    if(view==15) {DrawHistory(*w);return;}
    if(view==10) {Compare();return;}
    if(view==5) {Results();return;}
    if(view==22) {GenerationRecombination();return;}
    if(view>=6 && view<10) {AC();return;}
    const bool replacing=w->pending && (w->pending->request.at("operation")=="project_load" || w->pending->request.at("operation")=="load" || w->pending->request.at("operation")=="layer");
    ImGui::BeginDisabled(replacing);
    if(w->Current().document.is_object()) {
        auto draft=w->Current().document;
        if(!w->edits.empty()) draft=w->edits.back().at("document");
        else if(w->pending && w->pendingDevice==w->project.active_device && w->pending->request.value("operation",std::string{})=="edit" && w->pending->request.contains("document")) draft=w->pending->request.at("document");
        numericDocument=&draft;
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
            auto &layers=draft["layers"]; int pinned=0; auto &selected=pinLayer.empty()?w->Current().selectedLayer:pinned; // pinned window: layer found by stable ID, independent of the tree selection
            if(!pinLayer.empty()) for(int i=0;i<int(layers.size());++i) if(layers[i].at("scapslab_id")==pinLayer) pinned=i;
            selected=std::clamp(selected,0,int(layers.size())-1);
            if(pinLayer.empty()) {BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {
                for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i); if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),selected==i)) selected=i; ImGui::PopID();} ImGui::EndCombo();
            }
            EndRow();}
            auto &l=layers[selected]; const auto id=l.at("scapslab_id");
            if(ImGui::Button("Add")) Send({{"operation","layer"},{"action","add"}}); if(pinLayer.empty()) w->tutorialOverlay.Item("scapslab.Layers","add-layer"); ImGui::SameLine();
            if(ImGui::Button("Remove")) Send({{"operation","layer"},{"action","remove"},{"layer_id",id}}); ImGui::SameLine();
            if(ImGui::Button("Up") && selected>0) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected-1}}); ImGui::SameLine();
            if(ImGui::Button("Down") && selected+1<int(layers.size())) Send({{"operation","layer"},{"action","move"},{"layer_id",id},{"index",selected+1}});
            if(pinLayer.empty()) {ImGui::SameLine();if(ImGui::Button("Open in new window")) OpenLayerWindow(w,w->project.active_device,id.get<std::string>());if(ImGui::IsItemHovered())ImGui::SetTooltip("Edit this layer in a separate window, side by side with other layers");}
            ImGui::TextDisabled("Stable ID: %s",id.get<std::string>().c_str());
            {   // Defects / interface shortcuts: edit through the Defects and Interfaces panels.
                static std::string picked;const bool last=selected+1>=int(layers.size());
                if(ImGui::Button("Add defect")) {w->pendingDefect=AddDefectToDraft(draft,selected,false);w->pendingSheet=false;w->Current().selectedInterface=-1;w->focusPanel=11;picked=draft["layers"][selected]["defects"].back().at("scapslab_id");changed=true;++gesture;}
                ImGui::SameLine();ImGui::BeginDisabled(last);
                if(ImGui::Button("Add interface")) {w->pendingDefect=AddDefectToDraft(draft,selected,true);w->pendingSheet=true;w->Current().selectedInterface=selected;w->focusPanel=12;picked=draft["interface_defects"].back().at("id");changed=true;++gesture;}
                ImGui::EndDisabled();if(last&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("The last layer has no next layer to form an interface with.");
                ImGui::SameLine();
                if(ImGui::Button("Remove##defect")&&!picked.empty()) {
                    auto &bulk=draft["layers"][selected]["defects"];auto &sheets=draft["interface_defects"];
                    const auto bi=std::find_if(bulk.begin(),bulk.end(),[&](const Json &v){return v.at("scapslab_id")==picked;});
                    if(bi!=bulk.end()) {bulk.erase(bi);changed=true;++gesture;picked.clear();}
                    else if(sheets.is_array()) {const auto si=std::find_if(sheets.begin(),sheets.end(),[&](const Json &v){return v.at("id")==picked;});if(si!=sheets.end()) {sheets.erase(si);changed=true;++gesture;picked.clear();}}
                }
                const auto &bulk=draft["layers"][selected]["defects"];
                for(int i=0;i<int(bulk.size());++i) {
                    const auto &d=bulk[i];const std::string idv=d.at("scapslab_id");
                    const char *type=d.value("amphoteric",false)?"amphoteric":d.value("charge_type",std::string("neutral"))=="donor_like"?"donor":d.value("charge_type",std::string("neutral"))=="acceptor_like"?"acceptor":"neutral";
                    char row[160];std::snprintf(row,sizeof(row),"%s | Et %+.3g eV | Nt %.3g cm^-3##d%d",type,d.value("energy_offset_eV",0.0),d.value("Nt_m3",0.0)*1e-6,i);
                    if(ImGui::Selectable(row,picked==idv)) {picked=idv;w->pendingDefect=i;w->pendingSheet=false;w->Current().selectedInterface=-1;w->focusPanel=11;}
                }
                if(draft.contains("interface_defects")&&draft["interface_defects"].is_array()) for(int i=0;i<int(draft["interface_defects"].size());++i) {
                    const auto &d=draft["interface_defects"][i];if(d.at("left_layer_index")!=selected)continue;const std::string idv=d.at("id");
                    char row[160];std::snprintf(row,sizeof(row),"interface | Et %+.3g eV | Nt %.3g cm^-2##i%d",d.value("energy_offset_eV",0.0),d.value("Nt_m2",0.0)*1e-4,i);
                    if(ImGui::Selectable(row,picked==idv)) {picked=idv;w->pendingDefect=i;w->pendingSheet=true;w->Current().selectedInterface=selected;w->focusPanel=12;}
                }
            }
            static int materialSelection=0;BeginRow("Material selection");ImGui::Combo("##material_selection",&materialSelection,"Both A/B\0Pure A\0Pure B\0");EndRow();
            const char *selection=materialSelection==1?"A":materialSelection==2?"B":"both";
            BeginRow("SCAPS material [.material]");
            const bool materialLoad=ImGui::Button("Load...");w->tutorialOverlay.Item("scapslab.Layers","material-load");if(materialLoad)try {if(const auto path=OpenFileDialog({"SCAPS material","material"}))Send({{"operation","load_material"},{"layer_id",id},{"selection",selection},{"path",path->Utf8()}});}catch(const std::exception &e){w->status=e.what();}
            ImGui::SameLine();const bool materialSave=ImGui::Button("Save...");w->tutorialOverlay.Item("scapslab.Layers","material-save");if(materialSave)try {if(const auto path=SaveFileDialog({"SCAPS material","material"},"layer.material"))Send({{"operation","save_material"},{"layer_id",id},{"selection",selection},{"path",path->Utf8()}});}catch(const std::exception &e){w->status=e.what();}EndRow();
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
                bool flat=c.value("flat_band",false);
                if(Toggle("Flat band (ohmic, automatic phi_m)",flat)) {c["flat_band"]=flat;changed=true;++gesture;}
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Checked (SCAPS 'flat band'): phi_m follows the adjacent semiconductor, the contact is ohmic, no band bending.\nUnchecked: phi_m is used as entered; a metal on the wrong side of the semiconductor work function makes a depleting (Schottky-like) contact.");
                ImGui::BeginDisabled(flat);changed|=Number("Work function phi_m [eV]",c,"phi_m_eV");ImGui::EndDisabled();
                if(flat) ImGui::TextDisabled("phi_m is derived from the adjacent layer (flat band).");
                else {
                    // Documented formula (same non-degenerate model as scaps_core semiconductor_work_function_eV; Nc/Nv at reference T, full ionisation):
                    // ni = sqrt(Nc Nv) exp(-Eg/2kT); n0 = (net+sqrt(net^2+4ni^2))/2 for net=Nd-Na>=0 (else n0=ni^2/p0); phi_s = chi - kT ln(n0/Nc).
                    // n-type barrier = phi_m - phi_s, p-type barrier = phi_s - phi_m; barrier > 0 means depleting.
                    const auto &layer=draft.at("layers")[std::string(side)=="left"?0:draft.at("layers").size()-1];const auto &m=layer.at("material");
                    const double kT=8.617333262e-5*draft.value("temperature_K",300.),Nc=m.at("Nc_ref_m3"),Nv=m.at("Nv_ref_m3"),Eg=m.at("Eg_eV");
                    const double net=layer.value("doping",Json::object()).value("Nd_m3",0.)-layer.value("doping",Json::object()).value("Na_m3",0.);
                    const double ni=std::sqrt(Nc*Nv)*std::exp(-Eg/(2*kT)),root=std::sqrt(net*net+4*ni*ni),n0=net>=0?(net+root)/2:ni*ni/((-net+root)/2);
                    const double phiS=m.at("chi_eV").get<double>()-kT*std::log(n0/Nc),barrier=net>=0?c.value("phi_m_eV",0.)-phiS:phiS-c.value("phi_m_eV",0.);
                    if(barrier>1e-3) ImGui::TextColored(ImVec4(1.f,.7f,.3f,1.f),"semiconductor phi_s = %.3f eV -> barrier %.3f eV (depleting, Schottky-like)",phiS,barrier);
                    else ImGui::TextColored(ImVec4(.5f,.9f,.5f,1.f),"semiconductor phi_s = %.3f eV -> barrier %.3f eV (ohmic, no depletion)",phiS,barrier);
                    if(!c.contains("flat_band")) ImGui::TextDisabled("flat_band not set: engine default (ohmic only when phi_m is on the ohmic side of phi_s).");
                }
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
            changed|=Number("Step [V]",draft["voltage"],"step_V"); changed|=Number("T [K]",draft,"temperature_K");w->tutorialOverlay.Item("scapslab.Run","temperature");
            ImGui::TextWrapped("Applied voltage: left minus right. Signed J follows the scaps_core terminal-current convention.");
            bool automatic=w->autoRecalculate,bandAutomatic=w->autoRecalculateBand;
            if(ImGui::Checkbox("Auto##iv",&automatic)) Send({{"operation","auto_recalculate"},{"enabled",automatic}});TrackGesture();ImGui::SameLine();
            ImGui::BeginDisabled(w->Busy());
            if(ImGui::Button("Calculate I-V")) Send({{"operation","run"}});w->tutorialOverlay.Item("scapslab.Run","run");
            ImGui::EndDisabled();ImGui::SameLine(0,24);
            if(ImGui::Checkbox("Auto##band",&bandAutomatic)) Send({{"operation","auto_recalculate"},{"enabled",w->autoRecalculate},{"band_enabled",bandAutomatic}});TrackGesture();ImGui::SameLine();
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
void PlotJson(const char *title,const char *xLabel,const char *yLabel,const Json &x,const Json &y,bool log=false,float height=190,bool persistent=false) {
    // persistent: x/y live inside a stored result, so their vectors come from the plot cache; otherwise (per-frame temporaries) convert here
    std::vector<double> xl,yl;if(!persistent) {xl=x.get<std::vector<double>>();yl=y.get<std::vector<double>>();if(log) for(auto &v:yl) if(v<=0) v=NAN;}
    const auto &xs=persistent?Vec(x):xl,&ys=persistent?Vec(y,1.,log?1:0):yl;
    if(ImPlot::BeginPlot(title,ImVec2(-1,height))) {ImPlot::SetupAxes(xLabel,yLabel,ImPlotAxisFlags_None,ImPlotAxisFlags_None);if(log) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);ImPlot::PlotLine(title,xs.data(),ys.data(),int(xs.size()));ImPlot::EndPlot();}
}
}
bool Panel::OpticalFile(Json &object,const char *file,const char *table,const char *extension,const char *label) {
    ImGui::PushID(file);const auto previous=object;bool changed=FileField(label,object,file);
    BeginRow("Select file");if(ImGui::Button("Browse...")) {
        try {if(const auto path=OpenFileDialog({label,extension})){object[file]=path->Utf8();changed=true;}}catch(const std::exception &e){w->status=e.what();}
    }w->tutorialOverlay.Item(std::string(extension)=="abs"?"scapslab.Absorption":std::string(extension)=="opt"?"scapslab.Defects":"scapslab.Illumination",file);EndRow();
    if(changed) {
        try {
            const auto path=object.at(file).get<std::string>();Json points;
            const std::string kind=extension;
            if(kind=="gen")points=scaps_core::read_scaps_generation(path);
            else if(kind=="spe")points=scaps_core::read_scaps_spectrum(path);
            else if(kind=="abs")points=scaps_core::read_scaps_absorption(path);
            else if(kind=="ftr")points=scaps_core::read_scaps_filter(path);
            else points=scaps_core::read_scaps_absorption(path);
            object[table]=points;object[std::string("scapslab_")+file]=path;object[file]="";
        }catch(const std::exception &e){w->status=std::string(label)+": "+e.what();object=previous;changed=false;}
    }
    const auto points=object.value(table,Json::array());
    if(!points.empty()) {
        ImGui::TextWrapped("%s | %d points",object.value(std::string("scapslab_")+file,std::string("Embedded table")).c_str(),int(points.size()));
        Json x=Json::array(),y=Json::array();for(const auto &point:points){x.push_back(point[0].get<double>()*(std::string(extension)=="gen"?1e9:1.));y.push_back(point[1].get<double>()*(std::string(extension)=="gen"?1e-6:1.));}
        const std::string kind=extension;
        PlotJson(label,kind=="gen"?"x [nm]":"Wavelength [nm]",kind=="gen"?"G [cm^-3 s^-1]":kind=="spe"?"Power [W/m^2]":kind=="abs"?"alpha [m^-1]":kind=="ftr"?"Fraction [1]":"Cross section [m^2]",x,y,false,180);
        if(kind=="gen" && ImGui::BeginPopupContextItem("Export generation")) {
            ImGui::BeginDisabled(w->Busy());
            for(const auto *format:{"csv","grd"})if(ImGui::MenuItem((std::string("Export .")+format).c_str()))try {
                if(const auto path=SaveFileDialog({"Generation profile",format},(std::string("generation.")+format).c_str())) {
                    Send({{"operation","export_profile"},{"parameter_id","generation_m3_s"},{"path",path->Utf8()}});
                }
            }catch(const std::exception &e){w->status=e.what();}ImGui::EndDisabled();ImGui::EndPopup();
        }
    }
    ImGui::PopID();return changed;
}
bool Panel::Illumination(Json &draft) {
    bool changed=false;auto &light=draft["illumination"];
    if(!w->Busy() && w->Current().actions.contains("qe"))qeRequest.update(w->Current().actions.at("qe"),true);
    const auto previousQe=qeRequest;
    bool enabled=light.value("enabled",false);if(Toggle("Light enabled",enabled)) {light["enabled"]=enabled;changed=true;}
    const char *galleryTab=gallery?std::getenv("SCAPSLAB_OPTICAL_TAB"):nullptr;
    const int savedTab=w->project.ui.value("optical_tab",0);const bool restoreTab=opticalTab!=savedTab;
    int tutorialTab=-1;if(const auto *step=w->tutorial.Step();step && step->at("target").value("window",std::string{})=="scapslab.Illumination"){const auto item=step->at("target").value("item",std::string{});tutorialTab=item=="light-iv"?1:item=="qe"?2:-1;} // the coach must point at a visible button
    auto tabFlags=[&](int tab){return (galleryTab && std::atoi(galleryTab)==tab) || (restoreTab && savedTab==tab) || tutorialTab==tab?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;};
    auto selectTab=[&](int tab){opticalTab=tab;w->project.ui["optical_tab"]=tab;};
    if(ImGui::BeginTabBar("Optical views")) {
        if(ImGui::BeginTabItem("Spectrum / G(x)",nullptr,tabFlags(0))) {selectTab(0);
            const bool wide=ImGui::GetContentRegionAvail().x>750;
            if(wide) {ImGui::BeginTable("Spectrum and generation",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            int source=light.value("mode",std::string("uniform"))=="generation_file"?3:light.value("mode",std::string("uniform"))=="uniform"?4:light.value("mode",std::string{})=="monochromatic"?2:light.value("scapslab_spectrum",std::string{})=="AM1.5G"?0:1;
            BeginRow("Spectrum");const bool sourceChanged=ImGui::Combo("##source",&source,"AM1.5G\0Spectrum from file\0Monochromatic\0Generation profile from file\0Uniform generation\0");w->tutorialOverlay.Item("scapslab.Illumination","source");if(sourceChanged) {
                std::string spectrumPath;bool apply=true;
                if(source==1 || source==3) {try {const auto path=OpenFileDialog({source==3?"SCAPS generation":"SCAPS spectrum",source==3?"gen":"spe"});if(path)spectrumPath=path->Utf8();else apply=false;}catch(const std::exception &e){w->status=e.what();apply=false;}}
                if(apply) {light["mode"]=source==3?"generation_file":source==4?"uniform":source==2?"monochromatic":"spectrum";light["scapslab_spectrum"]=source==0?"AM1.5G":"custom";light["spectrum_file"]="";light["spectrum_nm_W_m2"]=source==0?AM15Spectrum():Json::array();if(source==1)light["spectrum_file"]=spectrumPath;if(source==3){light["generation_file"]=spectrumPath;light["generation_m_m3s"]=Json::array();}changed=true;++gesture;}
            }EndRow();
            if(source==1) {if(OpticalFile(light,"spectrum_file","spectrum_nm_W_m2","spe","Spectrum file [.spe]")) {light["mode"]="spectrum";light["scapslab_spectrum"]="custom";changed=true;}}
            if(source==3)changed|=OpticalFile(light,"generation_file","generation_m_m3s","gen","Generation profile [.gen]");
            if(source==4)changed|=Number("Uniform G [m^-3 s^-1]",light,"uniform_generation_m3_s");
            if(source==2) {changed|=Number("Wavelength [nm]",light,"wavelength_nm");changed|=Number("Power [W/m^2]",light,"monochromatic_power_W_m2");}
            changed|=Number("Intensity multiplier",light,"intensity_multiplier");
            bool right=light.value("from_right",false);if(Toggle("Illuminate from right",right)) {light["from_right"]=right;changed=true;}
            changed|=Number("Front transmission",light,"front_transmission");changed|=Number("Back reflection",light,"back_reflection");changed|=Number("Internal reflection",light,"front_internal_reflection");
            changed|=OpticalFile(light,"front_filter_file","front_filter_nm_fraction","ftr","Front transmission [.ftr]");changed|=OpticalFile(light,"back_filter_file","back_filter_nm_fraction","ftr","Back reflection [.ftr]");
            if(wide) ImGui::TableNextColumn();
            const auto &r=w->Current().bandResult.contains("optics")?w->Current().bandResult:w->Current().result;
            if(r.contains("optics")) {const auto &o=r.at("optics");ImGui::Text("Incident power %.6g W/m^2",o.at("incident_power_W_m2").get<double>());PlotJson("G(x)","x [m]","G [m^-3 s^-1]",o.at("x_m"),o.at("generation_m3_s"),false,330,true);}
            else ImGui::TextWrapped("Generation preview updates after automatic band calculation.");
            if(wide) ImGui::EndTable();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Light I-V",nullptr,tabFlags(1))) {selectTab(1);
            ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Calculate light I-V")) Send({{"operation","run"}});w->tutorialOverlay.Item("scapslab.Illumination","light-iv");ImGui::EndDisabled();ImGui::SameLine();ShotBar(*w,"iv");ImGui::TextDisabled("Illuminated J-V curve with Voc, Jsc and MPP markers");
            const auto &r=w->Current().result;
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
            } else if(live.Stale() && r.contains("photovoltaic")) ImGui::TextDisabled("PV metrics below are outdated (model edited); recalculating replaces them.");
            if(!estimating && r.contains("photovoltaic")) {const auto &pv=r.at("photovoltaic");ImGui::TextWrapped("PV: %s",pv.at("status").get<std::string>().c_str());if(!pv.value("message",std::string{}).empty()) ImGui::TextWrapped("%s",pv.at("message").get<std::string>().c_str());
                if(ImGui::BeginTable("PV metrics",2,ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    for(const auto &[key,label,scale]:{std::tuple{"Voc_V","Voc [V]",1.},std::tuple{"Jsc_A_m2","Jsc [mA/cm^2]",.1},std::tuple{"FF_fraction","FF [%]",100.},std::tuple{"efficiency_fraction","Efficiency [%]",100.}}) {
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();
                        if(pv.at(key).is_number())ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",pv.at(key).get<double>()*scale);else ImGui::TextUnformatted("unavailable");
                    }ImGui::EndTable();
                }
            }
            if(r.contains("points") || live.liveRunning) {
                const bool reset=ImGui::Button("Reset PV view");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Default range: -1.2 Jsc to +Jsc. Scroll to zoom; drag to pan.");
                std::vector<double>x,y;if(r.contains("points")) for(const auto &p:r.at("points")) {x.push_back(p.at("voltage_V"));y.push_back(p.value("valid",false) && p.at("J_A_m2").is_number()?p.at("J_A_m2").get<double>()*.1:NAN);}
                const auto pv=r.value("photovoltaic",Json::object());
                const bool valid=pv.value("status",std::string{})=="converged";
                const double jsc=valid && pv.at("Jsc_A_m2").is_number()?std::abs(pv.at("Jsc_A_m2").get<double>())*.1:0.;
                if(BeginSavedPlot(*w,"Light I-V",ImVec2(-1,std::max(280.f,ImGui::GetContentRegionAvail().y-30)))) {
                    ImPlot::SetupAxes("Voltage [V]","J [mA/cm^2]");ImPlot::SetupLegend(ImPlotLocation_NorthWest,ImPlotLegendFlags_Horizontal);
                    if(x.size()>1 && x.front()!=x.back())ImPlot::SetupAxisLimits(ImAxis_X1,*std::min_element(x.begin(),x.end()),*std::max_element(x.begin(),x.end()),reset?ImGuiCond_Always:ImGuiCond_Once);
                    if(jsc>0)ImPlot::SetupAxisLimits(ImAxis_Y1,-1.2*jsc,jsc,reset?ImGuiCond_Always:ImGuiCond_Once);
                    HistoryLines(*w,w->Current(),"iv","voltage_V","J_A_m2",false,.1);
                    {StaleDim dim(live);ImPlot::PlotLine("Light I-V",x.data(),y.data(),int(x.size()),{ImPlotProp_LineWeight,2.5f});}
                    PreviewOverlay(live,false,.1);
                    if(estimating) { // markers of the estimate: hollow-style, labelled as such
                        const auto &e=live.pvEstimate;const double zero=0.,sc=-std::abs(e.Jsc_A_m2)*.1,mj=e.Jmpp_A_m2*.1;
                        if(std::isfinite(e.Voc_V))ImPlot::PlotScatter("Voc (estimate)",&e.Voc_V,&zero,1);
                        if(std::isfinite(sc))ImPlot::PlotScatter("Jsc (estimate)",&zero,&sc,1);
                        if(std::isfinite(e.Vmpp_V) && std::isfinite(mj))ImPlot::PlotScatter("MPP (estimate)",&e.Vmpp_V,&mj,1);
                    }
                    if(valid && !estimating) {
                        const double zero=0.,shortCircuit=-jsc;
                        if(pv.at("Voc_V").is_number()) {const double voc=pv.at("Voc_V");ImPlot::PlotScatter("Voc",&voc,&zero,1);}
                        if(jsc>0)ImPlot::PlotScatter("Jsc",&zero,&shortCircuit,1);
                        if(pv.contains("Vmpp_V") && pv.at("Vmpp_V").is_number() && pv.at("Jmpp_A_m2").is_number()) {const double v=pv.at("Vmpp_V"),j=pv.at("Jmpp_A_m2").get<double>()*.1;ImPlot::PlotScatter("MPP",&v,&j,1);}
                    }EndSavedPlot(*w,"Light I-V");
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("QE(lambda)",nullptr,tabFlags(2))) {selectTab(2);
            const bool wide=ImGui::GetContentRegionAvail().x>750;if(wide){ImGui::BeginTable("QE controls and plot",2,ImGuiTableFlags_SizingStretchSame);ImGui::TableNextColumn();}
            Number("Start wavelength [nm]",qeRequest,"start_nm");Number("Stop wavelength [nm]",qeRequest,"stop_nm");Number("Wavelength samples",qeRequest,"points");
            Choice("Probe mode",qeRequest,"probe_mode",{{"Constant photons","constant_flux"},{"Constant power","constant_power"}});
            Number(qeRequest["probe_mode"]=="constant_flux"?"Photon flux [m^-2 s^-1]":"Probe power [W/m^2]",qeRequest,qeRequest["probe_mode"]=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2");
            ImGui::TextWrapped("Dark differential EQE at 0 V; current device temperature. Full spectrum coverage is required for integrated Jsc.");
            ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Calculate QE")) Send(qeRequest);w->tutorialOverlay.Item("scapslab.Illumination","qe");ImGui::EndDisabled();ImGui::SameLine();ShotBar(*w,"qe");ImGui::TextDisabled("External quantum efficiency vs. wavelength");
            if(wide) ImGui::TableNextColumn();
            if(w->Current().acResults.contains("qe")) {const auto &q=w->Current().acResults.at("qe");ImGui::TextWrapped("Effective: %s | T %.6g K | 0 V | %d samples",q.at("probe_mode").get<std::string>().c_str(),q.at("temperature_K").get<double>(),int(q.at("points").size()));Json x=Json::array(),y=Json::array();for(const auto &p:q.at("points")) {x.push_back(p.at("wavelength_nm"));y.push_back(p.at("EQE").is_number()?p.at("EQE"):Json(NAN));}
                ImGui::TextWrapped("Integrated Jsc: %s A/m^2 | Light Jsc: %s A/m^2",q.at("integrated_Jsc_A_m2").dump().c_str(),q.at("light_Jsc_A_m2").dump().c_str());ImGui::TextWrapped("%s",q.at("integration_note").get<std::string>().c_str());if(BeginSavedPlot(*w,"QE history",ImVec2(-1,300))) {ImPlot::SetupAxes("Wavelength [nm]","EQE [fraction]");HistoryLines(*w,w->Current(),"qe","wavelength_nm","EQE");{std::vector<double> qx,qy;for(const auto &p:q.at("points")){qx.push_back(p.at("wavelength_nm"));qy.push_back(p.at("EQE").is_number()?p.at("EQE").get<double>():NAN);}ImPlot::PlotLine("EQE",qx.data(),qy.data(),int(qx.size()),{ImPlotProp_LineWeight,2.5f});}EndSavedPlot(*w,"QE history");}
                if(ImGui::BeginTable("QE sample voltages",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,120))) {
                    for(const auto *label:{"Wavelength [nm]","Terminal [V]","Internal [V]"}) ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
                    for(const auto &p:q.at("points")) {ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%.6g",p.at("wavelength_nm").get<double>());ImGui::TableNextColumn();ImGui::Text("%.6g",p.value("terminal_voltage_V",p.at("voltage_V")).get<double>());ImGui::TableNextColumn();const auto v=p.value("internal_voltage_V",Json(nullptr));if(v.is_number()) ImGui::Text("%.9g",v.get<double>());else ImGui::TextUnformatted("unavailable");}
                    ImGui::EndTable();
                }
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
    auto &a=layers[selected]["absorption"];const auto before=a;bool changed=Choice("Absorption model",a,"model",{{"None","none"},{"Square root","sqrt"},{"SCAPS model","scaps"},{"File","file"}});w->tutorialOverlay.Item("scapslab.Absorption","model");
    if(changed && a["model"]=="file" && a.value("file",std::string{}).empty()) {try {const auto path=OpenFileDialog({"SCAPS absorption","abs"});if(path)a["file"]=path->Utf8();else {a=before;changed=false;}}catch(const std::exception &e){w->status=e.what();a=before;changed=false;}}
    if(a["model"]=="file") changed|=OpticalFile(a,"file","table_nm_m1","abs","Absorption file [.abs]");
    if(a["model"]=="sqrt") {changed|=Number("A [m^-1 eV^1/2]",a,"A_m1_eV_half");changed|=Number("B [m^-1 eV^-1/2]",a,"B_m1_eV_minus_half");}
    if(a["model"]=="scaps") {changed|=Number("Background [m^-1]",a,"background_m1");changed|=Number("Step [m^-1]",a,"step_m1");changed|=Number("Sqrt alpha [m^-1]",a,"sqrt_alpha_m1");changed|=Number("Sqrt beta [m^-1]",a,"sqrt_beta_m1");}
    bool mixing=a.contains("composition");
    if(Toggle("Composition absorption A/B",mixing)) {
        if(!mixing){a.erase("composition");changed=true;}
        else try {
            const auto fileA=OpenFileDialog({"Pure A absorption","abs"});
            if(fileA)if(const auto fileB=OpenFileDialog({"Pure B absorption","abs"})) {
                const double gap=layers[selected].at("material").at("Eg_eV");
                a["composition"]={{"table_a_nm_m1",scaps_core::read_scaps_absorption(fileA->Utf8())},{"table_b_nm_m1",scaps_core::read_scaps_absorption(fileB->Utf8())},
                    {"file_a",""},{"file_b",""},{"scapslab_file_a",fileA->Utf8()},{"scapslab_file_b",fileB->Utf8()},{"Eg_a_eV",gap},{"Eg_b_eV",gap},{"uv_a_nm",200.},{"uv_b_nm",200.}};
                changed=true;
            }
        }catch(const std::exception &e){w->status=e.what();}
    }
    if(a.contains("composition")) {
        auto &c=a["composition"];
        changed|=OpticalFile(c,"file_a","table_a_nm_m1","abs","Pure A absorption [.abs]");
        changed|=OpticalFile(c,"file_b","table_b_nm_m1","abs","Pure B absorption [.abs]");
        changed|=Number("Pure A gap [eV]",c,"Eg_a_eV");changed|=Number("Pure B gap [eV]",c,"Eg_b_eV");
        changed|=Number("Pure A UV limit [nm]",c,"uv_a_nm");changed|=Number("Pure B UV limit [nm]",c,"uv_b_nm");
    }
    bool tail=a.value("urbach_enabled",false);if(Toggle("Urbach tail",tail)) {a["urbach_enabled"]=tail;changed=true;}if(tail) changed|=Number("Urbach E_U [eV]",a,"urbach_energy_eV");
    ImGui::TextWrapped("Eg-joint Urbach extension. A pure square-root model has alpha(Eg)=0 and therefore no tail; use a nonzero step/background or a file.");
    if(wide) ImGui::TableNextColumn();
    const auto &r=w->Current().bandResult.contains("optics")?w->Current().bandResult:w->Current().result;
    if(r.contains("optics")) for(const auto &curve:r.at("optics").at("absorption")) if(curve.at("layer_id")==layers[selected].at("scapslab_id")) {
        const auto &x=Vec(curve.at("wavelength_nm")),&y=Vec(curve.at("alpha_m1"),1.,1),&base=Vec(curve.at("without_tail_m1"),1.,1);
        if(BeginSavedPlot(*w,"Absorption preview",ImVec2(-1,300))) {ImPlot::SetupAxes("Wavelength [nm]","alpha [m^-1]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);ImPlot::PlotLine("With tail",x.data(),y.data(),int(x.size()));ImPlot::PlotLine("Without tail",x.data(),base.data(),int(x.size()));EndSavedPlot(*w,"Absorption preview");}
    }if(wide) ImGui::EndTable();return changed;
}
bool Panel::ProfileEditor(Json &g,double thickness,const std::string &parameter,const Json &composition,bool diffusion) {
    ImGui::PushID(parameter.c_str());bool changed=false;
    const bool density=parameter.find("_m3")!=std::string::npos;
    const bool mobility=parameter.find("_m2_V_s")!=std::string::npos;
    const double scale=density?1e-6:mobility?1e4:1.;
    const std::string unit=density?"cm^-3":mobility?"cm^2/(V s)":parameter=="composition" || parameter=="eps_r"?"1":parameter.find("vth_")==0?"m/s":"eV";
    const auto old=g;
    if(!g.contains("law"))g["law"]="uniform";
    changed|=Choice("Profile law",g,"law",{{"Uniform","uniform"},{"Linear","linear"},{"Parabolic","parabolic"},{"Two parabolas","parabolic2"},
        {"Logarithmic","logarithmic"},{"Exponential","exponential"},{"Beta","beta"},{"Power","power"},{"Effective medium","effective_medium"},{"Gaussian","gaussian"},{"From file","table"}});
    w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","law");
    if(diffusion) {BeginRow("Diffusion law");const char* owner=parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading";if(ImGui::Button("Gauss")){g["law"]="gauss_diffusion";changed=true;}w->tutorialOverlay.Item(owner,"diffusion-gauss");ImGui::SameLine();if(ImGui::Button("erfc")){g["law"]="erfc_diffusion";changed=true;}w->tutorialOverlay.Item(owner,"diffusion-erfc");EndRow();}
    auto load=[&] {
        try {if(const auto file=OpenFileDialog({"SCAPS grading","grd"})) {
            auto loaded=ProfileFromFile(file->Utf8(),density);
            if(!w->path.empty()) {std::error_code ec;const auto relative=std::filesystem::relative(file->Native(),std::filesystem::path(w->path).parent_path(),ec);if(!ec)loaded["source_file"]=relative.generic_string();}
            g=std::move(loaded);changed=true;
        }else if(old.value("law",std::string{})!="table") {g=old;changed=false;}}catch(const std::exception &e){w->status=e.what();g=old;changed=false;}
    };
    if(changed && g["law"]=="table" && !g.contains("coordinate"))load();
    if(g.value("law",std::string{})=="table") {
        BeginRow("Grading file [.grd]");if(ImGui::Button("Browse..."))load();w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","load-profile");EndRow();
        ImGui::TextWrapped("%s | %d points",g.value("source_file",std::string("Embedded table")).c_str(),int(g.value("coordinate",Json::array()).size()));
        bool log=g.value("interpolation_logarithmic",density);if(Toggle("Logarithmic interpolation",log)){g["interpolation_logarithmic"]=log;changed=true;}
    } else {
        auto number=[&](const char *key,const char *label,double fallback,double factor=1.) {
            if(!g.contains(key))g[key]=fallback;
            const char *valueKey=factor==scale?(density?"Nt_m3":parameter.c_str()):key;Json value={{valueKey,g.at(key)}};
            ImGui::PushID(key);
            if(Number(label,value,valueKey,factor)){g[key]=value.at(valueKey);changed=true;}ImGui::PopID();
        };
        number("left",("Left / amplitude ["+unit+"]").c_str(),0.,scale);number("right",("Right ["+unit+"]").c_str(),0.,scale);
        const auto law=g.at("law").get<std::string>();
        if(law=="parabolic")number("bowing",("Bowing ["+unit+"]").c_str(),0.,scale);
        if(law=="parabolic2"){number("midpoint","Middle coordinate [x/d or y]",.5);number("middle",("Middle value ["+unit+"]").c_str(),0.,scale);}
        if(law=="exponential"){number("background",("Background ["+unit+"]").c_str(),0.,scale);number("length_left","Left length [x/d or y]",1.);number("length_right","Right length [x/d or y]",1.);}
        if(law=="beta"){number("beta_a","Beta a [1]",1.);number("beta_b","Beta b [1]",1.);}
        if(law=="power")number("power","Power [1]",1.);
        if(law=="gaussian"){number("background",("Background ["+unit+"]").c_str(),0.,scale);number("gaussian_center","Centre [x/d or y]",.5);number("gaussian_width","Sigma [x/d or y]",.1);}
        if(law=="gauss_diffusion" || law=="erfc_diffusion") {
            number("diffusion_depth_m","Signed diffusion depth [um]",-1e-6,1e6);
            if(law=="gauss_diffusion"){number("projected_range_m","Projected range [um]",0.,1e6);number("dose_m2","Dose [cm^-2]",1e14,1e-4);}
            else number("surface_density_m3","Surface density [cm^-3]",1e20,1e-6);
        }
    }
    if(parameter=="Nd_m3" || parameter=="Na_m3" || parameter=="Nt_m3") {bool dependent=g.value("composition_dependent",false);if(Toggle("Depends on composition y",dependent)){g["composition_dependent"]=dependent;changed=true;}}
    try {
        const auto layerId=w->Current().document.at("layers")[w->Current().selectedLayer].at("scapslab_id").get<std::string>();
        const auto &document=numericDocument?*numericDocument:w->Current().document;
        auto displayProfile=g;
        if(parameter!="composition" && parameter!="Nd_m3" && parameter!="Na_m3" && parameter!="Nt_m3") {
            displayProfile["composition_dependent"]=true;ImGui::TextDisabled("Material P(y): x is mapped through composition y(x).");
        }
        auto samples=ProfileSamples(ResolveSharedProfile(displayProfile,document,layerId,parameter),thickness,ResolveSharedProfile(composition,document,layerId,"composition"));
        samples["parameter_id"]=parameter;
        for(const auto &warning:g.value("scapslab_warnings",Json::array()))ImGui::TextWrapped("File warning: %s",warning.get<std::string>().c_str());
        auto x=samples.at("x_m").get<std::vector<double>>(),y=samples.at("value_SI").get<std::vector<double>>();for(auto &v:x)v*=1e9;for(auto &v:y)v*=scale;
        PlotJson("Profile preview","Local x [nm]",("Value ["+unit+"]").c_str(),x,y,density,220);w->tutorialOverlay.Item(parameter=="Nt_m3"?"scapslab.Defects":"scapslab.Grading","export-profile");
        if(ImGui::BeginPopupContextItem("Export profile")) {
            ImGui::BeginDisabled(w->Busy());
            for(const auto *format:{"csv","grd"}) if(ImGui::MenuItem((std::string("Export .")+format).c_str())) {
                try {if(const auto path=SaveFileDialog({"Profile",format},(std::string("profile.")+format).c_str())) {
                    Json request={{"operation","export_profile"},{"layer_id",layerId},{"parameter_id",parameter},{"path",path->Utf8()}};
                    if(parameter=="Nt_m3")request["defect_id"]=w->Current().document.at("layers")[w->Current().selectedLayer].at("defects")[selectedDefect].at("scapslab_id");
                    Send(request);
                }}catch(const std::exception &e){w->status=e.what();}
            }ImGui::EndDisabled();ImGui::EndPopup();
        }
        ImGui::TextDisabled("Right-click preview to export CSV / .grd (SI values).");
    }catch(const std::exception &e){ImGui::TextWrapped("Profile error: %s",e.what());}
    ImGui::PopID();return changed;
}
bool Panel::Grading(Json &draft) {
    bool changed=false;auto &layers=draft["layers"];auto &selected=w->Current().selectedLayer;selected=std::clamp(selected,0,int(layers.size())-1);
    BeginRow("Layer");if(ImGui::BeginCombo("##layer",layers[selected].at("name").get<std::string>().c_str())) {for(int i=0;i<int(layers.size());++i) {ImGui::PushID(i);if(ImGui::Selectable(layers[i].at("name").get<std::string>().c_str(),i==selected)) selected=i;ImGui::PopID();}ImGui::EndCombo();}EndRow();
    auto &l=layers[selected];BeginRow("Parameter (SI / eV)");const bool parameterOpen=ImGui::BeginCombo("##parameter",gradingParameter.c_str());w->tutorialOverlay.Item("scapslab.Grading","parameter");if(parameterOpen) {for(const auto *key:{"composition","Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"}) if(ImGui::Selectable(key,gradingParameter==key)) gradingParameter=key;ImGui::EndCombo();}EndRow();
    bool enabled=l.contains("grading") && l["grading"].contains(gradingParameter);
    if(Toggle("Grade parameter",enabled)) {if(enabled) {const auto &obj=(gradingParameter=="Nd_m3" || gradingParameter=="Na_m3")?l.at("doping"):l.at("material");const double value=gradingParameter=="composition"?.5:obj.at(gradingParameter).get<double>();l["grading"][gradingParameter]={{"law","linear"},{"left",value},{"right",value}};}else {l["grading"].erase(gradingParameter);if(l["grading"].empty()) l.erase("grading");}changed=true;}
    if(enabled) {
        const auto composition=l["grading"].value("composition",Json::object());
        changed|=ProfileEditor(l["grading"][gradingParameter],l.at("thickness_m"),gradingParameter,composition,gradingParameter=="Nd_m3" || gradingParameter=="Na_m3");
        if(gradingParameter=="composition") {
            ImGui::TextUnformatted("Derived material parameters (left / right, SI / eV)");
            for(const auto &[key,value]:l.at("material").items())if(value.is_number()) {
                try {double left=value,right=value;if(l["grading"].contains(key)){auto profile=l["grading"].at(key);profile["composition_dependent"]=true;const auto sample=ProfileSamples(profile,l.at("thickness_m"),l["grading"]["composition"]);left=sample["value_SI"].front();right=sample["value_SI"].back();}BeginRow(key.c_str());ImGui::Text("%.6g / %.6g",left,right);EndRow();}catch(const std::exception &e){ImGui::TextWrapped("%s",e.what());}
            }
        }
    }
    changed|=Structure(draft);return changed;
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
    if(w->pendingDefect>=0 && w->pendingSheet==sheet) {selectedDefect=w->pendingDefect;w->pendingDefect=-1;}
    if(ImGui::Button("Add defect")) {selectedDefect=AddDefectToDraft(draft,selected,sheet);changed=true;++gesture;}
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
        bool spatial=d.contains("density_profile_m3");if(Toggle("Spatial Nt(x)",spatial)){if(spatial)d["density_profile_m3"]={{"law","uniform"},{"left",d.at("Nt_m3")},{"right",d.at("Nt_m3")}};else d.erase("density_profile_m3");changed=true;}
        const bool opticalOpen=ImGui::CollapsingHeader("Optical capture / IPV");w->tutorialOverlay.Item("scapslab.Defects","optical-capture");
        if(opticalOpen) {
            auto &opt=d["optical"];if(!opt.is_object())opt=Json::object();bool enabled=opt.value("enabled",false);if(Toggle("Optical capture enabled",enabled)){opt["enabled"]=enabled;changed=true;}
            changed|=OpticalFile(opt,"electron_file","electron_table_nm_m2","opt","Electron capture [.opt]");
            changed|=OpticalFile(opt,"hole_file","hole_table_nm_m2","opt","Hole capture [.opt]");
        }
        if(spatial)changed|=ProfileEditor(d["density_profile_m3"],layers[selected].at("thickness_m"),"Nt_m3",layers[selected].value("grading",Json::object()).value("composition",Json::object()),true);
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
    if(!BeginSavedPlot(*w,"Nt(E) and occupancy",ImVec2(-1,std::max(300.f,ImGui::GetContentRegionAvail().y-8)))) {ImPlot::PopStyleVar();return;}
    ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);
    ImPlot::SetupAxes("E - Ev [eV]",sheet?"Dit [cm^-2/eV]":"Nt [cm^-3/eV]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
    ImPlot::SetupAxis(ImAxis_Y2,"Occupation fraction",ImPlotAxisFlags_AuxDefault);ImPlot::SetupAxisLimits(ImAxis_Y2,-.05,1.05,ImGuiCond_Always);
    bool edgesDrawn=false;
    for(const auto &c:point.at("defect_previews")) {
        if(c.at("sheet").get<bool>()!=sheet) continue;
        if(sheet?c.at("left_layer_index")!=w->Current().selectedInterface:c.at("layer_id")!=w->Current().document.at("layers")[w->Current().selectedLayer].at("scapslab_id")) continue;
        const auto id=c.at("id").get<std::string>();const auto *xp=&Vec(c.at("energy_eV")),*yp=&Vec(c.at(sheet?"density_m2_eV":"density_m3_eV"),sheet?1e-4:1e-6);
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);
        if(c.at("single").get<bool>()) {const double e=c.at("level_energy_eV");ImPlot::PlotInfLines((id+" (discrete Nt)").c_str(),&e,1);if(c.contains("second_level_energy_eV")) {const double e2=c.at("second_level_energy_eV");ImPlot::PlotInfLines((id+" second transition").c_str(),&e2,1);}}
        else ImPlot::PlotLine(id.c_str(),xp->data(),yp->data(),int(xp->size()));
        if(!edgesDrawn) {const auto &edges=Vec(c.at("band_edges_eV"));for(int e=0;e<int(edges.size());++e) {const char *labels[]={"EvL","EcL","EvR","EcR"};ImPlot::PlotInfLines(sheet?labels[e]:e==0?"Ev":"Ec",&edges[e],1,ImPlotSpec(ImPlotProp_LineColor,ImVec4(.9f,.85f,.4f,1),ImPlotProp_LineWeight,2));ImPlot::TagX(edges[e],ImVec4(.5f,.45f,.15f,1),"%s",sheet?labels[e]:e==0?"Ev":"Ec");}edgesDrawn=true;}
        if(point.at("status")=="Converged") {
            xp=&Vec(c.at("occupation_energy_eV"));yp=&Derived(c.at("occupation"),160,[&](Cols &o){for(const auto &v:c.at("occupation")) o.a.push_back(v.is_number()?v.get<double>():NAN);}).a;
            ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);ImPlotSpec occupationStyle(ImPlotProp_LineWeight,2,ImPlotProp_Marker,ImPlotMarker_None);ImPlot::PlotLine((id+(c.at("amphoteric").get<bool>() && c.at("correlated").get<bool>()?" P-":" f")).c_str(),xp->data(),yp->data(),int(xp->size()),occupationStyle);
        }
    }EndSavedPlot(*w,"Nt(E) and occupancy");ImPlot::PopStyleVar();
}
bool Panel::Structure(Json &draft) {
    auto &layers=draft["layers"];double total=0,emin=0,emax=-1e10;
    for(const auto &l:layers) {total+=l.at("thickness_m").get<double>();const auto &m=l.at("material");emin=std::min(emin,-m.at("chi_eV").get<double>()-m.at("Eg_eV").get<double>());emax=std::max(emax,-m.at("chi_eV").get<double>());}
    const bool compact=view==22 || view==5;
    if(!compact) ImGui::TextWrapped("Structure to scale | total %.5g um. Click layer/interface; drag a layer's right edge to change thickness.",total*1e6);
    const auto origin=ImGui::GetCursorScreenPos();const float width=std::max(1.f,ImGui::GetContentRegionAvail().x),height=compact?40.f:view==14?std::max(190.f,ImGui::GetContentRegionAvail().y-12):190.f;
    auto *draw=ImGui::GetWindowDrawList();double offset=0;bool changed=false;
    auto y=[&](double e){return origin.y+16+float((emax-e)/(emax-emin))*std::max(78.f,height-120);};
    for(int i=0;i<int(layers.size());++i) {
        auto &l=layers[i];const auto &m=l.at("material");const float x1=origin.x+float(offset/total)*width;offset+=l.at("thickness_m").get<double>();const float x2=origin.x+float(offset/total)*width;
        draw->AddRectFilled(ImVec2(x1,origin.y),ImVec2(x2,origin.y+height),LayerStripColour(l));
        if(w->Current().selectedInterface<0 && w->Current().selectedLayer==i) draw->AddRect(ImVec2(x1+1,origin.y+1),ImVec2(x2-1,origin.y+height-1),IM_COL32(255,190,70,255),0,0,2.f); // selection = border, never a different fill
        const double ec=-m.at("chi_eV").get<double>(),ev=ec-m.at("Eg_eV").get<double>();
        if(!compact) {draw->AddLine(ImVec2(x1,y(ec)),ImVec2(x2,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(ev)),ImVec2(x2,y(ev)),IM_COL32(255,173,110,255),2);
        if(i>0) {const auto &a=layers[i-1].at("material");const double prevEc=-a.at("chi_eV").get<double>();draw->AddLine(ImVec2(x1,y(prevEc)),ImVec2(x1,y(ec)),IM_COL32(110,200,255,255),2);draw->AddLine(ImVec2(x1,y(prevEc-a.at("Eg_eV").get<double>())),ImVec2(x1,y(ev)),IM_COL32(255,173,110,255),2);}
        }
        draw->PushClipRect(ImVec2(x1+2,origin.y),ImVec2(x2-2,origin.y+height),true);
        draw->AddText(ImVec2(x1+5,origin.y+(compact?4:height-90)),IM_COL32_WHITE,l.at("name").get<std::string>().c_str());
        char thickness[80];std::snprintf(thickness,sizeof(thickness),"%.4g um",l.at("thickness_m").get<double>()*1e6);
        if(x2-x1>ImGui::CalcTextSize(thickness).x+10) draw->AddText(ImVec2(x1+5,origin.y+(compact?20:height-50)),IM_COL32_WHITE,thickness);
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
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s) | %.6g um | chi %.4g eV | Eg %.4g eV\nClick: select layer. Drag right edge: thickness (total %.5g um).",l.at("name").get<std::string>().c_str(),LayerType(l)>0?"n":LayerType(l)<0?"p":"i",l.at("thickness_m").get<double>()*1e6,m.at("chi_eV").get<double>(),m.at("Eg_eV").get<double>(),total*1e6);
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
// Layer-edge drag: edit a local preview while the mouse is down, send ONE edit (one undo step, one recalculation) on release.
void Panel::StructureEdit() {
    static Json dragDoc;static std::string dragDevice;
    auto &device=w->Current();
    if(!dragDoc.empty() && dragDevice!=device.id) dragDoc=Json();
    const bool held=ImGui::IsMouseDown(ImGuiMouseButton_Left);
    auto draft=dragDoc.empty()?device.document:dragDoc;
    const bool changed=Structure(draft);
    if(changed && held) {dragDoc=std::move(draft);dragDevice=device.id;return;}
    if(changed) {dragDoc=Json();Send({{"operation","edit"},{"document",draft},{"gesture_id",std::to_string(gesture)}});return;}
    if(!dragDoc.empty() && !held) {auto doc=std::move(dragDoc);dragDoc=Json();if(doc!=device.document)Send({{"operation","edit"},{"document",doc},{"gesture_id",std::to_string(gesture)}});}
}
void Panel::Results() {
    const Json *selected=nullptr;
    for(const auto &run:w->Current().history) if(run.at("run_id")==w->Current().selectedRun && (run.at("kind")=="iv" || run.at("kind")=="band")) {
        if(!run.value("profiles_stored",true) && !HistoryResult(w->Current(),run).at("points").front().contains("x_m")){ImGui::TextUnformatted("Profiles not stored. Recalculate this model to view spatial profiles.");return;}
        selected=&run;
    }
    const bool bandOnly=view==5 && w->Current().result.empty() && !w->Current().bandResult.empty();
    const auto &result=selected?HistoryResult(w->Current(),*selected):bandOnly?*w->Current().bandResult:*w->Current().result;
    const auto profileKind=selected?selected->at("kind").get<std::string>():bandOnly?"band":"iv";
    const auto &layers=(selected?selected->at("document"):w->Current().document).at("layers");
    double total=0;for(const auto &l:layers) total+=l.at("thickness_m").get<double>();
    if(view==5) StructureEdit();
    if(!result.contains("points") || result["points"].empty()) {ImGui::TextUnformatted("Calculate I-V to view results.");return;}
    const auto &points=result.at("points");int bias=bandOnly?0:w->Current().selectedBias;bias=std::clamp(bias,0,int(points.size())-1);
    { // one row: Bias point [slider showing V] | Single shot | Shots
        char biasFormat[64];std::snprintf(biasFormat,sizeof(biasFormat),"%.4g V  (#%%d)",points[bias].at("voltage_V").get<double>());
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Bias point");ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x*.5f));ImGui::SliderInt("##bias",&bias,0,int(points.size())-1,biasFormat);if(!bandOnly)w->Current().selectedBias=bias;
        ImGui::SameLine();ShotBar(*w,"iv");
    }
    const auto &band=w->Current().bandResult;
    const auto &p=selected?points[bias]:view==5?PointAtBias(w->Current().result,band,bias):points[bias];
    const bool preview=view==5 && !band.empty() && &p==&band.at("points")[0];
    const auto &metrics=preview?(const Json &)band:result;
    w->Current().biasVoltage_V=p.at("voltage_V");
    {   // one small grey status line
        const double jAbs=p.at("J_A_m2").is_number()?std::abs(p.at("J_A_m2").get<double>()):0;char residual[80]="";
        if(jAbs>0 && p.contains("residual")) std::snprintf(residual,sizeof(residual)," | residual %.2e | dJ/J %.1e",p.at("residual").get<double>(),p.at("conservation_error_A_m2").get<double>()/jAbs);
        ImGui::TextDisabled("%.0f ms | %d nodes | terminal %.5g V | internal %.5g V | %s | %d it%s",metrics.value("compute_time_ms",0.0),metrics.value("mesh_node_count",0),p.at("terminal_voltage_V").get<double>(),p.at("internal_voltage_V").get<double>(),p.at("status").get<std::string>().c_str(),p.at("iterations").get<int>(),residual);
    }
    if(view==10) {
        Toggle("Log |J|",absoluteLog);
        const auto &ivc=IvCols(points,absoluteLog);const auto &v=ivc.a,&j=ivc.b;const int failed=ivc.n;
        ImGui::TextWrapped("Curve validity: %s | %d / %d valid points; invalid points masked",result.value("validity_status",std::string("unknown")).c_str(),int(points.size())-failed,int(points.size()));
        if(ImGui::CollapsingHeader("Per-point convergence")) {
            for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %s | %s",pt.at("bias_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("status").get<std::string>().c_str(),pt.at("validity_status").get<std::string>().c_str());
        }
        if(BeginSavedPlot(*w,"I-V",ImVec2(-1,-1))) {
            ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
            if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
            HistoryLines(*w,w->Current(),"iv","voltage_V","J_A_m2",absoluteLog);if(!Archived(w->Current(),result,"iv")){StaleDim dim(w->Current());ImPlot::PlotLine("I-V",v.data(),j.data(),int(v.size()),LiveStyle(w->Current(),2.f));}
            PreviewOverlay(w->Current(),absoluteLog);ImPlot::PlotScatter("Selected bias",&v[bias],&j[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
            double selectedVoltage=v[bias];
            if(ImPlot::DragLineX(1,&selectedVoltage,ImVec4(1,.7f,.3f,1))) {
                bias=int(std::min_element(v.begin(),v.end(),[&](double a,double b){return std::abs(a-selectedVoltage)<std::abs(b-selectedVoltage);})-v.begin());
            }
            w->Current().selectedBias=bias;w->Current().biasVoltage_V=v[bias];
            EndSavedPlot(*w,"I-V");
        }
        return;
    }
    if(p.at("status")!="Converged" || p.at("J_A_m2").is_null()) {ImGui::TextWrapped("This bias did not converge; profiles are masked. Select a converged bias.");return;}
    const auto &x=Vec(p.at("x_m"),1e6);
    if(x.empty()) return;
    // Bands and Carriers share the free height; the splitter fraction is kept in project.ui["band_split"].
    const float splitterH=8.f,availH=std::max(160.f,ImGui::GetContentRegionAvail().y-splitterH-2*ImGui::GetStyle().ItemSpacing.y);
    float split=std::clamp(w->project.ui.value("band_split",.5f),.15f,.85f);
    const float h=std::max(60.f,availH*split),h2=std::max(60.f,availH-h);
    const auto &contactDoc=(selected?selected->at("document"):w->Current().document).at("contacts");
    if(BeginSavedPlot(*w,"Bands / contacts",ImVec2(-1,h))) {
        ImPlot::SetupAxes("Position [um]","Energy [eV]");ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        const double end=total*1e6,metal=end*.04;
        FitWhenChanged("bands",ImAxis_X1,end,-metal,end+metal);
        {double lo=1e300,hi=-1e300;for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) for(double v:Vec(p.at(key))) if(std::isfinite(v)){lo=std::min(lo,v);hi=std::max(hi,v);}
        if(hi>lo){const double m=.05*(hi-lo);FitWhenChanged("bands",ImAxis_Y1,lo*7.31+hi+p.at("voltage_V").get<double>()*1e-3,lo-m,hi+m);}}
        for(const auto *key:{"Ec_eV","Ev_eV","EFn_eV","EFp_eV"}) {const bool dashed=key[1]=='F';HistoryProfiles(*w,key,1.,false,dashed);if(Archived(w->Current(),result,profileKind))continue;const auto &y=Vec(p.at(key));if(y.size()==x.size()){const auto c=XColour(key);ImPlot::PlotLine(key,x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(c.x,c.y,c.z,dashed?.6f:1.f),ImPlotProp_LineWeight,2.f});if(dashed){ImPlot::PushPlotClipRect();DashedLine(x,y,c);ImPlot::PopPlotClipRect();}}}
        const auto limits=ImPlot::GetPlotLimits();auto *draw=ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for(int side=0;side<2;++side) {
            const double lo=side?end:-metal,hi=side?end+metal:0;
            const auto a=ImPlot::PlotToPixels(lo,limits.Y.Max),b=ImPlot::PlotToPixels(hi,limits.Y.Min);
            draw->AddRectFilled(a,b,IM_COL32(255,176,66,38));
            const double phi=result.at("contact_phi_eV")[side];
            const double chi=layers[side?layers.size()-1:0].at("material").at("chi_eV");
            const auto &ec=Vec(p.at("Ec_eV"));
            if(!ec.empty()) {
                const double ef=(side?ec.back():ec.front())+chi-phi;const double xx[]={lo,hi},yy[]={ef,ef};
                const bool flatContact=contactDoc.at(side?"right":"left").value("flat_band",false);
                ImPlot::PlotLine(side?"Right contact Fermi level (phi_m)":"Left contact Fermi level (phi_m)",xx,yy,2);
                ImPlot::Annotation((lo+hi)/2,ef,ImVec4(1,.8f,.4f,1),ImVec2(side?-90.f:5.f,-28.f),true,"%s contact phi_m = %.3f eV%s",side?"Right":"Left",phi,flatContact?" (flat band)":"");
            }
        }
        ImPlot::PopPlotClipRect();DrawLayerRegions(layers);EndSavedPlot(*w,"Bands / contacts");
    }
    ImGui::InvisibleButton("##band-split",ImVec2(-1,splitterH));
    if(ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if(ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y!=0) w->project.ui["band_split"]=std::clamp(split+ImGui::GetIO().MouseDelta.y/availH,.15f,.85f);
    if(ImGui::IsItemDeactivated()) {w->projectDirty=true;w->RefreshDirty();}
    {const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const float cy=(a.y+b.y)*.5f;ImGui::GetWindowDrawList()->AddLine({a.x+4,cy},{b.x-4,cy},ImGui::IsItemHovered()||ImGui::IsItemActive()?IM_COL32(255,190,70,255):IM_COL32(110,110,120,200),2.f);}
    if(BeginSavedPlot(*w,"Carriers",ImVec2(-1,h2))) {
        ImPlot::SetupAxes("Position [um]","Density [cm^-3]");ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_Y1,[](double value,char *buffer,int size,void*) {return std::snprintf(buffer,size,"1e%.0f",std::log10(value));});
        ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);FitWhenChanged("carriers",ImAxis_X1,total*1e6,0,total*1e6);
        {double lo=1e300,hi=0;for(const auto *key:{"n_m3","p_m3"}) for(double v:Vec(p.at(key))) if(std::isfinite(v) && v>0){lo=std::min(lo,v*1e-6);hi=std::max(hi,v*1e-6);}
        if(hi>0)FitWhenChanged("carriers",ImAxis_Y1,std::log10(lo)*7.31+std::log10(hi)+p.at("voltage_V").get<double>()*1e-3,std::max(lo,hi*1e-16),hi*3);}
        for(const auto *key:{"n_m3","p_m3"}) {HistoryProfiles(*w,key,1e-6);if(Archived(w->Current(),result,profileKind))continue;const auto &y=Vec(p.at(key),1e-6);if(y.size()==x.size())ImPlot::PlotLine(key==std::string("n_m3")?"n":"p",x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,XColour(key),ImPlotProp_LineWeight,2.f});}
        DrawLayerRegions(layers);EndSavedPlot(*w,"Carriers");
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
    ImGui::EndDisabled();ImGui::SameLine();ShotBar(*w,kind);
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
    const int failed=AcCols(points,view).n;
    const char *key=view==7?"inverse_C2_F_minus2":view==9?"G_over_omega_F":"capacitance_F";
    if(failed) ImGui::TextWrapped("%d invalid points (convergence/current conservation); raw CSV rows retain validity flags.",failed);
    if(ImGui::CollapsingHeader("Per-point convergence"))
        for(const auto &pt:points) ImGui::TextWrapped("%s | %.5g V | %.5g Hz | %s | %s",pt.at("point_id").get<std::string>().c_str(),pt.at("voltage_V").get<double>(),pt.at("frequency_Hz").get<double>(),pt.at("status").get<std::string>().c_str(),pt.at("validity_status").get<std::string>().c_str());
    const char *ylabel=view==7?"1/C^2 [F^-2]":view==9?"G/omega [F]":"C [F]";
    if(BeginSavedPlot(*w,names[view],ImVec2(-1,-1))) {
        ImPlot::SetupAxes(cv?"Voltage [V]":"Frequency [Hz]",ylabel,ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        if(view==6 || view==8) ImPlot::SetupAxis(ImAxis_Y2,"G/omega [F]",ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisFormat(ImAxis_Y1,view==8?"%.8g":"%.3g");
        if(!cv) ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->acResults.contains(kind)) {
            if(!d->history.empty()) {
                ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);HistoryLines(*w,*d,kind,cv?"voltage_V":"frequency_Hz",key);
                if(view==6 || view==8){ImPlot::SetAxes(ImAxis_X1,ImAxis_Y2);HistoryLines(*w,*d,kind,cv?"voltage_V":"frequency_Hz","G_over_omega_F");}if(Archived(*d,d->acResults.at(kind),kind))continue;
            }
            const auto &ac=AcCols(d->acResults.at(kind).at("points"),view);const auto &xx=ac.a,&yy=ac.b,&gg=ac.c;
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
    if(ImGui::Button(ICON_FA_PLUS " New device")) Send({{"operation","device_add"}});w->tutorialOverlay.Item("scapslab.Project","new");ImGui::SameLine();
    if(ImGui::Button(ICON_FA_FILE_IMPORT " Import device")) {try {if(const auto path=OpenFileDialog({"Device YAML","yaml,yml"})) Send({{"operation","device_add"},{"path",path->Utf8()}});}catch(const std::exception &e){w->status=e.what();}}
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Import a device YAML (or use File / Import SCAPS .def). Unsupported items are reported as warnings.");
    if(ImGui::Button("Undo")) Send({{"operation","undo"}});w->tutorialOverlay.Item("scapslab.Project","undo");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (also restores a deleted device)");
    ImGui::SameLine();if(ImGui::Button("Redo")) Send({{"operation","redo"}});w->tutorialOverlay.Item("scapslab.Project","redo");
    ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button(ICON_FA_PLAY " Run all checked") && !w->Busy()) Send({{"operation","run_all"}});
    ImGui::SameLine();ImGui::TextDisabled("Compare: show in plots");
    if(w->Busy()) {
        const auto job=w->jobs->GetJob(w->active);
        if(w->batchTotal) ImGui::ProgressBar(float(w->batchCompleted)/float(w->batchTotal),ImVec2(-90,0),(std::to_string(w->batchCompleted)+" / "+std::to_string(w->batchTotal)+" devices").c_str());
        else ImGui::ProgressBar(job && job->totalWork>0?float(job->completedWork/job->totalWork):0.f,ImVec2(-90,0),job?job->currentStage.c_str():"");
        ImGui::SameLine();if(ImGui::Button("Cancel")) Send({{"operation","cancel"}});
    } else if(w->dirty || !w->status.empty()) ImGui::TextDisabled("%s%s",w->dirty?"* unsaved  ":"",w->status.c_str());
    ImGui::Separator();
    const auto select=[&](const DeviceState &d) {if(w->project.active_device!=d.id) Send({{"operation","device_select"},{"device_id",d.id}});};
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
                if(ImGui::BeginDragDropTarget()) {if(const auto *payload=ImGui::AcceptDragDropPayload("SCAPSLAB_DEVICE")) Send({{"operation","device_move"},{"device_id",static_cast<const char*>(payload->Data)},{"index",int(&dp-&devices[0])}});ImGui::EndDragDropTarget();}
                if(ImGui::BeginPopupContextItem("device-menu")) {
                    select(d);
                    if(ImGui::MenuItem("Rename...")) {renameId=d.id;std::snprintf(renameBuf,sizeof(renameBuf),"%s",d.name.c_str());ImGui::CloseCurrentPopup();openRename=true;}
                    if(ImGui::MenuItem("Duplicate")) Send({{"operation","device_duplicate"},{"device_id",d.id}});
                    if(ImGui::MenuItem("Delete",nullptr,false,devices.size()>1)) Send({{"operation","device_remove"},{"device_id",d.id}});
                    ImGui::Separator();
                    if(ImGui::MenuItem("Add layer")) {Send({{"operation","layer"},{"action","add"}});go(1);}
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
                    else if(entered || ImGui::IsItemDeactivated()) {if(renameBuf[0] && d.name!=renameBuf) Send({{"operation","device_update"},{"device_id",d.id},{"name",renameBuf}});inlineId.clear();}
                } else {
                    if(compare) ImGui::TextUnformatted((d.name+(d.dirty?" *":"")).c_str());else ImGui::TextDisabled("%s",(d.name+(d.dirty?" *":"")).c_str());
                    const bool dbl=ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                    const bool f2=active && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2);
                    if(dbl || f2) {inlineId=d.id;focusInline=true;std::snprintf(renameBuf,sizeof(renameBuf),"%s",d.name.c_str());}
                }
                if(w->Busy() && w->pendingDevice==d.id) {
                    const auto job=w->jobs->GetJob(w->active);ImGui::SameLine(0,6);
                    ImGui::ProgressBar(job && job->totalWork>0?float(job->completedWork/job->totalWork):0.f,ImVec2(48,box*.35f),"");
                }
                ImGui::SameLine();ImGui::SetCursorPosX(rowRight-box);
                ImGui::BeginDisabled(w->Busy());if(ImGui::Checkbox("##compare",&compare)) Send({{"operation","device_update"},{"device_id",d.id},{"compare",compare}});ImGui::EndDisabled();
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Compare: show in plots");
                if(open) {
                    const auto &doc=d.document;
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
                                            if(payload->IsDelivery()) {select(d);Send({{"operation","layer"},{"action","move"},{"layer_id",from},{"index",i}});}
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                            }
                            if(ImGui::BeginPopupContextItem("layer-menu")) {
                                select(d);w->Current().selectedLayer=i;
                                if(ImGui::MenuItem("Add defect")) {AddDefectTo(false,i);go(11);}
                                if(ImGui::MenuItem("Add layer")) {Send({{"operation","layer"},{"action","add"}});go(1);}
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
                                            if(payload->IsDelivery()) {select(d);Send({{"operation","layer"},{"action","move"},{"layer_id",from},{"index",target}});}
                                        }
                                    }
                                    ImGui::EndDragDropTarget();
                                }
                                if(ImGui::BeginPopupContextItem("interface-menu")) {
                                    select(d);w->Current().selectedInterface=i;w->Current().selectedLayer=i;
                                    if(ImGui::MenuItem("Add interface defect")) {AddDefectTo(true,i);go(12);}
                                    if(ImGui::MenuItem("Remove defects")) {auto draft=d.document;auto &arr=draft["interface_defects"];for(int k=int(arr.size())-1;k>=0;--k) if(arr[k].value("left_layer_index",-1)==i) arr.erase(arr.begin()+k);Send({{"operation","edit"},{"document",draft}});}
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
                                if(ImGui::MenuItem(("Pin (Single shot)"+count).c_str()))each({{"pinned",true}});
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
            if(renameId.empty()) Send({{"operation","project_edit"},{"name",renameBuf}});else Send({{"operation","device_update"},{"device_id",renameId},{"name",renameBuf}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void Panel::AddDefectTo(bool sheet,int layer) {
    auto draft=w->Current().document;if(!draft.is_object() || !draft.contains("layers") || layer<0 || layer>=int(draft["layers"].size()) || (sheet && layer+1>=int(draft["layers"].size()))) return;
    auto d=NewDefect(sheet);unsigned n=1;std::set<std::string> ids;
    if(sheet) for(const auto &v:draft.value("interface_defects",Json::array())) ids.insert(v.at("id"));
    else for(const auto &l:draft["layers"]) for(const auto &v:l["defects"]) ids.insert(v.at("scapslab_id"));
    const std::string prefix=sheet?"interface-defect-":"defect-";
    while(ids.contains(prefix+std::to_string(n))) ++n;
    d[sheet?"id":"scapslab_id"]=prefix+std::to_string(n);
    if(sheet) {d["left_layer_index"]=layer;if(!draft.contains("interface_defects") || !draft["interface_defects"].is_array()) draft["interface_defects"]=Json::array();draft["interface_defects"].push_back(d);selectedDefect=int(draft["interface_defects"].size())-1;}
    else {draft["layers"][layer]["defects"].push_back(d);selectedDefect=int(draft["layers"][layer]["defects"].size())-1;}
    Send({{"operation","edit"},{"document",draft}});
}
void Panel::Compare() {
    absoluteLog=w->project.ui.value("iv_absolute_log",true);
    static bool historyOpen=false; // one compact row: Run history toggle | Log |J| | Run all checked | Single shot | Shots
    if(ImGui::Button(historyOpen?"Run history  v":"Run history  >"))historyOpen=!historyOpen;
    ImGui::SameLine();
    if(ImGui::Checkbox("Log |J|",&absoluteLog)) {auto ui=w->project.ui;ui["iv_absolute_log"]=absoluteLog;Send({{"operation","project_edit"},{"ui",ui}});}TrackGesture();
    ImGui::SameLine();ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Run all checked")) Send({{"operation","run_all"}});ImGui::EndDisabled();ImGui::SameLine();ShotBar(*w,"iv");
    if(historyOpen) DrawHistory(*w);
    bool any=false;for(const auto &d:w->project.devices) any|=(w->project.compare_set.contains(d->id) || d->id==w->project.active_device) && (!d->result.empty() || !d->history.empty() || d->liveRunning);
    if(!any) {ImGui::TextWrapped("Tick 'Compare' on devices in the Project tree to overlay them.");return;}
    bool light=false;for(const auto &d:w->project.devices) light|=w->project.compare_set.contains(d->id) && d->result.contains("photovoltaic");
    if(light && ImGui::BeginTable("Solar metrics",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        for(const auto *label:{"Device","Voc [V]","Jsc [mA/cm^2]","FF [%]","Efficiency [%]"}) ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
        for(const auto &d:w->project.devices) if(w->project.compare_set.contains(d->id) && d->result.contains("photovoltaic")) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(d->name.c_str());
            for(const auto *key:{"Voc_V","Jsc_A_m2","FF_fraction","efficiency_fraction"}) {ImGui::TableNextColumn();const auto &value=d->result.at("photovoltaic").at(key);if(value.is_number()) ImGui::Text(std::string(key)=="Voc_V"?"%.4f":"%.4g",value.get<double>()*(std::string(key)=="Jsc_A_m2"?.1:std::string(key)=="Voc_V"?1:100));else ImGui::TextUnformatted("--");}
        }ImGui::EndTable();
    }
    if(BeginSavedPlot(*w,"I-V compare",ImVec2(-1,-1))) {
        ImPlot::SetupAxes("Voltage [V]",absoluteLog?"|J| [A/m^2]":"Signed J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        if(absoluteLog) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        for(const auto &d:w->project.devices) if((w->project.compare_set.contains(d->id) || d->id==w->project.active_device) && (!d->result.empty() || !d->history.empty() || d->liveRunning)) {
            if(d->result.empty()){HistoryLines(*w,*d,"iv","voltage_V","J_A_m2",absoluteLog);if(d->id==w->project.active_device)PreviewOverlay(*d,absoluteLog);continue;}
            const auto &ivc=IvCols(d->result.at("points"),absoluteLog);const auto &x=ivc.a,&y=ivc.b;

            if(!d->history.empty()){HistoryLines(*w,*d,"iv","voltage_V","J_A_m2",absoluteLog);if(Archived(*d,d->result,"iv"))continue;}
            const float c[4]={d->colour[0].get<float>(),d->colour[1].get<float>(),d->colour[2].get<float>(),d->colour[3].get<float>()};
            static std::string label;label=d->name;label+="##";label+=d->id; // reused buffer: no per-frame allocation
            {StaleDim dim(*d);ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),{ImPlotProp_LineColor,ImVec4(c[0],c[1],c[2],SelectedOffLive(*d,"iv")?.3f:c[3]),ImPlotProp_LineWeight,2.5f});}
            if(d->id==w->project.active_device) PreviewOverlay(*d,absoluteLog);
            if(d->id==w->project.active_device && !x.empty()) {
                int bias=std::clamp(d->selectedBias,0,int(x.size())-1);double voltage=x[bias];
                ImPlot::PlotScatter("Selected bias",&x[bias],&y[bias],1,{ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,6.f});
                const auto nearest=[&](double at){return int(std::min_element(x.begin(),x.end(),[&](double a,double b){return std::abs(a-at)<std::abs(b-at);})-x.begin());};
                if(ImPlot::DragLineX(1,&voltage,ImVec4(c[0],c[1],c[2],c[3]))) bias=nearest(voltage);
                else if(ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(0) && ImGui::GetIO().MouseDragMaxDistanceSqr[0]<16.f) bias=nearest(ImPlot::GetPlotMousePos().x); // click selects the nearest bias point
                d->selectedBias=bias;d->biasVoltage_V=x[bias];
            }
        }EndSavedPlot(*w,"I-V compare");
    }
}

}






namespace SST::ScapsLab {
namespace {
std::string MemberLabel(const Json &row,const Json &spec) {
    std::string label=row.at("id").get<std::string>();
    for(const auto &a:spec.at("axes")) {const auto path=a.at("path").get<std::string>();label+=" | "+ParameterLabel(path,a.value("unit",std::string("1")),row.at("parameters").at(path));}
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
    if(table.at("columns").empty())return;recorderPlot=w->project.ui.value("recorder_plot",0);recorderPlot=std::clamp(recorderPlot,0,int(table.at("columns").size())-1);BeginRow("Plot scalar");if(ImGui::BeginCombo("##quantity",table.at("columns")[recorderPlot].at("id").get<std::string>().c_str())) {for(int i=0;i<int(table.at("columns").size());++i)if(ImGui::Selectable(table.at("columns")[i].at("id").get<std::string>().c_str(),i==recorderPlot))recorderPlot=i;ImGui::EndCombo();}EndRow();
    w->project.ui["recorder_plot"]=recorderPlot;
    const auto &c=table.at("columns")[recorderPlot];const auto &axes=table.at("spec").at("axes");const auto path=axes[0].at("path").get<std::string>();
    if(BeginSavedPlot(*w,"Recorder vs parameter",ImVec2(-1,std::max(220.f,ImGui::GetContentRegionAvail().y-5)))) {ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);ImPlot::SetupAxes((path+" ["+axes[0].at("unit").get<std::string>()+"]").c_str(),(c.at("id").get<std::string>()+" ["+c.at("unit").get<std::string>()+"]").c_str(),ImPlotAxisFlags_None,ImPlotAxisFlags_None);if(axes[0].value("spacing",std::string{})=="log")ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        std::map<std::string,std::vector<std::pair<double,double>>> groups;for(const auto &r:rows) {std::string label=c.at("id");if(axes.size()==2) {const auto p=axes[1].at("path").get<std::string>();label=ParameterLabel(p,axes[1].at("unit"),r.at("parameters").at(p));}const auto &v=r.at("values").at(c.at("id").get<std::string>());groups[label].push_back({r.at("parameters").at(path).get<double>(),v.is_number() && r.at("statuses").at(c.at("id").get<std::string>())=="ok"?v.get<double>():NAN});}
        for(auto &[label,points]:groups) {std::sort(points.begin(),points.end(),[](auto a,auto b){return a.first<b.first;});std::vector<double>x,y;for(auto [a,b]:points){x.push_back(a);y.push_back(b);}ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),ImPlotSpec(ImPlotProp_Marker,ImPlotMarker_Circle,ImPlotProp_MarkerSize,3));}EndSavedPlot(*w,"Recorder vs parameter");}
}
void Panel::BatchPlots() {
    auto &d=w->Current();
    if(!d.liveMembers.empty()) { // members finished so far in the running batch: curves only, marked, never stored (the table replaces them when the batch ends)
        ImGui::TextColored(ImVec4(1.f,.6f,.15f,1.f),"RUNNING: %d of %d members finished (partial curves, not saved; the table follows)",int(d.liveMembers.size()),int(d.liveMemberTotal));
        if(BeginSavedPlot(*w,"Batch I-V running",ImVec2(-1,-1))) {
            ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);ImPlot::SetupAxes("Voltage [V] (left minus right)","J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
            for(const auto &m:d.liveMembers) {const auto name="member "+std::to_string(m.index+1)+" (running)##live"+std::to_string(m.index);ImPlot::PlotLine(name.c_str(),m.v.data(),m.j.data(),int(std::min(m.v.size(),m.j.size())),{ImPlotProp_LineWeight,1.5f});}
            PlotBadge("RUNNING (partial)",IM_COL32(255,140,25,230),IM_COL32(0,0,0,255),0);
            EndSavedPlot(*w,"Batch I-V running");
        }
        return;
    }
    if(d.batchResult.empty()) {ImGui::TextUnformatted("Run a batch to overlay its curves.");return;}
    const auto &table=d.batchResult;const auto &rows=table.at("rows");d.selectedMember=std::clamp(d.selectedMember,0,int(rows.size())-1);
    BeginRow("Selected member");if(ImGui::BeginCombo("##member",MemberLabel(rows[d.selectedMember],table.at("spec")).c_str())) {for(int i=0;i<int(rows.size());++i)if(ImGui::Selectable((MemberLabel(rows[i],table.at("spec"))+" | "+rows[i].at("status").get<std::string>()).c_str(),i==d.selectedMember))Send({{"operation","batch_select"},{"index",i}});ImGui::EndCombo();}EndRow();
    const auto &selected=rows[d.selectedMember];ImGui::TextWrapped("%s | %s",selected.at("status").get<std::string>().c_str(),selected.value("message",std::string{}).c_str());
    ImGui::BeginDisabled(w->Busy());if(ImGui::Button("Retry selected"))Send({{"operation","retry_batch"},{"member_id",selected.at("id")}});ImGui::SameLine();if(ImGui::Button("Export selected CSV..."))if(const auto target=SaveFileDialog({"Recorder CSV","csv"},"scapslab-member.csv"))Send({{"operation","export_batch"},{"member_id",selected.at("id")},{"path",target->Utf8()}});ImGui::EndDisabled();
    ImGui::TextWrapped("Colour: %s [%s]. Click near a curve sample to select. Failed members remain listed.",table.at("spec").at("axes")[0].at("path").get<std::string>().c_str(),table.at("spec").at("axes")[0].at("unit").get<std::string>().c_str());
    if(BeginSavedPlot(*w,"Batch I-V overlay",ImVec2(-1,-1))) {ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside);ImPlot::SetupAxes("Voltage [V] (left minus right)","J [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);double nearest=64;int hit=-1;
        for(int i=0;i<int(rows.size());++i) {const auto &r=rows[i];if(!r.contains("curves"))continue;std::vector<double>x,y;for(const auto &p:r.at("curves").at("iv")) {const double v=p.at("voltage_V");const double j=p.at("J_Am2").is_number() && p.at("status")=="Converged"?p.at("J_Am2").get<double>():NAN;x.push_back(v);y.push_back(j);if(std::isfinite(j) && ImPlot::IsPlotHovered()) {const auto pixel=ImPlot::PlotToPixels(v,j);const auto mouse=ImGui::GetMousePos();const double dist=(pixel.x-mouse.x)*(pixel.x-mouse.x)+(pixel.y-mouse.y)*(pixel.y-mouse.y);if(dist<nearest){nearest=dist;hit=i;}}}ImPlot::PlotLine(MemberLabel(r,table.at("spec")).c_str(),x.data(),y.data(),int(x.size()),ImPlotSpec(ImPlotProp_LineColor,MemberColour(r,table),ImPlotProp_LineWeight,i==d.selectedMember?3.f:2.f));}
        if(hit>=0 && ImGui::IsMouseClicked(0) && !w->Busy())Send({{"operation","batch_select"},{"index",hit}});EndSavedPlot(*w,"Batch I-V overlay");}
}
}

namespace SST::ScapsLab {
void Panel::GenerationRecombination() {
    grFollow=w->project.ui.value("gr_follow",true);grHidden=w->project.ui.value("gr_hidden",std::set<std::string>{});grBias=w->project.ui.value("gr_bias",Json{{"voltage_V",0.}});
    const int savedTab=w->project.ui.value("gr_tab",0);const bool restoreTab=grTab!=savedTab;
    auto &device=w->Current();
    StructureEdit();
    ShotBar(*w,"iv");ImGui::SameLine();
    const bool followToggled=ImGui::Checkbox("Follow I-V working point",&grFollow);TrackGesture();
    if(followToggled && !grFollow) {
        if(device.result.contains("points") && !device.result.at("points").empty()) device.biasVoltage_V=device.result.at("points")[std::clamp(device.selectedBias,0,int(device.result.at("points").size())-1)].at("voltage_V");
        grBias["voltage_V"]=device.biasVoltage_V;device.needsBand=true;device.changedAt=std::chrono::steady_clock::now();
    }
    if(grFollow && device.result.contains("points") && !device.result.at("points").empty()) {
        ImGui::SameLine();ImGui::SetNextItemWidth(std::max(100.f,ImGui::GetContentRegionAvail().x));
        {const auto &pts=device.result.at("points");const int sel=std::clamp(device.selectedBias,0,int(pts.size())-1);char fmt[64];std::snprintf(fmt,sizeof(fmt),"%.4g V  (#%%d)",pts[sel].at("voltage_V").get<double>());
        ImGui::SliderInt("##point",&device.selectedBias,0,int(pts.size())-1,fmt);}
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
    w->project.ui["gr_follow"]=grFollow;w->project.ui["gr_bias"]=grBias;
    const auto &snapshot=grFollow?device.result:device.bandResult;
    if(!snapshot.contains("points") || snapshot.at("points").empty()) {ImGui::TextWrapped("Calculate I-V or select a working voltage. %s",w->status.c_str());return;}
    const int index=grFollow?std::clamp(device.selectedBias,0,int(snapshot.at("points").size())-1):0;
    const auto &point=snapshot.at("points")[index];
    if(!point.contains("generation_recombination")) {ImGui::TextUnformatted("Recalculate this snapshot to obtain G/R.");return;}
    const auto &gr=point.at("generation_recombination");
    if(ImGui::CollapsingHeader("Details")) {
    ImGui::TextWrapped("%s | terminal %.6g V | internal %.6g V | %.5g K | solver %s | profile validity %s",point.at("bias_id").get<std::string>().c_str(),point.at("terminal_voltage_V").get<double>(),point.at("internal_voltage_V").get<double>(),snapshot.at("project_snapshot").at("temperature_K").get<double>(),point.at("status").get<std::string>().c_str(),point.at("validity_status").get<std::string>().c_str());
    ImGui::TextWrapped("%s | illumination %s | %s",device.id.c_str(),snapshot.at("project_snapshot").value("illumination",Json::object()).value("enabled",false)?"on":"off",w->status.c_str());
    ImGui::TextWrapped("Terminal budget: %s | relative error %.5g",gr.value("terminal_budget_valid",false)?"valid <= 1e-6":"NOT VALID",gr.at("terminal_budget_relative_error").is_number()?gr.at("terminal_budget_relative_error").get<double>():NAN);
    for(const auto &c:gr.at("channels")) ImGui::TextWrapped("%s / %s: %s",c.at("mechanism").get<std::string>().c_str(),c.at("id").get<std::string>().c_str(),c.at("validity_status").get<std::string>().c_str());
    ImGui::TextWrapped("Log plot shows positive rates only; signed rates and integrals are retained in export. Sheets use the right axis [m^-2 s^-1].");
    }
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
    if(ImGui::BeginTabItem("Profiles",nullptr,(gallery && !budgetShot) || (restoreTab && savedTab==0)?ImGuiTabItemFlags_SetSelected:0)) {grTab=0;w->project.ui["gr_tab"]=0;
    bool showGeneration=!grHidden.contains("generation");
    if(ImGui::Checkbox("Generation",&showGeneration)) {if(showGeneration)grHidden.erase("generation");else grHidden.insert("generation");}
    for(const auto *mechanism:{"srh_bulk","interface","radiative","auger"}) {bool show=!grHidden.contains(mechanism);ImGui::SameLine();if(ImGui::Checkbox(mechanism,&show)){if(show)grHidden.erase(mechanism);else grHidden.insert(mechanism);}}
    w->project.ui["gr_hidden"]=grHidden;
    if(!valid) ImGui::TextWrapped("Invalid working point: profiles are masked; raw values remain available in export.");
    // data windows: left axis 12 decades below the max visible bulk rate; right axis only when a sheet channel is visible
    double bulkMax=0,sheetMax=0;
    if(valid) {
        if(showGeneration) for(double v:Vec(gr.at("generation_m3_s"))) if(std::isfinite(v))bulkMax=std::max(bulkMax,v);
        for(const auto &c:gr.at("channels")) if(!grHidden.contains(c.at("mechanism").get<std::string>()) && c.value("valid",false)) {
            const bool sh=!c.at("R_sheet_m2_s").empty();for(double v:Vec(c.at(sh?"R_sheet_m2_s":"R_m3_s"))) if(std::isfinite(v))(sh?sheetMax:bulkMax)=std::max(sh?sheetMax:bulkMax,v);
        }
    }
    const double totalUm=[&]{double t=0;for(const auto &l:snapshot.at("project_snapshot").at("layers"))t+=l.at("thickness_m").get<double>()*1e6;return t;}();
    if(BeginSavedPlot(*w,"G(x), R(x)",ImVec2(-1,-1))) {
        ImPlot::SetupAxes("Position [um]","Bulk rate [m^-3 s^-1]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        FitWhenChanged("gr",ImAxis_X1,totalUm,0,totalUm);
        if(bulkMax>0)FitWhenChanged("gr",ImAxis_Y1,bulkMax,bulkMax*1e-12,bulkMax*5);
        if(sheetMax>0){ImPlot::SetupAxis(ImAxis_Y2,"Sheet rate [m^-2 s^-1]",ImPlotAxisFlags_AuxDefault);ImPlot::SetupAxisScale(ImAxis_Y2,ImPlotScale_Log10);FitWhenChanged("gr",ImAxis_Y2,sheetMax,sheetMax*1e-8,sheetMax*5);}
        auto plot=[&](const std::string &name,const Json &xx,const Json &yy,bool sheet,bool curveValid,int shade=0) {
            const auto &x=Vec(xx,1e6),&y=Vec(yy,1.,curveValid?2:3);if(x.size()!=y.size())return;
            if(sheet && sheetMax<=0)return;ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);
            const auto mech=name.substr(0,name.find(' '));const ImPlotSpec style(ImPlotProp_LineColor,XColour(mech,shade),ImPlotProp_LineWeight,2.f,ImPlotProp_MarkerFillColor,XColour(mech,shade),ImPlotProp_MarkerLineColor,XColour(mech,shade));
            if(sheet)ImPlot::PlotScatter(name.c_str(),x.data(),y.data(),int(x.size()),style);else ImPlot::PlotLine(name.c_str(),x.data(),y.data(),int(x.size()),style);
        };
        if(showGeneration)HistoryProfiles(*w,"generation_m3_s",1.,true);
        if(showGeneration && !Archived(device,snapshot,grFollow?"iv":"band"))plot("G",gr.at("x_m"),gr.at("generation_m3_s"),false,valid);
        for(const auto &run:device.history) {
            if(!run.at("visible").get<bool>() || !ShowRun(run) || run.at("run_id")==device.selectedRun || (run.at("kind")!="iv" && run.at("kind")!="band") || !HistoryResult(device,run).contains("points"))continue;
            const auto &points=HistoryResult(device,run).at("points");if(points.empty())continue;
            const auto &p=points[std::clamp(device.selectedBias,0,int(points.size())-1)];
            if(!p.contains("generation_recombination"))continue;
            const auto colour=run.at("colour").get<std::vector<float>>();const bool selectedRun=run.at("run_id")==device.selectedRun;std::map<std::string,int> shadeOf;
            for(const auto &channel:p.at("generation_recombination").at("channels")) {
                if(grHidden.contains(channel.at("mechanism").get<std::string>()))continue;
                const bool sheet=!channel.at("R_sheet_m2_s").empty();
                const auto &x=Vec(channel.at("x_m"),1e6),&y=Vec(channel.at(sheet?"R_sheet_m2_s":"R_m3_s"),1.,channel.value("valid",false)?1:3);if(x.size()!=y.size())continue;
                const auto label=channel.at("mechanism").get<std::string>()+" / "+channel.at("id").get<std::string>()+" / "+RunLabel(run)+"##"+run.at("run_id").get<std::string>();
                if(sheet && sheetMax<=0)continue;ImPlot::SetAxes(ImAxis_X1,sheet?ImAxis_Y2:ImAxis_Y1);
                const auto mechName=channel.at("mechanism").get<std::string>();const ImVec4 lc=selectedRun?XColour(mechName,shadeOf[mechName]++):ImVec4(colour[0],colour[1],colour[2],colour[3]);ImPlotSpec style(ImPlotProp_LineColor,lc,ImPlotProp_LineWeight,selectedRun?2.5f:1.f,ImPlotProp_MarkerFillColor,lc,ImPlotProp_MarkerLineColor,lc);
                if(sheet)ImPlot::PlotScatter(label.c_str(),x.data(),y.data(),int(x.size()),style);else ImPlot::PlotLine(label.c_str(),x.data(),y.data(),int(x.size()),style);
            }
        }
        std::map<std::string,int> curShade;
        for(const auto &c:gr.at("channels")) if(!Archived(device,snapshot,grFollow?"iv":"band") && !grHidden.contains(c.at("mechanism").get<std::string>())) {
            const bool sheet=!c.at("R_sheet_m2_s").empty();plot(c.at("mechanism").get<std::string>()+" / "+c.at("id").get<std::string>(),c.at("x_m"),c.at(sheet?"R_sheet_m2_s":"R_m3_s"),sheet,c.value("valid",false),curShade[c.at("mechanism").get<std::string>()]++);
        }
        ImPlot::SetAxes(ImAxis_X1,ImAxis_Y1);DrawLayerRegions(snapshot.at("project_snapshot").at("layers"));
        EndSavedPlot(*w,"G(x), R(x)");
    }
    ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Integrated currents",nullptr,budgetShot || (restoreTab && savedTab==1)?ImGuiTabItemFlags_SetSelected:0)) {grTab=1;w->project.ui["gr_tab"]=1;
    ImGui::TextWrapped("q integral G = %.8g A/m^2 | q integral R = %.8g A/m^2 | net = %.8g A/m^2",gr.at("generation_current_A_m2").get<double>(),gr.at("recombination_current_A_m2").get<double>(),gr.at("net_generation_current_A_m2").get<double>());
    ImGui::TextWrapped("Terminal budget: %s | relative error %.5g.",gr.value("terminal_budget_valid",false)?"valid <= 1e-6":"NOT VALID",gr.at("terminal_budget_relative_error").is_number()?gr.at("terminal_budget_relative_error").get<double>():NAN);
    if(gr.contains("predicted_terminal_current_A_m2")) {
        for(const auto &c:gr.at("contact_recombination")) ImGui::Text("Contact %s: electron %.8g | hole %.8g A/m^2 (signed removal)",c.at("id").get<std::string>().c_str(),c.at("electron_current_A_m2").get<double>(),c.at("hole_current_A_m2").get<double>());
        ImGui::TextWrapped("Shunt %.8g | series %.8g | predicted terminal %.8g A/m^2",gr.at("shunt_current_A_m2").get<double>(),gr.at("series_current_A_m2").get<double>(),gr.at("predicted_terminal_current_A_m2").get<double>());
        ImGui::TextWrapped("%s",gr.at("terminal_budget_identity").get<std::string>().c_str());
    } else ImGui::TextWrapped("Legacy bulk-only budget; recalculate to include contacts and external branches.");
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
    if(BeginSavedPlot(*w,"Integrated loss breakdown",ImVec2(-1,std::max(150.f,ImGui::GetContentRegionAvail().y-4)))) {
        ImPlot::SetupAxes("Mechanism index","J_R [A/m^2]",ImPlotAxisFlags_None,ImPlotAxisFlags_None);
        ImPlot::SetupLegend(ImPlotLocation_North,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
        int i=0;for(const auto &c:gr.at("channels")) {const double x=i++,y=valid?c.at("integrated_current_A_m2").get<double>():NAN;ImPlot::PlotBars((c.at("mechanism").get<std::string>()+" / "+c.at("id").get<std::string>()).c_str(),&x,&y,1,.7);}
        EndSavedPlot(*w,"Integrated loss breakdown");
    }
    ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
}
