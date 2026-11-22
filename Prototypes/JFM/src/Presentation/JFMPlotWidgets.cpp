#include "Presentation/JFMPlotWidgets.hpp"
#include "IO/JFMPlotExport.hpp"
#include "Core/ScientificRuntime/PlotExport/PlotExport.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <implot.h>
#include <implot_internal.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <map>

namespace SST::Prototypes::JFM
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    bool BeginJFMPlotFrame(JFMPlotDocument &doc)
    {
        static std::map<std::string, ImFont *> fonts;
        const auto &g = std::get<pl::Graph>(doc.project.object(doc.graph).data);
        try {
            const auto key = g.fontFamily + "\n" + g.fontFile;
            auto &font = fonts[key];
            auto *atlas = ImGui::GetIO().Fonts;
            if (!font || !atlas->Fonts.contains(font)) {
                auto resolved = ResolvePlotFont(g.fontFamily, g.fontFile);
                ImFontConfig config; config.Flags |= ImFontFlags_NoLoadError;
                font = atlas->AddFontFromFileTTF(resolved.first.c_str(), 0, &config);
                if (!font) throw std::runtime_error("Cannot load graph font");
            }
            ImGui::PushFont(font, static_cast<float>(g.fontSizePt * 96.0 / 72.0));
            doc.BeginFrame();
            return true;
        } catch (const std::exception &e) { ImGui::TextWrapped("Graph font unavailable: %s", e.what()); return false; }
    }
    void AddJFMPlotSeries(JFMPlotDocument &doc, const plot::SeriesEntryView &input, bool absoluteCurrent)
    {
        const auto id = doc.SetSeries(input, absoluteCurrent);
        auto entry = doc.project.series(id);
        entry.id = input.id; // Keep existing legend interaction IDs; references use engine IDs.
        plot::RenderSeries(entry);
    }
    void EndJFMPlot(JFMPlotDocument &doc, plot::PlotSpec spec)
    {
        doc.EndFrame(std::move(spec));
        const auto limits = ImPlot::GetPlotLimits();
        doc.view.viewport = plot::Bounds{limits.X.Min, limits.X.Max, limits.Y.Min, limits.Y.Max};
        auto *current = ImPlot::GetCurrentPlot();
        std::vector<std::pair<pl::Id, ImGuiID>> ids;
        for (const auto &[id, label] : doc.rendered) ids.emplace_back(id, ImGui::GetID(label.c_str()));
        ImPlot::EndPlot();
        auto ticks = [](const ImPlotAxis &axis, auto &out) {
            out.clear();
            for (int i = 0; i < axis.Ticker.Ticks.Size; ++i) {
                const auto &t = axis.Ticker.Ticks[i];
                if (t.PlotPos >= axis.Range.Min && t.PlotPos <= axis.Range.Max)
                    out.push_back({t.PlotPos, t.ShowLabel && t.TextOffset >= 0 ? axis.Ticker.GetText(i) : "", t.Major});
            }
        };
        ticks(current->Axes[ImAxis_X1], doc.view.xTicks); ticks(current->Axes[ImAxis_Y1], doc.view.yTicks);
        for (const auto &[id, itemId] : ids)
            if (const auto *item = current->Items.GetItem(itemId)) doc.view.visibility[std::to_string(id)] = item->Show;
    }
    void JFMPlotExportControls(JFMPlotDocument &doc)
    {
        ImGui::PushID(&doc);
        if (ImGui::Button("Export SVG/PDF/PNG...")) ImGui::OpenPopup("Publication export");
        // Stretch columns need a resolved width inside an auto-sized popup.
        ImGui::SetNextWindowSize({30 * ImGui::GetFontSize(), 0});
        if (ImGui::BeginPopup("Publication export")) {
            auto o = doc.project.object(doc.graph);
            auto &g = std::get<pl::Graph>(o.data);
            bool changed = sst::ui::widgets::Double("Width (mm)", &g.widthMm);
            changed |= sst::ui::widgets::Double("Height (mm)", &g.heightMm);
            changed |= sst::ui::widgets::Double("Font (pt)", &g.fontSizePt);
            if (sst::ui::widgets::BeginCombo("Family", g.fontFamily.c_str())) {
                for (const char *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"})
                    if (ImGui::Selectable(family, g.fontFamily == family)) { g.fontFamily = family; g.fontFile.clear(); changed = true; }
                sst::ui::widgets::EndCombo();
            }
            if (ImGui::Button("Load font (.ttf/.otf)...")) {
                try {
                    if (auto file = OpenFileDialog({"Font", "ttf,otf"})) {
                        auto resolved = ResolvePlotFont("", file->Utf8());
                        g.fontFile = resolved.first; g.fontFamily = resolved.second; changed = true;
                    }
                } catch (const std::exception &e) { ImGui::TextWrapped("%s", e.what()); }
            }
            static int dpi = 300, format = 0;
            auto &status = doc.exportStatus;
            sst::ui::widgets::Int("Resolution (dpi)", &dpi);
            sst::ui::widgets::Combo("Format", &format, "SVG\0PDF\0PNG\0");
            try {
                if (changed) doc.project.change("Publication settings", {o}).apply(doc.project);
                if (ImGui::Button("Export")) {
                    const char *formats[] = {"svg", "pdf", "png"};
                    auto destination = SaveFileDialog({"Publication figure", formats[format]}, (std::string("figure.")+formats[format]).c_str(), JFMPlotExportDirectory());
                    if (destination) {
                        auto result = ExportJFMPlot(doc.Snapshot(), destination->Utf8(), formats[format], dpi);
                        if (result) { doc.exportJob = *result; status = "Export queued"; }
                        else status = result.Error().userMessage;
                    }
                }
            } catch (const std::exception &e) { status = e.what(); }
            if (doc.exportJob) {
                const auto result = JFMPlotExportStatus(doc.exportJob);
                static constexpr const char *names[] = {"queued", "running", "completed", "failed", "cancelled"};
                status = !result ? result.Error().userMessage :
                    (result->status == JobStatus::Failed || result->status == JobStatus::Cancelled) ? result->errorMessage :
                    std::string("Export ") + names[static_cast<int>(result->status)];
            }
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}
