#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "Domain/CapLabCompare.hpp"
#include "Domain/CapLabSourceOperations.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/IO/RecentFiles.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <cctype>
namespace SST::Prototypes::CapLab
{
    void DocumentJob::Execute(JobContext &context)
    {
        context.SetProgress(0, 1);
        context.SetStage(operation == Operation::Preview ? "Reading import data" : operation == Operation::Open || operation == Operation::OpenOrImport ? "Opening project / import file" : operation == Operation::Save ? "Saving project" : "Exporting data / graph snapshots");
        context.ThrowIfCancellationRequested();
        std::string error;
        if (operation==Operation::Relink) {
            context.SetStage("Checking source provenance");
            std::string text;
            if (!TextFileIO::Load(path,text,error)) throw std::runtime_error(error);
            const auto origin=ReadLabAS(text,path,decimal,context,Json::object(),true);
            auto draft=std::move(input); RelinkSource(draft,measurementId,origin);
            preview={{"measurement_id",measurementId},{"source",path.Utf8()},{"provenance_only",true}};
            context.ThrowIfCancellationRequested(); opened=std::move(draft); context.SetProgress(1,1); return;
        }
        if (operation == Operation::Open || operation == Operation::Preview || operation == Operation::OpenOrImport || operation==Operation::Reimport)
        {
            std::string text;
            if (operation == Operation::Open || operation == Operation::OpenOrImport) {
                if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
                if (operation == Operation::OpenOrImport) {
                    const auto json=Json::parse(text,nullptr,false);
                    operation=json.is_object() && json.contains("schema_version") ? Operation::Open : Operation::Preview;
                }
            }
            if (operation == Operation::Open) opened.emplace(Parse(text));
            else {
                if (sources.empty()) sources.push_back(path);
                const auto collected = CollectSources(sources, recursive, [&](const Path &source) {
                    return importer->recognizes(source.Utf8(), "");
                });
                sourcePreviews = PreviewSources<Json>(context, collected, [&](const ImportSource &source) -> Result<Json> {
                    std::string content, readError;
                    if (!TextFileIO::Load(source.path, content, readError)) throw std::runtime_error(readError);
                    auto parserOptions = importOptions;
                    if (parserOptions.empty()) {
                        auto extension = source.path.extension().Utf8();
                        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (extension == ".json") parserOptions={{"profile","propho_json"}};
                        else if (extension == ".cf") parserOptions={{"profile","scaps_cf"}};
                        else if (extension == ".cv") parserOptions={{"profile","scaps_cv"}};
                    }
                    auto value = importer->preview(content,source.path,decimal,parserOptions,context);
                    // Validate before rendering metadata from an untrusted file.
                    if (value.contains("samples")) { Project checked; BundleChange validation(checked,value); }
                    else {
                        const auto temperature=Optional(value.at("temperature_K"));
                        ValidatePreview(value,temperature);
                        if (!temperature) (void)Optional(value.at("suggested_temperature_K"));
                    }
                    return value;
                });
                if (sourcePreviews.empty()) throw std::runtime_error("No supported CapLab data files were found.");
                for (const auto &item : sourcePreviews) if (item.value) { preview=*item.value; break; }
                if (operation==Operation::Reimport) {
                    if (sourcePreviews.size()!=1 || !sourcePreviews[0].value) throw std::runtime_error("Source preview failed: "+(sourcePreviews[0].error ? sourcePreviews[0].error->technicalDetails : std::string{}));
                    auto draft=std::move(input);
                    const auto imported=preview;
                    preview=ReplaceMeasurement(draft,measurementId,imported);
                    preview["import_preview"]=imported;
                    opened=std::move(draft);
                }
            }
        }
        else if (operation == Operation::Plot)
        {
            auto p=std::move(input);
            context.ThrowIfCancellationRequested();
            BuildCompareGraph(p,analysisOptions,plotView);
            context.ThrowIfCancellationRequested();
            opened=std::move(p);
        }
        else if (operation == Operation::Analyze)
        {
            auto p = std::move(input);
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
            auto project = std::move(input);
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
                preview=ExportData(input,path,context);
            } else {
            if (format!=".png" && format!=".svg" && format!=".pdf") throw std::runtime_error("Choose CSV, XLSX, PNG, SVG or PDF export.");
            if (!snapshot && snapshots.empty()) throw std::runtime_error("No current graph snapshot.");
            if (snapshots.empty()) graphExport.graphs.push_back({snapshot,path});
            else {
                graphExport.batch=true;
                for (const auto& graph : snapshots) {
                    auto destination=path.Native().parent_path() / path.Native().stem();
                    destination+=std::filesystem::path("-"+std::to_string(graph->graph)+format);
                    graphExport.graphs.push_back({graph,Path::FromResolved(destination)});
                }
            }
            ExportGraphs(graphExport,dpi,context);
            }
        }
        if (!saveCommitted && operation!=Operation::Export) context.ThrowIfCancellationRequested();
        context.SetProgress(1,1);
    }
    void CapLabWorkflow::Start(Operation operation, Path target, std::vector<Path> sources, bool recursive)
    {
        if (operation==Operation::Analyze) analysisQueued=false;
        try
        {
            if (activeJob) throw std::runtime_error("Wait for the current job or cancel it.");
            if (closeRequested && operation != Operation::Save && operation != Operation::SaveAs && operation != Operation::Close)
                throw std::runtime_error("Resolve the pending close request first.");
            if (operation == Operation::New) {
                CapLabDocument fresh; fresh.project.importProfiles=Document().project.importProfiles;
                const auto handle = workspace.Open(std::move(fresh), false);
                (void)Activate(handle); status = "New project."; return;
            }
            if (operation == Operation::Close) { (void)RequestClose(); return; }
            auto &document = Document();
            if (operation==Operation::Reimport || operation==Operation::Relink) {
                const auto meta=SourceMetadata(document.project,sourceMeasurement);
                if (target.Empty()) {
                    if (operation==Operation::Relink) {
                        const auto selected=OpenFileDialog({"Moved measurement source","dat,txt,csv,cf,cv,json"});
                        if (!selected) {status="Dialog cancelled."; return;}
                        target=*selected;
                    } else target=Path::FromResolved(std::filesystem::u8path(meta.at("source").get<std::string>()));
                }
                auto options=meta.value("parser_options",Json::object()); options["profile"]=meta.at("profile");
                if (!meta.contains("parser_options") && meta.contains("temperature_K") && !meta.at("temperature_K").is_null()) options["temperature_K"]=meta.at("temperature_K");
                importOptions=std::move(options); decimal=meta.value("decimal",std::string("."))[0];
                sourceDraft.reset(); sourceDiff=nullptr;
                sourceMetadataConfirmed=false;
            }
            const bool saveAs = operation == Operation::SaveAs;
            if (saveAs) operation = Operation::Save;
            if (operation == Operation::PreviewFolder) {
                if (target.Empty() && sources.empty()) sources=PickFoldersDialog(true);
                operation=Operation::Preview;
                if (target.Empty() && sources.empty()) { status="Dialog cancelled."; return; }
            }
            if (operation == Operation::Preview) {
                if (!target.Empty()) sources.push_back(target);
                if (sources.empty()) sources=OpenFilesDialog({"CapLab measurements / ProPho", "dat,txt,csv,cf,cv,json"});
                if (sources.empty()) { status="Dialog cancelled."; return; }
                target=sources.front();
            }
            if (operation != Operation::Analyze && operation != Operation::Plot && target.Empty())
            {
                if (operation == Operation::Save && !saveAs) target = document.path.value_or(Path{});
                if (target.Empty())
                {
                    const NativeFileDialogFilter filter{operation == Operation::Preview ? "CapLab measurements / ProPho" : operation == Operation::Export ? "CapLab export (choose extension)" : "CapLab project", operation == Operation::Preview ? "dat,txt,csv,cf,cv,json" : operation == Operation::Export ? "csv,xlsx,png,svg,pdf" : "json"};
                    const auto selected = operation == Operation::Save || operation == Operation::Export
                        ? SaveFileDialog(filter,operation == Operation::Save ? "project.caplab.json" : ("graph."+exportFormat).c_str()) : OpenFileDialog(filter);
                    if (!selected) { status = "Dialog cancelled."; return; }
                    target = *selected;
                }
            }
            if (operation == Operation::Open || operation == Operation::OpenOrImport) {
                if (const auto existing = workspace.FindByPath(target)) { (void)Activate(*existing); TouchRecent(recent,*Document().path); status = "Project already open."; return; }
            }
            if (operation == Operation::Save) {
                if (const auto existing = workspace.FindByPath(target); existing && *existing != workspace.active)
                    throw std::runtime_error("This path belongs to another open project.");
            }
            if (operation==Operation::Save) {
                if (document.project.graph && view.viewport) StoreGraphView(document.project.graph,view);
                for (const auto &[id,value]:graphViews) StoreGraphView(id,value);
            }
            auto job = CreateRef<DocumentJob>(operation,operation == Operation::Analyze || operation == Operation::Plot || operation == Operation::Save || operation == Operation::Reimport || operation == Operation::Relink ? EditSnapshot() : operation == Operation::Export ? CloneProject(document.project) : Project{},target);
            job->measurementId=sourceMeasurement;
            job->plotView=plotView;
            job->analysisOptions = analysisOptions; job->analysisOptions["input_revision"] = document.revision;
            job->sources=std::move(sources); job->recursive=recursive;
            job->decimal = decimal; job->importOptions = importOptions; job->importer = labImporter;
            const auto extension=target.Native().extension();
            if (operation == Operation::Export && (extension==".png" || extension==".svg" || extension==".pdf")) {
                job->dpi=exportDpi;
                if (exportGraphs.empty()) job->snapshot=pl::snapshotGraph(*document.project.engine,document.project.graph,view.viewport ? view : GraphView(document.project.graph));
                else {
                    std::set<pl::Id> seen;
                    for (auto id : exportGraphs) {
                        if (!seen.insert(id).second) throw std::runtime_error("Duplicate export graph_id");
                        job->snapshots.push_back(pl::snapshotGraph(*document.project.engine,id,GraphView(id)));
                    }
                }
            }
            pendingRevision = document.revision; pendingProject = workspace.active;
            pendingSelectionRevision = document.selectionRevision;
            activeJob = jobs->Submit(job);
            if (!activeJob) throw std::runtime_error("Job submission failed.");
            pending = std::move(job); status.clear(); failure.reset();
            if (operation==Operation::Analyze) analysisDraft.reset();
            if (operation == Operation::Preview || operation == Operation::OpenOrImport) {
                pendingGeneration=previewGeneration.Next();
                metadataConfirmed=false;
                preview.reset(); sourcePreviews.clear(); selectedSources.clear(); sourceTemperatures.clear(); previewSourceIndex=-1;
            }
        }
        catch (const std::exception &error) {
            status=error.what();
            failure=StructuredError{ErrorCategory::Validation,Severity::Error,"Document operation failed",status,"Check the file and import settings.","CapLab","caplab.document"};
        }
    }
    void CapLabWorkflow::ClearPreview()
    {
        previewGeneration.Next(); preview.reset(); sourcePreviews.clear();
        selectedSources.clear(); sourceTemperatures.clear(); previewSourceIndex=-1;
        metadataConfirmed=false;
    }
    void CapLabWorkflow::OnEvent(Event &event)
    {
        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<FilesDroppedEvent>([&](const auto &drop) {
            if (drop.paths.empty() || !commands) return false;
            CommandContext context; context.Set("paths",drop.paths); context.Set("recursive",recursiveImport);
            (void)commands->Execute(CommandID{"caplab.preview"},std::move(context));
            return true;
        });
    }
    Json CapLabWorkflow::ImportGroups() const
    {
        std::map<std::string,Json> groups;
        for (const auto &source:sourcePreviews) if (source.value) {
            auto add=[&](const Json &m, std::size_t sample, std::size_t measurement) {
                auto partitions=m.value("groups",Json::array());
                if (partitions.empty()) partitions.push_back(Json::object());
                for (const auto &part:partitions) {
                Json key{{"target_sample_id",importTargetSample},{"kind",m.value("kind",std::string("FrequencySweep"))},
                    {"variant",m.value("variant",std::string("Cp_Rp"))},{"temperature_K",part.value("temperature_K",m.value("temperature_K",Json(nullptr)))},
                    {"frequency_Hz",part.value("frequency_Hz",m.value("frequency_Hz",Json(nullptr)))},{"bias_V",part.value("bias_V",m.value("bias_V",Json(nullptr)))}};
                const auto id=key.dump();
                if (!groups.contains(id)) {key["sources"]=Json::array(); groups[id]=std::move(key);}
                groups[id]["sources"].push_back({{"source_id",source.source.path.Utf8()},{"sample_index",sample},{"measurement_index",measurement},{"row_indices",part.value("rows",Json(nullptr))}});
                }
            };
            if (source.value->contains("samples")) {
                const auto &samples=source.value->at("samples");
                for (std::size_t s=0;s<samples.size();++s) {
                    const auto &measurements=samples[s].at("measurements");
                    for (std::size_t m=0;m<measurements.size();++m) add(measurements[m],s,m);
                }
            } else add(*source.value,0,0);
        }
        Json result=Json::array(); for (const auto &[key,value]:groups) result.push_back(value); return result;
    }
    void CapLabWorkflow::SelectPreview(int index)
    {
        if (index==previewSourceIndex) return;
        if (previewSourceIndex>=0 && previewSourceIndex<static_cast<int>(sourceTemperatures.size()))
            sourceTemperatures[previewSourceIndex]=temperature;
        previewSourceIndex=index; preview.reset(); temperature.reset();
        if (index<0 || index>=static_cast<int>(sourcePreviews.size())) return;
        preview=sourcePreviews[index].value;
        if (preview && !preview->contains("samples")) {
            temperature=sourceTemperatures.at(index);
        }
    }
    Project CapLabWorkflow::EditSnapshot() const
    {
        return CloneProject(Document().project,Document().identityHighWater);
    }
    void CapLabWorkflow::Changed(bool inputsChanged)
    {
        auto &document = Document();
        if (inputsChanged) ++document.inputRevision;
        document.identityHighWater=std::max(document.identityHighWater,NextEntityId(*document.project.engine));
        RefreshAnalyses(document.project);
        RefreshComparisonLabels(document.project);
        ReconcileSelection();
        document.Changed(); view = {}; graphViews.clear(); exportGraphs.clear();
        if (bus) { DocumentChanged e; e.project = document.handle; e.revision = document.revision; bus->Queue(e); }
    }
    void CapLabWorkflow::OnUpdate(float)
    {
        if (!activeJob) return;
        auto &document = *workspace.Find(pendingProject);
        const auto job = jobs->GetJob(activeJob);
        if (job && (job->status == JobStatus::Queued || job->status == JobStatus::Running)) return;
        const bool saved = pending->saveCommitted;
        if (!job) status = "Job removed before completion.";
        else if (pending->saveCommitted) { document.Saved(pending->path,pendingRevision); TouchRecent(recent,pending->path); status = "Saved."; }
        else if (job->status == JobStatus::Completed)
        {
            status = "Completed.";
            if (pending->operation == Operation::Export) {status = "Exported."; exportResult=std::move(pending->preview); graphExport=std::move(pending->graphExport);}
            else if (document.revision != pendingRevision ||
                (pending->analysisOptions.value("preview",false) && document.selectionRevision!=pendingSelectionRevision) ||
                (pending->operation==Operation::Preview && !previewGeneration.IsCurrent(pendingGeneration))) status = "Stale revision: result discarded.";
            else if (pending->operation==Operation::Reimport || pending->operation==Operation::Relink) {
                sourceDraft=std::move(pending->opened); sourceDiff=pending->preview;
                sourceRevision=pendingRevision; sourceRelink=pending->operation==Operation::Relink; sourceMetadataConfirmed=false;
                status="Source diff ready; review and confirm.";
            }
            else if (pending->operation == Operation::Analyze || pending->operation == Operation::Plot)
            {
                analysisDraft = std::move(pending->opened); analysisRevision = pendingRevision;
                if (pending->analysisOptions.value("fit",false) || pending->analysisOptions.value("preview",false)) status="Preview ready; review and accept or discard. Accepted results are unchanged.";
                else if (commands) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
            }
            else if (pending->operation == Operation::Open)
            {
                CapLabDocument opened;
                opened.Opened(std::move(*pending->opened),pending->path,0);
                for (const auto &[name,profile]:Document().project.importProfiles) opened.project.importProfiles.try_emplace(name,profile);
                const auto handle = workspace.Open(std::move(opened), false);
                // Release the completed job before switching the active document.
                activeJob = 0;
                (void)Activate(handle);
                TouchRecent(recent,pending->path);
            }
            else if (pending->operation == Operation::Preview)
            {
                previewRevision = pendingRevision;
                sourcePreviews=std::move(pending->sourcePreviews);
                previewInputs=pending->sources; previewRecursive=pending->recursive;
                selectedSources.clear(); sourceTemperatures.clear(); previewSourceIndex=-1;
                for (int i=0;i<static_cast<int>(sourcePreviews.size());++i) {
                    std::optional<double> value;
                    if (sourcePreviews[i].value) {
                        selectedSources.push_back(i);
                        const auto &item=*sourcePreviews[i].value;
                        if (!item.contains("samples")) {
                            value=Optional(item.at("temperature_K"));
                        }
                    }
                    sourceTemperatures.push_back(value);
                }
                SelectPreview(selectedSources.empty() ? 0 : selectedSources.front());
                status="Preview ready; select sources and inspect units and metadata before confirming.";
                if (selectedSources.empty()) {
                    status=sourcePreviews.front().error->technicalDetails;
                    failure=StructuredError{ErrorCategory::Validation,Severity::Error,"Import preview failed",status,
                        "Check the file and import settings.","CapLab","caplab.preview"};
                }
            }
        }
        else {
            status = job->status == JobStatus::Cancelled ? "Cancelled." : job->errorMessage;
            if (job->status!=JobStatus::Cancelled)
                failure=StructuredError{ErrorCategory::Validation,Severity::Error,"Document operation failed",status,"Check the file and import settings.","CapLab",pending->operation==Operation::Preview ? "caplab.preview" : "caplab.document"};
        }
        activeJob = 0; pending.reset();
        if (saveBeforeClose) {
            saveBeforeClose = false;
            if (saved && !Document().IsDirty()) ResolveClose("discard");
            else if (saved) status = "Newer edits remain unsaved; review before closing.";
        }
    }
}

namespace SST::Prototypes::CapLab
{
    void CapLabWorkflow::ResetProjectUI()
    {
        sourceDraft.reset(); sourceDiff=nullptr; sourceMeasurement=0;
        ClearPreview(); graphViews.clear(); exportGraphs.clear(); importTargetSample=0;
        analysisDraft.reset(); pendingCandidate = nullptr;
        analysisOptions = Json::object(); view = {}; failure.reset(); status = "Ready.";
        const auto &p = Document().project;
        if (p.graph) view=std::get<pl::Layer>(p.engine->object(p.engine->children(p.graph).at(0)).data).view;
        if (bus) { DocumentChanged event; event.project = workspace.active; event.revision = Document().revision; bus->Queue(event); }
    }
    bool CapLabWorkflow::Activate(std::uint64_t handle)
    {
        if (activeJob || analysisQueued || !workspace.Find(handle)) return false;
        if (workspace.active == handle) return true;
        if (undo) std::swap(*undo, Document().history);
        (void)workspace.Activate(handle);
        if (undo) std::swap(*undo, Document().history);
        ResetProjectUI(); return true;
    }
    bool CapLabWorkflow::IsDirty() const
    {
        return std::any_of(workspace.projects.begin(), workspace.projects.end(), [](const auto &d) { return d.IsDirty(); });
    }
    bool CapLabWorkflow::CloseProject(bool discard)
    {
        if (activeJob || analysisQueued || (Document().IsDirty() && !discard)) return false;
        if (undo) undo->Clear();
        (void)workspace.Close(workspace.active);
        if (workspace.projects.empty()) (void)workspace.Open(CapLabDocument{});
        if (undo) std::swap(*undo, Document().history);
        ResetProjectUI(); return true;
    }
    bool CapLabWorkflow::RequestClose(bool application)
    {
        if (application && !activeJob && !IsDirty()) return true;
        if (!application && !activeJob && !Document().IsDirty()) return CloseProject();
        closeApplication = application; closeReady = false; closeRequested = true;
        if (application && !activeJob && !Document().IsDirty())
            for (const auto &d : workspace.projects) if (d.IsDirty()) { (void)Activate(d.handle); break; }
        return false;
    }
    void CapLabWorkflow::ResolveClose(const std::string &decision, Path target)
    {
        if (decision == "cancel") { closeRequested = false; closeApplication = false; saveBeforeClose = false; return; }
        if (!closeRequested || activeJob) return;
        if (decision == "save") {
            Start(Operation::Save, std::move(target)); saveBeforeClose = activeJob != 0; return;
        }
        if (decision != "discard") throw std::runtime_error("Unknown close decision.");
        const bool application = closeApplication;
        if (!CloseProject(true)) return;
        closeRequested = false;
        if (application) {
            if (IsDirty()) (void)RequestClose(true);
            else closeReady = true;
        }
    }
    Json CapLabWorkflow::WorkspaceJson() const
    {
        Json projects = Json::array();
        for (const auto &d : workspace.projects) projects.push_back({{"project_id",d.handle},
            {"project",ProjectJson(d.project)},{"path",d.path ? Json(d.path->Utf8()) : Json(nullptr)},
            {"revision",d.revision},{"saved_revision",d.savedRevision},{"dirty",d.IsDirty()},
            {"next_entity_id",d.identityHighWater},
            {"selection",{{"kind",d.selectionKind},{"sample_id",d.selectedSample},{"measurement_id",d.selectedMeasurement},
                {"defect_id",d.selectedDefect},{"analysis_id",d.selectedAnalysis},{"circuit_id",d.selectedCircuit}}}});
        Json files = Json::array(); for (const auto &path : recent) files.push_back(path.Utf8());
        return {{"projects",projects},{"active_project_id",workspace.active},{"next_project_id",workspace.nextHandle},
            {"recent_projects",files},{"close_requested",closeRequested},{"close_application",closeApplication},{"close_ready",closeReady}};
    }
    void CapLabWorkflow::RestoreWorkspace(const Json &value)
    {
        ProjectWorkspace<CapLabDocument> restored;
        for (const auto &item : value.at("projects")) {
            if (!item.at("project_id").is_number_unsigned() || !item.at("revision").is_number_unsigned() || !item.at("saved_revision").is_number_unsigned())
                throw std::runtime_error("Project IDs and revisions must be nonnegative integers.");
            CapLabDocument d; d.handle = item.at("project_id").get<std::uint64_t>();
            if (!d.handle || d.handle == UINT64_MAX || restored.Find(d.handle)) throw std::runtime_error("Invalid or duplicate project_id.");
            if (item.contains("next_entity_id") && !item.at("next_entity_id").is_number_unsigned()) throw std::runtime_error("Invalid next_entity_id.");
            d.identityHighWater=item.value("next_entity_id",pl::Id{0});
            auto project=item.at("project");
            // Reconcile an external workspace counter once, before native Undo snapshots.
            if (project.at("engine").at("nextId").get<pl::Id>()<d.identityHighWater) project["engine"]["nextId"]=d.identityHighWater;
            d.project = ProjectFromJson(project);
            if (item.contains("selection")) {
                const auto &s=item.at("selection");
                d.selectionKind=s.value("kind",std::string{}); d.selectedSample=s.value("sample_id",pl::Id{0});
                d.selectedMeasurement=s.value("measurement_id",pl::Id{0}); d.selectedDefect=s.value("defect_id",pl::Id{0});
                d.selectedAnalysis=s.value("analysis_id",pl::Id{0}); d.selectedCircuit=s.value("circuit_id",pl::Id{0});
            }
            if (!item.at("path").is_null()) d.path = Path{item.at("path").get<std::string>()};
            d.revision = item.at("revision"); d.savedRevision = item.at("saved_revision");
            if (d.revision == UINT64_MAX || (d.path && d.path->Empty()) || d.savedRevision > d.revision) throw std::runtime_error("Invalid saved revision.");
            if (d.path && restored.FindByPath(*d.path)) throw std::runtime_error("Duplicate project path.");
            restored.nextHandle = std::max(restored.nextHandle, d.handle + 1);
            restored.projects.push_back(std::move(d));
        }
        if (!value.at("active_project_id").is_number_unsigned() || !restored.Activate(value.at("active_project_id"))) throw std::runtime_error("Missing active project.");
        if (value.contains("next_project_id") && !value.at("next_project_id").is_number_unsigned()) throw std::runtime_error("Invalid next_project_id.");
        restored.nextHandle = std::max(restored.nextHandle,value.value("next_project_id",restored.nextHandle));
        if (restored.nextHandle == UINT64_MAX) throw std::runtime_error("Project handles exhausted.");
        std::vector<Path> files;
        const auto paths=value.at("recent_projects").get<std::vector<std::string>>();
        for (auto it=paths.rbegin();it!=paths.rend();++it) TouchRecent(files,Path{*it});
        const bool requested=value.value("close_requested",false), application=value.value("close_application",false), ready=value.value("close_ready",false);
        workspace = std::move(restored); if (undo) undo->Clear(); recent=std::move(files);
        closeRequested=requested; closeApplication=application; closeReady=ready;
        ReconcileSelection();
    }
    Result<void> CapLabWorkflow::WorkspaceRequest(const Json &request)
    {
        try {
            const std::string operation = request.at("operation");
            if (operation == "close_decision") {
                if (!closeRequested) throw std::runtime_error("No pending close request.");
                const std::string decision=request.at("decision");
                if (decision!="save" && decision!="discard" && decision!="cancel") throw std::runtime_error("Unknown close decision.");
                const auto target=Path{request.value("path",std::string{})};
                if (decision=="save" && target.Empty() && !Document().path) throw std::runtime_error("Save requires an explicit path.");
                ResolveClose(decision,target);
            } else {
                if (activeJob || analysisQueued) throw std::runtime_error("Wait for the current job or cancel it.");
                if (closeRequested && operation != "workspace") throw std::runtime_error("Resolve the pending close request first.");
                if (operation == "new") Start(Operation::New);
                else if (operation == "activate") {
                    if (!request.at("project_id").is_number_unsigned() || !Activate(request.at("project_id"))) throw std::runtime_error("Unknown project_id.");
                } else if (operation == "close") (void)RequestClose(request.value("application",false));
                else if (operation != "workspace") throw std::runtime_error("Unknown workspace operation.");
            }
            return {};
        } catch (const std::exception &e) {
            return StructuredError{ErrorCategory::Validation,Severity::Error,"Workspace operation failed",e.what(),"Check the project and active jobs.","CapLab","caplab.workspace"};
        }
    }
}

namespace SST::Prototypes::CapLab {
Json CapLabWorkflow::SelectionJson() const {
    const auto &d=Document();
    return {{"sample_id",d.selectedSample},{"measurement_id",d.selectedMeasurement},{"defect_id",d.selectedDefect},
        {"analysis_id",d.selectedAnalysis},{"circuit_id",d.selectedCircuit}};
}
void CapLabWorkflow::ReconcileSelection() {
    auto &d=Document(); auto &p=d.project; const auto before=SelectionJson();
    if (d.selectedSample && std::none_of(p.samples.begin(),p.samples.end(),[&](const auto &s) {return s.id==d.selectedSample;})) {
        d.selectedSample=d.selectedMeasurement=d.selectedDefect=d.selectedAnalysis=d.selectedCircuit=0;
        p.graph=0;
    }
    if (d.selectedMeasurement) {
        try {d.selectedSample=AnalysisMeasurement(p,d.selectedMeasurement).at("sample_id");}
        catch (const std::exception &) {d.selectedMeasurement=0; if (d.selectionKind=="measurement") p.graph=0;}
    }
    auto exists=[&](const Json &items,pl::Id id) {return std::any_of(items.begin(),items.end(),[&](const auto &v) {return v.at("id")==id;});};
    if (!exists(p.defects,d.selectedDefect)) d.selectedDefect=0;
    if (!exists(p.circuits,d.selectedCircuit)) d.selectedCircuit=0;
    if (!exists(p.analyses,d.selectedAnalysis)) d.selectedAnalysis=0;
    if (d.selectionKind=="analysis") for (const auto &a:p.analyses) if (a.at("id")==d.selectedAnalysis) d.selectedSample=AnalysisSample(a);
    if (d.selectionKind=="circuit") for (const auto &c:p.circuits) if (c.at("id")==d.selectedCircuit) d.selectedSample=c.at("sample_id");
    if (d.selectionKind=="defect") for (const auto &v:p.defects) if (v.at("id")==d.selectedDefect) d.selectedSample=v.at("sample_id");
    if (d.selectedMeasurement && AnalysisMeasurement(p,d.selectedMeasurement).at("sample_id")!=d.selectedSample) d.selectedMeasurement=0;
    for (const auto &c:p.circuits) if (c.at("id")==d.selectedCircuit && c.at("sample_id")!=d.selectedSample) d.selectedCircuit=0;
    for (const auto &v:p.defects) if (v.at("id")==d.selectedDefect && v.at("sample_id")!=d.selectedSample) d.selectedDefect=0;
    if (importTargetSample && std::none_of(p.samples.begin(),p.samples.end(),[&](const auto &s) {return s.id==importTargetSample;})) importTargetSample=0;
    if (SelectionJson()!=before) ++d.selectionRevision;
}
void CapLabWorkflow::Select(const Json &request) {
        auto &d=Document(); auto &p=d.project;
    if (!request.at("id").is_number_integer() || (request.at("id").is_number_integer() && request.at("id").get<std::int64_t>()<0)) throw std::runtime_error("Selection ID must be a nonnegative integer.");
    const std::string kind=request.at("kind"); const auto id=request.at("id").get<pl::Id>();
    if (kind=="graph") {ShowGraph(id); analysisDraft.reset(); ++d.selectionRevision; return;}
    pl::Id sample=0, graph=0, measurement=0, defect=0, analysis=0, circuit=0;
    if (kind=="clear") {if (id) throw std::runtime_error("Clear selection requires id=0.");}
    else if (kind=="sample") {
        for (const auto &s:p.samples) if (s.id==id) sample=id;
    } else if (kind=="measurement") {
        sample=AnalysisMeasurement(p,id).at("sample_id"); measurement=id;
        for (auto it=p.analyses.rbegin();it!=p.analyses.rend();++it)
            for (const auto &input:it->at("inputs")) if (input.at("id")==id && !graph && !it->at("graphs").empty()) graph=it->at("graphs").at(0);
        if (!graph) for (const auto &[oid,o]:p.engine->objects()) if (const auto *plot=std::get_if<pl::Plot>(&o->data); plot && plot->worksheet==id) graph=p.engine->object(o->parent).parent;
        if (!graph) {
            std::vector<pl::Id> columns;
            for (const auto &m:p.measurements) if (m.at("id")==id) columns=m.at("columns").get<std::vector<pl::Id>>();
            for (const auto &m:p.sweeps) if (m.id==id) columns={m.frequency,m.capacitance};
            if (columns.size()>=2) {
                p.engine=EditSnapshot().engine;
                graph=p.engine->reserveId(); const auto layer=p.engine->reserveId();
                pl::Layer axes; const auto m=AnalysisMeasurement(p,id);
                axes.spec.x.label=m.at("names").at(0).get<std::string>()+" ["+m.at("units").at(0).get<std::string>()+"]";
                axes.spec.y.label=m.at("names").at(1).get<std::string>()+" ["+m.at("units").at(1).get<std::string>()+"]";
                if (m.at("names").at(0)=="f") axes.spec.x.scale=sst::ui::plot::AxisScale::Log10;
                auto op=p.engine->change("Measurement plot",{{graph,p.engine->root(),"Measurement "+std::to_string(id),0,pl::Graph{}},
                    {layer,graph,"Measured",0,axes},{p.engine->reserveId(),layer,"Measured",0,pl::Plot{id,columns[0],columns[1]}}});
                op.apply(*p.engine); d.Changed(); d.identityHighWater=NextEntityId(*p.engine);
            }
        }
    } else {
        const auto &items=kind=="defect" ? p.defects : kind=="analysis" ? p.analyses : kind=="circuit" ? p.circuits : throw std::runtime_error("Unknown selection kind.");
        for (const auto &item:items) if (item.at("id")==id) {
            if (kind=="analysis") {
                analysis=id;
                if (item.contains("circuit")) circuit=item.at("circuit").at("id");
                if (item.contains("defect")) defect=item.at("defect").at("id");
                if (item.contains("sample_id")) sample=item.at("sample_id");
                else if (item.contains("defect")) sample=item.at("defect").at("sample_id");
                else sample=item.at("inputs").at(0).at("sample_id");
                if (!item.at("graphs").empty()) graph=item.at("graphs").at(0);
                if (!item.at("inputs").empty()) measurement=item.at("inputs").at(0).at("id");
            } else {sample=item.at("sample_id"); if (kind=="defect") defect=id; else circuit=id;}
        }
    }
    if (kind!="clear" && !sample) throw std::runtime_error("Missing selection ID.");
    if (kind=="sample" || kind=="circuit" || kind=="defect") {
        for (auto it=p.analyses.rbegin();it!=p.analyses.rend();++it) {
            const bool matches=kind=="sample" ? AnalysisSample(*it)==sample : kind=="circuit" ?
                it->contains("circuit") && it->at("circuit").at("id")==id :
                it->contains("defect") && it->at("defect").at("id")==id;
            if (matches && !it->at("graphs").empty()) {graph=it->at("graphs").at(0); break;}
        }
    }
    const bool sameSample=d.selectedSample==sample;
    d.selectionKind=kind;
    d.selectedSample=sample; d.selectedMeasurement=measurement ? measurement : sameSample && kind!="sample" ? d.selectedMeasurement : 0;
    d.selectedDefect=defect ? defect : sameSample && kind!="sample" ? d.selectedDefect : 0;
    d.selectedCircuit=circuit ? circuit : sameSample && kind!="sample" ? d.selectedCircuit : 0;
    d.selectedAnalysis=analysis;
    ShowGraph(graph);
    analysisDraft.reset();
    ++d.selectionRevision; ReconcileSelection();
}
sst::ui::plot::PlotViewState CapLabWorkflow::GraphView(pl::Id graph) const {
    if (const auto it=graphViews.find(graph);it!=graphViews.end()) return it->second;
    const auto &p=*Document().project.engine;
    if (!std::holds_alternative<pl::Graph>(p.object(graph).data)) throw std::runtime_error("Expected graph_id.");
    return std::get<pl::Layer>(p.object(p.children(graph).at(0)).data).view;
}
void CapLabWorkflow::StoreGraphView(pl::Id graph, const sst::ui::plot::PlotViewState &value) {
    if (!graph || !value.viewport) return;
    auto &engine=Document().project.engine;
    auto &p=*engine;
    auto layer=p.object(p.children(graph).at(0));
    auto &saved=std::get<pl::Layer>(layer.data).view;
    if (SameStoredView(saved,value)) return;
    const bool hadView=saved.viewport.has_value();
    saved=value;
    if (engine.use_count()>1) engine=CloneEngine(*engine);
    engine->change("Graph view",{std::move(layer)}).apply(*engine);
    if (hadView) Document().Changed();
}
void CapLabWorkflow::ShowGraph(pl::Id graph) {
    auto &p=Document().project;
    if (graph==p.graph) return;
    if (graph) (void)GraphView(graph);
    StoreGraphView(p.graph,view);
    if (p.graph && view.viewport) graphViews[p.graph]=view;
    p.graph=graph; view=graph ? GraphView(graph) : sst::ui::plot::PlotViewState{};
}
}
