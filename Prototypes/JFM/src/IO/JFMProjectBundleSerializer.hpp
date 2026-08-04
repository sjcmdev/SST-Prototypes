#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	struct JFMProjectBundleSaveResult
	{
		std::vector<std::string> warnings;
	};

	struct JFMProjectBundleLoadResult
	{
		JFMProjectBundleSnapshot snapshot;
		std::vector<std::string> warnings;
	};

	class JFMProjectBundleSerializer
	{
	public:
		[[nodiscard]] static Result<JFMProjectBundleSaveResult> Save(const Path &root, const JFMProjectBundleSnapshot &snapshot);
		[[nodiscard]] static Result<JFMProjectBundleLoadResult> Load(const Path &root);
	};
} // namespace SST::Prototypes::JFM
