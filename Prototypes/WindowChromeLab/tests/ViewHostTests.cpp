#include <gtest/gtest.h>
#include "Presentation/Panels/ViewHost.hpp"
#include "Vendor/ImGui/imgui.h"
#include "Vendor/ImGui/imgui_internal.h"
#include <limits>

namespace SST
{
    using Json = nlohmann::json;
    class ViewHostTest : public testing::Test
    {
    protected:
        PanelRegistry panels;
        Ref<WindowWorkspace> workspace = CreateRef<WindowWorkspace>(panels);
        Ref<CommandRegistry> commands = CreateRef<CommandRegistry>();
        Ref<JobSystem> jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
        Ref<ViewHost> host = CreateRef<ViewHost>(panels, workspace);
        Json catalog{{"operation", "catalog"}, {"metrics", Json::array({{{"id", "voc_V"}, {"name", "Voc"}, {"unit", "V"}}})},
            {"items", Json::array({
                {{"id", "a"}, {"name", "Alpha"}, {"metrics", {{"voc_V", .8}}}},
                {{"id", "b"}, {"name", "Beta"}, {"metrics", {{"voc_V", .7}}}},
                {{"id", "c"}, {"name", "Missing"}, {"metrics", Json::object()}}})}};
        void SetUp() override
        {
            ASSERT_TRUE(workspace->Bind(commands, {}));
            ASSERT_TRUE(host->Bind(commands, jobs));
            ASSERT_TRUE(host->RegisterTemplate({"iv", "I-V", [](ViewInstance &, const Json &) { ImGui::TextUnformatted("Plot content"); }}));
            ASSERT_TRUE(call(catalog).at("ok").get<bool>());
            call({{"operation", "active"}, {"item_id", "a"}});
        }
        Json call(const Json &request)
        {
            auto response = host->Execute(request);
            EXPECT_TRUE(response.at("ok").get<bool>()) << response.dump(); return response;
        }
        WindowId open(std::string type = "iv", Json binding = {{"mode", "follow_active"}})
        {
            return call({{"operation", "open"}, {"template_id", type}, {"binding", binding}}).at("instance_id").get<WindowId>();
        }
        Json rows(WindowId id) { return call({{"operation", "rows"}, {"instance_id", id}}).at("items"); }
        Json all() { return {{"mode", "explicit"}, {"item_ids", Json::array({"a", "b", "c"})}}; }
    };
    TEST_F(ViewHostTest, FollowPinnedAndExplicitStayIndependent)
    {
        const auto follow = open(), pinned = open("iv", {{"mode", "pinned"}, {"item_ids", Json::array({"a"})}}), set = open("iv", all());
        call({{"operation", "active"}, {"item_id", "b"}});
        EXPECT_EQ(rows(follow)[0]["id"], "b"); EXPECT_EQ(rows(pinned)[0]["id"], "a"); EXPECT_EQ(rows(set).size(), 3u);
        call({{"operation", "active"}, {"item_id", ""}}); EXPECT_TRUE(rows(follow).empty());
        EXPECT_NE(host->Find(follow)->ImGuiName(), host->Find(pinned)->ImGuiName());
        auto withoutA = catalog; withoutA["items"].erase(0); call(withoutA);
        EXPECT_TRUE(rows(pinned).empty()); EXPECT_EQ(rows(set).size(), 2u);
        EXPECT_EQ(host->Find(pinned)->Binding()["item_ids"][0], "a");
    }
    TEST_F(ViewHostTest, DuplicateCopiesBindingAndCloseKeepsData)
    {
        const auto a = open();
        const auto b = call({{"operation", "duplicate"}, {"instance_id", a}}).at("instance_id").get<WindowId>();
        call({{"operation", "bind"}, {"instance_id", b}, {"binding", {{"mode", "pinned"}, {"item_ids", Json::array({"b"})}}}});
        EXPECT_EQ(rows(a)[0]["id"], "a"); EXPECT_EQ(rows(b)[0]["id"], "b");
        call({{"operation", "close"}, {"instance_id", a}}); EXPECT_FALSE(host->Find(a)->IsVisible()); EXPECT_EQ(rows(b).size(), 1u);
        call({{"operation", "activate"}, {"instance_id", a}}); EXPECT_TRUE(host->Find(a)->IsVisible());
        EXPECT_EQ(rows(a)[0]["id"], "a");
        for (int i = 0; i < 20; ++i) EXPECT_NE(open(), a);
    }
    TEST_F(ViewHostTest, ComparisonSortFilterAndSelectionDriveOnlyBoundOverlays)
    {
        const auto table = open("core.comparison", all());
        auto binding = all(); binding["selection_source"] = table;
        const auto overlay = open("iv", binding), other = open("iv", all());
        call({{"operation", "table"}, {"instance_id", table}, {"table", {{"sort_metric_id", "voc_V"}}}});
        EXPECT_EQ(rows(table)[0]["id"], "b"); EXPECT_EQ(rows(table)[2]["id"], "c");
        call({{"operation", "table"}, {"instance_id", table}, {"table", {{"sort_metric_id", "voc_V"}, {"descending", true}, {"selected_item_ids", Json::array({"a", "b"})}}}});
        EXPECT_EQ(rows(table)[0]["id"], "a"); EXPECT_EQ(rows(table)[2]["id"], "c"); EXPECT_EQ(rows(overlay).size(), 2u);
        call({{"operation", "table"}, {"instance_id", table}, {"table", {{"filter_text", "ALP"}, {"selected_item_ids", Json::array({"a", "b"})}}}});
        EXPECT_EQ(rows(overlay).size(), 1u); EXPECT_EQ(rows(overlay)[0]["id"], "a"); EXPECT_EQ(rows(other).size(), 3u);
        call({{"operation", "table"}, {"instance_id", table}, {"table", {{"selected_item_ids", Json::array()}}}}); EXPECT_TRUE(rows(overlay).empty());
        call({{"operation", "table"}, {"instance_id", table}, {"table", Json::object()}}); EXPECT_EQ(rows(overlay).size(), 3u);
        ASSERT_TRUE(panels.Remove(table)); EXPECT_TRUE(rows(overlay).empty()); EXPECT_EQ(rows(other).size(), 3u);
    }
    TEST_F(ViewHostTest, WorkspaceRestoresAllInstancesWithoutSeedWindows)
    {
        const auto table = open("core.comparison", all()); auto b = all(); b["selection_source"] = table;
        const auto overlay = open("iv", b);
        call({{"operation", "table"}, {"instance_id", table}, {"table", {{"filter_text", "alpha"}, {"selected_item_ids", Json::array({"a"})}}}});
        auto state = host->Find(overlay)->GetState(); state.dockId = 42; host->Find(overlay)->ApplyState(state);
        const auto saved = call({{"operation", "save"}}).at("layout").get<std::string>();
        panels.Clear(); call({{"operation", "load"}, {"layout", saved}});
        EXPECT_EQ(workspace->Save(), saved); EXPECT_EQ(rows(overlay).size(), 1u); EXPECT_EQ(host->Find(overlay)->GetState().dockId, 42u);
        auto broken = Json::parse(saved); auto content = Json::parse(broken["windows"][1]["view"].get<std::string>());
        content["binding"]["mode"] = "invalid"; broken["windows"][1]["view"] = content.dump();
        EXPECT_FALSE(host->Execute({{"operation", "load"}, {"layout", broken.dump()}}).at("ok").get<bool>());
        EXPECT_EQ(workspace->Save(), saved);
    }
    TEST_F(ViewHostTest, RejectsInvalidRequestsWithoutChangingState)
    {
        const auto id = open(); const auto saved = workspace->Save();
        for (const auto &request : Json::array({
            {{"operation", "open"}, {"template_id", "unknown"}},
            {{"operation", "bind"}, {"instance_id", id}, {"binding", {{"mode", "pinned"}}}},
            {{"operation", "bind"}, {"instance_id", id}, {"binding", {{"mode", "explicit"}, {"item_ids", Json::array({"a", "a"})}}}},
            {{"operation", "bind"}, {"instance_id", id}, {"binding", {{"mode", "explicit"}, {"selection_source", id}}}},
            {{"operation", "close"}, {"instance_id", -1}},
            {{"operation", "close"}, {"instance_id", 1.5}},
            {{"operation", "active"}, {"item_id", "unknown"}},
            {{"operation", "table"}, {"instance_id", id}, {"table", Json::object()}}
        }))
        {
            const auto result = host->Execute(request); EXPECT_FALSE(result.at("ok").get<bool>()) << result.dump();
            EXPECT_TRUE(result.contains("error")); EXPECT_EQ(workspace->Save(), saved);
        }
        auto invalid = catalog; invalid["items"][0]["metrics"]["voc_V"] = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(host->Execute(invalid).at("ok").get<bool>()); EXPECT_EQ(rows(id)[0]["metrics"]["voc_V"], .8);
        invalid = catalog; invalid["items"].push_back(invalid["items"][0]); EXPECT_FALSE(host->Execute(invalid).at("ok").get<bool>());
        EXPECT_FALSE(host->RegisterTemplate({"iv", "Duplicate", {}}));
        EXPECT_FALSE(host->RegisterTemplate({"bad#id", "Invalid", {}}));
    }
    TEST_F(ViewHostTest, UsesCommandAndJobAndRejectsShutdown)
    {
        auto observed = 0;
        commands->AddObserver([&](const CommandExecutionEvent &event) { if (event.id.value == "view.execute") ++observed; });
        const auto before = jobs->GetAllJobs().size(); open(); EXPECT_EQ(jobs->GetAllJobs().size(), before + 1); EXPECT_EQ(observed, 2);
        jobs->Shutdown(); EXPECT_FALSE(host->Execute({{"operation", "list"}}).at("ok").get<bool>());
    }
    TEST_F(ViewHostTest, MultipleTablesRenderWithoutIdConflicts)
    {
        ImGui::CreateContext(); auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {1280, 900};
        io.ConfigDebugHighlightIdConflicts = true; io.Fonts->AddFontDefault(); io.Fonts->Build();
        const auto a = open("core.comparison", all()); open("core.comparison", all()); open();
        call({{"operation", "table"}, {"instance_id", a}, {"table", {{"sort_metric_id", "voc_V"}, {"descending", true}}}});
        for (int frame = 0; frame < 3; ++frame)
        {
            io.DeltaTime = 1.f / 60; ImGui::NewFrame();
            for (const auto &entry : panels.Entries()) entry.panel->Render();
            ImGui::Render(); EXPECT_EQ(GImGui->DebugDrawIdConflictsId, 0u);
        }
        EXPECT_EQ(rows(a).size(), 3u); EXPECT_EQ(host->Find(a)->Table()["sort_metric_id"], "voc_V");
        EXPECT_TRUE(host->Find(a)->Table()["descending"].get<bool>()); ImGui::DestroyContext();
    }
}
