#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <bit>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
void Execute(Workflow& w,Json request) {
    ASSERT_TRUE(w.Dispatch(request).value("ok",false));
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.value("ok",false))<<w.response;
}
void CheckParameters(const Json& document) {
    const auto draft=DraftFromDocument(document);auto project=draft->project;
    for(const auto& parameter:BatchParameters(document)) {
        const std::string path=parameter.at("path"),pointer=parameter.at("pointer");
        if(std::any_of(draft->extensions.begin(),draft->extensions.end(),[&](const auto& field){return pointer==field.first || pointer.starts_with(field.first+"/");}))continue;
        const double before=parameter.at("value");
        ASSERT_EQ(std::bit_cast<std::uint64_t>(ProjectParameter(project,path)),std::bit_cast<std::uint64_t>(before))<<path;
        const auto key=ParameterBoundsKey(path);const auto bounds=BoundsTable()->find(key);const double factor=ParameterToSI(key);
        if(bounds!=BoundsTable()->end() && !bounds->second.Contains(before*factor))continue; // inactive engine coefficients retain zero; they are not editable physical values
        const bool integer=bounds!=BoundsTable()->end() && bounds->second.integer;
        double after=integer?before+1:std::nextafter(before,INFINITY);
        if(bounds!=BoundsTable()->end() && !bounds->second.Contains(after*factor))after=integer?before-1:std::nextafter(before,-INFINITY);
        SetProjectParameter(project,path,after);EXPECT_EQ(ProjectParameter(project,path),after)<<path;
        SetProjectParameter(project,path,before);
        EXPECT_EQ(std::bit_cast<std::uint64_t>(ProjectParameter(project,path)),std::bit_cast<std::uint64_t>(before))<<path;
        EXPECT_EQ(std::bit_cast<std::uint64_t>(ProjectParameter(draft->project,path)),std::bit_cast<std::uint64_t>(before))<<path;
    }
}
}
TEST(ProjectDraft, NumericRegistryCoversExampleAndSweepAxes) {
    for(const auto& file:std::filesystem::directory_iterator(ExamplesDirectory()))if(file.path().extension()==".scapslab" || file.path().extension()==".yaml") {
        Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;Execute(w,{{"operation","project_load"},{"path",file.path().string()}});
        for(const auto& device:w.project.devices)CheckParameters(device->Document());
    }
    if(const char* sweep=std::getenv("TYPED2_SWEEP5")) {
        Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
        Execute(w,{{"operation","project_load"},{"path",sweep}});
        for(const auto& device:w.project.devices)CheckParameters(device->Document());
    }
}
TEST(ProjectDraft, UnknownExtensionsAndStableIdentities) {
    auto document=NewDeviceDocument();document["future_extension"]={{"unknown",Json::array({1,"two",{{"three",3}}})}};
    document["layers"][0]["future_layer"]={{"opaque",42}};
    const auto draft=DraftFromDocument(document);
    EXPECT_EQ(draft->extensions.at("/future_extension"),document.at("future_extension"));
    EXPECT_EQ(draft->extensions.at("/layers/0/future_layer"),document.at("layers")[0].at("future_layer"));
    EXPECT_EQ(draft->layers[0].id,document.at("layers")[0].at("scapslab_id").get<std::string>());
    auto project=draft->project;SetProjectParameter(project,"temperature_K",310.);
    EXPECT_EQ(draft->project.temperature_K,document.at("temperature_K").get<double>());
    EXPECT_THROW(SetProjectParameter(project,"mesh.points_per_layer",3.5),std::invalid_argument);
    EXPECT_THROW(SetProjectParameter(project,"temperature_K",INFINITY),std::invalid_argument);
    EXPECT_THROW(SetProjectParameter(project,"missing",1),std::invalid_argument);
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDocument(document);
    const auto file=std::filesystem::temp_directory_path()/"scapslab-typed2-extensions.scapslab";
    Execute(w,{{"operation","project_save"},{"path",file.string()}});
    Execute(w,{{"operation","project_load"},{"path",file.string()}});
    const auto loaded=DraftFromDocument(w.Current().Document());
    EXPECT_EQ(loaded->extensions,draft->extensions);EXPECT_EQ(w.Current().Document(),document);
    std::filesystem::remove(file);
}
TEST(ProjectDraft, ExistingUndoRedoRestoresTypedSnapshot) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
    w.Current().SetDocument(NewDeviceDocument());
    const auto before=DraftFromDocument(w.Current().Document());
    Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",310.}}},{"gesture_id","typed2"}});
    const auto after=DraftFromDocument(w.Current().Document());EXPECT_EQ(ProjectParameter(after->project,"temperature_K"),310.);
    Execute(w,{{"operation","undo"}});EXPECT_EQ(ProjectParameter(DraftFromDocument(w.Current().Document())->project,"temperature_K"),before->project.temperature_K);
    Execute(w,{{"operation","redo"}});EXPECT_EQ(ProjectParameter(DraftFromDocument(w.Current().Document())->project,"temperature_K"),310.);
    EXPECT_EQ(before->project.temperature_K,300.);
}
