#include "Core/dspch.hpp"
#include "Core/App/Managers/SettingsReset.hpp"
#include "Core/App/Managers/ConfigManager.hpp"
#include "Core/Diagnostics/OperationOutcome.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <thread>

namespace SST {
namespace {
struct ResetJob final : IJob {
    ApplicationConfig candidate;
    SettingsSection section;
    std::optional<Result<ApplicationConfig>> response;
    std::string GetName() const override {return "Reset settings section";}
    std::string GetType() const override {return "app.settings.reset";}
    void Execute(JobContext& context) override {
        context.ThrowIfCancellationRequested();
        const auto defaults=ConfigManager::CreateDefaultConfig(candidate.directory);
        switch(section) {
        case SettingsSection::System: candidate.jobs=defaults.jobs;candidate.eventQueue=defaults.eventQueue;candidate.log=defaults.log;break;
        case SettingsSection::Interface: candidate.ui=defaults.ui;candidate.appearance=defaults.appearance;break;
        case SettingsSection::Viewport: candidate.appearance.clearColor=defaults.appearance.clearColor;break;
        default: throw std::invalid_argument("Unknown settings section. Choose system, interface or viewport.");
        }
        std::string error;
        if(!ConfigManager::Validate(candidate,error)) response=MakeOperationError(ErrorCategory::Validation,error,"Settings");
        else response=std::move(candidate);
    }
};
class ResetCommand final : public ICommand {
public:
    ResetCommand(JobSystem& jobs,Ref<ResetJob> job):jobs(jobs),job(std::move(job)){}
    std::string Description() const override{return "Prepare settings section reset";}
    Result<void> Execute(CommandContext&) override {
        const auto id=jobs.Submit(job);
        if(!id)return MakeOperationError(ErrorCategory::Internal,"Cannot submit settings reset.","Settings");
        for(;;) {
            const auto snapshot=jobs.GetJob(id);
            if(snapshot && (snapshot->status==JobStatus::Failed || snapshot->status==JobStatus::Cancelled))return snapshot->error.value_or(MakeOperationError(ErrorCategory::Internal,snapshot->errorMessage,"Settings"));
            if(snapshot && snapshot->status==JobStatus::Completed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(!job->response)return MakeOperationError(ErrorCategory::Internal,"Settings reset produced no response.","Settings");
        return *job->response ? VoidResult{} : VoidResult{job->response->Error()};
    }
private:
    JobSystem& jobs;Ref<ResetJob> job;
};
}
Result<ApplicationConfig> ResetSettingsSection(CommandRegistry& commands,JobSystem& jobs,ApplicationConfig candidate,SettingsSection section) {
    const CommandID id{"app.settings.reset"};
    if(!commands.HasCommand(id)) {
        const auto registered=commands.Register({id,"Reset settings section","Settings",{}, {},CommandFlags::HiddenFromPalette|CommandFlags::AsyncCapable},[](CommandContext& context)->Unique<ICommand> {
            const auto jobs=context.TryGet<JobSystem*>("jobs");const auto job=context.TryGet<Ref<ResetJob>>("job");
            if(!jobs || !*jobs || !job || !*job)throw std::invalid_argument("Missing typed settings reset request.");
            return CreateUnique<ResetCommand>(**jobs,*job);
        });
        if(!registered)return registered.Error();
    }
    auto job=CreateRef<ResetJob>();job->candidate=std::move(candidate);job->section=section;
    // Submit through the same command for desktop and headless callers.
    CommandContext context;context.Set("jobs",&jobs);context.Set("job",job);
    const auto executed=commands.Execute(id,std::move(context));
    if(!executed)return executed.Error();
    return std::move(*job->response);
}
}
