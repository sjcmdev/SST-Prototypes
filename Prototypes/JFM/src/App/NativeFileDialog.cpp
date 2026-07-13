#include "Core/dspch.hpp"

#include "App/NativeFileDialog.hpp"

#include <nfd.h>

#include <string>
#include <stdexcept>
#include <utility>

namespace SST::Prototypes::JFM
{

	class NativeFileDialogSession
	{
	public:
		NativeFileDialogSession()
		{
			NFD_Init();
		}

		~NativeFileDialogSession()
		{
			NFD_Quit();
		}
	};

	std::string LastError()
	{
		const char *error = NFD_GetError();
		return error != nullptr ? std::string(error) : "Native file dialog failed.";
	}

	nfdu8filteritem_t ToNfdFilter(const NativeFileDialogFilter &filter)
	{
		return nfdu8filteritem_t{filter.name, filter.spec};
	}

	void ThrowIfDialogFailed(nfdresult_t result)
	{
		if (result == NFD_ERROR)
			throw std::runtime_error(LastError());
	}

	std::optional<Path> TakePath(nfdu8char_t *rawPath)
	{
		if (rawPath == nullptr)
			return std::nullopt;

		std::string pathText(rawPath);
		NFD_FreePathU8(rawPath);
		return Path::FromResolved(pathText);
	}

	const nfdu8char_t *DefaultPathOrNull(const Path &path, std::string &storage)
	{
		if (path.Empty())
			return nullptr;
		storage = path.String();
		return storage.c_str();
	}

	std::vector<Path> CollectPathSet(const nfdpathset_t *pathSet)
	{
		nfdpathsetsize_t count = 0;
		std::vector<Path> paths;
		if (NFD_PathSet_GetCount(pathSet, &count) != NFD_OKAY)
			return paths;

		paths.reserve(static_cast<std::size_t>(count));
		for (nfdpathsetsize_t index = 0; index < count; ++index)
		{
			nfdu8char_t *rawPath = nullptr;
			if (NFD_PathSet_GetPathU8(pathSet, index, &rawPath) == NFD_OKAY)
			{
				if (auto path = TakePath(rawPath))
					paths.emplace_back(std::move(*path));
			}
		}
		return paths;
	}
	

	std::optional<Path> OpenFileDialog(const NativeFileDialogFilter &filter, const Path &defaultPath)
	{
		NativeFileDialogSession session;
		nfdu8filteritem_t nfdFilter = ToNfdFilter(filter);
		nfdu8char_t *rawPath = nullptr;
		std::string defaultPathText;
		const nfdresult_t result = NFD_OpenDialogU8(&rawPath, &nfdFilter, 1, DefaultPathOrNull(defaultPath, defaultPathText));
		if (result == NFD_CANCEL)
			return std::nullopt;
		ThrowIfDialogFailed(result);
		return TakePath(rawPath);
	}

	std::optional<Path> SaveFileDialog(const NativeFileDialogFilter &filter, const char *defaultName, const Path &defaultPath)
	{
		NativeFileDialogSession session;
		nfdu8filteritem_t nfdFilter = ToNfdFilter(filter);
		nfdu8char_t *rawPath = nullptr;
		std::string defaultPathText;
		const nfdresult_t result = NFD_SaveDialogU8(&rawPath, &nfdFilter, 1, DefaultPathOrNull(defaultPath, defaultPathText), defaultName);
		if (result == NFD_CANCEL)
			return std::nullopt;
		ThrowIfDialogFailed(result);
		return TakePath(rawPath);
	}

	std::vector<Path> OpenFilesDialog(const NativeFileDialogFilter &filter, const Path &defaultPath)
	{
		NativeFileDialogSession session;
		nfdu8filteritem_t nfdFilter = ToNfdFilter(filter);
		const nfdpathset_t *pathSet = nullptr;
		std::string defaultPathText;
		const nfdresult_t result = NFD_OpenDialogMultipleU8(&pathSet, &nfdFilter, 1, DefaultPathOrNull(defaultPath, defaultPathText));
		if (result == NFD_CANCEL)
			return {};
		ThrowIfDialogFailed(result);

		std::vector<Path> paths = CollectPathSet(pathSet);
		NFD_PathSet_Free(pathSet);
		return paths;
	}

	std::vector<Path> PickFoldersDialog(bool multiple, const Path &defaultPath)
	{
		NativeFileDialogSession session;
		std::string defaultPathText;
		const nfdu8char_t *defaultPathTextOrNull = DefaultPathOrNull(defaultPath, defaultPathText);
		if (multiple)
		{
			const nfdpathset_t *pathSet = nullptr;
			const nfdresult_t result = NFD_PickFolderMultipleU8(&pathSet, defaultPathTextOrNull);
			if (result == NFD_CANCEL)
				return {};
			ThrowIfDialogFailed(result);

			std::vector<Path> paths = CollectPathSet(pathSet);
			NFD_PathSet_Free(pathSet);
			return paths;
		}

		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_PickFolderU8(&rawPath, defaultPathTextOrNull);
		if (result == NFD_CANCEL)
			return {};
		ThrowIfDialogFailed(result);

		std::optional<Path> path = TakePath(rawPath);
		if (!path)
			return {};
		return {std::move(*path)};
	}
} // namespace SST::Prototypes::JFM
