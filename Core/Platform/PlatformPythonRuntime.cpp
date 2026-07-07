#include "Core/dspch.hpp"

#include <cstdlib>
#include <fstream>

#include "Core/Platform/PlatformPythonRuntime.hpp"

namespace SST::Platform
{
	static std::string trim(std::string value)
	{
		const std::size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
			return {};
		const std::size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	static Path readVenvHome(const Path &venvRoot)
	{
		const Path configPath = venvRoot / "pyvenv.cfg";
		if (!FileSystem::Exists(configPath.Native()))
			return {};

		std::ifstream file(configPath.Native());
		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t separator = line.find('=');
			if (separator == std::string::npos)
				continue;

			if (trim(line.substr(0, separator)) == "home")
				return Path(trim(line.substr(separator + 1)));
		}

		return {};
	}

	static Path findNearbyVenvHome(Path root)
	{
		for (int depth = 0; depth < 8 && !root.Empty(); ++depth)
		{
			const Path dotVenvHome = readVenvHome(root / ".venv");
			if (!dotVenvHome.Empty())
				return dotVenvHome;

			const Path venvHome = readVenvHome(root / "venv");
			if (!venvHome.Empty())
				return venvHome;

			const Path parent = root.parent_path();
			if (parent == root)
				break;
			root = parent;
		}

		return {};
	}

	static Path getAppPythonPlatformExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonPlatformRoot() / "python.exe";
#else
		return GetAppPythonPlatformRoot() / "bin" / "python3";
#endif
	}

	static Path getAppPythonGenericExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonRoot() / "python.exe";
#else
		return GetAppPythonRoot() / "bin" / "python3";
#endif
	}

	static Path getVenvPythonExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return Path(".venv") / "Scripts" / "python.exe";
#else
		return Path(".venv") / "bin" / "python";
#endif
	}

	Path GetAppPythonRoot()
	{
		return Path("install") / "app" / "python";
	}

	Path GetAppPythonPlatformRoot()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonRoot() / "windows";
#elif defined(DS_PLATFORM_LINUX)
		return GetAppPythonRoot() / "linux";
#elif defined(DS_PLATFORM_MACOS)
		return GetAppPythonRoot() / "macos";
#else
		return {};
#endif
	}

	Path ResolvePreferredPythonExecutable()
	{
		const Path platformPython = getAppPythonPlatformExecutable();
		if (FileSystem::Exists(platformPython.Native()))
			return platformPython;

		const Path genericPython = getAppPythonGenericExecutable();
		if (FileSystem::Exists(genericPython.Native()))
			return genericPython;

		const Path venvPython = getVenvPythonExecutable();
		if (FileSystem::Exists(venvPython.Native()))
			return venvPython;

		return Path("python");
	}

	Path ResolveEmbeddedPythonHome()
	{
		const Path venvHome = findNearbyVenvHome(Path::FromResolved(FileSystem::CurrentPath()));
		if (!venvHome.Empty() && FileSystem::Exists(venvHome.Native()))
			return venvHome;

		const Path platformRoot = GetAppPythonPlatformRoot();
		if (!platformRoot.Empty() && FileSystem::Exists(platformRoot.Native()))
			return platformRoot;

		const Path appRoot = GetAppPythonRoot();
		if (FileSystem::Exists(appRoot.Native()))
			return appRoot;

		return {};
	}

	void SetPythonHomeEnvironment(const Path &pythonHome)
	{
		if (pythonHome.Empty())
			return;

#if defined(DS_PLATFORM_WINDOWS)
		_wputenv_s(L"PYTHONHOME", pythonHome.wstring().c_str());
#else
		const std::string pythonHomeUtf8 = pythonHome.String();
		setenv("PYTHONHOME", pythonHomeUtf8.c_str(), 1);
#endif
	}
} // namespace SST::Platform
