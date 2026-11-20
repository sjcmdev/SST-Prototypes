#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/IO/RecentFiles.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include <sst/plotlab/SnapshotJson.hpp>
namespace SST::Prototypes::CapLab
{
    void DocumentJob::Execute(JobContext &context)
    {
        context.SetProgress(0, 1);
        context.SetStage(operation == Operation::Preview ? "Reading import data" : operation == Operation::Open || operation == Operation::OpenOrImport ? "Opening project / import file" : operation == Operation::Save ? "Saving project" : "Exporting C-f snapshot");
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
            } else measurement=AnalysisMeasurement(p,analysisOptions.at("measurement_id").get<pl::Id>());
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
                } else result=SimulateIS(measurement,*it,analysisOptions,context);
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
            if (format!=".png") throw std::runtime_error("Choose CSV, XLSX or PNG export.");
            if (!snapshot) throw std::runtime_error("No current graph snapshot.");
            const auto text = pl::SnapshotJson(*snapshot);
            preview = Json::parse(text);
            const PlotExportRequest request{text,path.Native(),"png",300};
            ExportPlotsJson(context,{&request,1});
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
            if (closeRequested && operation != Operation::Save && operation != Operation::SaveAs && operation != Operation::Close)
                throw std::runtime_error("Resolve the pending close request first.");
            if (operation == Operation::New) {
                const auto handle = workspace.Open(CapLabDocument{}, false);
                (void)Activate(handle); status = "New project."; return;
            }
            if (operation == Operation::Close) { (void)RequestClose(); return; }
            auto &document = Document();
            const bool saveAs = operation == Operation::SaveAs;
            if (saveAs) operation = Operation::Save;
            if (operation != Operation::Analyze && target.Empty())
            {
                if (operation == Operation::Save && !saveAs) target = document.path.value_or(Path{});
                if (target.Empty())
                {
                    const NativeFileDialogFilter filter{operation == Operation::Preview ? "CapLab measurements / ProPho" : operation == Operation::Export ? "CapLab export (choose extension)" : "CapLab project", operation == Operation::Preview ? "dat,txt,csv,cf,cv,json" : operation == Operation::Export ? "csv,xlsx,png" : "json"};
                    const auto selected = operation == Operation::Save || operation == Operation::Export
                        ? SaveFileDialog(filter,operation == Operation::Save ? "project.caplab.json" : "cf.png") : OpenFileDialog(filter);
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
            auto job = CreateRef<DocumentJob>(operation,operation == Operation::Save || operation == Operation::Analyze || operation == Operation::Export ? Serialize(document.project) : "",target);
            job->analysisOptions = analysisOptions; job->analysisOptions["input_revision"] = document.revision;
            job->decimal = decimal; job->importOptions = importOptions; job->importer = labImporter;
            if (operation == Operation::Export && target.Native().extension()==".png") job->snapshot = pl::snapshotGraph(*document.project.engine,document.project.graph,view);
            pendingRevision = document.revision; pendingProject = workspace.active;
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
        auto &document = Document();
        RefreshAnalyses(document.project);
        document.Changed(); view = {};
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
                CapLabDocument opened;
                opened.Opened(std::move(*pending->opened),pending->path,0);
                const auto handle = workspace.Open(std::move(opened), false);
                // Release the completed job before switching the active document.
                activeJob = 0;
                (void)Activate(handle);
                TouchRecent(recent,pending->path);
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
        preview.reset(); analysisDraft.reset(); pendingCandidate = nullptr;
        analysisOptions = Json::object(); view = {}; failure.reset(); status = "Ready.";
        if (bus) { DocumentChanged event; event.project = workspace.active; event.revision = Document().revision; bus->Queue(event); }
    }
    bool CapLabWorkflow::Activate(std::uint64_t handle)
    {
        if (activeJob || !workspace.Find(handle)) return false;
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
        if (activeJob || (Document().IsDirty() && !discard)) return false;
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
            {"project",Json::parse(Serialize(d.project))},{"path",d.path ? Json(d.path->Utf8()) : Json(nullptr)},
            {"revision",d.revision},{"saved_revision",d.savedRevision},{"dirty",d.IsDirty()}});
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
            d.project = Parse(item.at("project").dump());
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
                if (activeJob) throw std::runtime_error("Wait for the current job or cancel it.");
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
