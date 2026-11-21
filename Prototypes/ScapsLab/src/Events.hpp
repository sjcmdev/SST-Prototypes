#pragma once
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
// ScapsLab domain events. Published on the UI thread by Workflow (UpdateOperation/UpdateSave) after the state is mutated.
// Payloads are ids/revisions only; consumers read the new state from Workflow.
namespace SST::ScapsLab {
struct DocumentEdited final : BusEvent { // device document replaced (edit/undo/import...)
    std::string device; std::uint64_t revision=0;
    DocumentEdited(std::string d,std::uint64_t r):device(std::move(d)),revision(r) {}
};
struct RunCompleted final : BusEvent { // run/band/cv/cf/qe/batch finished with a valid result
    std::string device, runId, kind; std::uint64_t revision=0;
    RunCompleted(std::string d,std::string run,std::string k,std::uint64_t r):device(std::move(d)),runId(std::move(run)),kind(std::move(k)),revision(r) {}
};
struct RunFailed final : BusEvent {
    std::string device, kind, message; bool cancelled=false;
    RunFailed(std::string d,std::string k,std::string m,bool c):device(std::move(d)),kind(std::move(k)),message(std::move(m)),cancelled(c) {}
};
struct HistoryChanged final : BusEvent {std::string device; explicit HistoryChanged(std::string d):device(std::move(d)) {}};
struct SelectionChanged final : BusEvent {std::string device; explicit SelectionChanged(std::string d):device(std::move(d)) {}};
struct DeviceListChanged final : BusEvent {};
struct ProjectLoaded final : BusEvent {std::string path; explicit ProjectLoaded(std::string p):path(std::move(p)) {}};
struct ProjectSaved final : BusEvent {std::string path; bool autosave=false; ProjectSaved(std::string p,bool a):path(std::move(p)),autosave(a) {}};
struct ProjectSaveFailed final : BusEvent {std::string path, message; bool autosave=false; ProjectSaveFailed(std::string p,std::string m,bool a):path(std::move(p)),message(std::move(m)),autosave(a) {}};
struct ProjectDirtyChanged final : BusEvent {bool dirty=false; explicit ProjectDirtyChanged(bool d):dirty(d) {}};
// S10: requests handled by Workflow::RegisterIo (IOLayer handlers; same pattern as CapLab/JFM). Panels/menus publish these instead of touching files.
struct ProjectSaveRequested final : BusEvent {std::string path; bool includeResults=true; ProjectSaveRequested(std::string p,bool r=true):path(std::move(p)),includeResults(r) {}};
struct ProjectOpenRequested final : BusEvent {std::string path; explicit ProjectOpenRequested(std::string p):path(std::move(p)) {}};
struct ProjectExportRequested final : BusEvent {std::string path,format; bool all=false; std::vector<std::string> runIds; ProjectExportRequested(std::string p,std::string f,bool a,std::vector<std::string> ids):path(std::move(p)),format(std::move(f)),all(a),runIds(std::move(ids)) {}};
}
