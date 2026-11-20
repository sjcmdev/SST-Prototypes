#pragma once
#include <algorithm>
#include <list>
#include <limits>
#include <stdexcept>
#include "Core/App/DocumentState.hpp"

namespace SST
{
    // Documents carry their app data, a stable handle and DocumentState; callers own synchronization and close policy.
    template <typename Document>
    struct ProjectWorkspace
    {
        using Handle = std::uint64_t;
        std::list<Document> projects;
        Handle active = 0;
        Handle nextHandle = 1;

        Document *Find(Handle handle)
        {
            const auto it = std::find_if(projects.begin(), projects.end(), [handle](const auto &d) { return d.handle == handle; });
            return it == projects.end() ? nullptr : &*it;
        }
        const Document *Find(Handle handle) const
        {
            const auto it = std::find_if(projects.begin(), projects.end(), [handle](const auto &d) { return d.handle == handle; });
            return it == projects.end() ? nullptr : &*it;
        }
        bool Activate(Handle handle)
        {
            if (!Find(handle)) return false;
            active = handle;
            return true;
        }
        Handle Open(Document document, bool activate = true)
        {
            if (!nextHandle || nextHandle == (std::numeric_limits<Handle>::max)()) throw std::overflow_error("Project handles exhausted.");
            document.handle = nextHandle++;
            projects.push_back(std::move(document));
            if (activate || !Find(active)) active = projects.back().handle;
            return projects.back().handle;
        }
        bool Close(Handle handle)
        {
            const auto it = std::find_if(projects.begin(), projects.end(), [handle](const auto &d) { return d.handle == handle; });
            if (it == projects.end()) return false;
            projects.erase(it);
            if (active == handle) active = projects.empty() ? 0 : projects.front().handle;
            return true;
        }
        std::optional<Handle> FindByPath(const Path &path) const
        {
            if (path.Empty()) return {};
            const auto requested = NormalizedPath(path);
            for (const auto &document : projects)
                if (document.path && NormalizedPath(*document.path) == requested) return document.handle;
            return {};
        }
    private:
        static std::filesystem::path NormalizedPath(const Path &path)
        {
            std::error_code error;
            auto normalized = std::filesystem::weakly_canonical(path.Native(), error);
            return (error ? path.Native() : normalized).lexically_normal();
        }
    };
}
