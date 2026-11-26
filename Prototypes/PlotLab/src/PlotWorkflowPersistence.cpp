#include "PlotWorkflowState.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <sst/plotlab/edges/ViewJson.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>

namespace SST::Prototypes::PlotLab {
namespace {
namespace pl=sst::plotlab;
using Json=nlohmann::json;
Json EncodeParameters(const std::vector<pl::AnalysisParameter>& values) {
    Json out=Json::array();for(const auto& p:values)out.push_back({{"name",p.name},{"units",p.units},{"start",p.start},{"lower",p.lower},{"upper",p.upper},{"fixed",p.fixed},{"shared",p.shared}});return out;
}
std::vector<pl::AnalysisParameter> DecodeParameters(const Json& values) {
    std::vector<pl::AnalysisParameter> out;
    for(const auto& p:values) {pl::AnalysisParameter value{p.at("name"),p.at("units"),p.at("start"),p.at("lower"),p.at("upper"),p.at("fixed"),p.value("shared",false)};
        if(!std::isfinite(value.start) || !std::isfinite(value.lower) || !std::isfinite(value.upper) || value.lower>value.upper)throw std::invalid_argument("Invalid analysis parameter bounds");out.push_back(std::move(value));}
    return out;
}
Json EncodeRecipe(const pl::AnalysisRecipe& r) {
    const auto& s=r.settings;Json parameters=EncodeParameters(s.parameters),anchors=Json::array(),peaks=Json::array(),inputs=Json::array();
    for(const auto& peak:s.peaks)peaks.push_back({{"id",peak.id},{"shape",static_cast<int>(peak.shape)}});
    for(const auto& input:r.globalInputs)inputs.push_back({{"id",input.id},{"worksheet_id",input.worksheet},{"x_column_id",input.xColumn},{"y_column_id",input.yColumn},{"sigma_column_id",input.sigmaColumn},{"parameters",EncodeParameters(input.parameters)}});
    for(const auto& [x,y]:s.anchors)anchors.push_back({x,y});
    return {{"id",r.id},{"name",r.name},{"version",r.version},{"worksheet_id",r.worksheet},{"x_column_id",r.xColumn},{"y_column_id",r.yColumn},{"sigma_column_id",r.sigmaColumn},
        {"exclude_filtered",r.excludeFiltered},{"show_residuals",r.showResiduals},{"auto_preview",r.autoPreview},{"global_inputs",inputs},
        {"settings",{{"operation",static_cast<int>(s.operation)},{"model_version",s.modelVersion},{"user_model_id",s.userModelId},{"weights",static_cast<int>(s.weights)},
            {"parameters",parameters},{"anchors",anchors},{"x_from",s.xFrom?Json(*s.xFrom):Json(nullptr)},{"x_to",s.xTo?Json(*s.xTo):Json(nullptr)},
            {"target_x",s.targetX},{"max_iterations",s.maxIterations},{"tolerance",s.tolerance},{"include_curves",s.includeCurves},
            {"output_from",s.outputFrom?Json(*s.outputFrom):Json(nullptr)},{"output_to",s.outputTo?Json(*s.outputTo):Json(nullptr)},
            {"acknowledge_extrapolation",s.acknowledgeExtrapolation},{"peaks",peaks}}}};
}
pl::AnalysisRecipe DecodeRecipe(const Json& j) {
    pl::AnalysisRecipe r;r.id=j.at("id");r.name=j.at("name");r.version=j.at("version");
    if(r.version!=1)throw std::invalid_argument("Unsupported analysis recipe version");
    r.worksheet=j.at("worksheet_id");r.xColumn=j.at("x_column_id");r.yColumn=j.at("y_column_id");r.sigmaColumn=j.at("sigma_column_id");
    r.excludeFiltered=j.at("exclude_filtered");r.showResiduals=j.at("show_residuals");r.autoPreview=j.at("auto_preview");
    const auto& v=j.at("settings");auto& s=r.settings;int op=v.at("operation"),weights=v.at("weights");
    if(op<0 || op>=static_cast<int>(std::size(pl::scientificOperationIds)) || weights<0 || weights>2)throw std::invalid_argument("Invalid analysis operation or weights");
    s.operation=static_cast<pl::ScientificOperation>(op);s.weights=static_cast<pl::WeightSemantics>(weights);s.modelVersion=v.at("model_version");s.userModelId=v.at("user_model_id");
    s.parameters=DecodeParameters(v.at("parameters"));
    for(const auto& a:v.at("anchors")) {if(a.size()!=2)throw std::invalid_argument("Expected physical X/Y anchor");s.anchors.emplace_back(a.at(0),a.at(1));}
    if(!v.at("x_from").is_null())s.xFrom=v.at("x_from").get<double>();if(!v.at("x_to").is_null())s.xTo=v.at("x_to").get<double>();
    s.targetX=v.at("target_x").get<std::vector<double>>();s.maxIterations=v.at("max_iterations");s.tolerance=v.at("tolerance");s.includeCurves=v.at("include_curves");
    if(v.contains("output_from") && !v.at("output_from").is_null())s.outputFrom=v.at("output_from").get<double>();
    if(v.contains("output_to") && !v.at("output_to").is_null())s.outputTo=v.at("output_to").get<double>();
    s.acknowledgeExtrapolation=v.value("acknowledge_extrapolation",false);
    if(v.contains("peaks"))for(const auto& peak:v.at("peaks")) {int shape=peak.at("shape");if(shape<0 || shape>=static_cast<int>(std::size(pl::peakShapeIds)))throw std::invalid_argument("Unknown peak shape");s.peaks.push_back({peak.at("id"),static_cast<pl::PeakShape>(shape)});}
    if(j.contains("global_inputs"))for(const auto& input:j.at("global_inputs"))r.globalInputs.push_back({input.at("id"),input.at("worksheet_id"),input.at("x_column_id"),input.at("y_column_id"),input.at("sigma_column_id"),DecodeParameters(input.at("parameters"))});
    if((s.outputFrom && !std::isfinite(*s.outputFrom)) || (s.outputTo && !std::isfinite(*s.outputTo)))throw std::invalid_argument("Invalid extrapolation range");
    if(s.modelVersion!=1 || s.maxIterations<0 || !std::isfinite(s.tolerance) || s.tolerance<=0)throw std::invalid_argument("Invalid analysis model/settings");
    return r;
}
}
WorkflowState LoadWorkflowState(const std::filesystem::path& project) {
    WorkflowState state;if(project.empty())return state;auto file=project;file+=".analysis.json";
    if(!std::filesystem::exists(file))return state; // Projects from before the analysis editor retain their existing graphs/data.
    std::ifstream in(file);Json j;in>>j;
    if(j.at("format")!="sst.plotlab.analysis-editor" || (j.at("version")!=1 && j.at("version")!=2))throw std::invalid_argument("Unsupported PlotLab analysis editor file");
    state.recipe=DecodeRecipe(j.at("recipe"));state.analysisOpen=j.at("open");state.previewView=pl::decodeView(j.at("preview_view"));state.residualView=pl::decodeView(j.at("residual_view"));
    if(j.contains("accepted_recipes"))for(const auto& item:j.at("accepted_recipes")) {const auto id=item.at("workbook_id").get<pl::Id>();if(!id || !state.acceptedRecipes.emplace(id,std::make_shared<const pl::AnalysisRecipe>(DecodeRecipe(item.at("recipe")))).second)throw std::invalid_argument("Duplicate/invalid accepted analysis workbook ID");}return state;
}
void SaveWorkflowState(const std::filesystem::path& project,const WorkflowState& state) {
    auto file=project;file+=".analysis.json";
    Json j={{"format","sst.plotlab.analysis-editor"},{"version",2},{"recipe",EncodeRecipe(state.recipe)},{"open",state.analysisOpen},{"preview_view",pl::encodeView(state.previewView)},{"residual_view",pl::encodeView(state.residualView)}};
    j["accepted_recipes"]=Json::array();for(const auto& [id,r]:state.acceptedRecipes)j["accepted_recipes"].push_back({{"workbook_id",id},{"recipe",EncodeRecipe(*r)}});
    std::string error;if(!TextFileIO::Save(Path(file),j.dump(2),error))throw std::runtime_error(error);
}
}
