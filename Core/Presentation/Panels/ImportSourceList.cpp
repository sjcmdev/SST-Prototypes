#include "Core/dspch.hpp"
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include <imgui.h>
#include <algorithm>

namespace SST
{
    void RenderImportSources(std::span<const ImportSourceRow> sources, std::vector<int> &selected,
        int &focused, const std::function<void(int)> &details)
    {
        ImGui::PushID("import_sources");
        if (!ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable)) { ImGui::PopID(); return; }
        ImGui::TableSetupColumn("Sources", ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableSetupColumn("Details");
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::BeginChild("list", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (int index = 0; index < static_cast<int>(sources.size()); ++index) {
            const auto &row = sources[index];
            ImGui::PushID(index);
            const auto found = std::find(selected.begin(), selected.end(), index);
            const bool active = found != selected.end();
            if (ImGui::Selectable((row.label + (row.warning.empty() ? "" : " [warning]") + "##source").c_str(), active)) {
                const bool append = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
                if (active && append) selected.erase(found);
                else {
                    if (!append) selected.clear();
                    if (!active || !append) selected.push_back(index);
                }
                focused = index;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s%s", row.path.c_str(),
                row.warning.empty() ? "" : "\n", row.warning.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TableNextColumn();
        if (details && focused >= 0 && focused < static_cast<int>(sources.size())) details(focused);
        ImGui::EndTable();
        ImGui::PopID();
    }
}
