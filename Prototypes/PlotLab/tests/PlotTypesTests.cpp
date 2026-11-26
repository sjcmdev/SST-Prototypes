#include "PlotLabExport.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <sst/plotlab/PlotEditing.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <implot_internal.h>
#include <cmath>
#include <cstdlib>
using namespace SST::Prototypes::PlotLab;
TEST(PlotTypes, TypedImageAndDataReachSharedPythonFileEdge) {
    static SST::PythonInterpreter interpreter;
    sst::plotlab::Project p;sst::plotlab::ImageGrid image;image.columns=3;image.rows=2;image.pixels={1,2,3,4,5,6};image.xUnit="um";image.yUnit="K";image.zUnit="counts";image.spec.contours.levels={2.5};
    auto op=sst::plotlab::createImageMap(p,p.root(),std::move(image));op.apply(p);
    sst::plotlab::Id graph=0,sheet=0;
    for(const auto& [id,o]:p.objects()) {if(std::holds_alternative<sst::plotlab::Graph>(o->data)) graph=id;if(std::holds_alternative<sst::plotlab::Worksheet>(o->data)) sheet=id;}
    sst::ui::plot::PlotViewState view;view.viewport=sst::ui::plot::Bounds{-.5,2.5,-.5,1.5};auto snapshot=sst::plotlab::snapshotGraph(p,graph,view);
    const auto artifact=std::getenv("PLOTGAP_ARTIFACT_DIR");
    const auto root=artifact?std::filesystem::path(artifact):std::filesystem::temp_directory_path()/"plotgap-app-edges";std::filesystem::create_directories(root);
    if(artifact) {std::ofstream out(root/"snapshot.json",std::ios::binary);out<<SnapshotJson(*snapshot);ASSERT_TRUE(out.good());}
    ASSERT_NO_THROW(ExportGraph(*snapshot,root/"map.jpg","jpeg",100,SST_PLOTLAB_PYTHON_ROOT,{.2,.4,.6}));
    auto data=sst::plotlab::snapshotData(p,{sheet,p.children(sheet),{5,0},sst::plotlab::DataMaskPolicy::Preserve,sst::plotlab::DataMissingPolicy::Empty});
    ASSERT_NO_THROW(ExportData(*data,root/"data.tsv","tsv",SST_PLOTLAB_PYTHON_ROOT));
    std::ifstream stream(root/"data.tsv");std::string names,units,first;std::getline(stream,names);std::getline(stream,units);std::getline(stream,first);
    EXPECT_EQ(units,"um\t\tK\t\tcounts\t");EXPECT_EQ(first,"2.0\t0\t1.0\t0\t6.0\t0");
    EXPECT_THROW(ExportData(*data,root/"data.tsv","tsv",SST_PLOTLAB_PYTHON_ROOT,[]{return true;}),std::runtime_error);
    stream.close();
    if(!artifact) std::filesystem::remove_all(root);
}
TEST(PlotTypes, LogMapSharesContourGeometryAndCachesBetweenFrames) {
    namespace plot=sst::ui::plot;
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,800};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    double x[]={0,1,2,0,1,2},y[]={0,0,0,1,1,1},z[]={1,2,3,3,4,5};
    plot::SeriesEntryView entry{"map",{x,y,6,nullptr,z,3},plot::SeriesKind::Heatmap};
    plot::PlotSpec spec;spec.customAxes=true;spec.autoFit=plot::AutoFit::Once;spec.z.scale=plot::AxisScale::Log10;spec.z.unit="counts";spec.contours.levels={2.5,3.5};
    plot::PlotViewState view;std::shared_ptr<const plot::ContourGeometry> first;
    for(int frame=0;frame<3;++frame) {
        if(frame==1) spec.contours.color={1,0,0,1};
        if(frame==2) {z[0]=1.5;view.dataChanged=true;}
        ImGui::NewFrame();ImGui::SetNextWindowSize({900,750});ImGui::Begin("map-test");
        EXPECT_NO_THROW(plot::RenderPlot("map",spec,&entry,1,view,{},ImVec2(600,400)));
        ASSERT_TRUE(view.contourGeometry);EXPECT_FALSE(view.contourGeometry->empty());
        if(frame==0) first=view.contourGeometry;
        if(frame==1) EXPECT_EQ(first.get(),view.contourGeometry.get());
        if(frame==2) EXPECT_NE(first.get(),view.contourGeometry.get());
        for(const auto& vertex:ImGui::GetWindowDrawList()->VtxBuffer) {EXPECT_TRUE(std::isfinite(vertex.pos.x));EXPECT_TRUE(std::isfinite(vertex.pos.y));}
        EXPECT_TRUE(view.frame);EXPECT_TRUE(view.viewport);EXPECT_FALSE(view.dataChanged);
        ImGui::End();ImGui::Render();
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
