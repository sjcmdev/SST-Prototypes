#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include <scaps_core/device_project_yaml.hpp>
#include <fstream>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
void Wait(Workflow& w){while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(w.response.value("ok",false))<<w.response;}
Ref<const ProjectDraft> SmallDraft(){auto p=scaps_core::make_default_device_project();p.layers.resize(2);for(auto& l:p.layers)l.defects.clear();p.interface_defects.clear();p.mesh.points_per_layer=11;p.voltage={0,.01,.01};return DraftFromProject(std::move(p));}
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
