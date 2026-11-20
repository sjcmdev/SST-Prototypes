#include "Core/dspch.hpp"

#include "IO/JFMPythonRuntime.hpp"

#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#endif

namespace SST::Prototypes::JFM
{
#if DS_PYTHON_CAPI_AVAILABLE
	namespace
	{
		namespace nb = nanobind;

		std::string pyString(nb::handle value)
		{
			if (!value.is_valid() || value.is_none())
				return {};
			const nb::str text(value);
			const char *raw = text.c_str();
			return raw == nullptr ? std::string{} : std::string(raw);
		}

		bool appendStringToSysPath(const std::string &path)
		{
			if (path.empty())
				return true;
			nb::list sysPath = nb::cast<nb::list>(nb::module_::import_("sys").attr("path"));
			for (nb::handle item : sysPath)
			{
				if (pyString(item) == path)
					return true;
			}
			sysPath.insert(0, nb::str(path.c_str()));
			return true;
		}

		void appendVenvSitePackages(const Path &venvRoot)
		{
			if (venvRoot.Empty() || !FileSystem::Exists((venvRoot / Path("pyvenv.cfg")).Native()))
				return;
#if defined(DS_PLATFORM_WINDOWS)
			const Path sitePackages = venvRoot / Path("Lib") / Path("site-packages");
			if (FileSystem::Exists(sitePackages.Native()))
				(void)appendStringToSysPath(sitePackages.Utf8());
#else
			const Path libRoot = venvRoot / Path("lib");
			if (!FileSystem::Exists(libRoot.Native()))
				return;
			std::error_code error;
			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(libRoot.Native(), error))
			{
				if (error || !entry.is_directory())
					continue;
				const Path sitePackages = Path::FromResolved(entry.path()) / Path("site-packages");
				if (FileSystem::Exists(sitePackages.Native()))
					(void)appendStringToSysPath(sitePackages.String());
			}
#endif
		}

		void appendNearbyVenvs(Path root)
		{
			for (int depth = 0; depth < 8 && !root.Empty(); ++depth)
			{
				appendVenvSitePackages(root / Path(".venv"));
				appendVenvSitePackages(root / Path("venv"));
				const Path parent = root.parent_path();
				if (parent == root)
					break;
				root = parent;
			}
		}
	}
#endif

	bool ConfigureJFMPythonPaths(const Path &pythonRoot)
	{
#if DS_PYTHON_CAPI_AVAILABLE
		appendNearbyVenvs(Path::FromResolved(FileSystem::CurrentPath()));
		appendNearbyVenvs(pythonRoot);
        (void)appendStringToSysPath(SST_IMPORT_PYTHON_ROOT);
		return appendStringToSysPath(pythonRoot.Utf8());
#else
		(void)pythonRoot;
		return false;
#endif
	}
}
