#include "Presentation/JFMPlotWidgets.hpp"
#include "Core/PlotExport/PlotEditing.hpp"
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
    bool BeginJFMPlotFrame(JFMPlotDocument &doc, ImVec2 available)
    {
        try {
            if (available.x<=0 || available.y<=0) available=ImGui::GetContentRegionAvail();
            doc.preview=plot::InteractivePreview(available);
            ImGui::PushFont(nullptr, 0);
            doc.BeginFrame();return true;
        } catch (const std::exception &e) { ImGui::TextWrapped("Graph font unavailable: %s", e.what()); return false; }
    }
    void AddJFMPlotSeries(JFMPlotDocument &doc, const plot::SeriesEntryView &input, bool positiveCurrentOnly)
    {
        auto source=input;source.group=doc.currentGroup;
        const auto id = doc.SetSeries(source, positiveCurrentOnly);
        (void)id; // Draw once in persisted z-order after all domain series have been refreshed.
    }
    void SetupJFMPlotAxes(JFMPlotDocument &doc, bool seriesReady)
    {
        const auto& spec = std::get<pl::Layer>(doc.project.object(doc.layer).data).spec;
        if(spec.autoFit==plot::AutoFit::Always || spec.x.rescale==plot::RescaleMode::Auto || spec.y.rescale==plot::RescaleMode::Auto) {
            auto fit=spec;fit.autoFit=plot::AutoFit::Always;fit.bounds.reset();
            std::vector<plot::SeriesEntryView> entries;
            if (seriesReady) for (const auto& [id,name] : doc.rendered) entries.push_back(doc.project.series(id));
            else for (auto id : doc.project.children(doc.layer)) entries.push_back(doc.project.series(id));
            plot::PlotViewState fitView; fitView.visibility = doc.view.visibility;
            if (!seriesReady) fitView.excluded = doc.view.excluded;
            if(const auto bounds=plot::ResolveBounds(fit,entries.data(),entries.size(),fitView)) {
                if(spec.autoFit==plot::AutoFit::Always || spec.x.rescale==plot::RescaleMode::Auto) ImPlot::SetupAxisLimits(ImAxis_X1,bounds->minX,bounds->maxX,ImPlotCond_Always);
                if(spec.autoFit==plot::AutoFit::Always || spec.y.rescale==plot::RescaleMode::Auto) ImPlot::SetupAxisLimits(ImAxis_Y1,bounds->minY,bounds->maxY,ImPlotCond_Always);
            }
        }
        plot::SetupPlotEditorAxes(spec, doc.view, doc.preview.metrics, &doc.editor);
    }
    void EndJFMPlot(JFMPlotDocument &doc, plot::PlotSpec requested, const char* plotId)
    {
        const auto id = std::string(plotId);
        const auto spec = std::get<pl::Layer>(doc.project.object(doc.layer).data).spec;
        std::vector<plot::SeriesEntryView> entries;
        for (auto key : doc.project.children(doc.layer)) {
            if (std::none_of(doc.rendered.begin(),doc.rendered.end(),[&](const auto& e) {return e.first==key;})) continue;
            auto entry = doc.project.series(key);
            entry.id = doc.project.object(key).name + "###" + std::to_string(key);
            entries.push_back(entry);
        }
        plot::PaintPlotEditorAxes(spec, doc.view, true, doc.preview.metrics);
        for (auto entry : entries) {
            if (std::find(doc.editor.selected.begin(),doc.editor.selected.end(),plot::SeriesKey(entry.id))!=doc.editor.selected.end())
                entry.style.lineWidthPt=(entry.style.lineWidthPt<0 ? 1 : entry.style.lineWidthPt)+1.5f;
            plot::RenderSeries(entry, doc.preview.metrics, &spec);
        }
        plot::PaintPlotEditorAxes(spec, doc.view, false, doc.preview.metrics);
        const auto* currentPlot = ImPlot::GetCurrentPlot();
        doc.autoFitInterrupted |= currentPlot->Held || currentPlot->Selecting
            || (currentPlot->Hovered && ImGui::GetIO().MouseWheel != 0);
        ImPlot::EndPlot();
        doc.EndFrame(std::move(requested));
        plot::RenderPlotEditor(id.c_str(), spec, entries.data(), entries.size(), doc.view, doc.editor, doc.preview.metrics);
        doc.ApplyEdits();
    }
    void JFMPlotExportControls(JFMPlotDocument &doc)
    {
        ImGui::PushID(&doc);
        if (ImGui::Button("Undo plot")) (void)doc.editUndo->Undo();
        ImGui::SameLine(); if (ImGui::Button("Redo plot")) (void)doc.editUndo->Redo();
        ImGui::SameLine(); if (ImGui::Button("Save figure...")) {
            try { if (auto file=SaveFileDialog({"JFM figure", "plotlab"}, "figure.plotlab", JFMPlotExportDirectory())) doc.Save(file->Native()); }
            catch (const std::exception& e) { doc.exportStatus=e.what(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Open figure...")) {
            try { if (auto file=OpenFileDialog({"JFM figure", "plotlab"})) doc.Load(file->Native()); }
            catch (const std::exception& e) { doc.exportStatus=e.what(); }
        }
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
