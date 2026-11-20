#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewHost.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace SST
{
    void RenderComparisonTable(ViewHost &host, ViewInstance &view)
    {
        auto settings = view.Table();
        auto filter = settings.at("filter_text").get<std::string>();
        auto update = [&] { (void)host.Execute({{"operation", "table"}, {"instance_id", view.GetId()}, {"table", settings}}); };
        if (sst::ui::widgets::Text("Filter", &filter)) { settings["filter_text"] = filter; update(); }
        if (ImGui::Button("All overlays")) { settings["selected_item_ids"] = nullptr; update(); }
        ImGui::SameLine();
        if (ImGui::Button("Clear selection")) { settings["selected_item_ids"] = nlohmann::json::array(); update(); }
        ImGui::TextDisabled("Ctrl+click toggles rows; Shift+click selects a range.");
        if (!ImGui::BeginTable("##comparison", int(host.Metrics().size()) + 1,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) return;
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_DefaultSort, 0, 0);
        int column = 1;
        for (const auto &metric : host.Metrics())
        {
            const auto unit = metric.at("unit").get<std::string>();
            auto label = metric.at("name").get<std::string>() + (unit.empty() ? "" : " (" + unit + ")") + "##" + metric.at("id").get<std::string>();
            ImGui::TableSetupColumn(label.c_str(), 0, 0, column++);
        }
        ImGui::TableSetupScrollFreeze(1, 1); ImGui::TableHeadersRow();
        // The model also changes through Python/workspace restore, outside header clicks.
        const auto sortKey = ImGui::GetID("##model_sort");
        const auto sortSignature = ImGui::GetID((settings.at("sort_metric_id").get<std::string>() + settings.at("descending").dump()).c_str());
        if (ImGuiID(ImGui::GetStateStorage()->GetInt(sortKey)) != sortSignature)
        {
            int sortedColumn = 0;
            for (std::size_t i = 0; i < host.Metrics().size(); ++i)
                if (host.Metrics()[i].at("id") == settings.at("sort_metric_id")) sortedColumn = int(i) + 1;
            ImGui::TableSetColumnSortDirection(sortedColumn, settings.at("descending").get<bool>() ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending, false);
            ImGui::TableGetSortSpecs()->SpecsDirty = false;
            ImGui::GetStateStorage()->SetInt(sortKey, int(sortSignature));
        }
        if (auto specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount)
        {
            const auto &sort = specs->Specs[0];
            settings["sort_metric_id"] = sort.ColumnUserID == 0 ? std::string{} : host.Metrics().at(sort.ColumnUserID - 1).at("id").get<std::string>();
            settings["descending"] = sort.SortDirection == ImGuiSortDirection_Descending;
            update(); specs->SpecsDirty = false;
            ImGui::GetStateStorage()->SetInt(sortKey, int(ImGui::GetID((settings.at("sort_metric_id").get<std::string>() + settings.at("descending").dump()).c_str())));
        }
        const auto rows = host.Items(view, false);
        auto selected = settings.at("selected_item_ids");
        // ImGui storage scopes the range anchor to this window and stable row ID.
        const auto anchorKey = ImGui::GetID("##selection_anchor");
        auto storage = ImGui::GetStateStorage();
        std::vector<ImGuiID> rowIds;
        for (const auto &item : rows) rowIds.push_back(ImGui::GetID(item.at("id").get<std::string>().c_str()));
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const auto &item = rows[index]; const auto id = item.at("id").get<std::string>();
            ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
            const bool chosen = selected.is_null() || std::find(selected.begin(), selected.end(), id) != selected.end();
            const auto label = item.at("name").get<std::string>() + "###row";
            if (ImGui::Selectable(label.c_str(), chosen, ImGuiSelectableFlags_SpanAllColumns))
            {
                if (selected.is_null())
                {
                    selected = nlohmann::json::array();
                    if (ImGui::GetIO().KeyCtrl) for (const auto &row : rows) selected.push_back(row.at("id"));
                }
                if (ImGui::GetIO().KeyShift)
                {
                    const auto found = std::find(rowIds.begin(), rowIds.end(), ImGuiID(storage->GetInt(anchorKey)));
                    const auto anchor = found == rowIds.end() ? int(index) : int(found - rowIds.begin());
                    selected = nlohmann::json::array();
                    for (int i = std::min(anchor, int(index)); i <= std::max(anchor, int(index)) && i < int(rows.size()); ++i)
                        if (i >= 0) selected.push_back(rows[i].at("id"));
                }
                else if (ImGui::GetIO().KeyCtrl)
                {
                    auto it = std::find(selected.begin(), selected.end(), id);
                    if (it == selected.end()) selected.push_back(id); else selected.erase(it);
                }
                else selected = nlohmann::json::array({id});
                storage->SetInt(anchorKey, int(rowIds[index])); settings["selected_item_ids"] = selected; update();
            }
            for (const auto &metric : host.Metrics())
            {
                ImGui::TableNextColumn();
                const auto value = item.at("metrics").value(metric.at("id").get<std::string>(), nlohmann::json());
                if (value.is_null()) ImGui::TextDisabled("--"); else ImGui::Text("%.6g", value.get<double>());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
