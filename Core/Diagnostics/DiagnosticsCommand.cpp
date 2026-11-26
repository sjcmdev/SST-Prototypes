#include "Core/dspch.hpp"
#include "Core/Diagnostics/DiagnosticsCommand.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include <future>

namespace SST {
namespace {
StructuredError exportError(std::string code,std::string details) {
    return {ErrorCategory::IO, Severity::Error, "Diagnostic report was not saved.",std::move(details),
        "Choose a writable destination with free disk space and retry.","Diagnostics",std::move(code)};
}
class ExportJob final:public IJob {
public:
    DiagnosticExport request;
    std::promise<void> completion;
    std::string GetName() const override {return "Export diagnostics";}
    std::string GetType() const override {return "app.diagnostics.export";}
    void Execute(JobContext&) override {
        try {
            std::string error;
            if(!ExportDiagnostics(request.destination,request.preview,error)) throw std::runtime_error(error);
            completion.set_value();
        } catch(...) {completion.set_exception(std::current_exception());throw;}
    }
};
class ExportCommand final:public ICommand {
public:
    explicit ExportCommand(Ref<JobSystem> jobs):m_Jobs(std::move(jobs)){}
    std::string Description() const override {return "Save the reviewed redacted diagnostic report";}
    Result<void> Execute(CommandContext& context) override {
        auto* request=context.TryGet<DiagnosticExport>("request");
        if(!request || request->destination.Empty() || request->destination.Native().native().find(FilePath::value_type{})!=FilePath::string_type::npos)
            return StructuredError{ErrorCategory::Validation,Severity::Error,"Diagnostic report was not saved.","Expected a nonempty destination without NUL characters.","Choose a valid file path and retry.","Diagnostics","diagnostics.invalid_request"};
        try {
            auto job=CreateRef<ExportJob>(); job->request=std::move(*request);
            auto completion=job->completion.get_future();
            if(!m_Jobs->Submit(job)) return exportError("diagnostics.submit_failed","The job system is shutting down.");
            completion.get(); return {};
        } catch(const std::exception& error) {return exportError("diagnostics.write_failed",error.what());}
    }
private:
    Ref<JobSystem> m_Jobs;
};
}
void RegisterDiagnosticsCommand(const Ref<CommandRegistry>& commands,const Ref<JobSystem>& jobs) {
    const auto result=commands->Register({CommandID{"app.diagnostics.export"},"Export diagnostics","Support","Save a reviewed redacted report locally.",{},CommandFlags::HiddenFromPalette},
        [jobs](CommandContext&){return CreateUnique<ExportCommand>(jobs);});
    if(!result) throw std::runtime_error(result.Error().technicalDetails);
}
Result<CommandOutcome> ExecuteDiagnosticsExport(CommandRegistry& commands,DiagnosticExport request) {
    CommandContext context; context.SetSource("Diagnostics GUI / CLI / Python");context.Set("request",std::move(request));
    return commands.Execute(CommandID{"app.diagnostics.export"},std::move(context));
}
Result<CommandOutcome> ExportDiagnosticsHeadless(DiagnosticExport request) {
    auto jobs=CreateRef<JobSystem>(Ref<EventBus>{},1);auto commands=CreateRef<CommandRegistry>();
    RegisterDiagnosticsCommand(commands,jobs);
    auto result=ExecuteDiagnosticsExport(*commands,std::move(request));
    jobs->Shutdown();return result;
}
}
