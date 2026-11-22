#include "../../../tools/PerformanceGuard.hpp"
#include "Panel.hpp"
#include "Commands/Commands.hpp"
#include <scaps_core/boundary_probe.hpp>
#include <imgui_internal.h>
#include <implot.h>
#include <thread>

using namespace SST;
using namespace SST::ScapsLab;
namespace {
void Wait(Workflow& w) {
    const auto deadline=perfguard::Clock::now()+std::chrono::minutes(8);
    while(w.Pending() && perfguard::Clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());ASSERT_TRUE(w.response.value("ok",false))<<w.response;
}
void Small(Workflow& w) {
    w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
    w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.Current().document["mesh"]["points_per_layer"]=21;
    w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.01},{"step_V",.01}};
}
void Sweep5(Workflow& w) {
    w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
    const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures/perfguard-sweep5.scapslab";
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path.string()}}).value("ok",false));Wait(w);
    for(const auto& device:w.project.devices)if(device->name=="light")w.project.active_device=device->id;
}
}
TEST(PerformanceGuard, StaticDataBoundaries) {perfguard::BoundaryCheck(__FILE__);}
TEST(PerformanceGuard, CommandJobTypedConsumerHasNoResultSerialization) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));Small(w);
    scaps_core::boundary_probe::result_adapters=0;
    ASSERT_TRUE(Cmd::Run::With().Execute(w));Wait(w);
    ASSERT_TRUE(w.Current().Iv());ASSERT_FALSE(w.Current().Iv()->points.empty());
    auto spec=BatchDefaults();spec["axes"][0]={{"parameter_id","temperature_K"},{"values",{290.,310.}}};
    ASSERT_TRUE(Cmd::RunBatch::With({{"spec",spec}}).Execute(w));Wait(w);
    ASSERT_TRUE(w.Current().batchResult.typed());ASSERT_EQ(w.Current().batchResult.typed()->rows.size(),2u);
    EXPECT_EQ(scaps_core::boundary_probe::result_adapters,0u);
    (void)w.Current().Iv()->Dump();EXPECT_GT(scaps_core::boundary_probe::result_adapters,0u);
}
TEST(PerformanceGuard, Batch342PanelsFrameBudget_F04_F35) {
    auto* gui=ImGui::CreateContext();auto* plot=ImPlot::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1500,1000};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        Workflow w(CreateRef<JobSystem>(nullptr,4));Sweep5(w);
        // Deterministic typed result fixture: real sweep5 spec and 342 retained member curves, without solving during frame timing.
        auto configured=w.Current().batchSpec;configured["columns"]=RecorderDefaults();
        const auto preview=BatchPreview(w.Current().document,configured);
        ASSERT_EQ(preview.at("member_count"),342);
        auto table=CreateRef<BatchResult>();table->native=true;table->body.prim={{"columns",Json::array({{{"id","J_at_0V"},{"quantity","J"},{"unit","A/m^2"}}})},{"spec",preview.at("spec")}};
        for(int i=0;i<342;++i){auto row=CreateRef<scaps_core::BatchRow>();row->id="point_"+std::to_string(i);for(const auto& [path,value]:preview.at("combinations")[i].items())row->parameters[path]=value.get<double>();row->values["J_at_0V"]={double(i),"ok"};for(int v=0;v<200;++v){scaps_core::IVSample point;point.voltage_V=v*.005;point.J_Am2=i*.01+v;point.status=scaps_core::SolverStatus::Converged;row->iv.push_back(point);}table->rows.push_back(row);}
        w.Current().batchResult.adopt(table);
        for(int view:{16,17,18}) {
            Panel panel(&w,view);
            perfguard::Frames(("ScapsLab.view"+std::to_string(view)+".F04.F35").c_str(),[&]{
                ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1450,950});
                ImGui::Begin("ScapsLab guard");panel.RenderContent();ImGui::End();ImGui::Render();
                EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
            },view==16?20.:view==17?2.:5.);
        }
    }
    ImPlot::DestroyContext(plot);ImGui::DestroyContext(gui);
}
TEST(PerformanceGuard, Sweep5BatchMemoryPlateau) {
    Workflow w(CreateRef<JobSystem>(nullptr,4));Sweep5(w);w.project.ui["history_retention"]=1;
    perfguard::Plateau("ScapsLab.sweep5",[&]{
        ASSERT_TRUE(Cmd::RunBatch::With().Execute(w));Wait(w);
        ASSERT_TRUE(w.Current().batchResult.typed());ASSERT_EQ(w.Current().batchResult.typed()->rows.size(),342u);
    },32ull*1024*1024);
}
