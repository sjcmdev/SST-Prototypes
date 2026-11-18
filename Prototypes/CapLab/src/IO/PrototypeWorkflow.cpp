#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Domain/CapLabIS.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "PlotLabExport.hpp"
namespace SST::Prototypes::CapLab
{
    void DocumentJob::Execute(JobContext &context)
    {
        context.SetProgress(0, 1);
        context.SetStage(operation == Operation::Preview ? "Reading import data" : operation == Operation::Open ? "Opening project" : operation == Operation::Save ? "Saving project" : "Exporting C-f snapshot");
        context.ThrowIfCancellationRequested();
        std::string error;
        if (operation == Operation::Open || operation == Operation::Preview)
        {
            std::string text;
            if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
            if (operation == Operation::Open) opened.emplace(Parse(text));
            else
            {
                pl::ImportOptions options; options.decimal = decimal; options.extra["path"] = path.Utf8(); options.extra["options"] = importOptions;
                const auto table = importer->preview(text, options, {
                    [&] { return context.IsCancellationRequested(); },
                    [&](double value) { context.SetProgress(static_cast<float>(value), 1); }});
                preview = Json::parse(table.source.at("preview_json"));
            }
        }
        else if (operation == Operation::Analyze)
        {
            auto p = Parse(input);
            Json measurement;
            if (analysisOptions.at("type")=="Arrhenius") {
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
            if (analysisOptions.contains("xd_measurement_id")) xd = AnalysisMeasurement(p,analysisOptions.at("xd_measurement_id").get<pl::Id>());
            Json result;
            if (analysisOptions.at("type")=="Circuit") {
                const auto it=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==analysisOptions.at("circuit_id");});
                if (it==p.circuits.end()) throw std::runtime_error("Missing circuit");
                result=SimulateIS(measurement,*it,analysisOptions,context);
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
            if (!snapshot) throw std::runtime_error("No current graph snapshot.");
            const auto executable = Platform::CurrentExecutablePath();
            if (!executable) throw std::runtime_error(executable.Error().technicalDetails);
            const auto local = executable.Value().parent_path().Join("python").Native();
            const auto pythonRoot = FileSystem::Exists(local / "sst_plotlab") ? local : std::filesystem::path(SST_PLOTLAB_PYTHON_ROOT);
            if (snapshot->spec.equalAspect) {
                // The existing exporter uses equal fractional plot dimensions.
                // Match physical dimensions to the resolved data ranges for 1:1 units.
                auto equal=*snapshot;
                const double ratio=(equal.ranges.maxX-equal.ranges.minX)/(equal.ranges.maxY-equal.ranges.minY);
                if (ratio>equal.widthMm/equal.heightMm) equal.heightMm=equal.widthMm/ratio;
                else equal.widthMm=equal.heightMm*ratio;
                PlotLab::ExportGraph(equal,path.Native(),"png",300,pythonRoot);
            } else PlotLab::ExportGraph(*snapshot,path.Native(),"png",300,pythonRoot);
        }
        if (!saveCommitted) context.ThrowIfCancellationRequested();
        context.SetProgress(1,1);
    }
    void CapLabWorkflow::Start(Operation operation, Path target)
    {
        try
        {
            if (activeJob) throw std::runtime_error("Wait for the current job or cancel it.");
            if (operation == Operation::Open && document.dirty) throw std::runtime_error("Save pending changes before opening another project.");
            if (operation != Operation::Analyze && target.Empty())
            {
                if (operation == Operation::Save) target = document.path;
                if (target.Empty())
                {
                    const NativeFileDialogFilter filter{operation == Operation::Preview ? "CapLab measurements / ProPho" : "CapLab project", operation == Operation::Preview ? "dat,txt,csv,cf,cv,json" : operation == Operation::Export ? "png" : "json"};
                    const auto selected = operation == Operation::Save || operation == Operation::Export
                        ? SaveFileDialog(filter,operation == Operation::Save ? "project.caplab.json" : "cf.png") : OpenFileDialog(filter);
                    if (!selected) { status = "Dialog cancelled."; return; }
                    target = *selected;
                }
            }
            auto job = CreateRef<DocumentJob>(operation,operation == Operation::Save || operation == Operation::Analyze ? Serialize(document.project) : "",target);
            job->analysisOptions = analysisOptions; job->analysisOptions["input_revision"] = document.revision;
            job->decimal = decimal; job->importOptions = importOptions; job->importer = labImporter;
            if (operation == Operation::Export) job->snapshot = pl::snapshotGraph(*document.project.engine,document.project.graph,view);
            pendingRevision = document.revision;
            activeJob = jobs->Submit(job);
            if (!activeJob) throw std::runtime_error("Job submission failed.");
            pending = std::move(job); status.clear();
            if (operation == Operation::Preview) preview.reset();
        }
        catch (const std::exception &error) { status = error.what(); }
    }
    void CapLabWorkflow::Changed()
    {
        RefreshAnalyses(document.project);
        document.Changed(); view = {};
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
            if (pending->operation == Operation::Export) status = "Exported PNG.";
            else if (document.revision != pendingRevision) status = "Stale revision: result discarded.";
            else if (pending->operation == Operation::Analyze)
            {
                analysisDraft = std::move(pending->opened); analysisRevision = pendingRevision;
                if (commands) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
            }
            else if (pending->operation == Operation::Open)
            {
                if (undo) undo->Clear();
                document.Opened(std::move(*pending->opened),pending->path,pendingRevision);
                preview.reset(); view = {};
                if (bus) { DocumentChanged e; e.project = document.project.engine->id(); e.revision = document.revision; bus->Queue(e); }
            }
            else if (pending->operation == Operation::Preview)
            {
                preview = std::move(pending->preview); previewRevision = pendingRevision;
                if (preview->contains("samples")) { status = "Preview ready; inspect samples, units and provenance before confirming."; }
                else {
                temperature = Optional(preview->at("temperature_K"));
                if (!temperature) temperature = Optional(preview->at("suggested_temperature_K"));
                status = "Preview ready; check units and temperature before confirming."; }
            }
        }
        else status = job->status == JobStatus::Cancelled ? "Cancelled." : job->errorMessage;
        activeJob = 0; pending.reset();
    }
}
