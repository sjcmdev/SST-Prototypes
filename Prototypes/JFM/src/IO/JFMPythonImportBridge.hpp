#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMImportBatchResult
	{
		std::vector<JFMCharacteristic> characteristics;
		std::vector<std::string> warnings;
	};

	class JFMPythonImportBridge
	{
	public:
		using ProgressCallback = std::function<void(std::size_t completed, std::size_t total, const std::string &message)>;

		explicit JFMPythonImportBridge(Path pythonRoot);

		[[nodiscard]] static bool IsAvailable();
		[[nodiscard]] Result<JFMImportBatchResult> ImportFiles(
			const std::vector<Path> &paths,
			const JFMImportOptions &options,
			const ProgressCallback &progressCallback);
		[[nodiscard]] Result<JFMImportBatchResult> ImportFiles(const std::vector<Path> &paths, const ProgressCallback &progressCallback);

	private:
		Path m_PythonRoot;
	};
} // namespace SST::Prototypes::JFM
