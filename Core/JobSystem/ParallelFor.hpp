#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"

namespace SST
{
	// Generic opt-in parallel-for over the JobSystem. The calling job runs indices itself and submits up to
	// maxWorkers-1 child helper jobs (visible in the ProgressTracker under the caller), so it never deadlocks, even with
	// one worker (helpers are then simply never needed). fn(i) must write only its own output slot.
	// Cancellation of the caller stops claiming new indices; returns false when some were skipped (cancel/exception).
	// The first exception from fn is rethrown after all running indices finished.
	inline bool ParallelFor(JobContext &ctx, std::size_t n, std::function<void(std::size_t)> fn, std::size_t maxWorkers = 0)
	{
		struct State
		{
			std::function<void(std::size_t)> fn;
			JobContext ctx;
			std::size_t n;
			std::atomic<std::size_t> next{0}, running{0}, done{0};
			std::atomic<bool> stop{false};
			std::exception_ptr failure;
			std::mutex lock;
			State(std::function<void(std::size_t)> f, const JobContext &c, std::size_t count) : fn(std::move(f)), ctx(c), n(count) {}
			void Work()
			{
				while (!stop)
				{
					if (ctx.IsCancellationRequested()) { stop = true; break; }
					++running; // claim only while running>0 so the caller's drain below cannot miss us
					const std::size_t i = next.fetch_add(1);
					if (i >= n || stop) { --running; break; }
					try { fn(i); ctx.SetProgress(float(++done), float(n)); }
					catch (...) { std::lock_guard g(lock); if (!failure) failure = std::current_exception(); stop = true; }
					--running;
				}
			}
		};
		class Helper final : public IJob
		{
		public:
			explicit Helper(std::shared_ptr<State> s) : m_S(std::move(s)) {}
			std::string GetName() const override { return "ParallelFor helper"; }
			std::string GetType() const override { return "core.parallel_for"; }
			void Execute(JobContext &) override { if (!m_S->stop) m_S->Work(); }
		private:
			std::shared_ptr<State> m_S;
		};

		if (n == 0) return true;
		auto s = std::make_shared<State>(std::move(fn), ctx, n);
		std::size_t workers = maxWorkers ? maxWorkers : std::max(1u, std::thread::hardware_concurrency());
		workers = std::min(workers, n);
		for (std::size_t t = 1; t < workers; ++t) (void)ctx.SubmitJob(CreateRef<Helper>(s));
		s->Work();
		s->stop = true; // helpers starting late find nothing to do and never touch fn again
		while (s->running > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (s->failure) std::rethrow_exception(s->failure);
		return s->done == n;
	}
} // namespace SST
