#include "DocumentFixture.hpp"
// S9: command-based undo with deltas. Core UndoStack limits (opt-in), random edit undo/redo property, gesture merge, stack size, synchronous undo, exact
// restore of device/history/project operations.
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/UndoOps.hpp"
#include <scaps_core/device_project_yaml.hpp>
#include <chrono>
#include <random>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Go(Workflow &w, const Json &r) {
    const auto started = w.Dispatch(r);if(!started.at("ok").get<bool>()) return started;
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return w.response;
}
void Drain(Workflow &w) {while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
bool Ok(const Json &j) {return j.at("ok").get<bool>();}
Workflow &Fast(Workflow &w) { // short sweep: a run takes a few ms
    w.Current().SetDocument( ReadProject(SCAPSLAB_PROJECT));EditFixture(w.Current(),[&](Json& document){document["voltage"]= {{"start_V", 0.}, {"stop_V", .1}, {"step_V", .05}};});return w;
}

// ---- Core UndoStack limits ----------------------------------------------------------------------------------------------------------------------------
struct Step final : ICommand {
    int *value;int delta;std::size_t bytes;std::string gesture;
    Step(int *v, int d, std::size_t b = 0, std::string g = {}) : value(v), delta(d), bytes(b), gesture(std::move(g)) {}
    Result<void> Execute(CommandContext &) override {*value += delta;return {};}
    Result<void> Undo(CommandContext &) override {*value -= delta;return {};}
    std::string Description() const override {return "step";}
    bool IsUndoable() const noexcept override {return true;}
    std::size_t ByteSize() const noexcept override {return bytes;}
    bool CanMerge(const ICommand &n) const noexcept override {const auto *s = dynamic_cast<const Step *>(&n);return s && !gesture.empty() && s->gesture == gesture;}
    Result<void> Merge(std::unique_ptr<ICommand> n) override {const auto &s = static_cast<const Step &>(*n);delta += s.delta;bytes += s.bytes;return {};}
};
}
TEST(UndoStackLimits, DefaultIsUnlimited) {
    UndoStack s;int v = 0;for(int i = 0; i < 1000; ++i) {v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1, 1u << 20)));}
    EXPECT_EQ(s.GetUndoDepth(), 1000u);EXPECT_EQ(s.GetByteSize(), 1000u << 20);
}
TEST(UndoStackLimits, DepthCapDropsOldestAndKeepsApplyingOrder) {
    UndoStack s;s.SetLimits(3, 0);int v = 0;
    for(int i = 0; i < 5; ++i) {v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1)));}
    EXPECT_EQ(s.GetUndoDepth(), 3u);
    for(int i = 0; i < 3; ++i) ASSERT_TRUE(s.Undo());
    EXPECT_FALSE(s.CanUndo());EXPECT_EQ(v, 2); // the two dropped steps stay applied
    for(int i = 0; i < 3; ++i) ASSERT_TRUE(s.Redo());
    EXPECT_EQ(v, 5);
}
TEST(UndoStackLimits, ByteCapKeepsNewestStepEvenWhenItAloneIsOver) {
    UndoStack s;s.SetLimits(0, 250);int v = 0;
    for(int i = 0; i < 5; ++i) {v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1, 100)));}
    EXPECT_EQ(s.GetUndoDepth(), 2u);EXPECT_EQ(s.GetByteSize(), 200u);
    v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1, 1000)));
    EXPECT_EQ(s.GetUndoDepth(), 1u);EXPECT_EQ(s.GetByteSize(), 1000u);
}
TEST(UndoStackLimits, MergeGrowthIsCappedAndCleanPointFollowsTheDrop) {
    UndoStack s;s.SetLimits(2, 0);int v = 0;
    v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1)));
    v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1)));
    s.MarkClean();EXPECT_TRUE(s.IsClean());
    v += 1;ASSERT_TRUE(s.PushExecuted(CreateUnique<Step>(&v, 1))); // drops the oldest: the clean point (index 2) is now index 1
    EXPECT_EQ(s.GetUndoDepth(), 2u);EXPECT_FALSE(s.IsClean());
    ASSERT_TRUE(s.Undo());EXPECT_TRUE(s.IsClean());
    UndoStack m;m.SetLimits(0, 150);int w = 0;
    w += 1;ASSERT_TRUE(m.PushExecuted(CreateUnique<Step>(&w, 1, 100)));
    for(int i = 0; i < 3; ++i) {w += 1;ASSERT_TRUE(m.PushExecuted(CreateUnique<Step>(&w, 1, 100, "drag")));}
    EXPECT_EQ(m.GetUndoDepth(), 1u); // [100][drag 100] -> 200 > 150 drops the first; later merges keep the lone newest step
    UndoStack gone;gone.SetLimits(1, 0);int x = 0; // clean at the start (index 0)
    x += 1;ASSERT_TRUE(gone.PushExecuted(CreateUnique<Step>(&x, 1)));x += 1;ASSERT_TRUE(gone.PushExecuted(CreateUnique<Step>(&x, 1)));
    ASSERT_TRUE(gone.Undo());EXPECT_FALSE(gone.IsClean()); // the state before the dropped step can no longer be reached: never clean again
}

// ---- random edits: N undos == original (bit for bit), N redos == final -----------------------------------------------------------------------------------
TEST(ScapsLabUndo, RandomEditsUndoToTheOriginalAndRedoToTheFinalBitForBit) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));w.Current().SetDocument( ReadProject(SCAPSLAB_PROJECT));
    std::mt19937_64 rng(20261121);
    const auto uniform = [&](double lo, double hi) {return std::uniform_real_distribution<double>(lo, hi)(rng);};
    std::vector<std::string> states{w.Current().Document().dump()}; // document after each undo step (index = steps applied)
    std::string lastGesture;int gestureNo = 0,edits = 0;
    for(int i = 0; i < 120; ++i) {
        const int kind = int(rng() % 9);Json request;std::string gesture;
        auto doc = w.Current().Document();const auto layers = doc.at("layers").size();
        if(kind == 8 && layers < 5) request = {{"operation", "layer"}, {"action", "add"}};
        else if(kind == 7 && layers > 1) request = {{"operation", "layer"}, {"action", "remove"}, {"layer_id", doc["layers"].back().at("scapslab_id")}};
        else {
            if(rng() % 10 < 4 && !lastGesture.empty()) gesture = lastGesture;else gesture = "g" + std::to_string(++gestureNo);
            switch(kind) {
                case 0: doc["temperature_K"] = uniform(250.37, 399.91);break;
                case 1: doc["layers"][rng() % layers]["thickness_m"] = uniform(1.01e-6, 2.99e-6);break;
                case 2: doc["layers"][rng() % layers]["material"]["eps_r"] = uniform(5.01, 14.99);break;
                case 3: doc["contacts"]["left"]["phi_m_eV"] = uniform(4.81, 5.59);break;
                case 4: doc["voltage"]["stop_V"] = uniform(.51, .89);break;
                case 5: doc["mesh"]["points_per_layer"] = int(101 + rng() % 300);break;
                default: doc["layers"][rng() % layers]["doping"]["Nd_m3"] = uniform(1.01e21, 9.99e21);break;
            }
            request = {{"operation", "edit"}, {"document", doc}, {"gesture_id", gesture}};
        }
        const auto before = w.Current().Document().dump();
        const auto out = Go(w, request);ASSERT_TRUE(Ok(out)) << out.dump();
        const auto after = w.Current().Document().dump();if(after == before) continue;
        ++edits;
        const auto g = request.value("gesture_id", std::string{});
        if(!g.empty() && g == lastGesture) states.back() = after;else states.push_back(after);
        lastGesture = g;
    }
    ASSERT_GT(edits, 60);ASSERT_EQ(w.Current().undo.GetUndoDepth(), states.size() - 1);
    const auto depth = states.size() - 1;
    for(std::size_t k = depth; k-- > 0;) {ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));ASSERT_EQ(w.Current().Document().dump(), states[k]) << "undo to step " << k;}
    EXPECT_FALSE(w.Current().undo.CanUndo());
    for(std::size_t k = 1; k <= depth; ++k) {ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}})));ASSERT_EQ(w.Current().Document().dump(), states[k]) << "redo to step " << k;}
    // a random walk over the same stack lands on the recorded state every time
    std::size_t at = depth;
    for(int i = 0; i < 200; ++i) {
        const bool undo = at == 0 ? false : at == depth ? true : rng() % 2;
        ASSERT_TRUE(Ok(Go(w, {{"operation", undo ? "undo" : "redo"}})));at += undo ? std::size_t(-1) : 1;
        ASSERT_EQ(w.Current().Document().dump(), states[at]);
    }
    std::cout << "UNDO_PROPERTY edits=" << edits << " steps=" << depth << " stack_bytes=" << w.Current().undo.GetByteSize() << std::endl;
}

// ---- gesture merge -----------------------------------------------------------------------------------------------------------------------------------
TEST(ScapsLabUndo, OneDragIsOneStepAndTheMergedPatchStaysSmall) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);const auto original = w.Current().Document();
    for(int i = 0; i < 60; ++i) ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 301. + i * .37}}}, {"gesture_id", "drag"}})));
    ASSERT_EQ(w.Current().undo.GetUndoDepth(), 1u);
    EXPECT_LT(w.Current().undo.GetByteSize(), 2048u) << "a 60-tick drag keeps one patch pair, not 60";
    const auto last = w.Current().Document();
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));EXPECT_EQ(w.Current().Document().dump(), original.dump());
    ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}})));EXPECT_EQ(w.Current().Document().dump(), last.dump());
    // Esc revert: the value returns to where the gesture started; still one step, and undo/redo stay exact
    for(const double t : {320., 330., 300.}) ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", t}}}, {"gesture_id", "esc"}})));
    EXPECT_EQ(w.Current().undo.GetUndoDepth(), 2u);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));EXPECT_EQ(w.Current().Document().dump(), last.dump());
    ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}})));EXPECT_EQ(w.Current().Document().at("temperature_K"), 300.);
    // a different gesture, or none, never merges
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 311.5}}}, {"gesture_id", "other"}})));
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 312.5}}}})));
    EXPECT_EQ(w.Current().undo.GetUndoDepth(), 4u);
}

// ---- stack size --------------------------------------------------------------------------------------------------------------------------------------
TEST(ScapsLabUndo, TwoHundredEditsStayUnderOneMegabyte) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));w.Current().SetDocument( ReadProject(SCAPSLAB_PROJECT));
    // heaviest document: the embedded AM1.5G spectrum is what a snapshot undo would copy per edit
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"illumination", {{"scapslab_spectrum", "AM1.5G"}}}}}})));
    w.Current().undo.Clear();
    const auto docBytes = Undo::JsonBytes(w.Current().Document());
    for(int i = 0; i < 200; ++i) ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 250.1 + i * .31}}}})));
    ASSERT_EQ(w.Current().undo.GetUndoDepth(), 200u);
    const auto bytes = w.Current().undo.GetByteSize();
    std::cout << "UNDO_SIZE edits=200 stack_bytes=" << bytes << " document_bytes=" << docBytes << " snapshot_undo_would_be=" << 400 * docBytes << std::endl;
    EXPECT_LT(bytes, 1u << 20);EXPECT_LT(bytes * 20, 400 * docBytes);
    // the opt-in cap drops the oldest steps and never the newest
    w.Current().undo.SetLimits(50, 0);EXPECT_EQ(w.Current().undo.GetUndoDepth(), 50u);
}

// ---- undo is synchronous ---------------------------------------------------------------------------------------------------------------------------------
TEST(ScapsLabUndo, UndoNeedsNoJobAndCancelsTheRunningCalculationOfThatDevice) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);const auto original = w.Current().Document();
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 310.5}}}})));
    ASSERT_TRUE(w.jobs->PauseAllJobs()); // nothing can finish: whatever happens next is the caller's thread alone
    ASSERT_TRUE(w.Dispatch({{"operation", "run"}}).at("ok").get<bool>());const auto running = w.ActiveJob();ASSERT_NE(running, 0u);
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = w.Dispatch({{"operation", "undo"}});
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(Ok(out)) << out.dump();
    EXPECT_EQ(w.Current().Document().dump(), original.dump()) << "applied during the call, with the job queue paused";
    EXPECT_LT(ms, 50.) << "undo latency";std::cout << "UNDO_LATENCY_MS " << ms << std::endl;
    ASSERT_TRUE(w.jobs->ResumeAllJobs());Drain(w);
    EXPECT_EQ(w.jobs->GetJob(running)->status, JobStatus::Cancelled);EXPECT_TRUE(w.Current().result.empty()) << "a result for the undone document must not be adopted";
    EXPECT_EQ(w.Current().Document().dump(), original.dump());
    ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}})));EXPECT_EQ(w.Current().Document().at("temperature_K"), 310.5);
}
TEST(ScapsLabUndo, UndoEditsWithoutWaitingForPausedWorkers) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 301.5}}}})));
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 302.5}}}})));
    ASSERT_TRUE(w.jobs->PauseAllJobs());
    ASSERT_TRUE(w.Dispatch({{"operation", "edit"}, {"patch", {{"temperature_K", 333.5}}}}).at("ok").get<bool>()); // applied without a worker
    ASSERT_TRUE(w.CanUndo());
    ASSERT_TRUE(Ok(w.Dispatch({{"operation", "undo"}})));
    EXPECT_EQ(w.Current().undo.GetUndoDepth(), 2u) << "the synchronous edit is undone without waiting for a worker";
    ASSERT_TRUE(w.jobs->ResumeAllJobs());Drain(w);
    EXPECT_EQ(w.Current().Document().at("temperature_K"), 302.5) << "the undone edit does not reappear";EXPECT_EQ(w.Current().undo.GetUndoDepth(), 2u);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));EXPECT_EQ(w.Current().Document().at("temperature_K"), 301.5);
}
TEST(ScapsLabUndo, UndoOnOneDeviceLeavesTheOtherDevicesCalculationAlone) {
    Workflow w(CreateRef<JobSystem>(nullptr, 2));Fast(w);const auto first = w.project.active_device;
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_duplicate"}, {"name", "B"}})));const auto second = w.project.active_device;
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_select"}, {"device_id", first}})));
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 305.5}}}})));
    ASSERT_TRUE(w.jobs->PauseAllJobs());
    w.Submit(second, {{"operation", "run"}});const auto otherJob = w.LaneJob(second);ASSERT_NE(otherJob, 0u);
    ASSERT_TRUE(Ok(w.Dispatch({{"operation", "undo"}})));
    EXPECT_EQ(w.Device(first).Document().at("temperature_K"), 300) << "undone at once while the other device's job is pending";
    ASSERT_TRUE(w.jobs->ResumeAllJobs());Drain(w);
    EXPECT_EQ(w.jobs->GetJob(otherJob)->status, JobStatus::Completed);EXPECT_FALSE(w.Device(second).result.empty());
}
TEST(ScapsLabUndo, UndoingADeviceAddWhileItCalculatesCancelsItAndNothingBreaks) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_duplicate"}})));
    ASSERT_TRUE(w.jobs->PauseAllJobs());
    w.Submit(w.project.active_device, {{"operation", "run"}});const auto job = w.ActiveJob();ASSERT_NE(job, 0u);
    ASSERT_TRUE(Ok(w.Dispatch({{"operation", "undo"}})));EXPECT_EQ(w.project.devices.size(), 1u);
    ASSERT_TRUE(w.jobs->ResumeAllJobs());Drain(w);
    EXPECT_EQ(w.jobs->GetJob(job)->status, JobStatus::Cancelled);EXPECT_EQ(w.project.devices.size(), 1u);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}})));EXPECT_EQ(w.project.devices.size(), 2u);
}

// ---- device / history / project operations restore exactly ------------------------------------------------------------------------------------------------
namespace {
void ExpectExact(Workflow &w, const Json &request, const char *what) {
    const auto before = w.Workspace();const auto activeBefore = w.project.active_device;
    ASSERT_TRUE(Ok(Go(w, request))) << what << ": " << w.response.dump();
    const auto after = w.Workspace();EXPECT_NE(after, before) << what << " changed nothing";
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}}))) << what;
    EXPECT_EQ(w.Workspace(), before) << what << ": undo is not exact";EXPECT_EQ(w.project.active_device, activeBefore) << what;
    ASSERT_TRUE(Ok(Go(w, {{"operation", "redo"}}))) << what;
    EXPECT_EQ(w.Workspace(), after) << what << ": redo is not exact";
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}}))) << what;EXPECT_EQ(w.Workspace(), before) << what << ": second undo";
}
}
TEST(ScapsLabUndo, DeviceAddRemoveRenameMoveAreUndoableWithExactRestore) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    ExpectExact(w, {{"operation", "device_add"}, {"name", "Added"}}, "device_add");
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_add"}, {"name", "Added"}})));ASSERT_TRUE(Ok(Go(w, {{"operation", "device_duplicate"}})));
    ASSERT_EQ(w.project.devices.size(), 3u);const auto middle = w.project.devices[1]->id;
    ExpectExact(w, {{"operation", "device_update"}, {"device_id", middle}, {"name", "Renamed"}, {"colour", {.1, .2, .3, 1.}}, {"description", "d"}, {"compare", false}}, "device_update");
    ExpectExact(w, {{"operation", "device_move"}, {"device_id", middle}, {"index", 0}}, "device_move");
    ExpectExact(w, {{"operation", "device_remove"}, {"device_id", middle}}, "device_remove");
    ExpectExact(w, {{"operation", "device_remove"}, {"device_id", w.project.active_device}}, "device_remove of the active device");
    ExpectExact(w, {{"operation", "project_edit"}, {"name", "Other name"}, {"ui", [&] {auto ui = w.project.ui;ui["history_retention"] = 7;return ui;}()}}, "project_edit");
}
TEST(ScapsLabUndo, RemovedDeviceComesBackWithItsOwnUndoStack) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_duplicate"}})));const auto second = w.project.active_device;
    for(const double t : {311.5, 312.5}) ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", t}}}})));
    ASSERT_TRUE(Ok(Go(w, {{"operation", "device_remove"}, {"device_id", second}})));ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));
    ASSERT_EQ(w.project.devices.size(), 2u);EXPECT_EQ(w.Device(second).undo.GetUndoDepth(), 2u);EXPECT_EQ(w.Device(second).Document().at("temperature_K"), 312.5);
}
TEST(ScapsLabUndo, HistoryUpdateClearAndRetentionTrimAreUndoableSelectIsNot) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    auto ui = w.project.ui;ui["history_retention"] = 10;ASSERT_TRUE(Ok(Go(w, {{"operation", "project_edit"}, {"ui", ui}})));
    for(int i = 0; i < 4; ++i) {ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 300.5 + i}}}})));ASSERT_TRUE(Ok(Go(w, {{"operation", "run"}})));}
    ASSERT_EQ(w.Current().history.size(), 4u);
    const auto first = w.Current().history.front().at("run_id");const auto second = w.Current().history[1].at("run_id");
    {const auto depth = w.projectUndo.GetUndoDepth();ASSERT_TRUE(Ok(Go(w, {{"operation", "history_select"}, {"run_id", first}})));
     EXPECT_EQ(w.projectUndo.GetUndoDepth(), depth) << "selecting a run must not create an undo step";EXPECT_EQ(w.Current().selectedRun, first.get<std::string>());}
    ExpectExact(w, {{"operation", "history_update"}, {"run_id", second}, {"pinned", true}, {"name", "Keep"}, {"visible", false}, {"colour", {.2, .4, .6, 1.}}}, "history_update (pin/hide/rename)");
    ExpectExact(w, {{"operation", "history_clear"}, {"run_ids", Json::array({first})}}, "delete one run");
    ExpectExact(w, {{"operation", "history_clear"}}, "clear unpinned");
    ExpectExact(w, {{"operation", "history_clear"}, {"include_pinned", true}}, "clear all");
    auto small = w.project.ui;small["history_retention"] = 2;
    ExpectExact(w, {{"operation", "project_edit"}, {"ui", small}}, "lower retention trims history");
}
TEST(ScapsLabUndo, ProjectLoadClearsTheStacksAndSaveStaysCleanAfterUndo) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));Fast(w);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "edit"}, {"patch", {{"temperature_K", 301.5}}}})));EXPECT_TRUE(w.Current().dirty);
    ASSERT_TRUE(Ok(Go(w, {{"operation", "undo"}})));EXPECT_FALSE(w.Current().undo.CanUndo());
    ASSERT_TRUE(Ok(Go(w, {{"operation", "project_new"}})));EXPECT_FALSE(w.projectUndo.CanUndo());EXPECT_FALSE(w.Current().undo.CanUndo());
}
