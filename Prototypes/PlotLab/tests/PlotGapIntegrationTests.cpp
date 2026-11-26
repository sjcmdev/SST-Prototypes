#include "PlotLabWorkspace.hpp"
#include "WorksheetPython.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <fstream>

using namespace SST;
using namespace SST::Prototypes::PlotLab;

TEST(PlotGapIntegration, ImportUsesRegistryJobAndPublicationCommand) {
    auto commands=CreateRef<CommandRegistry>();auto undo=CreateRef<UndoStack>();Document doc(commands,undo,CreateRef<EventBus>());
    std::vector<std::string> succeeded;
    const auto observer=commands->AddObserver([&](const CommandExecutionEvent& event){if(event.state==CommandExecutionState::Succeeded)succeeded.push_back(event.id.value);});
    const auto file=std::filesystem::temp_directory_path()/"plotimpl-c-registry.csv";
    {std::ofstream out(file);out<<"X,Y\n1,2\n";}
    ImportRequest request;request.paths={file};request.recipe.mapping={{"X","X","",pl::Role::X},{"Y","Y","",pl::Role::Y}};
    const auto job=doc.ImportCommand(std::move(request));EXPECT_GT(job,0u);auto preview=doc.WaitImport();
    ASSERT_TRUE(preview->items[0].plan->ready());doc.CommitImport(preview);auto result=doc.WaitImport();ASSERT_GT(result->items[0].worksheet,0u);
    EXPECT_EQ(succeeded,(std::vector<std::string>{"plotlab.import","plotlab.import","plotlab.edit"}));EXPECT_EQ(undo->GetUndoDepth(),1u);
    commands->RemoveObserver(observer);std::filesystem::remove(file);
}

TEST(PlotGapIntegration, ImportPanelKeepsNativeImGuiIdsUnique) {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,900};io.DeltaTime=.05f;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.ConfigDebugHighlightIdConflicts=true;
    auto doc=CreateRef<Document>(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),CreateRef<EventBus>());
    doc->importUi.open=true;doc->importUi.recipe.mapping={{"X","X","V",pl::Role::X},{"Y","Y","A",pl::Role::Y}};
    doc->importUi.paths={std::filesystem::temp_directory_path()/"one.dat",std::filesystem::temp_directory_path()/"two.dat"};
    auto session=CreateRef<Session>(doc,nullptr,std::filesystem::path{});ImportPanel panel(*session);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();panel.Render();panel.Render();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);EXPECT_EQ(ImGui::FindWindowByName("Import data")->BeginCount,1);}
    EXPECT_NE(ImGui::FindWindowByName("Import data"),nullptr);EXPECT_FALSE(doc->PollImport());
    const auto file=std::filesystem::temp_directory_path()/"plotimpl-c-wide.csv";
    {std::ofstream out(file);for(int c=0;c<1000;++c)out<<(c?",":"")<<"C"<<c;out<<'\n';for(int c=0;c<1000;++c)out<<(c?",":"")<<1;out<<'\n';}
    panel.Open({file});
    for(int frame=0;frame<8;++frame){ImGui::NewFrame();panel.Render();ImGui::Render();}
    const auto preview=doc->WaitImport();ASSERT_TRUE(preview);ASSERT_EQ(preview->items[0].plan->parsedPreview.columns.size(),1000u);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();panel.Render();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);}
    EXPECT_EQ(doc->project->objects().size(),1u);std::filesystem::remove(file);
    ImGui::DestroyContext();
}
