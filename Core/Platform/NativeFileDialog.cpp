#include "Core/dspch.hpp"

#include "Core/Platform/NativeFileDialog.hpp"

#include <nfd.h>

#include <string>
#include <stdexcept>
#include <utility>
#include <memory>

namespace SST
{
	namespace
	{
	std::string LastError();

	class NativeFileDialogSession
	{
	public:
		NativeFileDialogSession()
		{
			if (NFD_Init() != NFD_OKAY)
				throw std::runtime_error(LastError());
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

	Path TakePath(nfdu8char_t *rawPath, bool fromSet = false)
	{
		if (rawPath == nullptr)
			throw std::runtime_error("Native file dialog returned an empty path.");

		auto release = [fromSet](nfdu8char_t *value) {
			if (fromSet) NFD_PathSet_FreePathU8(value);
			else NFD_FreePathU8(value);
		};
		std::unique_ptr<nfdu8char_t, decltype(release)> owner(rawPath, release);
		return Path::FromResolved(std::filesystem::u8path(rawPath));
	}

	const nfdu8char_t *DefaultPathOrNull(const Path &path, std::string &storage)
	{
		if (path.Empty())
			return nullptr;
		const auto utf8 = path.Native().u8string();
		storage.assign(utf8.begin(), utf8.end());
		return storage.c_str();
	}

	std::vector<Path> CollectPathSet(const nfdpathset_t *pathSet)
	{
		nfdpathsetsize_t count = 0;
		std::vector<Path> paths;
		std::unique_ptr<const nfdpathset_t, decltype(&NFD_PathSet_Free)> owner(pathSet, &NFD_PathSet_Free);
		if (NFD_PathSet_GetCount(pathSet, &count) != NFD_OKAY)
			throw std::runtime_error(LastError());

		paths.reserve(static_cast<std::size_t>(count));
		for (nfdpathsetsize_t index = 0; index < count; ++index)
		{
			nfdu8char_t *rawPath = nullptr;
			if (NFD_PathSet_GetPathU8(pathSet, index, &rawPath) != NFD_OKAY)
				throw std::runtime_error(LastError());
			paths.emplace_back(TakePath(rawPath, true));
		}
		return paths;
	}
	} // namespace

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
			return paths;
		}

		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_PickFolderU8(&rawPath, defaultPathTextOrNull);
		if (result == NFD_CANCEL)
			return {};
		ThrowIfDialogFailed(result);

		return {TakePath(rawPath)};
	}
} // namespace SST
