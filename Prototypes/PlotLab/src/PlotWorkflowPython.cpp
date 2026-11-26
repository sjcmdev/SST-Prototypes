#include "PlotWorkflow.hpp"
#include "PlotLabExport.hpp"
#include "WorksheetPython.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/array.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace SST::Prototypes::PlotLab::PythonBinding {
namespace {
namespace nb=nanobind;
const char* Error(pl::AnalysisError);
template<class T>T Field(const nb::dict& d,const char* key,T value) {return d.contains(key)?nb::cast<T>(d[key]):std::move(value);}
void Keys(const nb::dict& d,std::initializer_list<std::string_view> allowed) {for(auto [key,value]:d)if(std::find(allowed.begin(),allowed.end(),nb::cast<std::string>(key))==allowed.end())throw std::invalid_argument("Unknown workflow field: "+nb::cast<std::string>(key));}
nb::dict Success(nb::object value) {nb::dict d;d["ok"]=true;d["result"]=std::move(value);return d;}
nb::dict Failure(const std::exception& e) {nb::dict d,error;const auto* analysis=dynamic_cast<const pl::AnalysisException*>(&e);error["type"]=analysis?Error(analysis->code):"invalid_input";error["message"]=e.what();d["ok"]=false;d["error"]=error;return d;}
template<class F>nb::dict Respond(F&& f) {try{return Success(f());}catch(const std::exception& e){return Failure(e);}}
nb::object Number(double value) {return std::isfinite(value)?nb::cast(value):nb::none();}
nb::list Numbers(const std::vector<double>& values) {nb::list out;for(auto v:values)out.append(Number(v));return out;}
pl::AnalysisRecipe Recipe(const nb::dict& d) {
    Keys(d,{"id","name","version","worksheet_id","x_column_id","y_column_id","sigma_column_id","exclude_filtered","settings","view","user_model"});
    pl::AnalysisRecipe r;r.id=Field<std::string>(d,"id","");r.name=Field<std::string>(d,"name",r.id);r.version=Field<unsigned>(d,"version",1);
    r.worksheet=nb::cast<pl::Id>(d["worksheet_id"]);r.xColumn=nb::cast<pl::Id>(d["x_column_id"]);r.yColumn=nb::cast<pl::Id>(d["y_column_id"]);r.sigmaColumn=Field<pl::Id>(d,"sigma_column_id",0);
    r.excludeFiltered=Field<bool>(d,"exclude_filtered",false);
    if(d.contains("view")){auto v=nb::cast<nb::dict>(d["view"]);Keys(v,{"show_residuals","auto_preview"});r.showResiduals=Field<bool>(v,"show_residuals",true);r.autoPreview=Field<bool>(v,"auto_preview",true);}
    if(!d.contains("settings"))return r;auto v=nb::cast<nb::dict>(d["settings"]);auto& s=r.settings;
    Keys(v,{"operation","model_version","user_model_id","weights","parameters","x_from_x_units","x_to_x_units","target_x_units","anchors","max_iterations","tolerance","include_curves"});
    auto op=Field<std::string>(v,"operation","linear");auto it=std::find_if(std::begin(pl::scientificOperationIds),std::end(pl::scientificOperationIds),[&](const char* id){return op==id;});
    if(it==std::end(pl::scientificOperationIds))throw std::invalid_argument("Unknown scientific operation: "+op);s.operation=static_cast<pl::ScientificOperation>(it-std::begin(pl::scientificOperationIds));
    s.modelVersion=Field<unsigned>(v,"model_version",1);s.userModelId=Field<std::string>(v,"user_model_id","");
    const auto weights=Field<std::string>(v,"weights","unweighted");
    if(weights=="absolute_sigma")s.weights=pl::WeightSemantics::AbsoluteSigma;else if(weights=="relative_sigma")s.weights=pl::WeightSemantics::RelativeSigma;else if(weights!="unweighted")throw std::invalid_argument("Unknown weight semantics");
    if(v.contains("parameters"))for(auto p:nb::cast<nb::list>(v["parameters"])) {auto a=nb::cast<nb::dict>(p);Keys(a,{"name","units","start_parameter_units","lower_parameter_units","upper_parameter_units","fixed"});s.parameters.push_back({nb::cast<std::string>(a["name"]),Field<std::string>(a,"units",""),Field<double>(a,"start_parameter_units",0),Field<double>(a,"lower_parameter_units",-1e100),Field<double>(a,"upper_parameter_units",1e100),Field<bool>(a,"fixed",false)});}
    if(v.contains("x_from_x_units") && !v["x_from_x_units"].is_none())s.xFrom=nb::cast<double>(v["x_from_x_units"]);
    if(v.contains("x_to_x_units") && !v["x_to_x_units"].is_none())s.xTo=nb::cast<double>(v["x_to_x_units"]);
    s.targetX=Field<std::vector<double>>(v,"target_x_units",{});
    if(v.contains("anchors"))for(auto a:nb::cast<nb::list>(v["anchors"])) {auto anchor=nb::cast<nb::dict>(a);Keys(anchor,{"x_x_units","y_y_units"});s.anchors.emplace_back(nb::cast<double>(anchor["x_x_units"]),nb::cast<double>(anchor["y_y_units"]));}
    s.maxIterations=Field<int>(v,"max_iterations",4000);s.tolerance=Field<double>(v,"tolerance",1e-8);s.includeCurves=Field<bool>(v,"include_curves",true);
    for(const auto& p:s.parameters)if(!std::isfinite(p.start) || !std::isfinite(p.lower) || !std::isfinite(p.upper) || p.lower>p.upper)throw std::invalid_argument("Expected finite parameter values and ordered bounds");
    for(auto a:s.anchors)if(!std::isfinite(a.first) || !std::isfinite(a.second))throw std::invalid_argument("Expected finite physical anchors");
    for(double x:s.targetX)if(!std::isfinite(x))throw std::invalid_argument("Expected finite target X values");return r;
}
nb::dict RecipeDict(const pl::AnalysisRecipe& r) {
    nb::dict d,s,view;d["id"]=r.id;d["name"]=r.name;d["version"]=r.version;d["worksheet_id"]=r.worksheet;d["x_column_id"]=r.xColumn;d["y_column_id"]=r.yColumn;d["sigma_column_id"]=r.sigmaColumn;d["exclude_filtered"]=r.excludeFiltered;
    const auto& settings=r.settings;s["operation"]=pl::scientificOperationIds[static_cast<unsigned>(settings.operation)];s["model_version"]=settings.modelVersion;s["user_model_id"]=settings.userModelId;
    s["weights"]=settings.weights==pl::WeightSemantics::Unweighted?"unweighted":settings.weights==pl::WeightSemantics::AbsoluteSigma?"absolute_sigma":"relative_sigma";
    nb::list parameters,anchors;for(const auto& p:settings.parameters){nb::dict a;a["name"]=p.name;a["units"]=p.units;a["start_parameter_units"]=p.start;a["lower_parameter_units"]=p.lower;a["upper_parameter_units"]=p.upper;a["fixed"]=p.fixed;parameters.append(a);}
    for(const auto& [x,y]:settings.anchors){nb::dict a;a["x_x_units"]=x;a["y_y_units"]=y;anchors.append(a);}s["parameters"]=parameters;s["anchors"]=anchors;
    s["x_from_x_units"]=settings.xFrom?nb::cast(*settings.xFrom):nb::none();s["x_to_x_units"]=settings.xTo?nb::cast(*settings.xTo):nb::none();s["target_x_units"]=nb::cast(settings.targetX);s["max_iterations"]=settings.maxIterations;s["tolerance"]=settings.tolerance;s["include_curves"]=settings.includeCurves;
    view["show_residuals"]=r.showResiduals;view["auto_preview"]=r.autoPreview;d["settings"]=s;d["view"]=view;return d;
}
const char* Error(pl::AnalysisError e) {static const char* names[]={"none","invalid_input","units","insufficient_points","rank_deficient","nonconvergence","cancelled","model","trust","stale"};return names[static_cast<int>(e)];}
nb::dict AnalysisStatusDict(const AnalysisStatus& s,bool includeArrays=true) {
    static const char* stages[]={"idle","running","preview_ready","accepted","failed","cancelled","stale"};
    nb::dict d;d["job_id"]=s.job;d["stage"]=stages[static_cast<int>(s.stage)];d["exact"]=s.exact;d["error"]=Error(s.error);d["message"]=s.message;d["previous_preview"]=s.preview && s.stage!=AnalysisStage::PreviewReady && s.stage!=AnalysisStage::Accepted;nb::list items;
    if(s.preview)for(const auto& item:s.preview->items) {const auto& r=*item.result;nb::dict v;
        v["id"]=item.id;v["recipe_id"]=item.recipeId;v["ok"]=r.ok();v["error"]=Error(r.error);v["message"]=r.message;v["model_id"]=r.modelId;v["model_version"]=r.modelVersion;
        v["project_id"]=r.project;v["worksheet_id"]=r.worksheet;v["x_column_id"]=r.xColumn;v["y_column_id"]=r.yColumn;v["revision"]=r.revision;v["x_units"]=r.xUnits;v["y_units"]=r.yUnits;v["output_units"]=r.outputUnits;
        v["point_count"]=r.x.size();v["peak_count"]=r.peakX.size();v["arrays_included"]=includeArrays;
        if(includeArrays) {
            v["x_x_units"]=Numbers(r.x);v["fitted_y_units"]=Numbers(r.values);v["observed_y_units"]=Numbers(r.observed);v["residual_y_units"]=Numbers(r.residuals);v["normalized_residual"]=Numbers(r.normalizedResiduals);v["baseline_y_units"]=Numbers(r.baseline);
            v["covariance_parameter_units_squared"]=Numbers(r.covariance);v["row_ids"]=nb::cast(r.rowIds);
            v["peak_x_x_units"]=Numbers(r.peakX);v["peak_y_y_units"]=Numbers(r.peakY);v["peak_height_y_units"]=Numbers(r.peakHeight);v["peak_fwhm_x_units"]=Numbers(r.peakFwhm);v["peak_row_ids"]=nb::cast(r.peakRowIds);
        }
        v["parameter_values_parameter_units"]=Numbers(r.parameters);v["standard_errors_parameter_units"]=Numbers(r.standardErrors);nb::list definitions;
        static constexpr const char* estimates[]={"available","fixed","active_bound","rank_deficient","insufficient_degrees_of_freedom","unavailable"};v["error_estimate"]=estimates[static_cast<int>(r.errorEstimate)];
        for(const auto& p:r.parameterDefinitions){nb::dict a;a["name"]=p.name;a["units"]=p.units;definitions.append(a);}v["parameters"]=definitions;
        v["used_points"]=r.usedPoints;v["excluded_points"]=r.excludedPoints;v["objective"]=Number(r.objective);v["degrees_of_freedom"]=r.degreesOfFreedom;items.append(v);
    }
    d["items"]=items;d["worksheet_ids"]=nb::cast(s.outputWorksheets);return d;
}
void CheckJob(PlotWorkflow& w,JobId job) {if(!job || w.analysis.Status().job!=job)throw std::invalid_argument("Unknown or replaced analysis job ID");}
pl::UserAnalysisModel UserModel(const nb::dict& d,nb::object callback,Document& doc) {
    pl::UserAnalysisModel m;if(callback.is_none())return m;
    if(!doc.PythonTrusted())throw pl::AnalysisException(pl::AnalysisError::Trust,"Trust this project before executing a user model");
    auto v=nb::cast<nb::dict>(d["user_model"]);m.id=nb::cast<std::string>(v["id"]);m.version=nb::cast<unsigned>(v["version"]);m.xUnits=nb::cast<std::string>(v["x_units"]);m.yUnits=nb::cast<std::string>(v["y_units"]);m.parameterNames=nb::cast<std::vector<std::string>>(v["parameter_names"]);
    auto owner=std::shared_ptr<nb::object>(new nb::object(std::move(callback)),[](nb::object* p){nb::gil_scoped_acquire gil;delete p;});
    auto cached=std::make_shared<std::shared_ptr<const std::vector<double>>>();
    m.evaluate=[owner,cached](std::span<const double> x,std::span<const double> parameters,std::span<double> output) {
        nb::gil_scoped_acquire gil;
        try {
            // The Python function may retain X. Its capsule owns one immutable copy per input grid.
            if(!*cached || (*cached)->size()!=x.size() || !std::equal(x.begin(),x.end(),(*cached)->begin()))*cached=std::make_shared<const std::vector<double>>(x.begin(),x.end());
            auto values=*cached;
            auto capsule=nb::capsule(new std::shared_ptr<const std::vector<double>>(values),[](void* p) noexcept{delete static_cast<std::shared_ptr<const std::vector<double>>*>(p);});
            auto xx=nb::cast(nb::ndarray<nb::numpy,const double>(values->data(),{values->size()},capsule));
            auto args=nb::cast(std::vector<double>(parameters.begin(),parameters.end()));
            auto result=nb::module_::import_("sst_plotlab.analysis").attr("evaluate_model")(*owner,xx,args);
            auto array=nb::cast<nb::ndarray<nb::numpy,const double,nb::c_contig>>(result);
            if(array.ndim()!=1 || array.size()!=output.size())throw std::invalid_argument("User model returned the wrong shape");std::copy_n(array.data(),output.size(),output.begin());
        }catch(const nb::python_error& e){throw std::runtime_error(e.what());}
    };return m;
}
pl::MapPlotRequest MapRequest(const nb::dict& d) {
    Keys(d,{"worksheet_id","x_column_id","y_column_id","z_column_id","grid_columns","name","log_color","nonpositive","contour_levels_z_units","contour_labels","colormap","color_range_z_units","mask","kind"});
    pl::MapPlotRequest r;r.worksheet=nb::cast<pl::Id>(d["worksheet_id"]);r.x=nb::cast<pl::Id>(d["x_column_id"]);r.y=nb::cast<pl::Id>(d["y_column_id"]);r.z=nb::cast<pl::Id>(d["z_column_id"]);r.gridColumns=nb::cast<std::size_t>(d["grid_columns"]);r.name=Field<std::string>(d,"name","Map");
    const auto kind=Field<std::string>(d,"kind","heatmap");if(kind=="surface")r.kind=plot::SeriesKind::Surface;else if(kind=="scatter3d")r.kind=plot::SeriesKind::Scatter3D;else if(kind=="line3d")r.kind=plot::SeriesKind::Line3D;else if(kind!="heatmap")throw std::invalid_argument("Expected heatmap/surface/scatter3d/line3d");
    r.mask=Field<std::vector<unsigned char>>(d,"mask",{});r.spec.z.scale=Field<bool>(d,"log_color",false)?plot::AxisScale::Log10:plot::AxisScale::Linear;r.spec.colorScale=true;
    auto policy=Field<std::string>(d,"nonpositive","mask");if(policy!="mask" && policy!="error")throw std::invalid_argument("Expected nonpositive policy mask/error");r.spec.nonpositiveColor=policy=="error"?plot::NonpositiveColorPolicy::Error:plot::NonpositiveColorPolicy::Mask;
    r.spec.contours.levels=Field<std::vector<double>>(d,"contour_levels_z_units",{});r.spec.contours.labels=Field<bool>(d,"contour_labels",true);r.spec.colormap=Field<std::string>(d,"colormap","Viridis");
    if(d.contains("color_range_z_units")) {auto range=nb::cast<std::vector<double>>(d["color_range_z_units"]);if(range.size()!=2)throw std::invalid_argument("Expected two physical Z bounds");r.spec.colorRange=std::array<double,2>{range[0],range[1]};}return r;
}
nb::dict BatchStatusDict(const BatchPlotStatus& s) {nb::dict d;d["running"]=s.running;d["cancelled"]=s.cancelled;d["next"]=s.next;d["graph_ids"]=nb::cast(s.graphs);d["errors"]=nb::cast(s.errors);return d;}
}
pl::UserAnalysisModel LoadUserAnalysisModel(Document& doc,const std::filesystem::path& path,pl::AnalysisRecipe& recipe) {
    if(!doc.PythonTrusted())throw pl::AnalysisException(pl::AnalysisError::Trust,"Trust this project before loading a Python model");
    nb::gil_scoped_acquire gil;
    try {
        const auto loaded=nb::cast<nb::tuple>(nb::module_::import_("sst_plotlab.analysis").attr("_load_model_file")(Path(path).Utf8()));
        nb::dict request,settings;request["user_model"]=loaded[0];request["id"]=recipe.id;request["worksheet_id"]=recipe.worksheet;request["x_column_id"]=recipe.xColumn;request["y_column_id"]=recipe.yColumn;
        settings["operation"]="user";settings["parameters"]=loaded[2];request["settings"]=settings;
        auto parameters=Recipe(request).settings.parameters;auto model=UserModel(request,nb::borrow<nb::object>(loaded[1]),doc);
        if(parameters.size()!=model.parameterNames.size())throw std::invalid_argument("Model parameter definitions must match its named parameters");
        for(std::size_t i=0;i<parameters.size();++i)if(parameters[i].name!=model.parameterNames[i])throw std::invalid_argument("Model parameter names must match in order");
        recipe.settings.operation=pl::ScientificOperation::UserFit;recipe.settings.userModelId=model.id;recipe.settings.modelVersion=model.version;recipe.settings.parameters=std::move(parameters);return model;
    }catch(const nb::python_error& e){throw std::runtime_error(e.what());}
}
void BindPlotWorkflow(nb::class_<Document>& cls) {
    cls.def("analysis_load_model",[](Document& d,const std::string& path){return Respond([&]()->nb::object{auto& w=d.Workflow();auto recipe=w.panel.Recipe();auto model=LoadUserAnalysisModel(d,std::filesystem::u8path(path),recipe);w.panel.SetRecipe(recipe);w.panel.userModel=std::move(model);d.workflowDirty=true;return RecipeDict(recipe);});});
    cls.def_prop_ro("analysis_recipes",[](Document& d){nb::list out;for(const auto& [id,recipe]:d.workflowState.acceptedRecipes){nb::dict item;item["workbook_id"]=id;item["recipe"]=RecipeDict(*recipe);out.append(item);}return out;});
    cls.def("analysis_submit",[](Document& d,nb::dict request,nb::object callback,bool preview) {return Respond([&]()->nb::object {
        auto model=UserModel(request,std::move(callback),d);auto& w=d.Workflow();if(!model.evaluate)model=w.panel.userModel;JobId id;
        if(request.contains("items")) {std::vector<std::pair<std::string,pl::AnalysisRecipe>> items;for(auto v:nb::cast<nb::list>(request["items"])) {auto item=nb::cast<nb::dict>(v);items.emplace_back(nb::cast<std::string>(item["id"]),Recipe(nb::cast<nb::dict>(item["recipe"])));}id=w.analysis.SubmitBatch(std::move(items),std::move(model),preview);}
        else {auto recipe=Recipe(request);w.panel.SetRecipe(recipe);d.workflowDirty=true;w.panel.userModel=model;id=w.analysis.Submit(std::move(recipe),std::move(model),preview);}
        nb::dict result;result["job_id"]=id;return result;
    });},nb::arg("request"),nb::arg("model_callable")=nb::none(),nb::arg("preview")=false)
    .def("analysis_status",[](Document& d,JobId id,bool arrays){return Respond([&]()->nb::object {auto& w=d.Workflow();CheckJob(w,id);return AnalysisStatusDict(w.analysis.Poll(),arrays);});},nb::arg("job_id"),nb::arg("include_arrays")=false)
    .def("analysis_wait",[](Document& d,JobId id){return Respond([&]()->nb::object {auto& w=d.Workflow();CheckJob(w,id);{nb::gil_scoped_release release;w.analysis.Wait();}const auto& s=w.analysis.Status();return AnalysisStatusDict(s,s.stage==AnalysisStage::PreviewReady || s.stage==AnalysisStage::Accepted);});})
    .def("analysis_cancel",[](Document& d,JobId id){return Respond([&]()->nb::object {auto& w=d.Workflow();CheckJob(w,id);w.analysis.Cancel();return AnalysisStatusDict(w.analysis.Status(),false);});})
    .def("analysis_accept",[](Document& d,JobId id,pl::Id folder){return Respond([&]()->nb::object {auto& w=d.Workflow();CheckJob(w,id);w.analysis.Accept(folder);return AnalysisStatusDict(w.analysis.Status(),false);});},nb::arg("job_id"),nb::arg("folder_id")=0)
    .def("create_map",[](Document& d,nb::dict request){return Respond([&]()->nb::object {nb::dict r;r["graph_id"]=d.Workflow().CreateMap(MapRequest(request));return r;});})
    .def("create_layer",[](Document& d,pl::Id graph,nb::dict request){return Respond([&]()->nb::object {
        Keys(request,{"name","frame_page_fraction","copy_layer_id"});auto& p=*d.project;const auto before=p.children(graph);
        const auto frame=Field<std::array<double,4>>(request,"frame_page_fraction",{0,0,1,1});const auto source=Field<pl::Id>(request,"copy_layer_id",0);const auto name=Field<std::string>(request,"name","Layer");
        if(source){if(p.object(source).parent!=graph)throw std::invalid_argument("Copy source must belong to this graph");d.Execute(pl::duplicateLayer(p,source,name,frame,&d.graphViews));}
        else {pl::Layer l;l.spec=std::get<pl::Layer>(p.object(before.front()).data).spec;l.spec.bounds.reset();l.spec.autoFit=plot::AutoFit::Once;l.frame=frame;d.Execute(pl::createLayer(p,graph,name,std::move(l)));}
        nb::dict result;for(auto key:p.children(graph))if(std::find(before.begin(),before.end(),key)==before.end())result["layer_id"]=key;return result;
    });})
    .def("edit_layer",[](Document& d,pl::Id layer,nb::dict request){return Respond([&]()->nb::object {
        Keys(request,{"frame_page_fraction","shared_x_layer_id","shared_y_layer_id","active"});auto l=std::get<pl::Layer>(d.project->object(layer).data);
        l.frame=Field<std::array<double,4>>(request,"frame_page_fraction",l.frame);l.sharedX=Field<pl::Id>(request,"shared_x_layer_id",l.sharedX);l.sharedY=Field<pl::Id>(request,"shared_y_layer_id",l.sharedY);l.active=Field<bool>(request,"active",l.active);
        d.Execute(pl::editLayer(*d.project,layer,std::move(l)));return nb::dict();
    });})
    .def("arrange_layers",[](Document& d,pl::Id graph,unsigned columns,bool x,bool y){return Respond([&]()->nb::object {d.Execute(pl::arrangeLayers(*d.project,graph,columns,x,y));return nb::dict();});},nb::arg("graph_id"),nb::arg("columns"),nb::arg("shared_x")=false,nb::arg("shared_y")=false)
    .def("import_image",[](Document& d,const std::string& file,nb::dict calibration){return Respond([&]()->nb::object {
        Keys(calibration,{"first_x_x_units","first_y_y_units","step_x_x_units","step_y_y_units","x_units","y_units","z_units"});
        pl::ImageGrid grid;grid.firstX=nb::cast<double>(calibration["first_x_x_units"]);grid.firstY=nb::cast<double>(calibration["first_y_y_units"]);grid.stepX=nb::cast<double>(calibration["step_x_x_units"]);grid.stepY=nb::cast<double>(calibration["step_y_y_units"]);grid.xUnit=nb::cast<std::string>(calibration["x_units"]);grid.yUnit=nb::cast<std::string>(calibration["y_units"]);grid.zUnit=nb::cast<std::string>(calibration["z_units"]);
        nb::dict r;r["graph_id"]=d.Workflow().CreateImage(LoadIntensityImage(std::filesystem::u8path(file),std::move(grid)));return r;
    });})
    .def("save_graph_template",[](Document& d,pl::Id graph,const std::string& file){return Respond([&]()->nb::object {pl::saveGraphTemplate(pl::captureGraphTemplate(*d.project,graph),std::filesystem::u8path(file));return nb::dict();});})
    .def("template_batch",[](Document& d,const std::string& file,nb::list inputs,const std::string& format,bool wait){return Respond([&]()->nb::object {
        auto t=std::make_shared<const pl::GraphTemplate>(pl::loadGraphTemplate(std::filesystem::u8path(file)));std::vector<pl::BatchPlotInput> batch;
        for(auto value:inputs){auto v=nb::cast<nb::dict>(value);batch.push_back({nb::cast<pl::Id>(v["worksheet_id"]),std::filesystem::u8path(Field<std::string>(v,"destination","")),Field<std::map<std::string,pl::Id>>(v,"bindings",{})});}
        auto plan=pl::planBatchPlots(*d.project,std::move(t),batch);nb::list items;
        for(const auto& item:plan.items) {nb::dict i;i["worksheet_id"]=item.worksheet;i["bindings"]=nb::cast(item.mapping.columns);nb::list issues;for(const auto& issue:item.mapping.issues)issues.append(issue.message);i["issues"]=issues;items.append(i);}
        nb::dict result;result["valid"]=plan.valid();result["items"]=items;
        if(plan.valid()) {auto& w=d.Workflow();w.StartBatch(std::move(plan),format,SST_PLOTLAB_PYTHON_ROOT);if(wait){nb::gil_scoped_release release;w.WaitBatch();}result["status"]=BatchStatusDict(w.PollBatch());}return result;
    });},nb::arg("template_file"),nb::arg("inputs"),nb::arg("format")="png",nb::arg("wait")=true)
    .def("template_batch_status",[](Document& d){return BatchStatusDict(d.Workflow().PollBatch());})
    .def("template_batch_cancel",[](Document& d){d.Workflow().CancelBatch();})
    .def("export_data",[](Document& d,nb::dict request){return Respond([&]()->nb::object {
        Keys(request,{"worksheet_id","column_ids","row_indices","destination","format","mask_policy","missing_policy"});
        pl::DataExportSelection s;s.worksheet=nb::cast<pl::Id>(request["worksheet_id"]);s.columns=nb::cast<std::vector<pl::Id>>(request["column_ids"]);s.rows=nb::cast<std::vector<std::size_t>>(request["row_indices"]);
        auto masks=Field<std::string>(request,"mask_policy","preserve"),missing=Field<std::string>(request,"missing_policy","empty");
        if(masks=="missing")s.masks=pl::DataMaskPolicy::Missing;else if(masks=="omit_rows")s.masks=pl::DataMaskPolicy::OmitRows;else if(masks!="preserve")throw std::invalid_argument("Invalid mask policy");
        if(missing=="nan")s.missing=pl::DataMissingPolicy::NaN;else if(missing!="empty")throw std::invalid_argument("Invalid missing policy");
        ExportRequest r;r.data=pl::snapshotData(*d.project,s);r.destination=std::filesystem::u8path(nb::cast<std::string>(request["destination"]));r.format=nb::cast<std::string>(request["format"]);r.pythonRoot=SST_PLOTLAB_PYTHON_ROOT;
        auto& w=d.Workflow();auto id=w.Export(std::move(r));{nb::gil_scoped_release release;w.WaitExport(id);}nb::dict result;result["job_id"]=id;return result;
    });})
    .def("export_figure",[](Document& d,pl::Id graph,const std::string& path,const std::string& format,double dpi,std::optional<double> width,std::optional<double> height,const std::vector<double>& background){return Respond([&]()->nb::object {
        if(width.has_value()!=height.has_value())throw std::invalid_argument("Provide both width_mm and height_mm");
        if(background.size()!=3 || std::any_of(background.begin(),background.end(),[](double v){return !std::isfinite(v) || v<0 || v>1;}))throw std::invalid_argument("Expected three finite background_rgb channels in [0,1]");
        ExportRequest r;r.graph=d.SnapshotGraph(graph,width?std::optional<std::array<double,2>>{{*width,*height}}:std::nullopt);r.destination=std::filesystem::u8path(path);r.format=format;r.dpi=dpi;r.pythonRoot=SST_PLOTLAB_PYTHON_ROOT;std::copy(background.begin(),background.end(),r.background.begin());
        auto& w=d.Workflow();auto id=w.Export(std::move(r));{nb::gil_scoped_release release;w.WaitExport(id);}nb::dict result;result["job_id"]=id;return result;
    });},nb::arg("graph_id"),nb::arg("destination"),nb::arg("format"),nb::arg("dpi")=300,nb::arg("width_mm")=nb::none(),nb::arg("height_mm")=nb::none(),nb::arg("background_rgb")=nb::make_tuple(1.,1.,1.));
}
}
#else
namespace SST::Prototypes::PlotLab::PythonBinding {
pl::UserAnalysisModel LoadUserAnalysisModel(Document&,const std::filesystem::path&,pl::AnalysisRecipe&) {throw std::runtime_error("Embedded Python is unavailable");}
}
#endif
