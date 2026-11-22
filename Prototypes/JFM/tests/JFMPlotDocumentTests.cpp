#include <gtest/gtest.h>
#include "Presentation/JFMPlotDocument.hpp"
#include "IO/JFMPlotExport.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include <limits>
#include <algorithm>
#include "Presentation/Panels/MonteCarloCurvesPanel.hpp"
#include "Vendor/implot/implot.h"
#include "Vendor/implot/implot_internal.h"

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
    TEST(JFMPlotDocumentTests, TypedSnapshotRetainsStableIdsAndRejectsInvalidRanges)
    {
        JFMPlotDocument doc;
        const double x[] = {0,1}, y[] = {2,3};
        doc.BeginFrame(); auto id = doc.SetSeries({"Data###data", {x,y,2}}); doc.EndFrame({});
        doc.view.viewport = plot::Bounds{-1,2,1,4};
        const auto snapshot = doc.Snapshot();
        ASSERT_EQ(snapshot->series.size(), 1);
        EXPECT_EQ(snapshot->series[0].plot, id);
        EXPECT_EQ(snapshot->fontFamily, "DejaVu Sans");
        const auto invalidSize = ExportJFMPlot(snapshot, "figure.png", "png", 300, JFMPlotSize{0, 60});
        ASSERT_FALSE(invalidSize);
        EXPECT_EQ(invalidSize.Error().code, "jfm.plot.validation");
        doc.view.viewport = plot::Bounds{0,0,1,4};
        EXPECT_THROW(doc.Snapshot(), std::exception);
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

    TEST(JFMPlotDocumentTests, OutsideThreeSigmaAndZeroVarianceKeepOwnClasses)
    {
        JFMPlotDocument doc; JFMMonteCarloScatterStatistics stats;
        stats.points = {{1,2,0,30,3},{2,3,0,0,4}};
        JFMMonteCarloScatterPlotOptions options;
        doc.view.viewport = PopulateJFMMonteCarloPlot(doc,stats,options,"I0","I_ph");
        auto snapshot = doc.Snapshot(); ASSERT_EQ(snapshot->series.size(),2);
        EXPECT_EQ(snapshot->series[0].label,"Unclassified (zero variance)");
        EXPECT_EQ(snapshot->series[1].label,">3 sigma");
        EXPECT_STREQ(JFMSigmaTsvLabels[3],"gt_3_sigma");
        EXPECT_EQ(snapshot->series[1].x.size(),1);
    }

    TEST(JFMPlotDocumentTests, CurvesHideRejectedSamplesByDefault)
    {
        auto store = CreateRef<JFMWorkspaceStore>(); auto bus = CreateRef<EventBus>();
        auto project = store->GetActiveProjectHandle();
        JFMCharacteristic curve; curve.info.name = "MC regression"; curve.info.temperatureK = 300.;
        curve.data.voltage = {.2,.3}; curve.data.current = {1e-5,2e-5};
        const auto id = store->AddCharacteristics(project,{curve}).front();
        JFMCharacteristicRef ref{project,id};
        ASSERT_TRUE(store->SetPrefitModel(project,{id},"SD-0"));
        JFMPrefitState pre; pre.modelId = "SD-0";
        ASSERT_TRUE(store->ApplyPrefitResult(ref,store->BeginPrefit(ref),pre,{}));
        JFMFitState fit; fit.modelId = "SD-0";
        ASSERT_TRUE(store->ApplyFitResult(ref,store->BeginFit(ref),fit,{}));
        JFMMonteCarloState state; state.modelId = "SD-0";
        JFMMonteCarloResult result; result.parameterNames = {"I0","A"};
        result.voltage = {.2,.3}; result.idealCurrent = {1e-5,2e-5};
        result.noisyCurrent = {1e-5,2e-5,1e-5,2e-5};
        result.samples = {{{1e-10,1.5},0,0,1,true},{{1e-9,1.5},0,0,1,false}};
        const auto shared=CreateRef<const JFMMonteCarloResult>(std::move(result));
        ASSERT_TRUE(store->ApplyMonteCarloResult(ref,store->BeginMonteCarlo(ref),state,shared));
        EXPECT_EQ(store->GetMonteCarloResult(ref),shared);
        auto *gui = ImGui::CreateContext(); auto *plot = ImPlot::CreateContext();
        auto &io = ImGui::GetIO(); io.DisplaySize = {1200,800}; io.DeltaTime = 1.f/60;
        io.IniFilename = nullptr;
        unsigned char *pixels; int width,height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        {
            MonteCarloCurvesPanel panel(store,bus,"MC regression",true);
            bus->PublishNew<MonteCarloRequested>(project,std::vector<CharacteristicId>{id});
            ImGui::NewFrame(); ImGui::SetNextWindowSize({1100,700}); panel.Render(); ImGui::Render();
            ASSERT_EQ(plot->Plots.GetBufSize(),1);
            // Ideal + accepted noisy + accepted fitted; rejected curves create no items.
            EXPECT_EQ(plot->Plots.GetByIndex(0)->Items.GetItemCount(),3);
        }
        ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
    }

    TEST(JFMPlotDocumentTests, PicoampScatterKeepsPhysicalSpanAndPublicationTicks)
    {
        JFMPlotDocument doc; JFMMonteCarloScatterStatistics stats;
        stats.points = {{1.7,3.1e-12,0,0,0},{1.8,3.8e-12,0,30,3},{1.75,3.4e-12,0,5,1}};
        JFMMonteCarloScatterPlotOptions options;
        const auto b = PopulateJFMMonteCarloPlot(doc, stats, options, "A [1]", "I0 [A]");
        EXPECT_LT(b.minY, 3.1e-12); EXPECT_GT(b.maxY, 3.8e-12);
        EXPECT_LT(b.maxY-b.minY, 1e-12);
        doc.view.viewport = b;
        const auto snapshot = doc.Snapshot();
        EXPECT_EQ(snapshot->spec.y.label, "I0 [A]");
        EXPECT_TRUE(snapshot->spec.legend.outside);
        ASSERT_FALSE(snapshot->xTicks.empty()); ASSERT_FALSE(snapshot->yTicks.empty());
        EXPECT_TRUE(std::any_of(snapshot->yTicks.begin(), snapshot->yTicks.end(), [](const auto &t) { return !t.label.empty(); }));
        ASSERT_EQ(snapshot->series.size(), 3);
        EXPECT_EQ(snapshot->series.front().label, ">3 sigma");
        EXPECT_EQ(snapshot->series.back().label, "<= 1 sigma");
        for (const auto &series : snapshot->series) {
            EXPECT_LE(series.style.markerSizePt, 1.5);
            EXPECT_EQ(series.style.color, series.style.markerFill);
            EXPECT_EQ(series.style.color, series.style.markerOutline);
        }
        // Previously rendered ticks and user zoom must survive export unchanged.
        doc.view.yTicks = {{3.3e-12,"custom",true}};
        EXPECT_EQ(doc.Snapshot()->yTicks.front().label, "custom");
    }
    TEST(JFMPlotDocumentTests, ConstantScatterAxisRemainsExportableWithZeroPadding)
    {
        JFMPlotDocument doc; JFMMonteCarloScatterStatistics stats;
        stats.points = {{1,3e-12,0,0,0},{1,3e-12,0,0,0},{1,3e-12,0,0,0}};
        JFMMonteCarloScatterPlotOptions options; options.zoomOut = 0;
        doc.view.viewport = PopulateJFMMonteCarloPlot(doc,stats,options,"A [1]","I0 [A]");
        EXPECT_NO_THROW(doc.Snapshot());
        EXPECT_GT(doc.view.viewport->minY, 0);
        EXPECT_LT(doc.view.viewport->maxY-doc.view.viewport->minY, 1e-12);
    }

    TEST(JFMPlotDocumentTests, ExportDialogDefaultIsIndependentOfTheImportAndWorkingDirectory)
    {
        const auto path = JFMPlotExportDirectory();
        EXPECT_TRUE(path.Native().is_absolute());
        EXPECT_TRUE(FileSystem::Exists(path.Native()));
        auto current = path.Native();
        while (!current.empty()) {
            EXPECT_FALSE(FileSystem::Exists(current/"Prototypes"/"JFM"/"src"));
            const auto parent = current.parent_path(); if (parent == current) break; current = parent;
        }
    }

}
