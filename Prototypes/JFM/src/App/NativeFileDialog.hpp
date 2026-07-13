#pragma once

#include "Core/Utils/Path.hpp"

#include <optional>
#include <vector>

namespace SST::Prototypes::JFM
{
	struct NativeFileDialogFilter
	{
		const char *name = "";
		const char *spec = "";
	};

	std::optional<Path> OpenFileDialog(const NativeFileDialogFilter &filter);
	std::optional<Path> SaveFileDialog(const NativeFileDialogFilter &filter, const char *defaultName);
	std::vector<Path> OpenFilesDialog(const NativeFileDialogFilter &filter);
	std::vector<Path> PickFoldersDialog(bool multiple);
} // namespace SST::Prototypes::JFM
