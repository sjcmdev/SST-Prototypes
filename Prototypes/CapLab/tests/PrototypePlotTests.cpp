#include <gtest/gtest.h>
#include "TestProject.hpp"
#include "Presentation/SelectionLabel.hpp"
#include <sst/plotlab/GraphSnapshot.hpp>
TEST(CapLabPlot, EngineSnapshotHasUnitsAndOwnsData) {
    using namespace SST::Prototypes::CapLab;
    auto p=FixtureProject();
    const auto layer=p.engine->children(p.graph).at(0);
    const auto &spec=std::get<pl::Layer>(p.engine->object(layer).data).spec;
    const auto series=p.engine->series(p.engine->children(layer).at(0));
    sst::ui::plot::PlotViewState view;
    view.viewport=sst::ui::plot::ResolveBounds(spec,&series,1,view);
    ASSERT_TRUE(view.viewport);
    const auto snapshot=pl::snapshotGraph(*p.engine,p.graph,view);
    EXPECT_EQ(snapshot->spec.x.label,"Frequency (Hz)");
    EXPECT_EQ(snapshot->spec.y.label,"Capacitance (F)");
    EXPECT_EQ(snapshot->spec.x.scale,sst::ui::plot::AxisScale::Log10);
    ASSERT_EQ(snapshot->series.size(),1); EXPECT_EQ(snapshot->series[0].x.size(),40);
    EXPECT_EQ(snapshot->series[0].label,"cf_100K_7, T = 100.0 K");
    p=Project{};
    EXPECT_DOUBLE_EQ(snapshot->series[0].y[0],8.8601e-10);
}

TEST(CapLabPlot, SelectionLabelsUseNamesAndEmptyHints) {
    using namespace SST::Prototypes::CapLab;
    auto p=FixtureProject();
    EXPECT_EQ(SelectionLabel(p,p.samples.front().id),"cf_100K_7");
    EXPECT_EQ(SelectionLabel(p,p.sweeps.front().id),"cf_100K_7, T = 100.0 K");
    p.samples.front().name="Renamed sample";
    EXPECT_EQ(SelectionLabel(p,p.sweeps.front().id),"cf_100K_7, T = 100.0 K");
    const auto measurement=p.engine->reserveId(), defect=p.engine->reserveId();
    p.measurements.push_back({{"id",measurement},{"metadata",{{"source","lab_cv.dat"},{"temperature_K",300.0}}}});
    p.defects.push_back({{"id",defect},{"name","E1"}});
    EXPECT_EQ(SelectionLabel(p,measurement),"lab_cv, T = 300.0 K");
    p.measurements[0]["metadata"]["temperature_K"]=nullptr;
    EXPECT_EQ(SelectionLabel(p,measurement),"lab_cv");
    EXPECT_EQ(SelectionLabel(p,defect),"E1");
    EXPECT_EQ(SelectionLabel(p,0),"none — select");
    EXPECT_EQ(SelectionLabel(p,0,"no defects yet"),"no defects yet");
    EXPECT_EQ(SelectionLabel(p,p.engine->reserveId()),"none — select");
}
