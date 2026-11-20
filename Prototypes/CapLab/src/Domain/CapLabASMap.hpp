#pragma once
#include "Domain/CapLabAnalysis.hpp"
#include <numeric>
#include <numbers>
#include <sst/physics/impedance.hpp>

namespace SST::Prototypes::CapLab {
// A derived view owns its columns; measured values and row IDs remain untouched.
inline void BuildASMapGraph(Project &p, const Json &request) {
    namespace plot = sst::ui::plot;
    const bool conductance=request.at("plot_type")=="AS G/omega(f,T)";
    std::string view=request.value("view_kind",std::string("lines"));
    const std::string policy=request.value("grid_policy",std::string("measured"));
    if (view!="lines" && view!="surface" && view!="heatmap") throw std::runtime_error("AS view_kind must be lines, surface or heatmap.");
    if (policy!="measured" && policy!="interpolate_log_f") throw std::runtime_error("AS grid_policy must be measured or interpolate_log_f.");
    if (!request.value("graph_ids",Json::array()).empty() || !request.value("analysis_ids",Json::array()).empty())
        throw std::runtime_error("AS maps use measurement_ids only.");
    if (policy=="measured" && request.contains("frequency_Hz")) throw std::runtime_error("Select interpolate_log_f explicitly to use frequency_Hz.");
    struct Row {pl::Id id; double temperature; std::vector<double> f,z;};
    std::vector<Row> rows;
    Json record{{"inputs",Json::array()},{"analysis_ids",Json::array()},{"diagnostics",Json::array()},
        {"plot_type",request.at("plot_type")},{"grid_policy",policy},{"requested_view",view}};
    std::set<pl::Id> seen;
    pl::Id sample=0;
    Json bias;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for (const auto &value:request.value("measurement_ids",Json::array())) {
        const auto id=value.get<pl::Id>();
        if (!seen.insert(id).second) throw std::runtime_error("Duplicate measurement_id.");
        auto m=AnalysisMeasurement(p,id);
        if (m.at("kind")!="FrequencySweep") throw std::runtime_error("AS maps require frequency sweeps.");
        if (sample && (m.at("sample_id")!=sample || m.value("bias_V",Json(nullptr))!=bias))
            throw std::runtime_error("Select one sample and one bias for an AS temperature series.");
        sample=m.at("sample_id"); bias=m.value("bias_V",Json(nullptr));
        if (!m.at("temperature_K").is_number() || !std::isfinite(m.at("temperature_K").get<double>()) || m.at("temperature_K").get<double>()<=0)
            throw std::runtime_error("AS maps require confirmed positive temperature_K for every measurement.");
        auto column=[&](const char *name) {
            const auto names=m.at("names").get<std::vector<std::string>>();
            auto it=std::find(names.begin(),names.end(),name);
            if (it==names.end()) throw std::runtime_error(std::string("AS measurement lacks ")+name);
            std::vector<double> out;
            for (const auto &v:m.at("columns").at(std::distance(names.begin(),it))) out.push_back(v.is_null()?nan:v.get<double>());
            return out;
        };
        auto f=column("f");
        std::vector<double> z;
        const std::string variant=m.at("variant");
        if (variant=="ReZ_ImZ") {
            auto re=column("ReZ"),im=column("ImZ");
            for (std::size_t i=0;i<f.size();++i) {
                try {
                    const auto y=sst::physics::reciprocalImmittance({re.at(i),im.at(i)});
                    z.push_back((conductance?y.real():y.imag())/(2*std::numbers::pi*f[i]));
                } catch (const std::invalid_argument &) {z.push_back(nan);}
            }
        } else if (variant=="Cp_Rp" || variant=="C_G") {
            z=column(conductance ? (variant=="Cp_Rp"?"Rp":"G") : "C");
            if (conductance) for (std::size_t i=0;i<f.size();++i) {
                try {
                    const double g=variant=="Cp_Rp"?sst::physics::parallelConductance(z.at(i)):z.at(i);
                    z.at(i)=g/(2*std::numbers::pi*f[i]);
                } catch (const std::invalid_argument &) {z.at(i)=nan;}
            }
        } else throw std::runtime_error("Unsupported AS measurement variant.");
        if (f.size()<2 || z.size()!=f.size()) throw std::runtime_error("AS sweeps need at least two frequency/value pairs.");
        std::vector<std::size_t> order(f.size()); std::iota(order.begin(),order.end(),0);
        for (double v:f) if (!std::isfinite(v) || v<=0) throw std::runtime_error("AS frequency_Hz must be finite and positive.");
        std::sort(order.begin(),order.end(),[&](auto a,auto b) {return f[a]<f[b];});
        Row row{id,m.at("temperature_K"),{}, {}};
        for (auto i:order) {
            if (!row.f.empty() && row.f.back()==f[i]) throw std::runtime_error("Duplicate AS frequency_Hz.");
            row.f.push_back(f[i]); row.z.push_back(std::isfinite(z[i])?z[i]:nan);
        }
        rows.push_back(std::move(row)); record["inputs"].push_back(std::move(m));
    }
    if (rows.size()<2) throw std::runtime_error("Select at least two AS temperatures.");
    std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b) {return a.temperature<b.temperature;});
    for (std::size_t i=1;i<rows.size();++i) if (rows[i-1].temperature==rows[i].temperature)
        throw std::runtime_error("AS maps require one measurement per temperature_K.");
    if (policy=="interpolate_log_f") {
        const auto target=request.at("frequency_Hz").get<std::vector<double>>();
        if (target.size()<2 || target.size()>1000000/rows.size()) throw std::runtime_error("AS grid needs 2 or more frequencies and at most 1000000 cells.");
        for (std::size_t i=0;i<target.size();++i)
            if (!std::isfinite(target[i]) || target[i]<=0 || (i && target[i]<=target[i-1])) throw std::runtime_error("Target frequency_Hz must be positive and strictly increasing.");
        for (auto &row:rows) {
            std::vector<double> values;
            for (double f:target) {
                const auto it=std::lower_bound(row.f.begin(),row.f.end(),f);
                const auto i=static_cast<std::size_t>(it-row.f.begin());
                double z=nan;
                if (it!=row.f.end() && *it==f) z=row.z[i];
                else if (i && it!=row.f.end() && std::isfinite(row.z[i-1]) && std::isfinite(row.z[i])) {
                    const double weight=(std::log(f)-std::log(row.f[i-1]))/(std::log(row.f[i])-std::log(row.f[i-1]));
                    z=std::lerp(row.z[i-1],row.z[i],weight);
                }
                values.push_back(z);
            }
            row.f=target; row.z=std::move(values);
        }
        record["frequency_Hz"]=target;
        record["diagnostics"].push_back("Explicit interpolation in log f only; no temperature interpolation, extrapolation or bridging missing cells.");
    }
    const bool common=std::all_of(rows.begin(),rows.end(),[&](const auto &r) {return r.f==rows.front().f;});
    if (!common && view!="lines") {
        view="lines";
        record["diagnostics"].push_back("Unequal measured frequency grids: showing per-temperature 3D lines. Select interpolate_log_f and an explicit frequency_Hz grid for surface/heatmap.");
    }
    record["view_kind"]=view;
    record["diagnostics"].push_back("Missing or nonconvertible values remain gaps; raw measurements are unchanged.");
    const auto graph=p.engine->reserveId(),layer=p.engine->reserveId();
    const std::string name=request.value("name",request.at("plot_type").get<std::string>()+" / "+view);
    pl::Layer axes; axes.spec.autoFit=plot::AutoFit::Once;
    axes.spec.legend.visible=view=="lines";
    axes.spec.x.label="Frequency"; axes.spec.x.unit="Hz"; axes.spec.x.scale=plot::AxisScale::Log10;
    axes.spec.y.label="Temperature"; axes.spec.y.unit="K";
    axes.spec.z.label=conductance?"G/omega":"Capacitance"; axes.spec.z.unit="F";
    FormatCompareAxes(axes.spec);
    std::vector<pl::Object> objects{{graph,p.engine->root(),name,0,pl::Graph{130,100}},{layer,graph,name,0,axes}};
    auto append=[&](const std::string &label,std::vector<double> x,std::vector<double> y,std::vector<double> z,std::size_t columns,plot::Color color) {
        const auto sheet=p.engine->reserveId(),cx=p.engine->reserveId(),cy=p.engine->reserveId(),cz=p.engine->reserveId();
        pl::Worksheet data;
        data.source["AS_grid_policy"]=policy;
        for (std::size_t i=0;i<x.size();++i) data.rows.push_back(p.engine->reserveId());
        pl::Column a,b,c; a.role=pl::Role::X; b.role=pl::Role::Y; a.units="Hz"; b.units="K"; c.units="F";
        a.values=std::move(x); b.values=std::move(y); c.values=std::move(z);
        pl::Plot curve{sheet,cx,cy,cz}; curve.gridColumns=columns;
        curve.kind=view=="lines"?plot::SeriesKind::Line3D:view=="surface"?plot::SeriesKind::Surface:plot::SeriesKind::Heatmap;
        curve.style.color=color;
        objects.push_back({sheet,sample,label,0,std::move(data)});
        objects.push_back({cx,sheet,"Frequency",0,std::move(a)}); objects.push_back({cy,sheet,"Temperature",0,std::move(b)});
        objects.push_back({cz,sheet,axes.spec.z.label,0,std::move(c)});
        objects.push_back({p.engine->reserveId(),layer,label,0,std::move(curve)});
    };
    if (view=="lines") for (const auto &row:rows) {
        char label[64];std::snprintf(label,sizeof(label),"%.6g K",row.temperature);
        auto color=SampleTemperatureColor(sample,row.temperature);
        const auto colors=request.value("source_colors",Json::object());
        if (!colors.is_object()) throw std::runtime_error("source_colors must map source IDs to RGBA arrays.");
        if (colors.contains(std::to_string(row.id))) {
            if (colors.at(std::to_string(row.id)).size()!=4) throw std::runtime_error("Expected four RGBA channels.");
            color=colors.at(std::to_string(row.id)).get<plot::Color>();
            for (auto channel:color) if (!std::isfinite(channel) || channel<0 || channel>1) throw std::runtime_error("RGBA channels must be finite and between 0 and 1.");
        }
        append(label,row.f,std::vector<double>(row.f.size(),row.temperature),row.z,0,color);
    }
    else {
        std::vector<double> x,y,z;
        for (const auto &row:rows) for (std::size_t i=0;i<row.f.size();++i) {x.push_back(row.f[i]); y.push_back(row.temperature); z.push_back(row.z[i]);}
        append(axes.spec.z.label,std::move(x),std::move(y),std::move(z),rows.front().f.size(),{});
    }
    p.engine->change(name,std::move(objects)).apply(*p.engine);
    p.graph=graph; record["graph_id"]=graph; p.comparisons.push_back(std::move(record));
    Validate(p);
}
}
