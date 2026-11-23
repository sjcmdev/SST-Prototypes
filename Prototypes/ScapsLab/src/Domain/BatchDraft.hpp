#pragma once
#include "Domain/ProjectDraft.hpp"
#include <scaps_core/batch.hpp>
namespace SST::ScapsLab {
struct NumericParameter {std::string id,path,pointer,label,unit;double value=0;};
using IlluminationValue=std::variant<bool,double,std::string,scaps_core::OpticalTable>;
using IlluminationPatch=std::map<std::string,IlluminationValue>;
struct InitialStateDraft {
    bool explicitObject=false,exportWp2=false;
    std::string file,scapsRoot;
    std::optional<double> temperature_K,voltage_V;
    std::optional<int> number_of_points;
    std::optional<std::optional<double>> generation_total_m2_s;
    IlluminationPatch illumination;
    std::map<std::string,std::string> provenance_spellings;
};
struct BatchAxis {
    std::string parameter_id="temperature_K",path,unit,spacing="linear";
    double start=270,stop=330;
    int count=3;
    std::optional<std::vector<double>> values;
};
struct RecorderDraft {
    std::vector<scaps_core::RecorderColumn> columns;
    std::shared_ptr<const nlohmann::json> source,canonical;
    RecorderDraft(){scaps_core::RecorderColumn c;c.id="J_at_0V";c.quantity="J";c.voltage_V=0;columns.push_back(std::move(c));}
};
struct BatchDraft {
    std::string mode="nested";
    std::vector<BatchAxis> axes{BatchAxis{}};
    bool include_curves=true;
    int threads=1;
    std::optional<std::vector<double>> voltages_V,frequencies_Hz,mesh_coordinates_m;
    double photovoltaic_voltage_limit_V=2;
    std::optional<double> incident_power_W_m2;
    std::optional<scaps_core::BatchConditioning> conditioning;
    bool projectTransitions=true;
    std::optional<InitialStateDraft> initial;
    std::shared_ptr<const nlohmann::json> source,canonical;
};
struct BatchPlan {
    BatchDraft spec;
    std::vector<std::map<std::string,double>> combinations;
    scaps_core::BatchSpec engine;
    RecorderDraft recorder;
    std::size_t SampleCount() const {return combinations.size()*engine.voltages_V.size()*(1+engine.frequencies_Hz.size());}
};
std::vector<NumericParameter> ProjectParameters(const ProjectDraft&);
std::shared_ptr<const BatchDraft> ReadBatchDraft(const nlohmann::json&);
std::shared_ptr<const RecorderDraft> ReadRecorderDraft(const nlohmann::json&);
nlohmann::json BatchDraftJson(const BatchDraft&);
nlohmann::json RecorderDraftJson(const RecorderDraft&);
BatchPlan PrepareBatch(const ProjectDraft&,const BatchDraft&,const RecorderDraft&);
BatchDraft PrepareQuickSweep(const ProjectDraft&,const std::string& parameter_id,double delta,int points,const std::string& spacing);
nlohmann::json BatchPlanJson(const BatchPlan&);
scaps_core::BatchSpec PrepareBatchMember(const scaps_core::DeviceProject&,const BatchDraft&,scaps_core::BatchSpec);
IlluminationPatch ReadIlluminationPatch(const nlohmann::json&);
void ApplyIlluminationPatch(scaps_core::Illumination&,const IlluminationPatch&);
scaps_core::MetastableSpec ReadMetastableSpec(const nlohmann::json&);
// Shared lossless merge used only by file/Python adapters.
nlohmann::json MergeFileDocument(const nlohmann::json& original,const nlohmann::json& before,const nlohmann::json& after);
}
