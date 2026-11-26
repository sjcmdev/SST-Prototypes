#include "Core/dspch.hpp"
#include "Core/Presentation/ProjectRecoveryLayer.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobOutcome.hpp"
#include "Core/Logging/Logger.hpp"
#include <thread>
#include <imgui.h>

namespace SST {
namespace {
struct RecoveryJob final : IJob {
    ProjectRecovery& recovery;
    std::vector<RecoveryWrite> writes;
    RecoveryJob(ProjectRecovery& recovery,std::vector<RecoveryWrite> writes):recovery(recovery),writes(std::move(writes)){}
    std::string GetName() const override{return "Autosave project recovery copies";}
    std::string GetType() const override{return "app.recovery.checkpoint";}
    void Execute(JobContext& context) override {
        try {
            recovery.Begin();
            for(const auto& item:writes) {
                context.ThrowIfCancellationRequested();
                recovery.Save(item.key,item.revision,item.original,item.extension.c_str(),item.write);
            }
            recovery.End();
        } catch(const JobCancelledException&) {throw;}
          catch(const std::exception& e) {throw OperationException(MakeOperationError(ErrorCategory::IO,e.what(),"Recovery"));}
    }
};
}
ProjectRecoveryLayer::ProjectRecoveryLayer(Path root,Ref<JobSystem> jobs,Checkpoint checkpoint,Restore restore)
    : Layer("Project recovery"), recovery(root), checkpoint(std::move(checkpoint)),restore(std::move(restore)),candidates(ProjectRecovery::Candidates(root)),jobs(std::move(jobs)) {}
bool ProjectRecoveryLayer::finish(bool wait) {
    while(active) {
        const auto snapshot=jobs->GetJob(active);
        if(!snapshot || snapshot->status==JobStatus::Completed || snapshot->status==JobStatus::Failed || snapshot->status==JobStatus::Cancelled) {
            const bool ok=snapshot && snapshot->status==JobStatus::Completed;
            if(!ok) error="Autosave failed: "+(snapshot ? JobError(*snapshot).technicalDetails : "The checkpoint job disappeared.")+". Save the project manually and check the settings directory and free disk space.";
            else error.clear();
            active=0;pending.reset();return ok;
        }
        if(!wait)return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
bool ProjectRecoveryLayer::save(bool wait) {
    if(active && !finish(wait))return false;
    try {
        ProjectRecoveryPlan plan(recovery);checkpoint(plan);
        pending=CreateRef<RecoveryJob>(recovery,std::move(plan.writes));active=jobs->Submit(pending);
        if(!active) {
            if(!wait)throw std::runtime_error("The job system is shutting down.");
            JobContext context({}, {}, {}, {}, []{return false;});pending->Execute(context);pending.reset();return true;
        }
        return !wait || finish(true);
    } catch(const std::exception& e) { error="Autosave failed: "+std::string(e.what())+". Check the settings directory and free disk space, then save your project manually."; DS_LOG_ERROR("{}",error);return false; }
}
void ProjectRecoveryLayer::OnUpdate(float seconds) { if(active)(void)finish(false);elapsed+=seconds;if(elapsed>=30 && !active){elapsed=0;(void)save();} }
void ProjectRecoveryLayer::OnDetach() { (void)save(true); }
void ProjectRecoveryLayer::OnImGuiRender() {
    if (show && !candidates.empty() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::OpenPopup("Recover project");
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    if (ImGui::BeginPopupModal("Recover project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("An earlier session left unsaved project changes.");
        ImGui::TextUnformatted("Restore opens a copy. The original project is preserved; use Save As after checking it.");
        for (std::size_t i=0;i<candidates.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(candidates[i].original.Empty()?"Untitled project":candidates[i].original.filename().Utf8().c_str());
            if (ImGui::Button("Restore copy")) {
                try {
                    if(!finish(true))throw std::runtime_error(error);
                    restore(candidates[i]);
                    if(!save(true))throw std::runtime_error(error);
            ProjectRecovery::Discard(candidates[i]);candidates.erase(candidates.begin()+i);
                }
                catch (const std::exception& e) { error="Cannot restore this snapshot: "+std::string(e.what())+". The snapshot was kept; inspect it or choose another backup."; }
                ImGui::PopID(); break;
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard snapshot")) ImGui::OpenPopup("Confirm discard");
            if (ImGui::BeginPopupModal("Confirm discard",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Permanently discard this document's recovery snapshots?");
                if (ImGui::Button("Discard")) {
                    try { ProjectRecovery::Discard(candidates[i]); candidates.erase(candidates.begin()+i); }
                    catch (const std::exception& e) { error=e.what(); }
                    ImGui::CloseCurrentPopup(); ImGui::EndPopup(); ImGui::PopID(); break;
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!error.empty()) ImGui::TextWrapped("%s",error.c_str());
        if (candidates.empty() || ImGui::Button("Later") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { show=false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    } else if (!error.empty()) {
        if (ImGui::Begin("Autosave error")) { ImGui::TextWrapped("%s",error.c_str()); if(ImGui::Button("Dismiss")) error.clear(); } ImGui::End();
    }
}
}
