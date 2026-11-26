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
#ifdef _WIN32
	namespace
	{
		std::filesystem::path WindowsFilePath(const std::filesystem::path &path, std::error_code &error)
		{
			auto absolute = std::filesystem::absolute(path, error).lexically_normal().make_preferred();
			if (error) return {};
			const auto &native = absolute.native();
			if (native.starts_with(L"\\\\?\\") || native.starts_with(L"\\\\.\\")) return absolute;
			if (native.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + native.substr(2);
			return L"\\\\?\\" + native;
		}
	}
#endif
	bool TextFileIO::Save(const Path &path, std::string_view text, std::string &error)
	{
		return Save(path, [text](FILE *stream) { return std::fwrite(text.data(), 1, text.size(), stream) == text.size(); }, error);
	}

	bool TextFileIO::Save(const Path &path, const std::function<bool(FILE *)> &write, std::string &error)
	{
		if (path.Empty())
		{
			error = "Target path is empty";
			return false;
		}

		auto destination = path.Native();
#ifdef _WIN32
		std::error_code pathError;
		destination = WindowsFilePath(destination, pathError);
		if (pathError) { error = pathError.message(); return false; }
#endif
		if (!destination.parent_path().empty())
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(destination.parent_path(), directoryError);
			if (directoryError)
			{
				error = directoryError.message();
				return false;
			}
		}

		// Create exclusively beside the destination so replacement stays on one filesystem.
		static std::atomic_uint64_t sequence{0};
		auto temporary = destination;
		temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
			+ "-" + std::to_string(sequence++);
		FILE *stream = nullptr;
#ifdef _WIN32
		const auto openError = _wfopen_s(&stream, temporary.c_str(), L"wbx");
#else
		stream = std::fopen(temporary.c_str(), "wbx");
#endif
		if (stream == nullptr)
		{
			error = "Unable to create temporary file for writing: " + path.Utf8();
#ifdef _WIN32
			error += ": " + std::generic_category().message(openError);
#endif
			return false;
		}
		struct Cleanup
		{
			const std::filesystem::path &path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
		} cleanup{temporary};
		bool written = false;
		try { written = write(stream); }
		catch (...) { std::fclose(stream); throw; }
		const bool flushed = std::fflush(stream) == 0;
		const bool closed = std::fclose(stream) == 0;
		if (!written || !flushed || !closed)
		{
			error = "Unable to write file: " + path.Utf8();
			return false;
		}
#ifdef _WIN32
		if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
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

		auto source = path.Native();
#ifdef _WIN32
		std::error_code pathError;
		source = WindowsFilePath(source, pathError);
		if (pathError) { error = pathError.message(); return false; }
#endif
		std::ifstream stream(source, std::ios::binary);
		if (!stream)
		{
			error = "Unable to open file for reading: " + path.Utf8();
			return false;
		}

		text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}
} // namespace SST
