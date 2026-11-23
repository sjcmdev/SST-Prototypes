#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <vector>
#include "Core/JobSystem/JobContext.hpp"

namespace SST {
inline thread_local std::size_t ParallelWorkerBudget = 0;

// The parent claims chunks too; queued helpers are harmless after the synchronous drain.
inline bool ParallelFor(JobContext& ctx, std::size_t n, std::function<void(std::size_t)> fn,
    std::size_t maxWorkers = 0, std::size_t grain = 1, std::stop_token stop = {}) {
    if (!n) return true;
    grain = std::max<std::size_t>(1, grain);
    auto budget = std::min(maxWorkers ? maxWorkers : ctx.GetWorkerCount(), ctx.GetWorkerCount());
    if (ParallelWorkerBudget) budget = std::min(budget, ParallelWorkerBudget);
    const auto workers = std::max<std::size_t>(1, std::min(budget, 1 + (n - 1) / grain));
    struct State {
        JobContext ctx;
        std::function<void(std::size_t)> fn;
        std::size_t n, grain, childBudget;
        std::stop_token stop;
        std::atomic<std::size_t> next{0}, done{0};
        std::atomic<bool> failed{false};
        std::mutex mutex;
        std::condition_variable drained;
        std::size_t running = 0;
        bool closed = false;
        std::exception_ptr failure;
        std::chrono::steady_clock::time_point reportAt{};
        void Work() {
            { std::lock_guard lock(mutex); if (closed) return; ++running; }
            struct BudgetScope {
                std::size_t previous = ParallelWorkerBudget;
                explicit BudgetScope(std::size_t value) { ParallelWorkerBudget = value; }
                ~BudgetScope() { ParallelWorkerBudget = previous; }
            } scope(childBudget);
            try {
                while (!failed && !stop.stop_requested() && !ctx.IsCancellationRequested()) {
                    const auto begin = next.fetch_add(grain);
                    if (begin >= n) break;
                    for (auto i = begin; i < std::min(n, begin + grain); ++i) {
                        if (failed || stop.stop_requested() || ctx.IsCancellationRequested()) break;
                        fn(i); ++done;
                    }
                    std::lock_guard lock(mutex);
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= reportAt) { ctx.SetProgress(float(done.load()), float(n)); reportAt = now + std::chrono::milliseconds(50); }
                }
            } catch (...) {
                std::lock_guard lock(mutex);
                if (!failure) failure = std::current_exception();
                failed = true;
            }
            { std::lock_guard lock(mutex); --running; }
            drained.notify_all();
        }
    };
    class Helper final : public IJob {
    public:
        explicit Helper(std::shared_ptr<State> state) : state(std::move(state)) {}
        std::string GetName() const override { return "ParallelFor helper"; }
        std::string GetType() const override { return "core.parallel_for"; }
        void Execute(JobContext&) override { state->Work(); }
    private:
        std::shared_ptr<State> state;
    };
    auto state = std::make_shared<State>(ctx, std::move(fn), n, grain, std::max<std::size_t>(1, budget / workers), stop);
    std::vector<JobId> helpers;
    try {
        for (std::size_t i = 1; i < workers; ++i) helpers.push_back(ctx.SubmitJob(CreateRef<Helper>(state)));
        state->Work();
    } catch (...) { std::lock_guard lock(state->mutex); state->failure = std::current_exception(); state->failed = true; }
    { std::lock_guard lock(state->mutex); state->closed = true; }
    for (const auto id : helpers) if (id) (void)ctx.WaitForJob(id);
    std::unique_lock lock(state->mutex);
    state->drained.wait(lock, [&] { return state->running == 0; });
    if (state->failure) std::rethrow_exception(state->failure);
    ctx.SetProgress(float(state->done.load()), float(n));
    return state->done == n;
}

// Callable adapter for typed engine ExecutionContext; Core has no engine dependency.
struct JobExecutor {
    JobContext context;
    void operator()(std::size_t count, std::size_t grain, std::stop_token stop,
        const std::function<void(std::size_t)>& body) {
        if (!ParallelFor(context, count, body, 0, grain, stop) && !stop.stop_requested()) context.ThrowIfCancellationRequested();
    }
};
} // namespace SST
