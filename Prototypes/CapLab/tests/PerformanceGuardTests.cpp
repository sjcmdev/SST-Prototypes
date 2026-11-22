#include "../../../tools/PerformanceGuard.hpp"
#include "TestProject.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Domain/CapLabIS.hpp"
#include "CapLabCommands.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <thread>

namespace SST::Prototypes::CapLab {
TEST(PerformanceGuard, StaticDataBoundaries) {perfguard::BoundaryCheck(__FILE__);}
namespace {
Project LargeProject() {
    auto preview=FixturePreview();
    preview["columns"]=Json::array({Json::array(),Json::array(),Json::array()});
    for(int i=0;i<100000;++i) {
        preview["columns"][0].push_back(std::pow(10.,6.*i/99999.));
        preview["columns"][1].push_back(1e-9+1e-12*i/99999.);
        preview["columns"][2].push_back(1e6);
    }
    preview["source_rows"]=Json::array();
    for(std::size_t i=0;i<preview["columns"][0].size();++i)preview["source_rows"].push_back(i+1);
    Project p; ImportChange change(p,preview,100,CFTemplate{});return p;
}
void Analyze(CapLabWorkflow& workflow,Project p) {
    EditCircuit(p,{{"action","create"},{"sample_name","Guard"},{"name","RC"},{"topology","R0-C0"}});
    workflow.Document().project=std::move(p);
    workflow.analysisOptions={{"type","Circuit"},{"circuit_id",workflow.Document().project.circuits[0].at("id")},
        {"frequency_Hz",{1.,10.,100.,1000.}},{"temperature_K",300.},{"bias_V",0.}};
    workflow.Start(Operation::Analyze);ASSERT_NE(workflow.activeJob,0u);
    const auto deadline=perfguard::Clock::now()+std::chrono::minutes(1);
    while(workflow.activeJob && perfguard::Clock::now()<deadline){workflow.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_EQ(workflow.activeJob,0u);ASSERT_FALSE(workflow.failure);
    ASSERT_FALSE(workflow.Document().project.analyses.empty());
}
}
TEST(PerformanceGuard, LargeProjectPanelsFrameBudget_F05) {
    auto* gui=ImGui::CreateContext();auto* plot=ImPlot::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1500,1000};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        CapLabWorkflow workflow(CreateRef<JobSystem>(nullptr,1));workflow.Document().project=LargeProject();
        CapLabPrototypePanel panel(&workflow,{},CapLabPrototypePanel::View::Plot);
        perfguard::Frames("CapLab.large-project.F05",[&]{
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1450,950});
            ImGui::Begin("CapLab guard");panel.RenderContent();ImGui::End();ImGui::Render();
            EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
        },40.);
    }
    ImPlot::DestroyContext(plot);ImGui::DestroyContext(gui);
}
TEST(PerformanceGuard, ImportAnalysisMemoryPlateau) {
    auto bus=CreateRef<EventBus>();auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow workflow(CreateRef<JobSystem>(nullptr,1),bus);
    RegisterCommands(commands,bus,workflow);
    perfguard::Plateau("CapLab.import-analysis",[&]{Analyze(workflow,LargeProject());},8ull*1024*1024);
}
}
