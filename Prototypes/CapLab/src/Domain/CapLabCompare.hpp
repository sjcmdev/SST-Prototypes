#pragma once
#include "Domain/CapLabAnalysis.hpp"
#include <complex>
#include <numbers>
#include <tuple>
#include <sst/physics/impedance.hpp>
#include <sst/plotlab/GraphSnapshot.hpp>

namespace SST::Prototypes::CapLab {
inline std::string CompareStatus(const Project &p, const Json &record) {
    for (const auto &input:record.at("inputs")) {
        try {if (AnalysisMeasurement(p,input.at("id")).dump()!=input.dump()) return "stale";}
        catch (const std::exception &) {return "missing_input";}
    }
    for (const auto &id:record.at("analysis_ids")) {
        auto found=std::find_if(p.analyses.begin(),p.analyses.end(),[&](const auto &a) {return a.at("id")==id;});
        if (found==p.analyses.end()) return "missing_input";
        if (found->at("status")!="valid") return found->at("status");
    }
    return "valid";
}
inline void RefreshComparisonLabels(Project &p) {
    std::vector<pl::Object> changed;
    for (auto &record:p.comparisons) {
        const auto status=CompareStatus(p,record), previous=record.value("status",std::string("valid"));
        record["status"]=status;
        if (status==previous) continue;
        const auto suffix=" ["+previous+"]";
        for (auto layer:p.engine->children(record.at("graph_id"))) for (auto id:p.engine->children(layer)) {
            auto object=p.engine->object(id);
            if (previous!="valid" && object.name.ends_with(suffix)) object.name.resize(object.name.size()-suffix.size());
            if (status!="valid") object.name+=" ["+status+"]";
            changed.push_back(std::move(object));
        }
    }
    if (!changed.empty()) p.engine->change("Comparison source status",std::move(changed)).apply(*p.engine);
}
// Uses stored columns/results only; this operation never fits or invents material constants.
inline void BuildCompareGraph(Project &p, const Json &request) {
    for (const char *key:{"measurement_ids","analysis_ids","graph_ids"})
        if (request.contains(key) && !request.at(key).is_array()) throw std::runtime_error(std::string(key)+" must be an array of IDs.");
    if (request.contains("action") && request.at("action")!="view") throw std::runtime_error("Unknown plot action.");
    if (request.value("action",std::string{})=="view") {
        const auto graph=request.at("graph_id").get<pl::Id>();
        if (!std::holds_alternative<pl::Graph>(p.engine->object(graph).data)) throw std::runtime_error("Expected graph_id.");
        auto layer=p.engine->object(p.engine->children(graph).at(0));
        auto &data=std::get<pl::Layer>(layer.data);
        if (request.contains("view")) {
            auto view=pl::viewStateFromJson(request.at("view").dump());
            if (view.viewport && ((data.spec.x.scale==sst::ui::plot::AxisScale::Log10 && view.viewport->minX<=0) ||
                (data.spec.y.scale==sst::ui::plot::AxisScale::Log10 && view.viewport->minY<=0))) throw std::runtime_error("Logarithmic view ranges must be positive.");
            for (const auto &[key,visible]:view.visibility) {
                bool found=false;
                for (auto id:p.engine->children(layer.id)) {
                    const auto stable=p.engine->series(id).id;
                    found |= key==stable || key.ends_with("###"+stable);
                }
                if (!found) throw std::runtime_error("Visibility ID belongs to another graph.");
            }
            (void)pl::snapshotGraph(*p.engine,graph,view);
            data.view=std::move(view);
        }
        auto scale=[&](const char *key,auto &axis) {
            if (!request.contains(key)) return;
            const std::string value=request.at(key);
            if (value!="linear" && value!="log10") throw std::runtime_error("Axis scale must be linear or log10.");
            axis.scale=value=="linear" ? sst::ui::plot::AxisScale::Linear : sst::ui::plot::AxisScale::Log10;
            data.view={};
        };
        scale("x_scale",data.spec.x); scale("y_scale",data.spec.y);
        if (request.value("refit",false)) data.view={};
        FormatCompareAxes(data.spec);
        p.engine->change("Graph view settings",{std::move(layer)}).apply(*p.engine);
        p.graph=graph; return;
    }
    const std::string type=request.value("plot_type",std::string("C-f"));
    Json spec{{"name",request.value("name",request.value("measurement_ids",Json::array()).empty() ? std::string("Stored results comparison") : type+" comparison")},{"curves",Json::array()}};
    Json record{{"inputs",Json::array()},{"analysis_ids",Json::array()}};
    struct Curve {std::string sample; double temperature; pl::Id id; Json data; sst::ui::plot::Color color;};
    std::vector<Curve> curves;
    std::set<pl::Id> seen;
    auto axes=[&](const std::string &x,const std::string &y,bool logX=false,bool logY=false,bool equal=false) {
        const Json config{{"x_label",x},{"y_label",y},{"log_x",logX},{"log_y",logY},{"equal_aspect",equal}};
        if (spec.contains("x_label")) for (const auto &[key,value]:config.items())
            if (spec.at(key)!=value) throw std::runtime_error("Incompatible comparison axes; select graphs with the same quantities and scales.");
        spec.update(config);
    };
    auto append=[&](pl::Id sample,Json temperature,pl::Id id,Json curve,const std::string &suffix) {
        const auto s=std::find_if(p.samples.begin(),p.samples.end(),[&](const auto &s) {return s.id==sample;});
        if (s==p.samples.end()) throw std::runtime_error("Missing comparison sample.");
        std::string label=s->name+" ["+std::to_string(sample)+"]";
        if (temperature.is_number()) label+=", T = "+temperature.dump()+" K";
        curve["name"]=label+" / "+suffix+" ["+std::to_string(id)+"]";
        curve["temperature_K"]=temperature;
        curves.push_back({s->name,temperature.is_number() ? temperature.get<double>() : -1.,id,std::move(curve),SampleTemperatureColor(sample,temperature)});
    };
    for (const auto &value:request.value("measurement_ids",Json::array())) {
        const auto id=value.get<pl::Id>();
        if (!seen.insert(id).second) throw std::runtime_error("Duplicate measurement_id.");
        const auto m=AnalysisMeasurement(p,id);
        record["inputs"].push_back(m);
        auto column=[&](const char *name) {
            const auto names=m.at("names").get<std::vector<std::string>>();
            const auto it=std::find(names.begin(),names.end(),name);
            if (it==names.end()) throw std::runtime_error(std::string("Selected measurement lacks ")+name+"; use an existing analysis graph for derived results.");
            return m.at("columns").at(std::distance(names.begin(),it)).get<std::vector<double>>();
        };
        std::vector<double> x,y;
        const std::string kind=m.at("kind");
        if (type=="C-V" || type=="Mott-Schottky" || type=="N_CV(W)" || type=="N_DLCP(W)") {
            if (kind!="CVSweep" && kind!="DLCPProfile" && kind!="CV-derived") throw std::runtime_error("Select a CV sweep or imported profile.");
            if (type=="C-V") {x=column("V"); y=column(kind=="CVSweep" ? "C" : "C0"); axes("Voltage (V)","Capacitance (F)");}
            else if (type=="Mott-Schottky") {
                x=column("V");
                if (kind=="CVSweep") {
                    if (m.at("area_m2").is_null()) throw std::runtime_error("Mott-Schottky requires sample area_m2.");
                    y=column("C"); const double area=m.at("area_m2");
                    for (auto &v:y) v=v!=0 ? area*area/(v*v) : std::numeric_limits<double>::quiet_NaN();
                } else y=column("A2_C02");
                axes("Voltage (V)","A2/C2 (m4/F2)");
            } else if (type=="N_CV(W)") {x=column("W_CV"); y=column("N_CV"); axes("Width (m)","N_CV (m^-3)");}
            else {
                if (kind!="DLCPProfile") throw std::runtime_error("CV-derived profile is not measured DLCP.");
                x=column("W"); y=column("N_DL"); axes("x_e (m)","N_DL (m^-3)");
            }
        } else {
            if (kind!="FrequencySweep") throw std::runtime_error("Select a frequency sweep.");
            const auto f=column("f");
            if (type=="C-f" && m.at("variant")!="ReZ_ImZ") {x=f; y=column("C"); axes("Frequency (Hz)","Capacitance (F)",true);}
            else {
                std::vector<std::complex<double>> z;
                if (m.at("variant")=="ReZ_ImZ") {
                    auto re=column("ReZ"),im=column("ImZ");
                    for (std::size_t i=0;i<f.size();++i) z.emplace_back(re[i],im[i]);
                } else {
                    auto c=column("C"),g=m.at("variant")=="Cp_Rp" ? column("Rp") : column("G");
                    for (std::size_t i=0;i<f.size();++i) {
                        if (m.at("variant")=="Cp_Rp") g[i]=sst::physics::parallelConductance(g[i]);
                        z.push_back(sst::physics::reciprocalImmittance(sst::physics::parallelAdmittance(f[i],c[i],g[i])));
                    }
                }
                for (std::size_t i=0;i<f.size();++i) {
                    x.push_back(type=="Nyquist" ? z[i].real() : f[i]);
                    if (type=="Nyquist") y.push_back(-z[i].imag());
                    else if (type=="Bode magnitude") y.push_back(std::abs(z[i]));
                    else if (type=="Bode phase") y.push_back(std::arg(z[i])*180/std::numbers::pi);
                    else {
                        const auto a=sst::physics::reciprocalImmittance(z[i]);
                        if (type=="G/omega-f") y.push_back(a.real()/(2*std::numbers::pi*f[i]));
                        else if (type=="C-f") y.push_back(a.imag()/(2*std::numbers::pi*f[i]));
                        else throw std::runtime_error("Unknown measurement plot_type.");
                    }
                }
                if (type=="Nyquist") axes("Re Z (Ohm)","-Im Z (Ohm)",false,false,true);
                else axes("Frequency (Hz)",type=="Bode magnitude" ? "|Z| (Ohm)" : type=="Bode phase" ? "Phase (deg)" : type=="C-f" ? "Capacitance (F)" : "G/omega (F)",true,type=="Bode magnitude");
            }
        }
        append(m.at("sample_id"),m.value("temperature_K",Json(nullptr)),id,Json{{"x",x},{"y",y}},type);
    }
    Json sourceGraphs=request.value("graph_ids",Json::array());
    seen.clear();
    for (const auto &value:request.value("analysis_ids",Json::array())) {
        const auto id=value.get<pl::Id>();
        if (!seen.insert(id).second) throw std::runtime_error("Duplicate analysis_id.");
        const auto found=std::find_if(p.analyses.begin(),p.analyses.end(),[&](const auto &a) {return a.at("id")==id;});
        if (found==p.analyses.end()) throw std::runtime_error("Missing analysis_id.");
        sourceGraphs.push_back(found->at("graphs").at(request.value("plot_index",std::size_t{0})));
    }
    seen.clear();
    for (const auto &value:sourceGraphs) {
        const auto id=value.get<pl::Id>();
        if (!seen.insert(id).second) throw std::runtime_error("Duplicate graph_id.");
        if (!std::holds_alternative<pl::Graph>(p.engine->object(id).data)) throw std::runtime_error("Expected graph_id.");
        const auto layers=p.engine->children(id);
        if (layers.size()!=1) throw std::runtime_error("Comparison requires one XY layer.");
        const auto &source=std::get<pl::Layer>(p.engine->object(layers[0]).data).spec;
        const auto analysis=std::find_if(p.analyses.begin(),p.analyses.end(),[&](const auto &a) {return std::find(a.at("graphs").begin(),a.at("graphs").end(),Json(id))!=a.at("graphs").end();});
        if (analysis==p.analyses.end()) throw std::runtime_error("Select an analysis graph for graph_ids; raw data use measurement_ids.");
        if ((source.x.scale!=sst::ui::plot::AxisScale::Linear && source.x.scale!=sst::ui::plot::AxisScale::Log10) ||
            (source.y.scale!=sst::ui::plot::AxisScale::Linear && source.y.scale!=sst::ui::plot::AxisScale::Log10)) throw std::runtime_error("Unsupported comparison scale.");
        auto label=[](const std::string &value) {
            static const std::map<std::string,std::string> legacy{{"f [Hz]","Frequency (Hz)"},{"Re Z [Ohm]","Re Z (Ohm)"},
                {"-Im Z [Ohm]","-Im Z (Ohm)"},{"|Z| [Ohm]","|Z| (Ohm)"},{"phase [deg]","Phase (deg)"},
                {"C [F]","Capacitance (F)"},{"G/omega [F]","G/omega (F)"}};
            const auto it=legacy.find(value); return it==legacy.end() ? value : it->second;
        };
        axes(label(source.x.label),label(source.y.label),source.x.scale==sst::ui::plot::AxisScale::Log10,source.y.scale==sst::ui::plot::AxisScale::Log10,source.equalAspect);
        record["analysis_ids"].push_back(analysis->at("id"));
        Json t=nullptr;
        if (!analysis->at("inputs").empty()) t=analysis->at("inputs").at(0).value("temperature_K",Json(nullptr));
        std::size_t curveIndex=0;
        for (auto plot:p.engine->children(layers[0])) {
            const auto entry=p.engine->series(plot);
            if (entry.kind!=sst::ui::plot::SeriesKind::Line && entry.kind!=sst::ui::plot::SeriesKind::Scatter && entry.kind!=sst::ui::plot::SeriesKind::LineScatter) throw std::runtime_error("Select a 2D XY graph.");
            std::vector<double> x,y;
            if (entry.data.count) {x.assign(entry.data.x,entry.data.x+entry.data.count); y.assign(entry.data.y,entry.data.y+entry.data.count);}
            Json curve{{"x",x},{"y",y},
                {"fit",entry.kind==sst::ui::plot::SeriesKind::Line},{"candidates",entry.kind==sst::ui::plot::SeriesKind::Scatter}};
            auto temperature=t;
            if (analysis->at("type")=="Walter" && curveIndex<analysis->at("spectra").size()) temperature=analysis->at("spectra").at(curveIndex).value("temperature_K",Json(nullptr));
            const auto sheet=std::get<pl::Plot>(p.engine->object(plot).data).worksheet;
            const auto &metadata=std::get<pl::Worksheet>(p.engine->object(sheet).data).source;
            if (metadata.contains("temperature_K")) temperature=Json::parse(metadata.at("temperature_K"));
            append(AnalysisSample(*analysis),temperature,plot,std::move(curve),p.engine->object(plot).name+" / "+analysis->at("status").get<std::string>());
            ++curveIndex;
        }
    }
    if (curves.empty()) throw std::runtime_error("Select measurements or analysis graphs by ID.");
    std::stable_sort(curves.begin(),curves.end(),[](const auto &a,const auto &b) {return std::tie(a.sample,a.temperature,a.id)<std::tie(b.sample,b.temperature,b.id);});
    for (const auto &c:curves) spec["curves"].push_back(c.data);
    p.graph=AnalysisPlot(p,p.samples.front().id,spec);
    const auto layer=p.engine->children(p.graph).front();
    auto axesObject=p.engine->object(layer);
    auto &config=std::get<pl::Layer>(axesObject.data).spec;
    if (spec.at("log_y").get<bool>()) config.y.scale=sst::ui::plot::AxisScale::Log10;
    FormatCompareAxes(config);
    std::vector<pl::Object> styles{std::move(axesObject)};
    std::size_t i=0;
    for (auto id:p.engine->children(layer)) {
        auto object=p.engine->object(id); object.order=i;
        std::get<pl::Plot>(object.data).style.color=curves[i++].color;
        styles.push_back(std::move(object));
    }
    p.engine->change("Comparison styles",std::move(styles)).apply(*p.engine);
    record["graph_id"]=p.graph; p.comparisons.push_back(std::move(record));
    Validate(p);
}
}
