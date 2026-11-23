#include "Domain/BatchDraft.hpp"
#include <scaps_core/device_project_def.hpp>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
namespace SST::ScapsLab {
std::vector<double> ParseSweepList(std::string_view text) {
    std::vector<double> values;
    while(!text.empty()) {
        const auto separator=[](char c){return c==';' || std::isspace(static_cast<unsigned char>(c));};
        while(!text.empty() && separator(text.front()))text.remove_prefix(1);
        if(text.empty())break;
        std::size_t length=0;while(length<text.size() && !separator(text[length]))++length;
        auto token=text.substr(0,length);text.remove_prefix(length);
        if(token.front()=='+')token.remove_prefix(1);
        double value=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
        if(token.empty() || parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size() || !std::isfinite(value))
            throw std::invalid_argument("Invalid list element "+std::to_string(values.size()+1)+" (use decimal point; separate with spaces or semicolons)");
        if(values.size()==10000)throw std::invalid_argument("Maximum 10000 members");
        values.push_back(value);
    }
    if(values.empty())throw std::invalid_argument("List requires 1..10000 values");
    return values;
}
std::vector<double> SweepValues(const SweepRange& range) {
    if(range.sampling==SweepSampling::List) {
        if(range.custom.empty() || range.custom.size()>10000)throw std::invalid_argument("List requires 1..10000 values");
        for(std::size_t i=0;i<range.custom.size();++i)if(!std::isfinite(range.custom[i]))throw std::invalid_argument("Invalid list element "+std::to_string(i+1));
        return range.custom;
    }
    const double lo=range.minimum,hi=range.maximum;
    if(!std::isfinite(lo) || !std::isfinite(hi) || lo>hi)throw std::invalid_argument("Minimum must be finite and <= maximum");
    if(range.logarithmic && lo<=0)throw std::invalid_argument("Log range requires positive endpoints");
    if(range.sampling==SweepSampling::Step && (!std::isfinite(range.step) || range.step<=0))throw std::invalid_argument("Step must be finite and positive");
    if(range.sampling==SweepSampling::Points && (range.points<1 || range.points>10000 || (lo!=hi && range.points<2)))throw std::invalid_argument("Use 2..10000 points, or one point for equal endpoints");
    if(lo==hi)return {lo};
    std::vector<double> values{lo};
    if(range.sampling==SweepSampling::Points) {
        for(int i=1;i<range.points-1;++i) {
            const double t=double(i)/(range.points-1);
            values.push_back(range.logarithmic?std::pow(10.,std::lerp(std::log10(lo),std::log10(hi),t)):std::lerp(lo,hi,t));
        }
    } else {
        const long double a=range.logarithmic?std::log10(lo):lo,b=range.logarithmic?std::log10(hi):hi;
        long double intervals=(b-a)/range.step;
        if(!std::isfinite(intervals))throw std::invalid_argument("Maximum 10000 members");
        const auto nearest=std::round(intervals);
        if(std::abs(intervals-nearest)<=8*std::numeric_limits<double>::epsilon()*std::abs(intervals))intervals=nearest;
        intervals=std::max(1.L,std::ceil(intervals));
        if(intervals>9999)throw std::invalid_argument("Maximum 10000 members");
        for(int i=1;i<int(intervals);++i) {
            const long double x=a+i*static_cast<long double>(range.step);
            const double value=range.logarithmic?double(std::pow(10.L,x)):double(x);
            if(value>=hi)break;
            if(value<=values.back())throw std::invalid_argument("Step is below numeric precision");
            values.push_back(value);
        }
    }
    values.push_back(hi);
    for(std::size_t i=1;i<values.size();++i)if(!std::isfinite(values[i]) || values[i]<=values[i-1])throw std::invalid_argument("Sampling exceeds numeric precision");
    return values;
}
std::vector<double> SweepNativeValues(const SweepEditor& editor) {
    if(!std::isfinite(editor.displayScale) || editor.displayScale<=0)throw std::invalid_argument("Invalid display unit scale");
    if(editor.component!=SweepComponent::FullValue && (!std::isfinite(editor.fixed) || (editor.component==SweepComponent::Exponent && (editor.fixed<1 || editor.fixed>=10)) || (editor.component==SweepComponent::Mantissa && editor.fixed!=std::floor(editor.fixed))))throw std::invalid_argument("Invalid fixed component");
    if(editor.component==SweepComponent::Exponent && (editor.range.logarithmic || (editor.range.sampling==SweepSampling::Step && editor.range.step!=std::floor(editor.range.step))))throw std::invalid_argument("Exponent requires integer linear sampling");
    auto values=SweepValues(editor.range);
    for(std::size_t i=0;i<values.size();++i) {
        double& value=values[i];
        long double native=value;
        if(editor.component==SweepComponent::Mantissa) {
            if(value<1 || value>=10)throw std::invalid_argument("Mantissa must be >= 1 and < 10 (element "+std::to_string(i+1)+")");
            native*=std::pow(10.L,editor.fixed);
        } else if(editor.component==SweepComponent::Exponent) {
            if(value!=std::floor(value))throw std::invalid_argument("Exponent must be an integer (element "+std::to_string(i+1)+")");
            native=editor.fixed*std::pow(10.L,value);
        }
        value=double(native/editor.displayScale);
        if(!std::isfinite(value) || (native!=0 && value==0) || (editor.component!=SweepComponent::FullValue && value==0))throw std::invalid_argument("Sweep value overflow/underflow (element "+std::to_string(i+1)+")");
    }
    return values;
}
std::vector<double> BatchAxisValues(const BatchAxis& axis) {
    if(axis.values)return *axis.values;
    const int n=axis.count;const double a=axis.start,b=axis.stop;
    if(n<1 || n>10000 || !std::isfinite(a) || !std::isfinite(b) || (axis.spacing!="linear" && axis.spacing!="log") || (axis.spacing=="log" && (a<=0 || b<=0)))throw std::invalid_argument("Invalid axis range/count/spacing");
    std::vector<double> values;
    for(int i=0;i<n;++i){const double t=n==1?0:double(i)/(n-1);values.push_back(i==0?a:i==n-1?b:axis.spacing=="log"?std::exp(std::lerp(std::log(a),std::log(b),t)):std::lerp(a,b,t));}
    return values;
}
SweepEditor NewSweep(const NumericParameter& parameter,SweepComponent component,double scale,std::string unit) {
    SweepEditor editor;editor.component=component;editor.displayScale=scale;editor.displayUnit=unit.empty()?parameter.unit:std::move(unit);
    const double value=parameter.value*scale;
    if(!std::isfinite(value) || (component!=SweepComponent::FullValue && value<=0))throw std::invalid_argument("Zero has no mantissa/exponent; use Full value");
    if(component==SweepComponent::FullValue){const double delta=std::max(1e-30,std::abs(value)*.1);editor.range.minimum=value-delta;editor.range.maximum=value+delta;editor.range.step=delta/2;}
    else {
        double exponent=std::floor(std::log10(value)),mantissa=double(value/std::pow(10.L,exponent));
        if(mantissa>=10){mantissa/=10;++exponent;}else if(mantissa<1){mantissa*=10;--exponent;}
        editor.fixed=component==SweepComponent::Mantissa?exponent:mantissa;
        editor.range.minimum=component==SweepComponent::Mantissa?1:exponent-1;
        editor.range.maximum=component==SweepComponent::Mantissa?9:exponent+1;
        editor.range.step=1;editor.range.sampling=SweepSampling::Step;
    }
    return editor;
}
SweepEditor EditSweep(const BatchAxis& axis,const NumericParameter& parameter) {
    if(axis.sweep_ui)try{if(SweepNativeValues(*axis.sweep_ui)==BatchAxisValues(axis))return *axis.sweep_ui;}catch(const std::exception&){}
    SweepEditor editor;editor.displayUnit=parameter.unit;
    if(axis.values || axis.start>axis.stop || (axis.start==axis.stop && axis.count>1) || (axis.count==1 && axis.start!=axis.stop)) {
        editor.range.sampling=SweepSampling::List;editor.range.custom=BatchAxisValues(axis);
    } else {editor.range.minimum=axis.start;editor.range.maximum=axis.stop;editor.range.points=axis.count;editor.range.logarithmic=axis.spacing=="log";}
    return editor;
}
BatchAxis PrepareSweepAxis(const ProjectDraft& project,const std::string& id,const SweepEditor& editor) {
    const auto parameters=ProjectParameters(project);const auto found=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==id;});
    if(found==parameters.end())throw std::invalid_argument("Unknown parameter_id; select it from the project tree");
    BatchAxis axis;axis.parameter_id=id;axis.path=found->path;axis.unit=found->unit;axis.values=SweepNativeValues(editor);axis.sweep_ui=editor;
    auto test=project;
    for(std::size_t i=0;i<axis.values->size();++i)try{SetProjectParameter(test.project,axis.path,(*axis.values)[i]);ValidateDraft(test);}catch(const std::exception& e){throw std::invalid_argument("Element "+std::to_string(i+1)+": "+e.what());}
    return axis;
}
namespace {
using Json=nlohmann::json;
Json Scalar(double v){if(std::isinf(v))return v>0?Json(".inf"):Json("-.inf");if(std::isnan(v))return nullptr;return v;}
Json RecorderJson(const std::vector<scaps_core::RecorderColumn>& columns) {
    auto out=Json::array();for(const auto& c:columns) {
        Json v={{"id",c.id},{"quantity",c.quantity}};if(!c.unit.empty())v["unit"]=c.unit;if(!c.field.empty())v["field"]=c.field;
        for(const auto& [key,value]:{std::pair{"voltage_V",c.voltage_V},{"frequency_Hz",c.frequency_Hz},{"x_m",c.x_m},{"voltage_min_V",c.voltage_min_V},{"voltage_max_V",c.voltage_max_V}})if(std::isfinite(value))v[key]=value;out.push_back(std::move(v));
    }return out;
}
Json TransitionJson(const scaps_core::MetastableSpec& s) {
    Json out={{"id",s.id},{"layer_index",s.layer_index},{"donor_index",s.donor_index},{"acceptor_index",s.acceptor_index},{"donor_charge",s.donor_charge},{"total_density_m3",s.total_density_m3},{"EC_eV",s.EC_eV},{"HC_eV",s.HC_eV},{"TR_eV",s.TR_eV},{"initial_acceptor_fraction",s.initial_acceptor_fraction},{"phonon_frequency_Hz",s.phonon_frequency_Hz},
        {"electron_process",s.electron_process==scaps_core::MetastableProcess::DoubleCapture?"double_capture":"capture_and_emission"},{"hole_process",s.hole_process==scaps_core::MetastableProcess::DoubleCapture?"double_capture":"capture_and_emission"},{"transition_reference",s.transition_reference==scaps_core::DefectEnergyReference::RelativeToEc?"Ec":s.transition_reference==scaps_core::DefectEnergyReference::RelativeToEv?"Ev":"Ei"}};
    for(const auto& [key,v]:{std::pair{"sigma_don_n_m2",s.sigma_don_n_m2},{"sigma_don_p_m2",s.sigma_don_p_m2},{"sigma_acc_n_m2",s.sigma_acc_n_m2},{"sigma_acc_p_m2",s.sigma_acc_p_m2}})if(v)out[key]=*v;return out;
}
Json LightPatchJson(const IlluminationPatch& patch) {auto out=Json::object();for(const auto& [path,v]:patch)out[path.substr(13)]=std::visit([](const auto& value)->Json{if constexpr(std::is_same_v<std::decay_t<decltype(value)>,double>)return Scalar(value);else return value;},v);return out;}
Json ConditioningJson(const BatchDraft& d) {
    if(!d.conditioning)return nullptr;const auto& c=*d.conditioning;const auto& s=c.settings;
    Json out={{"max_iterations",s.max_iterations},{"relative_tolerance",s.relative_tolerance},{"clamping",s.clamping},{"final_unclamped_cycle",s.final_unclamped_cycle},{"residual_convergence",s.residual_convergence},{"initial_voltage_V",c.initial_voltage_V}};
    if(!d.projectTransitions){out["transitions"]=Json::array();for(const auto& t:c.transitions)out["transitions"].push_back(TransitionJson(t));}
    if(c.initial_temperature_K)out["initial_temperature_K"]=*c.initial_temperature_K;if(c.initial_illumination)out["initial_illumination"]=IlluminationDocument(*c.initial_illumination);if(c.initial_mesh_coordinates_m)out["initial_mesh_coordinates_m"]=*c.initial_mesh_coordinates_m;return out;
}
Json SweepEditorJson(const SweepEditor& e) {
    const auto& r=e.range;
    return {{"component",int(e.component)},{"fixed",e.fixed},{"display_scale",e.displayScale},{"display_unit",e.displayUnit},
        {"sampling",int(r.sampling)},{"logarithmic",r.logarithmic},{"minimum",r.minimum},{"maximum",r.maximum},{"step",r.step},{"points",r.points},{"custom",r.custom}};
}
void ReadSweepEditor(BatchAxis& axis,const Json& v) {
    try {
        SweepEditor e;const int component=v.at("component"),sampling=v.at("sampling");
        if(component<0 || component>2 || sampling<0 || sampling>2)return;
        e.component=SweepComponent(component);e.fixed=v.at("fixed");e.displayScale=v.at("display_scale");e.displayUnit=v.at("display_unit");
        auto& r=e.range;r.sampling=SweepSampling(sampling);r.logarithmic=v.at("logarithmic");r.minimum=v.at("minimum");r.maximum=v.at("maximum");r.step=v.at("step");r.points=v.at("points");r.custom=v.at("custom").get<std::vector<double>>();
        if(SweepNativeValues(e)==BatchAxisValues(axis))axis.sweep_ui=std::move(e);
    } catch(const std::exception&) {} // Incompatible UI metadata must never change the native samples.
}
Json Canonical(const BatchDraft& d) {
    Json axes=Json::array();for(const auto& a:d.axes){Json axis={{"parameter_id",a.parameter_id},{"path",a.path},{"unit",a.unit}};if(a.values)axis["values"]=*a.values;else {axis["start"]=a.start;axis["stop"]=a.stop;axis["count"]=a.count;axis["spacing"]=a.spacing;}if(a.sweep_ui)axis["sweep_ui"]=SweepEditorJson(*a.sweep_ui);axes.push_back(std::move(axis));}
    Json out={{"mode",d.mode},{"axes",axes},{"include_curves",d.include_curves},{"include_profiles",d.include_profiles},{"threads",d.threads},{"photovoltaic_voltage_limit_V",d.photovoltaic_voltage_limit_V},{"metastable",ConditioningJson(d)}};
    for(const auto& [key,value]:{std::pair{"voltages_V",d.voltages_V},{"frequencies_Hz",d.frequencies_Hz},{"mesh_coordinates_m",d.mesh_coordinates_m}})out[key]=value?Json(*value):Json{};
    out["incident_power_W_m2"]=d.incident_power_W_m2?Json(*d.incident_power_W_m2):Json{};
    out["initial_state"]=nullptr;out["initial_state_file"]=nullptr;out["scaps_root"]=nullptr;out["export_initial_state_wp2"]=false;
    if(d.initial){const auto& i=*d.initial;out["export_initial_state_wp2"]=i.exportWp2;if(!i.file.empty())out["initial_state_file"]=i.file;if(!i.scapsRoot.empty())out["scaps_root"]=i.scapsRoot;
        if(i.explicitObject){Json v=Json::object();if(i.temperature_K)v["temperature_K"]=*i.temperature_K;if(i.voltage_V)v["voltage_V"]=*i.voltage_V;if(i.number_of_points)v["number_of_points"]=*i.number_of_points;if(i.generation_total_m2_s)v["generation_total_m2_s"]=*i.generation_total_m2_s?Json(**i.generation_total_m2_s):Json{};if(!i.illumination.empty())v["illumination"]=LightPatchJson(i.illumination);out["initial_state"]=std::move(v);}
    }return out;
}
void ApplyInitial(const InitialStateDraft& input,scaps_core::BatchInitialStateOptions& options) {
    if(input.temperature_K)options.temperature_K=*input.temperature_K;if(input.voltage_V)options.voltage_V=*input.voltage_V;if(input.number_of_points)options.number_of_points=*input.number_of_points;
    if(input.generation_total_m2_s)options.generation_total_m2_s=*input.generation_total_m2_s;
    if(!options.illumination)options.illumination.emplace();ApplyIlluminationPatch(*options.illumination,input.illumination);
    if(input.illumination.contains("illumination.uniform_generation_m3_s"))options.generation_total_m2_s.reset();
}
}
std::shared_ptr<const RecorderDraft> ReadRecorderDraft(const Json& input) {
    if(!input.is_array())throw std::invalid_argument("columns must be a list");auto out=std::make_shared<RecorderDraft>();out->columns.clear();
    for(const auto& v:input){scaps_core::RecorderColumn c;c.id=v.at("id");c.quantity=v.at("quantity");c.unit=v.value("unit",std::string{});c.field=v.value("field",std::string{});c.voltage_V=v.value("voltage_V",double(NAN));c.frequency_Hz=v.value("frequency_Hz",double(NAN));c.x_m=v.value("x_m",double(NAN));c.voltage_min_V=v.value("voltage_min_V",-INFINITY);c.voltage_max_V=v.value("voltage_max_V",INFINITY);if((v.contains("voltage_min_V")&&!std::isfinite(c.voltage_min_V)) || (v.contains("voltage_max_V")&&!std::isfinite(c.voltage_max_V)))throw std::invalid_argument("Vbi window bounds must be finite");out->columns.push_back(std::move(c));}
    scaps_core::validate_recorder_columns(out->columns);out->source=std::make_shared<const Json>(input);out->canonical=std::make_shared<const Json>(RecorderJson(out->columns));return out;
}
Json RecorderDraftJson(const RecorderDraft& d){const auto current=RecorderJson(d.columns);return d.source?MergeFileDocument(*d.source,*d.canonical,current):current;}
std::shared_ptr<const BatchDraft> ReadBatchDraft(const Json& input) {
    if(!input.is_object())throw std::invalid_argument("Batch spec must be an object");auto out=std::make_shared<BatchDraft>();out->axes.clear();out->mode=input.value("mode",std::string("nested"));out->include_curves=input.value("include_curves",true);out->include_profiles=input.value("include_profiles",false);out->threads=input.value("threads",1);
    if(!input.contains("axes") || !input.at("axes").is_array() || input.at("axes").empty())throw std::invalid_argument("Select at least one axis");
    if(out->mode!="linked" && out->mode!="together" && input.at("axes").size()>2)throw std::invalid_argument("Nested mode supports at most two axes");
    for(const auto& v:input.at("axes")){BatchAxis a;a.parameter_id=v.value("parameter_id",std::string{});a.path=v.value("path",std::string{});a.unit=v.value("unit",std::string{});a.spacing=v.value("spacing",std::string("linear"));if(v.contains("values")){if(!v.at("values").is_array())throw std::invalid_argument("Values must be a numeric list");a.values=v.at("values").get<std::vector<double>>();}else {a.start=v.at("start");a.stop=v.at("stop");if(!v.at("count").is_number_integer())throw std::invalid_argument("Member count must be an integer");const double n=v.at("count").get<double>();if(n<1 || n>10000)throw std::invalid_argument("Invalid axis range/count/spacing");a.count=int(n);}if(v.contains("sweep_ui"))ReadSweepEditor(a,v.at("sweep_ui"));out->axes.push_back(std::move(a));}
    if(input.contains("voltages_V"))out->voltages_V=input.at("voltages_V").get<std::vector<double>>();if(input.contains("frequencies_Hz"))out->frequencies_Hz=input.at("frequencies_Hz").get<std::vector<double>>();if(input.contains("mesh_coordinates_m"))out->mesh_coordinates_m=input.at("mesh_coordinates_m").get<std::vector<double>>();out->photovoltaic_voltage_limit_V=input.value("photovoltaic_voltage_limit_V",2.);if(input.contains("incident_power_W_m2"))out->incident_power_W_m2=input.at("incident_power_W_m2");
    if(input.contains("metastable")){const auto& v=input.at("metastable");if(!v.is_object() || (v.contains("transitions")&&!v.at("transitions").is_array()))throw std::invalid_argument("metastable transitions must be a list");out->conditioning.emplace();auto& c=*out->conditioning;out->projectTransitions=!v.contains("transitions");if(!out->projectTransitions)for(const auto& t:v.at("transitions"))c.transitions.push_back(ReadMetastableSpec(t));auto& s=c.settings;s.max_iterations=v.value("max_iterations",s.max_iterations);s.relative_tolerance=v.value("relative_tolerance",s.relative_tolerance);s.clamping=v.value("clamping",s.clamping);s.final_unclamped_cycle=v.value("final_unclamped_cycle",s.final_unclamped_cycle);s.residual_convergence=v.value("residual_convergence",s.residual_convergence);if(v.contains("initial_temperature_K"))c.initial_temperature_K=v.at("initial_temperature_K");c.initial_voltage_V=v.value("initial_voltage_V",0.);if(v.contains("initial_illumination")){c.initial_illumination.emplace();ApplyIlluminationPatch(*c.initial_illumination,ReadIlluminationPatch(v.at("initial_illumination")));}if(v.contains("initial_mesh_coordinates_m"))c.initial_mesh_coordinates_m=v.at("initial_mesh_coordinates_m").get<std::vector<double>>();}
    if(input.contains("initial_state") || input.contains("initial_state_file") || input.value("export_initial_state_wp2",false)) {
        out->initial.emplace();auto& i=*out->initial;i.explicitObject=input.contains("initial_state");i.exportWp2=input.value("export_initial_state_wp2",false);i.file=input.value("initial_state_file",std::string{});i.scapsRoot=input.value("scaps_root",std::string{});
        if(i.explicitObject){const auto& v=input.at("initial_state");if(!v.is_object())throw std::invalid_argument("initial_state must be an object");for(const auto& [key,value]:v.items())if(key!="temperature_K" && key!="voltage_V" && key!="number_of_points" && key!="generation_total_m2_s" && key!="illumination"){if(key=="frequency_Hz")throw scaps_core::DefError("unsupported_parameter","SCAPS initial working point has no supported frequency (manual 4.4, 7.3)");throw std::invalid_argument("unknown initial_state field: "+key);}if(v.contains("temperature_K"))i.temperature_K=v.at("temperature_K");if(v.contains("voltage_V"))i.voltage_V=v.at("voltage_V");if(v.contains("number_of_points")){if(!v.at("number_of_points").is_number_integer())throw std::invalid_argument("initial_state.number_of_points must be an integer");i.number_of_points=v.at("number_of_points").get<int>();}if(v.contains("generation_total_m2_s"))i.generation_total_m2_s=v.at("generation_total_m2_s").is_null()?std::optional<double>{}:std::optional<double>{v.at("generation_total_m2_s").get<double>()};if(v.contains("illumination"))i.illumination=ReadIlluminationPatch(v.at("illumination"));for(const auto& [key,value]:v.items())if(key=="illumination"){for(const auto& [field,scalar]:value.items())i.provenance_spellings["illumination."+field]=scalar.dump();}else i.provenance_spellings[key]=value.dump();if(i.generation_total_m2_s && *i.generation_total_m2_s && i.illumination.contains("illumination.uniform_generation_m3_s"))throw std::invalid_argument("initial_state must use total generation or volumetric generation, not both");}
    }
    out->source=std::make_shared<const Json>(input);out->canonical=std::make_shared<const Json>(Canonical(*out));return out;
}
Json BatchDraftJson(const BatchDraft& d) {
    auto canonical=Canonical(d);if(d.include_profiles)canonical["include_profiles"]=true;if(d.source)return MergeFileDocument(*d.source,*d.canonical,canonical);
    auto out=canonical;for(const auto* key:{"threads","photovoltaic_voltage_limit_V","metastable","incident_power_W_m2","voltages_V","frequencies_Hz","mesh_coordinates_m","initial_state","initial_state_file","scaps_root","export_initial_state_wp2"})if(out.at(key).is_null() || (std::string_view(key)=="threads" && d.threads==1) || (std::string_view(key)=="photovoltaic_voltage_limit_V" && d.photovoltaic_voltage_limit_V==2) || (std::string_view(key)=="export_initial_state_wp2" && !out.at(key).get<bool>()))out.erase(key);
    if(!d.include_profiles)out.erase("include_profiles");
    for(auto& a:out["axes"]){if(a.at("path")=="")a.erase("path");if(a.at("unit")=="")a.erase("unit");}return out;
}
BatchPlan PrepareBatch(const ProjectDraft& project,const BatchDraft& input,const RecorderDraft& recorder) {
    BatchPlan out;out.spec=input;auto& spec=out.spec;const bool linked=spec.mode=="linked" || spec.mode=="together";
    if(spec.mode!="nested" && !linked)throw std::invalid_argument("Mode must be nested or linked");
    if(spec.axes.empty() || (!linked && spec.axes.size()>2))throw std::invalid_argument("Select one axis, two nested axes, or linked parameters");
    const auto parameters=ProjectParameters(project);out.combinations.emplace_back();std::set<std::string> seen;
    for(auto& axis:spec.axes) {
        const auto found=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return !axis.parameter_id.empty()?p.id==axis.parameter_id:p.path==axis.path;});if(found==parameters.end())throw std::invalid_argument("Unknown numeric parameter; select it from the project tree");if(!seen.insert(found->path).second)throw std::invalid_argument("Axes must use different parameters");axis.path=found->path;axis.parameter_id=found->id;axis.unit=found->unit;
        const auto values=BatchAxisValues(axis);
        if(values.empty() || values.size()>10000)throw std::invalid_argument("Axis requires 1..10000 values");for(double v:values)if(!std::isfinite(v))throw std::invalid_argument("Axis values must be finite numbers");
        if(linked){if(out.combinations.size()==1 && out.combinations[0].empty())out.combinations.assign(values.size(),{});if(out.combinations.size()!=values.size())throw std::invalid_argument("Linked parameters require equal lengths");for(std::size_t i=0;i<values.size();++i)out.combinations[i][axis.path]=values[i];}
        else {if(out.combinations.size()>10000/values.size())throw std::invalid_argument("Maximum 10000 members");std::vector<std::map<std::string,double>> expanded;for(const auto& c:out.combinations)for(double v:values){auto point=c;point[axis.path]=v;expanded.push_back(std::move(point));}out.combinations=std::move(expanded);}
    }
    auto& core=out.engine;core.voltages_V=spec.voltages_V.value_or(scaps_core::make_voltage_list(project.project.voltage));core.frequencies_Hz=spec.frequencies_Hz.value_or(std::vector<double>{});core.columns=recorder.columns;scaps_core::validate_recorder_columns(core.columns);
    if(core.voltages_V.empty() || core.voltages_V.size()>10001)throw std::invalid_argument("Voltage sampling requires 1..10001 points");for(double v:core.voltages_V)if(!std::isfinite(v))throw std::invalid_argument("Invalid voltage sample");if(core.frequencies_Hz.size()>1000)throw std::invalid_argument("Invalid frequency sampling");for(double f:core.frequencies_Hz)if(!std::isfinite(f) || f<=0)throw std::invalid_argument("Frequency must be positive");
    const auto [low,high]=std::minmax_element(core.voltages_V.begin(),core.voltages_V.end());const double first=*low,last=*high;
    for(const auto& c:core.columns){const double v=c.voltage_V;if(std::isfinite(v) && v>=first && v<=last && std::find(core.voltages_V.begin(),core.voltages_V.end(),v)==core.voltages_V.end())for(std::size_t i=1;i<core.voltages_V.size();++i){const double a=core.voltages_V[i-1],b=core.voltages_V[i];if(v>=std::min(a,b) && v<=std::max(a,b)){core.voltages_V.insert(core.voltages_V.begin()+i,v);break;}}if(std::isfinite(c.frequency_Hz) && std::find(core.frequencies_Hz.begin(),core.frequencies_Hz.end(),c.frequency_Hz)==core.frequencies_Hz.end())core.frequencies_Hz.push_back(c.frequency_Hz);}
    if(core.voltages_V.size()>10001 || core.frequencies_Hz.size()>1000)throw std::invalid_argument("Recorder samples exceed batch sampling limits");spec.voltages_V=core.voltages_V;spec.frequencies_Hz=core.frequencies_Hz;spec.threads=1;out.recorder=recorder;out.recorder.columns=core.columns;core.photovoltaic=true;core.photovoltaic_voltage_limit_V=spec.photovoltaic_voltage_limit_V;core.incident_power_W_m2=spec.incident_power_W_m2;core.mesh_coordinates_m=spec.mesh_coordinates_m;return out;
}
BatchDraft PrepareQuickSweep(const ProjectDraft& project,const std::string& id,double delta,int points,const std::string& spacing) {
    const auto parameters=ProjectParameters(project);const auto parameter=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==id;});if(parameter==parameters.end())throw std::invalid_argument("Unknown parameter_id");if(!std::isfinite(delta) || delta<=0)throw std::invalid_argument("delta must be positive, in the parameter's native unit");BatchDraft out;auto& axis=out.axes[0];axis.parameter_id=id;axis.start=parameter->value-delta;axis.stop=parameter->value+delta;axis.count=points;axis.spacing=spacing;return out;
}
Json BatchPlanJson(const BatchPlan& plan) {
    auto spec=BatchDraftJson(plan.spec);spec["include_curves"]=plan.spec.include_curves;spec["threads"]=1;spec["columns"]=RecorderDraftJson(plan.recorder);spec["voltages_V"]=plan.engine.voltages_V;spec["frequencies_Hz"]=plan.engine.frequencies_Hz;
    return {{"spec",spec},{"combinations",plan.combinations},{"member_count",plan.combinations.size()},{"bias_count",plan.engine.voltages_V.size()},{"frequency_count",plan.engine.frequencies_Hz.size()},{"sample_count",plan.SampleCount()},{"voltages_V",plan.engine.voltages_V},{"frequencies_Hz",plan.engine.frequencies_Hz},{"time_estimate_s",nullptr},{"time_estimate_status","unavailable before first run"}};
}
scaps_core::BatchSpec PrepareBatchMember(const scaps_core::DeviceProject& project,const BatchDraft& draft,scaps_core::BatchSpec spec) {
    if(draft.conditioning){auto c=*draft.conditioning;if(draft.projectTransitions)c.transitions=project.metastable_transitions;spec.conditioning=scaps_core::make_batch_conditioning(project,std::move(c));}
    if(draft.initial){const auto& input=*draft.initial;scaps_core::BatchInitialStateOptions options;options.illumination=project.illumination;options.illumination->enabled=false;options.export_wp2=input.exportWp2;options.explicit_conditioning=draft.conditioning.has_value();options.provenance_scalar_spellings=input.provenance_spellings;ApplyInitial(input,options);std::optional<scaps_core::BatchSource> source;
        if(!input.file.empty()){auto file=scaps_core::read_batch_initial_state_file(input.file,input.scapsRoot,options);options=std::move(file.options);source=std::move(file.source);ApplyInitial(input,options);}else if(input.exportWp2 && !input.explicitObject && draft.conditioning){const auto& c=*draft.conditioning;options.temperature_K=c.initial_temperature_K.value_or(project.temperature_K);options.voltage_V=c.initial_voltage_V;options.illumination=c.initial_illumination.value_or(project.illumination);}
        auto state=scaps_core::make_batch_initial_state(project,options);state.source=std::move(source);spec.initial_state=std::make_shared<const scaps_core::BatchInitialState>(std::move(state));}
    return spec;
}
}
