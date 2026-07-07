#pragma once

#include "Core/Utils/Path.hpp"

struct GLFWwindow;

namespace SST::Platform
{
	void InitializeWindowPlatform(GLFWwindow *window, const Path &iconPath);
	void ShutdownWindowPlatform(GLFWwindow *window);
} // namespace SST::Platform
