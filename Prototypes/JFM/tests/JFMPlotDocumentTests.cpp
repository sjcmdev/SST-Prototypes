#include <gtest/gtest.h>
#include "Presentation/JFMPlotDocument.hpp"
#include "IO/JFMPlotExport.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include <limits>

namespace SST::Prototypes::JFM
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    TEST(JFMPlotDocumentTests, RenameAndRefreshPreservePlotColumnAndRowIdentities)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1,2}, y[] = {-1,0,2};
        doc.BeginFrame();
        auto id = doc.SetSeries({"Measurement###curve_7", {x,y,3}});
        doc.EndFrame({});
        const auto first = std::get<pl::Plot>(doc.project.object(id).data);
        const auto rows = std::get<pl::Worksheet>(doc.project.object(first.worksheet).data).rows;
        doc.BeginFrame();
        EXPECT_EQ(id, doc.SetSeries({"Renamed###curve_7", {x,y,3}}));
        doc.EndFrame({});
        const auto second = std::get<pl::Plot>(doc.project.object(id).data);
        EXPECT_EQ(first.x, second.x); EXPECT_EQ(first.y, second.y);
        EXPECT_EQ(rows, std::get<pl::Worksheet>(doc.project.object(second.worksheet).data).rows);
        const auto revision = doc.project.revision();
        doc.BeginFrame(); doc.SetSeries({"Renamed###curve_7", {x,y,3}}); doc.EndFrame({});
        EXPECT_EQ(revision, doc.project.revision());
    }
    TEST(JFMPlotDocumentTests, SemilogRetainsJfmAbsoluteCurrentAndOmitsZero)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1,2}, y[] = {-1,0,2};
        doc.BeginFrame();
        const auto id = doc.SetSeries({"Raw###raw", {x,y,3}}, true);
        doc.EndFrame({});
        auto data = doc.project.series(id).data;
        ASSERT_EQ(data.count, 2); EXPECT_EQ(data.x[0], 0); EXPECT_EQ(data.y[0], 1); EXPECT_EQ(data.y[1], 2);
        doc.BeginFrame(); EXPECT_EQ(id, doc.SetSeries({"Raw###raw", {x,y,3}})); doc.EndFrame({});
        data = doc.project.series(id).data;
        ASSERT_EQ(data.count, 3); EXPECT_EQ(data.y[0], -1); EXPECT_EQ(data.y[1], 0);
    }
    TEST(JFMPlotDocumentTests, SnapshotOwnsFitAndErrorsAndRespectsHiddenSeries)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1}, y[] = {2,3}, e[] = {.1,.2};
        doc.BeginFrame();
        auto raw = doc.SetSeries({"Measurement###raw", {x,y,2,e}, plot::SeriesKind::ErrorBars});
        auto fit = doc.SetSeries({"Fit###fit", {x,y,2}});
        doc.EndFrame({});
        doc.view.viewport = plot::Bounds{-1,2,1,4};
        doc.view.visibility[std::to_string(fit)] = false;
        auto snapshot = doc.Snapshot();
        ASSERT_EQ(snapshot->series.size(), 1); EXPECT_EQ(snapshot->series[0].plot, raw);
        EXPECT_EQ(snapshot->series[0].yError, (std::vector<double>{.1,.2}));
        doc.BeginFrame(); doc.EndFrame({});
        EXPECT_TRUE(doc.Snapshot()->series.empty());
        EXPECT_EQ(snapshot->series[0].y, (std::vector<double>{2,3}));
    }
    TEST(JFMPlotDocumentTests, ErrorColumnCanBeRemovedDuringResize)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1}, y[] = {2,3}, e[] = {.1,.2};
        doc.BeginFrame(); const auto id = doc.SetSeries({"Data###data", {x,y,2,e}}); doc.EndFrame({});
        doc.BeginFrame(); EXPECT_EQ(id, doc.SetSeries({"Data###data", {x,y,1}})); doc.EndFrame({});
        EXPECT_EQ(doc.project.series(id).data.count, 1);
        EXPECT_EQ(doc.project.series(id).data.yError, nullptr);
    }
    TEST(JFMPlotDocumentTests, JsonSnapshotRoundTripsStableIdsAndRejectsInvalidRequests)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1}, y[] = {2,3};
        doc.BeginFrame(); auto id = doc.SetSeries({"Data###data", {x,y,2}}); doc.EndFrame({});
        nlohmann::json request = {{"operation","snapshot"},{"project",nlohmann::json::parse(doc.project.toJson())},
            {"graph_id",doc.graph},{"ranges",{-1,2,1,4}}};
        auto result = RunJFMPlotRequest(request);
        ASSERT_EQ(result.at("ok"), true) << result.dump();
        EXPECT_EQ(result.at("snapshot").at("series").at(0).at("plot_id"), id);
        EXPECT_EQ(result.at("snapshot").at("font_family"), "DejaVu Sans");
        request["ranges"] = {0,0,1,4};
        result = RunJFMPlotRequest(request);
        EXPECT_EQ(result.at("ok"), false); EXPECT_EQ(result.at("error").at("code"), "jfm.plot.validation");
    }
    TEST(JFMPlotDocumentTests, MonteCarloHeadlessFigureRetainsSigmaClassesAndVisibility)
    {
        JFMPlotDocument doc;
        JFMMonteCarloScatterStatistics stats;
        stats.points = {{1,2,0,0,0},{2,3,0,0,1},{3,4,0,0,2}};
        JFMMonteCarloScatterPlotOptions options;
        options.showSigmaClasses[1] = false;
        const auto bounds = PopulateJFMMonteCarloPlot(doc, stats, options, "A", "Rs");
        doc.view.viewport = bounds;
        const auto snapshot = doc.Snapshot();
        ASSERT_EQ(snapshot->series.size(), 2);
        EXPECT_EQ(snapshot->series[0].label, "2-3 sigma");
        EXPECT_EQ(snapshot->series[1].label, "<= 1 sigma");
        EXPECT_LT(bounds.minX, 1); EXPECT_GT(bounds.maxY, 4);
        auto id = snapshot->series[0].plot;
        PopulateJFMMonteCarloPlot(doc, stats, options, "A", "Rs");
        EXPECT_EQ(doc.Snapshot()->series[0].plot, id);
    }

}
