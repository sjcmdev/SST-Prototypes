#include "Core/dspch.hpp"

#include "IO/TextFileIO.hpp"

#include <fstream>
#include <iterator>
#include <atomic>
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace SST
{
	bool TextFileIO::Save(const Path &path, std::string_view text, std::string &error)
	{
		if (path.Empty())
		{
			error = "Target path is empty";
			return false;
		}

		if (!path.parent_path().empty())
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
			if (directoryError)
			{
				error = directoryError.message();
				return false;
			}
		}

		// Create exclusively beside the destination so replacement stays on one filesystem.
		static std::atomic_uint64_t sequence{0};
		auto temporary = path.Native();
		temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
			+ "-" + std::to_string(sequence++);
		FILE *stream = nullptr;
#ifdef _WIN32
		_wfopen_s(&stream, temporary.c_str(), L"wbx");
#else
		stream = std::fopen(temporary.c_str(), "wbx");
#endif
		if (stream == nullptr)
		{
			error = "Unable to create temporary file for writing: " + path.Utf8();
			return false;
		}
		struct Cleanup
		{
			const std::filesystem::path &path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
		} cleanup{temporary};
		const bool written = std::fwrite(text.data(), 1, text.size(), stream) == text.size();
		const bool flushed = std::fflush(stream) == 0;
		const bool closed = std::fclose(stream) == 0;
		if (!written || !flushed || !closed)
		{
			error = "Unable to write file: " + path.Utf8();
			return false;
		}
#ifdef _WIN32
		if (!MoveFileExW(temporary.c_str(), path.Native().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			error = "Unable to replace " + path.Utf8() + ": " + std::system_category().message(GetLastError());
			return false;
		}
#else
		std::error_code replaceError;
		std::filesystem::rename(temporary, path.Native(), replaceError);
		if (replaceError)
		{
			error = "Unable to replace " + path.Utf8() + ": " + replaceError.message();
			return false;
		}
#endif
		error.clear();
		return true;
	}

	bool TextFileIO::Load(const Path &path, std::string &text, std::string &error)
	{
		if (path.Empty())
		{
			error = "Target path is empty";
			return false;
		}

		std::ifstream stream(path.Native(), std::ios::binary);
		if (!stream)
		{
			error = "Unable to open file for reading: " + path.Utf8();
			return false;
		}

		text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}
} // namespace SST