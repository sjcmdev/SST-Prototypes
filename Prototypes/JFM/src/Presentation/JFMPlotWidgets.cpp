#include "Presentation/JFMPlotWidgets.hpp"
#include "IO/JFMPlotExport.hpp"
#include "Prototypes/PlotLab/src/PlotLabExport.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <imgui.h>
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
                auto resolved = SST::Prototypes::PlotLab::ResolveFont(g.fontFamily, g.fontFile, SST_PLOTLAB_PYTHON_ROOT);
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
        if (ImGui::BeginPopup("Publication export")) {
            auto o = doc.project.object(doc.graph);
            auto &g = std::get<pl::Graph>(o.data);
            bool changed = ImGui::InputDouble("Width (mm)", &g.widthMm);
            changed |= ImGui::InputDouble("Height (mm)", &g.heightMm);
            changed |= ImGui::InputDouble("Font (pt)", &g.fontSizePt);
            if (ImGui::BeginCombo("Family", g.fontFamily.c_str())) {
                for (const char *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"})
                    if (ImGui::Selectable(family, g.fontFamily == family)) { g.fontFamily = family; g.fontFile.clear(); changed = true; }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Load font (.ttf/.otf)...")) {
                try {
                    if (auto file = OpenFileDialog({"Font", "ttf,otf"})) {
                        auto resolved = SST::Prototypes::PlotLab::ResolveFont("", file->Utf8(), SST_PLOTLAB_PYTHON_ROOT);
                        g.fontFile = resolved.first; g.fontFamily = resolved.second; changed = true;
                    }
                } catch (const std::exception &e) { ImGui::TextWrapped("%s", e.what()); }
            }
            static int dpi = 300, format = 0;
            auto &status = doc.exportStatus;
            ImGui::InputInt("DPI", &dpi);
            ImGui::Combo("Format", &format, "SVG\0PDF\0PNG\0");
            try {
                if (changed) doc.project.change("Publication settings", {o}).apply(doc.project);
                if (ImGui::Button("Export")) {
                    const char *formats[] = {"svg", "pdf", "png"};
                    auto destination = SaveFileDialog({"Publication figure", formats[format]}, (std::string("figure.")+formats[format]).c_str());
                    if (destination) {
                        auto result = ExportJFMPlot(*doc.Snapshot(), destination->Utf8(), formats[format], dpi);
                        if (result.at("ok").get<bool>()) { doc.exportJob = result.at("job_id"); status = "Export queued"; }
                        else status = result.at("error").at("message").get<std::string>();
                    }
                }
            } catch (const std::exception &e) { status = e.what(); }
            if (doc.exportJob) {
                const auto result = RunJFMPlotRequest({{"operation", "status"}, {"job_id", doc.exportJob}});
                status = result.at("ok").get<bool>() ? "Export " + result.at("status").get<std::string>()
                    : result.at("error").at("message").get<std::string>();
            }
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}
