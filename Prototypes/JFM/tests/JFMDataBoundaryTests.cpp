#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "IO/JFMProjectYamlSerializer.hpp"
#include "IO/JFMProjectBundleSerializer.hpp"
#include "IO/JFMPlotExport.hpp"
#include "Presentation/JFMPlotDocument.hpp"
#include <sst/plotlab/SnapshotJson.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Presentation/Panels/HostedEditorWindow.hpp"
#include <sst/ui/Plot/Axis.hpp>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <imgui_internal.h>

namespace SST::Prototypes::JFM {
namespace {
std::string ReadBytes(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void CheckBaseline(const std::string &name, const std::string &bytes) {
    const auto *directory = std::getenv("JFMTY1_BASELINE");
    if (!directory) return;
    auto path = std::filesystem::path(directory) / name;
    auto normalized = [&](const std::string &text) {
        if (name != "snapshot.json") return text;
        auto value = nlohmann::json::parse(text); value["project_id"] = 0; return value.dump();
    };
    if (std::getenv("JFMTY1_RECORD")) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary); out << bytes;
    } else { ASSERT_TRUE(std::filesystem::exists(path)) << path; if (normalized(ReadBytes(path)) != normalized(bytes)) { std::ofstream out(path.string() + ".actual", std::ios::binary); out << bytes; }
        EXPECT_TRUE(normalized(ReadBytes(path)) == normalized(bytes)) << name; }
}
}
TEST(JFMDataBoundaryTests, ProjectAndBundleFileBytesMatchBaseline) {
    for (const auto &entry : std::filesystem::directory_iterator("Prototypes/JFM/JFM-projects")) {
        if (!entry.is_regular_file()) continue;
        auto project = JFMProjectYamlSerializer::Deserialize(ReadBytes(entry.path()));
        ASSERT_TRUE(project) << entry.path() << project.Error().technicalDetails;
        auto encoded = JFMProjectYamlSerializer::Serialize(*project); ASSERT_TRUE(encoded);
        CheckBaseline("projects/" + entry.path().filename().string(), *encoded);
        JFMProjectBundleSnapshot bundle; bundle.project = *project;
        const auto temporary = std::filesystem::temp_directory_path();
        const auto dir = temporary / ("jfmty1-" + entry.path().filename().string());
        ASSERT_TRUE(std::filesystem::equivalent(dir.parent_path(), temporary));
        auto saved = JFMProjectBundleSerializer::Save(Path::FromResolved(dir), bundle); ASSERT_TRUE(saved);
        CheckBaseline("bundles/" + entry.path().filename().string(), ReadBytes(dir / "project.yaml"));
        auto loaded = JFMProjectBundleSerializer::Load(Path::FromResolved(dir)); ASSERT_TRUE(loaded);
        // The bundle must remain readable and retain characteristic identities.
        auto reencoded = JFMProjectYamlSerializer::Serialize(loaded->snapshot.project); ASSERT_TRUE(reencoded);
        EXPECT_EQ(loaded->snapshot.project.characteristics.size(), project->characteristics.size());
        std::filesystem::remove_all(dir);
    }
}
TEST(JFMDataBoundaryTests, LargestProjectSnapshotBenchmark) {
    const auto source = std::filesystem::path("Prototypes/JFM/JFM-projects/High-Ga.yaml.jfm.yaml");
    auto project = JFMProjectYamlSerializer::Deserialize(ReadBytes(source)); ASSERT_TRUE(project);
    JFMPlotDocument doc;
    // Stress the largest fixture's curve names and IDs with 1000 deterministic points per curve.
    doc.BeginFrame();
    std::vector<double> x(1000), y(1000);
    for (size_t i = 0; i < x.size(); ++i) { x[i] = double(i) / 999; y[i] = x[i] * x[i]; }
    for (const auto &curve : project->characteristics)
        doc.SetSeries({curve.name + "###" + std::to_string(curve.id), {x.data(),y.data(),x.size()}});
    doc.EndFrame({}); doc.view.viewport = sst::ui::plot::Bounds{0,1,0,1};
    if (const auto *directory = std::getenv("JFMTY1_BASELINE")) {
        std::ofstream out(std::filesystem::path(directory) / "plot-project.json", std::ios::binary);
        out << "{\"graph_id\":" << doc.graph << ",\"project\":" << doc.project.toJson() << "}";
    }
    constexpr int repeats = 5;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) { auto result = doc.Snapshot(); ASSERT_EQ(result->series.size(), project->characteristics.size()); }
    auto elapsed = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/repeats;
    std::cout << "JFMTY1 snapshot_ms=" << elapsed << " curves=" << project->characteristics.size() << " points_per_curve=" << x.size() << '\n';
    CheckBaseline("snapshot.json", sst::plotlab::SnapshotJson(*doc.Snapshot()));
}
}

namespace SST {
TEST(JFMDataBoundaryTests, TypedLayoutMatchesIniAndRestoresDocking) {
    std::unique_ptr<ImGuiContext, void(*)(ImGuiContext *)> context(ImGui::CreateContext(), ImGui::DestroyContext);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    {
        PanelRegistry panels;
        auto workspace = CreateRef<WindowWorkspace>(panels);
        auto window = CreateRef<HostedEditorWindow>("Data###boundary");
        const auto id = panels.AddExisting(window);
        workspace->InstallSettingsHandler();
        ImGui::DockBuilderAddNode(100);
        ImGui::DockBuilderGetNode(100)->SetLocalFlags(ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(100, {1000,700});
        ImGuiID left, right;
        ImGui::DockBuilderSplitNode(100, ImGuiDir_Left, .3f, &left, &right);
        ImGui::DockBuilderDockWindow(window->ImGuiName().c_str(), left);
        ImGui::DockBuilderFinish(100);
        const auto original = window->GetState();
        const auto typed = workspace->CaptureLayout();
        const auto bytes = workspace->Save();
        ASSERT_TRUE(workspace->Apply({WindowAction::SaveWorkspace, 0, "named"}));
        const std::string ini = ImGui::SaveIniSettingsToMemory();
        auto changedState = original; changedState.shortName = "Changed"; window->ApplyState(changedState);
        ImGui::DockBuilderRemoveNode(100);
        ASSERT_TRUE(workspace->Apply({WindowAction::LoadWorkspace, 0, "named"}));
        EXPECT_EQ(window->GetState(), original);
        ASSERT_NE(ImGui::DockBuilderGetNode(100), nullptr);
        ASSERT_NE(ImGui::DockBuilderGetNode(100)->ChildNodes[0], nullptr);
        EXPECT_EQ(ImGui::DockBuilderGetNode(100)->ChildNodes[0]->ID, left);
        EXPECT_EQ(workspace->Save(), workspace->SaveLayout(typed));
        ImGui::LoadIniSettingsFromMemory(ini.c_str());
        EXPECT_EQ(workspace->Save(), bytes);
        const std::string roundtrip = ImGui::SaveIniSettingsToMemory();
        EXPECT_EQ(ini, roundtrip);
        // Invalid typed input must leave the live layout unchanged.
        auto invalid = typed; invalid.items.front().state.width = -1;
        EXPECT_FALSE(workspace->LoadLayout(invalid));
        EXPECT_EQ(workspace->Save(), bytes);
        workspace->RemoveSettingsHandler();
    }
}
TEST(JFMDataBoundaryTests, TypedAxisMatchesJsonWithAllFields) {
    using namespace sst::ui::plot;
    AxisSpec axis; axis.label = "Temperature"; axis.unit = "K";
    axis.scale = AxisScale::OffsetReciprocal; axis.offset = 1.; axis.reverse = true;
    axis.range = {200.,400.}; axis.restoreRange = {210.,390.}; axis.rescale = RescaleMode::Restore;
    axis.ticks.mode = TickMode::Positions; axis.ticks.positions = {200,250,300}; axis.ticks.minorCount = 2;
    axis.labels.prefix = "T="; axis.labels.suffix = " K"; axis.labels.rotationDeg = 30;
    axis.title.text = "Thermal"; axis.title.fontFamily = "DejaVu Sans";
    axis.referenceLines = {273.15}; axis.position = 0.;
    const auto bytes = AxisToJson(axis);
    const auto restored = AxisFromJson(bytes);
    EXPECT_TRUE(axis == restored); EXPECT_EQ(bytes, AxisToJson(restored));
    PlotViewState::AxisState state{axis, axis, axis, std::nullopt};
    auto changed = state; changed.top->ticks.positions.push_back(350.);
    EXPECT_FALSE(state == changed);
    changed = state; changed.y.labels.suffix = "mK";
    EXPECT_FALSE(state == changed);
    std::unique_ptr<ImGuiContext, void(*)(ImGuiContext *)> gui(ImGui::CreateContext(), ImGui::DestroyContext);
    std::unique_ptr<ImPlotContext, void(*)(ImPlotContext *)> plot(ImPlot::CreateContext(), ImPlot::DestroyContext);
    auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {800,600}; io.DeltaTime = 1.f/60;
    io.ConfigDebugHighlightIdConflicts = true;
    unsigned char *pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    PlotSpec cacheSpec; cacheSpec.customAxes = true; cacheSpec.bounds = Bounds{0,1,0,1};
    cacheSpec.x.ticks.mode = TickMode::Positions; cacheSpec.x.ticks.positions = {0,.5,1};
    PlotViewState cacheState;
    auto frame = [&] {
        ImGui::NewFrame(); ImGui::SetNextWindowSize({700,500}); ImGui::Begin("Axis cache");
        RenderPlot("##cache", cacheSpec, nullptr, 0, cacheState, {}, {600,400});
        ImGui::End(); ImGui::Render(); EXPECT_EQ(GImGui->DebugDrawIdConflictsId, 0u);
    };
    frame(); frame(); ASSERT_TRUE(cacheState.axisState);
    const auto *positions = cacheState.axisState->x.ticks.positions.data();
    frame(); EXPECT_EQ(positions, cacheState.axisState->x.ticks.positions.data());
    cacheSpec.x.ticks.positions = {0,.25,.5,.75,1}; frame();
    EXPECT_EQ(cacheState.axisState->x.ticks.positions, cacheSpec.x.ticks.positions);
}
}

