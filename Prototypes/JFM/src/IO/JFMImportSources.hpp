#pragma once

#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMImportGroup
	{
		std::vector<Path> paths;
		std::string targetSeriesId;
		std::vector<std::string> groupPath;
		std::string seriesName;
	};

	[[nodiscard]] bool IsJFMDataFile(const Path &path);
	[[nodiscard]] std::vector<JFMImportGroup> BuildImportGroups(
		const std::vector<Path> &paths,
		bool recursive,
		JFMImportPlacement placement,
		const std::string &targetSeriesId,
		std::vector<std::string> *warnings = nullptr);
	[[nodiscard]] std::vector<Path> CollectImportSourcePaths(const std::vector<Path> &paths, bool recursive);
} // namespace SST::Prototypes::JFM
