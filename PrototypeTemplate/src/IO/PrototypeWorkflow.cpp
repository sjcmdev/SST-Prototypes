#include "Core/dspch.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Platform/NativeFileDialog.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
    DocumentJob::DocumentJob(Operation operation, PrototypeInput input, Path path)
        : operation(operation), input(input), path(std::move(path)) {}

    void DocumentJob::Execute(JobContext &context)
    {
        context.SetProgress(0, 2);
        context.SetStage(operation == Operation::Open ? "Reading" : operation == Operation::Save ? "Saving" : "Evaluating");
        context.ThrowIfCancellationRequested();
        std::string error;
        if (operation == Operation::Open)
        {
            std::string text;
            if (!TextFileIO::Load(path, text, error))
                throw std::runtime_error(error);
            input = ParseInput(text);
            windowLayout = nlohmann::json::parse(text).value("window_layout", std::string{});
        }
        ValidateInput(input);
        result = PrototypeBackend::Evaluate(input);
        context.SetProgress(1, 2);
        context.ThrowIfCancellationRequested();
        if (operation == Operation::Save)
        {
            auto json = nlohmann::json::parse(SerializeInput(input));
            if (!windowLayout.empty()) json["window_layout"] = windowLayout;
            if (!TextFileIO::Save(path, json.dump(2), error)) throw std::runtime_error(error);
        }
        saveCommitted = operation == Operation::Save;
        // Once replacement succeeds the save is committed, even if cancellation arrives now.
        context.SetProgress(2, 2);
    }

    void PrototypeWorkflow::Start(Operation operation)
    {
        try
        {
            if (activeJob != 0)
                throw std::runtime_error("Wait for the current document job or cancel it.");
            if (operation == Operation::Open && document.dirty)
                throw std::runtime_error("Save your changes before opening another document.");
            Path target = document.path;
            const NativeFileDialogFilter filter{"Prototype document", "json"};
            if (operation == Operation::Open || (operation == Operation::Save && target.Empty()))
            {
                const auto selected = operation == Operation::Open ? OpenFileDialog(filter) : SaveFileDialog(filter, "document.json");
                if (!selected) { status = "Dialog cancelled."; return; }
                target = *selected;
            }
            pending = CreateRef<DocumentJob>(operation, document.input, target);
            if (operation == Operation::Save && captureWindowLayout) pending->windowLayout = captureWindowLayout();
            pendingRevision = document.revision;
            activeJob = jobs->Submit(pending);
            if (activeJob == 0)
                throw std::runtime_error("Job submission failed.");
            status.clear();
        }
        catch (const std::exception &error) { status = error.what(); }
    }

    void PrototypeWorkflow::OnUpdate(float)
    {
        if (!activeJob) return;
        const auto snapshot = jobs->GetJob(activeJob);
        if (!snapshot)
        {
            status = "Job was removed before completion could be applied.";
            activeJob = 0;
            pending.reset();
            return;
        }
        if (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running) return;
        if (pending->saveCommitted)
        {
            document.Saved(pending->path, pendingRevision);
            status = "Saved.";
        }
        else if (snapshot->status == JobStatus::Completed)
        {
            status = "Completed.";
            if (pending->operation == Operation::Open)
            {
                if (!document.Opened(pending->input, pending->path, pendingRevision))
                    status = "Document changed during open; loaded data was not applied.";
                else if (!pending->windowLayout.empty() && restoreWindowLayout && !restoreWindowLayout(pending->windowLayout))
                    status = "Document loaded; window layout could not be restored.";
            }
            else if (document.revision == pendingRevision)
                document.result = pending->result;
            else
                status = "Document changed; evaluate again for current results.";
        }
        else status = snapshot->status == JobStatus::Cancelled ? "Cancelled." : snapshot->errorMessage;
        activeJob = 0;
        pending.reset();
    }
}
