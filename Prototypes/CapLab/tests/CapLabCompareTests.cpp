#include <gtest/gtest.h>
#include "Domain/CapLabCompare.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "Domain/CapLabProjectTree.hpp"
#include "CapLabCommands.hpp"
#include "TestProject.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <implot3d.h>
#include <cstdlib>

namespace SST::Prototypes::CapLab {
static Json PlotSpec() {
    return {{"name","C-V"},{"x_label","Voltage (V)"},{"y_label","Capacitance (F)"},
        {"curves",Json::array({{{"name","CV"},{"x",{0.,1.}},{"y",{1e-9,2e-9}}}})}};
}
TEST(CapLabCompare, ImportNeverAppendsFrequencyToVoltageGraph) {
    auto p=FixtureProject(); const auto cf=p.graph;
    p.graph=AnalysisPlot(p,p.samples.front().id,PlotSpec()); const auto cv=p.graph;
    const auto layer=p.engine->children(cv).at(0);
    const auto before=p.engine->children(layer);
    ImportChange change(p,FixturePreview(),200,CFTemplate{});
    EXPECT_EQ(p.graph,cf); EXPECT_EQ(p.engine->children(layer),before);
    change.Undo(); EXPECT_EQ(p.graph,cv); change.Redo(); EXPECT_EQ(p.graph,cf);
    EXPECT_EQ(p.engine->children(p.engine->children(cf).at(0)).size(),2);
    Validate(p);
}
TEST(CapLabCompare, BundleImportCreatesFrequencyGraphWhenOnlyVoltageGraphExists) {
    Project p;
    const auto sample=p.engine->reserveId();
    p.engine->change("Sample",{{sample,p.engine->root(),"sample",0,pl::Workbook{}}}).apply(*p.engine);
    p.samples.push_back({sample,"sample",{}});
    p.graph=AnalysisPlot(p,sample,PlotSpec()); const auto cv=p.graph;
    Json measurement{{"kind","FrequencySweep"},{"variant","Cp_Rp"},{"profile","lab_as_cp_rp"},
        {"names",{"f","C","Rp"}},{"units",{"Hz","F","Ohm"}},{"columns",{{100.,1000.},{1e-9,2e-9},{1e6,1e6}}},
        {"source_rows",{1,2}},{"temperature_K",300.},{"frequency_Hz",nullptr},{"bias_V",0.},{"source","new.dat"}};
    Json preview{{"samples",Json::array({{{"name","new"},{"area_m2",nullptr},{"metadata",Json::object()},{"measurements",Json::array({measurement})}}})},
        {"historical",Json::array()}};
    BundleChange change(p,preview);
    EXPECT_NE(p.graph,cv); EXPECT_TRUE(IsCFGraph(*p.engine,p.graph));
    EXPECT_EQ(p.engine->children(p.engine->children(cv).at(0)).size(),1);
    Validate(p);
}
TEST(CapLabCompare, ImportDoesNotModifySavedComparisonEvenWhenAxesMatch) {
    auto p=FixtureProject(); const auto imported=p.graph;
    BuildCompareGraph(p,Json{{"plot_type","C-f"},{"measurement_ids",{p.sweeps[0].id}}});
    const auto comparison=p.graph, layer=p.engine->children(comparison)[0];
    const auto plots=p.engine->children(layer);
    ImportChange added(p,FixturePreview(),200,CFTemplate{});
    EXPECT_EQ(p.graph,imported); EXPECT_EQ(p.engine->children(layer),plots);
}
TEST(CapLabCompare, StableColorsAndNumericTemperatureLegendOrder) {
    auto p=FixtureProject();
    ImportChange second(p,FixturePreview(),20,CFTemplate{});
    const auto a=p.sweeps[0].id,b=p.sweeps[1].id;
    const Json request{{"plot_type","C-f"},{"measurement_ids",{a,b}}};
    BuildCompareGraph(p,request);
    auto layer=p.engine->children(p.graph).at(0); auto plots=p.engine->children(layer);
    ASSERT_EQ(plots.size(),2);
    EXPECT_EQ(p.engine->object(plots[0]).name,p.samples[1].name+", 20 K");
    const auto color=p.engine->series(plots[0]).style.color;
    BuildCompareGraph(p,Json{{"plot_type","C-f"},{"measurement_ids",{b,a}}});
    layer=p.engine->children(p.graph).at(0); plots=p.engine->children(layer);
    EXPECT_EQ(p.engine->series(plots[0]).style.color,color);
    const auto &axes=std::get<pl::Layer>(p.engine->object(layer).data).spec;
    EXPECT_EQ(axes.x.labels.format,sst::ui::plot::LabelFormat::Power10);
    EXPECT_EQ(axes.y.labels.format,sst::ui::plot::LabelFormat::Engineering);
    EXPECT_TRUE(axes.customAxes);
    auto reopened=Parse(Serialize(p)); EXPECT_EQ(reopened.comparisons,p.comparisons);
}
TEST(CapLabCompare, ImpedancePlotsUseIndependentAnalyticValues) {
    auto p=FixtureProject();
    const auto id=p.sweeps[0].id;
    const auto f=std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(p.sweeps[0].frequency).data).values)[0];
    const auto c=std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(p.sweeps[0].capacitance).data).values)[0];
    const auto r=std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(p.sweeps[0].resistance).data).values)[0];
    for (const std::string type:{"C-f","G/omega-f","Nyquist","Bode magnitude","Bode phase"}) {
        BuildCompareGraph(p,Json{{"plot_type",type},{"measurement_ids",{id}}});
        auto layer=p.engine->children(p.graph).at(0);
        const auto data=p.engine->series(p.engine->children(layer).at(0)).data;
        const double g=1/r,b=2*std::acos(-1.)*f*c,den=g*g+b*b;
        const double expected=type=="C-f" ? c : type=="G/omega-f" ? g/(2*std::acos(-1.)*f) : type=="Nyquist" ? b/den :
            type=="Bode magnitude" ? 1/std::sqrt(den) : -std::atan2(b,g)*180/std::acos(-1.);
        EXPECT_NEAR(data.y[0],expected,std::max(1e-25,std::abs(expected)*1e-12));
    }
}
TEST(CapLabCompare, PerGraphViewsSurviveSwitchSaveAndOpen) {
    auto p=FixtureProject(); const auto first=p.graph;
    pl::ImportOptions options; options.decimal=','; options.extra["options"]=R"({"profile":"lab_as_cp_rp"})";
    p.importProfiles["lab"]=options;
    BuildCompareGraph(p,Json{{"plot_type","Nyquist"},{"measurement_ids",{p.sweeps[0].id}}}); const auto second=p.graph;
    CapLabWorkflow w(CreateRef<JobSystem>(nullptr,1)); w.Document().project=p;
    w.view.viewport=sst::ui::plot::Bounds{1,3,2,4}; w.view.fitted=true;
    const auto plot=p.engine->children(p.engine->children(second).at(0)).at(0);
    w.view.visibility[p.engine->series(plot).id]=false;
    w.ShowGraph(first); w.view.viewport=sst::ui::plot::Bounds{10,1000,1e-12,1e-8}; w.view.fitted=true;
    w.ShowGraph(second); ASSERT_TRUE(w.view.viewport); EXPECT_EQ(w.view.viewport->minX,1);
    EXPECT_FALSE(w.view.visibility.at(p.engine->series(plot).id));
    auto reopened=Parse(Serialize(w.Document().project));
    EXPECT_EQ(reopened.comparisons,p.comparisons);
    EXPECT_EQ(pl::importProfilesJson(reopened.importProfiles),pl::importProfilesJson(p.importProfiles));
    for (auto graph:{first,second}) EXPECT_TRUE(std::get<pl::Layer>(reopened.engine->object(reopened.engine->children(graph)[0]).data).view.viewport);
}
TEST(CapLabCompare, InitialFitDoesNotDirtyAnOpenedProjectButZoomDoes) {
    CapLabWorkflow w(CreateRef<JobSystem>(nullptr,1)); w.Document().project=FixtureProject();
    sst::ui::plot::PlotViewState view; view.viewport=sst::ui::plot::Bounds{10,1000,1e-12,1e-8}; view.fitted=true;
    w.StoreGraphView(w.Document().project.graph,view);
    EXPECT_FALSE(w.Document().IsDirty());
    view.viewport->minX=20;
    w.StoreGraphView(w.Document().project.graph,view);
    EXPECT_TRUE(w.Document().IsDirty());
}
TEST(CapLabCompare, StaleAndMissingInputsRemainVisibleWithoutRebinding) {
    auto p=FixtureProject();
    BuildCompareGraph(p,Json{{"plot_type","C-f"},{"measurement_ids",{p.sweeps[0].id}}});
    EXPECT_EQ(CompareStatus(p,p.comparisons[0]),"valid");
    p.sweeps[0].temperature_K=120;
    EXPECT_EQ(CompareStatus(p,p.comparisons[0]),"stale");
    RefreshComparisonLabels(p);
    const auto plot=p.engine->children(p.engine->children(p.graph)[0])[0];
    EXPECT_TRUE(p.engine->object(plot).name.ends_with(" [stale]"));
    p.sweeps.clear(); EXPECT_EQ(CompareStatus(p,p.comparisons[0]),"missing_input");
    Validate(p);
}
TEST(CapLabCompare, CommandJobPublishesOneUndoEntryAndCancellationKeepsProject) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow w(jobs,bus,undo); w.Document().project=FixtureProject(); RegisterCommands(commands,bus,w);
    const auto before=Serialize(w.Document().project);
    auto sub=bus->Subscribe<DocumentRequested>([&](const auto &e) {w.Start(e.operation,e.target);});
    w.analysisOptions={{"plot_type","C-f"},{"measurement_ids",{w.Document().project.sweeps[0].id}}};
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.plot"})); bus->ProcessQueue(); jobs->Shutdown(); w.OnUpdate(0);
    ASSERT_EQ(w.Document().project.comparisons.size(),1) << w.status;
    EXPECT_TRUE(undo->CanUndo()); ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(w.Document().project),before);
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(w.Document().project.comparisons.size(),1);
    JobContext cancel({}, {}, {}, {}, [] {return true;});
    DocumentJob job(Operation::Plot,before); job.analysisOptions=w.analysisOptions;
    EXPECT_THROW(job.Execute(cancel),JobCancelledException);
}
TEST(CapLabCompare, BrowserAndDirectActionsRenderWithoutIdConflicts) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow w(jobs,bus,CreateRef<UndoStack>()); w.Document().project=FixtureProject(); RegisterCommands(commands,bus,w);
    w.Select(Json{{"kind","measurement"},{"id",w.Document().project.sweeps[0].id}});
    BuildCompareGraph(w.Document().project,Json{{"plot_type","C-f"},{"measurement_ids",{w.Document().project.sweeps[0].id}}});
    const std::string previous=std::getenv("SST_CAPLAB_GALLERY_SECTION") ? std::getenv("SST_CAPLAB_GALLERY_SECTION") : "";
#ifdef _WIN32
    _putenv_s("SST_CAPLAB_GALLERY_SECTION","Compare");
#else
    setenv("SST_CAPLAB_GALLERY_SECTION","Compare",1);
#endif
    auto *gui=ImGui::CreateContext(); auto *plot=ImPlot::CreateContext();
    auto &io=ImGui::GetIO(); io.DisplaySize={1600,1200}; io.DeltaTime=1.f/60; io.IniFilename=nullptr; io.ConfigDebugHighlightIdConflicts=true;
    unsigned char *pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    CapLabPrototypePanel tree(&w,commands),browser(&w,commands,CapLabPrototypePanel::View::Plot);
    for (int frame=0;frame<3;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({600,1100},ImGuiCond_Always);
        ImGui::Begin("Project"); tree.RenderContent(); ImGui::End();
        ImGui::SetNextWindowSize({900,1100},ImGuiCond_Always);
        ImGui::Begin("Browser"); browser.RenderContent(); ImGui::End();
        ImGui::Render(); EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
    }
    ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
#ifdef _WIN32
    _putenv_s("SST_CAPLAB_GALLERY_SECTION",previous.c_str());
#else
    if (previous.empty()) unsetenv("SST_CAPLAB_GALLERY_SECTION"); else setenv("SST_CAPLAB_GALLERY_SECTION",previous.c_str(),1);
#endif
}
TEST(CapLabCompare, ASMapCommandIsUndoableAndSpatialPanelsHaveNoIdConflicts) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>();
    auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow w(jobs,bus,undo); w.Document().project=FixtureProject();
    {ImportChange second(w.Document().project,FixturePreview(),230,CFTemplate{});}
    MoveMeasurements(w.Document().project,{w.Document().project.sweeps[1].id},w.Document().project.samples[0].id);
    RegisterCommands(commands,bus,w);
    const auto before=Serialize(w.Document().project);
    auto sub=bus->Subscribe<DocumentRequested>([&](const auto &e) {w.Start(e.operation,e.target);});
    const auto &p=w.Document().project;
    w.analysisOptions={{"plot_type","AS C(f,T)"},{"view_kind","surface"},{"measurement_ids",{p.sweeps[1].id,p.sweeps[0].id}}};
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.plot"}));bus->ProcessQueue();jobs->Shutdown();w.OnUpdate(0);
    ASSERT_EQ(w.Document().project.comparisons.size(),1) << w.status;
    ASSERT_TRUE(undo->Undo());EXPECT_EQ(Serialize(w.Document().project),before);
    ASSERT_TRUE(undo->Redo());
    auto* gui=ImGui::CreateContext();auto* plot=ImPlot::CreateContext();auto* spatial=ImPlot3D::CreateContext();
    auto& io=ImGui::GetIO();io.DisplaySize={1400,1200};io.DeltaTime=1.f/60;io.IniFilename=nullptr;io.ConfigDebugHighlightIdConflicts=true;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    CapLabPrototypePanel panel(&w,commands,CapLabPrototypePanel::View::Plot);
    for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();ImGui::SetNextWindowSize({1000,1100},ImGuiCond_Always);ImGui::Begin("AS map");
        panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
    }
    EXPECT_TRUE(w.view.camera);EXPECT_TRUE(w.view.viewport);
    const auto layer=w.Document().project.engine->children(w.Document().project.graph).at(0);
    EXPECT_TRUE(std::get<pl::Layer>(w.Document().project.engine->object(layer).data).view.camera);
    ImPlot3D::DestroyContext(spatial);ImPlot::DestroyContext(plot);ImGui::DestroyContext(gui);
}
}
