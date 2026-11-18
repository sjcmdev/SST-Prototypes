#pragma once
#include <sst/plotlab/GraphSnapshot.hpp>
#include <map>
#include <set>

namespace SST::Prototypes::JFM
{
    struct JFMMonteCarloScatterStatistics;
    struct JFMMonteCarloScatterPlotOptions;
    // Derived domain results live in engine columns; JFM project IO remains authoritative.
    class JFMPlotDocument
    {
    public:
        JFMPlotDocument();
        void BeginFrame();
        sst::plotlab::Id SetSeries(const sst::ui::plot::SeriesEntryView &, bool absoluteCurrent = false);
        void EndFrame(sst::ui::plot::PlotSpec);
        std::shared_ptr<const sst::plotlab::GraphSnapshot> Snapshot() const;
        sst::plotlab::Project project{"JFM"};
        sst::plotlab::Id graph = 0, layer = 0, workbook = 0;
        sst::ui::plot::PlotViewState view;
        std::uint64_t exportJob = 0;
        std::string exportStatus;
        std::vector<std::pair<sst::plotlab::Id, std::string>> rendered;
    private:
        std::map<std::string, sst::plotlab::Id> series;
        std::set<sst::plotlab::Id> active;
    };
    sst::ui::plot::Bounds PopulateJFMMonteCarloPlot(JFMPlotDocument &,
        const JFMMonteCarloScatterStatistics &, const JFMMonteCarloScatterPlotOptions &,
        const std::string &xLabel, const std::string &yLabel);
}
