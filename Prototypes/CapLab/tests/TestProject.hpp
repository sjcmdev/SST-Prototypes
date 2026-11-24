#pragma once
#include "Domain/CapLabImport.hpp"
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include <fstream>
#include <sstream>
namespace SST::Prototypes::CapLab {
inline Json FixturePreview() {
    std::ifstream file(std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path() / "tests/fixtures/cf_100K_7.dat");
    if (!file) throw std::runtime_error("Missing PFS I2 fixture");
    std::vector<std::vector<double>> columns(3);
    std::vector<std::size_t> rows;
    std::string line;
    while (std::getline(file,line)) {
        std::istringstream stream(line); double f,c,r;
        if (!(stream >> f >> c >> r)) throw std::runtime_error("Invalid frozen fixture");
        columns[0].push_back(f); columns[1].push_back(c); columns[2].push_back(r); rows.push_back(rows.size()+1);
    }
    return Json{{"columns",columns},{"source_rows",rows},{"profile","lab_as_cp_rp"},
        {"source","cf_100K_7.dat"},{"sha256","b46ce13e84432645850ccf0d3bd5445d3bd5a978da40942bb44e78adb9a479c6"},
        {"header",""},{"decimal","."},{"temperature_K",nullptr},{"suggested_temperature_K",100}};
}
inline Project FixtureProject() {
    Project p; ImportChange change(p,FixturePreview(),100,CFTemplate{}); return p;
}
// JSON here is the frozen file-schema fixture adapter, not a production API.
inline Json ResultFixtureJson(const AnalysisResult &result) {
    auto out=result.record;out["inputs"]=Json::array();out["plots"]=Json::array();
    for(const auto &m:result.inputs)out["inputs"].push_back(MeasurementJson(m));
    for(const auto &p:result.plots) {
        Json plot{{"name",p.name},{"x_label",p.xLabel},{"y_label",p.yLabel},{"log_x",p.logX},{"log_y",p.logY},{"equal_aspect",p.equalAspect},{"curves",Json::array()}};
        for(const auto &c:p.curves) {
            auto array=[](const auto &v){Json a=Json::array();for(double x:v)a.push_back(std::isfinite(x)?Json(x):Json(nullptr));return a;};
            Json curve{{"name",c.name},{"x",array(c.x)},{"y",array(c.y)},{"fit",c.fit},{"candidates",c.candidates}};
            if(c.temperature_K)curve["temperature_K"]=*c.temperature_K;
            plot["curves"].push_back(std::move(curve));
        }
        out["plots"].push_back(std::move(plot));
    }
    return out;
}
inline AnalysisResult ResultFromFixture(const Json &j) {
    AnalysisResult result;result.record=j;result.record.erase("inputs");result.record.erase("plots");
    for(const auto &m:j.at("inputs"))result.inputs.push_back(MeasurementFromJson(m));
    for(const auto &p:j.at("plots")) {
        AnalysisPlotData plot;plot.name=p.at("name");plot.xLabel=p.at("x_label");plot.yLabel=p.at("y_label");
        plot.logX=p.value("log_x",false);plot.logY=p.value("log_y",false);plot.equalAspect=p.value("equal_aspect",false);
        for(const auto &c:p.at("curves")) {
            AnalysisCurveData curve;curve.name=c.at("name");curve.fit=c.value("fit",false);curve.candidates=c.value("candidates",false);
            for(const auto &v:c.at("x"))curve.x.push_back(v.is_null()?std::numeric_limits<double>::quiet_NaN():v.get<double>());
            for(const auto &v:c.at("y"))curve.y.push_back(v.is_null()?std::numeric_limits<double>::quiet_NaN():v.get<double>());
            if(c.contains("temperature_K"))curve.temperature_K=Optional(c.at("temperature_K"));
            plot.curves.push_back(std::move(curve));
        }
        result.plots.push_back(std::move(plot));
    }
    return result;
}
inline Json SimulateIS(const Json &m,const Json &c,const Json &settings,JobContext &context,bool plots=true) {
    return ResultFixtureJson(SimulateIS(MeasurementFromJson(m),c,settings,context,plots));
}
inline Json FitIS(const Json &m,const Json &c,const Json &settings,JobContext &context,const ISEvaluationObserver &observer={}) {
    return ResultFixtureJson(FitIS(MeasurementFromJson(m),c,settings,context,observer));
}
inline void OverlayISMeasurement(Json &r,const Json &m) {
    auto result=ResultFromFixture(r);OverlayISMeasurement(result,MeasurementFromJson(m));r=ResultFixtureJson(result);
}
inline void AppendAnalysis(Project &p,Json result) {AppendAnalysis(p,ResultFromFixture(result));}
}
