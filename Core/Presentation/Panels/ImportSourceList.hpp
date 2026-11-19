#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace SST
{
    struct ImportSourceRow { std::string label, path, warning; };
    void RenderImportSources(std::span<const ImportSourceRow> sources, std::vector<int> &selected,
        int &focused, const std::function<void(int)> &details);
}
