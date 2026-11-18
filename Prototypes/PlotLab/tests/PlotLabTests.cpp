#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "PlotLabDocument.hpp"
#include "PlotLabExport.hpp"
#include "Core/Platform/PlatformFontDiscovery.hpp"
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
    const auto directory = Temp("-font-portability");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{directory};
    PythonInterpreter interpreter;
    ASSERT_TRUE(interpreter.Start());
    for (const auto *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"}) {
        const auto font = ResolveFont(family, "", SST_PLOTLAB_PYTHON_ROOT);
        EXPECT_EQ(font.second, family);
        EXPECT_TRUE(std::filesystem::is_regular_file(font.first));
    }
    EXPECT_THROW(ResolveFont("DejaVu Sans", "missing-custom.otf", SST_PLOTLAB_PYTHON_ROOT), std::runtime_error);
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
    std::filesystem::create_directories(directory / "before");
    std::filesystem::create_directories(directory / "after");
    const auto original = directory / "custom.ttf";
    std::filesystem::copy_file(ResolveFont("DejaVu Serif", "", SST_PLOTLAB_PYTHON_ROOT).first, original);
    auto object = doc.project->object(graph);
    auto &font = std::get<pl::Graph>(object.data);
    font.fontFile = original.generic_string(); font.fontFamily = "DejaVu Serif";
    doc.Execute(doc.project->change("Custom font", {object}));
    const auto file = directory / "before/project.plotlab";
    doc.Save(file);
    nlohmann::json saved;
    { std::ifstream in(file); in >> saved; }
    EXPECT_EQ(saved["version"], 4);
    std::filesystem::copy(file, directory / "after/project.plotlab");
    std::filesystem::copy(directory / "before/project.plotlab.fonts", directory / "after/project.plotlab.fonts", std::filesystem::copy_options::recursive);
    auto fallback = pl::Project::load(directory / "after/project.plotlab");
    const auto embedded = std::filesystem::u8path(std::get<pl::Graph>(fallback.object(graph).data).fontFile);
    EXPECT_NE(embedded, original);
    std::filesystem::remove(embedded);
    auto fallbackOnly = pl::Project::load(directory / "after/project.plotlab");
    EXPECT_EQ(std::get<pl::Graph>(fallbackOnly.object(graph).data).fontFile, font.fontFile);
    std::filesystem::copy_file(original, embedded);
    std::filesystem::remove(original);
    doc.Open(directory / "after/project.plotlab");
    EXPECT_EQ(std::filesystem::u8path(std::get<pl::Graph>(doc.project->object(graph).data).fontFile), embedded);
    { std::ofstream corrupt(embedded, std::ios::binary | std::ios::app); corrupt << "changed"; }
    EXPECT_THROW(doc.Open(directory / "after/project.plotlab"), std::runtime_error);
    std::filesystem::remove(embedded);
    EXPECT_THROW(doc.Open(directory / "after/project.plotlab"), std::runtime_error);
    std::filesystem::copy_file(directory / "before/project.plotlab.fonts" / embedded.filename(), embedded);
    doc.Save(directory / "resaved.plotlab");
    EXPECT_NO_THROW(pl::Project::load(directory / "resaved.plotlab"));
    plot::PlotViewState customView; customView.viewport = plot::Bounds{1, 3, 2, 6};
    const auto customSnapshot = pl::snapshotGraph(*doc.project, graph, customView);
    for (const auto *format : {"svg", "pdf"}) {
        const auto output = directory / (std::string("export.") + format);
        ExportGraph(*customSnapshot, output, format, 300, SST_PLOTLAB_PYTHON_ROOT);
        std::ifstream in(output, std::ios::binary);
        const std::string bytes(std::istreambuf_iterator<char>(in), {});
        EXPECT_NE(bytes.find("DejaVuSerif"), std::string::npos);
    }
    for (auto &entry : saved["objects"]) if (entry.contains("embeddedFont")) entry["embeddedFont"] = "../outside.ttf";
    { std::ofstream out(directory / "after/project.plotlab"); out << saved; }
    EXPECT_THROW(doc.Open(directory / "after/project.plotlab"), std::runtime_error);
}

TEST(PlotLabCells, NumericEditingRejectsPartialInput)
{
    EXPECT_DOUBLE_EQ(ParseNumericCell("1.25e-3"), .00125);
    EXPECT_TRUE(std::isnan(ParseNumericCell("")));
    EXPECT_TRUE(std::isnan(ParseNumericCell("nan")));
    EXPECT_THROW(ParseNumericCell("12abc"), std::invalid_argument);
    EXPECT_THROW(ParseNumericCell("1,5"), std::invalid_argument);
}

TEST_F(PlotLabApp, GraphFontRoundTripSnapshotUndoAndLegacy)
{
    auto o = doc.project->object(graph);
    auto &g = std::get<pl::Graph>(o.data);
    g.fontFamily = "Custom Family"; g.fontFile = "C:/fonts/custom.otf"; g.fontSizePt = 11.5;
    doc.Execute(doc.project->change("Graph font", {o}));
    const auto text = doc.project->toJson();
    auto loaded = pl::Project::fromJson(text);
    const auto &restored = std::get<pl::Graph>(loaded.object(graph).data);
    EXPECT_EQ(restored.fontFamily, g.fontFamily); EXPECT_EQ(restored.fontFile, g.fontFile);
    EXPECT_DOUBLE_EQ(restored.fontSizePt, 11.5);
    plot::PlotViewState view; view.viewport = plot::Bounds{1, 3, 2, 6};
    auto snapshot = pl::snapshotGraph(loaded, graph, view);
    auto json = nlohmann::json::parse(SnapshotJson(*snapshot));
    EXPECT_EQ(json["font_family"], g.fontFamily); EXPECT_EQ(json["font_file"], g.fontFile);
    EXPECT_EQ(json["font_pt"], 11.5);
    ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(std::get<pl::Graph>(doc.project->object(graph).data).fontFamily, "DejaVu Sans");
    ASSERT_TRUE(undo->Redo());
    auto legacyV3 = nlohmann::json::parse(text); legacyV3["version"] = 3;
    auto v3 = pl::Project::fromJson(legacyV3.dump());
    EXPECT_EQ(std::get<pl::Graph>(v3.object(graph).data).fontFile, g.fontFile);
    for (int version : {1, 2}) {
        auto legacy = nlohmann::json::parse(text); legacy["version"] = version;
        for (auto &entry : legacy["objects"]) if (entry["type"] == 4) {
            entry.erase("fontFamily"); entry.erase("fontFile"); entry.erase("fontSizePt");
        }
        auto old = pl::Project::fromJson(legacy.dump());
        const auto &font = std::get<pl::Graph>(old.object(graph).data);
        EXPECT_EQ(font.fontFamily, "DejaVu Sans"); EXPECT_TRUE(font.fontFile.empty());
        EXPECT_DOUBLE_EQ(font.fontSizePt, 8);
    }
    std::get<pl::Graph>(o.data).fontSizePt = 0;
    EXPECT_THROW(doc.Execute(doc.project->change("Invalid font", {o})), std::runtime_error);
}

TEST(PlotLabFonts, CoreDefaultUsesSystemSegoe)
{
    const auto directory = Path::FromResolved(std::filesystem::path(SST_PLOTLAB_PYTHON_ROOT).parent_path() / "install/app/assets/fonts");
    const auto font = Platform::DefaultFontOption(directory);
    EXPECT_TRUE(std::filesystem::is_regular_file(std::filesystem::u8path(font.path)));
#ifdef DS_PLATFORM_WINDOWS
    EXPECT_EQ(std::filesystem::u8path(font.path).filename(), "segoeui.ttf");
    EXPECT_EQ(font.source, "System");
#endif
}

