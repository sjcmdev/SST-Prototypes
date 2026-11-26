#include <gtest/gtest.h>
#include "Library/Library.hpp"
#include "Library/LibraryUi.hpp"
#include "Library/PythonLibrary.hpp"
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include "Io.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/boundary_probe.hpp>
#include <yaml-cpp/yaml.h>
#include <imgui_internal.h>
#include <implot.h>
#include <bit>
#include <fstream>
#include <thread>

using namespace SST;
using namespace SST::ScapsLab;
namespace Lib=SST::ScapsLab::Library;
namespace {
Assets::Metadata Meta(std::string id="silicon") {Assets::Metadata m;m.id=std::move(id);m.name="Silicon";m.collection="Materials/Elemental";m.tags={"Si","example"};m.changeNote="Initial values";m.provenance={"example","Synthetic unit-test input","","test","assets1","2026-11-25","",""};return m;}
std::string Read(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const std::filesystem::path& file,const std::string& text){std::string error;
    ASSERT_TRUE(Io::Save(file,text,error))<<error;}
struct LibraryFiles:testing::Test {
    std::filesystem::path dir;
    void SetUp() override {const auto root=std::getenv("ASSETS1_EVIDENCE");dir=(root?std::filesystem::path(root):std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/"build/assets1-tests")/testing::UnitTest::GetInstance()->current_test_info()->name()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(dir);}
};
void Finish(Workflow& w){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_FALSE(w.Pending());}
void Do(Workflow& w,Lib::Request r){ASSERT_TRUE(w.Exec(Cmd::LibraryOperation::Make(std::move(r))).value("ok",false));Finish(w);
    ASSERT_TRUE(w.library.errorCode.empty())<<w.library.message;}
std::shared_ptr<const ProjectDraft> Small() {auto p=scaps_core::make_default_device_project();p.mesh.points_per_layer=15;p.voltage={0,.02,.02};for(auto& l:p.layers)l.defects.clear();return DraftFromProject(std::move(p));}
}
TEST(AssetLibrary,ImmutableVersionsSearchReadOnlyAndDerivation) {
    auto c=Lib::Empty("user","User");auto material=std::make_shared<const Lib::Payload>(Lib::Material{});auto first=c.Create(Meta(),material);
    auto changed=std::get<Lib::Material>(*material);changed.values.Eg_eV=1.2;auto m=first->meta;m.changeNote="Updated gap";
    auto second=c.Revise(m,std::make_shared<const Lib::Payload>(changed),1);
    EXPECT_EQ(second->version,2u);
    EXPECT_EQ(std::get<Lib::Material>(*first->payload).values.Eg_eV,1.12);
    EXPECT_THROW(c.Revise(m,material,1),Assets::Error);
    EXPECT_EQ(c.Search({"silicon","Materials","Si"}).size(),1u);
    EXPECT_TRUE(c.Search({"GaAs"}).empty());
    c.SetLocked(m.id,true);
    EXPECT_THROW(c.Revise(m,material,2),Assets::Error);c.SetLocked(m.id,false);
    auto copyMeta=Meta("derived");auto derived=c.Derive(c.id,*second,copyMeta);
    ASSERT_TRUE(derived->meta.basedOn);
    EXPECT_EQ(derived->payload,second->payload);
    EXPECT_EQ(derived->meta.basedOn->version,2u);
    c.scope=Assets::Scope::Builtin;
    EXPECT_THROW(c.Create(Meta("other"),material),Assets::Error);
    EXPECT_THROW(c.SetLocked("silicon",true),Assets::Error);
    auto invalid=Meta("../escape");
    EXPECT_THROW(Assets::Validate(invalid),Assets::Error);invalid=Meta();invalid.validity.temperatureMin_K=400;invalid.validity.temperatureMax_K=300;
    EXPECT_THROW(Assets::Validate(invalid),Assets::Error);
    auto clean=Lib::Empty("clean","Clean");
    EXPECT_THROW(clean.Restore({nullptr}),Assets::Error);
}
TEST(AssetLibrary,BoundsUnitsTablesAndValidityRejectInvalidScientificData) {
    Lib::Material m;m.values.Eg_eV=-1;
    EXPECT_THROW(Lib::ValidatePayload(m),std::exception);m.values.Eg_eV=NAN;
    EXPECT_THROW(Lib::ValidatePayload(m),std::exception);
    Lib::Optical optical;optical.absorption.model="file";optical.absorption.table_nm_m1={{500,1},{400,2}};
    EXPECT_THROW(Lib::ValidatePayload(optical),std::exception);
    auto meta=Meta();meta.parameters.push_back({"layers[0].material.Eg_eV","eV","test",1.,1.2,.02});Lib::Revision r{meta,1,std::make_shared<const Lib::Payload>(Lib::Material{})};
    EXPECT_NO_THROW(Lib::ValidateRevision(r));r.meta.parameters[0].unit="J";
    EXPECT_THROW(Lib::ValidateRevision(r),Assets::Error);
    r.meta.parameters[0].unit="eV";r.meta.parameters[0].upper=1.1;
    EXPECT_THROW(Lib::ValidateRevision(r),Assets::Error);
    r.meta.parameters.clear();r.meta.validity.temperatureMax_K=290;auto draft=Small();
    EXPECT_THROW(Lib::Apply(*draft,r,"user",{draft->layers.front().id}),Assets::Error);
    EXPECT_EQ(Lib::NativeUnit("layers[0].material.Eg_eV"),"eV");
    EXPECT_EQ(Lib::NativeUnit("layers[0].doping.Nd_m3"),"m^-3");
}
TEST_F(LibraryFiles,EveryPayloadHistoryAndExtrasRoundTripAtTheFileEdge) {
    auto c=Lib::Empty("roundtrip","Round trip");const auto seeds=Lib::Builtins();
    for(const auto& [id,history]:seeds->libraries.at("sst-examples").catalog.Items()){auto m=history.back()->meta;m.locked=false;c.Create(m,history.back()->payload);}
    auto material=std::get<Lib::Material>(*c.Find("si-default")->payload);material.bandgapTemperature=Lib::BandgapTemperature{};material.effectiveMass=Lib::EffectiveMass{1.08,.56};auto meta=c.Find("si-default")->meta;meta.changeNote="Add measured law";c.Revise(meta,std::make_shared<const Lib::Payload>(material),1);
    const auto file=dir/"all.sstlibrary.yaml";Lib::Save(c,file);auto restored=Lib::Load(file);
    EXPECT_EQ(restored.catalog.Items().size(),c.Items().size());
    EXPECT_EQ(restored.catalog.Find("si-default")->version,2u);
    EXPECT_EQ(restored.catalog.Items().at("si-default").size(),2u);
    const auto& round=std::get<Lib::Material>(*restored.catalog.Find("si-default")->payload);
    ASSERT_TRUE(round.bandgapTemperature);
    EXPECT_DOUBLE_EQ(round.bandgapTemperature->alpha_eV_K,material.bandgapTemperature->alpha_eV_K);
    ASSERT_TRUE(round.effectiveMass);
    EXPECT_DOUBLE_EQ(round.effectiveMass->holeRatio,.56);
    for(const auto& [id,h]:c.Items()){const auto b=restored.catalog.Find(id);
    EXPECT_STREQ(Lib::Kind(*b->payload),Lib::Kind(*h.back()->payload));
    EXPECT_EQ(ProjectNumbers(Lib::Carrier(*b->payload)),ProjectNumbers(Lib::Carrier(*h.back()->payload)))<<id;
    EXPECT_EQ(b->meta,h.back()->meta);}
    auto next=restored.catalog;auto revision=next.Find("si-default");auto changed=revision->meta;changed.changeNote="Third version";next.Revise(changed,revision->payload,2);Lib::Save(next,file,restored.source.fingerprint);
    EXPECT_EQ(Lib::Load(file).catalog.Find("si-default")->version,3u);
    EXPECT_THROW(Lib::Save(next,file,restored.source.fingerprint),Assets::Error);
    EXPECT_THROW(Lib::Save(next,file),Assets::Error);const auto retained=Read(file);auto invalid=next;invalid.readOnly=true;
    EXPECT_THROW(Lib::Save(invalid,file,Lib::Load(file).source.fingerprint),Assets::Error);
    EXPECT_EQ(Read(file),retained);
}
TEST_F(LibraryFiles,MigrationZeroFutureSchemasAndMalformedHistoryAreTransactional) {
    auto c=Lib::Empty("legacy","Legacy");c.Create(Meta(),std::make_shared<const Lib::Payload>(Lib::Material{}));const auto file=dir/"legacy.yaml";Lib::Save(c,file);
    auto node=YAML::Load(Read(file));node["schema_version"]=0;node["assets"]=node["items"];node.remove("items");node["assets"][0]["revision"]=node["assets"][0]["version"];node["assets"][0].remove("version");Write(file,YAML::Dump(node));const auto original=Read(file);auto migrated=Lib::Load(file);
    EXPECT_EQ(migrated.catalog.Find("silicon")->version,1u);
    EXPECT_EQ(Read(file),original);
    node["schema_version"]=999;Write(file,YAML::Dump(node));
    EXPECT_THROW(Lib::Load(file),Assets::Error);
    node["schema_version"]=0;node["assets"][0]["revision"]=3;Write(file,YAML::Dump(node));
    EXPECT_THROW(Lib::Load(file),Assets::Error);
}
TEST_F(LibraryFiles,CsvAndScapsImportsHavePreviewableTypedPayloadsAndUnits) {
    Lib::Profile profile;profile.value.law=scaps_core::GradingLaw::Table;profile.value.coordinate={0,.0001,.4321,1};profile.value.value={1e20,9e22,2e20,1e20};Lib::ExportCsv(profile,dir/"profile.csv");const auto profileRound=std::get<Lib::Profile>(*Lib::ImportCsv(dir/"profile.csv","profile","Na_m3")->payload);
    EXPECT_EQ(profileRound.value.coordinate,profile.value.coordinate);
    EXPECT_EQ(profileRound.value.value,profile.value.value);
    const auto profilePreview=Lib::MakePreview(profile);
    EXPECT_EQ(profilePreview.x,profile.value.coordinate);
    EXPECT_EQ(profilePreview.y,profile.value.value);
    auto malformed=profile;malformed.value.value.pop_back();
    EXPECT_THROW(Lib::ExportCsv(malformed,dir/"malformed.csv"),std::exception);
    EXPECT_FALSE(std::filesystem::exists(dir/"malformed.csv"));
    EXPECT_THROW(Lib::MakePreview(malformed),Assets::Error);
    Write(dir/"alpha.csv","wavelength_nm,alpha_m1\n300,1000000\n600,500000\n1200,100\n");auto alpha=Lib::ImportCsv(dir/"alpha.csv","optical");Lib::ExportCsv(*alpha->payload,dir/"alpha-export.csv");auto round=Lib::ImportCsv(dir/"alpha-export.csv","optical");
    EXPECT_EQ(std::get<Lib::Optical>(*alpha->payload).absorption.table_nm_m1,std::get<Lib::Optical>(*round->payload).absorption.table_nm_m1);
    Write(dir/"bad.csv","wavelength_m,alpha_m1\n3e-7,1\n6e-7,2\n");
    EXPECT_THROW(Lib::ImportCsv(dir/"bad.csv","optical"),Assets::Error);Write(dir/"bad.csv","wavelength_nm,alpha_m1\n600,1\n300,2\n");
    EXPECT_THROW(Lib::ImportCsv(dir/"bad.csv","optical"),std::exception);
    const auto& source=Lib::Builtins()->libraries.at("sst-examples").catalog;const auto material=source.Find("si-default");Lib::ExportScaps(*material->payload,dir/"si.material");auto imported=Lib::ImportScaps(dir/"si.material");
    EXPECT_DOUBLE_EQ(std::get<Lib::Material>(*imported.Find("imported-asset")->payload).values.Eg_eV,1.12);
    auto p=Small()->project;Lib::ExportScaps(Lib::Device{p},dir/"device.def");auto device=Lib::ImportScaps(dir/"device.def");
    EXPECT_EQ(std::get<Lib::Device>(*device.Find("imported-asset")->payload).value.layers.size(),p.layers.size());
}
TEST_F(LibraryFiles,CommandJobApplyUndoWhereUsedAndProjectPersistence) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDraft(Small());const auto original=w.Current().draft;
    Lib::Request r;r.action=Lib::Action::Apply;r.libraryId="sst-examples";r.itemId="pyscaps-cigs";r.target.layerId=original->layers.front().id;Do(w,r);
    ASSERT_EQ(w.Current().draft->assetUses.size(),1u);
    EXPECT_DOUBLE_EQ(w.Current().draft->project.layers[0].material.Eg_eV,1.2726);
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);
    r.action=Lib::Action::WhereUsed;Do(w,r);
    ASSERT_EQ(w.library.whereUsed.size(),1u);
    EXPECT_EQ(w.library.whereUsed[0].use.source.itemId,"pyscaps-cigs");
    ASSERT_TRUE(w.Exec(Cmd::Undo{}).value("ok",false));
    EXPECT_TRUE(w.Current().draft->assetUses.empty());
    EXPECT_EQ(ProjectNumbers(w.Current().draft->project),ProjectNumbers(original->project));
    ASSERT_TRUE(w.Exec(Cmd::Redo{}).value("ok",false));
    EXPECT_EQ(w.Current().draft->assetUses.size(),1u);
    Lib::Request derive;derive.action=Lib::Action::Derive;derive.libraryId="project";derive.sourceLibraryId="sst-examples";derive.itemId="pyscaps-cigs";derive.newId="local-cigs";Do(w,derive);w.library.browser.grid=true;w.library.browser.search="CIGS";
    const auto file=dir/"linked.scapslab";
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(file.string(),false)).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false));
    Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;
    ASSERT_TRUE(loaded.Exec(Cmd::LoadProject::Make(file.string())).value("ok",false));Finish(loaded);
    ASSERT_TRUE(loaded.response.value("ok",false))<<loaded.status;
    EXPECT_EQ(loaded.Current().draft->assetUses,w.Current().draft->assetUses);
    EXPECT_TRUE(loaded.library.state->libraries.at("project").catalog.Items().contains("local-cigs"));
    EXPECT_TRUE(loaded.library.browser.grid);
    EXPECT_EQ(loaded.library.browser.search,"CIGS");
}
TEST(AssetLibrary,LinkedUpdatesPropagateAndCopiesRetainTheirValues) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDraft(Small());Lib::Request r;r.action=Lib::Action::Derive;r.libraryId="user";r.sourceLibraryId="sst-examples";r.itemId="si-default";r.newId="local";Do(w,r);
    r={};r.action=Lib::Action::Apply;r.libraryId="user";r.itemId="local";r.target.layerId=w.Current().draft->layers[0].id;r.policy=Assets::UsePolicy::Linked;Do(w,r);
    auto source=w.library.state->libraries.at("user").catalog.Find("local");auto material=std::get<Lib::Material>(*source->payload);material.values.Eg_eV=1.19;r={};r.action=Lib::Action::Revise;r.libraryId="user";r.metadata=source->meta;r.metadata.changeNote="New measurement";r.expectedVersion=1;r.payload=std::make_shared<const Lib::Payload>(material);Do(w,r);
    EXPECT_DOUBLE_EQ(w.Current().draft->project.layers[0].material.Eg_eV,1.19);
    EXPECT_EQ(w.Current().draft->assetUses[0].source.version,2u);
    auto copied=Lib::Apply(*Small(),*source,"user",{Small()->layers[0].id});
    EXPECT_TRUE(Lib::UpdateAvailable(copied->assetUses[0],w.library.state->libraries.at("user").catalog));
    EXPECT_DOUBLE_EQ(copied->project.layers[0].material.Eg_eV,1.12);
    auto refreshed=Lib::Refresh(*copied,*w.library.state,false);
    EXPECT_DOUBLE_EQ(refreshed->project.layers[0].material.Eg_eV,1.19);
    EXPECT_EQ(refreshed->assetUses[0].policy,Assets::UsePolicy::Copy);
    Lib::Request undo;undo.action=Lib::Action::Undo;Do(w,undo);
    EXPECT_DOUBLE_EQ(w.Current().draft->project.layers[0].material.Eg_eV,1.12);
    EXPECT_EQ(w.Current().draft->assetUses[0].source.version,1u);undo.action=Lib::Action::Redo;Do(w,undo);
    EXPECT_DOUBLE_EQ(w.Current().draft->project.layers[0].material.Eg_eV,1.19);
    EXPECT_EQ(w.Current().draft->assetUses[0].source.version,2u);
}
TEST(AssetLibrary,AppliedMaterialIsNumericallyIdenticalToHandEnteredValues) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;const auto original=Small();w.Current().SetDraft(original);
    Lib::Material material;material.values=original->project.layers[0].material;material.values.Eg_eV=1.15;material.values.mu_n_m2Vs=.1;material.Nd_m3=original->project.layers[0].Nd_m3;material.Na_m3=original->project.layers[0].Na_m3;
    Lib::Request create;create.action=Lib::Action::Create;create.libraryId="user";create.metadata=Meta("manual-match");create.payload=std::make_shared<const Lib::Payload>(material);Do(w,create);
    Lib::Request apply;apply.action=Lib::Action::Apply;apply.libraryId="user";apply.itemId="manual-match";apply.target.layerId=original->layers[0].id;Do(w,apply);
    auto manual=original->project;manual.layers[0].material.Eg_eV=1.15;manual.layers[0].material.mu_n_m2Vs=.1;
    EXPECT_EQ(ProjectNumbers(w.Current().draft->project),ProjectNumbers(manual));scaps_core::boundary_probe::result_adapters=0;
    ASSERT_TRUE(w.Exec(Cmd::Run::Make()).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false))<<w.status;
    const auto device=scaps_core::make_device_from_project(manual);auto voltages=scaps_core::make_voltage_list(manual.voltage);for(auto& v:voltages)v=-v;const auto pc=scaps_core::physical_constants(manual.constants_mode);const auto mesh=scaps_core::prepare_mesh(device,manual.mesh,voltages,{manual.temperature_K},manual.solver,pc);const auto exact=scaps_core::solve_iv(device,mesh,voltages,manual.temperature_K,manual.solver,pc);
    const auto result=w.Current().Iv();
    ASSERT_EQ(result->points.size(),exact.size());for(std::size_t i=0;i<exact.size();++i){ASSERT_EQ(exact[i].status,scaps_core::SolverStatus::Converged);const double current=exact[i].state.Jn_Am2.front()+exact[i].state.Jp_Am2.front();
    EXPECT_EQ(std::bit_cast<std::uint64_t>(result->points[i].J_A_m2),std::bit_cast<std::uint64_t>(current));}EXPECT_EQ(scaps_core::boundary_probe::result_adapters,0u);
}
TEST(AssetLibraryUi,AllItemKindsRetainPreviewsWithoutDiskOrSerializationOnIdleFrames) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1100};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    {Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(Small());Lib::Editor editor;const auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1550,1050});ImGui::Begin("Library UI test");editor.Draw(w);ImGui::End();ImGui::Render();
    EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
        Io::Watch(true);scaps_core::boundary_probe::result_adapters=0;for(const auto& [id,h]:w.library.state->libraries.at("sst-examples").catalog.Items()){w.library.browser.item=id;frame();frame();const auto builds=editor.PreviewBuilds();for(int i=0;i<3;++i)frame();
    EXPECT_EQ(editor.PreviewBuilds(),builds);const auto p=Lib::MakePreview(*h.back()->payload);
    EXPECT_FALSE(p.x.empty())<<id;
    EXPECT_EQ(p.x.size(),p.y.size());}EXPECT_TRUE(Io::UiCalls().empty());Io::Watch(false);
    EXPECT_EQ(scaps_core::boundary_probe::result_adapters,0u);
    EXPECT_FALSE(w.Pending());}
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
TEST_F(LibraryFiles,JsonDraftRecoveryAndStreamingWhereUsedSkipStoredResults) {
    auto catalog=Lib::Empty("team","Team",Assets::Scope::Shared);catalog.Create(Meta(),std::make_shared<const Lib::Payload>(Lib::Material{}));
    const auto file=dir/"team.json";Lib::Save(catalog,file);
    ASSERT_EQ(Read(file).front(),'{');auto mounted=Lib::Load(file);
    EXPECT_EQ(mounted.catalog.Find("silicon")->meta,Meta());
    auto state=std::make_shared<Lib::State>(*Lib::Builtins());state->libraries.emplace("team",mounted);
    auto edited=std::get<Lib::Material>(*mounted.catalog.Find("silicon")->payload);edited.values.Eg_eV=1.17;
    auto drafts=std::make_shared<Lib::DraftMap>();(*drafts)["team/silicon"]={Meta(),std::make_shared<const Lib::Payload>(edited),1};
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(Small());w.library.state=state;w.library.drafts=drafts;
    const auto project=dir/"draft.scapslab";
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(project.string(),false)).value("ok",false));Finish(w);
    Workflow restored(CreateRef<JobSystem>(nullptr,1));restored.lightResponse=true;
    ASSERT_TRUE(restored.Exec(Cmd::LoadProject::Make(project.string())).value("ok",false));Finish(restored);
    ASSERT_EQ(restored.library.drafts->size(),1u);
    EXPECT_DOUBLE_EQ(std::get<Lib::Material>(*restored.library.drafts->at("team/silicon").payload).values.Eg_eV,1.17);
    EXPECT_EQ(restored.library.state->libraries.at("team").source.path,file);
    const auto draft=Lib::Apply(*Small(),*mounted.catalog.Find("silicon"),"team",{Small()->layers[0].id});
    Json document=DraftDocument(*draft);Json stored={{"devices",Json::array({{{"id","device-on-disk"},{"name","Saved device"},{"document",document},{"result",{{"large_unused_array",std::vector<double>(200000,42.)}}}}})}};Write(dir/"usage.scapslab",stored.dump());
    const auto uses=Lib::ScanProjects(dir);
    ASSERT_EQ(uses.size(),1u);
    EXPECT_EQ(uses[0].device,"device-on-disk");
    EXPECT_EQ(uses[0].use.source.itemId,"silicon");
    auto invalid=std::make_shared<Lib::DraftMap>(*drafts);invalid->at("team/silicon").metadata.name.clear();w.library.drafts=invalid;
    const auto before=Read(project);
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(project.string(),false)).value("ok",false));Finish(w);
    EXPECT_FALSE(w.response.value("ok",true));
    EXPECT_EQ(Read(project),before);
}
TEST(AssetLibrary,EveryReusableKindAppliesThroughTypedTargetsWithoutAccumulatingUpdates) {
    auto draft=Small();const auto& seeds=Lib::Builtins()->libraries.at("sst-examples").catalog;
    auto defect=Lib::Apply(*draft,*seeds.Find("neutral-srh"),seeds.id,{draft->layers[0].id},Assets::UsePolicy::Linked);
    ASSERT_EQ(defect->project.layers[0].defects.size(),1u);const auto defectId=defect->layers[0].defects[0].id;
    auto next=std::make_shared<Lib::State>(*Lib::Builtins());auto copy=Lib::Empty("editable","Editable");auto meta=seeds.Find("neutral-srh")->meta;auto first=copy.Create(meta,seeds.Find("neutral-srh")->payload);auto value=std::get<Lib::Defect>(*first->payload);value.value.Nt_m3=2e20;meta.changeNote="Density change";copy.Revise(meta,std::make_shared<const Lib::Payload>(value),1);next->libraries.emplace("editable",Lib::Mounted{copy,{}});
    auto applied=Lib::Apply(*draft,*first,"editable",{draft->layers[0].id},Assets::UsePolicy::Linked);auto refreshed=Lib::Refresh(*applied,*next);
    EXPECT_EQ(refreshed->project.layers[0].defects.size(),1u);
    EXPECT_EQ(refreshed->layers[0].defects[0].id,defectId);
    EXPECT_DOUBLE_EQ(refreshed->project.layers[0].defects[0].Nt_m3,2e20);
    Lib::Target boundary;boundary.interfaceAfter=0;auto sheet=Lib::Apply(*draft,*seeds.Find("neutral-sheet"),seeds.id,boundary);auto sheetAgain=Lib::Apply(*sheet,*seeds.Find("neutral-sheet"),seeds.id,boundary);
    EXPECT_EQ(sheetAgain->project.interface_defects.size(),1u);
    auto profile=Lib::Apply(*draft,*seeds.Find("doping-constant"),seeds.id,{draft->layers[0].id});
    ASSERT_TRUE(profile->project.layers[0].grading->Na_m3);
    EXPECT_DOUBLE_EQ(profile->project.layers[0].grading->Na_m3->left,1e22);
    EXPECT_THROW(Lib::Apply(*profile,*seeds.Find("si-default"),seeds.id,{draft->layers[0].id}),Assets::Error);
    auto contact=Lib::Apply(*draft,*seeds.Find("contact-default"),seeds.id,{});
    EXPECT_EQ(contact->assetUses.back().target,"contact:left");
    auto illumination=Lib::Apply(*draft,*seeds.Find("am15-example"),seeds.id,{});
    EXPECT_FALSE(illumination->project.illumination.spectrum_nm_W_m2.empty());
    auto optical=Lib::Apply(*draft,*seeds.Find("alpha-demo"),seeds.id,{draft->layers[0].id});
    EXPECT_FALSE(optical->project.layers[0].absorption.table_nm_m1.empty());
    Lib::Target append;append.appendLayer=true;auto layer=Lib::Apply(*draft,*seeds.Find("si-layer"),seeds.id,append);
    EXPECT_EQ(layer->project.layers.size(),3u);
    EXPECT_EQ(layer->project.layers.back().name,std::get<Lib::Layer>(*seeds.Find("si-layer")->payload).value.name);
}
TEST(AssetLibrary,CancelBeforeExecutionLeavesThePublishedCatalogUntouched) {
    auto jobs=CreateRef<JobSystem>(nullptr,1);Workflow w(jobs);w.lightResponse=true;const auto before=w.library.state;
    ASSERT_TRUE(jobs->PauseAllJobs());
    Lib::Request request;request.action=Lib::Action::Create;request.libraryId="user";request.metadata=Meta();request.payload=std::make_shared<const Lib::Payload>(Lib::Material{});
    ASSERT_TRUE(w.Exec(Cmd::LibraryOperation::Make(request)).value("ok",false));
    ASSERT_TRUE(w.Exec(Cmd::Cancel{}).value("ok",false));
    ASSERT_TRUE(jobs->ResumeAllJobs());Finish(w);
    EXPECT_EQ(w.library.state,before);
    EXPECT_EQ(w.library.errorCode,"cancelled");
}
TEST(AssetLibrary,PreviewTracksBandgapAndPreservesLargeTableExtremaWithBoundedOutput) {
    Lib::Material material;const auto initial=Lib::MakePreview(material);material.values.Eg_eV=1.3;const auto edited=Lib::MakePreview(material);
    EXPECT_EQ(initial.y,edited.y);
    EXPECT_NE(initial.y2,edited.y2);
    EXPECT_DOUBLE_EQ(edited.y[0]-edited.y2[0],1.3);
    Lib::Optical optical;optical.absorption.model="file";optical.absorption.table_nm_m1.reserve(100000);for(int i=0;i<100000;++i)optical.absorption.table_nm_m1.emplace_back(200+i*.01,i==54321?1e9:1.);
    const auto preview=Lib::MakePreview(optical);
    EXPECT_LE(preview.x.size(),4096u);
    EXPECT_DOUBLE_EQ(*std::max_element(preview.y.begin(),preview.y.end()),1e9);
    EXPECT_EQ(preview.x.front(),optical.absorption.table_nm_m1.front().first);
    EXPECT_EQ(preview.x.back(),optical.absorption.table_nm_m1.back().first);
    EXPECT_LT(Lib::EditableNumbers(optical).size(),100u);
    EXPECT_NO_THROW(Lib::EditableNumbers(Lib::Layer{}));
    EXPECT_NO_THROW(Lib::EditableNumbers(Lib::Device{}));
    Lib::Profile constant;const auto fields=Lib::EditableNumbers(constant);
    EXPECT_EQ(fields.size(),1u);
    const auto request=Lib::Python::ReadRequest({{"target",{{"defect_id","existing-defect"}}}});
    EXPECT_EQ(request.target.defectId,"existing-defect");
    EXPECT_EQ(optical.absorption.table_nm_m1.size(),100000u);
    Lib::Profile a,b;b.value.law=scaps_core::GradingLaw::Linear;const auto diff=Lib::Compare(a,b);
    EXPECT_TRUE(std::any_of(diff.begin(),diff.end(),[](const auto& d){return d.path=="profile.law" && !d.beforeText.empty();}));
}
TEST_F(LibraryFiles,CreateReviewableGuiFixture) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDraft(Small());
    Lib::Request derive;derive.action=Lib::Action::Derive;derive.libraryId="project";derive.sourceLibraryId="sst-examples";derive.itemId="si-default";derive.newId="silicon-study";Do(w,derive);
    Lib::Request apply;apply.action=Lib::Action::Apply;apply.libraryId="project";apply.itemId="silicon-study";apply.target.layerId=w.Current().draft->layers[0].id;Do(w,apply);
    const auto first=w.library.state->libraries.at("project").catalog.Find("silicon-study");auto material=std::get<Lib::Material>(*first->payload);material.values.Eg_eV=1.15;
    Lib::Request revise;revise.action=Lib::Action::Revise;revise.libraryId="project";revise.expectedVersion=1;revise.metadata=first->meta;revise.metadata.changeNote="Synthetic UI comparison variant; not a literature measurement";revise.metadata.parameters.push_back({"layers[0].material.Eg_eV","eV","Synthetic UI fixture; no literature uncertainty claimed",1.,1.5,.02});revise.payload=std::make_shared<const Lib::Payload>(material);Do(w,revise);
    w.library.browser.library="project";w.library.browser.item="silicon-study";
    const auto root=std::getenv("ASSETS1_EVIDENCE");if(root){const auto path=std::filesystem::path(root)/"assets-review.scapslab";
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(path.string(),false)).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false));w.library.browser.library="cigs-literature-libdata1";w.library.browser.item="znob-ou2013";w.library.browser.grid=true;const auto literature=std::filesystem::path(root)/"assets-literature-review.scapslab";
    ASSERT_TRUE(w.Exec(Cmd::SaveProject::Make(literature.string(),false)).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false));}
}
TEST(AssetLibraryUi,DragTargetProducesACopyByDefaultBeforeAnyDeviceEdit) {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={600,400};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.Fonts->Build();
    {Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDraft(Small());const auto before=w.Current().draft;Lib::AssetDrag drag;std::snprintf(drag.library,sizeof(drag.library),"sst-examples");std::snprintf(drag.item,sizeof(drag.item),"si-default");drag.version=1;ImVec2 center;
        const auto frame=[&](bool source){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({550,350});ImGui::Begin("Drop target");if(source && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)){ImGui::SetDragDropPayload("SST_SCIENTIFIC_ASSET",&drag,sizeof(drag));ImGui::EndDragDropSource();}ImGui::Button("Destination layer",{250,90});const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();center={.5f*(a.x+b.x),.5f*(a.y+b.y)};Lib::DropTarget(w,{before->layers[0].id});ImGui::End();ImGui::Render();};
        frame(false);io.AddMousePosEvent(center.x,center.y);io.AddMouseButtonEvent(0,true);frame(true);frame(true);io.AddMouseButtonEvent(0,false);frame(true);
    ASSERT_TRUE(w.libraryDrop);
    EXPECT_EQ(w.libraryDrop->policy,Assets::UsePolicy::Copy);
    EXPECT_EQ(w.libraryDrop->target.layerId,before->layers[0].id);
    EXPECT_EQ(w.Current().draft,before);
    EXPECT_FALSE(w.Pending());}
    ImGui::DestroyContext();
}
TEST_F(LibraryFiles,LiteraturePackagePreservesCitationsAndRequiresInterfaceReferenceReview) {
    const auto state=Lib::Builtins();
    ASSERT_TRUE(state->warnings.empty())<<(state->warnings.empty()?"":state->warnings.front());
    const auto found=state->libraries.find("cigs-literature-libdata1");
    ASSERT_NE(found,state->libraries.end());const auto& catalog=found->second.catalog;
    ASSERT_EQ(catalog.Items().size(),23u);
    EXPECT_TRUE(catalog.ReadOnly());
    std::size_t parameters=0,review=0;auto editable=Lib::Empty("literature-roundtrip","Literature round trip");
    for(const auto& [id,history]:catalog.Items()){const auto& revision=*history.back();
    EXPECT_NO_THROW(Lib::ValidateRevision(revision))<<id;
    EXPECT_FALSE(revision.meta.provenance.doi.empty());for(const auto& p:revision.meta.parameters){++parameters;
    EXPECT_FALSE(p.citation.empty());}
        if(const auto* interface=std::get_if<Lib::Interface>(revision.payload.get());interface && interface->referenceReviewRequired){++review;
    EXPECT_EQ(interface->defects.front().energy_reference,4);Lib::Target target;target.interfaceAfter=0;
    EXPECT_THROW(Lib::Apply(*Small(),revision,catalog.id,target),Assets::Error);}
        editable.Create(revision.meta,revision.payload);
    }
    EXPECT_EQ(parameters,209u);
    EXPECT_EQ(review,2u);const auto file=dir/"literature.json";Lib::Save(editable,file);const auto round=Lib::Load(file);std::size_t restoredReview=0;
    for(const auto& [id,h]:round.catalog.Items())if(const auto* interface=std::get_if<Lib::Interface>(h.back()->payload.get());interface && interface->referenceReviewRequired)++restoredReview;
    EXPECT_EQ(restoredReview,2u);
    EXPECT_EQ(std::get<Lib::Layer>(*catalog.Find("znob-ou2013")->payload).value.material.mu_n_m2Vs,.01);
    const auto frozen=catalog.Find("cigs-walker2014-x030");Lib::Target target;target.layerId=Small()->layers[0].id;
    EXPECT_THROW(Lib::Apply(*Small(),*frozen,catalog.id,target),Assets::Error);target.composition=.3;const auto applied=Lib::Apply(*Small(),*frozen,catalog.id,target);
    ASSERT_TRUE(applied->project.layers[0].grading->composition);
    EXPECT_DOUBLE_EQ(applied->project.layers[0].grading->composition->left,.3);target.composition=.4;
    EXPECT_THROW(Lib::Apply(*Small(),*frozen,catalog.id,target),Assets::Error);
}
TEST(AssetLibraryUi,UsePopupOpensAndOutsideClickDismissesThePendingChoice) {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,700};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.Fonts->Build();
    {Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDraft(Small());Lib::Request r;r.action=Lib::Action::Apply;r.libraryId="sst-examples";r.itemId="si-default";r.target.layerId=w.Current().draft->layers[0].id;w.libraryDrop=r;
        const auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({400,300});ImGui::Begin("Library host");ImGui::TextUnformatted("Device");ImGui::End();Lib::DrawDropChoice(w);ImGui::Render();};
        frame();frame();
    EXPECT_TRUE(w.libraryDropShown);
    EXPECT_FALSE(GImGui->OpenPopupStack.empty());if(!GImGui->OpenPopupStack.empty()){ASSERT_NE(GImGui->OpenPopupStack.back().Window,nullptr);
    EXPECT_EQ(GImGui->OpenPopupStack.back().Window->ViewportId,ImGui::GetMainViewport()->ID);}io.AddMousePosEvent(990,690);io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    EXPECT_FALSE(w.libraryDrop);
    EXPECT_FALSE(w.libraryDropShown);
    EXPECT_FALSE(w.Pending());}
    ImGui::DestroyContext();
}
