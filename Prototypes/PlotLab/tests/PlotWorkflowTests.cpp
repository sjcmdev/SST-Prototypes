#include "PlotWorkflow.hpp"
#include "PlotWorkflowPanel.hpp"
#include "PlotLabWorkspace.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include <nlohmann/json.hpp>
using namespace SST;
using namespace SST::Prototypes::PlotLab;
TEST(PlotShouldPersistence, NewSettingsRoundtripAndVersionOneMigration) {
    const auto file=std::filesystem::temp_directory_path()/"plotshould2-migration.plotlab";
    WorkflowState state;auto& r=state.recipe;r.id="shared-peaks";r.worksheet=12;r.xColumn=13;r.yColumn=14;
    auto& s=r.settings;s.operation=pl::ScientificOperation::MultiPeakFit;s.peaks={{"a",pl::PeakShape::Voigt},{"b",pl::PeakShape::SplitGaussian}};s.anchors={{0,1},{10,2}};
    s.parameters=pl::defaultAnalysisParameters(s,"ns","counts");s.parameters[2].shared=true;
    s.outputFrom=-2;s.outputTo=15;s.acknowledgeExtrapolation=true;s.targetX={-2,15};r.globalInputs={{"330K",22,23,24,0,s.parameters}};
    state.acceptedRecipes.emplace(80,std::make_shared<const pl::AnalysisRecipe>(r));SaveWorkflowState(file,state);
    auto loaded=LoadWorkflowState(file);EXPECT_EQ(loaded.recipe,r);ASSERT_EQ(loaded.acceptedRecipes.size(),1);EXPECT_EQ(*loaded.acceptedRecipes.at(80),r);
    auto sidecar=file;sidecar+=".analysis.json";nlohmann::json j;{std::ifstream in(sidecar);in>>j;}j["version"]=1;j["accepted_recipes"]=nlohmann::json::array();
    auto& old=j["recipe"];old.erase("global_inputs");auto& settings=old["settings"];settings["operation"]=0;
    for(auto key:{"output_from","output_to","acknowledge_extrapolation","peaks"})settings.erase(key);
    for(auto& p:settings["parameters"])p.erase("shared");{std::ofstream out(sidecar);out<<j;}
    loaded=LoadWorkflowState(file);EXPECT_TRUE(loaded.recipe.globalInputs.empty());EXPECT_FALSE(loaded.recipe.settings.outputFrom);EXPECT_FALSE(loaded.recipe.settings.outputTo);EXPECT_FALSE(loaded.recipe.settings.acknowledgeExtrapolation);EXPECT_TRUE(loaded.recipe.settings.peaks.empty());
    for(const auto& p:loaded.recipe.settings.parameters)EXPECT_FALSE(p.shared);EXPECT_EQ(loaded.recipe.settings.operation,pl::ScientificOperation::LinearFit);
    j["version"]=3;{std::ofstream out(sidecar);out<<j;}EXPECT_THROW(LoadWorkflowState(file),std::invalid_argument);std::filesystem::remove(sidecar);
}

TEST(PlotWorkflow, SharedCommandsAnalysisMapTemplateAndPersistenceMigration) {
    auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,2);
    Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Grid",pl::CsvImporter{}.read("X,Y,Z\n0,0,1\n1,0,2\n2,0,3\n0,1,4\n1,1,5\n2,1,6\n")));
    auto book=doc.project->children(doc.project->root()).front();auto sheet=doc.project->children(book).front();auto columns=doc.project->children(sheet);
    auto& workflow=doc.Workflow();pl::MapPlotRequest map;map.worksheet=sheet;map.x=columns[0];map.y=columns[1];map.z=columns[2];map.gridColumns=3;map.spec.contours.levels={2.5,4.5};map.spec.z.scale=plot::AxisScale::Log10;
    const auto before=doc.undo->GetUndoDepth();auto graph=workflow.CreateMap(map);ASSERT_EQ(doc.undo->GetUndoDepth(),before+1);
    auto layer=doc.project->children(graph).front();auto series=doc.project->series(doc.project->children(layer).front());
    EXPECT_EQ(series.data.x,std::get<std::vector<double>>(std::get<pl::Column>(doc.project->object(columns[0]).data).values).data());
    auto t=std::make_shared<const pl::GraphTemplate>(pl::captureGraphTemplate(*doc.project,graph));pl::BatchPlotPlan plan{t,{{sheet,"Grid copy",{},pl::mapGraphTemplate(*doc.project,sheet,*t)}}};ASSERT_TRUE(plan.valid());
    auto active=doc.activeSheet;workflow.StartBatch(plan,"png",{});auto status=workflow.WaitBatch();EXPECT_EQ(status.graphs.size(),1);EXPECT_TRUE(status.errors.empty());EXPECT_EQ(doc.activeSheet,active);
    workflow.StartBatch(plan,"png",{});workflow.CancelBatch();EXPECT_TRUE(workflow.WaitBatch().graphs.empty());
    pl::AnalysisRecipe r;r.id="fit-grid";r.name="Grid fit";r.worksheet=sheet;r.xColumn=columns[0];r.yColumn=columns[2];r.settings.xTo=2;r.showResiduals=false;r.autoPreview=false;
    // Select the first physical scan line; the full grid's duplicate X values are deliberately rejected.
    auto fit=doc.AddColumn(sheet);doc.SetValues(fit,std::vector<double>{0,1,2,3,4,5});r.xColumn=fit;
    workflow.panel.SetRecipe(r);workflow.panel.open=true;workflow.panel.previewView.viewport=plot::Bounds{0,5,1,6};
    workflow.analysis.Submit(r);ASSERT_EQ(workflow.analysis.Wait().stage,AnalysisStage::PreviewReady);
    const auto& result=*workflow.analysis.Status().preview->items.front().result;EXPECT_NEAR(result.parameters[0],1,1e-12);EXPECT_NEAR(result.parameters[1],1,1e-12);
    workflow.analysis.Accept();ASSERT_EQ(doc.workflowState.acceptedRecipes.size(),1);
    const auto file=std::filesystem::temp_directory_path()/"plotint1-workflow.plotlab";doc.Save(file);
    Document reopened(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);reopened.Open(file);EXPECT_EQ(reopened.workflowState.recipe,r);EXPECT_TRUE(reopened.Workflow().panel.open);ASSERT_TRUE(reopened.Workflow().panel.previewView.viewport);EXPECT_EQ(reopened.Workflow().panel.previewView.viewport->maxX,5);ASSERT_EQ(reopened.workflowState.acceptedRecipes.size(),1);EXPECT_EQ(*reopened.workflowState.acceptedRecipes.begin()->second,r);
    auto sidecar=file;sidecar+=".analysis.json";{std::ofstream out(sidecar);out<<"{\"format\":\"sst.plotlab.analysis-editor\",\"version\":99}";}
    const auto original=reopened.project;EXPECT_THROW(reopened.Open(file),std::exception);EXPECT_EQ(reopened.project,original);
    std::filesystem::remove(sidecar);reopened.Open(file);EXPECT_EQ(reopened.workflowState.recipe.worksheet,0);EXPECT_FALSE(reopened.Workflow().panel.open);EXPECT_EQ(reopened.project->children(graph),doc.project->children(graph));
    std::filesystem::remove(file);jobs->Shutdown(true);
}

TEST(PlotWorkflow, CancellableMapPreviewKeepsLastGoodMapAndDoesNotMutateDocument) {
    auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,1);Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Grid",pl::CsvImporter{}.read("X,Y,Z\n0,0,1\n1,0,2\n0,1,3\n1,1,4\n")));
    auto sheet=doc.project->children(doc.project->children(doc.project->root()).front()).front();auto c=doc.project->children(sheet);auto& w=doc.Workflow();auto revision=doc.project->revision();
    pl::MapPlotRequest r;r.worksheet=sheet;r.x=c[0];r.y=c[1];r.z=c[2];r.gridColumns=2;w.PreviewMap(r);
    while(w.PollMap().running)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(w.PollMap().project);auto previous=w.PollMap().project;
    r.gridColumns=3;w.PreviewMap(r);while(w.PollMap().running)std::this_thread::sleep_for(std::chrono::milliseconds(1));EXPECT_EQ(w.PollMap().project,previous);EXPECT_FALSE(w.PollMap().message.empty());EXPECT_EQ(doc.project->revision(),revision);jobs->Shutdown(true);
}

TEST(PlotWorkflow, IntegratedPanelsHaveOneWindowAndUniqueIds) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1000};io.DeltaTime=.05f;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.ConfigDebugHighlightIdConflicts=true;
    auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,1);auto doc=CreateRef<Document>(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);
    doc->Execute(pl::importTable(*doc->project,doc->project->root(),"Grid",pl::CsvImporter{}.read("X,Y,Z\n0,0,1\n1,0,2\n0,1,3\n1,1,4\n")));
    auto sheet=doc->project->children(doc->project->children(doc->project->root()).front()).front();auto session=CreateRef<Session>(doc,jobs,std::filesystem::path{});PlotWorkflowPanel panel(*session);
    panel.OpenAnalysis(sheet);doc->Workflow().panel.OpenBatch();panel.OpenMap(sheet);panel.OpenTemplates();panel.OpenData(sheet);panel.OpenImage();
    for(int frame=0;frame<5;++frame){ImGui::NewFrame();panel.Render();panel.Render();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);EXPECT_EQ(ImGui::FindWindowByName("Scientific analysis")->BeginCount,1);}
    jobs->Shutdown(true);ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(PlotWorkflow, WorksheetErrorChoiceReachesPublicationWithoutChangingAssociation) {
    auto bus=CreateRef<EventBus>();Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Errors",pl::CsvImporter{}.read("X,Y\n0,1\n1,2\n2,3\n")));
    const auto sheet=doc.project->children(doc.project->children(doc.project->root()).front()).front();const auto columns=doc.project->children(sheet);const auto error=doc.AddColumn(sheet);
    doc.SetValues(error,std::vector<double>{.1,.2,.3});auto object=doc.project->object(error);auto& sigma=std::get<pl::Column>(object.data);sigma.role=pl::Role::YError;sigma.errorOf=columns[1];doc.Execute(doc.project->change("Associate yErr",{std::move(object)}));
    const auto graph=doc.PlotColumns(sheet,{columns[1]},plot::SeriesKind::LineScatter,0,columns[0],error);
    const auto snapshot=PublicationSnapshot(*doc.project,graph);ASSERT_EQ(snapshot->series.size(),1);EXPECT_EQ(snapshot->series[0].yError,(std::vector<double>{.1,.2,.3}));EXPECT_LT(snapshot->ranges.minY,.9);EXPECT_GT(snapshot->ranges.maxY,3.3);
    EXPECT_EQ(std::get<pl::Column>(doc.project->object(error).data).errorOf,columns[1]);
    doc.graphViews[graph].viewport=plot::Bounds{.2,1.5,1.2,2.5};const auto zoomed=doc.SnapshotGraph(graph);EXPECT_EQ(zoomed->ranges.minX,.2);EXPECT_EQ(zoomed->ranges.maxY,2.5);
    doc.graphViews[graph].axisState.emplace();doc.graphViews[graph].axisState->x.ticks.positions.resize(5000);const auto captured=CapturePublicationView(doc.graphViews[graph]);EXPECT_FALSE(captured.axisState);EXPECT_FALSE(captured.contourGeometry);EXPECT_EQ(captured.viewport->minX,.2);
    const auto file=std::filesystem::temp_directory_path()/"plotint1-current-view.plotlab";doc.Save(file);Document reopened(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);reopened.Open(file);EXPECT_EQ(reopened.SnapshotGraph(graph)->ranges.minX,.2);std::filesystem::remove(file);
}

TEST(PlotWorkflow, AnalysisPreviewRefitsWhenOutputUnitsChange) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1200};io.DeltaTime=.05f;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,1);Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Signal",pl::CsvImporter{}.read("X,Y\n0,1\n1,3\n2,5\n")));
    const auto sheet=doc.project->children(doc.project->children(doc.project->root()).front()).front();const auto columns=doc.project->children(sheet);
    for(int i=0;i<2;++i){auto object=doc.project->object(columns[i]);std::get<pl::Column>(object.data).units=i?"A":"s";doc.Execute(doc.project->change("Units",{std::move(object)}));}
    auto& w=doc.Workflow();w.panel.open=true;pl::AnalysisRecipe r;r.id="unit-change";r.worksheet=sheet;r.xColumn=columns[0];r.yColumn=columns[1];r.autoPreview=false;r.showResiduals=false;
    Document untrusted(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);untrusted.SaveScript("Untrusted","raise RuntimeError('must not execute')");untrusted.Open(Ref<pl::Project>(untrusted.project->operationSnapshot().release()),{});
    EXPECT_FALSE(untrusted.PythonTrusted());EXPECT_THROW(PythonBinding::LoadUserAnalysisModel(untrusted,"must-not-execute.py",r),pl::AnalysisException);
    auto draw=[&](float height=1100){for(int frame=0;frame<3;++frame){ImGui::NewFrame();ImGui::SetNextWindowSize({800,height},ImGuiCond_Always);w.panel.Draw();ImGui::Render();}};
    r.settings.operation=pl::ScientificOperation::Integrate;
    w.panel.SetRecipe(r);w.analysis.Submit(r);ASSERT_EQ(w.analysis.Wait().stage,AnalysisStage::PreviewReady);w.analysis.Accept();draw();
    r.settings.operation=pl::ScientificOperation::Differentiate;w.panel.SetRecipe(r);w.analysis.Submit(r);ASSERT_EQ(w.analysis.Wait().stage,AnalysisStage::PreviewReady);draw(300);draw();
    EXPECT_EQ(w.analysis.Status().preview->items.front().result->outputUnits,"(A)/(s)");
    ASSERT_TRUE(w.panel.previewView.viewport);EXPECT_GT(w.panel.previewView.viewport->minY,1);EXPECT_LT(w.panel.previewView.viewport->maxY,3);
    jobs->Shutdown(true);ImPlot::DestroyContext();ImGui::DestroyContext();
}
TEST(PlotWorkflow, PeakMarkerDragEditsTypedParametersAndKeepsSourcesAndIds) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1000};io.DeltaTime=.05f;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.ConfigDebugHighlightIdConflicts=true;
    auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,1);Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs);
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Peak",pl::CsvImporter{}.read("X,Y\n-2,0.1353352832\n-1,0.6065306597\n0,1\n1,0.6065306597\n2,0.1353352832\n")));
    const auto sheet=doc.project->children(doc.project->children(doc.project->root()).front()).front();const auto c=doc.project->children(sheet);const auto revision=doc.project->revision();
    auto& w=doc.Workflow();w.panel.open=true;pl::AnalysisRecipe r;r.id="drag";r.worksheet=sheet;r.xColumn=c[0];r.yColumn=c[1];r.autoPreview=false;r.showResiduals=false;
    r.settings.operation=pl::ScientificOperation::MultiPeakFit;r.settings.peaks={{"p",pl::PeakShape::Gaussian}};r.settings.anchors={{0,0}};
    r.settings.parameters=pl::defaultAnalysisParameters(r.settings,"","");for(auto& p:r.settings.parameters){p.fixed=true;p.start=p.name=="p_center"?0:1;}
    w.panel.SetRecipe(r);w.analysis.Submit(r);ASSERT_EQ(w.analysis.Wait().stage,AnalysisStage::PreviewReady);
    auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);w.panel.Draw();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);};
    for(int i=0;i<3;++i)frame();
    ImPlotPlot* plot=nullptr;auto* context=ImPlot::GetCurrentContext();
    for(int i=0;i<context->Plots.GetBufSize();++i){auto* candidate=context->Plots.GetByIndex(i);if(candidate->TitleOffset>=0 && std::string(candidate->GetTitle())=="analysis-preview")plot=candidate;}
    ASSERT_NE(plot,nullptr);const float x=plot->Axes[ImAxis_X1].PlotToPixels(0),y=plot->Axes[ImAxis_Y1].PlotToPixels(1);
    io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMousePosEvent(x+30,y+10);frame();io.AddMouseButtonEvent(0,false);frame();
    const auto& changed=w.panel.Recipe();EXPECT_NE(changed.settings.parameters[1].start,r.settings.parameters[1].start);EXPECT_EQ(changed.settings.peaks,r.settings.peaks);EXPECT_EQ(doc.project->revision(),revision);
    w.analysis.Submit(changed);EXPECT_EQ(w.analysis.Wait().stage,AnalysisStage::PreviewReady);EXPECT_TRUE(w.analysis.Status().exact);
    jobs->Shutdown(true);ImPlot::DestroyContext();ImGui::DestroyContext();
}
