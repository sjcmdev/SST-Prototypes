#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace SST
{
    struct ImportSource { Path path, root; };
    struct ImportSourceEntry
    {
        ImportSource source;
        std::optional<StructuredError> error;
    };

    // Failed entries remain visible alongside readable files; directory symlinks are not followed.
    [[nodiscard]] std::vector<ImportSourceEntry> CollectSources(
        std::span<const Path> paths, bool recursive, const std::function<bool(const Path &)> &predicate);
    [[nodiscard]] std::string ImportSourceLabel(const ImportSource &source);
}
