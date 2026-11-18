#include <gtest/gtest.h>
#include <sst/ui/Plot/Model.hpp>

TEST(PrototypePlot, EvaluationDescriptionUsesSharedBounds) {
    using namespace sst::ui::plot;
    const double voltage[] = {0, 2}, current[] = {0, 6};
    const SeriesEntryView series{"Evaluation", {voltage, current, 2}};
    PlotSpec plot; plot.autoFit = AutoFit::Always;
    const auto bounds = ResolveBounds(plot, &series, 1, {});
    ASSERT_TRUE(bounds);
    EXPECT_LT(bounds->minX, 0); EXPECT_GT(bounds->maxX, 2);
    EXPECT_LT(bounds->minY, 0); EXPECT_GT(bounds->maxY, 6);
}
