#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "PlotLabDocument.hpp"
#include "PlotLabExport.hpp"
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace SST;
using namespace SST::Prototypes::PlotLab;
class PlotLabApp : public testing::Test
{
  protected:
    Ref<CommandRegistry> registry = CreateRef<CommandRegistry>();
    Ref<UndoStack> undo = CreateRef<UndoStack>();
    Ref<EventBus> bus = CreateRef<EventBus>();
    Document doc{registry, undo, bus};
    pl::Id book = 0, sheet = 0, x = 0, y = 0, graph = 0;
    void SetUp() override
    {
        doc.Execute(
            pl::importTable(*doc.project, doc.project->root(), "Book", pl::CsvImporter{}.read("X,Y\n1,2\n2,4\n3,6\n")));
        book = doc.project->children(doc.project->root()).front();
        sheet = doc.project->children(book).front();
        auto c = doc.project->children(sheet);
        x = c[0];
        y = c[1];
        graph = doc.AddGraph(doc.project->root(), sheet, x, y);
        undo->Clear();
        undo->MarkClean();
    }
    const std::vector<double> &Values(pl::Id id)
    {
        return std::get<std::vector<double>>(std::get<pl::Column>(doc.project->object(id).data).values);
    }
    std::filesystem::path Temp(const char *name)
    {
        return std::filesystem::temp_directory_path() /
               (std::string("plotlab-pl3-") + std::to_string(doc.project->id()) + name);
    }
};
TEST_F(PlotLabApp, CoreCommandUndoRedoKeepsObjectAndRowIds)
{
    const auto rows = std::get<pl::Worksheet>(doc.project->object(sheet).data).rows;
    doc.Execute(doc.project->setCells(y, 1, std::vector<double>{9}));
    EXPECT_DOUBLE_EQ(Values(y)[1], 9);
    EXPECT_EQ(undo->GetUndoDepth(), 1);
    ASSERT_TRUE(undo->Undo());
    EXPECT_DOUBLE_EQ(Values(y)[1], 4);
    ASSERT_TRUE(undo->Redo());
    EXPECT_DOUBLE_EQ(Values(y)[1], 9);
    EXPECT_EQ(doc.project->children(sheet), (std::vector<pl::Id>{x, y}));
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(sheet).data).rows, rows);
    doc.ResizeRows(sheet);
    const auto newRow = std::get<pl::Worksheet>(doc.project->object(sheet).data).rows.back();
    ASSERT_TRUE(undo->Undo());
    ASSERT_TRUE(undo->Redo());
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(sheet).data).rows.back(), newRow);
    doc.ResizeRows(sheet, 1);
    EXPECT_EQ(Values(y).size(), 3);
    ASSERT_TRUE(undo->Undo());
    EXPECT_DOUBLE_EQ(Values(y)[1], 9);
}
TEST_F(PlotLabApp, ExplorerAndConfirmedDependentsUndoAtomically)
{
    auto folder = doc.Create(doc.project->root(), 0, "Folder");
    doc.Rename(folder, "Renamed");
    EXPECT_EQ(doc.project->object(folder).name, "Renamed");
    ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(doc.project->object(folder).name, "Folder");
    auto z = doc.AddColumn(sheet);
    doc.Execute(doc.project->setFormula(z, "Y*2"));
    doc.project->recompute().wait();
    auto remove = doc.RemovalIds(y);
    EXPECT_NE(std::find(remove.begin(), remove.end(), z), remove.end());
    doc.Delete(y);
    EXPECT_FALSE(doc.project->objects().contains(y));
    EXPECT_FALSE(doc.project->objects().contains(z));
    ASSERT_TRUE(undo->Undo());
    doc.project->recompute().wait();
    EXPECT_EQ(Values(z), (std::vector<double>{4, 8, 12}));
    EXPECT_NO_THROW(doc.project->series(doc.project->children(doc.project->children(graph)[0])[0]));
}
TEST_F(PlotLabApp, DerivedResultsStayOutsideCoreHistoryAndSelectionBridgesToBus)
{
    std::vector<pl::Event> events;
    auto subscription = bus->Subscribe<ModelChanged>([&](const auto &e) { events.push_back(e.change); });
    doc.Execute(doc.project->setFormula(y, "X*3"));
    const auto depth = undo->GetUndoDepth();
    doc.project->recompute().wait();
    EXPECT_EQ(undo->GetUndoDepth(), depth);
    EXPECT_DOUBLE_EQ(Values(y)[2], 9);
    auto plot = doc.project->children(doc.project->children(graph)[0])[0];
    doc.SelectRow(sheet, 1, 1);
    EXPECT_EQ(doc.project->selectedPoints(plot), (std::vector<std::size_t>{1}));
    doc.SelectRow(sheet, 2, 2, false, plot);
    EXPECT_EQ(doc.project->selectedPoints(plot), (std::vector<std::size_t>{2}));
    bus->ProcessQueue();
    EXPECT_TRUE(std::any_of(events.begin(), events.end(),
                            [](const auto &e) { return e.kind == pl::EventKind::SelectionChanged; }));
    ASSERT_TRUE(undo->Undo());
    EXPECT_DOUBLE_EQ(Values(y)[2], 6);
    ASSERT_TRUE(undo->Redo());
    doc.project->recompute().wait();
    EXPECT_DOUBLE_EQ(Values(y)[2], 9);
    doc.Execute(doc.project->detach(y));
    doc.Execute(doc.project->setCells(y, 0, std::vector<double>{7}));
    EXPECT_DOUBLE_EQ(Values(y)[0], 7);
}
TEST_F(PlotLabApp, SnapshotOwnsExactVisibleDataRangesTicksAndStyles)
{
    plot::PlotViewState view;
    view.viewport = plot::Bounds{1.25, 2.75, -10, 100};
    view.xTicks = {{1.5, "one", true}, {2, "two", true}};
    const auto series = doc.project->children(doc.project->children(graph)[0])[0];
    auto snapshot = pl::snapshotGraph(*doc.project, graph, view);
    ASSERT_EQ(snapshot->series.size(), 1);
    EXPECT_EQ(snapshot->series[0].plot, series);
    EXPECT_EQ(snapshot->series[0].y, (std::vector<double>{2, 4, 6}));
    EXPECT_DOUBLE_EQ(snapshot->ranges.minX, 1.25);
    EXPECT_DOUBLE_EQ(snapshot->ranges.maxY, 100);
    EXPECT_EQ(snapshot->xTicks[0].label, "one");
    auto json = nlohmann::json::parse(SnapshotJson(*snapshot));
    EXPECT_EQ(json["ranges"][1], 2.75);
    EXPECT_EQ(json["series"][0]["kind"], 2);
    doc.Execute(doc.project->setCells(y, 0, std::vector<double>{77}));
    EXPECT_DOUBLE_EQ(snapshot->series[0].y[0], 2);
    view.visibility[std::to_string(series)] = false;
    EXPECT_TRUE(pl::snapshotGraph(*doc.project, graph, view)->series.empty());
    EXPECT_THROW(pl::snapshotGraph(*doc.project, graph, {}), std::invalid_argument);
    doc.project->setAutoRecompute(false);
    doc.Execute(doc.project->setFormula(y, "X*2"));
    view.visibility.clear();
    EXPECT_THROW(pl::snapshotGraph(*doc.project, graph, view), std::runtime_error);
}
TEST_F(PlotLabApp, FileRoundtripAndFailedOpenPreserveDocument)
{
    auto file = Temp(".plotlab");
    doc.Rename(y, "Signal");
    doc.Execute(doc.project->setFormula(y, "X*5"));
    doc.project->recompute().wait();
    doc.SelectRow(sheet, 1, 1);
    auto projectId = doc.project->id();
    auto rows = std::get<pl::Worksheet>(doc.project->object(sheet).data).rows;
    doc.Save(file);
    EXPECT_FALSE(doc.Dirty());
    doc.New();
    doc.Open(file);
    EXPECT_EQ(doc.project->id(), projectId);
    EXPECT_EQ(doc.project->object(y).name, "Signal");
    EXPECT_DOUBLE_EQ(Values(y)[2], 15);
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(sheet).data).rows, rows);
    EXPECT_FALSE(undo->CanUndo());
    auto broken = Temp("-bad.plotlab");
    {
        std::ofstream output(broken);
        output << "broken";
    }
    EXPECT_ANY_THROW(doc.Open(broken));
    EXPECT_EQ(doc.project->id(), projectId);
    EXPECT_EQ(doc.path, file);
    std::filesystem::remove(file);
    std::filesystem::remove(broken);
}
TEST_F(PlotLabApp, EmbeddedExporterUsesCppSnapshotForAllFormats)
{
    PythonInterpreter interpreter;
    ASSERT_TRUE(interpreter.Start());
    plot::PlotViewState view;
    view.viewport = plot::Bounds{0, 4, 0, 8};
    view.xTicks = {{0, "0", true}, {2, "2", true}, {4, "4", true}};
    view.yTicks = {{0, "0", true}, {4, "4", true}, {8, "8", true}};
    const auto snapshot = pl::snapshotGraph(*doc.project, graph, view);
    for (const auto *format : {"svg", "pdf", "png"})
    {
        auto target = Temp((std::string(".") + format).c_str());
        ASSERT_NO_THROW(ExportGraph(*snapshot, target, format, 300, SST_PLOTLAB_PYTHON_ROOT));
        EXPECT_GT(std::filesystem::file_size(target), 1000);
        std::filesystem::remove(target);
    }
}

TEST(PlotLabCells, NumericEditingRejectsPartialInput)
{
    EXPECT_DOUBLE_EQ(ParseNumericCell("1.25e-3"), .00125);
    EXPECT_TRUE(std::isnan(ParseNumericCell("")));
    EXPECT_TRUE(std::isnan(ParseNumericCell("nan")));
    EXPECT_THROW(ParseNumericCell("12abc"), std::invalid_argument);
    EXPECT_THROW(ParseNumericCell("1,5"), std::invalid_argument);
}
