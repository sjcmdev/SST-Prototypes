#include <gtest/gtest.h>
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include <nlohmann/json.hpp>
#include "Core/Utils/FramebufferScreenshot.hpp"

namespace SST
{
    class TestWindow : public EditorWindow
    {
    public:
        TestWindow(std::string type = "plot", WindowCapabilities capabilities = {.duplicable = true})
            : EditorWindow(std::move(type), "A", true, capabilities) {}
        WindowDuplicate Duplicate() const override { return {CreateRef<TestWindow>(*this), {}}; }
        bool IsDirty() const override { return dirty; }
        void RenderContent() override {}
        bool dirty = false;
        int revealed = 0;
    protected:
        void RevealInProject() override { ++revealed; }
    };
    class WindowStateTest : public testing::Test
    {
    protected:
        PanelRegistry panels;
        Ref<WindowWorkspace> workspace = CreateRef<WindowWorkspace>(panels);
        Ref<CommandRegistry> commands = CreateRef<CommandRegistry>();
        Ref<UndoStack> undo = CreateRef<UndoStack>();
        Ref<KeymapResolver> keymap = CreateRef<KeymapResolver>();
        WindowId a = 0, b = 0, tools = 0;
        void SetUp() override
        {
            commands->SetUndoStack(undo);
            ASSERT_TRUE(workspace->Bind(commands, keymap));
            a = panels.Add<TestWindow>(); b = panels.Add<TestWindow>();
            tools = panels.Add<TestWindow>("tools", WindowCapabilities{.closable = false, .duplicable = false});
        }
        Ref<EditorWindow> get(WindowId id) { return workspace->Find(id); }
        Result<void> act(WindowAction action, WindowId id = 0) { return workspace->Execute({action, id}); }
    };
    TEST_F(WindowStateTest, AuxiliaryWindowsStartHiddenAndAreNotPersisted)
    {
        const auto id = panels.Add<TestWindow>("auxiliary.import", WindowCapabilities{.auxiliary = true});
        EXPECT_FALSE(get(id)->IsVisible());
        ASSERT_TRUE(act(WindowAction::Activate, id)); EXPECT_TRUE(get(id)->IsVisible());
        const auto saved = nlohmann::json::parse(workspace->Save());
        for (const auto &item : saved["windows"]) EXPECT_NE(item["id"].get<WindowId>(), id);
        ASSERT_TRUE(act(WindowAction::Close, id)); EXPECT_FALSE(get(id)->IsVisible());
    }
    TEST_F(WindowStateTest, ResetLayoutCommandWorksWithoutImGui)
    {
        const auto initial = get(a)->GetState();
        ASSERT_TRUE(act(WindowAction::Maximize, a)); ASSERT_TRUE(act(WindowAction::Pin, b));
        ASSERT_TRUE(act(WindowAction::Close, a)); ASSERT_TRUE(act(WindowAction::LockLayout));
        ASSERT_TRUE(commands->Execute(CommandID{"app.layout.reset"}));
        EXPECT_EQ(get(a)->GetState(), initial); EXPECT_FALSE(get(b)->GetState().pinned); EXPECT_FALSE(workspace->IsLocked());
        EXPECT_EQ(get(a)->GetId(), a);
    }
    TEST_F(WindowStateTest, MinimizeMaximizedThenRestorePreservesOriginalGeometryAndDock)
    {
        auto initial = get(a)->GetState(); initial.x = 73; initial.dockId = 42; get(a)->ApplyState(initial);
        ASSERT_TRUE(act(WindowAction::Maximize, a));
        ASSERT_TRUE(act(WindowAction::Minimize, a));
        EXPECT_EQ(get(a)->GetState().mode, WindowMode::Minimized);
        ASSERT_TRUE(act(WindowAction::Restore, a));
        EXPECT_EQ(get(a)->GetState().mode, WindowMode::Maximized);
        ASSERT_TRUE(act(WindowAction::Restore, a));
        EXPECT_EQ(get(a)->GetState(), initial);
    }
    TEST_F(WindowStateTest, RepeatedMinimizeKeepsRestoreMode)
    {
        ASSERT_TRUE(act(WindowAction::Minimize, a)); ASSERT_TRUE(act(WindowAction::Minimize, a));
        ASSERT_TRUE(act(WindowAction::Restore, a)); EXPECT_EQ(get(a)->GetState().mode, WindowMode::Normal);
    }
    TEST_F(WindowStateTest, PinBlocksGeometryActionsAndUndoRestoresDock)
    {
        auto initial = get(a)->GetState(); initial.dockId = 42; get(a)->ApplyState(initial);
        ASSERT_TRUE(act(WindowAction::Pin, a));
        EXPECT_TRUE(get(a)->GetState().pinned);
        EXPECT_FALSE(act(WindowAction::Float, a)); EXPECT_FALSE(act(WindowAction::Maximize, a));
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(get(a)->GetState(), initial);
    }
    TEST_F(WindowStateTest, RenameKeepsIndependentIdAndImGuiIdentity)
    {
        const auto old = get(a)->ImGuiName();
        ASSERT_TRUE(workspace->Execute({WindowAction::Rename, a, "Short", "Long descriptive name"}));
        EXPECT_EQ(get(a)->GetState().longName, "Long descriptive name");
        EXPECT_EQ(get(a)->GetId(), a);
        EXPECT_EQ(old.substr(old.find("###")), get(a)->ImGuiName().substr(get(a)->ImGuiName().find("###")));
        EXPECT_FALSE(workspace->Execute({WindowAction::Rename, a, "bad###ID", "Long"}));
        EXPECT_FALSE(workspace->Execute({WindowAction::Rename, a, "", "Long"}));
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(get(a)->GetTitle(), "A");
    }
    TEST_F(WindowStateTest, DuplicateIdsStayUniqueAcrossUndoRedoAndNewBranch)
    {
        ASSERT_TRUE(act(WindowAction::Duplicate, a)); const auto copy = workspace->Windows().back(); const auto id = copy->GetId();
        EXPECT_NE(id, a); EXPECT_NE(id, b); EXPECT_NE(id, tools);
        ASSERT_TRUE(undo->Undo()); EXPECT_FALSE(panels.Contains(id));
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(get(id), copy);
        ASSERT_TRUE(undo->Undo()); ASSERT_TRUE(act(WindowAction::Duplicate, a));
        EXPECT_GT(workspace->Windows().back()->GetId(), id); EXPECT_FALSE(undo->CanRedo());
    }
    TEST_F(WindowStateTest, NonDuplicableWindowCannotBypassFactory)
    {
        EXPECT_FALSE(act(WindowAction::Duplicate, tools)); EXPECT_EQ(panels.Clone(a), 0);
        EXPECT_EQ(workspace->Windows().size(), 3u);
    }
    TEST_F(WindowStateTest, CloseOthersRespectsCapabilitiesAndUndoPreservesContent)
    {
        ASSERT_TRUE(act(WindowAction::CloseOthers, a));
        EXPECT_TRUE(get(a)->IsVisible()); EXPECT_FALSE(get(b)->IsVisible()); EXPECT_TRUE(get(tools)->IsVisible());
        ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(get(b)->IsVisible());
    }
    TEST_F(WindowStateTest, CloseTypeDoesNotCloseOtherTypes)
    {
        ASSERT_TRUE(act(WindowAction::CloseType, a)); EXPECT_FALSE(get(a)->IsVisible());
        EXPECT_FALSE(get(b)->IsVisible()); EXPECT_TRUE(get(tools)->IsVisible());
    }
    TEST_F(WindowStateTest, DirtyBulkCloseIsAtomicAndConfirmationDoesNotEraseData)
    {
        std::static_pointer_cast<TestWindow>(get(b))->dirty = true;
        EXPECT_FALSE(act(WindowAction::CloseType, a)); EXPECT_TRUE(get(a)->IsVisible()); EXPECT_TRUE(get(b)->IsVisible());
        WindowRequest request{WindowAction::CloseType, a}; request.confirmed = true;
        ASSERT_TRUE(workspace->Execute(request)); EXPECT_FALSE(get(b)->IsVisible());
        ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(get(b)->IsDirty()); EXPECT_TRUE(get(b)->IsVisible());
    }
    TEST_F(WindowStateTest, LockedLayoutRejectsPlacementButAllowsCloseAndRename)
    {
        ASSERT_TRUE(act(WindowAction::LockLayout));
        EXPECT_FALSE(act(WindowAction::Float, a)); EXPECT_FALSE(act(WindowAction::Pin, a));
        EXPECT_FALSE(act(WindowAction::TileVertical)); EXPECT_FALSE(act(WindowAction::Maximize, a));
        EXPECT_TRUE(workspace->Execute({WindowAction::Rename, a, "B", "B"})); EXPECT_TRUE(act(WindowAction::Close, a));
    }
    TEST_F(WindowStateTest, TileSkipsPinnedAndMinimizedWindows)
    {
        ASSERT_TRUE(act(WindowAction::Pin, a)); ASSERT_TRUE(act(WindowAction::Minimize, b));
        const auto pinned = get(a)->GetState(), minimized = get(b)->GetState();
        workspace->SetAvailableArea(10, 20, 1000, 600); ASSERT_TRUE(act(WindowAction::TileHorizontal));
        EXPECT_EQ(get(a)->GetState(), pinned); EXPECT_EQ(get(b)->GetState(), minimized);
        EXPECT_EQ(get(tools)->GetState().width, 1000); EXPECT_EQ(get(tools)->GetState().height, 600);
    }
    TEST_F(WindowStateTest, ArrangementsPartitionAvailableArea)
    {
        workspace->SetAvailableArea(0, 30, 1200, 900); ASSERT_TRUE(act(WindowAction::TileVertical));
        EXPECT_FLOAT_EQ(get(a)->GetState().width, 400); EXPECT_FLOAT_EQ(get(b)->GetState().x, 400);
        ASSERT_TRUE(act(WindowAction::TileHorizontal)); EXPECT_FLOAT_EQ(get(b)->GetState().y, 330);
        ASSERT_TRUE(act(WindowAction::Cascade)); EXPECT_FLOAT_EQ(get(b)->GetState().x, 24);
    }
    TEST_F(WindowStateTest, KeymapCyclesThroughMinimizedButNotClosedWindows)
    {
        ContextManager contexts;
        const auto binding = keymap->Resolve({KeyCode::Tab, KeyModifiers::Ctrl}, contexts);
        ASSERT_TRUE(binding); EXPECT_EQ(binding->commandId, CommandID{"window.next"});
        ASSERT_TRUE(act(WindowAction::Activate, a)); ASSERT_TRUE(act(WindowAction::Minimize, b));
        ASSERT_TRUE(commands->Execute(binding->commandId));
        EXPECT_EQ(workspace->Capture().active, b); EXPECT_EQ(get(b)->GetState().mode, WindowMode::Normal);
        ASSERT_TRUE(act(WindowAction::Close, b)); ASSERT_TRUE(act(WindowAction::Activate, a));
        ASSERT_TRUE(commands->Execute(binding->commandId)); EXPECT_EQ(workspace->Capture().active, tools);
    }
    TEST_F(WindowStateTest, ShowMinimizedDoesNotReopenClosedWindows)
    {
        ASSERT_TRUE(act(WindowAction::Minimize, a)); ASSERT_TRUE(act(WindowAction::Minimize, b));
        ASSERT_TRUE(act(WindowAction::Close, b)); ASSERT_TRUE(act(WindowAction::ShowMinimized));
        EXPECT_EQ(get(a)->GetState().mode, WindowMode::Normal); EXPECT_FALSE(get(b)->IsVisible());
    }
    TEST_F(WindowStateTest, LayoutRoundtripRecreatesCopiesAndPreservesState)
    {
        ASSERT_TRUE(act(WindowAction::Duplicate, a)); const auto copyId = workspace->Windows().back()->GetId();
        ASSERT_TRUE(workspace->Execute({WindowAction::Rename, copyId, "Copy", "Long copy"}));
        ASSERT_TRUE(act(WindowAction::Maximize, copyId)); ASSERT_TRUE(act(WindowAction::Minimize, copyId));
        ASSERT_TRUE(act(WindowAction::Pin, a)); ASSERT_TRUE(act(WindowAction::LockLayout));
        const auto saved = workspace->Save();
        ASSERT_TRUE(panels.Remove(copyId)); ASSERT_TRUE(workspace->Load(saved)); EXPECT_EQ(workspace->Save(), saved);
        EXPECT_NE(get(copyId).get(), get(a).get()); EXPECT_TRUE(workspace->IsLocked());
    }
    TEST_F(WindowStateTest, InvalidLayoutIsAtomic)
    {
        const auto original = workspace->Save(); auto json = nlohmann::json::parse(original);
        json["windows"][1]["id"] = a; EXPECT_FALSE(workspace->Load(json.dump())); EXPECT_EQ(workspace->Save(), original);
        json = nlohmann::json::parse(original); json["windows"][1]["state"]["width"] = -1;
        EXPECT_FALSE(workspace->Load(json.dump())); EXPECT_EQ(workspace->Save(), original);
        json = nlohmann::json::parse(original); json["windows"][1]["state"]["mode"] = 42;
        EXPECT_FALSE(workspace->Load(json.dump())); EXPECT_EQ(workspace->Save(), original);
        EXPECT_FALSE(workspace->Load("{not json")); EXPECT_EQ(workspace->Save(), original);
    }
    TEST_F(WindowStateTest, LayoutCannotExceedCapabilitiesOrReplaceWindowType)
    {
        auto json = nlohmann::json::parse(workspace->Save()); json["windows"][2]["state"]["visible"] = false;
        EXPECT_FALSE(workspace->Load(json.dump())); EXPECT_TRUE(get(tools)->IsVisible());
        json = nlohmann::json::parse(workspace->Save()); json["windows"][0]["type"] = "tools";
        EXPECT_FALSE(workspace->Load(json.dump()));
    }
    TEST_F(WindowStateTest, LoadingOlderWorkspaceRetainsNewDocumentWindows)
    {
        const auto old = workspace->Save(); ASSERT_TRUE(act(WindowAction::Duplicate, a));
        const auto copy = workspace->Windows().back(); std::static_pointer_cast<TestWindow>(copy)->dirty = true;
        ASSERT_TRUE(workspace->Load(old)); EXPECT_EQ(get(copy->GetId()), copy); EXPECT_TRUE(copy->IsDirty());
    }
    TEST_F(WindowStateTest, RevealUsesWindowExtensionAndCapabilities)
    {
        const auto id = panels.Add<TestWindow>("project.plot", WindowCapabilities{.projectItem = true});
        ASSERT_TRUE(act(WindowAction::Reveal, id)); EXPECT_EQ(std::static_pointer_cast<TestWindow>(get(id))->revealed, 1);
        EXPECT_FALSE(act(WindowAction::Reveal, a));
    }
    class EngineCommand final : public ICommand
    {
    public:
        explicit EngineCommand(Ref<int> count) : count(std::move(count)) {}
        Result<void> Execute(CommandContext &) override { ++*count; return {}; }
        Result<void> Undo(CommandContext &) override { --*count; return {}; }
        bool IsUndoable() const noexcept override { return true; }
        std::string Description() const override { return "Clone engine document"; }
        Ref<int> count;
    };
    TEST_F(WindowStateTest, StartupOpenAdaptsScapsLoadRequest)
    {
        nlohmann::json request;
        auto count = CreateRef<int>(0);
        ASSERT_TRUE(commands->Register({CommandID{"scapslab.load"}, "Load ScapsLab project"},
            [&request, count](CommandContext &context) {
                if (auto value = context.TryGet<nlohmann::json>("request")) request = *value;
                return CreateUnique<EngineCommand>(count);
            }));
        const Path path("C:/fixtures/device.yaml");
        auto opened = ApplicationDetail::OpenStartupFile(*commands, path);
        ASSERT_TRUE(opened); EXPECT_EQ(opened.Value().id.value, "scapslab.load");
        const nlohmann::json expected{{"operation", "load"}, {"path", path.Utf8()}};
        EXPECT_EQ(request, expected); EXPECT_EQ(*count, 1);
    }
    TEST_F(WindowStateTest, AuxiliaryFollowsSuccessfulOpenAndConfirmCommands)
    {
        auto count = CreateRef<int>(0);
        for (const auto *name : {"import.preview", "import.confirm"})
            ASSERT_TRUE(commands->Register({CommandID{name}, name}, [count](CommandContext &) { return CreateUnique<EngineCommand>(count); }));
        const auto id = panels.Add<TestWindow>("import", WindowCapabilities{.auxiliary = true,
            .openCommand = "import.preview", .closeCommand = "import.confirm"});
        ASSERT_TRUE(commands->Execute(CommandID{"import.preview"})); EXPECT_TRUE(get(id)->IsVisible());
        ASSERT_TRUE(act(WindowAction::Minimize, id));
        ASSERT_TRUE(commands->Execute(CommandID{"import.preview"})); EXPECT_EQ(get(id)->GetState().mode, WindowMode::Normal);
        ASSERT_TRUE(commands->Execute(CommandID{"import.confirm"})); EXPECT_FALSE(get(id)->IsVisible());
    }
    class EngineWindow final : public TestWindow
    {
    public:
        explicit EngineWindow(Ref<int> count) : TestWindow("engine"), count(std::move(count)) {}
        WindowDuplicate Duplicate() const override { return {CreateRef<EngineWindow>(*this), CreateUnique<EngineCommand>(count)}; }
        Ref<int> count;
    };
    TEST_F(WindowStateTest, EngineDuplicationSharesOneUndoRecordAndRedoDoesNotAllocateNewViewId)
    {
        auto count = CreateRef<int>(0); const auto id = panels.Add<EngineWindow>(count);
        ASSERT_TRUE(act(WindowAction::Duplicate, id)); EXPECT_EQ(*count, 1);
        const auto copyId = workspace->Windows().back()->GetId(); ASSERT_TRUE(undo->Undo()); EXPECT_EQ(*count, 0);
        EXPECT_FALSE(get(copyId)); ASSERT_TRUE(undo->Redo()); EXPECT_EQ(*count, 1); EXPECT_TRUE(get(copyId));
    }
    TEST_F(WindowStateTest, RegistryRejectsReusingAnInstanceOrId)
    {
        auto original = get(a); EXPECT_EQ(panels.AddExisting(original), 0); EXPECT_EQ(original->GetId(), a);
        EXPECT_EQ(panels.AddExisting(CreateRef<TestWindow>(), a), 0);
    }
    TEST_F(WindowStateTest, MaximumLengthDuplicateNameRemainsSerializable)
    {
        const std::string name(255, 'A');
        ASSERT_TRUE(workspace->Execute({WindowAction::Rename, a, name, name}));
        ASSERT_TRUE(act(WindowAction::Duplicate, a)); ASSERT_TRUE(workspace->Load(workspace->Save()));
    }

}
