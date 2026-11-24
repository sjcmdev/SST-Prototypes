#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "LegacyResultRuns.hpp"
#include <cstdlib>
#include <fstream>
#include <regex>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Exec(Workflow& w,const Json& r) {
    const auto started=w.Dispatch(r);if(!started.value("ok",false))return started;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(2);
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_FALSE(w.Pending());return w.response;
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};
}
}
// Compare the frozen pre-json2 file codec with the typed command/job path.
TEST(ExportBytes, ReferenceProject) {
    const char* configured=std::getenv("SCAPSLAB_EXPORT_BYTES_OUT");
    const auto out=configured?std::filesystem::path(configured):std::filesystem::temp_directory_path()/"json2-export-bytes";
    std::filesystem::create_directories(out);
    const char* baseline=std::getenv("SCAPSLAB_EXPORT_BASELINE");
    const auto legacy=baseline?std::filesystem::path(baseline):out/"legacy";
    std::filesystem::create_directories(legacy);
    const auto file=std::getenv("SCAPSLAB_EXPORT_PROJECT");
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
    ASSERT_TRUE(Exec(w,{{"operation","project_load"},{"path",file?std::string(file):(ExamplesDirectory()/"07-pn-compare-with-results.scapslab").string()}}).value("ok",false));
    const auto runs=w.Runs();ASSERT_FALSE(runs.empty());
    for(const char* format:{"csv","json"})for(std::size_t i=0;i<=runs.size();++i) {
        Json request={{"operation","export_results"},{"format",format},{"path",(std::filesystem::path(out)/(std::to_string(i)+"."+format)).string()}};
        if(i==runs.size())request["all"]=true;else request["run_ids"]=Json::array({runs[i].at("run_id")});
        const auto actual=std::filesystem::path(request.at("path").get<std::string>());
        const auto expected=legacy/actual.filename();auto oldRequest=request;oldRequest["path"]=expected.string();
        ASSERT_NO_THROW(LegacyExportResults(w.Workspace(),oldRequest));
        ASSERT_TRUE(Exec(w,request).value("ok",false))<<w.response;
        ASSERT_TRUE(std::filesystem::exists(expected))<<expected;
        EXPECT_TRUE(Bytes(actual)==Bytes(expected))<<actual.filename()<<" differs from the old exporter";
    }
}
void SST::ScapsLab::ComparePlotExports() {
    const char* configured=std::getenv("SCAPSLAB_EXPORT_BYTES_OUT");
    const auto out=(configured?std::filesystem::path(configured):std::filesystem::temp_directory_path()/"json2-export-bytes")/"plots";
    std::filesystem::create_directories(out);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;
    const auto file=std::getenv("SCAPSLAB_EXPORT_PROJECT");
    ASSERT_TRUE(Exec(w,{{"operation","project_load"},{"path",file?std::string(file):(ExamplesDirectory()/"07-pn-compare-with-results.scapslab").string()}}).value("ok",false));
    const auto runs=w.ResultRuns();ASSERT_FALSE(runs.empty());
    std::vector<ResultRun> selected;
    for(const auto& run:runs)if(run.runId==runs.front().runId)selected.push_back(run);
    for(const char* format:{"png","svg","pdf"}) {
        const auto before=out/(std::string("legacy.")+format),after=out/(std::string("typed.")+format);
        Json legacy;
        ASSERT_NO_THROW(legacy=LegacyExportResults(w.Workspace(),{{"path",before.string()},{"format",format},{"run_ids",Json::array({runs.front().runId})}}));
        const auto files=ExportResults(selected,after,format);ASSERT_EQ(files.size(),legacy.at("files").size());
        for(std::size_t i=0;i<files.size();++i) {
            auto old=Bytes(legacy.at("files")[i].at("path").get<std::string>()),now=Bytes(files[i]);
            // Export timestamps are metadata; geometry, pixels and all numeric content must match.
            if(std::string(format)=="svg") {const std::regex date("<dc:date>[^<]*</dc:date>");old=std::regex_replace(old,date,"");now=std::regex_replace(now,date,"");}
            if(std::string(format)=="pdf") {const std::regex date("/(CreationDate|ModDate) \\(D:[^)]*\\)");old=std::regex_replace(old,date,"");now=std::regex_replace(now,date,"");}
            EXPECT_TRUE(old==now)<<files[i]<<" differs from the old exporter";
        }
    }
}
