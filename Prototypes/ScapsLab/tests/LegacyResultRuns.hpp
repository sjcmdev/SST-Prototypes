#pragma once
#include "Workflow.hpp"
namespace SST::ScapsLab {
Json LegacyExportResults(const Json&,const Json&);
void ComparePlotExports();
// Frozen workspace adapter: test oracle for the pre-typed exporter.
inline Json ResultRuns(const Json &workspace) {
    Json runs=Json::array();
    for(const auto &d:workspace.at("devices")) {
        auto add=[&](const std::string &kind,const Json &result) {
            if(result.empty()) return;
            for(const auto &run:d.value("history",Json::array())) if(run.at("provenance")==result.value("provenance",Json()) && run.at("kind")==kind && kind!="batch")return;
            const auto id=d.at("id").get<std::string>()+"/"+kind+"/"+std::to_string(result.at("provenance").at("time_unix_ms").get<std::int64_t>());
            runs.push_back({{"run_id",id},{"device_id",d.at("id")},{"device_name",d.at("name")},{"kind",kind},{"result",result}});
        };
        for(const auto &run:d.value("history",Json::array())) {
            if(run.at("kind")=="batch" && !d.value("batch_result",Json::object()).empty() && run.at("provenance").at("time_unix_ms")==d.at("batch_result").at("provenance").at("time_unix_ms"))continue;
            const auto kind=run.at("kind").get<std::string>();
            const auto live=kind=="iv"?d.value("result",Json::object()):kind=="band"?d.value("band_result",Json::object()):d.value("ac_results",Json::object()).value(kind,Json::object());
            const auto &result=!live.empty() && live.at("provenance")==run.at("provenance")?live:run.at("result");
            runs.push_back({{"run_id",run.at("run_id")},{"device_id",d.at("id")},{"device_name",d.at("name")},{"kind",run.at("kind")},{"result",result}});
        }

        add("iv",d.value("result",Json::object()));add("band",d.value("band_result",Json::object()));
        for(auto it=d.at("ac_results").begin();it!=d.at("ac_results").end();++it) add(it.key(),it.value());
        add("batch",d.value("batch_result",Json::object()));
    }
    return runs;
}
}
