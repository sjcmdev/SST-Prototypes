#include "PlotLabWorkspace.hpp"
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include <sst/plotlab/PlotEditing.hpp>
#include <cmath>
#include "Core/Input/KeymapResolver.hpp"

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
    static pl::Id XChoice(WorkspacePanel& panel) { return panel.xChoice; }
    static void Process(WorkspacePanel& panel) { panel.ProcessCommands(); }
    static void Action(WorkspacePanel& panel, const std::string& action) { panel.RunAction(action); }
    static void TreeItem(WorkspacePanel& panel, pl::Id id) { panel.Tree(id); }
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
TEST_F(WorksheetUi, PlotDialogsUseOwnerUiFontAndStayInsideMainViewport) {
    const auto graph = doc->PlotColumns(sheet, {columns[1]}, plot::SeriesKind::LineScatter);
    auto& editor = session->plotEditors[graph];
    editor.details = true; editor.axisDialog = plot::AxisSide::Bottom;
    editor.Select(std::to_string(doc->project->children(doc->project->children(graph).front()).front()), false);
    ImGui::GetStyle().FontScaleMain = .75f; GraphFrame(graph); GraphFrame(graph);
    const auto* owner = ImGui::FindWindowByName("Graph test"); const auto* main = ImGui::GetMainViewport();
    int dialogs = 0;
    for (const auto* window : GImGui->Windows) if (window->Active && (std::string_view(window->Name).starts_with("Plot Details###") || std::string_view(window->Name).starts_with("Axis editor###"))) {
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
