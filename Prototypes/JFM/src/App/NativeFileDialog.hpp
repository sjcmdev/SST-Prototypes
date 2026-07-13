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

	std::optional<Path> OpenFileDialog(const NativeFileDialogFilter &filter, const Path &defaultPath = {});
	std::optional<Path> SaveFileDialog(const NativeFileDialogFilter &filter, const char *defaultName, const Path &defaultPath = {});
	std::vector<Path> OpenFilesDialog(const NativeFileDialogFilter &filter, const Path &defaultPath = {});
	std::vector<Path> PickFoldersDialog(bool multiple, const Path &defaultPath = {});
} // namespace SST::Prototypes::JFM
