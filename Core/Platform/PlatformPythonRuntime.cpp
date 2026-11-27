#include "Core/dspch.hpp"

#include <cstdlib>
#include <fstream>
#include "Core/Platform/ProcessRunner.hpp"

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

    static Path findNearbyVenvExecutable(Path root)
    {
#if defined(DS_PLATFORM_WINDOWS)
        const Path relative("Scripts/python.exe");
#else
        const Path relative("bin/python");
#endif
        for (int depth = 0; depth < 8 && !root.Empty(); ++depth)
        {
            for (const char *name : {".venv", "venv"})
            {
                const Path venv = root / name;
                const Path executable = venv / relative;
                if (FileSystem::Exists((venv / "pyvenv.cfg").Native()) && FileSystem::Exists(executable.Native()))
                    return executable;
            }
            const Path parent = root.parent_path();
            if (parent == root) break;
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


	Path GetAppPythonRoot()
	{
        const Path relative = Path("install") / "app" / "python";
        if (const auto executable = CurrentExecutablePath()) {
            const auto bundled = executable->parent_path() / relative;
            if (FileSystem::Exists(bundled.Native())) return bundled;
        }
        return Path::FromResolved(FileSystem::CurrentPath()) / relative;
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

		Path venvPython = findNearbyVenvExecutable(Path::FromResolved(FileSystem::CurrentPath()));
        if (venvPython.Empty())
            if (const auto executable = CurrentExecutablePath(); executable)
                venvPython = findNearbyVenvExecutable(executable.Value().parent_path());
		if (!venvPython.Empty() && FileSystem::Exists(venvPython.Native()))
			return venvPython;

		return Path("python");
	}

	Path ResolveEmbeddedPythonHome()
    {
		const Path platformRoot = GetAppPythonPlatformRoot();
		if (!platformRoot.Empty() && FileSystem::Exists(getAppPythonPlatformExecutable().Native()))
			return platformRoot;

		const Path appRoot = GetAppPythonRoot();
		if (FileSystem::Exists((getAppPythonGenericExecutable()).Native()))
			return appRoot;

        const Path executable = ResolvePreferredPythonExecutable();
        const Path venvHome = executable == Path("python") ? Path{} : readVenvHome(executable.parent_path().parent_path());
        if (!venvHome.Empty() && FileSystem::Exists(venvHome.Native())) return venvHome;
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
