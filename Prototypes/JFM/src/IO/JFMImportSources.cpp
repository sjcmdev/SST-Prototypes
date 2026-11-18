#include "Core/dspch.hpp"

#include "IO/JFMImportSources.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <system_error>

namespace SST::Prototypes::JFM::Detail
{
	bool IsDirectory(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_directory(path.Native(), error);
	}

	bool IsRegularFile(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(path.Native(), error);
	}

	std::string FolderName(const Path &path)
	{
		std::string name = path.filename().Utf8();
		return name.empty() ? path.Utf8() : name;
	}

	std::vector<std::string> ParentGroupPathForFolder(const Path &root, const Path &folder)
	{
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(folder.Native(), root.Native(), error);
		if (error || relative.empty() || relative == ".")
			return {};

		const std::filesystem::path parent = relative.parent_path();
		if (parent.empty())
			return {FolderName(root)};

		std::vector<std::string> names{FolderName(root)};
		for (const std::filesystem::path &part : parent)
			names.push_back(Path::FromResolved(part).Utf8());
		return names;
	}

	std::vector<Path> DirectDataFilesInDirectory(const Path &directory)
	{
		std::vector<Path> paths;
		std::error_code error;
		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
		{
			if (error)
				break;
			std::error_code fileError;
			const Path path = Path::FromResolved(entry.path());
			if (entry.is_regular_file(fileError) && IsJFMDataFile(path))
				paths.emplace_back(path);
		}
		std::sort(paths.begin(), paths.end(), [](const Path &left, const Path &right) { return left.Native() < right.Native(); });
		return paths;
	}

	std::map<std::string, std::vector<Path>> DataFilesByFolder(const Path &directory, bool recursive)
	{
		std::map<std::string, std::vector<Path>> filesByFolder;
		if (!recursive)
		{
			std::vector<Path> paths = DirectDataFilesInDirectory(directory);
			if (!paths.empty())
				filesByFolder.emplace(directory.Utf8(), std::move(paths));
			return filesByFolder;
		}

		std::error_code error;
		const auto options = std::filesystem::directory_options::skip_permission_denied;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(directory.Native(), options, error))
		{
			if (error)
				break;
			std::error_code fileError;
			const Path path = Path::FromResolved(entry.path());
			if (entry.is_regular_file(fileError) && IsJFMDataFile(path))
				filesByFolder[path.parent_path().Utf8()].emplace_back(path);
		}
		for (auto &[folder, paths] : filesByFolder)
			std::sort(paths.begin(), paths.end(), [](const Path &left, const Path &right) { return left.Native() < right.Native(); });
		return filesByFolder;
	}
} // namespace SST::Prototypes::JFM::Detail

namespace SST::Prototypes::JFM
{
	bool IsJFMDataFile(const Path &path)
	{
		std::string extension = path.extension().Utf8();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".dat" || extension == ".iv" || extension == ".txt" || extension == ".csv";
	}

	std::vector<JFMImportGroup> BuildImportGroups(
		const std::vector<Path> &paths,
		bool recursive,
		JFMImportPlacement placement,
		const std::string &targetSeriesId)
	{
		std::vector<JFMImportGroup> groups;
		std::vector<Path> files;
		const std::string destinationSeries = placement == JFMImportPlacement::ExistingSeries ? targetSeriesId : std::string{};
		const bool scanRecursively = recursive || placement == JFMImportPlacement::PreserveFolderTree;
		for (const Path &path : paths)
		{
			if (Detail::IsDirectory(path))
			{
				for (auto &[folder, folderFiles] : Detail::DataFilesByFolder(path, scanRecursively))
				{
					const Path folderPath = Path::FromResolved(std::filesystem::u8path(folder));
					groups.push_back({
						std::move(folderFiles),
						destinationSeries,
						destinationSeries.empty() ? Detail::ParentGroupPathForFolder(path, folderPath) : std::vector<std::string>{},
						destinationSeries.empty() ? Detail::FolderName(folderPath) : std::string{}});
				}
			}
			else if (Detail::IsRegularFile(path) || !std::filesystem::exists(path.Native()))
			{
				if (IsJFMDataFile(path))
					files.emplace_back(path);
			}
		}

		if (!files.empty())
		{
			std::sort(files.begin(), files.end(), [](const Path &left, const Path &right) { return left.Native() < right.Native(); });
			const std::string seriesName = Detail::FolderName(files.front().parent_path());
			groups.push_back({std::move(files), destinationSeries, {}, seriesName.empty() ? "Series" : seriesName});
		}
		return groups;
	}

	std::vector<Path> CollectImportSourcePaths(const std::vector<Path> &paths, bool recursive)
	{
		std::vector<Path> result;
		for (JFMImportGroup &group : BuildImportGroups(paths, recursive, JFMImportPlacement::NewSeries, {}))
			result.insert(result.end(), std::make_move_iterator(group.paths.begin()), std::make_move_iterator(group.paths.end()));
		std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) { return left.Native() < right.Native(); });
		result.erase(std::unique(result.begin(), result.end(), [](const Path &left, const Path &right) { return left == right; }), result.end());
		return result;
	}
} // namespace SST::Prototypes::JFM
