#include <gtest/gtest.h>
#include "CurveMorph.hpp"
#include "PlotUi.hpp"

using namespace SST::ScapsLab;
TEST(CurveMorph, CommonGridMonotoneFramesAndRetargetWithoutTeleport) {
    CurveMorph c;c.Update({0,.5,1},{0,0,0},0);
    c.Update({0,1},{1,2},.5,false,true);EXPECT_LT(c.y[1],.15);
    double previous=c.y[1],maxStep=c.y[1];
    for(int frame=0;frame<10;++frame){c.Update({0,1},{1,2},.01,false,true);EXPECT_EQ(c.x,(std::vector<double>{0,.5,1}));EXPECT_GE(c.y[1],previous);maxStep=std::max(maxStep,c.y[1]-previous);previous=c.y[1];}
    EXPECT_NEAR(c.y[1],1.5,1e-12);EXPECT_LT(maxStep,1.5*.15);
    const auto drawn=c.y;c.Update({0,1},{2,3},0,false,true);EXPECT_EQ(c.y,drawn);
    c.Update({0,1},{2,3},.1,false,true);EXPECT_NEAR(c.y[1],2.5,1e-12);
}
TEST(CurveMorph, DescendingAndDuplicateCoordinatesLogAndInvalidValues) {
    EXPECT_DOUBLE_EQ(CurveMorph::Sample({1,.5,.5,0},{100,10,10,1},.5,true),10);
    CurveMorph junction;junction.Update({0,.5,.5,1},{0,1,2,3},0);junction.Update({0,.5,.5,1},{1,2,3,4},0);junction.Update({0,.5,.5,1},{1,2,3,4},.1);EXPECT_EQ(junction.y,(std::vector<double>{1,2,3,4}));
    CurveMorph c;c.Update({1,0},{1,1},0,true);c.Update({1,0},{100,100},0,true);c.Update({1,0},{100,100},.05,true);EXPECT_NEAR(c.y[0],10,1e-12);
    c.Update({1,0},{NAN,100},0,true);const auto y=c.y;c.Update({1,0},{NAN,100},.05,true);EXPECT_GT(c.elapsed,0);
    EXPECT_EQ(c.y[0],y[0]);c.Update({1,0},{NAN,100},.05,true);EXPECT_TRUE(std::isnan(c.y[0]));
    c.Update({}, {},.1);EXPECT_EQ(c.x.size(),2u);
}
TEST(CurveMorph, QuantityStylesRemainDistinctFromTerminalAccent) {
    const auto blue=XColour("Ec_eV"),red=XColour("Ev_eV");EXPECT_GT(blue.z,blue.x);EXPECT_GT(red.x,red.z);
    EXPECT_EQ(XColour("n_m3").z,blue.z);EXPECT_EQ(XColour("p_m3").x,red.x);
    EXPECT_TRUE(DashedQuantity("EFn_eV"));EXPECT_TRUE(DashedQuantity("EFp_eV"));EXPECT_FALSE(DashedQuantity("Ec_eV"));
    EXPECT_NE(XColour("G").y,XColour("srh_bulk").y);EXPECT_NE(XColour("srh_bulk").z,XColour("interface").z);
    EXPECT_GT(LiveCurveColour.x,LiveCurveColour.z);EXPECT_EQ(SingleShotColour.w,1);
}
