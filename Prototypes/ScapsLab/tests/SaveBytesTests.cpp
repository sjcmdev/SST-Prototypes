// S4 gate: load -> save must produce identical bytes in every build. Run the same test on the base and the new build with the same
// SCAPSLAB_BYTES_OUT=<dir>, then compare the dirs (`cmp`/`diff -r`). SCAPSLAB_BYTES_EXTRA = extra project files separated by ';'.
// Scenarios per file: "load" (load -> save), "trim" (history_retention 2 -> save) and "select" (load -> history_select of the oldest run -> save: exercises profile dropping).
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Exec(Workflow &w,const Json &r) {
    const auto started=w.Dispatch(r);if(!started.at("ok").get<bool>()) return started;
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return w.response;
}
}
TEST(SaveBytes,LoadSaveIsByteStable) {
    const char *out=std::getenv("SCAPSLAB_BYTES_OUT");if(!out) GTEST_SKIP()<<"set SCAPSLAB_BYTES_OUT";
    std::filesystem::create_directories(out);
    std::vector<std::filesystem::path> files;
    for(const auto &e:std::filesystem::directory_iterator(ExamplesDirectory())) if(e.path().extension()==".scapslab" || e.path().extension()==".yaml") files.push_back(e.path());
    if(const char *extra=std::getenv("SCAPSLAB_BYTES_EXTRA")) {std::stringstream s(extra);std::string item;while(std::getline(s,item,';')) if(!item.empty()) files.push_back(item);}
    for(const auto &file:files) for(const char *scenario:{"load","select","trim"}) {
        Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
        ASSERT_TRUE(Exec(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false))<<file;
        if(std::string(scenario)=="select") for(const auto &d:w.project.devices) if(!d->history.empty()) {
            const Json select={{"operation","history_select"},{"device_id",d->id},{"run_id",d->history.front().at("run_id")}};
            ASSERT_TRUE(Exec(w,select).value("ok",false));
        }
        if(std::string(scenario)=="trim") {auto ui=w.project.ui;ui["history_retention"]=2;const Json edit={{"operation","project_edit"},{"ui",ui}};ASSERT_TRUE(Exec(w,edit).value("ok",false));}
        const auto target=std::filesystem::path(out)/(file.stem().string()+"."+scenario+".scapslab");
        ASSERT_TRUE(Exec(w,{{"operation","project_save"},{"path",target.string()}}).value("ok",false))<<file;
        std::printf("SAVEBYTES %s %s %zu\n",file.filename().string().c_str(),scenario,std::size_t(std::filesystem::file_size(target)));
    }
}
