#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Exec(Workflow &w,const Json &r) {
    const auto started=w.Dispatch(r); if(!started.at("ok").get<bool>()) return started;
    while(w.Busy()) {w.OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));} return w.response;
}
}
// Every shipped example opens, and every device's I-V calculates without error. Prints the compute time per device.
TEST(ScapsLabExamples,EveryExampleOpensAndComputes) {
    int count=0;
    for(const auto &entry:std::filesystem::directory_iterator(ExamplesDirectory())) if(entry.path().extension()==".scapslab") {
        ++count;
        Workflow w(CreateRef<JobSystem>(nullptr,1));
        ASSERT_TRUE(Exec(w,{{"operation","project_load"},{"path",entry.path().string()}}).value("ok",false))<<entry.path();
        const auto ids=w.Workspace().at("devices");
        for(std::size_t i=0;i<ids.size();++i) {
            ASSERT_TRUE(Exec(w,{{"operation","device_select"},{"device_id",ids[i].at("id")}}).value("ok",false))<<w.response.dump();
            EXPECT_FALSE(w.Current().description.empty())<<entry.path()<<" device "<<w.Current().name<<" needs a description";
            const auto t0=std::chrono::steady_clock::now();
            const auto r=Exec(w,{{"operation","run"}});
            const double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
            EXPECT_TRUE(r.value("ok",false))<<entry.path()<<" "<<w.Current().name<<": "<<r.dump();
            EXPECT_TRUE(w.Current().result.value("valid",false))<<entry.path()<<" "<<w.Current().name;
            if(w.Current().result.contains("photovoltaic"))std::cout<<"EXAMPLE_PV "<<w.Current().result["photovoltaic"].dump()<<std::endl;
            std::cout<<"EXAMPLE_TIME "<<entry.path().filename().string()<<" | "<<w.Current().name<<" | "<<s<<" s"<<std::endl;
            EXPECT_LT(s,60.);
        }
    }
    EXPECT_GE(count,7);
}
// Maintainer tool: SCAPSLAB_GEN_EXAMPLES=<dir> writes the teaching examples built from validated decks.
TEST(ScapsLabExamples,GenerateTeachingExamples) {
    const char *out=std::getenv("SCAPSLAB_GEN_EXAMPLES");if(!out) GTEST_SKIP();
    const std::filesystem::path dir=out;
    const std::string core="D:/SJCM/01_SemiconductorStudio/scaps_core-wt/ui/";
    struct Spec {std::string file,project,name,description,def;Json patch;};
    const std::vector<Spec> specs{
        {"09-cigs-cds-zno-am15.scapslab","CIGS / CdS / ZnO solar cell (AM1.5G)","CIGS solar cell",
         "CIGS / CdS / ZnO thin-film cell (2 um absorber, Eg 1.27 eV) under AM1.5G, 300 K. Run Calculate light I-V for Voc, Jsc, FF and efficiency, then QE(lambda). Parameters from the Aissat 2018 SCAPS deck.",
         "D:/SJCM/01_SemiconductorStudio/references/PyScaps/mydefs/CIGS-ZnO-CdS-Aissat2018.def",
         {{"illumination",{{"enabled",true},{"scapslab_spectrum","AM1.5G"}}},{"voltage",{{"start_V",0.},{"stop_V",0.8},{"step_V",0.02}}},{"temperature_K",300.}}},
        {"10-schottky-cv.scapslab","Schottky diode C-V","Schottky diode (n-type)",
         "n-type semiconductor with a Schottky back contact, dark, 300 K. Use C-V and Mott-Schottky to read the doping and built-in voltage; I-V shows the rectifying curve. From the validated a5-schottky deck.",
         core+"validation/cases/a5-schottky/scaps.def",
         {{"illumination",{{"enabled",false}}},{"voltage",{{"start_V",-0.5},{"stop_V",0.5},{"step_V",0.025}}},{"temperature_K",300.}}},
        {"11-si-pn-am15.scapslab","Si p-n solar cell (AM1.5G)","Si p-n solar cell",
         "2 um Si p-n junction (the 08-pn-illumination example) under the AM1.5G spectrum, 300 K. Calculate light I-V for Voc, Jsc, FF; QE(lambda) shows the spectral response.",
         (ExamplesDirectory()/"08-pn-illumination.yaml").string(),
         {{"illumination",{{"enabled",true},{"scapslab_spectrum","AM1.5G"}}},{"voltage",{{"start_V",0.},{"stop_V",0.7},{"step_V",0.02}}},{"temperature_K",300.}}},
    };
    for(const auto &s:specs) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));
        w.Current().document=ReadProject(SCAPSLAB_PROJECT);
        auto defPath=s.def;
        if(s.def.find("Aissat")!=std::string::npos) { // this older deck codes the interface-defect energy reference as 0 (above Ev); the importer wants 7 (above EV left)
            std::ifstream in(s.def);std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
            const auto block=text.find("interface recombination"),at=text.find("Reference for defect energy : 0",block);
            ASSERT_NE(at,std::string::npos);text.replace(at,std::string("Reference for defect energy : 0").size(),"Reference for defect energy : 7");
            defPath=(std::filesystem::temp_directory_path()/"cigs-iface7.def").string();std::ofstream(defPath)<<text;
        }
        if(s.def.ends_with(".yaml")) w.Current().document=ReadProject(s.def);
        else ASSERT_TRUE(Exec(w,{{"operation","import_def"},{"path",defPath}}).value("ok",false))<<w.response.dump();
        if(s.def.find("Aissat")!=std::string::npos) // the deck gives A = 1e7 /(m sqrt(eV)), B = 0 for all layers; the importer leaves the old-style lines unread
            for(auto &layer:w.Current().document["layers"]) {layer["absorption"]["model"]="sqrt";layer["absorption"]["A_m1_eV_half"]=1e7;layer["absorption"]["B_m1_eV_minus_half"]=0.;}
        const auto edit=Exec(w,{{"operation","edit"},{"patch",s.patch}});ASSERT_TRUE(edit.value("ok",false))<<edit.dump();
        w.Current().name=s.name;w.Current().description=s.description;
        auto workspace=w.Workspace();workspace["name"]=s.project;
        w.SetWorkspace(workspace);
        const auto save=Exec(w,{{"operation","project_save"},{"path",(dir/s.file).string()},{"include_results",false}});
        ASSERT_TRUE(save.value("ok",false))<<save.dump();
    }
}
