#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include <imgui.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::Render() {
    if (!IsVisible()) return;
    bool visible = true;
    const bool content = ImGui::Begin(GetTitle().c_str(), &visible);
    SetVisible(visible);
    if (!content) { ImGui::End(); return; }
    auto &doc = state->document;
    ImGui::Text("%s%s", doc.path.Empty() ? "Untitled" : doc.path.Utf8().c_str(), doc.dirty ? " (unsaved)" : "");
    bool comma = state->decimal == ',';
    if (ImGui::Checkbox("Decimal comma (semicolon/whitespace columns)", &comma)) state->decimal = comma ? ',' : '.';
    if (ImGui::Button("Import lab AS...")) (void)commands->Execute(CommandID{"caplab.preview"});
    ImGui::SameLine();
    if (ImGui::Button("Save project")) (void)commands->Execute(CommandID{"project.save"});
    if (state->preview) {
        const auto &preview = *state->preview;
        ImGui::SeparatorText("Import preview: lab Cp/Rp (SI)");
        ImGui::TextWrapped("%s", preview.at("source").get<std::string>().c_str());
        ImGui::TextUnformatted("Columns: f [Hz], Cp [F], Rp [Ohm]; additional columns kept as source data.");
        ImGui::Text("Header T: %s K; filename suggestion: %s K", preview.at("temperature_K").dump().c_str(),
            preview.at("suggested_temperature_K").dump().c_str());
        ImGui::TextUnformatted("Confirm temperature below; filename is only a suggestion.");
        bool known = state->temperature.has_value();
        if (ImGui::Checkbox("Temperature known", &known)) state->temperature = known ? std::optional<double>{300} : std::nullopt;
        if (state->temperature) ImGui::InputDouble("Temperature (K)", &*state->temperature, 0, 0, "%.6g");
        if (state->temperature && !preview.at("temperature_K").is_null() && *state->temperature != preview.at("temperature_K").get<double>())
            ImGui::TextWrapped("Temperature conflict: confirmation will override header T.");
        const auto columns = preview.at("columns").get<std::vector<std::vector<double>>>();
        ImGui::Text("%zu points; %zu columns", columns[0].size(), columns.size());
        if (ImGui::BeginTable("preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            for (auto name : {"Source row", "f (Hz)", "Cp (F)", "Rp (Ohm)"}) ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(columns[0].size()));
            while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::Text("%zu", preview.at("source_rows").at(row).get<std::size_t>());
                for (int c = 0; c < 3; ++c) { ImGui::TableNextColumn(); ImGui::Text("%.8g", columns[c][row]); }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Confirm import")) (void)commands->Execute(CommandID{"caplab.import"});
        ImGui::SameLine(); if (ImGui::Button("Discard preview")) state->preview.reset();
    }
    for (const auto &sample : doc.project.samples) {
        ImGui::PushID(static_cast<int>(sample.id));
        if (ImGui::TreeNode(sample.name.c_str())) {
            for (const auto &sweep : doc.project.sweeps) if (sweep.sample_id == sample.id)
                ImGui::Text("Cp/Rp: %zu points; T=%s K", std::get<pl::Worksheet>(doc.project.engine->object(sweep.id).data).rows.size(), Nullable(sweep.temperature_K).dump().c_str());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (doc.project.graph) {
        const auto &p = *doc.project.engine;
        const auto layer = p.children(doc.project.graph).at(0);
        std::vector<sst::ui::plot::SeriesEntryView> series;
        for (auto id : p.children(layer)) series.push_back(p.series(id));
        sst::ui::plot::RenderPlot("##cf", std::get<pl::Layer>(p.object(layer).data).spec,
            series.data(), series.size(), state->view, {}, ImVec2(-1, 260));
        if (ImGui::Button("Export C-f PNG...")) (void)commands->Execute(CommandID{"caplab.export"});
    }
    ImGui::TextWrapped("%s", state->status.c_str());
    if (const auto job = state->jobs->GetJob(state->activeJob)) {
        ImGui::TextUnformatted(job->currentStage.c_str());
        ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
        if (ImGui::Button("Cancel")) (void)state->jobs->RequestCancel(job->id);
    }
    ImGui::End();
}
}
