#include "PlotLabWorkspace.hpp"
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include <sst/plotlab/PlotEditing.hpp>
#include <cmath>
#include <chrono>
#include <thread>
#include "Core/Input/KeymapResolver.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "WorksheetPython.hpp"

using namespace SST;
using namespace SST::Prototypes::PlotLab;
namespace SST::Prototypes::PlotLab {
struct WorkspaceTestAccess {
    static auto& View(WorkspacePanel& panel, pl::Id sheet) { return panel.sheetViews[sheet]; }
    static void SelectColumn(WorkspacePanel& panel, pl::Id sheet, pl::Id col, bool shift = false, bool ctrl = false) { panel.SelectColumn(sheet, col, shift, ctrl); }
    static void SelectCell(WorkspacePanel& panel, pl::Id sheet, pl::Id col, pl::Id row, bool shift = false) { panel.SelectCell(sheet, col, row, shift); }
    static void Paste(WorkspacePanel& panel, pl::Id sheet, const char* text) { panel.PasteSelection(sheet, text); }
    static void Mask(WorkspacePanel& panel, pl::Id sheet, bool masked) { panel.MaskSelection(sheet, masked); }
    static void Plot(WorkspacePanel& panel, pl::Id sheet, plot::SeriesKind kind) { panel.PlotSelection(sheet, kind); }
    static pl::Id DialogColumn(const WorkspacePanel& panel) { return panel.metadata; }
    static bool ValuesDialog(const WorkspacePanel& panel) { return panel.valuesDialog; }
    static void BeginGraph(WorkspacePanel& panel, pl::Id graph, pl::Id sheet) { panel.BeginGraphDialog(graph, sheet); }
    static void AddGraph(WorkspacePanel& panel, const std::set<pl::Id>& columns) { panel.availableColumns = columns; panel.AddAvailablePlots(); }
    static void NewSheet(WorkspacePanel& panel, pl::Id book) { panel.NewWorksheet(book); }
    static void NewBook(WorkspacePanel& panel, pl::Id folder) { panel.NewWorkbook(folder); }
    static auto Layout(WorkspacePanel& panel) { panel.PrepareLayout(); return std::pair{panel.explorerDock, panel.contentDock}; }
    static bool LayoutRebuilt(const WorkspacePanel& panel) { return panel.layoutRebuilt; }
    static pl::Id XChoice(WorkspacePanel& panel) { return panel.xChoice; }
    static void Process(WorkspacePanel& panel) { panel.ProcessCommands(); }
    static void Action(WorkspacePanel& panel, const std::string& action) { panel.RunAction(action); }
    static void TreeItem(WorkspacePanel& panel, pl::Id id) { panel.Tree(id); }
    static void PythonFormula(WorkspacePanel& panel, pl::Id id,const std::string& source) {panel.EditColumn(id);panel.valuesDialog=true;panel.formulaMode=1;panel.formula=source;}
    static JobId PreviewJob(const WorkspacePanel& panel) {return panel.previewJob;}
    static const auto& Preview(const WorkspacePanel& panel) {return panel.formulaPreview;}
    static JobId ApplyPython(WorkspacePanel& panel,pl::Id id,const std::string& source) {panel.StartPythonFormula(id,source,false);return panel.pythonRun;}
    static int FormulaMode(const WorkspacePanel& panel) {return panel.formulaMode;}
    static void Script(WorkspacePanel& panel,const std::string& source) {panel.OpenScript(0,source);}
    static JobId ParametersJob(const WorkspacePanel& panel) {return panel.parameterJob;}
    static const auto& Parameters(const WorkspacePanel& panel) {return panel.scriptParameters;}
};
}
class WorksheetUi : public testing::Test {
protected:
    Ref<CommandRegistry> registry = CreateRef<CommandRegistry>();
    Ref<UndoStack> undo = CreateRef<UndoStack>();
    Ref<EventBus> bus = CreateRef<EventBus>();
    Ref<Document> doc = CreateRef<Document>(registry, undo, bus);
    Ref<Session> session = CreateRef<Session>(doc, nullptr, std::filesystem::path{});
    WorkspacePanel panel{session};
    pl::Id sheet = 0;
    std::vector<pl::Id> columns, rows;
    void SetUp() override {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {1400, 900}; io.DeltaTime = 1.f / 60;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigDebugHighlightIdConflicts = true;
        pl::Table table; table.names = {"A", "B", "C"}; table.columns.resize(3);
        for (auto& c : table.columns) c.values = std::vector<double>{1, 2, 3, 4, 5};
        table.columns[0].role = pl::Role::X; table.columns[1].role = table.columns[2].role = pl::Role::Y;
        doc->Execute(pl::importTable(*doc->project, doc->project->root(), "Book", table));
        sheet = doc->project->children(doc->project->children(doc->project->root()).front()).front();
        columns = doc->project->children(sheet); rows = std::get<pl::Worksheet>(doc->project->object(sheet).data).rows;
        Frame(); Frame();
    }
    void TearDown() override { ImPlot::DestroyContext(); ImGui::DestroyContext(); }
    void Frame(bool dialogs = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({1300, 800});
        ImGui::Begin("Worksheet test"); panel.WorksheetContents(sheet); ImGui::End();
        if (dialogs) panel.Dialogs();
        ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
    }
    ImGuiTable* Table() {
        auto* window = ImGui::FindWindowByName("Worksheet test");
        return GImGui->Tables.GetByKey(ImHashStr("data", 0, window->GetID(std::to_string(sheet).c_str())));
    }
    void GraphFrame(pl::Id graph) {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({1300, 800});
        ImGui::Begin("Graph test"); panel.GraphContents(graph); ImGui::End(); ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
    }
    void Click(ImVec2 point, int button = 0) {
        auto& io = ImGui::GetIO(); io.AddMousePosEvent(point.x, point.y); Frame();
        io.AddMouseButtonEvent(button, true); Frame(); io.AddMouseButtonEvent(button, false); Frame();
    }
    void Key(ImGuiKey key, bool ctrl = false, bool shift = false, bool dialogs = false) {
        auto& io = ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl, ctrl); io.AddKeyEvent(ImGuiMod_Shift, shift); io.AddKeyEvent(key, true); Frame(dialogs);
        io.AddKeyEvent(key, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); io.AddKeyEvent(ImGuiMod_Shift, false); Frame(dialogs);
    }
};
static void EnsureWorksheetPython() {
    static PythonInterpreter interpreter;
    if(!PythonInterpreter::IsRuntimeInitialized())ASSERT_TRUE(interpreter.Start());
    InstallWorksheetPython();
}
TEST_F(WorksheetUi, EmbeddedRuntimeLoadsNumpyFromItsOwnEnvironment) {
#if DS_PYTHON_CAPI_AVAILABLE
    static PythonInterpreter interpreter;
    if (!PythonInterpreter::IsRuntimeInitialized()) ASSERT_TRUE(interpreter.Start());
    const auto gil = PythonInterpreter::AcquireRuntimeGil();
    const int result = PyRun_SimpleString(
        "import sys, numpy as np\n"
        "from pathlib import Path\n"
        "assert Path(np.__file__).resolve().is_relative_to(Path(sys.prefix).resolve())\n"
        "assert (np.sqrt(np.array([1., 4., 9.])) * 10 + np.arange(1, 4)).tolist() == [11., 22., 33.]\n");
    PythonInterpreter::ReleaseRuntimeGil(gil);
    EXPECT_EQ(result, 0);
#else
    GTEST_SKIP() << "Embedded Python is unavailable";
#endif
}
TEST_F(WorksheetUi, PythonColdStartPreview) {
    static PythonInterpreter interpreter;
    if(!PythonInterpreter::IsRuntimeInitialized())ASSERT_TRUE(interpreter.Start());
    std::string pasted;
    for(int row=1;row<=20;++row)pasted+=std::to_string(row*row)+"\t0\n";
    WorkspaceTestAccess::SelectCell(panel,sheet,columns[0],rows[0]);
    WorkspaceTestAccess::Paste(panel,sheet,pasted.c_str());
    WorkspaceTestAccess::SelectColumn(panel,sheet,columns[1]);Key(ImGuiKey_Q,true,false,true);
    const std::string source="np.sqrt(A) * 10 + i";
    WorkspaceTestAccess::PythonFormula(panel,columns[1],source);
    for(int n=0;n<15;++n)Frame(true);
    const auto job=WorkspaceTestAccess::PreviewJob(panel);
    const auto result=job?doc->WaitPython(job):WorkspaceTestAccess::Preview(panel);
    ASSERT_TRUE(result.ok)<<result.error;
    ASSERT_EQ(result.preview.size(),20u);
    for(int row=1;row<=20;++row)EXPECT_DOUBLE_EQ(result.preview[row-1],11*row);
    const auto applied=doc->WaitPython(WorkspaceTestAccess::ApplyPython(panel,columns[1],source));
    ASSERT_TRUE(applied.ok)<<applied.error;
    doc->project->recompute().wait();
    doc->Execute(doc->project->setCells(columns[0],0,std::vector<double>{100}));
    doc->project->recompute().wait();
    const auto& values=std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values);
    ASSERT_EQ(values.size(),20u);EXPECT_DOUBLE_EQ(values[0],101);EXPECT_DOUBLE_EQ(values[19],220);
    Key(ImGuiKey_Escape,false,false,true);
}
TEST_F(WorksheetUi, PythonColdStartCompletionAndConsoleVariables) {
    static PythonInterpreter interpreter;
    if(!PythonInterpreter::IsRuntimeInitialized())ASSERT_TRUE(interpreter.Start());
    PythonRequest request;request.action=PythonAction::Completion;request.worksheet=sheet;request.source="sheets.";
    const auto completed=doc->WaitPython(doc->SubmitPython(request));
    ASSERT_TRUE(completed.ok)<<completed.error;
    EXPECT_TRUE(std::any_of(completed.completions.begin(),completed.completions.end(),[](const auto& c){return c.insert=="new";}));
    request.action=PythonAction::Console;request.source="iv = sheet";
    ASSERT_TRUE(doc->WaitPython(doc->SubmitPython(request)).ok);
    request.action=PythonAction::Completion;request.source="iv.";request.consoleSession=1;
    const auto variables=doc->WaitPython(doc->SubmitPython(request));
    ASSERT_TRUE(variables.ok)<<variables.error;
    EXPECT_TRUE(std::any_of(variables.completions.begin(),variables.completions.end(),[](const auto& c){return c.insert=="A";}));
}
TEST_F(WorksheetUi, PythonColdStartScriptRunAndStop) {
    static PythonInterpreter interpreter;
    if(!PythonInterpreter::IsRuntimeInitialized())ASSERT_TRUE(interpreter.Start());
    WorkspaceTestAccess::Script(panel,"print('embedded script')\nsheet.B[0] = 7");
    const auto parameters=WorkspaceTestAccess::ParametersJob(panel);
    ASSERT_NE(parameters,0);ASSERT_TRUE(doc->WaitPython(parameters).ok);
    ImGui::NewFrame();panel.PythonWindows();ImGui::Render();
    EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);
    PythonRequest request;request.worksheet=sheet;request.source="print('embedded script')\nsheet.B[0] = 7";
    const auto run=doc->WaitPython(doc->SubmitPython(request));
    ASSERT_TRUE(run.ok)<<run.error;EXPECT_EQ(run.output,"embedded script\n");
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0],7);
    request.source="import builtins\nbuiltins._plotlab_stop_started = True\nsheet.B[0] = 99\nwhile True:\n    pass";
    const auto depth=undo->GetUndoDepth();const auto job=doc->SubmitPython(request);
    // Cancel a running interpreter, rather than only a queued job.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool started=false;
    while(!started && std::chrono::steady_clock::now()<deadline) {
        const auto gil=PythonInterpreter::AcquireRuntimeGil();
        started=PyDict_GetItemString(PyEval_GetBuiltins(),"_plotlab_stop_started")!=nullptr;
        PythonInterpreter::ReleaseRuntimeGil(gil);
        std::this_thread::yield();
    }
    doc->CancelPython(job);const auto stopped=doc->WaitPython(job);
    EXPECT_TRUE(started);
    const auto gil=PythonInterpreter::AcquireRuntimeGil();
    if(started)EXPECT_EQ(PyDict_DelItemString(PyEval_GetBuiltins(),"_plotlab_stop_started"),0);
    PythonInterpreter::ReleaseRuntimeGil(gil);
    EXPECT_FALSE(stopped.ok);EXPECT_EQ(stopped.errorCode,PythonErrorCode::Cancelled);
    EXPECT_EQ(undo->GetUndoDepth(),depth);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0],7);
}
TEST_F(WorksheetUi, PythonCtrlQPreviewCompletionAutoRecalcAndLegacyMode) {
    EnsureWorksheetPython();
    WorkspaceTestAccess::SelectColumn(panel,sheet,columns[1]);Key(ImGuiKey_Q,true,false,true);
    ASSERT_TRUE(WorkspaceTestAccess::ValuesDialog(panel));
    WorkspaceTestAccess::PythonFormula(panel,columns[1],"np.asarray(A) * 2 + i");
    for(int n=0;n<15;++n)Frame(true);
    const auto previewJob=WorkspaceTestAccess::PreviewJob(panel);
    const auto preview=previewJob?doc->WaitPython(previewJob):WorkspaceTestAccess::Preview(panel);ASSERT_FALSE(preview.running);ASSERT_TRUE(preview.ok)<<preview.error;
    Frame(true);EXPECT_EQ(WorkspaceTestAccess::Preview(panel).preview,(std::vector<double>{3,6,9,12,15}));
    const auto applied=doc->WaitPython(WorkspaceTestAccess::ApplyPython(panel,columns[1],"np.asarray(A) * 2 + i"));ASSERT_TRUE(applied.ok)<<applied.error;
    doc->project->recompute().wait();
    doc->Execute(doc->project->setCells(columns[0],0,std::vector<double>{10}));doc->project->recompute().wait();
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0],21);
    PythonRequest completion;completion.action=PythonAction::Completion;completion.source="np.log";completion.worksheet=sheet;
    const auto completions=doc->WaitPython(doc->SubmitPython(completion));ASSERT_TRUE(completions.ok)<<completions.error;
    EXPECT_TRUE(std::any_of(completions.completions.begin(),completions.completions.end(),[](const auto& c){return c.insert=="log10" && !c.signature.empty() && !c.documentation.empty();}));
    pl::Formula old;old.expression="col(A)*2";doc->SetColumnValues(columns[1],old);doc->project->recompute().wait();
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0],20);
    const auto indexed=doc->WaitPython(WorkspaceTestAccess::ApplyPython(panel,columns[1],"sheet.columns[0] * 2"));ASSERT_TRUE(indexed.ok)<<indexed.error;doc->project->recompute().wait();
    std::vector<pl::Object> reordered;for(std::size_t pos=0;pos<columns.size();++pos){auto moved=doc->project->object(columns[pos]);moved.order=pos==2?0:pos+1;reordered.push_back(std::move(moved));}
    doc->Execute(doc->project->change("Move before source",std::move(reordered)));doc->project->recompute().wait();
    EXPECT_EQ(doc->project->children(sheet).front(),columns[2]);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0],20);
    Key(ImGuiKey_Escape,false,false,true);
}
TEST_F(WorksheetUi, PythonPreviewReportsRowAndDoesNotChangeDataOrHistory) {
    EnsureWorksheetPython();
    doc->Execute(doc->project->setCells(columns[0],0,std::vector<double>{1,-1,3,4,5}));
    doc->SetMasks(columns[0],{true,false,false,false,false});
    const auto depth=undo->GetUndoDepth();
    WorkspaceTestAccess::PythonFormula(panel,columns[1],"np.log(A)");
    for(int n=0;n<15;++n)Frame(true);
    const auto id=WorkspaceTestAccess::PreviewJob(panel);
    const auto result=id?doc->WaitPython(id):WorkspaceTestAccess::Preview(panel);ASSERT_FALSE(result.running);EXPECT_FALSE(result.ok);EXPECT_NE(result.error.find("row 2"),std::string::npos)<<result.error;
    Frame(true);EXPECT_EQ(undo->GetUndoDepth(),depth);EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[1],2);
    Key(ImGuiKey_Escape,false,false,true);
}
TEST_F(WorksheetUi, PythonTrustBlocksPreviewAndScriptUntilExplicitTrust) {
    EnsureWorksheetPython();
    doc->SaveScript("Untrusted","print('no automatic execution')");
    auto snapshot=Ref<pl::Project>(doc->project->operationSnapshot().release());doc->Open(snapshot,{});
    ASSERT_FALSE(doc->PythonTrusted());ASSERT_TRUE(doc->HasPython());
    // Render the same banner used in Project Explorer without a desktop backend.
    ImGui::NewFrame();ImGui::Begin("Trust fixture");panel.PythonTrustBanner();EXPECT_EQ(GImGui->LastItemData.ID,0u);ImGui::End();ImGui::Render();
    WorkspaceTestAccess::PythonFormula(panel,columns[1],"A*2");
    for(int n=0;n<15;++n)Frame(true);
    EXPECT_EQ(WorkspaceTestAccess::PreviewJob(panel),0);
    PythonRequest script;script.source="sheet.A[:] = [99]";script.worksheet=sheet;
    EXPECT_THROW(doc->SubmitPython(script),std::invalid_argument);
    doc->TrustPython();EXPECT_TRUE(doc->WaitPython(doc->SubmitPython(script)).ok);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[0]).data).values)[0],99);
    Key(ImGuiKey_Escape,false,false,true);
}
TEST_F(WorksheetUi, PythonScriptEditorRendersParametersAndCancellationKeepsData) {
    EnsureWorksheetPython();
    WorkspaceTestAccess::Script(panel,"params(area=.105, T=(200,330))\nprint(area, T)");
    auto id=WorkspaceTestAccess::ParametersJob(panel);ASSERT_NE(id,0);ASSERT_TRUE(doc->WaitPython(id).ok);
    for(int frame=0;frame<3;++frame){ImGui::NewFrame();panel.PythonWindows();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);}
    ASSERT_EQ(WorkspaceTestAccess::Parameters(panel).size(),2);
    auto* window=ImGui::FindWindowByName("Python Script");ASSERT_NE(window,nullptr);EXPECT_GT(window->Size.x,600);
    PythonRequest run;run.source="sheet.A[0] = 99\nwhile True:\n    pass";run.worksheet=sheet;
    const auto depth=undo->GetUndoDepth();id=doc->SubmitPython(run);doc->CancelPython(id);const auto result=doc->WaitPython(id);
    EXPECT_FALSE(result.ok);EXPECT_EQ(result.errorCode,PythonErrorCode::Cancelled);EXPECT_EQ(undo->GetUndoDepth(),depth);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[0]).data).values)[0],1);
}
TEST_F(WorksheetUi, HeaderClickShiftControlAndKeyboardContext) {
    auto* table = Table(); ASSERT_NE(table, nullptr);
    const float y = table->OuterRect.Min.y + ImGui::GetTextLineHeight() / 2 + 3;
    Click({(table->Columns[2].MinX + table->Columns[2].MaxX) / 2, y});
    EXPECT_EQ(WorkspaceTestAccess::View(panel, sheet).columns, (std::set<pl::Id>{columns[1]}));
    EXPECT_EQ(WorkspaceTestAccess::DialogColumn(panel), 0);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true); Frame();
    Click({(table->Columns[3].MinX + table->Columns[3].MaxX) / 2, y});
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false); Frame();
    EXPECT_EQ(WorkspaceTestAccess::View(panel, sheet).columns, (std::set<pl::Id>{columns[1], columns[2]}));
    Key(ImGuiKey_F10, false, true);
    EXPECT_GT(GImGui->OpenPopupStack.Size, 0);
    Click({1280, 770});
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}
TEST_F(WorksheetUi, CellRangesArrowsAndPlainPasteUndoAreAtomic) {
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[1]);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[2], rows[3], true);
    EXPECT_EQ(doc->project->selection().rows, (std::vector<pl::Id>{rows[1], rows[2], rows[3]}));
    EXPECT_EQ(WorkspaceTestAccess::View(panel, sheet).columns.size(), 2);
    Key(ImGuiKey_DownArrow, false, true);
    EXPECT_EQ(doc->project->selection().rows.size(), 4);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[0]);
    const auto depth = undo->GetUndoDepth();
    WorkspaceTestAccess::Paste(panel, sheet, "10\t20\n30\t40\n");
    EXPECT_EQ(undo->GetUndoDepth(), depth + 1);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[2]).data).values)[1], 40);
    ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0], 1);
    EXPECT_THROW(WorkspaceTestAccess::Paste(panel, sheet, "10\tBAD"), std::invalid_argument);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[0], 1);
}
TEST_F(WorksheetUi, MouseDragExtendsFromPressedCellAndShiftClickKeepsAnchor) {
    auto* table = Table(); ASSERT_NE(table, nullptr);
    const float rowHeight = table->RowPosY2 - table->RowPosY1;
    const float top = table->RowPosY1 - rowHeight * static_cast<float>(rows.size() - 1);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent((table->Columns[2].MinX + table->Columns[2].MaxX) / 2, top + rowHeight / 2); Frame();
    io.AddMouseButtonEvent(0, true); Frame();
    io.AddMousePosEvent((table->Columns[3].MinX + table->Columns[3].MaxX) / 2, top + rowHeight * 3.5f); Frame();
    io.AddMouseButtonEvent(0, false); Frame();
    EXPECT_EQ(doc->project->selection().rows, (std::vector<pl::Id>{rows[0], rows[1], rows[2], rows[3]}));
    EXPECT_EQ(WorkspaceTestAccess::View(panel, sheet).columns, (std::set<pl::Id>{columns[1], columns[2]}));
    io.AddKeyEvent(ImGuiMod_Shift, true); Frame();
    Click({(table->Columns[3].MinX + table->Columns[3].MaxX) / 2, top + rowHeight * 4.5f});
    io.AddKeyEvent(ImGuiMod_Shift, false); Frame();
    EXPECT_EQ(doc->project->selection().rows.size(), 5u);
}
TEST_F(WorksheetUi, GridStartsBelowOneToolbarAndSeventyColumnsScroll) {
    for (int n = 3; n < 70; ++n) doc->AddColumn(sheet);
    Frame(); Frame();
    const auto* table = Table(); ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->ColumnsCount, 71);
    EXPECT_LT(table->OuterRect.Min.y, 100);
    EXPECT_GT(table->OuterRect.GetHeight(), 600);
    EXPECT_GT(table->InnerWindow->ScrollMax.x, 0);
    auto& io = ImGui::GetIO(); io.AddMousePosEvent(500, 300); Frame(); io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddMouseWheelEvent(0, 1); Frame();
    EXPECT_GT(WorkspaceTestAccess::View(panel, sheet).zoom, 1);
    io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
}
TEST_F(WorksheetUi, ShortcutsOpenSeparateDialogsEscapeAndAddColumns) {
    WorkspaceTestAccess::SelectColumn(panel, sheet, columns[1]);
    Key(ImGuiKey_Q, true, false, true);
    EXPECT_EQ(WorkspaceTestAccess::DialogColumn(panel), columns[1]);
    EXPECT_TRUE(WorkspaceTestAccess::ValuesDialog(panel));
    auto* values = ImGui::FindWindowByName("Set Values"); ASSERT_NE(values, nullptr);
    EXPECT_GE(values->Size.x, 560);
    Key(ImGuiKey_Escape, false, false, true);
    EXPECT_EQ(WorkspaceTestAccess::DialogColumn(panel), 0);
    Key(ImGuiKey_F4, false, false, true);
    EXPECT_FALSE(WorkspaceTestAccess::ValuesDialog(panel));
    auto* properties = ImGui::FindWindowByName("Column Properties"); ASSERT_NE(properties, nullptr);
    EXPECT_GE(properties->Size.x, 560);
    Key(ImGuiKey_Escape, false, false, true);
    Key(ImGuiKey_D, true);
    EXPECT_EQ(doc->project->children(sheet).size(), 4);
}
TEST_F(WorksheetUi, TypedSetValuesAndMaskPreserveSourceAndExcludeAnalysis) {
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[0], rows[1]);
    WorkspaceTestAccess::Mask(panel, sheet, true);
    EXPECT_NE(doc->project->maskColor(columns[0], 1), 0u);
    const auto values = doc->project->analysisValues(columns[0]);
    EXPECT_EQ(std::count_if(values.begin(), values.end(), [](double n) { return std::isfinite(n); }), 4);
    pl::Formula f; f.expression = "col(A)*2"; f.excludeMasked = true;
    doc->SetColumnValues(columns[1], std::move(f)); doc->project->recompute().wait();
    const auto& result = std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values);
    EXPECT_EQ(result[0], 2); EXPECT_TRUE(std::isnan(result[1]));
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[0]).data).values)[1], 2);
    WorkspaceTestAccess::Mask(panel, sheet, false);
    EXPECT_EQ(doc->project->maskColor(columns[0], 1), 0u);
}
TEST_F(WorksheetUi, PlotSelectionCreatesSecondGraphAndLayerContentsAddsAndRemoves) {
    WorkspaceTestAccess::SelectColumn(panel, sheet, columns[1]);
    WorkspaceTestAccess::SelectColumn(panel, sheet, columns[2], true);
    WorkspaceTestAccess::Plot(panel, sheet, plot::SeriesKind::LineScatter);
    ASSERT_EQ(session->graphs.size(), 1u); const auto first = *session->graphs.begin();
    auto layer = doc->project->children(first).front();
    EXPECT_EQ(doc->project->children(layer).size(), 2u);
    WorkspaceTestAccess::Plot(panel, sheet, plot::SeriesKind::Column);
    ASSERT_EQ(session->graphs.size(), 2u);
    WorkspaceTestAccess::BeginGraph(panel, first, sheet); Frame(true);
    auto* contents = ImGui::FindWindowByName("Layer Contents"); ASSERT_NE(contents, nullptr);
    EXPECT_GE(contents->Size.x, 680);
    WorkspaceTestAccess::AddGraph(panel, {columns[1]});
    EXPECT_EQ(doc->project->children(layer).size(), 3u);
    const auto added = doc->project->children(layer).back();
    doc->Execute(doc->project->change("Remove from layer", {}, {added}));
    EXPECT_EQ(doc->project->children(layer).size(), 2u);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(doc->project->children(layer).size(), 3u);
    Key(ImGuiKey_Escape, false, false, true);
}
TEST_F(WorksheetUi, ImplicitXColumnBoundsGroupColoursAndF2UseSharedEditor) {
    auto object = doc->project->object(columns[0]); std::get<pl::Column>(object.data).role = pl::Role::Y;
    doc->Execute(doc->project->change("No X designation", {std::move(object)}));
    const auto graph = doc->PlotColumns(sheet, {columns[1], columns[2]}, plot::SeriesKind::Column);
    const auto layer = doc->project->children(graph).front(); const auto plots = doc->project->children(layer);
    const auto entry = doc->project->series(plots[0]);
    EXPECT_EQ(entry.data.x[0], 1); EXPECT_EQ(entry.data.x[4], 5);
    plot::PlotSpec spec; spec.autoFit = plot::AutoFit::Once;
    const auto bounds = plot::ResolveBounds(spec, &entry, 1, {}); ASSERT_TRUE(bounds);
    EXPECT_LT(bounds->minX, 1); EXPECT_LE(bounds->minY, 0);
    plot::PlotEdit edit; edit.kind = plot::PlotEditKind::GroupColors; edit.series = {std::to_string(plots[0])};
    doc->Execute(pl::editPlot(*doc->project, layer, edit));
    EXPECT_NE(doc->project->series(plots[0]).style.color, doc->project->series(plots[1]).style.color);
    const auto roundtrip = pl::Project::fromJson(doc->project->toJson());
    EXPECT_EQ(roundtrip.series(plots[0]).kind, plot::SeriesKind::Column);
    GraphFrame(graph); GraphFrame(graph);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F2, true); GraphFrame(graph);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F2, false); GraphFrame(graph);
    EXPECT_TRUE(session->plotEditors[graph].details);
    EXPECT_FALSE(session->plotEditors[graph].selected.empty());
}
TEST_F(WorksheetUi, NewProjectDiscardsOldDialogAndSelection) {
    WorkspaceTestAccess::SelectColumn(panel, sheet, columns[1]); Key(ImGuiKey_Q, true, false, true);
    ASSERT_NE(WorkspaceTestAccess::DialogColumn(panel), 0u);
    doc->New();
    pl::Table table; table.names = {"X", "Y"}; table.columns.resize(2);
    table.columns[0].role = pl::Role::X; table.columns[1].role = pl::Role::Y;
    for (auto& c : table.columns) c.values = std::vector<double>{10, 20};
    doc->Execute(pl::importTable(*doc->project, doc->project->root(), "Replacement", table));
    sheet = doc->project->children(doc->project->children(doc->project->root()).front()).front();
    Frame(true);
    EXPECT_EQ(WorkspaceTestAccess::DialogColumn(panel), 0u);
    EXPECT_TRUE(WorkspaceTestAccess::View(panel, sheet).columns.empty());
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 0);
}
TEST_F(WorksheetUi, SheetTabsUseWorkbookIdentityAndCreateUsableGrid) {
    const auto book = doc->project->object(sheet).parent;
    WorkspaceTestAccess::NewSheet(panel, book); const auto second = session->selected;
    ASSERT_NE(second, sheet); EXPECT_EQ(doc->project->children(second).size(), 2u);
    EXPECT_EQ(std::get<pl::Worksheet>(doc->project->object(second).data).rows.size(), 30u);
    Frame(); Frame();
    auto* window = ImGui::FindWindowByName("Worksheet test");
    auto* tabs = GImGui->TabBars.GetByKey(ImHashStr("Sheets", 0, window->GetID(("book" + std::to_string(book)).c_str())));
    ASSERT_NE(tabs, nullptr); ASSERT_GE(tabs->Tabs.Size, 2);
    Click({tabs->BarRect.Min.x + tabs->Tabs[1].Offset + tabs->Tabs[1].Width / 2, tabs->BarRect.GetCenter().y});
    EXPECT_EQ(session->selected, second);
    ASSERT_TRUE(undo->Undo()); EXPECT_FALSE(doc->project->objects().contains(second));
}
TEST_F(WorksheetUi, GraphContextAndMaskedSnapshotKeepSourceValues) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[1]); WorkspaceTestAccess::Mask(panel, sheet, true);
    GraphFrame(graph); GraphFrame(graph);
    auto* window = ImGui::FindWindowByName("Graph test"); auto* chart = GImPlot->Plots.GetByKey(window->GetID("##graph"));
    ASSERT_NE(chart, nullptr); const auto point = chart->PlotRect.GetCenter();
    auto& io = ImGui::GetIO(); io.AddMousePosEvent(point.x, point.y); GraphFrame(graph);
    io.AddMouseButtonEvent(1, true); GraphFrame(graph); io.AddMouseButtonEvent(1, false); GraphFrame(graph);
    ASSERT_GT(GImGui->OpenPopupStack.Size, 0);
    ASSERT_NE(GImGui->OpenPopupStack.back().Window, nullptr);
    EXPECT_EQ(GImGui->OpenPopupStack.back().PopupId, window->GetID("Graph actions"));
    const auto snapshot = pl::snapshotGraph(*doc->project, graph, session->views.at(graph), std::array<double,2>{100, 70});
    ASSERT_EQ(snapshot->series.size(), 1u); EXPECT_EQ(snapshot->series[0].maskedRows, (std::vector<std::size_t>{1}));
    EXPECT_EQ(snapshot->series[0].y[1], 2); EXPECT_EQ(snapshot->widthMm, 100);
    const auto restored = pl::snapshotFromJson(pl::snapshotJson(*snapshot));
    EXPECT_EQ(restored.series[0].maskedRows, snapshot->series[0].maskedRows);
    EXPECT_THROW(pl::snapshotGraph(*doc->project, graph, session->views.at(graph), std::array<double,2>{-1, 70}), std::invalid_argument);
}
TEST_F(WorksheetUi, UnmaskCellPreservesOtherCellsFromWholeRowMask) {
    auto o = doc->project->object(sheet); std::get<pl::Worksheet>(o.data).masks[rows[1]] = IM_COL32(255, 65, 65, 180);
    doc->Execute(doc->project->change("Mask row", {std::move(o)}));
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[1]); WorkspaceTestAccess::Mask(panel, sheet, false);
    EXPECT_EQ(doc->project->maskColor(columns[1], 1), 0u);
    EXPECT_NE(doc->project->maskColor(columns[0], 1), 0u);
    EXPECT_NE(doc->project->maskColor(columns[2], 1), 0u);
    ASSERT_TRUE(undo->Undo()); EXPECT_NE(doc->project->maskColor(columns[1], 1), 0u);
}
TEST(PlotLabFontErrors, MissingDependenciesAreShortAndNeverExposeTracebacks) {
    const auto message = FriendlyFontError("Traceback (most recent call last):\n  File export.py, line 1\nModuleNotFoundError: No module named 'matplotlib'\n");
    EXPECT_EQ(message.find("Traceback"), std::string::npos); EXPECT_LT(message.size(), 150u);
    EXPECT_NE(message.find("dependencies"), std::string::npos);
    EXPECT_EQ(FriendlyFontError("Embedded Python is unavailable").find("Traceback"), std::string::npos);
}
TEST_F(WorksheetUi, SelectionPaintsAccentCellsAndActiveOutlineAtUiScale) {
    ImGui::GetStyle().FontScaleMain = .75f;
    ImGui::GetStyle().Colors[ImGuiCol_Header] = {0,0,0,0};
    WorkspaceTestAccess::SelectColumn(panel, sheet, columns[1]); Frame(); Frame();
    const auto count = [&](ImU32 color) {
        unsigned found = 0; const auto* table = Table();
        for (const auto* window : GImGui->Windows) if (window->Active)
            for (const auto& v : window->DrawList->VtxBuffer) if (v.col == color && table->OuterRect.Contains(v.pos)) ++found;
        return found;
    };
    EXPECT_GT(count(ImGui::GetColorU32(ImGuiCol_CheckMark, .35f)), 16u);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[1]);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[2], rows[3], true); Frame();
    EXPECT_GT(count(ImGui::GetColorU32(ImGuiCol_CheckMark, .35f)), 16u);
    EXPECT_GT(count(ImGui::GetColorU32(ImGuiCol_CheckMark)), 0u);
    EXPECT_EQ(WorkspaceTestAccess::View(panel, sheet).columns.size(), 2u);
    EXPECT_EQ(doc->project->selection().rows.size(), 3u);
    auto* table = Table();
    EXPECT_NEAR(table->RowPosY2 - table->RowPosY1, ImGui::FindWindowByName("Worksheet test")->FontRefSize + ImGui::GetStyle().CellPadding.y * 2, 1);
}
TEST_F(WorksheetUi, EmptyCellDisplayIsBlankWhileCopyKeepsPrecision) {
    doc->Execute(doc->project->setCells(columns[1], 0, std::vector<double>{std::numeric_limits<double>::quiet_NaN(), 1.23456789012345}));
    EXPECT_TRUE(pl::formatValue(std::numeric_limits<double>::quiet_NaN(), {}).empty());
    EXPECT_EQ(pl::fullPrecision(1.23456789012345), "1.23456789012345");
    Frame(); EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
}
TEST_F(WorksheetUi, NormalRescalesDataAndFormulaEditsWithMarginAndFixedAxisStaysFixed) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    GraphFrame(graph); GraphFrame(graph);
    doc->Execute(doc->project->setCells(columns[1], 0, std::vector<double>{0, 100, 200, 300, 400}));
    GraphFrame(graph); ASSERT_TRUE(session->views[graph].viewport);
    EXPECT_GT(session->views[graph].viewport->maxY, 400);
    pl::Formula formula; formula.expression = "col(A)*.2";
    doc->SetColumnValues(columns[1], formula); doc->project->recompute().wait(); GraphFrame(graph);
    EXPECT_LT(session->views[graph].viewport->maxY, 2);
    EXPECT_GT(session->views[graph].viewport->maxY, 1);
    const auto layer = doc->project->children(graph).front();
    plot::PlotEdit edit; edit.kind = plot::PlotEditKind::Axis; edit.side = plot::AxisSide::Left;
    edit.axis = std::get<pl::Layer>(doc->project->object(layer).data).spec.y;
    edit.axis->range = std::array<double,2>{-10, 500}; edit.axis->rescale = plot::RescaleMode::Fixed;
    doc->Execute(pl::editPlot(*doc->project, layer, edit)); GraphFrame(graph);
    doc->Execute(doc->project->setCells(columns[0], 0, std::vector<double>{10, 20, 30, 40, 50}));
    doc->project->recompute().wait(); GraphFrame(graph);
    EXPECT_DOUBLE_EQ(session->views[graph].viewport->minY, -10); EXPECT_DOUBLE_EQ(session->views[graph].viewport->maxY, 500);
    EXPECT_GT(session->views[graph].viewport->maxX, 50);
    auto* window = ImGui::FindWindowByName("Graph test"); auto* plot = GImPlot->Plots.GetByKey(window->GetID("##graph"));
    ASSERT_NE(plot, nullptr); EXPECT_GT(plot->FrameRect.GetWidth(), 1150);
    EXPECT_GT(plot->PlotRect.GetWidth() / plot->FrameRect.GetWidth(), .65f);
}
TEST_F(WorksheetUi, GraphNamesAndLayerContentsUseRealDesignatedXAndMaskedStylePersists) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    EXPECT_EQ(doc->project->object(graph).name, "Graph1");
    const auto second = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::Scatter);
    EXPECT_EQ(doc->project->object(second).name, "Graph2");
    WorkspaceTestAccess::BeginGraph(panel, graph, 0); EXPECT_EQ(WorkspaceTestAccess::XChoice(panel), columns[0]);
    const auto layer = doc->project->children(graph).front(), series = doc->project->children(layer).front();
    EXPECT_EQ(doc->project->series(series).style.maskedColor, (plot::Color{1,0,0,1}));
    plot::PlotEdit edit; edit.kind = plot::PlotEditKind::Style; edit.series = {std::to_string(series)}; edit.style = doc->project->series(series).style;
    edit.style.maskedColor = {0,1,0,1}; doc->Execute(pl::editPlot(*doc->project, layer, edit));
    auto restored = pl::Project::fromJson(doc->project->toJson()); EXPECT_EQ(restored.series(series).style.maskedColor, edit.style.maskedColor);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(doc->project->series(series).style.maskedColor, (plot::Color{1,0,0,1}));
    const auto& spec = std::get<pl::Layer>(doc->project->object(layer).data).spec;
    EXPECT_TRUE(spec.legend.outside); EXPECT_EQ(spec.legend.position, plot::LegendPosition::OutsideRight);
    WorkspaceTestAccess::NewBook(panel, doc->project->root());
    EXPECT_EQ(doc->project->object(session->selected).name, "Sheet1");
    EXPECT_EQ(doc->project->object(doc->project->object(session->selected).parent).name, "Book1");
    doc->New(); WorkspaceTestAccess::NewBook(panel, doc->project->root());
    const auto newSheet = session->selected; const auto cols = doc->project->children(newSheet);
    EXPECT_EQ(doc->project->object(doc->AddGraph(doc->project->root(), newSheet, cols[0], cols[1])).name, "Graph1");
}
TEST_F(WorksheetUi, CommandMenusHaveShortcutsAndExecuteTypedEditsWithUndo) {
    auto keys = CreateRef<KeymapResolver>(); panel.RegisterCommands(keys);
    EXPECT_TRUE(keys->GetConflicts().empty());
    for (const auto& [id, category, shortcut] : {std::tuple{"values","Column","Ctrl+Q"}, {"properties","Column","F4"}, {"add_columns","Column","Ctrl+D"}, {"rescale","Graph","Ctrl+R"}, {"details","Graph","F2"}, {"save","File","Ctrl+S"}, {"copy","Edit","Ctrl+C"}}) {
        auto meta = registry->GetMeta(CommandID{"plotlab.ui." + std::string(id)}); ASSERT_TRUE(meta);
        EXPECT_EQ(meta->category, category); EXPECT_EQ(meta->description, shortcut);
    }
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[1], rows[1]);
    WorkspaceTestAccess::SelectCell(panel, sheet, columns[2], rows[3], true);
    session->selected = sheet;
    const auto depth = undo->GetUndoDepth(); ASSERT_TRUE(registry->Execute(CommandID{"plotlab.ui.clear"}));
    WorkspaceTestAccess::Process(panel);
    EXPECT_EQ(undo->GetUndoDepth(), depth + 1);
    const auto& values = std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values);
    EXPECT_DOUBLE_EQ(values[0], 1); EXPECT_TRUE(std::isnan(values[1])); EXPECT_TRUE(std::isnan(values[3])); EXPECT_DOUBLE_EQ(values[4], 5);
    ASSERT_TRUE(undo->Undo());
    EXPECT_DOUBLE_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc->project->object(columns[1]).data).values)[1], 2);
    CommandContext typing(ContextID{"core.keybinding"}); ImGui::GetIO().WantTextInput = true;
    ASSERT_TRUE(registry->Execute(CommandID{"plotlab.ui.add_columns"}, std::move(typing))); WorkspaceTestAccess::Process(panel);
    EXPECT_EQ(doc->project->children(sheet).size(), 3u); ImGui::GetIO().WantTextInput = false;
    ImGui::NewFrame(); ASSERT_TRUE(ImGui::BeginMainMenuBar()); panel.RenderMenus(); panel.RenderMenus(true); ImGui::EndMainMenuBar(); ImGui::Render();
    EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
}
TEST_F(WorksheetUi, DefaultAndResetLayoutSplitExplorerLeftAndContentInsideMainViewport) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const auto frame = [&] {
        ImGui::NewFrame(); const auto* main = ImGui::GetMainViewport();
        ImGui::DockSpaceOverViewport(main->ID, main);
        const auto docks = WorkspaceTestAccess::Layout(panel);
        ImGui::SetNextWindowDockID(docks.first, ImGuiCond_Always); ImGui::Begin("Explorer dock test"); ImGui::End();
        ImGui::SetNextWindowDockID(docks.second, ImGuiCond_Always); ImGui::Begin("Content dock test"); ImGui::End();
        ImGui::Render(); return docks;
    };
    const auto docks = frame(); frame();
    auto* explorer = ImGui::DockBuilderGetNode(docks.first); auto* content = ImGui::DockBuilderGetNode(docks.second);
    ASSERT_NE(explorer, nullptr); ASSERT_NE(content, nullptr);
    EXPECT_LT(explorer->Pos.x, content->Pos.x); EXPECT_EQ(explorer->ParentNode, content->ParentNode);
    EXPECT_LT(explorer->Size.x, content->Size.x);
    panel.ResetLayout(); const auto reset = frame(); frame();
    EXPECT_NE(reset.first, 0u); EXPECT_NE(reset.second, 0u);
    EXPECT_EQ(ImGui::GetPlatformIO().Viewports.Size, 1);
}
TEST_F(WorksheetUi, RestoredSplitLayoutDoesNotRedockDocumentsOrReopenExplorer) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::NewFrame();
    const auto root = ImGui::GetMainViewport()->ID;
    ImGui::DockSpaceOverViewport(root);
    ImGuiID left = 0, center = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, .3f, &left, &center);
    ImGui::DockBuilderFinish(root);
    panel.SetVisible(false);
    const auto docks = WorkspaceTestAccess::Layout(panel);
    EXPECT_EQ(docks.first, left); EXPECT_EQ(docks.second, center);
    EXPECT_FALSE(WorkspaceTestAccess::LayoutRebuilt(panel));
    EXPECT_FALSE(panel.IsVisible());
    panel.ResetLayout(); WorkspaceTestAccess::Layout(panel);
    EXPECT_TRUE(WorkspaceTestAccess::LayoutRebuilt(panel)); EXPECT_TRUE(panel.IsVisible());
    WorkspaceTestAccess::Layout(panel); EXPECT_FALSE(WorkspaceTestAccess::LayoutRebuilt(panel));
    ImGui::Render();
}
TEST_F(WorksheetUi, StaleIniSplitWithoutCentralNodeRebuildsLayout) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const auto root=ImGui::GetMainViewport()->ID;
    char ini[512];
    std::snprintf(ini,sizeof(ini),
        "[Docking][Data]\nDockSpace ID=0x%08X Pos=0,0 Size=1400,900 Split=X\n"
        " DockNode ID=0x00000011 Parent=0x%08X SizeRef=300,900\n"
        " DockNode ID=0x00000012 Parent=0x%08X SizeRef=1100,900\n",root,root,root);
    ImGui::LoadIniSettingsFromMemory(ini);
    auto* stale=ImGui::DockBuilderGetNode(root);
    ASSERT_NE(stale,nullptr);ASSERT_TRUE(stale->IsSplitNode());
    ASSERT_EQ(ImGui::DockBuilderGetCentralNode(root),nullptr);
    ImGui::NewFrame();
    const auto docks=WorkspaceTestAccess::Layout(panel);
    EXPECT_TRUE(WorkspaceTestAccess::LayoutRebuilt(panel));
    EXPECT_NE(docks.first,0u);EXPECT_NE(docks.second,0u);EXPECT_NE(docks.first,docks.second);
    ASSERT_NE(ImGui::DockBuilderGetCentralNode(root),nullptr);
    EXPECT_EQ(ImGui::DockBuilderGetCentralNode(root)->ID,docks.second);
    ImGui::Render();
    ImGui::NewFrame();WorkspaceTestAccess::Layout(panel);
    EXPECT_FALSE(WorkspaceTestAccess::LayoutRebuilt(panel));ImGui::Render();
}
TEST_F(WorksheetUi, PlotDialogsUseOwnerUiFontAndStayInsideMainViewport) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    auto& editor = session->plotEditors[graph];
    editor.details = true; editor.axisDialog = plot::AxisSide::Bottom;
    editor.Select(std::to_string(doc->project->children(doc->project->children(graph).front()).front()), false);
    ImGui::GetStyle().FontScaleMain = .75f; GraphFrame(graph); GraphFrame(graph);
    const auto* owner = ImGui::FindWindowByName("Graph test"); const auto* main = ImGui::GetMainViewport();
    int dialogs = 0;
    for (const auto* window : GImGui->Windows) if (window->Active && !(window->Flags & ImGuiWindowFlags_ChildWindow) && (std::string_view(window->Name).starts_with("Plot Details###") || std::string_view(window->Name).starts_with("Axis editor###"))) {
        ++dialogs; EXPECT_NEAR(window->FontRefSize, owner->FontRefSize, 1);
        EXPECT_GE(window->Pos.x, main->WorkPos.x); EXPECT_GE(window->Pos.y, main->WorkPos.y);
        EXPECT_LE(window->Pos.x + window->Size.x, main->WorkPos.x + main->WorkSize.x);
        EXPECT_LE(window->Pos.y + window->Size.y, main->WorkPos.y + main->WorkSize.y);
        EXPECT_LE(window->ContentSize.y, window->InnerRect.GetHeight());
    }
    EXPECT_EQ(dialogs, 2);
}
TEST_F(WorksheetUi, ExplorerGraphClickRequestsActivationAndWorkbookShowsExistingSheet) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    const auto frame = [&] {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({300,500});
        ImGui::Begin("Explorer test"); WorkspaceTestAccess::TreeItem(panel, graph);
        const ImRect item(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); ImGui::End(); ImGui::Render(); return item;
    };
    frame(); const auto item = frame(); auto& io = ImGui::GetIO();
    io.AddMousePosEvent(item.GetCenter().x, item.GetCenter().y); frame();
    io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame();
    EXPECT_EQ(session->activate, graph); EXPECT_EQ(session->selected, graph); EXPECT_TRUE(session->graphs.contains(graph));
    session->sheets.clear(); const auto book = doc->project->object(sheet).parent; session->Show(book);
    EXPECT_TRUE(session->sheets.contains(sheet)); EXPECT_EQ(session->activate, book);
}
TEST_F(WorksheetUi, SaveAndReopenRestoreTheTypedGraphViewport) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    session->RefreshWindows(); GraphFrame(graph); GraphFrame(graph);
    session->views[graph].viewport = plot::Bounds{1,4,1,6};
    const auto file = std::filesystem::temp_directory_path()/"plotui2-workspace-view.plotlab";
    doc->path = file; ASSERT_TRUE(session->Save()); EXPECT_FALSE(session->recentFiles.empty());
    doc->Open(file); session->RefreshWindows(); ASSERT_TRUE(session->views[graph].viewport);
    EXPECT_EQ(*session->views[graph].viewport, (plot::Bounds{1,4,1,6}));
    GraphFrame(graph); EXPECT_EQ(*session->views[graph].viewport, (plot::Bounds{1,4,1,6}));
    std::filesystem::remove(file);
}

TEST_F(WorksheetUi, PhysicalExportSizeAndFontsDoNotScaleTheInteractiveGraph) {
    const auto graph = doc->PlotColumns(sheet,{columns[1]},plot::SeriesKind::LineScatter);
    auto object=doc->project->object(graph); auto& settings=std::get<pl::Graph>(object.data);
    settings.widthMm=300; settings.heightMm=20; settings.fontSizePt=72;
    doc->project->change("Export settings",{object}).apply(*doc->project);
    for (float scale : {1.f,1.5f}) {
        ImGui::GetStyle().FontScaleMain=scale; GraphFrame(graph); GraphFrame(graph);
        auto* owner=ImGui::FindWindowByName("Graph test");
        const auto* plot=GImPlot->Plots.GetByKey(owner->GetID("##graph")); ASSERT_NE(plot,nullptr);
        EXPECT_GE(plot->FrameRect.GetWidth(),owner->InnerRect.GetWidth()*.9f);
        EXPECT_GE(plot->FrameRect.GetHeight(),(owner->WorkRect.Max.y-plot->FrameRect.Min.y)*.9f);
        ASSERT_TRUE(session->views[graph].legendLayout);
        const auto preview=plot::PreviewMetrics{(96.f/72)*scale,true};
        EXPECT_NEAR(preview.ToPixels(session->views[graph].legendLayout->fontSizePt),owner->FontRefSize,owner->FontRefSize*.15f);
    }
    const auto& saved=std::get<pl::Graph>(doc->project->object(graph).data);
    EXPECT_DOUBLE_EQ(saved.widthMm,300); EXPECT_DOUBLE_EQ(saved.heightMm,20); EXPECT_DOUBLE_EQ(saved.fontSizePt,72);
}

TEST_F(WorksheetUi, AxisEditorListsLayersAndRestoresEachLayerViewport) {
    const auto graph=doc->PlotColumns(sheet,{columns[1]},plot::SeriesKind::LineScatter);
    const auto first=doc->project->children(graph).front();
    auto second=doc->project->object(first);second.id=doc->project->reserveId();second.name="Second layer";second.order=1;
    std::get<pl::Layer>(second.data).view.viewport=plot::Bounds{10,20,30,40};
    doc->Execute(doc->project->change("Second layer",{second}));
    auto& editor=session->plotEditors[graph];editor.axisDialog=plot::AxisSide::Bottom;
    GraphFrame(graph);ASSERT_EQ(editor.layers.size(),2u);EXPECT_EQ(editor.layer,first);
    editor.layer=second.id;GraphFrame(graph);EXPECT_EQ(editor.viewLayer,second.id);
    ASSERT_TRUE(session->views[graph].viewport);EXPECT_NEAR(session->views[graph].viewport->minX,10,1e-9);
}
