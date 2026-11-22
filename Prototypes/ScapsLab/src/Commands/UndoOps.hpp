#pragma once
// S9: undoable ScapsLab commands. Every step stores a small delta (json::diff patch pair, run-history delta, ids and indices), never a document or workspace
// snapshot. Undo/redo run synchronously on the UI thread (no job round trip); the commands only change state and emit events, Workflow::Step decides which
// in-flight jobs the change invalidates. Pushed with UndoStack::PushExecuted after the in-place operation already ran.
#include "Workflow.hpp"
namespace SST::ScapsLab::Undo {
std::size_t JsonBytes(const Json &j); // approximate heap size of a Json tree (undo byte accounting)

// Document edit: typed field and structural deltas. Same non-empty gesture on the same device merges into one step (one slider drag = one undo step).
Unique<ICommand> Edit(Workflow &w, std::string device, const ProjectDraft &before, const ProjectDraft &after, std::string gesture);

// History delta of one device, keyed by run_id: erased entries (with their typed results), inserted entries, entry patches, plus selection/status and,
// for a clear, the live results. Built by comparing the device before and after the in-place operation.
struct DeviceBefore {
    std::string id, selected, status;
    Ref<const Json> history;
    DeviceResults results;
    explicit DeviceBefore(const DeviceState &d) : id(d.id), selected(d.selectedRun), status(d.status), history(d.history.share()), results(d.Results()) {}
};
struct DeviceDelta;
struct Deltas {
    std::vector<std::shared_ptr<DeviceDelta>> items;
    bool Empty() const {return items.empty();}
};
Deltas Compare(const std::vector<DeviceBefore> &before, Workflow &w, bool withLiveResults); // only devices that changed
Unique<ICommand> History(Workflow &w, std::string description, Deltas deltas);
Unique<ICommand> ProjectEdit(Workflow &w, std::string nameBefore, std::string nameAfter, const Json &uiBefore, const Json &uiAfter, Deltas trimmedHistory);

Unique<ICommand> DeviceAdded(Workflow &w, Ref<DeviceState> device, std::size_t index, std::string activeBefore, std::string description);
Unique<ICommand> DeviceRemoved(Workflow &w, Ref<DeviceState> device, std::size_t index, std::string activeBefore, std::string activeAfter, bool wasCompared);
struct DeviceMeta {std::string name, description; Json colour; bool compare = false;};
Unique<ICommand> DeviceUpdated(Workflow &w, std::string device, DeviceMeta before, DeviceMeta after);
Unique<ICommand> DeviceMoved(Workflow &w, std::string device, std::size_t from, std::size_t to);
}
