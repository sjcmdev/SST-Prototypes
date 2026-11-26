#pragma once
#include "DockLayout.hpp"
#include "CurveMorph.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>

namespace SST::ScapsLab {
struct PlotRange {
    double min=0,max=1;
    bool Valid(bool log=false) const {return std::isfinite(min) && std::isfinite(max) && min<max && (!log || min>=1e-200);}
    bool operator==(const PlotRange &) const = default;
};
inline PlotRange PadPlotRange(PlotRange r,double percent,bool log) {
    if(!r.Valid(log)) return r;
    const double m=std::clamp(percent,0.,30.)*.01;
    if(log) {const double d=(std::log10(r.max)-std::log10(r.min))*m;r.min=std::pow(10.,std::max(-200.,std::log10(r.min)-d));r.max=std::pow(10.,std::min(std::log10(std::numeric_limits<double>::max()),std::log10(r.max)+d));}
    else {const double d=(r.max-r.min)*m;const PlotRange padded{r.min-d,r.max+d};if(padded.Valid())r=padded;}
    return r;
}
inline bool CompactBatchLegend(std::size_t count) {return count>12;}
// UI-only limits: fit in axis coordinates, so logarithmic plots ease in decades.
struct SmoothPlotAxis {
    PlotRange current,from,target;
    double elapsed=.2;
    bool initialized=false,log=false;
    void Set(PlotRange range,bool logarithmic) {
        current=from=target=range;log=logarithmic;elapsed=.2;initialized=true;
    }
    void Fit(PlotRange range,bool logarithmic,bool hold,bool manual=false) {
        if(manual || !range.Valid(logarithmic))return;
        if(!initialized || log!=logarithmic){Set(range,logarithmic);return;}
        if(hold)range={std::min({range.min,current.min,target.min}),std::max({range.max,current.max,target.max})};
        if(range!=target){from=current;target=range;elapsed=0;}
    }
    PlotRange Step(double dt,bool hold,bool manual=false) {
        if(!initialized || manual)return current;
        if(hold){const PlotRange expanded{std::min(current.min,target.min),std::max(current.max,target.max)};if(expanded!=target){from=current;target=expanded;elapsed=0;}}
        elapsed=std::min(.2,elapsed+std::max(0.,dt));
        const double t=elapsed/.2,weight=t*t*(3-2*t);
        const auto ease=[&](double a,double b){return log?std::pow(10.,std::lerp(std::log10(a),std::log10(b),weight)):std::lerp(a,b,weight);};
        current=elapsed>=.2?target:PlotRange{ease(from.min,target.min),ease(from.max,target.max)};
        return current;
    }
};
struct PlotView {
    std::array<std::optional<bool>,6> log;
    std::array<std::optional<PlotRange>,6> ranges;
    std::optional<bool> legend;
    std::optional<int> legendLocation,legendFlags;
    std::set<std::uint32_t> hidden;
    bool manual=false;
    bool operator==(const PlotView &) const = default;
};
struct PlotSettings {
    float margin=5,bandSplit=.5f;
    bool ivAbsoluteLog=true,grFollow=true;
    std::optional<bool> batchAbsolute;
    std::optional<bool> contactAnnotations; // absent stays absent on an untouched project; the visible default is off
    double grVoltage=0;
    int opticalTab=0,grTab=0,recorderPlot=0;
    std::set<std::string> grHidden;
    std::map<std::string,PlotView> plots;
    std::map<std::string,bool> panels;
    std::string layout;
    bool operator==(const PlotSettings &) const = default;
    void ClearRanges() {for(auto &[id,p]:plots){p.manual=false;p.ranges={};}}

    // File-boundary adapters. Missing optional keys keep the current (global) settings.
    void Read(const nlohmann::json &ui) {
        if(!ui.is_object())return;
        const auto number=[&](const char *key,auto &v,double lo,double hi){auto it=ui.find(key);if(it!=ui.end() && it->is_number()){const double x=it->get<double>();if(std::isfinite(x) && x>=lo && x<=hi)v=static_cast<std::remove_reference_t<decltype(v)>>(x);}};
        number("auto_fit_margin_percent",margin,0,30);number("band_split",bandSplit,.15,.85);number("optical_tab",opticalTab,0,10000);number("gr_tab",grTab,0,10000);number("recorder_plot",recorderPlot,0,10000);
        for(auto [key,v]:{std::pair{"iv_absolute_log",&ivAbsoluteLog},{"gr_follow",&grFollow}})if(ui.contains(key) && ui[key].is_boolean())*v=ui[key].get<bool>();
        if(ui.contains("batch_absolute") && ui["batch_absolute"].is_boolean())batchAbsolute=ui["batch_absolute"].get<bool>();
        if(ui.contains("contact_annotations") && ui["contact_annotations"].is_boolean())contactAnnotations=ui["contact_annotations"].get<bool>();
        if(ui.contains("gr_bias") && ui["gr_bias"].is_object() && ui["gr_bias"].contains("voltage_V") && ui["gr_bias"]["voltage_V"].is_number()){const double v=ui["gr_bias"]["voltage_V"];if(std::isfinite(v))grVoltage=v;}
        if(ui.contains("gr_hidden") && ui["gr_hidden"].is_array()){grHidden.clear();for(const auto &v:ui["gr_hidden"])if(v.is_string())grHidden.insert(v.get<std::string>());}
        if(ui.contains("imgui_ini") && ui["imgui_ini"].is_string()){const auto &ini=ui["imgui_ini"].get_ref<const std::string &>();if(ValidDockLayout(ini))layout=ini;}
        if(ui.contains("panels") && ui["panels"].is_object())for(auto it=ui["panels"].begin();it!=ui["panels"].end();++it)if(it->is_boolean())panels[it.key()]=it->get<bool>();
        if(ui.contains("plots") && ui["plots"].is_object())for(auto it=ui["plots"].begin();it!=ui["plots"].end();++it)if(it->is_array() && (it->size()==4 || it->size()==6)) {
            auto &p=plots[it.key()];p.ranges={};p.manual=true;
            for(int i=0;i<int(it->size())/2;++i){if(!(*it)[i*2].is_number() || !(*it)[i*2+1].is_number()){p.manual=false;break;}PlotRange r{(*it)[i*2],(*it)[i*2+1]};if(!r.Valid()){p.manual=false;break;}p.ranges[i==0?0:i==1?3:4]=r;}
            if(!p.manual)p.ranges={};
        }
        if(ui.contains("plot_settings") && ui["plot_settings"].is_object())for(auto it=ui["plot_settings"].begin();it!=ui["plot_settings"].end();++it) {
            if(!it->is_object())continue;PlotView p;const auto &v=*it;
            if(v.contains("axes") && v["axes"].is_array())for(int i=0;i<std::min(6,int(v["axes"].size()));++i){const auto &a=v["axes"][i];if(!a.is_object())continue;if(a.contains("log") && a["log"].is_boolean())p.log[i]=a["log"].get<bool>();if(a.contains("range") && a["range"].is_array() && a["range"].size()==2 && a["range"][0].is_number() && a["range"][1].is_number()){PlotRange r{a["range"][0],a["range"][1]};if(r.Valid(p.log[i].value_or(false)))p.ranges[i]=r;}}
            if(v.contains("manual") && v["manual"].is_boolean())p.manual=v["manual"].get<bool>();
            if(v.contains("legend") && v["legend"].is_boolean())p.legend=v["legend"].get<bool>();
            if(v.contains("legend_location") && v["legend_location"].is_number_integer()){const auto x=v["legend_location"].get<std::int64_t>();if(x>=0 && x<=15 && (x&3)!=3 && (x&12)!=12)p.legendLocation=int(x);}
            if(v.contains("legend_flags") && v["legend_flags"].is_number_integer()){const int x=v["legend_flags"];if(x>=0 && x<1024)p.legendFlags=x;}
            if(v.contains("hidden") && v["hidden"].is_array())for(const auto &id:v["hidden"])if(id.is_number_unsigned() || (id.is_number_integer() && id.get<std::int64_t>()>=0))p.hidden.insert(id.get<std::uint32_t>());
            plots[it.key()]=std::move(p);
        }
    }
    nlohmann::json Write() const {
        using J=nlohmann::json;
        J ui={{"auto_fit_margin_percent",margin},{"band_split",bandSplit},{"iv_absolute_log",ivAbsoluteLog},{"gr_follow",grFollow},{"gr_bias",{{"voltage_V",grVoltage}}},{"optical_tab",opticalTab},{"gr_tab",grTab},{"recorder_plot",recorderPlot},{"gr_hidden",grHidden},{"panels",panels},{"imgui_ini",ValidDockLayout(layout)?layout:std::string{}}};
        if(batchAbsolute)ui["batch_absolute"]=*batchAbsolute;
        if(contactAnnotations)ui["contact_annotations"]=*contactAnnotations;
        ui["plot_settings"]=J::object();for(const auto &[id,p]:plots){if(p==PlotView{})continue;auto &v=ui["plot_settings"][id];v={{"manual",p.manual},{"hidden",p.hidden},{"axes",J::array()}};if(p.legend)v["legend"]=*p.legend;if(p.legendLocation)v["legend_location"]=*p.legendLocation;if(p.legendFlags)v["legend_flags"]=*p.legendFlags;for(int i=0;i<6;++i){J a=J::object();if(p.log[i])a["log"]=*p.log[i];if(p.manual && p.ranges[i])a["range"]={p.ranges[i]->min,p.ranges[i]->max};v["axes"].push_back(std::move(a));}}
        return ui;
    }
    void WriteChanges(nlohmann::json &ui,const PlotSettings &loaded) const {
        if(batchAbsolute)ui["batch_absolute"]=*batchAbsolute;
        if(contactAnnotations)ui["contact_annotations"]=*contactAnnotations;
        const auto now=Write();if(now==loaded.Write())return;
        for(auto it=now.begin();it!=now.end();++it)ui[it.key()]=*it;
        ui.erase("plots"); // one saved session snapshot supersedes legacy per-frame limits
    }
};
struct PlotRuntime {
    bool initialized=false,hasData=false,selecting=false;
    std::array<PlotRange,6> before;
    std::array<bool,6> logBefore{};
    std::array<bool,6> enabled{};
    bool legendShown=true;
    int legendLocation=0;
    std::array<std::optional<PlotRange>,6> explicitFit;
    std::array<SmoothPlotAxis,6> axes;
};
struct PlotSession {
    PlotSettings settings;
    std::map<std::string,CurveMorph> curves;
    int morphFrame=-1;
    std::map<std::string,PlotRuntime> runtime;
    unsigned epoch=~0u;
    std::uint64_t revision=0;
    bool smoothFit=false,holdFit=false,liveData=false;
    void ResetRanges() {settings.ClearRanges();runtime.clear();++revision;}
};
}
