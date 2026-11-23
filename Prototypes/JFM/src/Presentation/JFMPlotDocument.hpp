#pragma once
#include <sst/plotlab/GraphSnapshot.hpp>
#include <map>
#include <sst/ui/Plot/Editor.hpp>
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <set>

namespace SST::Prototypes::JFM
{
    struct JFMMonteCarloScatterStatistics;
    struct JFMMonteCarloScatterPlotOptions;
    // Derived domain results live in engine columns; JFM project IO remains authoritative.
    class JFMPlotDocument : public std::enable_shared_from_this<JFMPlotDocument>
    {
    public:
        JFMPlotDocument();
        void BeginFrame();
        sst::plotlab::Id SetSeries(const sst::ui::plot::SeriesEntryView &, bool absoluteCurrent = false);
        void SetSpec(sst::ui::plot::PlotSpec);
        void Save(const std::filesystem::path&) const;
        void Load(const std::filesystem::path&);
        void ApplyEdits();
        sst::ui::plot::PlotEditorState editor;
        std::string currentGroup;
        sst::ui::plot::PlotPreview preview{{}, {96.f/72}};
        Ref<CommandRegistry> editCommands = CreateRef<CommandRegistry>();
        Ref<UndoStack> editUndo = CreateRef<UndoStack>();
        void EndFrame(sst::ui::plot::PlotSpec);
        std::shared_ptr<const sst::plotlab::GraphSnapshot> Snapshot() const;
        Ref<sst::plotlab::Project> engine = CreateRef<sst::plotlab::Project>("JFM");
        sst::plotlab::Project& project = *engine;
        sst::plotlab::Id graph = 0, layer = 0, workbook = 0;
        sst::ui::plot::PlotViewState view;
        std::uint64_t exportJob = 0;
        std::string exportStatus;
        std::vector<std::pair<sst::plotlab::Id, std::string>> rendered;
    private:
        std::map<std::string, sst::plotlab::Id> series;
        std::set<sst::plotlab::Id> active;
        struct InputStyle { std::string name; sst::ui::plot::SeriesStyle style; bool visible; };
        std::map<sst::plotlab::Id,InputStyle> lastInput;
        std::optional<sst::ui::plot::PlotSpec> baseSpec;
    };
    sst::ui::plot::Bounds PopulateJFMMonteCarloPlot(JFMPlotDocument &,
        const JFMMonteCarloScatterStatistics &, const JFMMonteCarloScatterPlotOptions &,
        const std::string &xLabel, const std::string &yLabel);
}
