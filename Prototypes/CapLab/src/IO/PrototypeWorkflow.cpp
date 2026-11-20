#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include <sst/plotlab/SnapshotJson.hpp>
namespace SST::Prototypes::CapLab
{
    void DocumentJob::Execute(JobContext &context)
    {
        context.SetProgress(0, 1);
        context.SetStage(operation == Operation::Preview ? "Reading import data" : operation == Operation::Open || operation == Operation::OpenOrImport ? "Opening project / import file" : operation == Operation::Save ? "Saving project" : "Exporting data / graph snapshots");
        context.ThrowIfCancellationRequested();
        std::string error;
        if (operation == Operation::Open || operation == Operation::Preview || operation == Operation::OpenOrImport)
        {
            std::string text;
            if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
            if (operation == Operation::OpenOrImport) {
                const auto json=Json::parse(text,nullptr,false);
                operation=json.is_object() && json.contains("schema_version") ? Operation::Open : Operation::Preview;
                if (operation==Operation::Preview && importOptions.empty()) {
                    const auto extension=path.Native().extension();
                    if (extension==".json") importOptions=R"({"profile":"propho_json"})";
                    else if (extension==".cf") importOptions=R"({"profile":"scaps_cf"})";
                    else if (extension==".cv") importOptions=R"({"profile":"scaps_cv"})";
                }
            }
            if (operation == Operation::Open) opened.emplace(Parse(text));
            else
            {
                pl::ImportOptions options; options.decimal = decimal; options.extra["path"] = path.Utf8(); options.extra["options"] = importOptions;
                const auto table = importer->preview(text, options, {
                    [&] { return context.IsCancellationRequested(); },
                    [&](double value) { context.SetProgress(static_cast<float>(value), 1); }});
                preview = Json::parse(table.source.at("preview_json"));
                // Validate before the GUI renders metadata from an untrusted file.
                if (preview.contains("samples")) {
                    Project checked; BundleChange validation(checked,preview);
                } else if (!Optional(preview.at("temperature_K")))
                    (void)Optional(preview.at("suggested_temperature_K"));
            }
        }
        else if (operation == Operation::Analyze)
        {
            auto p = Parse(input);
            Json measurement;
            if (analysisOptions.at("type")=="WalterSaved") {
                bool found=false;
                for (const auto &h : p.historical) if (h.at("sample_id")==analysisOptions.at("sample_id")) {
                    if (found) throw std::runtime_error("Ambiguous historical Walter record");
                    analysisOptions["historical"]=h; found=true;
                }
                if (!found) throw std::runtime_error("Missing historical Walter record");
            } else if (analysisOptions.at("type")=="Walter") {
                analysisOptions["measurements"]=Json::array();
                const auto ids=analysisOptions.contains("measurement_ids") ? analysisOptions.at("measurement_ids") : Json::array({analysisOptions.at("measurement_id")});
                std::set<pl::Id> seen;
                for (const auto &id : ids) {
                    const auto mid=id.get<pl::Id>();
                    if (!seen.insert(mid).second) throw std::runtime_error("Duplicate Walter measurement");
                    analysisOptions["measurements"].push_back(AnalysisMeasurement(p,mid));
                }
                if (ids.empty()) throw std::runtime_error("Select Walter measurements");
                measurement=analysisOptions.at("measurements").at(0);
                analysisOptions["source_analyses"]=Json::array();
                for (const auto *key : {"cv_fit_id","arrhenius_fit_id","dlcp_fit_id"}) if (analysisOptions.contains(key)) {
                    bool found=false;
                    for (const auto &a : p.analyses) if (a.at("id")==analysisOptions.at(key)) {
                        if (a.at("status")!="valid" || a.at("type")!=(std::string(key)=="cv_fit_id" ? "CV" : std::string(key)=="dlcp_fit_id" ? "DLCPPolynomial" : "Arrhenius"))
                            throw std::runtime_error("Stale/invalid Walter source analysis");
                        analysisOptions["source_analyses"].push_back(a); found=true;
                    }
                    if (!found) throw std::runtime_error("Missing Walter source analysis");
                }
            } else if (analysisOptions.at("type")=="Arrhenius") {
                bool found=false;
                for (const auto &d : p.defects) if (d.at("id")==analysisOptions.at("defect_id")) { analysisOptions["defect"]=d; found=true; }
                if (!found) throw std::runtime_error("Missing defect.");
                analysisOptions["point_inputs"]=Json::array(); std::set<pl::Id> seen;
                for (const auto &point : analysisOptions.at("defect").at("points"))
                    if (point.at("enabled").get<bool>() && !point.at("measurement_id").is_null()) {
                        const auto id=point.at("measurement_id").get<pl::Id>();
                        const auto m=AnalysisMeasurement(p,id);
                        const auto rows=m.at("row_ids").get<std::vector<pl::Id>>();
                        const auto row=std::find(rows.begin(),rows.end(),point.at("row_id").get<pl::Id>());
                        const auto names=m.at("names").get<std::vector<std::string>>();
                        const auto f=std::find(names.begin(),names.end(),"f");
                        if (row==rows.end() || f==names.end() || m.at("temperature_K")!=point.at("temperature_K") ||
                            m.at("columns").at(std::distance(names.begin(),f)).at(std::distance(rows.begin(),row))!=point.at("frequency_Hz"))
                            throw std::runtime_error("Point input changed; reassign the point before fitting.");
                        if (seen.insert(id).second) analysisOptions["point_inputs"].push_back(m);
                    }
            } else if (analysisOptions.at("type")!="Circuit" || analysisOptions.value("fit",false) || analysisOptions.contains("measurement_id"))
                measurement=AnalysisMeasurement(p,analysisOptions.at("measurement_id").get<pl::Id>());
            Json xd;
            if (analysisOptions.at("type")=="Walter" && analysisOptions.value("xd_source",std::string("CV"))=="CV" &&
                !analysisOptions.contains("xd_measurement_id") && analysisOptions.contains("cv_fit_id"))
                for (const auto &a : analysisOptions.at("source_analyses")) if (a.at("type")=="CV") {
                    analysisOptions["xd_measurement_id"]=a.at("inputs").at(0).at("id");
                    if (!analysisOptions.contains("material")) {
                        analysisOptions["material"]="project"; analysisOptions["project_material"]=a.at("material");
                    }
                }
            if (analysisOptions.at("type")=="Walter" && analysisOptions.value("xd_source",std::string("CV"))=="DLCP" && analysisOptions.contains("dlcp_fit_id"))
                for (const auto &a : analysisOptions.at("source_analyses")) if (a.at("type")=="DLCPPolynomial")
                    analysisOptions["xd_measurement_id"]=a.at("inputs").at(0).at("id");
            if (analysisOptions.contains("xd_measurement_id")) xd = AnalysisMeasurement(p,analysisOptions.at("xd_measurement_id").get<pl::Id>());
            Json result;
            if (analysisOptions.at("type")=="Circuit") {
                const auto it=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==analysisOptions.at("circuit_id");});
                if (it==p.circuits.end()) throw std::runtime_error("Missing circuit");
                if (analysisOptions.value("fit",false)) {
                    result=FitIS(measurement,*it,analysisOptions,context);
                    *it=result.at("circuit");
                    RefreshAnalyses(p);
                } else {
                    result=SimulateIS(measurement,*it,analysisOptions,context);
                    if (measurement.is_null()) {
                        auto overlay=[&](pl::Id id) {
                            context.ThrowIfCancellationRequested();
                            const auto m=AnalysisMeasurement(p,id);
                            if (m.at("sample_id")==it->at("sample_id") && m.at("kind")=="FrequencySweep" &&
                                (m.at("variant")=="ReZ_ImZ" || m.at("variant")=="C_G" || m.at("variant")=="Cp_Rp"))
                                OverlayISMeasurement(result,m);
                        };
                        for (const auto &m : p.sweeps) overlay(m.id);
                        for (const auto &m : p.measurements) overlay(m.at("id"));
                    }
                }
            } else result = ReadAnalysis(measurement,analysisOptions,xd,context);
            result["input_revision"] = analysisOptions.at("input_revision");
            AppendAnalysis(p,std::move(result)); opened = std::move(p);
        }
        else if (operation == Operation::Save)
        {
            auto project = Parse(input);
            if (FileSystem::Exists(path.Native()))
            {
                std::string existing;
                if (!TextFileIO::Load(path,existing,error)) throw std::runtime_error(error);
                (void)Parse(existing);
            }
            context.ThrowIfCancellationRequested();
            if (!TextFileIO::Save(path,Serialize(project),error)) throw std::runtime_error(error);
            saveCommitted = true;
        }
        else
        {
            const auto format=path.Native().extension().string();
            if (format==".csv" || format==".xlsx") {
                preview=ExportData(Parse(input),path,context);
            } else {
            if (format!=".png" && format!=".svg" && format!=".pdf") throw std::runtime_error("Choose CSV, XLSX, PNG, SVG or PDF export.");
            if (!snapshot && snapshots.empty()) throw std::runtime_error("No current graph snapshot.");
            std::vector<PlotExportRequest> exports;
            auto add = [&](const pl::GraphSnapshot& graph, const std::filesystem::path& destination) {
                auto text = pl::SnapshotJson(graph);
                exports.push_back({text, destination, format.substr(1), dpi});
                return Json::parse(text);
            };
            if (snapshots.empty()) preview=add(*snapshot,path.Native());
            else {
                preview={{"graphs",Json::array()}};
                for (const auto& graph : snapshots) {
                    auto destination=path.Native().parent_path() / path.Native().stem();
                    destination+=std::filesystem::path("-"+std::to_string(graph->graph)+format);
                    auto result=add(*graph,destination);
                    preview["graphs"].push_back({{"graph_id",graph->graph},{"path",Path::FromResolved(destination).Utf8()},{"snapshot",std::move(result)}});
                }
            }
            ExportPlotsJson(context,exports);
            }
        }
        if (!saveCommitted && operation!=Operation::Export) context.ThrowIfCancellationRequested();
        context.SetProgress(1,1);
    }
    void CapLabWorkflow::Start(Operation operation, Path target)
    {
        try
        {
            if (activeJob) throw std::runtime_error("Wait for the current job or cancel it.");
            if ((operation == Operation::Open || operation == Operation::OpenOrImport) && document.dirty) throw std::runtime_error("Save pending changes before opening another project.");
            if (operation != Operation::Analyze && target.Empty())
            {
                if (operation == Operation::Save) target = document.path;
                if (target.Empty())
                {
                    const NativeFileDialogFilter filter{operation == Operation::Preview ? "CapLab measurements / ProPho" : operation == Operation::Export ? "CapLab export (choose extension)" : "CapLab project", operation == Operation::Preview ? "dat,txt,csv,cf,cv,json" : operation == Operation::Export ? "csv,xlsx,png,svg,pdf" : "json"};
                    const auto selected = operation == Operation::Save || operation == Operation::Export
                        ? SaveFileDialog(filter,operation == Operation::Save ? "project.caplab.json" : ("graph."+exportFormat).c_str()) : OpenFileDialog(filter);
                    if (!selected) { status = "Dialog cancelled."; return; }
                    target = *selected;
                }
            }
            if (operation==Operation::Save && document.project.graph && view.viewport) {
                auto& engine=*document.project.engine;
                auto layer=engine.object(engine.children(document.project.graph).at(0));
                std::get<pl::Layer>(layer.data).view=view;
                engine.change("Save graph view",{std::move(layer)}).apply(engine);
            }
            auto job = CreateRef<DocumentJob>(operation,operation == Operation::Save || operation == Operation::Analyze || operation == Operation::Export ? Serialize(document.project) : "",target);
            job->analysisOptions = analysisOptions; job->analysisOptions["input_revision"] = document.revision;
            job->decimal = decimal; job->importOptions = importOptions; job->importer = labImporter;
            const auto extension=target.Native().extension();
            if (operation == Operation::Export && (extension==".png" || extension==".svg" || extension==".pdf")) {
                job->dpi=exportDpi;
                if (exportGraphs.empty()) job->snapshot=pl::snapshotGraph(*document.project.engine,document.project.graph,view);
                else {
                    std::set<pl::Id> seen;
                    for (auto id : exportGraphs) {
                        if (!seen.insert(id).second) throw std::runtime_error("Duplicate export graph_id");
                        const auto found=graphViews.find(id);
                        if (found==graphViews.end()) throw std::runtime_error("Render each selected graph before batch export");
                        job->snapshots.push_back(pl::snapshotGraph(*document.project.engine,id,found->second));
                    }
                }
            }
            pendingRevision = document.revision;
            activeJob = jobs->Submit(job);
            if (!activeJob) throw std::runtime_error("Job submission failed.");
            pending = std::move(job); status.clear(); failure.reset();
            if (operation==Operation::Analyze) analysisDraft.reset();
            if (operation == Operation::Preview) preview.reset();
        }
        catch (const std::exception &error) {
            status=error.what();
            failure=StructuredError{ErrorCategory::Validation,Severity::Error,"Document operation failed",status,"Check the file and import settings.","CapLab","caplab.document"};
        }
    }
    void CapLabWorkflow::Changed()
    {
        RefreshAnalyses(document.project);
        document.Changed(); view = {}; graphViews.clear(); exportGraphs.clear();
        if (bus) { DocumentChanged e; e.project = document.project.engine->id(); e.revision = document.revision; bus->Queue(e); }
    }
    void CapLabWorkflow::OnUpdate(float)
    {
        if (!activeJob) return;
        const auto job = jobs->GetJob(activeJob);
        if (job && (job->status == JobStatus::Queued || job->status == JobStatus::Running)) return;
        if (!job) status = "Job removed before completion.";
        else if (pending->saveCommitted) { document.Saved(pending->path,pendingRevision); status = "Saved."; }
        else if (job->status == JobStatus::Completed)
        {
            status = "Completed.";
            if (pending->operation == Operation::Export) {status = "Exported."; exportResult=pending->preview;}
            else if (document.revision != pendingRevision) status = "Stale revision: result discarded.";
            else if (pending->operation == Operation::Analyze)
            {
                analysisDraft = std::move(pending->opened); analysisRevision = pendingRevision;
                if (pending->analysisOptions.value("fit",false)) status="Fit draft ready; review and accept or discard.";
                else if (commands) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
            }
            else if (pending->operation == Operation::Open)
            {
                if (undo) undo->Clear();
                document.Opened(std::move(*pending->opened),pending->path,pendingRevision);
                preview.reset(); view = {}; graphViews.clear(); exportGraphs.clear();
                if(document.project.graph) {const auto& p=*document.project.engine;view=std::get<pl::Layer>(p.object(p.children(document.project.graph).at(0)).data).view;}
                if (bus) { DocumentChanged e; e.project = document.project.engine->id(); e.revision = document.revision; bus->Queue(e); }
            }
            else if (pending->operation == Operation::Preview)
            {
                preview = std::move(pending->preview); previewRevision = pendingRevision;
                importOptions=pending->importOptions;
                if (preview->contains("samples")) { status = "Preview ready; inspect samples, units and provenance before confirming."; }
                else {
                temperature = Optional(preview->at("temperature_K"));
                if (!temperature) temperature = Optional(preview->at("suggested_temperature_K"));
                status = "Preview ready; check units and temperature before confirming."; }
            }
        }
        else {
            status = job->status == JobStatus::Cancelled ? "Cancelled." : job->errorMessage;
            if (job->status!=JobStatus::Cancelled)
                failure=StructuredError{ErrorCategory::Validation,Severity::Error,"Document operation failed",status,"Check the file and import settings.","CapLab",pending->operation==Operation::Preview ? "caplab.preview" : "caplab.document"};
        }
        activeJob = 0; pending.reset();
    }
}
