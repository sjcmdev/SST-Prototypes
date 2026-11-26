#pragma once
#include "Library/Library.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <set>

namespace SST::ScapsLab::Library {
enum class Action { Inspect, CreateLibrary, Mount, PreviewImport, CommitImport, Create, Revise, Derive, SetLocked, Save, Export, Apply, Refresh, WhereUsed, Undo, Redo };
struct Request {
    Action action=Action::Inspect;
    std::string libraryId="user",itemId,sourceLibraryId,newId,kind="material",profileTarget="Na_m3";
    std::filesystem::path path;
    Assets::Scope scope=Assets::Scope::User;
    Assets::Metadata metadata;
    std::shared_ptr<const Payload> payload;
    std::uint32_t version=0,expectedVersion=0;
    Target target;
    Assets::UsePolicy policy=Assets::UsePolicy::Copy;
    bool locked=false,readOnly=false;
};
struct Browser {
    std::string library="sst-examples",item="si-default",search,collection,tag,compareItem;
    std::vector<std::string> favorites; // library/item identities
    bool grid=false,favoritesOnly=false;
    bool operator==(const Browser&) const = default;
};
class Task;
struct EditBuffer;
struct DeviceUpdate {std::string device;std::shared_ptr<const ProjectDraft> before,after;};
struct ItemDraft {Assets::Metadata metadata;std::shared_ptr<const Payload> payload;std::uint32_t sourceVersion=1;};
using DraftMap=std::map<std::string,ItemDraft>;
bool NeedsPersistence(const State&,const Browser&,const DraftMap*);
struct Session {
    std::shared_ptr<const State> state=Builtins();
    std::shared_ptr<const Catalog> preview;
    Browser browser;
    std::shared_ptr<const DraftMap> drafts=std::make_shared<const DraftMap>();
    std::shared_ptr<Task> task;
    JobId job=0;
    bool cancellable=false;
    UndoStack undo;
    std::set<std::string> dirty;
    std::string message,errorCode;
    std::vector<Assets::Usage> whereUsed;
    RevisionRef selected;
    std::shared_ptr<const ProjectDraft> editBase;
    std::vector<DeviceUpdate> updates;
    std::weak_ptr<EditBuffer> activeEditor;
    std::string activeEditorKey;
    std::uint32_t editorWindow=0;
    bool Busy() const {return job!=0;}
    void Submit(JobSystem&,std::shared_ptr<const Request>,std::shared_ptr<const ProjectDraft>,std::string device,std::vector<Assets::Usage>,std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>> devices={});
    // Returns an optional device edit after a job has completed; the owner routes it through its normal edit lane.
    std::pair<std::string,std::shared_ptr<const ProjectDraft>> Poll(JobSystem&);
};
Target TargetForUse(const ProjectDraft&,const Assets::Use&);
std::shared_ptr<const ProjectDraft> Refresh(const ProjectDraft&,const State&,bool linkedOnly=true);
}
