#include "Commands/UndoOps.hpp"
#include <algorithm>
#include <map>
#include <optional>
namespace SST::ScapsLab::Undo {
std::size_t JsonBytes(const Json &j) {
    std::size_t n = sizeof(Json);
    if(const auto *s = j.get_ptr<const std::string *>()) n += s->capacity();
    else if(j.is_array()) {n += 24;for(const auto &e : j) n += JsonBytes(e);}
    else if(j.is_object()) for(auto it = j.begin(); it != j.end(); ++it) n += 48 + it.key().capacity() + JsonBytes(it.value());
    return n;
}
namespace {
StructuredError Failed(const std::exception &e) {
    return StructuredError{ErrorCategory::Validation, Severity::Error, "Undo failed", e.what(), "The step could not be applied", "ScapsLab", "undo.apply"};
}
void ProjectChanged(Workflow &w, bool list = false) {
    w.projectDirty = true;w.RefreshDirty();
    if(list) w.Emit(DeviceListChanged());
}
Json Concat(const Json &a, const Json &b) {Json r = a;for(const auto &op : b) r.push_back(op);return r;}

// ---- document edit ------------------------------------------------------------------------------------------------------------------------------------
class EditCmd final : public ICommand {
    Workflow &w;std::string device, gesture;Json fwd, back;std::size_t bytes = 0;
    void Measure() {bytes = sizeof(*this) + JsonBytes(fwd) + JsonBytes(back);}
    Result<void> Apply(const Json &patch) {
        try {
            auto &d = w.Device(device);
            w.ApplyDocument(d, d.document.patch(patch)); // patch() builds a new document: a failing patch leaves the old one untouched
            return {};
        } catch(const std::exception &e) {return Failed(e);}
    }
public:
    EditCmd(Workflow &w, std::string device, Json fwd, Json back, std::string gesture) : w(w), device(std::move(device)), gesture(std::move(gesture)), fwd(std::move(fwd)), back(std::move(back)) {Measure();}
    std::string Description() const override {return "Edit parameters";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(fwd);}
    Result<void> Undo(CommandContext &) override {return Apply(back);}
    std::size_t ByteSize() const noexcept override {return bytes;}
    bool CanMerge(const ICommand &next) const noexcept override {
        const auto *e = dynamic_cast<const EditCmd *>(&next);
        return e && !gesture.empty() && gesture == e->gesture && device == e->device;
    }
    // The newest step was just applied, so the live document is its "after". The merged step is the net change original -> live: one patch pair, however long the drag.
    Result<void> Merge(Unique<ICommand> next) override {
        const auto &n = static_cast<const EditCmd &>(*next);
        Json f = Concat(fwd, n.fwd), b = Concat(n.back, back); // always valid: patches compose by concatenation
        try {
            const Json &live = w.Device(device).document;
            const Json original = live.patch(b);
            if(original.patch(f) == live) {f = Json::diff(original, live);b = Json::diff(live, original);} // compact (net change only)
        } catch(const std::exception &) {}
        fwd = std::move(f);back = std::move(b);Measure();
        return {};
    }
};

// ---- run history --------------------------------------------------------------------------------------------------------------------------------------
struct RunsStep {
    struct Ins {std::size_t index;Json entry;Ref<const ResultBase> result;};
    std::vector<std::string> erase;
    std::vector<Ins> insert;                           // ascending index
    std::vector<std::pair<std::string, Json>> patch;   // run_id, json patch
    bool Empty() const {return erase.empty() && insert.empty() && patch.empty();}
    std::size_t Bytes() const {
        std::size_t n = 0;
        for(const auto &i : insert) n += JsonBytes(i.entry) + sizeof(Ins);
        for(const auto &p : patch) n += JsonBytes(p.second) + p.first.capacity();
        return n + erase.size() * sizeof(std::string);
    }
    void Apply(Json &history, RunMap &runs) const {
        for(const auto &id : erase) {
            std::erase_if(history.get_ref<Json::array_t &>(), [&](const Json &r) {return r.at("run_id") == id;});
            runs.erase(id);
        }
        for(const auto &i : insert) {
            history.insert(history.begin() + std::ptrdiff_t(std::min(i.index, history.size())), i.entry);
            if(i.result) runs[i.entry.at("run_id").get<std::string>()] = i.result;
        }
        for(const auto &[id, p] : patch) for(auto &r : history) if(r.at("run_id") == id) {r = r.patch(p);break;}
    }
};
}
struct DeviceDelta {
    std::string id, selBefore, selAfter, statusBefore, statusAfter;
    bool selChanged = false, statusChanged = false;
    RunsStep fwd, back;
    std::optional<std::pair<DeviceResults, DeviceResults>> live; // result/band/ac/batch before and after (clear only)
};
namespace {
Json WithoutBookkeeping(Json run) {run.erase("profiles_stored");return run;} // LimitProfiles flips this on its own; it is not part of the user's change
bool SameLive(const DeviceResults &a, const DeviceResults &b) {
    return a.result == b.result && a.band == b.band && a.ac == b.ac && a.batch == b.batch;
}
void ApplyDevice(Workflow &w, const DeviceDelta &dd, bool forward) {
    if(!w.HasDevice(dd.id)) return; // the device left the project; its own removal is further up the stack
    auto &d = w.Device(dd.id);
    const auto &step = forward ? dd.fwd : dd.back;
    if(!step.Empty()) {auto runs = CreateRef<RunMap>(*d.runs);step.Apply(d.history.mut(), *runs);d.runs = std::move(runs);}
    if(dd.live) {
        const auto &r = forward ? dd.live->second : dd.live->first;
        d.result.adopt(r.result);d.bandResult.adopt(r.band);d.acResults.clear();for(const auto &[k, v] : r.ac) d.acResults.set(k, v);d.batchResult.adopt(r.batch);
    }
    if(dd.selChanged) d.selectedRun = forward ? dd.selAfter : dd.selBefore;
    if(dd.statusChanged) d.status = forward ? dd.statusAfter : dd.statusBefore;
    w.Emit(HistoryChanged(d.id));w.Emit(SelectionChanged(d.id));
}
void ApplyAll(Workflow &w, const Deltas &deltas, bool forward) {for(const auto &dd : deltas.items) ApplyDevice(w, *dd, forward);}
std::size_t Bytes(const Deltas &deltas) {
    std::size_t n = 0;
    for(const auto &dd : deltas.items) n += sizeof(DeviceDelta) + dd->fwd.Bytes() + dd->back.Bytes();
    return n;
}
class HistoryCmd final : public ICommand {
    Workflow &w;std::string text;Deltas deltas;
public:
    HistoryCmd(Workflow &w, std::string text, Deltas d) : w(w), text(std::move(text)), deltas(std::move(d)) {}
    std::string Description() const override {return text;}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {ApplyAll(w, deltas, true);ProjectChanged(w);return {};}
    Result<void> Undo(CommandContext &) override {ApplyAll(w, deltas, false);ProjectChanged(w);return {};}
    std::size_t ByteSize() const noexcept override {return sizeof(*this) + Bytes(deltas);}
};
class ProjectEditCmd final : public ICommand {
    Workflow &w;std::string nameBefore, nameAfter;Json uiFwd, uiBack;Deltas trimmed;
    Result<void> Apply(const std::string &name, const Json &ui, bool forward) {
        try {
            w.project.name = name;w.project.ui = w.project.ui.patch(ui);
            ApplyAll(w, trimmed, forward);ProjectChanged(w, true);return {};
        } catch(const std::exception &e) {return Failed(e);}
    }
public:
    ProjectEditCmd(Workflow &w, std::string nb, std::string na, Json f, Json b, Deltas t) : w(w), nameBefore(std::move(nb)), nameAfter(std::move(na)), uiFwd(std::move(f)), uiBack(std::move(b)), trimmed(std::move(t)) {}
    std::string Description() const override {return "Edit project";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(nameAfter, uiFwd, true);}
    Result<void> Undo(CommandContext &) override {return Apply(nameBefore, uiBack, false);}
    std::size_t ByteSize() const noexcept override {return sizeof(*this) + JsonBytes(uiFwd) + JsonBytes(uiBack) + Bytes(trimmed);}
};

// ---- devices ------------------------------------------------------------------------------------------------------------------------------------------
// Add and remove share one shape: the device (kept alive here, with its own undo stack and results), where it sits, who was active.
class DeviceListCmd final : public ICommand {
    Workflow &w;Ref<DeviceState> dev;std::size_t index;std::string activeBefore, activeAfter, text;bool compared, addsOnExecute;
    void Insert(const std::string &active) {
        auto &v = w.project.devices;
        v.insert(v.begin() + std::ptrdiff_t(std::min(index, v.size())), dev);
        w.project.active_device = active;if(compared) w.project.compare_set.insert(dev->id);
    }
    void Remove(const std::string &active) {
        w.DropDeviceLane(dev->id); // a calculation of a device that leaves is cancelled and dropped
        std::erase_if(w.project.devices, [&](const Ref<DeviceState> &d) {return d->id == dev->id;});
        w.project.compare_set.erase(dev->id);w.project.active_device = active;
    }
public:
    DeviceListCmd(Workflow &w, Ref<DeviceState> d, std::size_t i, std::string before, std::string after, std::string text, bool compared, bool adds)
        : w(w), dev(std::move(d)), index(i), activeBefore(std::move(before)), activeAfter(std::move(after)), text(std::move(text)), compared(compared), addsOnExecute(adds) {}
    std::string Description() const override {return text;}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {
        if(addsOnExecute) Insert(activeAfter);else Remove(activeAfter);
        ProjectChanged(w, true);return {};
    }
    Result<void> Undo(CommandContext &) override {
        if(addsOnExecute) Remove(activeBefore);else Insert(activeBefore);
        ProjectChanged(w, true);return {};
    }
    std::size_t ByteSize() const noexcept override {return sizeof(*this);} // the device's own data is shared state, not a delta
};
class DeviceUpdateCmd final : public ICommand {
    Workflow &w;std::string device;DeviceMeta before, after;
    Result<void> Apply(const DeviceMeta &m) {
        if(!w.HasDevice(device)) return {};
        auto &d = w.Device(device);d.name = m.name;d.description = m.description;d.colour = m.colour;d.metadataDirty = true;
        w.project.compare_set.erase(device);if(m.compare) w.project.compare_set.insert(device);
        ProjectChanged(w, true);return {};
    }
public:
    DeviceUpdateCmd(Workflow &w, std::string d, DeviceMeta b, DeviceMeta a) : w(w), device(std::move(d)), before(std::move(b)), after(std::move(a)) {}
    std::string Description() const override {return "Update device";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(after);}
    Result<void> Undo(CommandContext &) override {return Apply(before);}
    std::size_t ByteSize() const noexcept override {return sizeof(*this) + 2 * (JsonBytes(before.colour) + before.name.capacity() + before.description.capacity() + after.name.capacity() + after.description.capacity());}
};
class DeviceMoveCmd final : public ICommand {
    Workflow &w;std::string device;std::size_t from, to;
    Result<void> Apply(std::size_t index) {
        auto &v = w.project.devices;
        const auto at = std::find_if(v.begin(), v.end(), [&](const Ref<DeviceState> &d) {return d->id == device;});
        if(at == v.end()) return {};
        auto d = *at;v.erase(at);v.insert(v.begin() + std::ptrdiff_t(std::min(index, v.size())), std::move(d));
        ProjectChanged(w, true);return {};
    }
public:
    DeviceMoveCmd(Workflow &w, std::string d, std::size_t f, std::size_t t) : w(w), device(std::move(d)), from(f), to(t) {}
    std::string Description() const override {return "Move device";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(to);}
    Result<void> Undo(CommandContext &) override {return Apply(from);}
    std::size_t ByteSize() const noexcept override {return sizeof(*this) + device.capacity();}
};
}
Unique<ICommand> Edit(Workflow &w, std::string device, const Json &before, const Json &after, std::string gesture) {
    return CreateUnique<EditCmd>(w, std::move(device), Json::diff(before, after), Json::diff(after, before), std::move(gesture));
}
Deltas Compare(const std::vector<DeviceBefore> &before, Workflow &w, bool withLiveResults) {
    Deltas out;
    for(const auto &b : before) {
        if(!w.HasDevice(b.id)) continue;
        auto &d = w.Device(b.id);
        auto dd = std::make_shared<DeviceDelta>();dd->id = b.id;
        const Json &hb = *b.history, &ha = *d.history;
        std::map<std::string, std::size_t> inAfter, inBefore;
        for(std::size_t j = 0; j < ha.size(); ++j) inAfter[ha[j].at("run_id").get<std::string>()] = j;
        for(std::size_t i = 0; i < hb.size(); ++i) inBefore[hb[i].at("run_id").get<std::string>()] = i;
        for(std::size_t i = 0; i < hb.size(); ++i) {
            const auto id = hb[i].at("run_id").get<std::string>();
            const auto it = inAfter.find(id);
            if(it == inAfter.end()) {
                dd->fwd.erase.push_back(id);
                const auto r = b.results.runs->find(id);
                dd->back.insert.push_back({i, hb[i], r == b.results.runs->end() ? Ref<const ResultBase>{} : r->second});
            } else if(&hb[i] != &ha[it->second]) {
                const auto x = WithoutBookkeeping(hb[i]), y = WithoutBookkeeping(ha[it->second]);
                if(x != y) {dd->fwd.patch.emplace_back(id, Json::diff(x, y));dd->back.patch.emplace_back(id, Json::diff(y, x));}
            }
        }
        for(std::size_t j = 0; j < ha.size(); ++j) {
            const auto id = ha[j].at("run_id").get<std::string>();
            if(inBefore.contains(id)) continue;
            dd->back.erase.push_back(id);dd->fwd.insert.push_back({j, ha[j], d.RunResult(id)});
        }
        dd->selChanged = b.selected != d.selectedRun;dd->selBefore = b.selected;dd->selAfter = d.selectedRun;
        dd->statusChanged = b.status != d.status;dd->statusBefore = b.status;dd->statusAfter = d.status;
        const auto now = d.Results();
        if(withLiveResults && !SameLive(b.results, now)) dd->live = std::make_pair(b.results, now);
        if(dd->fwd.Empty() && dd->back.Empty() && !dd->selChanged && !dd->statusChanged && !dd->live) continue;
        out.items.push_back(std::move(dd));
    }
    return out;
}
Unique<ICommand> History(Workflow &w, std::string description, Deltas deltas) {return CreateUnique<HistoryCmd>(w, std::move(description), std::move(deltas));}
Unique<ICommand> ProjectEdit(Workflow &w, std::string nb, std::string na, const Json &uiBefore, const Json &uiAfter, Deltas trimmed) {
    return CreateUnique<ProjectEditCmd>(w, std::move(nb), std::move(na), Json::diff(uiBefore, uiAfter), Json::diff(uiAfter, uiBefore), std::move(trimmed));
}
Unique<ICommand> DeviceAdded(Workflow &w, Ref<DeviceState> device, std::size_t index, std::string activeBefore, std::string description) {
    auto after = device->id;
    return CreateUnique<DeviceListCmd>(w, std::move(device), index, std::move(activeBefore), std::move(after), std::move(description), true, true);
}
Unique<ICommand> DeviceRemoved(Workflow &w, Ref<DeviceState> device, std::size_t index, std::string activeBefore, std::string activeAfter, bool wasCompared) {
    return CreateUnique<DeviceListCmd>(w, std::move(device), index, std::move(activeBefore), std::move(activeAfter), "Delete device", wasCompared, false);
}
Unique<ICommand> DeviceUpdated(Workflow &w, std::string device, DeviceMeta before, DeviceMeta after) {return CreateUnique<DeviceUpdateCmd>(w, std::move(device), std::move(before), std::move(after));}
Unique<ICommand> DeviceMoved(Workflow &w, std::string device, std::size_t from, std::size_t to) {return CreateUnique<DeviceMoveCmd>(w, std::move(device), from, to);}
}
