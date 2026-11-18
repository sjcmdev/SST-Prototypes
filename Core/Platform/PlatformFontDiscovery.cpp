#include "Core/dspch.hpp"

#include "Core/Platform/PlatformFontDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Logging/Logger.hpp"

namespace SST::Platform
{
	namespace
	{
		std::string toLowerCopy(std::string value)
		{
			for (char &character : value)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return value;
		}

		bool isSupportedFontFile(const FilePath &path)
		{
			const std::string extension = toLowerCopy(Path::FromResolved(path.extension()).Utf8());
			return extension == ".ttf" || extension == ".otf";
		}

		std::string fontLabelFromPath(const FilePath &fontPath, std::string_view source)
		{
			const std::string filename = Path::FromResolved(fontPath.filename()).Utf8();
			return std::string(source) + ": " + filename;
		}

		void collectFontsFromDirectory(std::vector<FontOption> &fontOptions,
		                               std::vector<std::string> &seenPaths,
		                               const FilePath &directory,
		                               std::string_view source)
		{
			std::error_code error;
			if (!std::filesystem::exists(directory, error) || error)
				return;

			std::filesystem::recursive_directory_iterator iterator(
				directory,
				std::filesystem::directory_options::skip_permission_denied,
				error);
			const std::filesystem::recursive_directory_iterator end;
			if (error)
			{
				DS_LOG_WARN("Font scan skipped [{}]: {}", Path::FromResolved(directory).Utf8(), error.message());
				return;
			}

			for (; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					DS_LOG_WARN("Font scan warning [{}]: {}", Path::FromResolved(directory).Utf8(), error.message());
					error.clear();
					continue;
				}

				const std::filesystem::directory_entry &entry = *iterator;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				const FilePath fontPath = entry.path();
				if (!isSupportedFontFile(fontPath))
					continue;

				const std::string normalizedPath = toLowerCopy(Path::FromResolved(fontPath.lexically_normal()).Utf8());
				if (std::find(seenPaths.begin(), seenPaths.end(), normalizedPath) != seenPaths.end())
					continue;

				FontOption option;
				option.label = fontLabelFromPath(fontPath, source);
				option.source = std::string(source);
				option.path = Path::FromResolved(FileSystem::Absolute(fontPath, error).lexically_normal()).Utf8();
				if (error)
				{
					error.clear();
					option.path = Path::FromResolved(fontPath.lexically_normal()).Utf8();
				}

				fontOptions.push_back(std::move(option));
				seenPaths.push_back(std::move(normalizedPath));
			}
		}
	} // namespace

	FontOption DefaultFontOption(const Path &bundledFontsDirectory)
	{
#ifdef DS_PLATFORM_WINDOWS
		for (const auto &directory : GetSystemFontDirectories())
		{
			const auto font = directory / "segoeui.ttf";
			if (FileSystem::Exists(font))
				return {"Default: Segoe UI", "System", Path::FromResolved(font).Utf8()};
		}
#endif
		const auto fallback = Path::FromResolved(bundledFontsDirectory.Native() / "DejaVuSans.ttf");
		DS_LOG_WARN("System Segoe UI unavailable; using bundled DejaVu Sans: {}", fallback.Utf8());
		return {"Default: DejaVu Sans", "Assets", fallback.Utf8()};
	}

	bool FontPathsEqual(std::string_view left, std::string_view right)
	{
		return toLowerCopy(std::string(left)) == toLowerCopy(std::string(right));
	}

	std::vector<FontOption> GetFontsFromDirectory(const Path &directory, std::string_view source)
	{
		std::vector<FontOption> fontOptions;
		std::vector<std::string> seenPaths;
		collectFontsFromDirectory(fontOptions, seenPaths, directory.Native(), source);
		std::sort(fontOptions.begin(), fontOptions.end(), [](const FontOption &left, const FontOption &right) {
			if (left.source != right.source)
				return left.source < right.source;
			return left.label < right.label;
		});
		return fontOptions;
	}

	std::vector<FontOption> GetSystemFonts()
	{
		std::vector<FontOption> fontOptions;
		std::vector<std::string> seenPaths;
		for (const FilePath &directory : GetSystemFontDirectories())
			collectFontsFromDirectory(fontOptions, seenPaths, directory, "System");
		std::sort(fontOptions.begin(), fontOptions.end(), [](const FontOption &left, const FontOption &right) {
			if (left.source != right.source)
				return left.source < right.source;
			return left.label < right.label;
		});
		return fontOptions;
	}
} // namespace SST::Platform
