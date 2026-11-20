#include "Core/dspch.hpp"
#include "Core/IO/ImportSources.hpp"
#include <algorithm>

namespace SST
{
    std::vector<ImportSourceEntry> CollectSources(
        std::span<const Path> paths, bool recursive, const std::function<bool(const Path &)> &predicate)
    {
        std::vector<ImportSourceEntry> result;
        auto fail = [&](const Path &path, const Path &root, const std::error_code &error) {
            result.push_back({{path, root}, StructuredError{ErrorCategory::IO, Severity::Warning,
                "Cannot read import source", path.Utf8() + ": " + error.message(),
                "Check the path and access permissions.", "ImportSources", "import.source_unreadable"}});
        };
        std::function<void(const Path &, const Path &, bool)> visit;
        visit = [&](const Path &path, const Path &root, bool descend) {
            std::error_code error;
            const auto status = std::filesystem::status(path.Native(), error);
            if (error) { fail(path, root, error); return; }
            if (std::filesystem::is_regular_file(status)) {
                if (!predicate || predicate(path)) result.push_back({{path, root}, {}});
                return;
            }
            if (!std::filesystem::is_directory(status)) {
                fail(path, root, std::make_error_code(std::errc::invalid_argument)); return;
            }
            std::filesystem::directory_iterator it(path.Native(), error), end;
            if (error) { fail(path, root, error); return; }
            while (it != end) {
                const Path child = Path::FromResolved(it->path());
                const auto childStatus = it->symlink_status(error);
                if (error) fail(child, root, error);
                else if (std::filesystem::is_regular_file(childStatus) ||
                    (descend && std::filesystem::is_directory(childStatus))) visit(child, root, descend);
                else if (std::filesystem::is_symlink(childStatus)) {
                    const bool regular=it->is_regular_file(error);
                    if (error) fail(child, root, error);
                    else if (regular) visit(child, root, false);
                }
                it.increment(error);
                if (error) { fail(path, root, error); break; }
            }
        };
        for (const auto &input : paths) {
            if (input.Empty()) { fail(input, {}, std::make_error_code(std::errc::invalid_argument)); continue; }
            std::error_code error;
            const Path path = Path::FromResolved(std::filesystem::absolute(input.Native(), error).lexically_normal());
            if (error) { fail(input, {}, error); continue; }
            const bool directory = std::filesystem::is_directory(path.Native(), error);
            visit(path, directory ? path : Path{}, recursive);
        }
        std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            return a.source.path.Native() < b.source.path.Native();
        });
        result.erase(std::unique(result.begin(), result.end(), [](const auto &a, const auto &b) {
            return a.source.path == b.source.path;
        }), result.end());
        return result;
    }

    std::string ImportSourceLabel(const ImportSource &source)
    {
        if (!source.root.Empty()) {
            const auto relative = source.path.Native().lexically_relative(source.root.Native());
            if (!relative.empty() && relative != "." && *relative.begin() != "..")
                return Path::FromResolved(relative).Utf8();
        }
        return source.path.filename().Utf8();
    }
}
