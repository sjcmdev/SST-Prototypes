#include "Core/dspch.hpp"

#include "IO/JFMImportSources.hpp"
#include "Core/IO/ImportSources.hpp"

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

	std::map<std::string, std::vector<Path>> DataFilesByFolder(const Path &directory, bool recursive, std::vector<std::string> *warnings)
	{
		std::map<std::string, std::vector<Path>> filesByFolder;
		for (const auto &entry : CollectSources(std::vector<Path>{directory}, recursive, IsJFMDataFile)) {
			if (entry.error) { if (warnings) warnings->push_back(entry.error->technicalDetails); }
			else filesByFolder[entry.source.path.parent_path().Utf8()].push_back(entry.source.path);
		}
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
		const std::string &targetSeriesId,
		std::vector<std::string> *warnings)
	{
		std::vector<JFMImportGroup> groups;
		std::vector<Path> files;
		const std::string destinationSeries = placement == JFMImportPlacement::ExistingSeries ? targetSeriesId : std::string{};
		const bool scanRecursively = recursive || placement == JFMImportPlacement::PreserveFolderTree;
		for (const Path &path : paths)
		{
			if (Detail::IsDirectory(path))
			{
				for (auto &[folder, folderFiles] : Detail::DataFilesByFolder(path, scanRecursively, warnings))
				{
					const Path folderPath = Path::FromResolved(std::filesystem::u8path(folder));
					groups.push_back({
						std::move(folderFiles),
						destinationSeries,
						destinationSeries.empty() ? Detail::ParentGroupPathForFolder(path, folderPath) : std::vector<std::string>{},
						destinationSeries.empty() ? Detail::FolderName(folderPath) : std::string{}});
				}
			}
			else
			{
				for (const auto &entry : CollectSources(std::vector<Path>{path}, false, IsJFMDataFile)) {
					if (entry.error && warnings) warnings->push_back(entry.error->technicalDetails);
					if (IsJFMDataFile(entry.source.path)) files.emplace_back(entry.source.path);
				}
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
		for (const auto &entry : CollectSources(paths, recursive, IsJFMDataFile))
			if (IsJFMDataFile(entry.source.path)) result.push_back(entry.source.path);
		return result;
	}
} // namespace SST::Prototypes::JFM
