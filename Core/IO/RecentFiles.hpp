#pragma once
#include <algorithm>
#include <vector>

namespace SST
{
    template <typename PathValue>
    void TouchRecent(std::vector<PathValue> &recent, const PathValue &path)
    {
        const auto value = path;
        if (value.empty()) return;
        recent.erase(std::remove(recent.begin(), recent.end(), value), recent.end());
        recent.insert(recent.begin(), value);
        if (recent.size() > 10) recent.resize(10);
    }
}
