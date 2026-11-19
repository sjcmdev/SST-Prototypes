#include <gtest/gtest.h>
#include "Presentation/CircuitDisplay.hpp"
#include "Presentation/CapLabISPlot.hpp"
#include <limits>
#include "Presentation/SelectionLabel.hpp"

namespace SST::Prototypes::CapLab {
TEST(CapLabISDisplay, ConversionAnnotationIsNotAMeasurementPath) {
    Project p;
    p.measurements.push_back({{"id",123u},{"metadata",{{"source","D:/data/cf_100K_7.dat (Cp/Rp -> C/G)"},{"temperature_K",100}}}});
    EXPECT_EQ(SelectionLabel(p,123),"cf_100K_7, T = 100 K");
}
TEST(CapLabISDisplay, EngineeringRoundTripAndExplicitCPEExponent) {
    EXPECT_EQ(Engineering(2.2e-15,"F"),"2.2 fF");
    EXPECT_EQ(Engineering(.85,"1"),"0.85 1");
    EXPECT_EQ(Engineering(134000,"Ω"),"134 kΩ");
    EXPECT_EQ(Engineering(2.2e-9,"F"),"2.2 nF");
    EXPECT_EQ(Engineering(1.5e-6,"s"),"1.5 µs");
    for (double value : {0.,-134000.,1e-18,2.2e-9,1.5e-6,.999999,999.999,1000.,1e20}) {
        const auto text=Engineering(value,"Ω");
        EXPECT_NEAR(ParseEngineering(text,"Ω"),value,std::max(std::abs(value)*1e-11,1e-30)) << text;
    }
    EXPECT_DOUBLE_EQ(ParseEngineering("1.5 us","s"),1.5e-6);
    EXPECT_THROW(ParseEngineering("2 kV","Ω"),std::invalid_argument);
    EXPECT_THROW(ParseEngineering("2 nF extra","F"),std::invalid_argument);
    EXPECT_THROW(Engineering(std::numeric_limits<double>::infinity(),"F"),std::invalid_argument);
    ccui::Element e; e.kind=ccui::Kind::CPE; e.secondary=.85;
    EXPECT_EQ(ElementUnit(e),"S·s^0.85");
    EXPECT_NEAR(ParseEngineering(Engineering(2.2e-9,ElementUnit(e)),ElementUnit(e)),2.2e-9,1e-20);
}
TEST(CapLabISDisplay, RenderedNyquistEqualScaleOnResizeAndWheelZoom) {
    const auto oldGui=ImGui::GetCurrentContext();
    const auto oldPlot=ImPlot::GetCurrentContext();
    auto* gui=ImGui::CreateContext();
    auto* plot=ImPlot::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1600,1600}; io.DeltaTime=1.f/60;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    sst::ui::plot::PlotSpec spec; spec.equalAspect=true; spec.autoFit=sst::ui::plot::AutoFit::Once;
    spec.x.label="Re Z [Ohm]"; spec.y.label="-Im Z [Ohm]";
    const double x[]{0,500,1000}, y[]{0,500,0};
    sst::ui::plot::SeriesEntryView series; series.id="measured"; series.data={x,y,3};
    sst::ui::plot::PlotViewState state;
    ImVec2 hover{};
    double initialRange=0;
    for (int frame=0;frame<9;++frame) {
        const ImVec2 size=frame<3 ? ImVec2(1100,400) : frame<6 ? ImVec2(450,1100) : ImVec2(700,700);
        io.AddMousePosEvent(hover.x,hover.y);
        if (frame==2 || frame==5 || frame==8) io.AddMouseWheelEvent(0,1);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1500,1500});
        ImGui::Begin("Nyquist test");
        const auto scale=RenderISPlot("Nyquist",spec,&series,1,state,size);
        EXPECT_GT(scale.xPixelsPerOhm,0);
        EXPECT_NEAR(scale.xPixelsPerOhm,scale.yPixelsPerOhm,scale.xPixelsPerOhm*1e-6);
        const auto* rendered=ImPlot::GetPlot("Nyquist");
        EXPECT_FALSE(rendered->Axes[ImAxis_Y1].IsInverted());
        char label[32];
        rendered->Axes[ImAxis_X1].Formatter(65000,label,sizeof(label),nullptr);
        EXPECT_EQ(std::string(label),"65 k");
        hover=rendered->PlotRect.GetCenter();
        if (frame==1) {
            initialRange=rendered->Axes[ImAxis_X1].Range.Size();
            for (int i=0;i<3;++i) {
                EXPECT_GT(x[i],rendered->Axes[ImAxis_X1].Range.Min);
                EXPECT_LT(x[i],rendered->Axes[ImAxis_X1].Range.Max);
                EXPECT_GT(y[i],rendered->Axes[ImAxis_Y1].Range.Min);
                EXPECT_LT(y[i],rendered->Axes[ImAxis_Y1].Range.Max);
            }
        }
        if (frame==2) EXPECT_LT(rendered->Axes[ImAxis_X1].Range.Size(),initialRange);
        ImGui::End(); ImGui::Render();
    }
    ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
    ImGui::SetCurrentContext(oldGui); ImPlot::SetCurrentContext(oldPlot);
}
}
