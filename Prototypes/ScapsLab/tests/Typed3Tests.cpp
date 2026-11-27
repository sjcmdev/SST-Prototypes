#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Commands/Commands.hpp"
#include <scaps_core/device_project_yaml.hpp>
#include <fstream>
#include <thread>
#include <scaps_core/boundary_probe.hpp>
#include "Panel.hpp"
#include "PlotUi.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
void Wait(Workflow& w){while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(w.response.value("ok",false))<<w.response;}
Ref<const ProjectDraft> SmallDraft(){auto p=scaps_core::make_default_device_project();p.layers.resize(2);for(auto& l:p.layers)l.defects.clear();p.interface_defects.clear();p.mesh.points_per_layer=11;p.voltage={0,.01,.01};return DraftFromProject(std::move(p));}
}
TEST(RunActions, ResultFocusSurvivesEditsDuringDragAndChoosesTheNewRunAction) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Wait(w);const auto run=w.Current().selectedRun;
    EXPECT_EQ(w.focusResultRun,run);w.dragging=w.interacting=true;
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=310;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));EXPECT_TRUE(w.Current().selectedRun.empty());
    w.RestoreResultFocus();EXPECT_TRUE(w.Current().selectedRun.empty());EXPECT_EQ(w.focusResultRun,run);
    w.dragging=w.interacting=false;w.project.ui["results_action"]="qe";w.RestoreResultFocus();EXPECT_EQ(w.Current().selectedRun,run);EXPECT_EQ(w.project.ui["results_action"],"iv");EXPECT_TRUE(w.focusResultRun.empty());
}
TEST(RunActions, LivePreviewResolutionUsesCommandPathAndValidatesHeadlessInputs) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;
    for(int every:{0,1,2,3,4}){ASSERT_TRUE(w.Exec(Cmd::LivePreviewResolution::Make(every)).value("ok",false));EXPECT_EQ(w.livePreviewStride,every);}
    ASSERT_TRUE(w.Dispatch({{"operation","live_preview_resolution"},{"every_nth",2}}).value("ok",false));EXPECT_EQ(w.livePreviewStride,2);
    for(const Json invalid:{Json(-1),Json(5),Json(true),Json("auto"),Json(10000000000LL)}){const auto reply=w.Dispatch({{"operation","live_preview_resolution"},{"every_nth",invalid}});EXPECT_FALSE(reply.value("ok",true));EXPECT_EQ(w.livePreviewStride,2);}
}
TEST(RunActions, PreviewSamplingKeepsEndpointsAndHonestShortCurves) {
    for(int size:{0,1,3,8,11,17,61,201})for(int stride:{1,2,3,4,100}) {
        std::vector<double> samples;for(int i=0;i<size;++i)samples.push_back(double(i));const auto coarse=PreviewSamples(samples,stride);
        if(size){EXPECT_EQ(coarse.front(),samples.front());EXPECT_EQ(coarse.back(),samples.back());}
        EXPECT_GE(coarse.size(),std::min<std::size_t>(8,samples.size()));EXPECT_LE(coarse.size(),samples.size());
        for(double value:coarse)EXPECT_EQ(value,samples[std::size_t(value)]);
    }
}
TEST(RunActions, ClearAllSimulationKindsOneUndoAndReleaseRetainedResults)
{
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(w);EXPECT_EQ(w.focusView,18);w.focusView=-1;
    ASSERT_TRUE(w.Exec(Cmd::CaptureLive::Make(ActionKind::Iv)).value("ok",false));
    auto batch=CreateRef<BatchDraft>(*w.Current().batch);batch->axes.front().values=std::vector<double>{300,301};
    ASSERT_TRUE(w.Exec(Cmd::QuickSweep::Make(batch,w.Current().recorder,w.Current().draft,plan)).value("ok",false));Wait(w);EXPECT_EQ(w.focusView,18);
    ASSERT_TRUE(w.Exec(Cmd::RunBatch::Make(batch,w.Current().recorder,plan)).value("ok",false));Wait(w);
    const auto count=w.StoredSimulationCount();EXPECT_GE(count,4u);const auto entries=w.Current().history.size();const auto depth=w.projectUndo.GetUndoDepth();
    std::weak_ptr<const ResultBase> retained=w.Current().batchResult.typed();
    ASSERT_TRUE(w.Exec(Cmd::ClearHistory::With({{"scope","all"},{"include_pinned",true}})).value("ok",false));
    EXPECT_EQ(w.StoredSimulationCount(),0u);EXPECT_TRUE(w.Current().runs->empty());EXPECT_TRUE(w.Current().batchResult.empty());EXPECT_TRUE(w.Current().result.empty());EXPECT_EQ(w.projectUndo.GetUndoDepth(),depth+1);EXPECT_FALSE(retained.expired());
    ASSERT_TRUE(w.Exec(Cmd::Undo::With()).value("ok",false));EXPECT_EQ(w.StoredSimulationCount(),count);EXPECT_EQ(w.Current().history.size(),entries);
    ASSERT_TRUE(w.Exec(Cmd::Redo::With()).value("ok",false));EXPECT_EQ(w.StoredSimulationCount(),0u);
    w.projectUndo.Clear();EXPECT_TRUE(retained.expired());
}
TEST(Typed3, DirectFileAdapter) {
    for(const auto& file:std::filesystem::directory_iterator(ExamplesDirectory())) {
        const auto extension=file.path().extension();if(extension!=".scapslab" && extension!=".yaml")continue;
        std::vector<Json> documents;
        if(extension==".scapslab"){std::ifstream input(file.path());Json value;input>>value;for(const auto& d:value.at("devices"))documents.push_back(NormalizeDocument(d.at("document")));}
        else documents.push_back(ReadProject(file.path().string()));
        for(const auto& document:documents){const auto draft=DraftFromDocument(document);EXPECT_EQ(DraftDocument(*draft).dump(),document.dump())<<file.path();EXPECT_EQ(scaps_core::serialize_device_project_yaml(draft->project),scaps_core::serialize_device_project_yaml(Model(document)))<<file.path();}
    }
}
TEST(Typed3, PanelGroupsUseTypedCommandAndUndoRoundTrip) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());
    const auto check=[&](auto edit,const std::string& gesture){const auto before=w.Current().draft;const auto document=DraftDocument(*before);auto next=CreateRef<ProjectDraft>(*before);edit(*next);ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,gesture)).value("ok",false));Wait(w);const auto after=w.Current().Document();EXPECT_EQ(DraftDocument(*before),document);ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Current().Document(),document);ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));EXPECT_EQ(w.Current().Document(),after);};
    check([](ProjectDraft& d){d.project.layers[0].name="typed layer";SetProjectField(d.project,"layers[0].thickness_m",2e-6);SetProjectField(d.project,"layers[0].doping.Nd_m3",1e20);SetProjectField(d.project,"contacts.left.flat_band",true);SetProjectField(d.project,"contacts.right.label",std::string("typed contact"));d.project.external.series_ohm_m2=1e-6;},"layers/contacts");
    check([](ProjectDraft& d){const int i=AddDraftDefect(d,0,false);AddDraftDefect(d,0,true);auto& v=d.project.layers[0].defects[i];v.amphoteric=true;v.energy_reference=scaps_core::DefectEnergyReference::RelativeToEc;v.energy_offset_eV=.4;v.density_profile_m3.emplace();v.density_profile_m3->left=v.density_profile_m3->right=1e18;d.project.interface_defects[0].density_convention="scaps_peak";},"defects/interfaces");
    check([](ProjectDraft& d){SetProjectField(d.project,"illumination.enabled",true);SetProjectField(d.project,"illumination.mode",std::string("uniform"));SetProjectField(d.project,"illumination.uniform_generation_m3_s",1e24);SetProjectField(d.project,"constants_mode",scaps_core::ConstantsMode::ScapsCompat);SetProjectField(d.project,"solver.max_newton_iterations",std::int64_t(120));d.project.temperature_K=315;},"illumination/solver");
    auto invalid=*w.Current().draft;EXPECT_THROW(SetProjectField(invalid.project,"mesh.points_per_layer",3.5),std::invalid_argument);
}
TEST(Typed3, StructuralEditsKeepOpaqueMetadataAndIdsThroughUndo) {
    auto document=DraftDocument(*SmallDraft());document["layers"][0]["future_layer"]={{"numeric_string","001"},{"nullable",nullptr}};document["scaps_native"]["future_native"]={{"version",7}};document["scaps_native"]["sections"]=Json::array({{{"name","convergence"},{"lines",{"opaque line"}},{"future_section",true}}});
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDocument(document);const auto original=w.Current().Document();
    auto edit=[&](auto fn){auto next=CreateRef<ProjectDraft>(*w.Current().draft);fn(*next);ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);const auto after=w.Current().Document();ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Current().Document(),original);ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));EXPECT_EQ(w.Current().Document(),after);ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));};
    edit([](ProjectDraft& d){EditLayer(d,"move",d.layers[0].id,1);EXPECT_TRUE(DraftDocument(d)["layers"][1].contains("future_layer"));});
    edit([](ProjectDraft& d){EditLayer(d,"add");});
    edit([](ProjectDraft& d){EditLayer(d,"remove",d.layers[0].id);});
    edit([](ProjectDraft& d){AddDraftDefect(d,0,false);AddDraftDefect(d,0,false);d.extensions["/layers/0/defects/0/opaque"]="keep";MoveDraftDefect(d,0,0,1,false);EXPECT_EQ(DraftDocument(d)["layers"][0]["defects"][1]["opaque"],"keep");RemoveDraftDefect(d,0,0,false);});
    edit([](ProjectDraft& d){AddDraftDefect(d,0,true);AddDraftDefect(d,0,true);d.extensions["/interface_defects/0/opaque"]="keep";MoveDraftDefect(d,0,0,1,true);EXPECT_EQ(DraftDocument(d)["interface_defects"][1]["opaque"],"keep");RemoveDraftDefect(d,0,0,true);});
}
TEST(Typed3, SliderGestureMergesAndJobSharesImmutableDraft) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());const auto original=w.Current().draft;
    for(double temperature:{301.,302.,303.}){auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=temperature;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"one drag")).value("ok",false));Wait(w);}
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Current().Document(),DraftDocument(*original));ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));ASSERT_TRUE(w.ActiveOp());EXPECT_EQ(w.ActiveOp()->draft,w.Current().draft);ASSERT_TRUE(w.jobs->ResumeAllJobs());Wait(w);
}
TEST(Typed3, TypedBatchPlanAndInitialStateMatchEdge) {
    const auto draft=SmallDraft();auto input=BatchDefaults();input["axes"][0]={{"parameter_id","temperature_K"},{"values",{290.,310.}}};input["voltages_V"]={0.,.01};input["initial_state"]={{"temperature_K",320.},{"voltage_V",.02},{"generation_total_m2_s",0.}};
    const auto config=ReadBatchDraft(input);const auto plan=PrepareBatch(*draft,*config,RecorderDraft{});ASSERT_EQ(plan.combinations.size(),2u);EXPECT_EQ(BatchDraftJson(*config),input);EXPECT_EQ(BatchPlanJson(plan),BatchPreview(*draft,input));
    const auto spec=PrepareBatchMember(draft->project,*config,plan.engine);ASSERT_TRUE(spec.initial_state);EXPECT_DOUBLE_EQ(spec.initial_state->temperature_K,320.);EXPECT_DOUBLE_EQ(spec.initial_state->voltage_V,.02);EXPECT_FALSE(spec.initial_state->illumination.enabled);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(draft);const auto depth=w.Current().undo.GetUndoDepth();ASSERT_TRUE(w.Exec(Cmd::SetBatchSettings::Make(config)).value("ok",false));Wait(w);EXPECT_EQ(w.Current().batch,config);EXPECT_EQ(w.Current().undo.GetUndoDepth(),depth);
}
TEST(Typed3, ProfileMetadataUsesProfilePathAndLegacyNumericScalarsLoad) {
    auto document=DraftDocument(*SmallDraft());document["solver"]["residual_tolerance"]="1e-10";const auto draft=DraftFromDocument(document);EXPECT_DOUBLE_EQ(draft->project.solver.residual_tolerance,1e-10);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(draft);ASSERT_TRUE(w.Dispatch({{"operation","load_profile"},{"layer_id",draft->layers[0].id},{"parameter_id","Na_m3"},{"path",(ExamplesDirectory()/"profiles/doping.grd").string()}}).value("ok",false));Wait(w);const auto& p=*w.Current().draft->project.layers[0].grading->Na_m3;EXPECT_EQ(ProjectFieldPath(w.Current().draft->project,&p),"layers[0].grading.Na_m3");EXPECT_TRUE(w.Current().Document()["layers"][0]["grading"]["Na_m3"].contains("scapslab_warnings"));
}

// slux3 S2: typed Actions, explicit storage, bounded/cancellable preview.
TEST(RunActions, OwnRangesAndSingleGroupUseTypedResults) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();for(auto& a:plan->actions)a.include=a.kind<ActionKind::Dlcp;
    plan->actions[0].voltage_V=.013;
    plan->actions[1].range=SweepRange{SweepSampling::List,false,0,0,1,2,{0,.012,.024}};
    plan->actions[2].range=SweepRange{SweepSampling::List,false,0,0,1,2,{0,.021}};plan->actions[2].frequency_Hz=1234;plan->actions[2].temperature_K=315;
    plan->actions[3].range=SweepRange{SweepSampling::List,true,0,0,1,2,{100,2000,30000}};plan->actions[3].voltage_V=.017;
    plan->actions[4].range=SweepRange{SweepSampling::List,false,0,0,1,2,{400,550,700}};
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(w);
    EXPECT_EQ(w.Current().Band()->points.size(),1u);EXPECT_DOUBLE_EQ(w.Current().Band()->points[0].voltage_V,.013);
    EXPECT_EQ(w.Current().Iv()->points.size(),3u);EXPECT_DOUBLE_EQ(w.Current().Iv()->points.back().voltage_V,.024);
    const auto cv=w.Current().Ac("cv"),cf=w.Current().Ac("cf"),qe=w.Current().Ac("qe");
    ASSERT_EQ(cv->points.size(),2u);EXPECT_DOUBLE_EQ(cv->points.back().voltage_V,.021);EXPECT_DOUBLE_EQ(cv->points[0].temperature_K,315);EXPECT_DOUBLE_EQ(cv->points[0].frequency_Hz,1234);
    ASSERT_EQ(cf->points.size(),3u);EXPECT_DOUBLE_EQ(cf->points.back().frequency_Hz,30000);EXPECT_DOUBLE_EQ(cf->points[0].voltage_V,.017);
    ASSERT_EQ(qe->points.size(),3u);EXPECT_DOUBLE_EQ(qe->points[1].wavelength_nm,550);
    ASSERT_EQ(w.Current().history.size(),5u);std::set<std::string> groups;
    for(const auto& run:w.Current().history){groups.insert(run.at("group_id"));EXPECT_TRUE(run.at("explicit"));EXPECT_FALSE(w.Current().RunResult(run.at("run_id"))->HasView());}
    EXPECT_EQ(groups.size(),1u);EXPECT_FALSE(groups.contains(""));EXPECT_EQ(w.Current().Iv()->runPlan,plan);
    const auto saved=w.Workspace();EXPECT_NO_THROW(ValidateWorkspace(saved));
    Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;loaded.SetWorkspace(saved);
    ASSERT_TRUE(loaded.Current().Ac("cf")->runPlan);EXPECT_EQ(RunPlanJson(*loaded.Current().Ac("cf")->runPlan),RunPlanJson(*plan));
}
TEST(RunActions, IncludeAndOnEditAreIndependentAndAutoExactNeverStores) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();plan->actions[1].include=false;plan->actions[1].onEdit=true;
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));
    for(int i=0;i<3;++i) {
        auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=301+i;
        ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);
        w.Current().changedAt=std::chrono::steady_clock::now()-std::chrono::seconds(1);w.OnUpdate(0);Wait(w);
        EXPECT_EQ(w.Current().history.size(),0u);EXPECT_EQ(w.Current().runs->size(),0u);EXPECT_DOUBLE_EQ(w.Current().Iv()->meta.prim.at("project_snapshot").at("temperature_K"),301+i);
        EXPECT_TRUE(w.Current().Iv()->liveOnly);EXPECT_TRUE(w.Workspace().at("devices")[0].at("result").empty());EXPECT_TRUE(w.Runs().empty());
    }
    EXPECT_FALSE(w.Exec(Cmd::SingleShot::Make()).value("ok",true));
    ASSERT_TRUE(w.Exec(Cmd::CaptureLive::Make(ActionKind::Iv)).value("ok",false));EXPECT_EQ(w.Current().history.size(),1u);
    const auto stored=w.Current().RunResult(w.Current().selectedRun);EXPECT_EQ(stored,w.Current().result.typed());
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=310;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);
    EXPECT_FALSE(w.Exec(Cmd::CaptureLive::Make(ActionKind::Iv)).value("ok",true));EXPECT_EQ(w.Current().history.size(),1u);
    // owner 2026-11-23: an edit returns the view to Live; the stored run is kept unchanged
    EXPECT_TRUE(w.Current().selectedRun.empty());EXPECT_EQ(stored,w.Current().RunResult(w.Current().history.back().at("run_id").get<std::string>()));
}
TEST(RunActions, InheritedIvMatchesLegacyExactAndCachesAllBiasProfiles) {
    Workflow w(CreateRef<JobSystem>(nullptr,1)),reference(CreateRef<JobSystem>(nullptr,1));w.lightResponse=reference.lightResponse=true;
    const auto draft=SmallDraft();w.Current().SetDraft(draft);reference.Current().SetDraft(draft);
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Wait(w);
    ASSERT_TRUE(reference.Dispatch({{"operation","run"}}).value("ok",false));Wait(reference);
    ASSERT_EQ(w.Current().Iv()->points.size(),reference.Current().Iv()->points.size());
    for(std::size_t i=0;i<w.Current().Iv()->points.size();++i) {
        EXPECT_DOUBLE_EQ(w.Current().Iv()->points[i].J_A_m2,reference.Current().Iv()->points[i].J_A_m2);
        EXPECT_TRUE(w.Current().Iv()->points[i].details);EXPECT_FALSE(w.Current().Iv()->points[i].x_m.empty());
    }
    EXPECT_FALSE(w.Current().Iv()->HasView());
}
TEST(RunActions, PauseResumeAndStopKeepCompletedActions) {
    for(bool stop:{false,true}) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
        auto plan=CreateRef<RunPlan>();plan->pauseAfterAction=true;plan->actions[0].include=true;
        ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));const auto op=w.ActiveOp();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!op->paused && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_TRUE(op->paused);EXPECT_EQ(op->completedActions.size(),1u);
        if(stop) {
            ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).value("ok",false));
            while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            EXPECT_EQ(w.Current().history.size(),1u);EXPECT_EQ(w.response.at("error").at("code"),"cancelled");
        } else {
            ASSERT_TRUE(w.Exec(Cmd::ResumeRun::With()).value("ok",false));Wait(w);EXPECT_EQ(w.Current().history.size(),2u);
        }
    }
}
TEST(RunActions, PreviewIsEveryStridePointOnExactMeshProgressiveAndCancellable) { // owner 2026-11-23: same mesh as exact, every 4th bias when slow
    auto draft=CreateRef<ProjectDraft>(*SmallDraft());draft->project.mesh.points_per_layer=401;draft->project.voltage={0,.2,.001};
    Operation preview(draft,{{"operation","run"},{"preview",true},{"stride",4}});preview.feed=CreateRef<LiveFeed>();preview.jsonResponse=false;
    bool progressive=false;JobContext ctx([&](float done,float total){if(done<total){progressive=true;EXPECT_GT(preview.feed->v.size(),0u);}},[](auto&){},[](auto&){},[](auto,auto&){},[]{return false;});
    preview.Execute(ctx);const auto result=std::static_pointer_cast<const IvResult>(preview.typedOut);
    EXPECT_TRUE(progressive);EXPECT_EQ(result->points.size(),51u);EXPECT_GT(preview.output.at("mesh_node_count").get<int>(),202);EXPECT_EQ(result->mesh_x_m.size(),0u);
    for(const auto& point:result->points){EXPECT_FALSE(point.details);EXPECT_EQ(point.x_m.size(),0u);}
    Operation cancelled(draft,{{"operation","run"},{"preview",true}});cancelled.feed=CreateRef<LiveFeed>();
    JobContext stopping([&](float,float){cancelled.cancellation.request_stop();},[](auto&){},[](auto&){},[](auto,auto&){},[]{return false;});
    EXPECT_THROW(cancelled.Execute(stopping),JobCancelledException);EXPECT_EQ(cancelled.feed->v.size(),1u); // stops after the first point
}
TEST(RunActions, StalePreviewIsInvalidatedAtEditAndCannotPublish) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());w.dragging=true;
    ASSERT_TRUE(w.jobs->PauseAllJobs());w.StartOperation({{"operation","run"},{"preview",true}},w.Current().id);
    const auto stale=w.ActiveOp();stale->feed->Point(0,123);w.Current().previewV={0};w.Current().previewJ={123};
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=320;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));EXPECT_TRUE(stale->superseded);EXPECT_TRUE(stale->cancellation.stop_requested());EXPECT_EQ(w.Current().previewV,std::vector<double>{0});
    stale->feed->Point(.01,999);w.PullLive(w.LaneOf(w.Current().id));EXPECT_EQ(w.Current().previewJ,std::vector<double>{123});
    ASSERT_TRUE(w.jobs->ResumeAllJobs());w.dragging=false;Wait(w);EXPECT_TRUE(w.Current().history.empty());
}
TEST(RunActions, ExplicitGroupsSurviveLegacyRetentionAndPlanRoundTrip) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();plan->actions[1].range=SweepRange{SweepSampling::Points,false,0,.02,.01,3};
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));
    for(int i=0;i<7;++i){ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Wait(w);}
    EXPECT_EQ(w.Current().history.size(),7u);const auto saved=w.Workspace();
    Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;loaded.SetWorkspace(saved);
    EXPECT_EQ(loaded.Current().history.size(),7u);EXPECT_EQ(RunPlanJson(*loaded.Current().runPlan),RunPlanJson(*plan));
    ASSERT_TRUE(loaded.Exec(Cmd::SelectRun::With({{"run_id",""}})).value("ok",false));EXPECT_TRUE(loaded.Current().selectedRun.empty());
}
TEST(RunActions, HeadlessAdapterAndTypedCommandUseTheSamePlan) {
    const auto draft=SmallDraft();Workflow typed(CreateRef<JobSystem>(nullptr,1)),edge(CreateRef<JobSystem>(nullptr,1));typed.lightResponse=edge.lightResponse=true;
    typed.Current().SetDraft(draft);edge.Current().SetDraft(draft);auto plan=CreateRef<RunPlan>();plan->actions[1].range=SweepRange{SweepSampling::List,false,0,0,1,2,{.01,0,.02}};plan->actions[1].temperature_K=315;
    ASSERT_TRUE(typed.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(typed);
    ASSERT_TRUE(edge.Dispatch({{"operation","single_shot"},{"actions",RunPlanJson(*plan)}}).value("ok",false));Wait(edge);
    ASSERT_EQ(typed.Current().Iv()->points.size(),edge.Current().Iv()->points.size());
    for(std::size_t i=0;i<typed.Current().Iv()->points.size();++i){EXPECT_DOUBLE_EQ(typed.Current().Iv()->points[i].voltage_V,edge.Current().Iv()->points[i].voltage_V);EXPECT_DOUBLE_EQ(typed.Current().Iv()->points[i].J_A_m2,edge.Current().Iv()->points[i].J_A_m2);}
    auto invalid=*plan;invalid.actions[1].range->custom={NAN};EXPECT_THROW(ValidateRunPlan(invalid,*draft),std::invalid_argument);
}
TEST(RunActions, PauseReturnsTheOnlyWorkerToOtherDeviceLanes) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();plan->actions[0].include=true;plan->pauseAfterAction=true;
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));const auto paused=w.ActiveOp();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!paused->paused && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(paused->paused);
    auto other=CreateRef<DeviceState>();other->id="pause-independent";other->name="Other";other->SetDraft(SmallDraft());w.project.devices.push_back(other);
    w.Submit(other->id,{{"operation","band"},{"voltage_V",0.}});
    while(w.Busy(other->id) && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Busy(other->id));EXPECT_FALSE(other->Band()->empty());EXPECT_TRUE(paused->paused);
    ASSERT_TRUE(w.Exec(Cmd::ResumeRun::With()).value("ok",false));Wait(w);EXPECT_EQ(w.Current().history.size(),2u);
}
TEST(RunActions, OnEditSettingsDoNotEnableAnotherDevice) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();plan->actions[1].onEdit=true;ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));
    EXPECT_FALSE(w.autoRecalculate);EXPECT_FALSE(w.autoRecalculateBand);
    auto other=CreateRef<DeviceState>();other->id="on-edit-independent";other->name="Other";other->SetDraft(SmallDraft());w.project.devices.push_back(other);w.SelectDevice(other->id);
    auto next=CreateRef<ProjectDraft>(*other->draft);next->project.temperature_K=315;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);
    other->changedAt=std::chrono::steady_clock::now()-std::chrono::seconds(1);w.OnUpdate(0);
    EXPECT_FALSE(w.Busy());EXPECT_TRUE(other->result.empty());EXPECT_TRUE(w.Device(w.project.devices.front()->id).runPlan->actions[1].onEdit);
}
namespace {
Ref<RunPlan> SmallActions() {
    auto plan=CreateRef<RunPlan>();for(auto& a:plan->actions)a.include=a.kind<ActionKind::Dlcp;
    plan->actions[0].voltage_V=.012;
    plan->actions[1].range=SweepRange{SweepSampling::List,false,0,0,1,1,{0,.012,.024}};
    plan->actions[2].range=SweepRange{SweepSampling::List,false,0,0,1,1,{0,.021}};plan->actions[2].frequency_Hz=1234;plan->actions[2].temperature_K=315;
    plan->actions[3].range=SweepRange{SweepSampling::List,false,0,0,1,1,{100,2000,30000}};plan->actions[3].voltage_V=.017;
    plan->actions[4].range=SweepRange{SweepSampling::List,false,0,0,1,1,{400,550,700}};return plan;
}
void ActionBatch(Workflow& w,Ref<const RunPlan> plan,int count=1,bool curves=true,bool profiles=false) {
    w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());
    auto batch=CreateRef<BatchDraft>();batch->axes[0].values=std::vector<double>(count,300);batch->include_curves=curves;batch->include_profiles=profiles;
    w.Current().SetBatch(batch);ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));ASSERT_TRUE(w.Exec(Cmd::RunBatch::With()).value("ok",false));
}
Ref<const ResultBase> ActionOf(const MemberActions& m,ActionKind kind){for(const auto& [k,r]:m.results)if(k==kind)return r;return {};}
void WaitPaused(Workflow& w,const Ref<Operation>& op) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!op->paused && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(op->paused);
}
}
TEST(ActionBatch, SingleAndOneMemberBatchHaveIdenticalActionSamplesWithoutJson) {
    Workflow single(CreateRef<JobSystem>(nullptr,1)),batch(CreateRef<JobSystem>(nullptr,1));single.lightResponse=true;single.Current().SetDraft(SmallDraft());const auto plan=SmallActions();
    ASSERT_TRUE(single.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(single);
    scaps_core::boundary_probe::result_adapters=0;scaps_core::boundary_probe::project_text=0;ActionBatch(batch,plan);Wait(batch);
    const auto result=batch.Current().batchResult.typed();ASSERT_TRUE(result);ASSERT_EQ(result->actionMembers.size(),1u);ASSERT_EQ(result->actionMembers[0]->results.size(),5u);
    EXPECT_EQ(batch.Current().history.size(),1u);EXPECT_TRUE(batch.Current().history.front().at("explicit"));EXPECT_TRUE(batch.Current().history.front().at("document").empty());
    EXPECT_EQ(scaps_core::boundary_probe::result_adapters,0u);EXPECT_EQ(scaps_core::boundary_probe::project_text,0u);
    for(const auto& [kind,r]:result->actionMembers[0]->results) {
        EXPECT_FALSE(r->HasView());EXPECT_FALSE(r->snapshot);
        if(const auto iv=std::dynamic_pointer_cast<const IvResult>(r)) {const auto ref=kind==ActionKind::Band?single.Current().Band():single.Current().Iv();ASSERT_EQ(iv->curve.size(),ref->points.size());EXPECT_TRUE(iv->points.empty());for(std::size_t i=0;i<iv->curve.size();++i){EXPECT_DOUBLE_EQ(iv->curve[i].J_Am2,ref->points[i].J_A_m2);EXPECT_DOUBLE_EQ(iv->curve[i].voltage_V,ref->points[i].voltage_V);EXPECT_FALSE(iv->curve[i].profiles);}}
        else {const auto ac=std::static_pointer_cast<const AcResult>(r),ref=single.Current().Ac(ActionId(kind));ASSERT_EQ(ac->points.size(),ref->points.size());for(std::size_t i=0;i<ac->points.size();++i){EXPECT_DOUBLE_EQ(ac->points[i].voltage_V,ref->points[i].voltage_V);if(kind==ActionKind::Qe){EXPECT_DOUBLE_EQ(ac->points[i].wavelength_nm,ref->points[i].wavelength_nm);EXPECT_DOUBLE_EQ(ac->points[i].EQE,ref->points[i].EQE);}else {EXPECT_DOUBLE_EQ(ac->points[i].frequency_Hz,ref->points[i].frequency_Hz);EXPECT_DOUBLE_EQ(ac->points[i].capacitance_F,ref->points[i].capacitance_F);}}}
    }
    const auto saved=batch.Workspace();EXPECT_NO_THROW(ValidateWorkspace(saved));Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;loaded.SetWorkspace(saved);ASSERT_TRUE(loaded.Current().batchResult.typed()->runPlan);EXPECT_EQ(loaded.Current().batchResult.typed()->actionMembers.size(),1u);
}
TEST(ActionBatch, PauseActionReturnsWorkerAndStopPreservesPartialMember) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));auto plan=SmallActions();plan->pauseAfterAction=true;ActionBatch(w,plan,2);const auto op=w.ActiveOp();WaitPaused(w,op);
    ASSERT_TRUE(op->actionBatch);EXPECT_EQ(op->actionBatch->actionMembers[0]->results.size(),1u);EXPECT_EQ(op->actionBatch->rows[1]->status,"Not started");
    auto other=CreateRef<DeviceState>();other->id="batch-other";other->name="Other";other->SetDraft(SmallDraft());w.project.devices.push_back(other);w.Submit(other->id,{{"operation","band"},{"voltage_V",0.}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(w.Busy(other->id) && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_FALSE(w.Busy(other->id));EXPECT_FALSE(other->Band()->empty());
    ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).value("ok",false));while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_EQ(w.Current().history.size(),1u);ASSERT_EQ(w.Current().batchResult.typed()->actionMembers[0]->results.size(),1u);EXPECT_EQ(w.Current().batchResult.typed()->rows[1]->status,"Not started");
}
TEST(ActionBatch, PauseMemberResumeAndRetryPreserveAttemptsAndOriginalSnapshot) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));auto plan=CreateRef<RunPlan>();plan->pauseAfterMember=true;ActionBatch(w,plan,2);const auto op=w.ActiveOp();WaitPaused(w,op);EXPECT_EQ(op->nextMember,1u);ASSERT_TRUE(w.Exec(Cmd::ResumeRun::With()).value("ok",false));Wait(w);
    const auto original=w.Current().batchResult.typed();const auto originalId=w.Current().selectedRun;
    auto changed=CreateRef<ProjectDraft>(*w.Current().draft);changed->project.temperature_K=370;w.Current().SetDraft(changed);
    // Retry does not pause after an unchanged member.
    ASSERT_TRUE(w.Exec(Cmd::RetryBatchMember::With({{"member_id","point_1"}})).value("ok",false));Wait(w);
    const auto retry=w.Current().batchResult.typed();EXPECT_EQ(w.Current().history.size(),2u);EXPECT_NE(w.Current().selectedRun,originalId);EXPECT_EQ(retry->parentRun,originalId);EXPECT_EQ(retry->projectSnapshot,original->projectSnapshot);EXPECT_EQ(retry->rows[0],original->rows[0]);EXPECT_EQ(retry->rows[1]->attempt,2);EXPECT_EQ(retry->actionMembers[1]->previous,original->actionMembers[1]);EXPECT_EQ(original->rows[1]->attempt,1);EXPECT_EQ(retry->Member(1,1),original->Member(1));EXPECT_EQ(retry->Member(1,1)->summary,original->rows[1]);EXPECT_FALSE(retry->Member(0,2));
    w.Current().selectedAttempt=2;ASSERT_TRUE(w.Exec(Cmd::SelectRun::Make(w.Current().id,originalId)).value("ok",false));EXPECT_EQ(w.Current().batchResult.typed(),original);EXPECT_EQ(w.Current().selectedAttempt,0);
    const auto saved=w.Workspace();EXPECT_NO_THROW(ValidateWorkspace(saved));
    ASSERT_TRUE(w.Exec(Cmd::UpdateRun::With({{"run_id",originalId},{"pinned",true}})).value("ok",false));EXPECT_TRUE(w.resultCompare.empty());w.resultCompare.insert({w.Current().id,originalId,0,int(ActionKind::Iv),1});ASSERT_TRUE(w.Exec(Cmd::ClearHistory::With()).value("ok",false));EXPECT_EQ(w.Current().history.size(),1u);EXPECT_EQ(w.Current().batchResult.typed(),original);EXPECT_EQ(w.resultCompare.size(),1u);
}
TEST(ActionBatch, RecorderExtractNeverAcquiresAndCalculateMissingCreatesLinkedRun) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));auto plan=CreateRef<RunPlan>();ActionBatch(w,plan);Wait(w);const auto original=w.Current().batchResult.typed();const auto id=w.Current().selectedRun;
    auto recorder=CreateRef<RecorderDraft>();scaps_core::RecorderColumn c;c.id="C_0V_1234Hz";c.quantity="C";c.voltage_V=0;c.frequency_Hz=1234;recorder->columns.push_back(c);scaps_core::validate_recorder_columns(recorder->columns);w.Current().SetRecorder(recorder);
    ASSERT_TRUE(w.Exec(Cmd::RecordBatch::With()).value("ok",false));Wait(w);const auto extracted=w.Current().batchResult.typed();EXPECT_EQ(w.Current().history.size(),1u);EXPECT_EQ(extracted->actionMembers[0]->results[0].second,original->actionMembers[0]->results[0].second);EXPECT_FALSE(std::isfinite(extracted->rows[0]->values.at(c.id).value));EXPECT_EQ(extracted->rows[0]->values.at(c.id).status,"not sampled");
    auto changed=CreateRef<ProjectDraft>(*w.Current().draft);changed->project.temperature_K=370;w.Current().SetDraft(changed);
    ASSERT_TRUE(w.Exec(Cmd::RecordBatch::With({{"calculate_missing",true}})).value("ok",false));Wait(w);const auto calculated=w.Current().batchResult.typed();EXPECT_EQ(w.Current().history.size(),2u);EXPECT_EQ(calculated->parentRun,id);EXPECT_EQ(calculated->projectSnapshot,original->projectSnapshot);EXPECT_EQ(calculated->rows[0]->values.at(c.id).status,"ok");EXPECT_TRUE(std::isfinite(calculated->rows[0]->values.at(c.id).value));EXPECT_FALSE(original->rows[0]->values.contains(c.id));
}
TEST(ActionBatch, CurvesAndProfilesOffKeepOnlyRequestedScalars) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));ActionBatch(w,CreateRef<RunPlan>(),1,false);Wait(w);const auto result=w.Current().batchResult.typed();EXPECT_TRUE(result->actionMembers[0]->results.empty());EXPECT_TRUE(result->rows[0]->iv.empty());EXPECT_TRUE(result->rows[0]->ac.empty());EXPECT_EQ(result->rows[0]->values.at("J_at_0V").status,"ok");
    auto recorder=CreateRef<RecorderDraft>();scaps_core::RecorderColumn missing;missing.id="not_sampled";missing.quantity="J";missing.voltage_V=.005;recorder->columns.push_back(missing);scaps_core::validate_recorder_columns(recorder->columns);w.Current().SetRecorder(recorder);ASSERT_TRUE(w.Exec(Cmd::RecordBatch::With()).value("ok",false));Wait(w);EXPECT_EQ(w.Current().batchResult.typed()->rows[0]->values.at("not_sampled").status,"not stored");
    Workflow stopped(CreateRef<JobSystem>(nullptr,1));auto plan=CreateRef<RunPlan>();plan->actions[0].include=true;plan->pauseAfterAction=true;ActionBatch(stopped,plan,1,false);const auto paused=stopped.ActiveOp();WaitPaused(stopped,paused);ASSERT_TRUE(stopped.Exec(Cmd::Cancel::With()).value("ok",false));while(stopped.Pending()){stopped.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(stopped.Current().batchResult.typed());EXPECT_TRUE(stopped.Current().batchResult.typed()->actionMembers[0]->results.empty());EXPECT_EQ(stopped.Current().batchResult.typed()->rows[0]->status,"Cancelled");
}
TEST(ActionBatch, RecorderProfilesUseTypedEngineFieldsAndRespectStorageFlags) {
    for(bool profiles:{false,true}) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));auto recorder=CreateRef<RecorderDraft>();scaps_core::RecorderColumn c;c.id="psi_at_0V";c.quantity="profile";c.field="psi_V";c.voltage_V=0;c.x_m=0;recorder->columns.push_back(c);scaps_core::validate_recorder_columns(recorder->columns);w.Current().SetRecorder(recorder);
        ActionBatch(w,CreateRef<RunPlan>(),1,false,profiles);Wait(w);const auto table=w.Current().batchResult.typed();ASSERT_EQ(table->rows[0]->values.at(c.id).status,"ok");EXPECT_TRUE(std::isfinite(table->rows[0]->values.at(c.id).value));EXPECT_EQ(table->actionMembers[0]->recorderSamples.empty(),!profiles);
        if(profiles){ASSERT_EQ(table->actionMembers[0]->recorderSamples.size(),1u);const auto& fields=table->actionMembers[0]->recorderSamples[0].profiles->fields;ASSERT_EQ(fields.size(),1u);EXPECT_TRUE(fields.contains("psi_V"));for(const auto& [kind,result]:table->actionMembers[0]->results)if(auto iv=std::dynamic_pointer_cast<const IvResult>(result))for(const auto& p:iv->points)EXPECT_FALSE(p.x_m.empty());const auto saved=w.Workspace();EXPECT_NO_THROW(ValidateWorkspace(saved));}
    }
}
TEST(ActionBatch, FailedMemberDoesNotDiscardOtherCompletedMembers) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());auto config=CreateRef<BatchDraft>();config->axes[0].values=std::vector<double>{300,-1,315};w.Current().SetBatch(config);ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(CreateRef<RunPlan>())).value("ok",false));ASSERT_TRUE(w.Exec(Cmd::RunBatch::With()).value("ok",false));Wait(w);const auto table=w.Current().batchResult.typed();ASSERT_EQ(table->rows.size(),3u);EXPECT_EQ(table->rows[1]->status,"Failed");EXPECT_FALSE(table->rows[1]->message.empty());EXPECT_FALSE(table->actionMembers[0]->results.empty());EXPECT_FALSE(table->actionMembers[2]->results.empty());EXPECT_EQ(w.Current().history.size(),1u);
}
TEST(ActionBatch, ExplicitPhotovoltaicColumnDoesNotRequireAnIncludedIvSweep) {
    for(bool knownPower:{false,true}) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;auto draft=CreateRef<ProjectDraft>(*SmallDraft());draft->project.illumination.enabled=true;draft->project.illumination.uniform_generation_m3s=1e23;w.Current().SetDraft(draft);
    auto config=CreateRef<BatchDraft>();config->axes[0].values=std::vector<double>{300};if(knownPower)config->incident_power_W_m2=1000;w.Current().SetBatch(config);auto recorder=CreateRef<RecorderDraft>();recorder->columns.clear();scaps_core::RecorderColumn c;c.id=c.quantity="Voc";recorder->columns.push_back(c);scaps_core::validate_recorder_columns(recorder->columns);w.Current().SetRecorder(recorder);
    auto plan=CreateRef<RunPlan>();plan->actions[1].include=false;plan->actions[0].include=true;ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));ASSERT_TRUE(w.Exec(Cmd::RunBatch::With()).value("ok",false));Wait(w);const auto table=w.Current().batchResult.typed();ASSERT_TRUE(table->rows[0]->photovoltaic_requested);EXPECT_EQ(bool(table->rows[0]->photovoltaic),knownPower);EXPECT_EQ(table->actionMembers[0]->results.size(),1u);EXPECT_TRUE(table->rows[0]->iv.empty());
    scaps_core::BatchSpec spec;spec.voltages_V={0.};spec.columns=recorder->columns;spec.photovoltaic=true;spec.incident_power_W_m2=config->incident_power_W_m2;const auto direct=scaps_core::run_batch_member(draft->project,spec);EXPECT_EQ(table->rows[0]->values.at("Voc").status,direct.values.at("Voc").status);if(std::isfinite(direct.values.at("Voc").value))EXPECT_DOUBLE_EQ(table->rows[0]->values.at("Voc").value,direct.values.at("Voc").value);
    }
}
TEST(ActionBatch, SingleShotRecorderUsesTypedFrozenResults) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(SmallDraft());ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Wait(w);const auto source=w.Current().Iv();ASSERT_TRUE(w.Exec(Cmd::RecordBatch::With()).value("ok",false));Wait(w);ASSERT_TRUE(w.Current().batchResult.typed());EXPECT_EQ(ActionOf(*w.Current().batchResult.typed()->actionMembers[0],ActionKind::Iv),source);EXPECT_EQ(w.Current().history.size(),1u);EXPECT_EQ(w.Current().RunResult(w.Current().selectedRun),source);
}
TEST(ActionBatch, ResultsTableAndCurvesRenderWithoutJsonViewsOrIdConflicts) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,1100};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontDefault();io.Fonts->Build();
    {Workflow w(CreateRef<JobSystem>(nullptr,1));ActionBatch(w,SmallActions(),3);Wait(w);const auto stored=w.Current().batchResult.typed();Panel browser(&w,18);scaps_core::boundary_probe::result_adapters=0;
        for(int frame=0;frame<4;++frame){ImGui::NewFrame();ImGui::SetNextWindowSize({1300,1000});ImGui::Begin("Results test");browser.RenderContent();ImGui::End();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);}
        EXPECT_EQ(w.Current().selectedMember,0);EXPECT_EQ(scaps_core::boundary_probe::result_adapters,0u);EXPECT_FALSE(stored->HasView());for(const auto& m:stored->actionMembers)for(const auto& [k,r]:m->results)EXPECT_FALSE(r->HasView());
    }ImPlot::DestroyContext();ImGui::DestroyContext();
}
TEST(ActionBatch, ParallelStopRetainsCompletedMembersAndMarksUnstartedMembers) {
    Workflow w(CreateRef<JobSystem>(nullptr,4));ActionBatch(w,CreateRef<RunPlan>(),5000,false,false);const auto active=w.ActiveOp();ASSERT_TRUE(active);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);bool completed=false;
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){{std::lock_guard guard(active->feed->lock);completed=!active->feed->members.empty();}if(completed)break;w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(completed);ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).value("ok",false));
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_FALSE(w.Pending());
    const auto result=w.Current().batchResult.typed();ASSERT_TRUE(result);EXPECT_TRUE(result->cancelled);
    EXPECT_GT(std::count_if(result->rows.begin(),result->rows.end(),[](const auto& row){return row->status=="Converged";}),0);
    EXPECT_GT(std::count_if(result->rows.begin(),result->rows.end(),[](const auto& row){return row->status=="Not started";}),0);
    for(const auto& row:result->rows)EXPECT_NE(row->status,"Running");for(const auto& member:result->actionMembers){EXPECT_TRUE(member->results.empty());EXPECT_TRUE(member->recorderSamples.empty());}
}
TEST(WorkspaceV4, LegacyHistoryAndStandaloneResultsAreRetainedLosslessly) {
    Workflow source(CreateRef<JobSystem>(nullptr,1));source.lightResponse=true;source.Current().SetDraft(SmallDraft());
    for(int i=0;i<3;++i){ASSERT_TRUE(source.Dispatch({{"operation","run"}}).value("ok",false));Wait(source);}
    auto old=source.Workspace();old["schema_version"]=3;old["ui"]["history_retention"]=1;old["future_workspace"]={{"nullable",nullptr},{"version","004"}};
    auto& device=old["devices"][0];device["future_device"]="keep";device["selection"]["future_selection"]=true;
    device["history"][0]["pinned"]=true;device["history"][0]["name"]="Original";device["history"][0]["future_run"]={1,2};
    const auto original=device["history"];device["history"].erase(2);device["selected_run"]=device["history"][0]["run_id"];const auto standalone=device["result"];
    Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;loaded.SetWorkspace(old);
    const auto saved=loaded.Workspace();const auto& after=saved.at("devices")[0];
    EXPECT_EQ(saved.at("schema_version"),4);EXPECT_EQ(saved.at("future_workspace"),old.at("future_workspace"));EXPECT_EQ(after.at("future_device"),"keep");EXPECT_TRUE(after.at("selection").at("future_selection"));
    ASSERT_EQ(after.at("history").size(),3u);
    for(int i=0;i<2;++i){auto expected=original[i];expected["legacy"]=true;EXPECT_EQ(after.at("history")[i],expected);}
    EXPECT_EQ(after.at("history")[2].at("result"),standalone);EXPECT_EQ(after.at("history")[2].at("label"),"Legacy current result / iv");
    EXPECT_EQ(after.at("selected_run"),device.at("selected_run"));EXPECT_NO_THROW(ValidateWorkspace(saved));
}
TEST(WorkspaceV4, FirstOverwriteBacksUpOriginalAndV4BytesAreStable) {
    const auto folder=std::filesystem::temp_directory_path()/("slux3d-migration-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);const auto path=folder/"old.scapslab";
    Workflow source(CreateRef<JobSystem>(nullptr,1));source.lightResponse=true;source.Current().SetDraft(SmallDraft());ASSERT_TRUE(source.Dispatch({{"operation","run"}}).value("ok",false));Wait(source);
    auto old=source.Workspace();old["schema_version"]=3;const auto original=WorkspaceText(PackResults(old));std::ofstream(path,std::ios::binary)<<original;
    const auto reserved=std::filesystem::path(path.string()+".v3-original");std::ofstream(reserved)<<"do not replace";
    const auto read=[](const auto& file){std::ifstream in(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path.string()}}).value("ok",false));Wait(w);EXPECT_EQ(read(path),original);
    ASSERT_TRUE(w.Dispatch({{"operation","project_save"},{"path",path.string()}}).value("ok",false));Wait(w);const auto first=read(path);const auto file=Json::parse(first);
    EXPECT_EQ(file.at("schema_version"),4);EXPECT_EQ(read(reserved),"do not replace");EXPECT_EQ(read(path.string()+".v3-original-1"),original);
    ASSERT_EQ(file.at("devices")[0].at("stored_results").size(),1u);EXPECT_EQ(file.at("devices")[0].at("result"),file.at("devices")[0].at("history")[0].at("result"));
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path.string()}}).value("ok",false));Wait(w);
    EXPECT_EQ(w.Current().result.typed(),w.Current().RunResult(w.Current().history[0].at("run_id")));
    ASSERT_TRUE(w.Dispatch({{"operation","project_save"},{"path",path.string()}}).value("ok",false));Wait(w);EXPECT_EQ(read(path),first);
    auto invalid=file;invalid["devices"][0]["result"]["$result_ref"]="missing";const auto before=w.Workspace();EXPECT_THROW(w.SetWorkspace(invalid),std::exception);EXPECT_EQ(w.Workspace(),before);
    invalid=before;invalid["devices"][0]["use_run_actions"]="yes";EXPECT_THROW(w.SetWorkspace(invalid),std::exception);EXPECT_EQ(w.Workspace(),before);
    invalid=before;invalid["devices"][0]["actions"]["iv"]["include"]="yes";EXPECT_THROW(w.SetWorkspace(invalid),std::exception);EXPECT_EQ(w.Workspace(),before);
}
TEST(WorkspaceV4, AttemptAndActionSelectionSurviveSaveLoad) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));ActionBatch(w,CreateRef<RunPlan>(),2);Wait(w);ASSERT_TRUE(w.Dispatch({{"operation","retry_batch"},{"member_id","point_1"}}).value("ok",false));Wait(w);
    const auto run=w.Current().selectedRun;ASSERT_TRUE(w.Dispatch({{"operation","history_select"},{"run_id",run},{"member_index",1},{"attempt",1},{"action","iv"}}).value("ok",false));
    auto data=w.Workspace();Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.SetWorkspace(data);
    EXPECT_EQ(loaded.Current().selectedRun,run);EXPECT_EQ(loaded.Current().selectedMember,1);EXPECT_EQ(loaded.Current().selectedAttempt,1);EXPECT_EQ(loaded.project.ui.at("results_action"),"iv");
    EXPECT_TRUE(loaded.Current().runPlanEdited);
    const auto selected=loaded.Current().batchResult.typed()->Member(1,1);ASSERT_TRUE(selected);EXPECT_EQ(selected->summary->attempt,1);
    EXPECT_FALSE(loaded.Dispatch({{"operation","history_select"},{"run_id",run},{"member_index",99}}).value("ok",true));EXPECT_EQ(loaded.Current().selectedMember,1);
    ASSERT_TRUE(loaded.Dispatch({{"operation","history_select"},{"run_id",run},{"member_index",1},{"attempt",0}}).value("ok",false));Workflow latest(CreateRef<JobSystem>(nullptr,1));latest.SetWorkspace(loaded.Workspace());EXPECT_EQ(latest.Current().selectedAttempt,0);
}
TEST(WorkspaceV4, NativeCurveFileAdapterIsLosslessAndKeepsUnknownPointData) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));ActionBatch(w,CreateRef<RunPlan>(),1);Wait(w);
    const auto iv=std::dynamic_pointer_cast<const IvResult>(ActionOf(*w.Current().batchResult.typed()->actionMembers[0],ActionKind::Iv));ASSERT_TRUE(iv);ASSERT_FALSE(iv->curve.empty());EXPECT_TRUE(iv->points.empty());
    auto json=iv->Dump();auto decoded=IvFromJson(json);EXPECT_FALSE(decoded.curve.empty());EXPECT_EQ(decoded.Dump().dump(),json.dump());
    json["points"][0]["future_point"]="keep";decoded=IvFromJson(json);EXPECT_TRUE(decoded.curve.empty());EXPECT_EQ(decoded.Dump().dump(),json.dump());
    json["points"][0].erase("future_point");json["points"][0]["bias_id"]="stable-custom-bias";decoded=IvFromJson(json);EXPECT_TRUE(decoded.curve.empty());EXPECT_EQ(decoded.Dump().dump(),json.dump());
}
TEST(WorkspaceV4, LegacyDeviceYamlOverwriteKeepsOriginalBytes) {
    const auto file=std::filesystem::temp_directory_path()/("slux3d-yaml-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".scapslab");const auto original=scaps_core::serialize_device_project_yaml(SmallDraft()->project);std::ofstream(file,std::ios::binary)<<original;
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",file.string()}}).value("ok",false));Wait(w);const auto before=w.Current().Document();
    ASSERT_TRUE(w.Dispatch({{"operation","project_save"},{"path",file.string()}}).value("ok",false));Wait(w);
    std::ifstream backup(file.string()+".v1-original",std::ios::binary);EXPECT_EQ(std::string(std::istreambuf_iterator<char>(backup),{}),original);
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",file.string()}}).value("ok",false));Wait(w);EXPECT_EQ(w.Current().Document(),before);
}

TEST(OwnerUi, NewDefectsUseCurrentEngineDistributionDefaults) {
    EXPECT_EQ(NewDefect(false).at("energy_levels"),scaps_core::ProjectDefect{}.energy_levels);
    EXPECT_EQ(NewDefect(false).at("energy_quadrature"),scaps_core::ProjectDefect{}.energy_quadrature);
    EXPECT_EQ(NewDefect(true).at("energy_levels"),scaps_core::ProjectInterfaceDefect{}.energy_levels);
    EXPECT_EQ(NewDefect(true).at("energy_quadrature"),scaps_core::ProjectInterfaceDefect{}.energy_quadrature);
    auto draft=*SmallDraft();const auto bulk=AddDraftDefect(draft,0,false),sheet=AddDraftDefect(draft,0,true);
    EXPECT_EQ(draft.project.layers[0].defects[bulk].energy_levels,scaps_core::ProjectDefect{}.energy_levels);
    EXPECT_EQ(draft.project.interface_defects[sheet].energy_quadrature,scaps_core::ProjectInterfaceDefect{}.energy_quadrature);
}
TEST(OwnerUi, DistributionAndPlotControlsRenderWithoutChangingImportedValuesOrConflictingIds) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1800,1400};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        Workflow numeric(CreateRef<JobSystem>(nullptr,1)),plots(CreateRef<JobSystem>(nullptr,1));numeric.lightResponse=plots.lightResponse=true;
        auto draft=CreateRef<ProjectDraft>(*SmallDraft());AddDraftDefect(*draft,0,false);AddDraftDefect(*draft,1,false);AddDraftDefect(*draft,0,true);
        for(auto& l:draft->project.layers)for(auto& d:l.defects){d.energy_distribution="gaussian";d.energy_levels=63;d.energy_quadrature="gauss_legendre";}
        draft->project.interface_defects[0].energy_distribution="gaussian";draft->project.interface_defects[0].energy_levels=63;draft->project.interface_defects[0].energy_quadrature="gauss_legendre";
        numeric.Current().SetDraft(draft);plots.Current().SetDraft(SmallDraft());ASSERT_TRUE(plots.Dispatch({{"operation","band"},{"voltage_V",0.}}).value("ok",false));Wait(plots);plots.plotSession.settings.grFollow=false;
        Panel settings(&numeric,3),bands(&plots,5),gr(&plots,22);
        for(int tab:{0,1})for(int frame=0;frame<3;++frame){plots.plotSession.settings.grTab=tab;ImGui::NewFrame();
            for(auto [name,panel]:{std::pair{"Numerical owner",&settings},{"Bands owner",&bands},{"G/R owner",&gr}}){ImGui::SetNextWindowSize({620,850});ImGui::Begin(name);panel->RenderContent();ImGui::End();}
            ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);
        }
        EXPECT_EQ(numeric.Current().draft,draft);EXPECT_EQ(draft->project.layers[0].defects[0].energy_levels,63);EXPECT_EQ(draft->project.interface_defects[0].energy_quadrature,"gauss_legendre");EXPECT_FALSE(plots.plotSession.settings.contactAnnotations.value_or(false));
        auto next=CreateRef<ProjectDraft>(*draft);SetProjectField(next->project,"layers[0].defects[0].energy_levels",std::int64_t(11));SetProjectField(next->project,"layers[0].defects[0].energy_quadrature",std::string("scaps_trapezoid"));ASSERT_TRUE(numeric.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(numeric);
        const auto roundtrip=DraftFromDocument(DraftDocument(*numeric.Current().draft));EXPECT_EQ(roundtrip->project.layers[0].defects[0].energy_levels,11);EXPECT_EQ(roundtrip->project.layers[0].defects[0].energy_quadrature,"scaps_trapezoid");
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(OwnerUi, LiveReferenceKeepsThePreviousExactTypedResultsUntilRelease) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();for(auto& a:plan->actions){a.include=a.kind<ActionKind::Dlcp;a.onEdit=false;}
    plan->actions[1].range=SweepRange{SweepSampling::List,false,0,0,1,1,{0.,.01}};
    plan->actions[2].range=SweepRange{SweepSampling::List,false,0,0,1,1,{0.,.01}};
    plan->actions[3].range=SweepRange{SweepSampling::List,false,0,0,1,1,{1000.,2000.}};
    plan->actions[4].range=SweepRange{SweepSampling::List,false,0,0,1,1,{500.,600.}};
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(w);ASSERT_TRUE(w.response.value("ok",false));
    const auto iv=w.Current().result.typed(),band=w.Current().bandResult.typed();const auto cv=w.Current().acResults.get("cv");const auto history=w.Current().history.size();
    w.dragging=true;auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=310;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);
    EXPECT_EQ(w.Current().changeIv,iv);EXPECT_EQ(w.Current().changeBand,band);EXPECT_EQ(w.Current().changeAc.at("cv"),cv);
    ASSERT_TRUE(w.Dispatch({{"operation","band"},{"live",true},{"voltage_V",0.},{"rev",w.Current().revision}}).value("ok",false));Wait(w);
    EXPECT_EQ(w.Current().changeBand,band);EXPECT_NE(w.Current().bandResult.typed(),band);
    next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=320;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Wait(w);
    EXPECT_EQ(w.Current().changeIv,iv);EXPECT_EQ(w.Current().changeBand,band);EXPECT_EQ(w.Current().history.size(),history);
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1800,1400};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontDefault();io.Fonts->Build();
    for(int view:{5,6,7,8,9,10,19,22}){Panel panel(&w,view);for(int frame=0;frame<2;++frame){ImGui::NewFrame();ImGui::SetNextWindowSize({1100,1000});ImGui::Begin("Live reference owner");panel.RenderContent();
        if(view==10 || view==6 || view==8){const auto plot=ImPlot::GetPlot(view==10?"I-V compare":view==6?"C-V":"C-f");EXPECT_NE(plot,nullptr);if(plot){int references=0;for(int i=0;i<plot->Items.GetLegendCount();++i)if(std::string_view(plot->Items.GetLegendLabel(i)).find("(before)")!=std::string_view::npos){++references;const auto colour=plot->Items.GetLegendItem(i)->Color;const auto alpha=(colour>>IM_COL32_A_SHIFT)&255u;EXPECT_EQ(alpha,255u);EXPECT_EQ(colour,ImGui::ColorConvertFloat4ToU32(SingleShotColour));}EXPECT_EQ(references,view==10?1:2);}}
        ImGui::End();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);}}
    ImPlot::DestroyContext();ImGui::DestroyContext();
    EXPECT_EQ(w.Current().history.size(),history);w.dragging=false;
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(w);ASSERT_TRUE(w.response.value("ok",false));
    EXPECT_FALSE(w.Current().changeIv);EXPECT_FALSE(w.Current().changeBand);EXPECT_TRUE(w.Current().changeAc.empty());
}
TEST(OwnerUi, CancelledExactSolveKeepsLastLiveCurveUntilReplacement) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());
    auto &d=w.Current();d.previewV={0,.01};d.previewJ={123,124};
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));
    EXPECT_EQ(d.previewJ,(std::vector<double>{123,124}));
    ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).value("ok",false));ASSERT_TRUE(w.jobs->ResumeAllJobs());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());EXPECT_EQ(d.previewJ,(std::vector<double>{123,124}));
}
TEST(OwnerUi, BiasNavigationAnchorsReferenceAndUsesStoredProfilesWithoutJobs) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(SmallDraft());
    auto plan=CreateRef<RunPlan>();plan->actions[1].range=SweepRange{SweepSampling::Points,false,0,.02,.01,3};
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Wait(w);auto& d=w.Current();const auto shot=d.result.typed();ASSERT_EQ(shot->points.size(),3u);
    ASSERT_TRUE(w.Exec(Cmd::SelectRun::Make(d.id,"")).value("ok",false));
    ASSERT_TRUE(w.Exec(Cmd::SelectBias::Make(1)).value("ok",false));EXPECT_FALSE(w.Busy());EXPECT_EQ(d.changeIv,shot);EXPECT_EQ(d.changeBias,0);EXPECT_EQ(d.ChangePoint(),&shot->points[0]);
    ASSERT_TRUE(w.Exec(Cmd::SelectBias::Make(2)).value("ok",false));EXPECT_FALSE(w.Busy());EXPECT_EQ(d.ChangePoint(),&shot->points[0]);EXPECT_EQ(d.ProfileSweep()->points[2].voltage_V,.02);EXPECT_TRUE(shot->points[2].details);EXPECT_EQ(d.history.size(),1u);
}

TEST(WorkspaceV4, ActionRangeUnknownFieldsAndDisabledMemberPauseSurviveRoundtrip) {
    RunPlan initial;initial.actions[1].range=SweepRange{SweepSampling::Points,false,0,.1,.01,3};auto original=RunPlanJson(initial);original["iv"]["pause_after_member"]=true;original["iv"]["range"]["future_range_metadata"]={{"owner","keep"}};
    auto plan=ReadRunPlan(original);ASSERT_TRUE(plan.pauseAfterMember);plan.pauseAfterMember=false;
    const auto saved=RunPlanJson(plan,original);EXPECT_FALSE(saved.at("iv").at("pause_after_member"));EXPECT_EQ(saved.at("iv").at("range").at("future_range_metadata"),original.at("iv").at("range").at("future_range_metadata"));EXPECT_FALSE(ReadRunPlan(saved).pauseAfterMember);
}
