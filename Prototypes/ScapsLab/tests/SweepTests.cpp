#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include "Panel.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include <limits>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
ProjectDraft SweepProject() {
    auto p=scaps_core::make_default_device_project();p.mesh.points_per_layer=11;p.voltage={0,.01,.01};
    for(auto& layer:p.layers)layer.defects.clear();p.interface_defects.clear();
    return *DraftFromProject(std::move(p));
}
NumericParameter Parameter(const ProjectDraft& draft,const std::string& path) {
    for(const auto& p:ProjectParameters(draft))if(p.path==path)return p;
    throw std::invalid_argument("Missing test parameter: "+path);
}
void WaitSweep(Workflow& w) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());ASSERT_TRUE(w.response.value("ok",false))<<w.response;
}
}
TEST(SweepRange,LinearStepIncludesShortLastIntervalAndSinglePoint) {
    SweepRange r;r.sampling=SweepSampling::Step;r.step=25;
    EXPECT_EQ(SweepValues(r),(std::vector<double>{270,295,320,330}));
    r.step=20;EXPECT_EQ(SweepValues(r),(std::vector<double>{270,290,310,330}));
    r.minimum=r.maximum=300;EXPECT_EQ(SweepValues(r),(std::vector<double>{300}));
    r.minimum=-1;r.maximum=0;r.step=.3;const auto samples=SweepValues(r);
    EXPECT_DOUBLE_EQ(samples.front(),-1);EXPECT_DOUBLE_EQ(samples.back(),0);EXPECT_EQ(samples.size(),5u);
    r.minimum=0;r.maximum=.3;r.step=.1;EXPECT_EQ(SweepValues(r).size(),4u);
}
TEST(SweepRange,LogDecadesAndPointsHaveExactEndpoints) {
    SweepRange r;r.minimum=1;r.maximum=200;r.step=1;r.logarithmic=true;r.sampling=SweepSampling::Step;
    EXPECT_EQ(SweepValues(r),(std::vector<double>{1,10,100,200}));
    r.maximum=10;r.step=.5;const auto steps=SweepValues(r);ASSERT_EQ(steps.size(),3u);EXPECT_NEAR(steps[1],std::sqrt(10.),1e-14);
    r.sampling=SweepSampling::Points;r.minimum=1e-200;r.maximum=1e200;r.points=3;
    const auto points=SweepValues(r);EXPECT_DOUBLE_EQ(points.front(),r.minimum);EXPECT_DOUBLE_EQ(points.back(),r.maximum);EXPECT_DOUBLE_EQ(points[1],1);
    r.logarithmic=false;r.minimum=-1e308;r.maximum=1e308;EXPECT_DOUBLE_EQ(SweepValues(r)[1],0);
    r.minimum=r.maximum=4;r.points=9;EXPECT_EQ(SweepValues(r),(std::vector<double>{4}));
}
TEST(SweepRange,RejectsInvalidRangesPrecisionAndLimits) {
    SweepRange r;r.sampling=SweepSampling::Step;
    for(double step:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){r.step=step;EXPECT_THROW(SweepValues(r),std::invalid_argument);}
    r.step=1;r.minimum=1;r.maximum=10000;EXPECT_EQ(SweepValues(r).size(),10000u);
    r.maximum=10001;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.maximum=2;r.minimum=3;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.minimum=0;r.logarithmic=true;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.minimum=-1;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.minimum=1;r.maximum=std::numeric_limits<double>::infinity();EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.logarithmic=false;r.minimum=-1e308;r.maximum=1e308;r.step=1e-308;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.sampling=SweepSampling::Points;r.minimum=1;r.maximum=2;r.points=1;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.points=10001;EXPECT_THROW(SweepValues(r),std::invalid_argument);
    r.points=3;r.maximum=std::nextafter(1.,2.);EXPECT_THROW(SweepValues(r),std::invalid_argument);
}
TEST(SweepRange,CustomListPreservesOrderDuplicatesAndReportsElement) {
    const auto values=ParseSweepList("330; +270\n300 270\t1.5e2");
    EXPECT_EQ(values,(std::vector<double>{330,270,300,270,150}));
    SweepRange r;r.sampling=SweepSampling::List;r.custom=values;r.logarithmic=true;EXPECT_EQ(SweepValues(r),values);
    for(const auto* text:{"", "1; nan", "1; inf", "1; 2,5", "1; 2bad", "1; 1e9999", "1; +"})EXPECT_THROW(ParseSweepList(text),std::invalid_argument)<<text;
    try{(void)ParseSweepList("1;2;bad");FAIL();}catch(const std::invalid_argument& e){EXPECT_NE(std::string(e.what()).find("element 3"),std::string::npos);}
    r.custom.assign(10001,1);EXPECT_THROW(SweepValues(r),std::invalid_argument);
}
TEST(SweepRange,QuickAndBatchUseIdenticalNativeSamplesAndValidatePhysicalDomain) {
    auto project=SweepProject();const auto p=Parameter(project,"temperature_K");auto editor=NewSweep(p,SweepComponent::FullValue);
    editor.range={SweepSampling::Step,false,270,330,25,5,{}};
    const auto axis=PrepareSweepAxis(project,p.id,editor);BatchDraft draft;draft.axes={axis};
    const auto batch=PrepareBatch(project,draft,RecorderDraft{});
    EXPECT_EQ(BatchAxisValues(axis),(std::vector<double>{270,295,320,330}));
    for(std::size_t i=0;i<batch.combinations.size();++i)EXPECT_DOUBLE_EQ(batch.combinations[i].at(p.path),(*axis.values)[i]);
    EXPECT_EQ(BatchPlanJson(batch),BatchPreview(project,BatchDraftJson(draft)));
    editor.range.minimum=0;EXPECT_THROW(PrepareSweepAxis(project,p.id,editor),std::invalid_argument);
    editor.range.minimum=300;editor.range.maximum=301;editor.range.step=.5;
    EXPECT_THROW(PrepareSweepAxis(project,"mesh.points_per_layer",editor),std::invalid_argument);
    EXPECT_THROW(PrepareSweepAxis(project,"missing-id",editor),std::invalid_argument);
}
TEST(SweepRange,MantissaAndExponentFreezeExactDisplayComponents) {
    auto project=SweepProject();project.project.layers[0].Nd_m3=2.5123456789e21;
    const auto p=Parameter(project,"layers[0].doping.Nd_m3");
    auto exponent=NewSweep(p,SweepComponent::Exponent,1e-6,"cm^-3");
    EXPECT_NEAR(exponent.fixed,2.5123456789,1e-14);
    exponent.range.minimum=14;exponent.range.maximum=17;exponent.range.step=1;
    const auto axis=PrepareSweepAxis(project,p.id,exponent);ASSERT_EQ(axis.values->size(),4u);
    for(int i=0;i<4;++i)EXPECT_NEAR((*axis.values)[i]/std::pow(10.,20+i),exponent.fixed,1e-14);
    auto mantissa=NewSweep(p,SweepComponent::Mantissa,1e-6,"cm^-3");EXPECT_DOUBLE_EQ(mantissa.fixed,15);
    mantissa.range.minimum=1;mantissa.range.maximum=5;mantissa.range.step=1;
    EXPECT_EQ(SweepNativeValues(mantissa).size(),5u);
    project.project.layers[0].Nd_m3=9e22;
    EXPECT_EQ(SweepNativeValues(exponent),*axis.values);
    auto inSI=exponent;inSI.displayScale=1;inSI.displayUnit="m^-3";inSI.range.minimum+=6;inSI.range.maximum+=6;
    const auto si=SweepNativeValues(inSI);for(std::size_t i=0;i<si.size();++i)EXPECT_NEAR(si[i]/(*axis.values)[i],1,1e-14);
    auto negative=mantissa;negative.fixed=-3;EXPECT_DOUBLE_EQ(SweepNativeValues(negative).front(),1000);
    negative.fixed=0;EXPECT_DOUBLE_EQ(SweepNativeValues(negative).front(),1e6);
}
TEST(SweepRange,ComponentsRejectZeroFractionalExponentAndOverflowWithoutClamping) {
    auto project=SweepProject();auto p=Parameter(project,"layers[0].doping.Nd_m3");p.value=0;
    EXPECT_THROW(NewSweep(p,SweepComponent::Mantissa),std::invalid_argument);
    EXPECT_THROW(NewSweep(p,SweepComponent::Exponent),std::invalid_argument);
    auto e=NewSweep(Parameter(project,"temperature_K"),SweepComponent::Exponent);
    e.range.sampling=SweepSampling::List;e.range.custom={1.5};EXPECT_THROW(SweepNativeValues(e),std::invalid_argument);
    e.range.custom={10000};EXPECT_THROW(SweepNativeValues(e),std::invalid_argument);
    e.range.custom={-10000};EXPECT_THROW(SweepNativeValues(e),std::invalid_argument);
    e.component=SweepComponent::Mantissa;e.fixed=0;e.range.custom={0};EXPECT_THROW(SweepNativeValues(e),std::invalid_argument);
    e.range.custom={10};EXPECT_THROW(SweepNativeValues(e),std::invalid_argument);
    e.range.custom={1,9.999};EXPECT_EQ(SweepNativeValues(e),e.range.custom);
}
TEST(SweepRange,LegacyDegenerateSamplingAndUnknownMetadataRemainLossless) {
    const auto project=SweepProject();const auto p=Parameter(project,"temperature_K");
    BatchAxis legacy;legacy.start=legacy.stop=300;legacy.count=3;
    EXPECT_EQ(EditSweep(legacy,p).range.custom,(std::vector<double>{300,300,300}));
    legacy.stop=310;legacy.count=1;EXPECT_EQ(EditSweep(legacy,p).range.custom,(std::vector<double>{300}));
    legacy.start=310;legacy.stop=290;legacy.count=3;EXPECT_EQ(EditSweep(legacy,p).range.custom,(std::vector<double>{310,300,290}));
    auto editor=NewSweep(p,SweepComponent::FullValue);BatchDraft config;config.axes={PrepareSweepAxis(project,p.id,editor)};
    auto json=BatchDraftJson(config);json["opaque"]="keep";
    const auto loaded=ReadBatchDraft(json);EXPECT_EQ(BatchDraftJson(*loaded),json);ASSERT_TRUE(loaded->axes[0].sweep_ui);
    json["axes"][0]["sweep_ui"]["fixed"]=999;json["axes"][0]["sweep_ui"]["minimum"]=1;
    const auto incompatible=ReadBatchDraft(json);EXPECT_FALSE(incompatible->axes[0].sweep_ui);EXPECT_EQ(BatchDraftJson(*incompatible),json);EXPECT_EQ(BatchAxisValues(incompatible->axes[0]),BatchAxisValues(config.axes[0]));
}
TEST(SweepRange,LinkedThreePlusParametersAndNestedAxisBInner) {
    const auto project=SweepProject();BatchDraft config;config.mode="linked";config.axes.clear();
    for(const auto& [path,values]:std::vector<std::pair<std::string,std::vector<double>>>{{"temperature_K",{290,310}},{"external.area_m2",{1e-4,2e-4}},{"mesh.points_per_layer",{11,21}},{"solver.max_newton_iterations",{100,120}}}){BatchAxis a;a.parameter_id=path;a.values=values;config.axes.push_back(a);}
    auto plan=PrepareBatch(project,config,RecorderDraft{});ASSERT_EQ(plan.combinations.size(),2u);
    EXPECT_EQ(plan.combinations[1],(std::map<std::string,double>{{"temperature_K",310},{"external.area_m2",2e-4},{"mesh.points_per_layer",21},{"solver.max_newton_iterations",120}}));
    EXPECT_EQ(BatchPlanJson(plan),BatchPreview(project,BatchDraftJson(config)));
    config.axes[2].values=std::vector<double>{11};EXPECT_THROW(PrepareBatch(project,config,RecorderDraft{}),std::invalid_argument);
    for(auto& axis:config.axes)axis.values->resize(1);EXPECT_EQ(PrepareBatch(project,config,RecorderDraft{}).combinations.size(),1u);
    config.axes[3]=config.axes[2];EXPECT_THROW(PrepareBatch(project,config,RecorderDraft{}),std::invalid_argument);
    config.axes.resize(2);config.axes[0].values=std::vector<double>{290,310};config.axes[1].values=std::vector<double>{1e-4,2e-4};config.mode="nested";
    plan=PrepareBatch(project,config,RecorderDraft{});ASSERT_EQ(plan.combinations.size(),4u);
    EXPECT_DOUBLE_EQ(plan.combinations[1].at("temperature_K"),290);EXPECT_DOUBLE_EQ(plan.combinations[1].at("external.area_m2"),2e-4);EXPECT_DOUBLE_EQ(plan.combinations[2].at("temperature_K"),310);
    config.axes[0].values->assign(101,300);config.axes[1].values->assign(100,1e-4);EXPECT_THROW(PrepareBatch(project,config,RecorderDraft{}),std::invalid_argument);
    config.mode="linked";config.axes[1].values->assign(101,1e-4);EXPECT_EQ(PrepareBatch(project,config,RecorderDraft{}).combinations.size(),101u);
    config.axes[0].values->assign(10001,300);config.axes[1].values->assign(10001,1e-4);EXPECT_THROW(PrepareBatch(project,config,RecorderDraft{}),std::invalid_argument);
}
TEST(SweepRange,StableTargetsSurviveReorderAndRejectUnresolvedIds) {
    auto project=SweepProject();const auto p=Parameter(project,"layers[0].doping.Nd_m3");auto editor=NewSweep(p,SweepComponent::FullValue);
    editor.range.sampling=SweepSampling::List;editor.range.custom={1e20,2e20};BatchDraft config;config.axes={PrepareSweepAxis(project,p.id,editor)};
    EditLayer(project,"move",project.layers[0].id,1);auto plan=PrepareBatch(project,config,RecorderDraft{});
    EXPECT_EQ(plan.spec.axes[0].path,"layers[1].doping.Nd_m3");EXPECT_EQ(plan.spec.axes[0].parameter_id,p.id);
    EditLayer(project,"remove",project.layers[1].id);EXPECT_THROW(PrepareBatch(project,config,RecorderDraft{}),std::invalid_argument);
}
TEST(SweepRange,TypedQuickJobSharesPlanAndRejectsStaleSnapshot) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDraft(CreateRef<const ProjectDraft>(SweepProject()));
    const auto snapshot=w.Current().draft;auto config=CreateRef<BatchDraft>();
    auto editor=NewSweep(Parameter(*snapshot,"temperature_K"),SweepComponent::FullValue);editor.range.sampling=SweepSampling::List;editor.range.custom={300};config->axes={PrepareSweepAxis(*snapshot,"temperature_K",editor)};
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Exec(Cmd::QuickSweep::Make(config,w.Current().recorder,snapshot)).value("ok",false));
    ASSERT_TRUE(w.ActiveOp());EXPECT_EQ(w.ActiveOp()->batch,config);EXPECT_EQ(w.ActiveOp()->draft,snapshot);EXPECT_FALSE(w.ActiveOp()->request.contains("spec"));
    const auto pending=w.ActiveOp();ASSERT_TRUE(w.jobs->ResumeAllJobs());WaitSweep(w);ASSERT_TRUE(w.Current().batchResult.typed());EXPECT_EQ(w.Current().batchResult.typed()->rows.size(),1u);
    ASSERT_TRUE(pending->feed->batch);EXPECT_EQ(pending->feed->batch,w.Current().batchResult.typed()->batchDraft);EXPECT_EQ(BatchAxisValues(pending->feed->batch->axes[0]),(std::vector<double>{300}));EXPECT_FALSE(w.Current().liveBatch);
    w.Current().SetDraft(CreateRef<const ProjectDraft>(*snapshot));
    EXPECT_FALSE(w.Exec(Cmd::QuickSweep::Make(config,w.Current().recorder,snapshot)).value("ok",true));EXPECT_FALSE(w.Pending());
}
TEST(SweepRange,SliderContextMenusAreSeparateKeyboardAccessibleAndHaveNoIdConflicts) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,1500};io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;auto draft=SweepProject();draft.project.layers[0].Nd_m3=2.5e21;w.Current().SetDraft(CreateRef<const ProjectDraft>(std::move(draft)));Panel panel(&w,1);
        ImGuiID focus=0;
        const auto frame=[&]{io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1300,1400});ImGui::Begin("Sweep keyboard");if(focus)ImGui::SetFocusID(focus,ImGui::GetCurrentWindow());panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);};
        frame();frame();auto* window=ImGui::FindWindowByName("Sweep keyboard");
        const auto row=ImHashStr("Donors [cm^-3]",0,ImHashStr("Donors [cm^-3]",0,window->ID));
        for(const auto& [slider,popup]:{std::pair{"##mantissa","Sweep mantissa"},std::pair{"##exponent","Sweep exponent"}}) {
            focus=ImHashStr(slider,0,row);frame();io.AddKeyEvent(ImGuiKey_Menu,true);frame();io.AddKeyEvent(ImGuiKey_Menu,false);frame();
            EXPECT_FALSE(GImGui->OpenPopupStack.empty())<<slider;
            if(!GImGui->OpenPopupStack.empty())EXPECT_EQ(GImGui->OpenPopupStack[0].PopupId,ImHashStr(popup,0,row));
            const auto activate=[&](ImGuiWindow* target,const std::string& label){focus=0;const auto id=target->GetID(label.c_str());bool found=false;for(float y=target->InnerRect.Min.y;y<target->InnerRect.Max.y && !found;y+=3){io.AddMousePosEvent(target->InnerRect.Min.x+25,y);frame();frame();if(GImGui->HoveredId==id){found=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}EXPECT_TRUE(found)<<label;return found;};
            if(!GImGui->OpenPopupStack.empty()) {
                if(activate(GImGui->OpenPopupStack[0].Window,"Add to batch...")) {
                    EXPECT_EQ(GImGui->OpenPopupStack.size(),2);
                    if(GImGui->OpenPopupStack.size()==2 && activate(GImGui->OpenPopupStack[1].Window,"Replace parameter 1: "+w.Current().batch->axes[0].parameter_id)) {
                        EXPECT_TRUE(GImGui->OpenPopupStack.empty());WaitSweep(w);
                        EXPECT_EQ(w.Current().batch->axes[0].parameter_id,Parameter(*w.Current().draft,"layers[0].doping.Nd_m3").id);
                    }
                }
            }
            focus=0;io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();frame();
        }
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

