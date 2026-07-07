#include "Core/dspch.hpp"

#include "IO/JFMImportPaths.hpp"

namespace SST::Prototypes::JFM
{
	std::vector<Path> BuildJFMPythonRootCandidates(const ApplicationConfig &config, const Path &searchRoot)
	{
		std::vector<Path> candidates;
		if (!config.paths.installRoot.Empty())
		{
			const Path bundleRoot = config.paths.installRoot.parent_path();
			candidates.push_back(bundleRoot / Path("python"));
		}

		if (!searchRoot.Empty())
		{
			candidates.push_back(searchRoot / Path("Prototypes") / Path("JFM") / Path("python"));
			candidates.push_back(searchRoot / Path("python"));
		}
		return candidates;
	}

	Path ResolveJFMPythonRoot(const ApplicationConfig &config)
	{
		return ResolveJFMPythonRoot(config, Path::FromResolved(FileSystem::CurrentPath()));
	}

	Path ResolveJFMPythonRoot(const ApplicationConfig &config, const Path &searchRoot)
	{
		std::vector<Path> candidates = BuildJFMPythonRootCandidates(config, searchRoot);
		for (const Path &candidate : candidates)
		{
			if (FileSystem::Exists(candidate.Native()))
				return candidate;
		}
		return candidates.empty() ? Path{} : candidates.front();
	}
} // namespace SST::Prototypes::JFM
