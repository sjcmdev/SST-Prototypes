#include "../../../tools/PerformanceGuard.hpp"
#include "Presentation/Panels/MonteCarloCurvesPanel.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include <imgui_internal.h>
#include <implot.h>

namespace SST::Prototypes::JFM {
TEST(PerformanceGuard, StaticDataBoundaries) {perfguard::BoundaryCheck(__FILE__);}
TEST(PerformanceGuard, MonteCarlo512FrameBudget_F32) {
    auto* gui=ImGui::CreateContext();auto* plot=ImPlot::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1500,1000};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        auto store=CreateRef<JFMWorkspaceStore>();auto bus=CreateRef<EventBus>();
        const auto project=store->GetActiveProjectHandle();
        JFMCharacteristicRecord record;record.info.name="Guard MC";record.info.temperatureK=300;
        record.info.fit=JFMFitState{};record.info.fit->status="completed";record.info.fit->modelId="SD-RsRsh";
        record.info.monteCarlo=JFMMonteCarloState{};record.info.monteCarlo->modelId="SD-RsRsh";
        auto data=CreateRef<JFMCharacteristicData>();data->voltage={0.,.5};data->current={0.,1.};record.data=data;
        const auto ids=store->AddCharacteristicRecords(project,{record});ASSERT_EQ(ids.size(),1u);
        JFMCharacteristicRef ref{project,ids[0]};const auto generation=store->BeginMonteCarlo(ref);
        JFMMonteCarloResult result;result.parameterNames={"I0","A","Rs","Rsh"};
        for(int i=0;i<1000;++i)result.voltage.push_back(.7*i/999.);
        result.idealCurrent.resize(1000,.001);result.noisyCurrent.resize(512*1000,.001);
        for(int i=0;i<512;++i){JFMMonteCarloSample sample;sample.parameters={1e-10,1.5,2.,1e6};sample.converged=true;sample.solverConverged=true;result.samples.push_back(sample);}
        JFMMonteCarloState state;state.modelId="SD-RsRsh";state.status="completed";state.sampleCount=512;state.acceptedCount=512;
        ASSERT_TRUE(store->ApplyMonteCarloResult(ref,generation,state,std::move(result)));
        MonteCarloCurvesPanel panel(store,bus);bus->PublishNew<MonteCarloCompleted>(ref,generation,state,JFMMonteCarloResult{});
        ASSERT_EQ(store->GetMonteCarloResult(ref)->samples.size(),512u);
        perfguard::Frames("JFM.MC512.F32",[&]{
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1450,950});
            ImGui::Begin("JFM guard");panel.RenderContent();ImGui::End();ImGui::Render();
            EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
            EXPECT_GT(ImGui::GetDrawData()->TotalVtxCount,1000);
        },350.);
    }
    ImPlot::DestroyContext(plot);ImGui::DestroyContext(gui);
}
}
