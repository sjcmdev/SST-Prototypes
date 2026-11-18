#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
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
        context.SetStage(operation == Operation::Preview ? "Reading lab AS (Cp/Rp)" : operation == Operation::Open ? "Opening project" : operation == Operation::Save ? "Saving project" : "Exporting C-f snapshot");
        context.ThrowIfCancellationRequested();
        std::string error;
        if (operation == Operation::Open || operation == Operation::Preview)
        {
            std::string text;
            if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
            if (operation == Operation::Open) opened.emplace(Parse(text));
            else
            {
                pl::ImportOptions options; options.decimal = decimal; options.extra["path"] = path.Utf8();
                const auto table = importer->preview(text, options, {
                    [&] { return context.IsCancellationRequested(); },
                    [&](double value) { context.SetProgress(static_cast<float>(value), 1); }});
                preview = Json::parse(table.source.at("preview_json"));
            }
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
            PlotLab::ExportGraph(*snapshot,path.Native(),"png",300,pythonRoot);
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
            if (target.Empty())
            {
                if (operation == Operation::Save) target = document.path;
                if (target.Empty())
                {
                    const NativeFileDialogFilter filter{operation == Operation::Preview ? "Lab AS: f [Hz], Cp [F], Rp [Ohm]" : "CapLab project", operation == Operation::Preview ? "dat,txt,csv" : operation == Operation::Export ? "png" : "json"};
                    const auto selected = operation == Operation::Save || operation == Operation::Export
                        ? SaveFileDialog(filter,operation == Operation::Save ? "project.caplab.json" : "cf.png") : OpenFileDialog(filter);
                    if (!selected) { status = "Dialog cancelled."; return; }
                    target = *selected;
                }
            }
            auto job = CreateRef<DocumentJob>(operation,operation == Operation::Save ? Serialize(document.project) : "",target);
            job->decimal = decimal; job->importer = labImporter;
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
                temperature = Optional(preview->at("temperature_K"));
                if (!temperature) temperature = Optional(preview->at("suggested_temperature_K"));
                status = "Preview ready; check units and temperature before confirming.";
            }
        }
        else status = job->status == JobStatus::Cancelled ? "Cancelled." : job->errorMessage;
        activeJob = 0; pending.reset();
    }
}
