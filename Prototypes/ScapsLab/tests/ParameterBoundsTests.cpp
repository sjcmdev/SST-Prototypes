#include "Panel.hpp"
#include "Domain/ParameterBounds.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <fstream>
#include <regex>
#include <thread>
#include <scaps_core/device_project_def.hpp>

using namespace SST;
using namespace SST::ScapsLab;
namespace {
const auto root=std::filesystem::path(__FILE__).parent_path().parent_path();
std::string Read(const std::filesystem::path& file) {std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const std::filesystem::path& file,std::string_view text) {std::string error;ASSERT_TRUE(TextFileIO::Save(Path{file},text,error))<<error;}
struct BoundsFiles : testing::Test {
    std::filesystem::path dir=root.parent_path().parent_path()/"build/parameter-bounds-tests",defaults=dir/"defaults.yaml",user=dir/"user.yaml";
    std::string previousUser,previousDefaults;
    void SetUp() override {Write(defaults,Read(root/"config/parameter_bounds.yaml"));Write(user,"schema_version: 1\nparameters: {}\n");}
    void TearDown() override {(void)InitializeParameterBounds((root/"config/parameter_bounds.yaml").string(),(dir/"absent.yaml").string());}
};
void Finish(Workflow& w) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while((w.Pending() || w.IoPending()) && std::chrono::steady_clock::now()<until){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());ASSERT_FALSE(w.IoPending());
}
}
TEST(ParameterBounds, ShippedFileMatchesCompiledFallbackAndRoundTrips) {
    const auto file=ReadParameterBoundsYaml(Read(root/"config/parameter_bounds.yaml"));
    const auto roundtrip=ReadParameterBoundsYaml(WriteParameterBoundsYaml(file));
    ASSERT_EQ(file.size(),BuiltinBounds().size());ASSERT_EQ(roundtrip.size(),file.size());
    for(const auto& [key,b]:file) {
        SCOPED_TRACE(key);const auto& compiled=BuiltinBounds().at(key);const auto& r=roundtrip.at(key);
        EXPECT_EQ(b.hardMin,compiled.hardMin);EXPECT_EQ(b.hardMax,compiled.hardMax);EXPECT_EQ(b.softMin,compiled.softMin);EXPECT_EQ(b.softMax,compiled.softMax);
        EXPECT_EQ(b.hardMin,r.hardMin);EXPECT_EQ(b.hardMax,r.hardMax);EXPECT_EQ(b.softMin,r.softMin);EXPECT_EQ(b.softMax,r.softMax);EXPECT_EQ(b.scale,r.scale);
        EXPECT_EQ(b.step,r.step);EXPECT_EQ(b.unit,r.unit);EXPECT_EQ(b.displayUnit,r.displayUnit);EXPECT_EQ(b.displayScale,r.displayScale);EXPECT_EQ(b.format,r.format);EXPECT_EQ(b.source,r.source);EXPECT_EQ(b.note,r.note);
        EXPECT_NO_THROW(b.Validate(key));
    }
}
TEST_F(BoundsFiles, UserMergeKeepsUnchangedKeysAndFields) {
    Write(user,"schema_version: 1\nparameters:\n  temperature_K: {soft_min: 200, soft_max: 800}\n");
    std::string diagnostic;const auto table=LoadParameterBounds(BuiltinBounds(),defaults.string(),user.string(),diagnostic);
    EXPECT_TRUE(diagnostic.empty());const auto& t=table.at("temperature_K");EXPECT_EQ(t.softMin,200);EXPECT_EQ(t.softMax,800);EXPECT_TRUE(t.userOverride);
    EXPECT_EQ(t.hardMin,BuiltinBounds().at("temperature_K").hardMin);EXPECT_EQ(t.unit,"K");EXPECT_FALSE(table.at("Eg_eV").userOverride);
}
TEST_F(BoundsFiles, InvalidFilesFallBackAtomicallyAndProduceDiagnostics) {
    for(const auto* bad:{"schema_version: 9\nparameters: {}", "schema_version: 1\nparameters:\n  temperature_K: {soft_min: -1}",
        "schema_version: 1\nparameters:\n  thickness_m: {soft_min: 0}", "schema_version: 1\nparameters:\n  made_up: {soft_max: 4}",
        "schema_version: 1\nparameters:\n  eps_r: {hard_min: .nan}", "schema_version: 1\nparameters:\n  eps_r: {format: '%s'}",
        "schema_version: 1\nparameters:\n  eps_r: {soft_min: 2, soft_min: 3}", "schema_version: 1\nparameters:\n  eps_r: {unit: J}",
        "schema_version: 1\nparameters:\n  points: {soft_min: 1.5}"}) {
        SCOPED_TRACE(bad);Write(user,bad);std::string diagnostic;const auto table=LoadParameterBounds(BuiltinBounds(),defaults.string(),user.string(),diagnostic);
        EXPECT_NE(diagnostic.find(user.filename().string()),std::string::npos);EXPECT_NE(diagnostic.find("built-in defaults"),std::string::npos);
        EXPECT_EQ(WriteParameterBoundsYaml(table),WriteParameterBoundsYaml(BuiltinBounds()));
    }
    Write(defaults,"bad: [");std::string diagnostic;EXPECT_EQ(LoadParameterBounds(BuiltinBounds(),defaults.string(),user.string(),diagnostic).size(),BuiltinBounds().size());EXPECT_FALSE(diagnostic.empty());
}
TEST_F(BoundsFiles, ReloadCommandUsesJobsAndNotifiesOnFailureThenResetRestoresDefaults) {
    ASSERT_TRUE(InitializeParameterBounds(defaults.string(),user.string()).empty());
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=false;w.autoRecalculateBand=false;
    const auto old=BoundsTable();Write(user,"schema_version: 1\nparameters:\n  temperature_K: {soft_max: 900}\n");
    ASSERT_TRUE(w.Exec(Cmd::BoundsFile::Make(BoundsFileAction::Reload)).value("ok",false));Finish(w);
    EXPECT_EQ(BoundsTable()->at("temperature_K").softMax,900);EXPECT_EQ(old->at("temperature_K").softMax,600);
    Write(user,"schema_version: 1\nparameters:\n  temperature_K: {hard_max: -1}\n");
    ASSERT_TRUE(w.Dispatch({{"operation","parameter_bounds"},{"action","reload"}}).value("ok",false));Finish(w);
    EXPECT_FALSE(w.boundsFileDiagnostic.empty());ASSERT_FALSE(w.notifier->GetHistory().empty());EXPECT_NE(w.notifier->GetHistory().back().message.find("built-in defaults"),std::string::npos);
    ASSERT_TRUE(w.Exec(Cmd::BoundsFile::Make(BoundsFileAction::Reset)).value("ok",false));Finish(w);
    EXPECT_TRUE(w.boundsFileDiagnostic.empty());EXPECT_FALSE(BoundsTable()->at("temperature_K").userOverride);
}
TEST_F(BoundsFiles, NarrowerReloadLeavesOpenValuesIntactAndRejectsRun) {
    ASSERT_TRUE(InitializeParameterBounds(defaults.string(),user.string()).empty());
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=false;w.autoRecalculateBand=false;auto draft=std::make_shared<ProjectDraft>(*DraftFromDocument(NewDeviceDocument()));draft->project.temperature_K=700;w.Current().SetDraft(draft);
    Write(user,"schema_version: 1\nparameters:\n  temperature_K: {hard_max: 500, soft_max: 500}\n");
    ASSERT_TRUE(w.Exec(Cmd::BoundsFile::Make(BoundsFileAction::Reload)).value("ok",false));Finish(w);
    EXPECT_TRUE(w.boundsFileDiagnostic.empty());EXPECT_EQ(w.Current().draft->project.temperature_K,700);
    EXPECT_FALSE(w.Exec(Cmd::Run::Make()).value("ok",true));EXPECT_FALSE(w.Pending());
}
TEST(ParameterBounds, HardLimitsRejectTypedAndPythonCommandEditsWithoutChangingTheDevice) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=false;w.autoRecalculateBand=false;w.Current().SetDocument(NewDeviceDocument());const auto original=w.Current().draft;
    auto invalid=std::make_shared<ProjectDraft>(*original);invalid->project.layers.front().material.eps_r=.5;
    const auto rejected=w.Exec(Cmd::EditDocument::Make(invalid));EXPECT_FALSE(rejected.value("ok",true));EXPECT_EQ(w.Current().draft,original);
    EXPECT_NE(rejected.at("error").at("message").get<std::string>().find("eps_r"),std::string::npos);
    const auto python=w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",-5.}}}});EXPECT_FALSE(python.value("ok",true));EXPECT_EQ(w.Current().draft,original);
    EXPECT_THROW(SetProjectParameter(invalid->project,"layers[0].thickness_m",0),std::invalid_argument);
    auto plan=std::make_shared<RunPlan>();plan->actions[1].temperature_K=-1;EXPECT_FALSE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",true));
    auto spec=BatchDraft{};spec.axes[0].values=std::vector<double>{300,1e20};const auto batchPlan=PrepareBatch(*original,spec,RecorderDraft{});EXPECT_THROW(SetProjectParameter(invalid->project,"temperature_K",batchPlan.combinations[1].at("temperature_K")),std::invalid_argument);
}
TEST_F(BoundsFiles, ImportedYamlIsClampedWithAFieldListAndValidFilesStayByteStable) {
    auto document=NewDeviceDocument();const auto original=DraftFromDocument(document);const auto untouched=DraftFromDocument(document,true);
    EXPECT_TRUE(untouched->importWarnings.empty());EXPECT_EQ(DraftDocument(*untouched),DraftDocument(*original));
    document["temperature_K"]=-2;document["layers"][0]["material"]["eps_r"]=.5;
    const auto imported=DraftFromDocument(document,true);ASSERT_EQ(imported->importWarnings.size(),2u);EXPECT_EQ(imported->project.temperature_K,BuiltinBounds().at("temperature_K").hardMin);EXPECT_EQ(imported->project.layers[0].material.eps_r,1);
    const auto path=dir/"outside.scapslab";auto workspace=SingleDeviceWorkspace(NewDeviceDocument(),"Bounds import");workspace["devices"][0]["document"]=document;Write(path,workspace.dump());
    Workflow w(CreateRef<JobSystem>(nullptr,1));ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path.string()}}).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false))<<w.status;EXPECT_EQ(w.Current().draft->importWarnings.size(),2u);EXPECT_TRUE(w.dirty);EXPECT_NE(w.status.find("clamped"),std::string::npos);
}
TEST(ParameterBounds, SoftRangeExtendsWithoutClampingAndLogExponentsFollowTheTable) {
    const auto& temperature=BuiltinBounds().at("temperature_K");EXPECT_TRUE(temperature.Contains(1000));EXPECT_FALSE(temperature.Typical(1000));
    EXPECT_EQ(temperature.SliderRange(1000),std::make_pair(100.,1000.));EXPECT_EQ(temperature.SliderRange(50),std::make_pair(50.,600.));
    const auto density=DisplayBounds("Nd_m3",1e-6);const auto exponent=density.ExponentRange(1e25);EXPECT_EQ(exponent.first,10);EXPECT_EQ(exponent.second,25);
    const auto sweep=DisplayBounds("sweep.exponent:Nd_m3",1e-6);EXPECT_EQ(sweep.softMin,10);EXPECT_EQ(sweep.softMax,20);
    EXPECT_EQ(DisplayBounds("sweep.step:Nd_m3",1e-6).softMin,BuiltinBounds().at("Nd_m3").step*1e-6);
    EXPECT_TRUE(BuiltinBounds().at("Sn_m_s").Contains(INFINITY));EXPECT_FALSE(BuiltinBounds().at("area_m2").Contains(INFINITY));EXPECT_FALSE(temperature.Contains(NAN));
    EXPECT_DOUBLE_EQ(ParameterToSI("A_m1_eV_half"),1/std::sqrt(1.602176634e-19));EXPECT_DOUBLE_EQ(ParameterToSI("B_m1_eV_minus_half"),std::sqrt(1.602176634e-19));
    EXPECT_EQ(ParameterToSI("wavelength_nm"),1e-9);EXPECT_EQ(ParameterToSI("radiative_cm3_s"),1e-6);EXPECT_EQ(ParameterToSI("auger_n_cm6_s"),1e-12);
    EXPECT_EQ(DisplayBounds("Eg_eV").unit,"eV");EXPECT_EQ(DisplayBounds("Nd_m3",1e-6).unit,"cm^-3");EXPECT_EQ(DisplayBounds("Nd_m3").unit,"m^-3");
}
TEST_F(BoundsFiles, DefImportClampsHardLimitsAndKeepsInactiveSingleLevelWidths) {
    auto native=scaps_core::make_default_device_project();native.layers[0].material.eps_r=.5;
    const auto file=dir/"outside.def";scaps_core::export_def(native,file);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(NewDeviceDocument());
    ASSERT_TRUE(w.Dispatch({{"operation","import_def"},{"path",file.string()}}).value("ok",false));Finish(w);
    ASSERT_TRUE(w.response.value("ok",false))<<w.status;EXPECT_EQ(w.Current().draft->project.layers[0].material.eps_r,1);
    ASSERT_FALSE(w.Current().draft->importWarnings.empty());EXPECT_NE(w.status.find("eps_r"),std::string::npos);
    auto document=NewDeviceDocument();document["layers"][0]["defects"]=Json::array({NewDefect()});document["layers"][0]["defects"][0]["characteristic_energy_eV"]=0;
    EXPECT_TRUE(DraftFromDocument(document,true)->importWarnings.empty());
}
TEST(ParameterBounds, NumericWidgetKeysAndBatchCatalogHaveBoundsAndNoPhysicalWidgetBypassesTheHelper) {
    const auto text=Read(root/"src/Panel.cpp");const std::regex keys(R"re(\bNumber\("[^"\n]*"\s*,[^;\n]*?,\s*"([^"]+)")re");
    int count=0;for(auto it=std::sregex_iterator(text.begin(),text.end(),keys);it!=std::sregex_iterator();++it){++count;EXPECT_TRUE(BuiltinBounds().contains((*it)[1].str()))<<(*it)[1];}EXPECT_GT(count,40);
    EXPECT_EQ(text.find("LogMap("),std::string::npos);EXPECT_EQ(text.find("ImGui::DragScalar("),std::string::npos);EXPECT_EQ(text.find("ImGui::DragInt("),std::string::npos);EXPECT_EQ(text.find("widgets::LogControl("),std::string::npos);
    EXPECT_EQ(text.find("ImGui::SliderInt(\""),std::string::npos);EXPECT_EQ(text.find("ImGui::SliderFloat("),std::string::npos);
    const std::regex directKeys(R"re((?:DrawBoundValue|BoundIndex)\("[^"\n]*"\s*,[^;\n]*?,\s*"([^"]+)")re");
    for(auto it=std::sregex_iterator(text.begin(),text.end(),directKeys);it!=std::sregex_iterator();++it)EXPECT_TRUE(BuiltinBounds().contains((*it)[1].str()))<<(*it)[1];
    auto draft=*DraftFromDocument(NewDeviceDocument());draft.project.layers[0].material.caughey_thomas.emplace();draft.project.layers[0].grading.emplace();
    for(const auto* key:{"composition","Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"})GradingProfile(*draft.project.layers[0].grading,key).emplace();
    AddDraftDefect(draft,0,false);
    if(draft.project.layers.size()<2){draft.project.layers.push_back(draft.project.layers[0]);draft.layers.push_back({"second","Second",{}});}
    AddDraftDefect(draft,0,true);
    for(const auto& parameter:ProjectParameters(draft))EXPECT_TRUE(BuiltinBounds().contains(ParameterBoundsKey(parameter.path)))<<parameter.path;
}
TEST(ParameterBounds, ExactUiEntryRejectsHardLimitsAndAcceptsOutsideTypicalValues) {
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={600,300};io.DeltaTime=1.f/60;io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    double value=300;ImVec2 at;
    const auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({550,250});ImGui::Begin("Bounds exact entry");DrawBoundValue("temperature",value,"temperature_K");at=ImGui::GetItemRectMin();at.x+=60;at.y+=8;ImGui::End();ImGui::Render();EXPECT_EQ(context->DebugDrawIdConflictsCount,0);};
    frame();frame();
    const auto type=[&](const char* number){io.AddMousePosEvent(at.x,at.y);frame();io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();EXPECT_TRUE(ImGui::TempInputIsActive(context->ActiveId));io.AddInputCharactersUTF8(number);frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();};
    type("-1");EXPECT_EQ(value,300);type("1000");EXPECT_EQ(value,1000);EXPECT_EQ(DisplayBounds("temperature_K").SliderRange(value).second,1000);
    ImGui::DestroyContext(context);
}
