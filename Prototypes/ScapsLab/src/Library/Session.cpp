#include "Library/Session.hpp"
#include "Core/JobSystem/JobContext.hpp"

namespace SST::ScapsLab::Library {
bool NeedsPersistence(const State& state,const Browser& browser,const DraftMap* drafts) {
    if(browser!=Browser{} || (drafts && !drafts->empty()))return true;
    for(const auto& [id,mounted]:state.libraries)if(mounted.catalog.scope!=Assets::Scope::Builtin && (!mounted.catalog.Items().empty() || (id!="user" && id!="project") || !mounted.source.path.empty()))return true;
    return false;
}
namespace {
class CatalogEdit final:public ICommand {
    Session& session;std::shared_ptr<const State> before,after;std::string libraryId;
    void Restore(const std::shared_ptr<const State>& snapshot) {
        auto next=std::make_shared<State>(*snapshot);
        for(auto& [id,mounted]:next->libraries)if(const auto current=session.state->libraries.find(id);current!=session.state->libraries.end())mounted.source=current->second.source;
        session.state=std::move(next);session.dirty.insert(libraryId);
    }
public:
    CatalogEdit(Session& s,std::shared_ptr<const State> a,std::shared_ptr<const State> b,std::string id):session(s),before(std::move(a)),after(std::move(b)),libraryId(std::move(id)) {}
    Result<void> Execute(CommandContext&) override {Restore(after);return {};}
    Result<void> Undo(CommandContext&) override {Restore(before);return {};}
    std::string Description() const override {return "Edit library";}
    bool IsUndoable() const noexcept override {return true;}
    std::size_t ByteSize() const noexcept override {return sizeof(*this);}
};
Mounted& Get(State& s,const std::string& id) {const auto found=s.libraries.find(id);if(found==s.libraries.end())throw Assets::Error(Assets::ErrorCode::Missing,"Unknown library: "+id);return found->second;}
}
class Task final:public IJob {
public:
    std::shared_ptr<const Request> request;
    std::shared_ptr<const State> before,after;
    std::shared_ptr<const ProjectDraft> input,edited;
    std::shared_ptr<const Catalog> previewIn,previewOut;
    std::vector<Assets::Usage> known,used;
    std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>> devices;
    std::vector<DeviceUpdate> updates;
    RevisionRef selected;
    std::string device,message,errorCode;
    bool mutated=false,saved=false,wroteFile=false;
    std::string GetName() const override {return "Asset library";}
    std::string GetType() const override {return "scapslab.library";}
    void Execute(JobContext& ctx) override try {
        ctx.ThrowIfCancellationRequested();ctx.SetStage("Asset library");const auto& r=*request;
        auto next=std::make_shared<State>(*before);
        if(r.action==Action::Inspect){const auto& c=Get(*next,r.libraryId).catalog;if(!r.itemId.empty())selected=c.Find(r.itemId,r.version);message="Library inspected";}
        else if(r.action==Action::CreateLibrary){Assets::Require(Assets::ValidId(r.libraryId),"Invalid library ID");Assets::Require(r.scope!=Assets::Scope::Builtin,"Built-in scope is reserved for shipped content");if(next->libraries.contains(r.libraryId))throw Assets::Error(Assets::ErrorCode::Conflict,"Library already exists");next->libraries.emplace(r.libraryId,Mounted{Empty(r.libraryId,r.metadata.name,r.scope),{}});mutated=true;}
        else if(r.action==Action::Mount){auto loaded=Load(r.path,r.readOnly);const auto id=loaded.catalog.id;if(const auto old=next->libraries.find(id);old!=next->libraries.end()){if(old->second.source.path.empty() || std::filesystem::absolute(old->second.source.path).lexically_normal()!=std::filesystem::absolute(r.path).lexically_normal())throw Assets::Error(Assets::ErrorCode::Conflict,"A different library with this ID is already mounted");if(old->second.catalog.ReadOnly() && !loaded.catalog.ReadOnly())loaded.catalog.readOnly=true;if(old->second.catalog.generation>loaded.catalog.generation)throw Assets::Error(Assets::ErrorCode::Conflict,"Mounted library has unsaved changes; save or undo before reloading");old->second=std::move(loaded);}else next->libraries.emplace(id,std::move(loaded));}
        else if(r.action==Action::PreviewImport){
            const auto extension=Assets::Fold(r.path.extension().string());
            Catalog imported=Empty("imported","Import preview");
            if(extension==".def" || extension==".material")imported=ImportScaps(r.path);
            else if(extension==".csv" || extension==".tsv"){auto revision=ImportCsv(r.path,r.kind,r.profileTarget);imported.Restore({revision});}
            else imported=Load(r.path).catalog;
            previewOut=std::make_shared<const Catalog>(std::move(imported));message="Import preview ready; confirm the destination";
        }
        else if(r.action==Action::CommitImport){Assets::Require(bool(previewIn),"Preview an import first");auto& c=Get(*next,r.libraryId).catalog;auto candidate=c;for(const auto& [id,history]:previewIn->Items()){auto m=history.back()->meta;m.locked=false;if(!r.newId.empty()){Assets::Require(previewIn->Items().size()==1,"An ID override requires a single imported item");m.id=r.newId;}candidate.Create(std::move(m),history.back()->payload);}c=std::move(candidate);mutated=true;}
        else if(r.action==Action::Create){Assets::Require(bool(r.payload),"Missing typed payload");Revision candidate{r.metadata,1,r.payload};ValidateRevision(candidate);selected=Get(*next,r.libraryId).catalog.Create(r.metadata,r.payload);mutated=true;}
        else if(r.action==Action::Revise){Assets::Require(bool(r.payload),"Missing typed payload");auto& catalog=Get(*next,r.libraryId).catalog;Assets::Require(catalog.Find(r.metadata.id)->payload->index()==r.payload->index(),"An asset revision must retain its kind");Revision candidate{r.metadata,r.expectedVersion+1,r.payload};ValidateRevision(candidate);selected=catalog.Revise(r.metadata,r.payload,r.expectedVersion);mutated=true;}
        else if(r.action==Action::Derive){const auto source=Get(*next,r.sourceLibraryId).catalog.Find(r.itemId,r.version);auto m=r.metadata;if(m.name.empty()){m=source->meta;m.id=r.newId;m.name=source->meta.name+" copy";m.changeNote="Derived from "+r.sourceLibraryId+"/"+r.itemId;}selected=Get(*next,r.libraryId).catalog.Derive(r.sourceLibraryId,*source,std::move(m));mutated=true;}
        else if(r.action==Action::SetLocked){Get(*next,r.libraryId).catalog.SetLocked(r.itemId,r.locked);mutated=true;}
        else if(r.action==Action::Save){auto& mounted=Get(*next,r.libraryId);const auto path=r.path.empty()?mounted.source.path:r.path;const bool same=!mounted.source.path.empty() && std::filesystem::absolute(path).lexically_normal()==std::filesystem::absolute(mounted.source.path).lexically_normal();Save(mounted.catalog,path,same?std::optional(mounted.source.fingerprint):std::nullopt);mounted.source=Load(path).source;saved=wroteFile=true;message="Library saved";}
        else if(r.action==Action::Export){const auto item=Get(*next,r.libraryId).catalog.Find(r.itemId,r.version);const auto extension=Assets::Fold(r.path.extension().string());if(extension==".def" || extension==".material")ExportScaps(*item->payload,r.path);else if(extension==".csv" || extension==".tsv")ExportCsv(*item->payload,r.path);else {auto c=Empty("exported","Exported item");auto m=item->meta;m.locked=false;c.Create(std::move(m),item->payload);Save(c,r.path);}message="Asset exported";}
        else if(r.action==Action::Apply){Assets::Require(bool(input),"No destination device");const auto item=Get(*next,r.libraryId).catalog.Find(r.itemId,r.version);edited=Apply(*input,*item,r.libraryId,r.target,r.policy);message="Asset applied";}
        else if(r.action==Action::Refresh){Assets::Require(bool(input),"No destination device");edited=Refresh(*input,*next,false);message="Asset updates applied";}
        else if(r.action==Action::WhereUsed){if(!r.path.empty()){auto disk=ScanProjects(r.path);known.insert(known.end(),std::make_move_iterator(disk.begin()),std::make_move_iterator(disk.end()));}used=Assets::WhereUsed(known,{r.libraryId,r.itemId,r.version});std::set<std::tuple<std::string,std::string,std::string>> seen;std::erase_if(used,[&](const auto& use){return !seen.emplace(use.project,use.device,use.use.target).second;});message=r.path.empty()?"Where-used: current project's devices":"Where-used: current project and selected project folder";}
        if(mutated || r.action==Action::Mount)for(const auto& [id,d]:devices) {
            if(!d || std::none_of(d->assetUses.begin(),d->assetUses.end(),[](const auto& use){return use.policy==Assets::UsePolicy::Linked;}))continue;
            auto refreshed=Refresh(*d,*next,true);if(!CompareDrafts(*d,*refreshed).Empty())updates.push_back({id,d,std::move(refreshed)});
        }
        if(r.action==Action::Export)wroteFile=true;
        if(!wroteFile)ctx.ThrowIfCancellationRequested();after=std::move(next);if(message.empty())message="Library updated";
    }catch(const JobCancelledException&){throw;}
     catch(const Assets::Error& e){message=e.what();errorCode=Assets::CodeName(e.code);}
     catch(const std::exception& e){message=e.what();errorCode="invalid_asset";}
};
void Session::Submit(JobSystem& jobs,std::shared_ptr<const Request> request,std::shared_ptr<const ProjectDraft> draft,std::string device,std::vector<Assets::Usage> known,std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>> devices) {
    if(Busy())throw Assets::Error(Assets::ErrorCode::Conflict,"An asset operation is still running");errorCode.clear();message.clear();
    cancellable=request->action!=Action::Save && request->action!=Action::Export;
    if(request->action==Action::Undo || request->action==Action::Redo){const auto result=request->action==Action::Undo?undo.Undo():undo.Redo();if(!result)throw Assets::Error(Assets::ErrorCode::Invalid,result.Error().technicalDetails);selected.reset();message="Library edit restored";return;}
    task=std::make_shared<Task>();task->before=state;task->request=std::move(request);task->input=std::move(draft);task->device=std::move(device);task->known=std::move(known);task->devices=std::move(devices);task->previewIn=preview;job=jobs.Submit(task);
}
std::pair<std::string,std::shared_ptr<const ProjectDraft>> Session::Poll(JobSystem& jobs) {
    if(!job)return {};const auto status=jobs.TryGetStatus(job);if(!status || *status==JobStatus::Queued || *status==JobStatus::Running)return {};
    const auto snapshot=jobs.GetJob(job);auto done=std::move(task);job=0;errorCode=done->errorCode;message=done->message;
    if(*status!=JobStatus::Completed && !(done->wroteFile && done->after)){errorCode=*status==JobStatus::Cancelled?"cancelled":"operation_failed";message=*status==JobStatus::Cancelled?"Asset operation cancelled":snapshot?snapshot->errorMessage:"Asset operation failed";return {};}
    if(!errorCode.empty())return {};
    if(done->after)state=done->after;if(done->previewOut)preview=done->previewOut;if(done->selected)selected=done->selected;
    if(done->request->action==Action::WhereUsed)whereUsed=std::move(done->used);
    if(done->request->action==Action::CommitImport)preview.reset();
    if(done->mutated){dirty.insert(done->request->libraryId);undo.SetLimits(100,8u<<20);(void)undo.PushExecuted(std::make_unique<CatalogEdit>(*this,done->before,state,done->request->libraryId));}
    if(done->saved)dirty.erase(done->request->libraryId);
    updates=std::move(done->updates);
    editBase=std::move(done->input);return {std::move(done->device),std::move(done->edited)};
}
Target TargetForUse(const ProjectDraft& draft,const Assets::Use& use) {
    Target t;const auto& s=use.target;
    if(s.starts_with("layer:")){const auto end=s.find(':',6);Assets::Require(end!=s.npos,"Invalid asset layer target");t.layerId=s.substr(6,end-6);}
    else if(s.starts_with("interface:")){const auto end=s.find(':',10);const auto id=s.substr(10,end==s.npos?s.npos:end-10);const auto found=std::find_if(draft.layers.begin(),draft.layers.end(),[&](const auto& l){return l.id==id;});Assets::Require(found!=draft.layers.end(),"Asset interface destination was removed");t.interfaceAfter=int(found-draft.layers.begin());}
    else if(s.starts_with("defect:")){t.defectId=s.substr(7);for(const auto& layer:draft.layers)if(std::any_of(layer.defects.begin(),layer.defects.end(),[&](const auto& d){return d.id==t.defectId;}))t.layerId=layer.id;Assets::Require(!t.layerId.empty(),"Asset defect destination was removed");}
    else if(s=="contact:right")t.rightContact=true;
    return t;
}
std::shared_ptr<const ProjectDraft> Refresh(const ProjectDraft& original,const State& state,bool linkedOnly) {
    auto out=std::make_shared<const ProjectDraft>(original);const auto uses=original.assetUses;
    for(const auto& use:uses){if(linkedOnly && use.policy!=Assets::UsePolicy::Linked)continue;const auto mounted=state.libraries.find(use.source.libraryId);if(mounted==state.libraries.end()){if(use.policy==Assets::UsePolicy::Linked)throw Assets::Error(Assets::ErrorCode::Missing,"Linked library is unavailable: "+use.source.libraryId);continue;}const auto latest=mounted->second.catalog.Find(use.source.itemId);const auto* material=std::get_if<Material>(latest->payload.get());const bool changed=use.policy==Assets::UsePolicy::Linked?latest->version!=use.source.version:UpdateAvailable(use,mounted->second.catalog);if(!changed && !(use.policy==Assets::UsePolicy::Linked && material && material->bandgapTemperature))continue;
        out=Apply(*out,*mounted->second.catalog.Find(use.source.itemId),use.source.libraryId,TargetForUse(*out,use),use.policy);
    }return out;
}
}
