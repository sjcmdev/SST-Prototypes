// S10: project save/open through IOLayer events, async settings/recent/recovery writes, autosave failure toast, and "no disk IO on the UI thread".
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Io.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
std::filesystem::path TempDir(const char *name) {
    auto dir=std::filesystem::temp_directory_path()/(std::string("slm10-")+name);
    std::error_code ec;std::filesystem::remove_all(dir,ec);std::filesystem::create_directories(dir);return dir;
}
template<class P> bool Pump(Workflow &w,P done,int ms=20000) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    while(!done()) {if(std::chrono::steady_clock::now()>until) return false;w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return true;
}
bool Settled(Workflow &w) {return Pump(w,[&]{return !w.Pending() && !w.IoPending();});}
std::string Read(const std::filesystem::path &p) {std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
struct App {
    Ref<EventBus> bus=CreateRef<EventBus>();IOLayer io{bus};Workflow w{CreateRef<JobSystem>(nullptr,2),{},bus};
    App() {w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.RegisterIo(io);}
};
}
TEST(Io,SaveAsAndOpenThroughEvents) {
    const auto dir=TempDir("events");const auto file=dir/"a.scapslab";
    App a;a.w.project.name="Saved by event";
    a.bus->PublishNew<ProjectSaveRequested>(file.string());
    ASSERT_TRUE(a.w.ioResult.value("ok",false))<<a.w.ioResult.dump();
    ASSERT_TRUE(Settled(a.w));EXPECT_TRUE(std::filesystem::exists(file));EXPECT_EQ(a.w.path,file.string());
    App b;b.bus->PublishNew<ProjectOpenRequested>(file.string());
    ASSERT_TRUE(b.w.ioResult.value("ok",false))<<b.w.ioResult.dump();
    ASSERT_TRUE(Settled(b.w));EXPECT_EQ(b.w.project.name,"Saved by event");EXPECT_EQ(b.w.path,file.string());
    b.bus->PublishNew<ProjectOpenRequested>((dir/"missing.scapslab").string());
    ASSERT_TRUE(Settled(b.w));ASSERT_FALSE(b.w.notifier->GetHistory().empty());EXPECT_EQ(b.w.notifier->GetHistory().back().title,"Load failed");
}
TEST(Io,CoreSaveEventAsksTheFileLayer) {
    App a;a.bus->PublishNew<CoreEvents::ProjectSaveRequested>();EXPECT_EQ(a.w.fileAction,"save");
}
TEST(Io,ExportThroughEvent) {
    const auto dir=TempDir("export");App a;
    ASSERT_TRUE(a.w.Dispatch({{"operation","run"}}).at("ok").get<bool>());ASSERT_TRUE(Settled(a.w));
    a.bus->PublishNew<ProjectExportRequested>((dir/"r.csv").string(),"csv",true,std::vector<std::string>{});
    ASSERT_TRUE(a.w.ioResult.value("ok",false))<<a.w.ioResult.dump();ASSERT_TRUE(Settled(a.w));EXPECT_TRUE(std::filesystem::exists(dir/"r.csv"));
}
TEST(Io,RecentUpdatedOnSaveAndPersistedAsync) {
    const auto dir=TempDir("recent");const auto file=dir/"p.scapslab";const auto recentFile=dir/"scapslab-recent.json";
    {std::ofstream(recentFile)<<R"(["old-1.scapslab","old-2.scapslab"])";}
    App a;a.w.SetRecentPath(recentFile.string());
    ASSERT_TRUE(Settled(a.w));ASSERT_EQ(a.w.recent.size(),2u);EXPECT_EQ(a.w.recoveryPath,(dir/"scapslab-recovery.scapslab").string());
    a.bus->PublishNew<ProjectSaveRequested>(file.string());ASSERT_TRUE(Settled(a.w));
    ASSERT_EQ(a.w.recent.size(),3u);EXPECT_EQ(a.w.recent[0],file.string());
    ASSERT_TRUE(Settled(a.w));
    const auto saved=Json::parse(Read(recentFile));ASSERT_EQ(saved.size(),3u);EXPECT_EQ(saved[0],file.string());EXPECT_EQ(saved[1],"old-1.scapslab");
}
TEST(Io,AutosaveFailureNotifiesOncePerStreak) {
    const auto dir=TempDir("autosave");const auto blocker=dir/"file.txt";{std::ofstream(blocker)<<"x";}
    const auto bad=(blocker/"recovery.scapslab").string(); // parent is a file: the write must fail
    App a;int failed=0;auto sub=a.bus->Subscribe<ProjectSaveFailed>([&](const ProjectSaveFailed &e){if(e.autosave)++failed;});
    for(int i=0;i<2;++i) {a.w.StartSave({{"operation","project_save"},{"path",bad},{"autosave",true}});ASSERT_TRUE(Settled(a.w));}
    EXPECT_EQ(failed,2);
    std::size_t toasts=0;for(const auto &n:a.w.notifier->GetHistory()) if(n.title=="Autosave failed") {++toasts;EXPECT_EQ(n.severity,NotificationSeverity::Warn);}
    EXPECT_EQ(toasts,1u)<<"second failure of the same streak must not toast again";
    const auto good=(dir/"recovery.scapslab").string();
    a.w.StartSave({{"operation","project_save"},{"path",good},{"autosave",true}});ASSERT_TRUE(Settled(a.w));EXPECT_FALSE(a.w.autosaveFailing);
    a.w.StartSave({{"operation","project_save"},{"path",bad},{"autosave",true}});ASSERT_TRUE(Settled(a.w));
    toasts=0;for(const auto &n:a.w.notifier->GetHistory()) if(n.title=="Autosave failed") ++toasts;EXPECT_EQ(toasts,2u)<<"a success re-arms the warning";
}
TEST(Io,NoDiskIoOnTheUiThread) {
    const auto dir=TempDir("uithread");const auto file=dir/"p.scapslab";
    App a;a.w.settingsPath=(dir/"scapslab-ui.json").string();
    Io::Watch(true); // this thread plays the UI thread from here on
    a.w.SetRecentPath((dir/"scapslab-recent.json").string());
    a.w.recoveryPath=(dir/"scapslab-recovery.scapslab").string();
    a.w.settings["layout_key"]="x";a.w.SaveSettings();a.w.SaveSettings(); // second one overlaps: must be coalesced, not run concurrently
    a.bus->PublishNew<ProjectSaveRequested>(file.string());ASSERT_TRUE(Settled(a.w));
    a.w.StartSave({{"operation","project_save"},{"path",a.w.recoveryPath},{"autosave",true}});ASSERT_TRUE(Settled(a.w));
    a.w.DropRecovery("");ASSERT_TRUE(Settled(a.w));
    ASSERT_TRUE(a.w.Dispatch({{"operation","run"}}).at("ok").get<bool>());ASSERT_TRUE(Settled(a.w));
    a.bus->PublishNew<ProjectExportRequested>((dir/"r.csv").string(),"csv",true,std::vector<std::string>{});ASSERT_TRUE(Settled(a.w));
    App b;b.bus->PublishNew<ProjectOpenRequested>(file.string());ASSERT_TRUE(Settled(b.w));
    auto calls=Io::UiCalls();Io::Watch(false);
    // Known, out of scope: document <-> scaps_core model conversion round-trips through a temp .yaml because the engine only has path based YAML loaders (engine is read-only).
    std::erase_if(calls,[](const std::string &c){return c.find("scapslab-")!=std::string::npos && c.ends_with(".yaml");});
    EXPECT_TRUE(calls.empty())<<"UI-thread disk access: "<<(calls.empty()?"":calls.front());
    EXPECT_EQ(Json::parse(Read(dir/"scapslab-ui.json")).value("layout_key",std::string{}),"x");
    EXPECT_TRUE(std::filesystem::exists(file));EXPECT_TRUE(std::filesystem::exists(dir/"r.csv"));EXPECT_FALSE(std::filesystem::exists(a.w.recoveryPath));
    EXPECT_EQ(b.w.project.name,a.w.project.name);
}
TEST(Io,WatchSeesUiThreadAccess) { // the instrument itself works
    Io::Watch(true);std::string t,e;(void)Io::Exists(std::filesystem::temp_directory_path());(void)Io::Load(std::filesystem::temp_directory_path()/"slm10-nonexistent",t,e);
    EXPECT_EQ(Io::UiCalls().size(),2u);Io::Watch(false);
}
