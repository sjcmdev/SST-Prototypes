#pragma once

#include <thread>

#include "Core/Utils/Assert.hpp"

namespace SST::Threading
{
	void SetMainThread();
	[[nodiscard]] bool IsMainThread();
	[[nodiscard]] std::thread::id GetMainThreadId();
} // namespace SST::Threading

#define ASSERT_MAIN_THREAD() DS_ASSERT(::SST::Threading::IsMainThread(), "Expected main thread")