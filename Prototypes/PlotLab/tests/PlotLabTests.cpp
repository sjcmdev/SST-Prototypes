#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "PlotLabDocument.hpp"
#include "WorksheetPython.hpp"
#include <sst/plotlab/edges/CommandJson.hpp>
#include "Core/JobSystem/JobContext.hpp"
#include "PlotLabExport.hpp"
#include "Core/Platform/PlatformFontDiscovery.hpp"
#include <cmath>
#include <chrono>
#include <thread>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace SST;
using namespace SST::Prototypes::PlotLab;
static PythonInterpreter& TestPython() { static PythonInterpreter interpreter; return interpreter; }
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
    auto& interpreter=TestPython();
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
    EXPECT_EQ(saved["version"], pl::Project::formatVersion);
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



#include "Core/IO/TextFileIO.hpp"
TEST_F(PlotLabApp, UnicodePathCsvAndProjectRoundtrip)
{
    const auto name = std::filesystem::u8path(u8"Za\u017c\u00f3\u0142\u0107_\u65e5\u672c_\U0001f600");
    const auto directory = Temp("-unicode") / name;
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{directory};
    const auto csv = directory / (name.native() + std::filesystem::path(".csv").native());
    const auto file = directory / (name.native() + std::filesystem::path(".plotlab").native());
    std::string text, error;
    ASSERT_TRUE(TextFileIO::Save(Path::FromResolved(csv), "X,Y\n1,2\n2,4\n", error)) << error;
    ASSERT_TRUE(TextFileIO::Load(Path::FromResolved(csv), text, error)) << error;
    const auto filename = Path::FromResolved(csv).Utf8();
    pl::CsvImporter importer;
    ASSERT_TRUE(importer.recognizes(filename, text));
    pl::ImportOptions options; options.extra["path"] = filename;
    auto table = importer.read(text, options, {});
    table.source["path"] = filename;
    doc.Execute(pl::importTable(*doc.project, doc.project->root(), Path::FromResolved(csv.stem()).Utf8(), table));
    const auto importedBook = doc.project->children(doc.project->root()).back();
    const auto importedSheet = doc.project->children(importedBook).front();
    const auto projectId = doc.project->id();
    doc.Save(file); doc.New(); doc.Open(file);
    EXPECT_EQ(doc.project->id(), projectId);
    EXPECT_EQ(doc.project->object(importedBook).name, Path::FromResolved(name).Utf8());
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(importedSheet).data).source.at("path"), filename);
}

TEST(PlotLabOpenJob, ImportsDemoCsvAndRejectsUnsupportedFiles)
{
    JobContext context([](float,float){}, [](const std::string &){}, [](const std::string &){},
        [](JobLogLevel,const std::string &){}, [] { return false; });
    OpenJob csv(Path("Prototypes/PlotLab/examples/demo.csv"));
    csv.Execute(context);
    ASSERT_TRUE(csv.table);
    ASSERT_EQ(csv.table->columns.size(), 4);
    EXPECT_EQ(csv.table->names.front(), "X");
    auto p = CreateRef<pl::Project>("Imported");
    pl::importTable(*p, p->root(), "demo", *csv.table).apply(*p);
    const auto path = std::filesystem::temp_directory_path() / "plotlab-open-ui1.plotlab";
    p->save(path);
    OpenJob project{Path(path)};
    project.Execute(context);
    ASSERT_TRUE(project.project);
    EXPECT_EQ(project.project->id(), p->id());
    std::filesystem::remove(path);
    OpenJob unsupported(Path("invalid.bin"));
    EXPECT_THROW(unsupported.Execute(context), std::invalid_argument);
}


TEST_F(PlotLabApp, WorksheetDesignationsAndNearestXFollowColumnOrder)
{
    for(auto role:{pl::Role::None,pl::Role::X,pl::Role::Y,pl::Role::Z,pl::Role::YError,pl::Role::Label,pl::Role::XError,pl::Role::Disregard}) {
        auto o=doc.project->object(y); std::get<pl::Column>(o.data).role=role;
        doc.Execute(doc.project->change("Role",{o}));
        EXPECT_EQ(std::get<pl::Column>(doc.project->object(y).data).role,role);
        ASSERT_TRUE(undo->Undo());
    }
    EXPECT_EQ(doc.project->pairedX(y),x);
    auto x2=doc.AddColumn(sheet), y2=doc.AddColumn(sheet), y3=doc.AddColumn(sheet);
    for(auto [id,role]:{std::pair{x2,"X"},std::pair{y2,"Y"},std::pair{y3,"Y"}})
        doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","column_settings"},{"column_id",id},{"role",role}})));
    EXPECT_EQ(doc.project->pairedX(y2),x2); EXPECT_EQ(doc.project->pairedX(y3),x2);
    auto reordered=doc.project->object(x2); reordered.order=0;
    doc.Execute(doc.project->change("Reorder",{reordered}));
    EXPECT_EQ(doc.project->pairedX(y),x2);
    EXPECT_EQ(doc.project->pairedX(y2),x2);
    ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(doc.project->pairedX(y),x);
    auto first=doc.project->object(x); first.order=100;
    doc.Execute(doc.project->change("Reorder",{first}));
    EXPECT_EQ(doc.project->pairedX(y),0);
    EXPECT_EQ(doc.project->pairedX(y3),x2);
    auto invalid=doc.project->object(y2); std::get<pl::Column>(invalid.data).role=static_cast<pl::Role>(99);
    EXPECT_THROW(doc.Execute(doc.project->change("Invalid",{invalid})),std::runtime_error);
}
TEST(PlotLabWorksheet, DisplayFormattingNeverChangesStorage)
{
    using M=pl::DisplayMode;
    EXPECT_EQ(pl::formatValue(1.23456,{M::Decimal,2}),"1.23");
    EXPECT_EQ(pl::formatValue(1.23456,{M::Significant,3}),"1.23");
    EXPECT_EQ(pl::formatValue(1,{M::Significant,3}),"1.00");
    EXPECT_EQ(pl::formatValue(1234,{M::Scientific,2}),"1.23e+03");
    EXPECT_EQ(pl::formatValue(1234,{M::Engineering,2}),"1.23 k");
    EXPECT_EQ(pl::formatValue(0.00000123,{M::Engineering,2}),"1.23 u");
    EXPECT_EQ(pl::formatValue(999999,{M::Engineering,2}),"1.00 M");
    EXPECT_EQ(pl::formatValue(0,{M::Engineering,2}),"0.00");
    EXPECT_EQ(pl::formatValue(0,{M::DateTime,2}),"1970-01-01 00:00:00 UTC");
    EXPECT_EQ(pl::formatValue(-1,{M::DateTime,2}),"1969-12-31 23:59:59 UTC");
    EXPECT_EQ(pl::formatValue(1.25,{M::Text,2}),"1.25");
    EXPECT_EQ(pl::formatValue(3,{M::Categorical,2}),"3");
    for(auto mode:{M::General,M::Decimal,M::Significant,M::Scientific,M::Engineering,M::Text,M::DateTime,M::Categorical}) {
        EXPECT_EQ(pl::formatValue(std::numeric_limits<double>::quiet_NaN(),{mode,3}),"nan");
        EXPECT_EQ(pl::formatValue(std::numeric_limits<double>::infinity(),{mode,3}),"inf");
        EXPECT_EQ(pl::formatValue(-std::numeric_limits<double>::infinity(),{mode,3}),"-inf");
        EXPECT_FALSE(pl::formatValue(1e-300,{mode,3}).empty());
    }
    EXPECT_EQ(pl::formatValue(1e-300,{M::Engineering,3}),"1.000e-300");
    EXPECT_FALSE(pl::formatValue(std::numeric_limits<double>::denorm_min(),{M::Engineering,3}).empty());
    EXPECT_EQ(ParseNumericCell(pl::fullPrecision(1.2345678901234567)),1.2345678901234567);
    EXPECT_THROW(pl::formatValue(1,{M::Significant,0}),std::invalid_argument);
}
TEST_F(PlotLabApp, WorksheetParametersFormatsRoundtripUndoAndMigration)
{
    auto edit=[&](nlohmann::json request){doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(request)));};
    edit({{"action","parameter_add"},{"worksheet_id",sheet},{"name","T"}});
    const auto parameter=std::get<pl::Worksheet>(doc.project->object(sheet).data).parameters.front().id;
    edit({{"action","parameter_value"},{"column_id",y},{"parameter_id",parameter},{"value","300 K"}});
    edit({{"action","parameter_visibility"},{"worksheet_id",sheet},{"parameter_id",parameter},{"visible",false}});
    edit({{"action","header_visibility"},{"worksheet_id",sheet},{"header","Comments"},{"visible",false}});
    edit({{"action","column_settings"},{"column_id",y},{"role","Y"},{"long_name","Signal"},{"comments","Measured"},{"display",{{"mode","engineering"},{"digits",3}}}});
    doc.Execute(doc.project->setCells(y,0,std::vector<double>{1.2345678901234567}));
    auto copy=nlohmann::json::parse(PythonBinding::WorksheetCommandJson(doc,nlohmann::json{{"action","copy_cell"},{"column_id",y},{"row_index",0}}.dump()));
    EXPECT_EQ(copy["result"]["text"],"1.2345678901234567");
    auto render=nlohmann::json::parse(PythonBinding::WorksheetCommandJson(doc,nlohmann::json{{"action","render_cell"},{"column_id",y},{"row_index",0}}.dump()));
    EXPECT_EQ(render["result"]["text"],"1.235");
    auto file=Temp("-worksheet.plotlab"); doc.Save(file);
    auto loaded=pl::Project::load(file); std::filesystem::remove(file);
    const auto& c=std::get<pl::Column>(loaded.object(y).data);
    EXPECT_EQ(c.parameters.at(parameter),"300 K"); EXPECT_EQ(c.comments,"Measured"); EXPECT_EQ(c.display.mode,pl::DisplayMode::Engineering);
    EXPECT_FALSE(std::get<pl::Worksheet>(loaded.object(sheet).data).parameters.front().visible);
    edit({{"action","parameter_remove"},{"worksheet_id",sheet},{"parameter_id",parameter}});
    EXPECT_TRUE(std::get<pl::Column>(doc.project->object(y).data).parameters.empty());
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(std::get<pl::Column>(doc.project->object(y).data).parameters.at(parameter),"300 K");
    ASSERT_TRUE(undo->Redo()); EXPECT_TRUE(std::get<pl::Worksheet>(doc.project->object(sheet).data).parameters.empty());
    for(int version=1;version<=4;++version) {
        auto old=nlohmann::json::parse(loaded.toJson()); old["version"]=version;
        for(auto& o:old["objects"]) { o.erase("display"); o.erase("parameters"); o.erase("hiddenHeaders"); }
        auto migrated=pl::Project::fromJson(old.dump());
        EXPECT_EQ(std::get<pl::Column>(migrated.object(y).data).display.mode,pl::DisplayMode::General);
        EXPECT_TRUE(std::get<pl::Worksheet>(migrated.object(sheet).data).parameters.empty());
        EXPECT_EQ(std::get<pl::Column>(migrated.object(y).data).role,pl::Role::Y);
    }
    edit({{"action","parameter_add"},{"worksheet_id",sheet},{"name","bias"}});
    EXPECT_THROW(edit({{"action","parameter_add"},{"worksheet_id",sheet},{"name","bias"}}),std::invalid_argument);
    EXPECT_THROW(edit({{"action","parameter_value"},{"column_id",y},{"parameter_id",999999},{"value","bad"}}),std::runtime_error);
    EXPECT_THROW(edit({{"action","column_settings"},{"column_id",y},{"display",{{"mode","decimal"},{"digits",-1}}}}),std::invalid_argument);
}
TEST_F(PlotLabApp, EmbeddedPythonWorksheetCommands)
{
#if DS_PYTHON_CAPI_AVAILABLE
    auto& interpreter=TestPython(); ASSERT_TRUE(interpreter.Start());
    InstallWorksheetPython();
    const auto fixture=doc.project->toJson();
    const auto gil=PythonInterpreter::AcquireRuntimeGil();
    const auto code=std::string("import json\nfixture = json.loads(")+nlohmann::json(fixture).dump()+")\nexec(open("+nlohmann::json(std::string(SST_PLOTLAB_PYTHON_ROOT)+"/tests/worksheet_headless.py").dump()+", encoding='utf-8').read())\n";
    const int result=PyRun_SimpleString(code.c_str());
    PythonInterpreter::ReleaseRuntimeGil(gil);
    EXPECT_EQ(result,0);
#else
    FAIL()<<"Embedded Python required";
#endif
}

TEST_F(PlotLabApp, ColumnValuesFunctionLibraryAndRanges)
{
    const std::vector<std::pair<std::string,std::vector<double>>> cases={
        {"exp(0)",{1,1,1}}, {"ln(e)",{1,1,1}}, {"log10(100)",{2,2,2}},
        {"sin(pi/2)",{1,1,1}}, {"cos(pi)",{-1,-1,-1}}, {"tan(pi/4)",{1,1,1}},
        {"atan2(1,0)",{1.5707963267948966,1.5707963267948966,1.5707963267948966}},
        {"min(col(A))",{1,1,1}}, {"max(col(A))",{3,3,3}}, {"mean(col(A))",{2,2,2}},
        {"sum(col(A))",{6,6,6}}, {"std(col(A))",{1,1,1}},
        {"wcol(1)",{1,2,3}}, {"i",{1,2,3}}, {"nrows",{3,3,3}},
        {"abs(-col(A))",{1,2,3}}, {"sqrt(col(A)^2)",{1,2,3}},
        {"exp(0)+ln(e)+log10(100)",{4,4,4}}, {"pow(col(A),2)",{1,4,9}},
        {"sin(pi/2)+cos(0)+tan(0)+atan2(0,1)",{2,2,2}},
        {"min(col(A))+max(col(A))+mean(col(A))+sum(col(A))+std(col(A))",{13,13,13}},
        {"diff(col(A),0)",{1,1,1}}, {"diff(col(A),1)",{1,1,1}},
        {"integ(col(A))",{0,1.5,4}}, {"interp(i,col(A),col(A))",{1,2,3}},
        {"smooth(col(A))",{1.5,2,2.5}}, {"smooth(col(A),1)",{1,2,3}},
        {"ifelse(i>1+1,col(A),0)",{0,0,3}}, {"wcol(1)+nrows",{4,5,6}},
        {"ifelse(col(A)>2,sqrt(col(A)-2),0)",{0,0,1}}
    };
    for(const auto& [expression,expected]:cases) {
        SCOPED_TRACE(expression);
        doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression",expression}})));
        ASSERT_NO_THROW(doc.project->recompute().wait());
        for(std::size_t row=0;row<expected.size();++row) EXPECT_NEAR(Values(y)[row],expected[row],1e-12);
    }
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","10*i"},{"row_from",2},{"row_to",2}})));
    doc.project->recompute().wait();
    EXPECT_EQ(Values(y),(std::vector<double>{0,20,1}));
}

TEST_F(PlotLabApp, ColumnValuesStableReferencesParametersAndPersistence)
{
    auto object=doc.project->object(x); std::get<pl::Column>(object.data).longName="Long Time";
    doc.Execute(doc.project->change("Long name",{object}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","parameter_add"},{"worksheet_id",sheet},{"name","T"}})));
    auto parameter=std::get<pl::Worksheet>(doc.project->object(sheet).data).parameters.front().id;
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","parameter_value"},{"column_id",x},{"parameter_id",parameter},{"value","10"}})));
    auto expression="col(\"Long Time\") + [Book]"+doc.project->object(sheet).name+"!col(A) + col(A)[T]";
    auto request=nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression",expression}};
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(request))); doc.project->recompute().wait();
    EXPECT_EQ(Values(y),(std::vector<double>{12,14,16}));
    doc.Rename(x,"Renamed"); doc.project->recompute().wait();
    EXPECT_EQ(Values(y)[2],16);
    auto formula=*std::get<pl::Column>(doc.project->object(y).data).formula;
    EXPECT_EQ(formula.evaluatorVersion,2u); EXPECT_EQ(formula.parameterBindings.at("col(A)[T]"),parameter);
    auto roundtrip=pl::Project::fromJson(doc.project->toJson());
    EXPECT_EQ(*std::get<pl::Column>(roundtrip.object(y).data).formula,formula);
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","parameter_value"},{"column_id",x},{"parameter_id",parameter},{"value","20"}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y)[2],26);
    ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y)[2],16);
    ASSERT_TRUE(undo->Redo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y)[2],26);
}

TEST_F(PlotLabApp, ColumnValuesDomainFailureBlocksDependentsAndPreservesCache)
{
    auto z=doc.AddColumn(sheet);
    auto set=[&](pl::Id id,std::string expression) {doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",id},{"expression",expression}})));};
    set(y,"col(A)*2"); set(z,"col(B)*2"); doc.project->recompute().wait();
    auto previous=Values(y), dependent=Values(z);
    set(y,"sqrt(col(A)-2)");
    EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    EXPECT_EQ(doc.project->recomputeInfo(y).state,pl::RecomputeState::Failed);
    EXPECT_NE(doc.project->recomputeInfo(y).diagnostic.find("row 1"),std::string::npos);
    EXPECT_EQ(doc.project->recomputeInfo(z).state,pl::RecomputeState::Blocked);
    EXPECT_EQ(Values(y),previous); EXPECT_EQ(Values(z),dependent);
    ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y),previous);
    ASSERT_TRUE(undo->Redo()); EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    set(y,"col(Missing)"); EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    EXPECT_EQ(doc.project->recomputeInfo(y).state,pl::RecomputeState::Blocked); EXPECT_EQ(Values(y),previous);
    ASSERT_TRUE(undo->Undo()); ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait();
    set(x,"col(C)"); EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    EXPECT_EQ(doc.project->recomputeInfo(x).state,pl::RecomputeState::Blocked);
    EXPECT_NE(doc.project->recomputeInfo(x).diagnostic.find("cycle"),std::string::npos);
    auto blocked=pl::Project::fromJson(doc.project->toJson());
    EXPECT_EQ(blocked.recomputeInfo(x).state,pl::RecomputeState::Blocked);
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(blocked.object(y).data).values),previous);
    EXPECT_EQ(Values(y),previous);
    ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait();
}

TEST_F(PlotLabApp, ColumnValuesNanAwareAndLegacyMigration)
{
    doc.Execute(doc.project->setCells(x,1,std::vector<double>{std::nan("")}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","mean(col(A))+sum(col(A))+std(col(A))"}})));
    doc.project->recompute().wait(); EXPECT_NEAR(Values(y)[0],6+std::sqrt(2.),1e-12);
    doc.Execute(doc.project->setFormula(y,"sqrt(X-2)")); doc.project->recompute().wait();
    auto old=pl::Project::fromJson(doc.project->toJson());
    EXPECT_EQ(std::get<pl::Column>(old.object(y).data).formula->evaluatorVersion,1u);
    EXPECT_TRUE(std::isnan(std::get<std::vector<double>>(std::get<pl::Column>(old.object(y).data).values)[0]));
}

TEST_F(PlotLabApp, EmbeddedPythonColumnValuesHeadless)
{
#if DS_PYTHON_CAPI_AVAILABLE
    ASSERT_TRUE(TestPython().Start()); InstallWorksheetPython();
    auto gil=PythonInterpreter::AcquireRuntimeGil();
    auto code="import json\nfixture=json.loads("+nlohmann::json(doc.project->toJson()).dump()+")\nexec(open("+nlohmann::json(std::string(SST_PLOTLAB_PYTHON_ROOT)+"/tests/formula_headless.py").dump()+",encoding='utf-8').read())\n";
    int result=PyRun_SimpleString(code.c_str()); PythonInterpreter::ReleaseRuntimeGil(gil);
    EXPECT_EQ(result,0);
#endif
}

TEST(PlotLabColumnValues, MillionRowsWorkerBenchmark)
{
    pl::Project p; p.setAutoRecompute(false);
    const auto owner=std::this_thread::get_id(); bool offThread=false;
    p.setBeforeFormulaRunner([&](const std::string&,const pl::Project::FormulaInputs&) {
        offThread=std::this_thread::get_id()!=owner; return std::map<std::string,double>{};
    });
    auto book=p.reserveId(), sheet=p.reserveId(), x=p.reserveId(), y=p.reserveId();
    pl::Worksheet worksheet; worksheet.rows.reserve(1000000);
    for(int row=0;row<1000000;++row) worksheet.rows.push_back(p.reserveId());
    pl::Column input, output; input.values=std::vector<double>(1000000,2); output.values=std::vector<double>(1000000,0);
    p.change("Million rows",{{book,p.root(),"Book",0,pl::Workbook{}},{sheet,book,"Sheet",0,std::move(worksheet)},{x,sheet,"A",0,std::move(input)},{y,sheet,"B",0,std::move(output)}}).apply(p);
    p.worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","sqrt(col(A))*2+i"},{"before_script","# worker-thread probe"}})).apply(p);
    auto begin=std::chrono::steady_clock::now(); p.recompute().wait();
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"P6b 1e6 rows worker recompute (including publication): "<<ms<<" ms\n";
    EXPECT_TRUE(offThread); EXPECT_LT(ms,1000); EXPECT_NEAR(std::get<std::vector<double>>(std::get<pl::Column>(p.object(y).data).values).back(),1000000+2*std::sqrt(2.),1e-8);
}

TEST_F(PlotLabApp, ColumnValuesVectorAlgorithms)
{
    doc.ResizeRows(sheet); doc.ResizeRows(sheet);
    doc.Execute(doc.project->setCells(x,0,std::vector<double>{1,4,9,16,25}));
    auto check=[&](std::string expression,std::vector<double> expected) {
        SCOPED_TRACE(expression);
        doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression",expression}})));
        doc.project->recompute().wait();
        for(std::size_t row=0;row<expected.size();++row) EXPECT_NEAR(Values(y)[row],expected[row],1e-12);
    };
    check("diff(col(A),0)",{3,5,7,9,9});
    check("diff(col(A),1)",{3,4,6,8,9});
    check("integ(col(A))",{0,2.5,9,21.5,42});
    check("smooth(col(A),1)",{1,4,9,16,25});
    check("smooth(col(A),0)",{2.5,14./3,29./3,50./3,20.5});
    check("interp(i/2+1,col(A),2*col(A))",{3,4,5,6,7});
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","100+i"},{"row_from",2},{"row_to",3}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{3,102,103,6,7}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","200+i"},{"row_from",4},{"row_to",5}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{3,102,103,204,205}));
    ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{3,102,103,6,7}));
    ASSERT_TRUE(undo->Redo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{3,102,103,204,205}));
}

TEST_F(PlotLabApp, ColumnValuesModesAndScriptDependencyDeletion)
{
    ASSERT_TRUE(TestPython().Start()); InstallWorksheetPython();
    auto command=[&](nlohmann::json j) {return nlohmann::json::parse(PythonBinding::WorksheetCommandJson(doc,j.dump()));};
    ASSERT_TRUE(command({{"action","set_column_values"},{"column_id",y},{"expression","scale+i"},{"before_script","scale = mean(col("+std::to_string(x)+"))"},{"recalculate","Manual"}}).at("ok"));
    ASSERT_TRUE(command({{"action","recompute"}}).at("ok"));
    EXPECT_EQ(Values(y),(std::vector<double>{3,4,5}));
    doc.Execute(doc.project->setCells(x,0,std::vector<double>{4}));
    EXPECT_EQ(doc.project->recomputeInfo(y).state,pl::RecomputeState::Dirty);
    ASSERT_TRUE(command({{"action","recompute"}}).at("ok")); EXPECT_EQ(Values(y),(std::vector<double>{4,5,6}));
    auto remove=doc.RemovalIds(x); EXPECT_NE(std::find(remove.begin(),remove.end(),y),remove.end());
    ASSERT_TRUE(command({{"action","set_column_values"},{"column_id",y},{"expression","col(A)*2"},{"recalculate","None"}}).at("ok"));
    ASSERT_TRUE(command({{"action","recompute"}}).at("ok")); EXPECT_EQ(Values(y),(std::vector<double>{8,4,6}));
    doc.Execute(doc.project->setCells(x,0,std::vector<double>{10}));
    ASSERT_TRUE(command({{"action","recompute"}}).at("ok")); EXPECT_EQ(Values(y),(std::vector<double>{8,4,6}));
    EXPECT_EQ(doc.project->recomputeInfo(y).state,pl::RecomputeState::Clean);
    EXPECT_THROW(doc.ResizeRows(sheet),std::invalid_argument);
    auto resized=doc.project->object(sheet),input=doc.project->object(x);
    std::get<pl::Worksheet>(resized.data).rows.push_back(doc.project->reserveId());
    std::get<std::vector<double>>(std::get<pl::Column>(input.data).values).push_back(0);
    EXPECT_THROW(doc.Execute(doc.project->change("Resize frozen worksheet",{resized,input})),std::runtime_error);
    ASSERT_TRUE(command({{"action","set_column_values"},{"column_id",y},{"expression","col(A)*2"},{"recalculate","None"}}).at("ok"));
    ASSERT_TRUE(command({{"action","recompute"}}).at("ok")); EXPECT_EQ(Values(y),(std::vector<double>{20,4,6}));
    ASSERT_TRUE(undo->Undo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{8,4,6}));
    ASSERT_TRUE(undo->Redo()); doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{20,4,6}));
    ASSERT_TRUE(command({{"action","set_column_values"},{"column_id",y},{"expression","sqrt(col(A)-4)"},{"recalculate","None"}}).at("ok"));
    EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    ASSERT_TRUE(command({{"action","set_column_values"},{"column_id",y},{"expression","i"}}).at("ok"));
    doc.project->recompute().wait(); ASSERT_TRUE(undo->Undo());
    EXPECT_THROW(doc.project->recompute().wait(),std::runtime_error);
    EXPECT_EQ(doc.project->recomputeInfo(y).state,pl::RecomputeState::Failed);
    EXPECT_EQ(Values(y),(std::vector<double>{20,4,6}));
}

TEST_F(PlotLabApp, ColumnValuesCrossBookReferencesSurviveRenames)
{
    auto otherBook=doc.Create(doc.project->root(),1,"Other Book");
    auto otherSheet=doc.Create(otherBook,2,"Other Sheet");
    for(int row=0;row<3;++row) doc.ResizeRows(otherSheet);
    auto source=doc.AddColumn(otherSheet);
    doc.Execute(doc.project->setCells(source,0,std::vector<double>{10,20,30}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","[Other Book]Other Sheet!col(A)+wcol(1)"}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{11,22,33}));
    doc.Rename(otherBook,"Book renamed"); doc.Rename(otherSheet,"Sheet renamed"); doc.Rename(source,"Column renamed");
    doc.Execute(doc.project->setCells(source,0,std::vector<double>{100}));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{101,22,33}));
    const auto& bindings=std::get<pl::Column>(doc.project->object(y).data).formula->bindings;
    EXPECT_EQ(bindings.at("[Other Book]Other Sheet!col(A)"),source);
    EXPECT_EQ(bindings.at("wcol(1)"),x);
}

TEST_F(PlotLabApp, ColumnValuesMigrationKeepsLegacyBuiltinNamedColumns)
{
    doc.Rename(x,"i");
    doc.Execute(doc.project->setFormula(y,"i*2")); doc.project->recompute().wait();
    auto saved=nlohmann::json::parse(doc.project->toJson());
    for(auto& o:saved["objects"]) if(o.contains("formula")) {
        for(const char* key:{"rowFrom","rowTo","beforeScript","recalculate","parameterBindings","variables","scriptInputs","bindingDiagnostic"}) o["formula"].erase(key);
    }
    auto migrated=pl::Project::fromJson(saved.dump());
    migrated.setCells(x,0,std::vector<double>{10}).apply(migrated); migrated.recompute().wait();
    EXPECT_EQ(std::get<std::vector<double>>(std::get<pl::Column>(migrated.object(y).data).values),(std::vector<double>{20,4,6}));
}

TEST_F(PlotLabApp, ColumnValuesDomainOutsideRangeDoesNotOverwriteOtherRows)
{
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","sqrt(col(A)-2)"},{"row_from",2},{"row_to",3}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{2,0,1}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","diff(sqrt(col(A)-2),0)"},{"row_from",2},{"row_to",2}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{2,1,1}));
}

TEST_F(PlotLabApp, ColumnValuesNumericParameterOnTextColumn)
{
    auto label=doc.AddColumn(sheet,true);
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","parameter_add"},{"worksheet_id",sheet},{"name","T"}})));
    auto parameter=std::get<pl::Worksheet>(doc.project->object(sheet).data).parameters.front().id;
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","parameter_value"},{"column_id",label},{"parameter_id",parameter},{"value","300"}})));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","col(C)[T]+i"}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{301,302,303}));
}

TEST_F(PlotLabApp, ColumnValuesPersistenceRejectsTruncatedIndicesAndIds)
{
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","col(A)"}})));
    doc.project->recompute().wait();
    const auto saved=nlohmann::json::parse(doc.project->toJson());
    for(const auto& [key,value]:std::vector<std::pair<std::string,nlohmann::json>>{
        {"rowFrom",1.5},{"rowTo",-1},{"scriptInputs",nlohmann::json::array({-1})},
        {"parameterBindings",nlohmann::json{{"col(A)[T]",0}}}}) {
        auto bad=saved;
        for(auto& object:bad["objects"]) if(object["id"]==y) object["formula"][key]=value;
        EXPECT_THROW(pl::Project::fromJson(bad.dump()),std::runtime_error)<<key;
    }
}

TEST_F(PlotLabApp, ColumnValuesInterpolationAcrossDifferentSheetLengths)
{
    auto sourceSheet=doc.Create(book,2,"Source");
    for(int row=0;row<5;++row) doc.ResizeRows(sourceSheet);
    auto sourceX=doc.AddColumn(sourceSheet),sourceY=doc.AddColumn(sourceSheet);
    doc.Execute(doc.project->setCells(sourceX,0,std::vector<double>{1,2,3,4,5}));
    doc.Execute(doc.project->setCells(sourceY,0,std::vector<double>{2,4,6,8,10}));
    auto set=[&](std::string expression) {
        doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression",expression},
            {"bindings",{{"sourceX",sourceX},{"sourceY",sourceY}}}})));
        doc.project->recompute().wait();
    };
    set("interp(i+1.5,sourceX,2*sourceY)"); EXPECT_EQ(Values(y),(std::vector<double>{10,14,18}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",y},{"expression","mean(source)"},{"bindings",{{"source",sourceY}}}})));
    doc.project->recompute().wait(); EXPECT_EQ(Values(y),(std::vector<double>{6,6,6}));
    doc.Execute(doc.project->worksheetEdit(pl::edges::worksheetEdit(nlohmann::json{{"action","set_column_values"},{"column_id",sourceY},{"expression","source"},{"bindings",{{"source",x}}}})));
    doc.project->recompute().wait();
    const auto& values=Values(sourceY); ASSERT_EQ(values.size(),5u); EXPECT_EQ(values[2],3); EXPECT_TRUE(std::isnan(values[3]));
}
#include "Presentation/Panels/HostedEditorWindow.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include <imgui.h>
#include <imgui_internal.h>

TEST(WindowMigration, LegacyIdentityFlagsGeometryAndWorkspaceActions)
{
    ImGui::CreateContext();
    struct Cleanup { ~Cleanup() { ImGui::DestroyContext(); } } cleanup;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDebugHighlightIdConflicts = true;
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    PanelRegistry panels;
    auto workspace = CreateRef<WindowWorkspace>(panels);
    auto commands = CreateRef<CommandRegistry>();
    ASSERT_TRUE(workspace->Bind(commands, {}));
    auto window = CreateRef<HostedEditorWindow>("Graph: Data###Graph: 1:2");
    const auto id = panels.AddExisting(window);
    const auto identity = ImHashStr(window->ImGuiName().c_str());
    EXPECT_EQ(identity, ImHashStr("Graph: Data###Graph: 1:2"));
    int rendered = 0;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900, 300), ImGuiCond_FirstUseEver);
        window->Render([&] { ++rendered; ImGui::TextUnformatted("Graph"); }, ImGuiWindowFlags_HorizontalScrollbar);
        // The registry must not draw hosted content a second time.
        for (const auto &entry : panels.Entries()) entry.panel->Render();
        workspace->RenderOverlays();
        ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
    };
    frame(); frame();
    EXPECT_EQ(rendered, 2);
    auto *gui = ImGui::FindWindowByName(window->ImGuiName().c_str());
    ASSERT_NE(gui, nullptr);
    EXPECT_TRUE(gui->Flags & ImGuiWindowFlags_HorizontalScrollbar);
    EXPECT_FLOAT_EQ(gui->SizeFull.x, 900);
    EXPECT_FLOAT_EQ(gui->SizeFull.y, 300);
    window->SetLegacyName("Graph: Renamed###Graph: 1:2");
    EXPECT_EQ(ImHashStr(window->ImGuiName().c_str()), identity);
    EXPECT_EQ(window->ImGuiName().find(ICON_FA_WINDOW_MAXIMIZE " Graph: Renamed"), 0);
    EXPECT_TRUE(workspace->Execute({WindowAction::Minimize, id}));
    frame();
    EXPECT_EQ(rendered, 2);
    EXPECT_TRUE(workspace->Execute({WindowAction::Restore, id}));
    frame();
    EXPECT_EQ(rendered, 3);
    EXPECT_TRUE(workspace->Execute({WindowAction::Close, id}));
    frame();
    EXPECT_EQ(rendered, 3);
    EXPECT_FALSE(window->IsVisible());

    // A fresh process restores hosted windows before app coordinators attach their content.
    const auto saved = workspace->Save();
    EXPECT_TRUE(panels.Remove(id));
    EXPECT_EQ(panels.AddExisting(window, id), id);
    EXPECT_EQ(ImHashStr(window->ImGuiName().c_str()), identity);
    PanelRegistry restoredPanels;
    auto restored = CreateRef<WindowWorkspace>(restoredPanels);
    EXPECT_TRUE(restored->Load(saved));
    ASSERT_NE(restored->Find(id), nullptr);
    EXPECT_EQ(restored->Find(id)->ImGuiName(), window->ImGuiName());
    EXPECT_FALSE(restored->Find(id)->IsVisible());
}

TEST(WindowMigration, PlainTitleIniMappingAndIndependentCopies)
{
    ImGui::CreateContext();
    struct Cleanup { ~Cleanup() { ImGui::DestroyContext(); } } cleanup;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    io.ConfigDebugHighlightIdConflicts = true;
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    ImGui::LoadIniSettingsFromMemory("[Window][Import Data]\nPos=42,74\nSize=777,555\nCollapsed=0\n");
    PanelRegistry panels;
    auto original = CreateRef<HostedEditorWindow>("Import Data");
    EXPECT_EQ(ImHashStr(original->ImGuiName().c_str()), ImHashStr("Import Data"));
    auto copy = CreateRef<HostedEditorWindow>("Data###data", false);
    auto otherCopy = CreateRef<HostedEditorWindow>("Data###data", false);
    panels.AddExisting(original); panels.AddExisting(copy); panels.AddExisting(otherCopy);
    EXPECT_NE(ImHashStr(copy->ImGuiName().c_str()), ImHashStr(otherCopy->ImGuiName().c_str()));
    for (int i = 0; i < 2; ++i)
    {
        ImGui::NewFrame();
        original->Render([] { ImGui::TextUnformatted("Import"); });
        ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount, 0);
    }
    auto *gui = ImGui::FindWindowByName(original->ImGuiName().c_str());
    ASSERT_NE(gui, nullptr);
    EXPECT_FLOAT_EQ(gui->Pos.x, 42);
    EXPECT_FLOAT_EQ(gui->Pos.y, 74);
    EXPECT_FLOAT_EQ(gui->SizeFull.x, 777);
    EXPECT_FLOAT_EQ(gui->SizeFull.y, 555);
}

TEST(WindowMigration, OlderWorkspaceDoesNotOverrideMigratedPanelIni)
{
    ImGui::CreateContext();
    struct Cleanup { ~Cleanup() { ImGui::DestroyContext(); } } cleanup;
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    ImGui::LoadIniSettingsFromMemory("[Window][Project Tree]\nPos=42,74\nSize=777,555\nCollapsed=0\n");
    PanelRegistry panels;
    auto workspace = CreateRef<WindowWorkspace>(panels);
    panels.AddExisting(CreateRef<HostedEditorWindow>("Existing"));
    const auto olderWorkspace = workspace->Save();
    class MigratedPanel final : public EditorWindow
    {
    public:
        MigratedPanel() : EditorWindow("jfm.project-tree", "Project Tree") {}
        void RenderContent() override {}
    };
    auto migrated = CreateRef<MigratedPanel>();
    panels.AddExisting(migrated);
    ASSERT_TRUE(workspace->Load(olderWorkspace));
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        migrated->Render();
        ImGui::Render();
    }
    auto *gui = ImGui::FindWindowByName(migrated->ImGuiName().c_str());
    ASSERT_NE(gui, nullptr);
    EXPECT_FLOAT_EQ(gui->Pos.x, 42);
    EXPECT_FLOAT_EQ(gui->Pos.y, 74);
    EXPECT_FLOAT_EQ(gui->SizeFull.x, 777);
    EXPECT_FLOAT_EQ(gui->SizeFull.y, 555);
}

TEST_F(PlotLabApp, DataCommandsUseCoreHistoryAndPublishCreatedSheetIds) {
    using J=nlohmann::json;
    auto command=[&](J request) {auto response=J::parse(PythonBinding::WorksheetCommandJson(doc,request.dump()));EXPECT_TRUE(response.at("ok").get<bool>())<<response;return response;};
    command({{"action","sort"},{"worksheet_id",sheet},{"keys",J::array({{{"column_id",x},{"descending",true}}})}});
    EXPECT_EQ(Values(y),(std::vector<double>{6,4,2}));EXPECT_EQ(undo->GetUndoDepth(),1u);ASSERT_TRUE(undo->Undo());EXPECT_EQ(Values(y),(std::vector<double>{2,4,6}));ASSERT_TRUE(undo->Redo());
    auto result=command({{"action","reduce"},{"worksheet_id",sheet},{"every_n",2}});
    auto output=result.at("result").at("worksheet_ids").at(0).get<pl::Id>();EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(output).data).rows.size(),2u);
    EXPECT_EQ(undo->GetUndoDepth(),2u);ASSERT_TRUE(undo->Undo());EXPECT_FALSE(doc.project->objects().contains(output));ASSERT_TRUE(undo->Redo());EXPECT_TRUE(doc.project->objects().contains(output));
}
TEST_F(PlotLabApp, DataJobRejectsStaleProjectWithoutPublishing) {
    using J=nlohmann::json;
    auto result=J::parse(PythonBinding::WorksheetCommandJson(doc,J{{"action","sort"},{"worksheet_id",sheet},{"wait",false},{"keys",J::array({{{"column_id",x},{"descending",true}}})}}.dump()));ASSERT_TRUE(result.at("ok").get<bool>())<<result;
    doc.Rename(sheet,"New name");
    do {std::this_thread::sleep_for(std::chrono::milliseconds(1));result=J::parse(PythonBinding::WorksheetCommandJson(doc,"{\"action\":\"data_status\"}"));} while(result.at("ok").get<bool>() && result.at("result").at("state")=="running");
    EXPECT_FALSE(result.at("ok").get<bool>());EXPECT_EQ(Values(x),(std::vector<double>{1,2,3}));EXPECT_EQ(doc.project->object(sheet).name,"New name");
}
TEST(PlotLabDataJobs, MillionRowsSortFilterWorkerBenchmark) {
    using J=nlohmann::json;
    Document doc{CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),CreateRef<EventBus>()};
    pl::Table table;table.names={"X","Y"};table.columns.resize(2);std::vector<double> x(1000000),y(1000000);
    for(std::size_t r=0;r<x.size();++r) {x[r]=static_cast<double>(x.size()-r);y[r]=x[r]*2;}
    table.columns[0].values=std::move(x);table.columns[1].values=std::move(y);doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Benchmark",table));
    auto book=doc.project->children(doc.project->root()).front(),sheet=doc.project->children(book).front(),column=doc.project->children(sheet).front();
    for(auto request:std::vector<J>{
        {{"action","sort"},{"keys",J::array({{{"column_id",column}}})}},
        {{"action","filter"},{"filters",J::array({{{"column_id",column},{"kind","range"},{"minimum_column_units",250000},{"maximum_column_units",750000}}})}}
    }) {
        request["worksheet_id"]=sheet;request["wait"]=false;
        auto start=std::chrono::steady_clock::now();auto response=J::parse(PythonBinding::WorksheetCommandJson(doc,request.dump()));ASSERT_TRUE(response.at("ok").get<bool>())<<response;
        double submit=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        ASSERT_EQ(response.at("result").at("state"),"running");
        do {std::this_thread::sleep_for(std::chrono::milliseconds(1));response=J::parse(PythonBinding::WorksheetCommandJson(doc,"{\"action\":\"data_status\"}"));} while(response.at("ok").get<bool>() && response.at("result").at("state")=="running");
        ASSERT_TRUE(response.at("ok").get<bool>())<<response;
        std::cout<<"P6c 1e6 "<<request.at("action")<<": submit="<<submit<<" ms worker="<<response.at("result").at("elapsed_ms")<<" ms total="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
    }
    const auto& values=std::get<std::vector<double>>(std::get<pl::Column>(doc.project->object(column).data).values);EXPECT_DOUBLE_EQ(values.front(),1);EXPECT_DOUBLE_EQ(values.back(),1000000);
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(sheet).data).visibleRows.size(),500001u);
}
TEST_F(PlotLabApp, EmbeddedPythonDataOperations) {
#if DS_PYTHON_CAPI_AVAILABLE
    auto& interpreter=TestPython();ASSERT_TRUE(interpreter.Start());InstallWorksheetPython();
    const auto gil=PythonInterpreter::AcquireRuntimeGil();
    const auto code=std::string("exec(open(")+nlohmann::json(std::string(SST_PLOTLAB_PYTHON_ROOT)+"/tests/data_headless.py").dump()+", encoding='utf-8').read())\n";
    const int result=PyRun_SimpleString(code.c_str());PythonInterpreter::ReleaseRuntimeGil(gil);EXPECT_EQ(result,0);
#endif
}

TEST_F(PlotLabApp, LinkedRowStatisticsFollowInsertedRowsAndUndo) {
    using J=nlohmann::json;
    const auto response=J::parse(PythonBinding::WorksheetCommandJson(doc,J{{"action","statistics"},{"worksheet_id",sheet},{"axis","row"}}.dump()));ASSERT_TRUE(response.at("ok").get<bool>())<<response;
    const auto output=response.at("result").at("worksheet_ids").at(0).get<pl::Id>();
    const auto before=std::get<pl::Worksheet>(doc.project->object(output).data).rows;
    doc.ResizeRows(sheet);doc.project->recompute().wait();EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(output).data).rows.size(),4u);
    ASSERT_TRUE(undo->Undo());doc.project->recompute().wait();EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(output).data).rows,before);
    ASSERT_TRUE(undo->Redo());doc.project->recompute().wait();EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(output).data).rows.size(),4u);
}

TEST(PlotLabDataJobs, SharedCorePoolSurvivesDocumentAndPublishedJobPayloadIsReleased) {
    using J=nlohmann::json;auto bus=CreateRef<EventBus>();auto jobs=CreateRef<JobSystem>(bus,1);
    SST::JobId id=0;std::weak_ptr<pl::Project> project;
    {
        Document doc{CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus,jobs};project=doc.project;
        doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Shared",pl::CsvImporter{}.read("X,Y\n1,2\n2,4\n")));
        auto book=doc.project->children(doc.project->root()).front(),sheet=doc.project->children(book).front(),column=doc.project->children(sheet).front();
        auto result=J::parse(PythonBinding::WorksheetCommandJson(doc,J{{"action","sort"},{"worksheet_id",sheet},{"keys",J::array({{{"column_id",column}}})}}.dump()));ASSERT_TRUE(result.at("ok").get<bool>())<<result;
        id=result.at("result").at("job_id").get<SST::JobId>();ASSERT_TRUE(jobs->GetJob(id));
    }
    EXPECT_TRUE(project.expired());
    ASSERT_TRUE(jobs->Retry(id));
    std::optional<JobSnapshot> status;
    do {std::this_thread::sleep_for(std::chrono::milliseconds(1));status=jobs->GetJob(id);} while(status && (status->status==JobStatus::Queued || status->status==JobStatus::Running));
    ASSERT_TRUE(status);EXPECT_EQ(status->status,JobStatus::Failed);EXPECT_NE(status->errorMessage.find("fresh snapshot"),std::string::npos);
    auto open=CreateRef<OpenJob>(Path::FromResolved("Prototypes/PlotLab/examples/demo.csv"));auto next=jobs->Submit(open);ASSERT_NE(next,0u);
    do {std::this_thread::sleep_for(std::chrono::milliseconds(1));status=jobs->GetJob(next);} while(status && (status->status==JobStatus::Queued || status->status==JobStatus::Running));
    ASSERT_TRUE(status);EXPECT_EQ(status->status,JobStatus::Completed);
}

TEST(PlotLabTypedCommands, NumericListIsParsedAtInputEdge) {
    EXPECT_EQ(ParseNumericList("1, 1.5, -2e-3"),(std::vector<double>{1,1.5,-2e-3}));
    EXPECT_TRUE(ParseNumericList("").empty());
    EXPECT_THROW(ParseNumericList("1,,2"),std::invalid_argument);
    EXPECT_THROW(ParseNumericList("1,"),std::invalid_argument);
    EXPECT_THROW(ParseNumericList("1,broken"),std::invalid_argument);
}
TEST(PlotLabTypedCommands, JsonAdapterRequiresIntegralDisplayDigits) {
    auto request=nlohmann::json{{"action","column_settings"},{"column_id",1},{"display",{{"mode","general"}}}};
    EXPECT_THROW(pl::edges::worksheetEdit(request),nlohmann::json::out_of_range);
    for(auto digits:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json(18),nlohmann::json(true)}) {
        request["display"]["digits"]=digits;EXPECT_THROW(pl::edges::worksheetEdit(request),std::invalid_argument);
    }
    request["display"]["digits"]=17;EXPECT_NO_THROW(pl::edges::worksheetEdit(request));
}
TEST_F(PlotLabApp, TypedCommandsKeepStableDependenciesThroughCoreHistorySaveLoadAndApply) {
    pl::SetColumnValues formula;formula.column=y;formula.formula.expression="abs(col(A))";
    doc.WorksheetCommand(formula);doc.project->recompute().wait();
    auto inserted=doc.AddColumn(sheet);auto added=doc.project->object(inserted),source=doc.project->object(x);added.order=1;source.order=10;
    doc.Execute(doc.project->change("Insert before source",{added,source}));
    auto verify=[&](double value){doc.project->recompute().wait();EXPECT_EQ(std::get<pl::Column>(doc.project->object(y).data).formula->bindings.at("col(A)"),x);EXPECT_DOUBLE_EQ(Values(y)[0],value);auto curve=doc.project->children(doc.project->children(graph).front()).front();EXPECT_DOUBLE_EQ(doc.project->series(curve).data.y[0],value);};
    doc.WorksheetCommand(formula);verify(1);
    doc.Execute(doc.project->setCells(x,0,std::vector<double>{-7}));verify(7);ASSERT_TRUE(undo->Undo());verify(1);ASSERT_TRUE(undo->Redo());verify(7);
    doc.Rename(x,"Input renamed");doc.Rename(sheet,"Sheet renamed");verify(7);
    doc.Delete(x);EXPECT_FALSE(doc.project->objects().contains(y));ASSERT_TRUE(undo->Undo());verify(7);ASSERT_TRUE(undo->Redo());EXPECT_FALSE(doc.project->objects().contains(x));ASSERT_TRUE(undo->Undo());verify(7);
    auto file=Temp("-typed-dependencies.plotlab");doc.Save(file);doc.Open(file);doc.WorksheetCommand(formula);verify(7);ASSERT_TRUE(undo->Undo());verify(7);ASSERT_TRUE(undo->Redo());verify(7);std::filesystem::remove(file);
}
