#pragma once

#include "Core/Layer.hpp"
#include <functional>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Storage/PrototypeDocument.hpp"

namespace SST::Prototypes::WindowChromeLab
{
    enum class Operation { Evaluate, Open, Save };
    struct DocumentRequested : BusEvent { Operation operation = Operation::Evaluate; };

    class DocumentJob final : public IJob
    {
    public:
        DocumentJob(Operation operation, PrototypeInput input, Path path = {});
        std::string GetName() const override { return "Prototype document"; }
        std::string GetType() const override { return "prototype.document"; }
        void Execute(JobContext &context) override;
        Operation operation;
        PrototypeInput input;
        PrototypeResult result;
        Path path;
        bool saveCommitted = false;
        std::string windowLayout;
    };

    class PrototypeWorkflow final : public Layer
    {
    public:
        explicit PrototypeWorkflow(Ref<JobSystem> jobs) : Layer("PrototypeWorkflow"), jobs(std::move(jobs)) {}
        void Start(Operation operation);
        void OnUpdate(float) override;
        PrototypeDocument document;
        Ref<JobSystem> jobs;
        JobId activeJob = 0;
        std::string status;
        bool scientificNotation = true;
        std::function<std::string()> captureWindowLayout;
        std::function<bool(const std::string &)> restoreWindowLayout;
    private:
        Ref<DocumentJob> pending;
        std::uint64_t pendingRevision = 0;
    };
}
