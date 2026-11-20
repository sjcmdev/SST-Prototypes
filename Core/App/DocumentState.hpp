#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include "Core/Utils/Path.hpp"

namespace SST
{
    struct DocumentState
    {
        std::optional<Path> path;
        std::uint64_t revision = 0;
        std::uint64_t savedRevision = 0;

        [[nodiscard]] bool IsDirty() const { return revision != savedRevision; }
        void MarkSaved(Path target, std::uint64_t writtenRevision)
        {
            path = std::move(target);
            savedRevision = writtenRevision;
        }
    };
}
