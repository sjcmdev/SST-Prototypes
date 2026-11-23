#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include "Panel.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <thread>
#include <fstream>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Ref<const ProjectDraft> ResumeDraft(){auto p=scaps_core::make_default_device_project();p.layers.resize(2);for(auto& l:p.layers)l.defects.clear();p.interface_defects.clear();p.mesh.points_per_layer=11;p.voltage={0,.01,.01};return DraftFromProject(std::move(p));}
}
TEST(Resume, SparseProfileDefaultsPreservePresenceAndUndo) {
    auto document=DraftDocument(*ResumeDraft());
    document["layers"][0]["grading"]["Na_m3"]={{"law","linear"},{"left",1e18},{"right",2e18}};
    const auto original=DraftFromDocument(document);EXPECT_EQ(DraftDocument(*original),document);
    auto next=CreateRef<ProjectDraft>(*original);auto& profile=*next->project.layers[0].grading->Na_m3;
    profile.law=scaps_core::GradingLaw::Gaussian;
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,profile,"gaussian_center",.5),.5);
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,profile,"gaussian_width",.1),.1);
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,profile,"background",0.),0.);
    profile.law=scaps_core::GradingLaw::GaussDiffusion;
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,profile,"dose_m2",1e14),1e14);
    profile.law=scaps_core::GradingLaw::ErfcDiffusion;
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,profile,"surface_density_m3",1e20),1e20);
    const auto saved=DraftDocument(*next);EXPECT_TRUE(saved["layers"][0]["grading"]["Na_m3"].contains("background"));
    const auto loaded=DraftFromDocument(saved);EXPECT_EQ(loaded->profileFields,next->profileFields);
    const auto delta=CompareDrafts(*original,*next);const auto undone=delta.Apply(*next,false);
    EXPECT_EQ(DraftDocument(*undone),document);EXPECT_EQ(undone->profileFields,original->profileFields);
    EXPECT_EQ(DraftDocument(*delta.Apply(*undone,true)),saved);
    const auto fields=next->profileFields;EditLayer(*next,"move",next->layers[0].id,1);
    for(const auto& field:fields)if(field.starts_with("/layers/0/"))EXPECT_TRUE(next->profileFields.contains("/layers/1/"+field.substr(10)));
    auto& moved=*next->project.layers[1].grading->Na_m3;
    EXPECT_DOUBLE_EQ(ProfileParameter(*next,moved,"dose_m2",999.),1e14);
}
TEST(Resume, ExplicitProfileValuesEqualToEngineDefaultsAreNotFallbacks) {
    auto document=DraftDocument(*ResumeDraft());
    document["layers"][0]["grading"]["Na_m3"]={{"law","linear"},{"left",1e18},{"right",2e18},{"gaussian_center",0.},{"gaussian_width",1.},{"dose_m2",0.},{"surface_density_m3",0.}};
    auto draft=CreateRef<ProjectDraft>(*DraftFromDocument(document));auto& profile=*draft->project.layers[0].grading->Na_m3;
    EXPECT_DOUBLE_EQ(ProfileParameter(*draft,profile,"gaussian_center",.5),0.);
    EXPECT_DOUBLE_EQ(ProfileParameter(*draft,profile,"gaussian_width",.1),1.);
    EXPECT_DOUBLE_EQ(ProfileParameter(*draft,profile,"dose_m2",1e14),0.);
    EXPECT_DOUBLE_EQ(ProfileParameter(*draft,profile,"surface_density_m3",1e20),0.);
    EXPECT_EQ(DraftDocument(*draft),document);
    document["layers"][0]["grading"]["Na_m3"].erase("gaussian_center");
    const auto patched=PatchDraft(*DraftFromDocument(document),Json::object(),Json::array({{{"path","/layers/0/grading/Na_m3/gaussian_center"},{"value",0.}}}));
    auto edited=*patched;EXPECT_DOUBLE_EQ(ProfileParameter(edited,*edited.project.layers[0].grading->Na_m3,"gaussian_center",.5),0.);
    EXPECT_TRUE(DraftDocument(edited)["layers"][0]["grading"]["Na_m3"].contains("gaussian_center"));
}
namespace {
void WaitProfile(Workflow& w) {
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.value("ok",false))<<w.response;
}
}
TEST(Resume, ProfileCommandsReplacePresenceAndUndoBeforePanelLawChange) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(ResumeDraft());
    const auto layer=w.Current().draft->layers[0].id;
    const auto set=[&](Json profile){ASSERT_TRUE(w.Exec(Cmd::SetProfile::With({{"layer_id",layer},{"parameter_id","Na_m3"},{"profile",std::move(profile)}})).value("ok",false));WaitProfile(w);};
    set({{"law","linear"},{"left",1e18},{"right",2e18},{"gaussian_center",0.},{"gaussian_width",1.},{"dose_m2",0.},{"surface_density_m3",0.}});
    const auto explicitDraft=w.Current().draft;
    auto panel=*explicitDraft;auto& p=*panel.project.layers[0].grading->Na_m3;
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,p,"gaussian_center",.5),0.);
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,p,"gaussian_width",.1),1.);
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,p,"dose_m2",1e14),0.);
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,p,"surface_density_m3",1e20),0.);
    set({{"law","linear"},{"left",1e18},{"right",2e18}});
    const auto sparseDraft=w.Current().draft;panel=*sparseDraft;
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,*panel.project.layers[0].grading->Na_m3,"gaussian_center",.5),.5);
    panel=*DraftFromDocument(DraftDocument(*sparseDraft));
    EXPECT_DOUBLE_EQ(ProfileParameter(panel,*panel.project.layers[0].grading->Na_m3,"gaussian_center",.5),.5);
    ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));
    EXPECT_EQ(w.Current().draft->profileFields,explicitDraft->profileFields);
    ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));
    EXPECT_EQ(w.Current().draft->profileFields,sparseDraft->profileFields);
    const auto path=std::filesystem::temp_directory_path()/"typed3d-profile.grd";
    {std::ofstream file(path);file<<"position: relative\n0 1e18\n1 2e18\n";}
    ASSERT_TRUE(w.Exec(Cmd::LoadProfile::With({{"layer_id",layer},{"parameter_id","Na_m3"},{"path",path.string()}})).value("ok",false));WaitProfile(w);
    const auto loaded=w.Current().draft;
    EXPECT_TRUE(loaded->profileFields.contains("/layers/0/grading/Na_m3/interpolation_logarithmic"));
    EXPECT_FALSE(loaded->profileFields.contains("/layers/0/grading/Na_m3/gaussian_center"));
    panel=*loaded;EXPECT_DOUBLE_EQ(ProfileParameter(panel,*panel.project.layers[0].grading->Na_m3,"gaussian_width",.1),.1);
    ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Current().draft->profileFields,sparseDraft->profileFields);
    ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));EXPECT_EQ(w.Current().draft->profileFields,loaded->profileFields);
    std::filesystem::remove(path);
}
TEST(Resume, PreviewInterpolationPresenceDoesNotChangeSolverDefault) {
    for(const auto flag:{Json{},Json(false),Json(true)}) {
        auto document=DraftDocument(*ResumeDraft());auto& layers=document["layers"];
        for(auto& layer:layers){layer["thickness_m"]=1e-6;layer["grading"]["Na_m3"]={{"law","table"},{"coordinate",{0.,1.}},{"value",{1e20,3e20}},{"position_relative",true},{"shared_range",true},{"source_file","shared.grd"}};if(!flag.is_null())layer["grading"]["Na_m3"]["interpolation_logarithmic"]=flag;}
        const auto draft=DraftFromDocument(document);const auto& source=*draft->project.layers[1].grading->Na_m3;
        EXPECT_EQ(source.interpolation_logarithmic,flag.is_null() || flag.get<bool>());
        const auto preview=ResolveSharedProfile(source,*draft,draft->layers[1].id,"Na_m3");
        EXPECT_DOUBLE_EQ(ProfileSamples(preview,1e-6).value_SI.front(),!flag.is_null() && flag.get<bool>()?std::sqrt(3.)*1e20:2e20);
        EXPECT_EQ(DraftDocument(*draft),document);
        const auto roundTrip=DraftFromDocument(DraftDocument(*draft));EXPECT_EQ(roundTrip->profileFields,draft->profileFields);
        auto invalid=document;invalid["temperature_K"]=-1.;EXPECT_THROW((void)DraftFromDocument(invalid),std::invalid_argument);
    }
}
namespace {
void CheckProfileFlags(const ProjectDraft& draft,const scaps_core::Profile& expected) {
    ASSERT_TRUE(draft.project.layers[0].grading && draft.project.layers[0].grading->Na_m3);
    const auto& profile=*draft.project.layers[0].grading->Na_m3;
    EXPECT_EQ(profile.position_relative,expected.position_relative);
    EXPECT_EQ(profile.shared_range,expected.shared_range);
    EXPECT_EQ(profile.composition_dependent,expected.composition_dependent);
    EXPECT_EQ(profile.interpolation_logarithmic,expected.interpolation_logarithmic);
    EXPECT_EQ(profile.source_file,expected.source_file);
}
void SaveLoadProfile(Workflow& w,const scaps_core::Profile& expected,const char* name) {
    const auto path=std::filesystem::temp_directory_path()/name;
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(path.string(),false)).value("ok",false));WaitProfile(w);
    ASSERT_TRUE(w.Exec(Cmd::LoadProject::Make(path.string())).value("ok",false));WaitProfile(w);
    CheckProfileFlags(*w.Current().draft,expected);
    auto edited=CreateRef<ProjectDraft>(*w.Current().draft);edited->project.temperature_K+=1.;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(edited)).value("ok",false));WaitProfile(w);
    ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));CheckProfileFlags(*w.Current().draft,expected);
    ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));CheckProfileFlags(*w.Current().draft,expected);std::filesystem::remove(path);
}
}
TEST(Resume, ImportedProfileFlagsSurviveSaveLoadAndUndoRedo) {
    for(bool setFile:{false,true})for(bool relative:{false,true})for(bool shared:{false,true})for(bool logarithmic:{false,true}) {
        SCOPED_TRACE(::testing::Message()<<"set_file="<<setFile<<" relative="<<relative<<" shared="<<shared<<" log="<<logarithmic);
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(ResumeDraft());
        const auto original=w.Current().draft;const auto path=std::filesystem::temp_directory_path()/"typed3e-import.grd";
        {std::ofstream file(path);file<<"position: "<<(relative?"relative":"absolute")<<"\nrange: "<<(shared?"shared":"this layer only")<<"\ninterpolation: "<<(logarithmic?"logarithmic":"linear")<<"\n0 1e18\n1 3e18\n";}
        const auto expected=ReadProfileFile(path.string(),true).profile;
        const auto layer=original->layers[0].id;
        if(setFile){ASSERT_TRUE(w.Exec(Cmd::SetProfile::With({{"layer_id",layer},{"parameter_id","Na_m3"},{"profile",{{"file",path.string()}}}})).value("ok",false));}
        else {ASSERT_TRUE(w.Exec(Cmd::LoadProfile::With({{"layer_id",layer},{"parameter_id","Na_m3"},{"path",path.string()}})).value("ok",false));}
        WaitProfile(w);CheckProfileFlags(*w.Current().draft,expected);
        ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));EXPECT_EQ(DraftDocument(*w.Current().draft),DraftDocument(*original));
        ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));CheckProfileFlags(*w.Current().draft,expected);
        std::filesystem::remove(path);SaveLoadProfile(w,expected,"typed3e-import.scapslab");
    }
}
TEST(Resume, ImportedProfileTogglesSurvivePanelUndoRedoAndSaveLoad) {
    auto* gui=ImGui::CreateContext();ImPlot::CreateContext();
    struct Cleanup {~Cleanup(){ImPlot::DestroyContext();ImGui::DestroyContext();}} cleanup;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,1400};io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(ResumeDraft());
    const auto path=std::filesystem::temp_directory_path()/"typed3e-toggle.grd";
    {std::ofstream file(path);file<<"position: absolute\nrange: this layer only\ninterpolation: linear\n0 1e18\n1 3e18\n";}
    ASSERT_TRUE(w.Exec(Cmd::LoadProfile::With({{"layer_id",w.Current().draft->layers[0].id},{"parameter_id","Na_m3"},{"path",path.string()}})).value("ok",false));WaitProfile(w);
    auto expected=*w.Current().draft->project.layers[0].grading->Na_m3;Panel panel(&w,21);panel.SelectGradingParameter("Na_m3");
    auto frame=[&]{io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1100,1300});ImGui::Begin("Profile flags");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);};
    frame();frame();
    for(const auto* label:{"Depends on composition y","Logarithmic interpolation"}) {
        const auto before=expected;const auto row=ImHashStr(label,0,ImGui::FindWindowByName("Profile flags")->GetID("Na_m3"));const auto id=ImHashStr("##value",0,ImHashStr(label,0,row));bool found=false;
        for(float y=30;y<700 && !found;y+=3)for(float x=350;x<450;x+=10){io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==id){found=true;break;}}
        ASSERT_TRUE(found)<<label;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();WaitProfile(w);
        if(std::string_view(label)=="Depends on composition y")expected.composition_dependent=true;else expected.interpolation_logarithmic=true;
        CheckProfileFlags(*w.Current().draft,expected);
        ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));CheckProfileFlags(*w.Current().draft,before);
        ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));CheckProfileFlags(*w.Current().draft,expected);
    }
    std::filesystem::remove(path);SaveLoadProfile(w,expected,"typed3e-toggle.scapslab");
}
TEST(Resume, EmbeddedProfileSourceKeepsSharedIdentityThroughSetSaveLoadAndUndoRedo) {
    auto initial=CreateRef<ProjectDraft>(*ResumeDraft());for(auto& layer:initial->project.layers)layer.thickness_m=1e-6;
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(initial);
    const Json spec={{"law","table"},{"coordinate",{0.,1.}},{"value",{1e18,3e18}},{"position_relative",true},{"shared_range",true},{"source_file","embedded-shared.grd"},{"interpolation_logarithmic",false},{"composition_dependent",false}};
    scaps_core::Profile expected;expected.position_relative=true;expected.shared_range=true;expected.source_file="embedded-shared.grd";
    for(const auto& layer:initial->layers){ASSERT_TRUE(w.Exec(Cmd::SetProfile::With({{"layer_id",layer.id},{"parameter_id","Na_m3"},{"profile",spec}})).value("ok",false));WaitProfile(w);ASSERT_TRUE(w.response.value("ok",false));}
    const auto check=[&]{CheckProfileFlags(*w.Current().draft,expected);const auto& draft=*w.Current().draft;const auto preview=ResolveSharedProfile(*draft.project.layers[1].grading->Na_m3,draft,draft.layers[1].id,"Na_m3");EXPECT_DOUBLE_EQ(ProfileSamples(preview,draft.project.layers[1].thickness_m).value_SI.front(),2e18);};
    check();ASSERT_TRUE(w.Dispatch({{"operation","undo"}}).value("ok",false));ASSERT_TRUE(w.Dispatch({{"operation","redo"}}).value("ok",false));check();
    SaveLoadProfile(w,expected,"typed3e-embedded.scapslab");check();
}
