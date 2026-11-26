#include "Workflow.hpp"
#include "Commands/Commands.hpp"

namespace SST::ScapsLab {
void Workflow::LibraryTyped(std::shared_ptr<const Library::Request> request) {
    if(ReplacingProject())throw Assets::Error(Assets::ErrorCode::Conflict,"Wait for the project to finish loading");
    std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>> devices;
    for(const auto& d:project.devices)devices.emplace_back(d->id,d->draft);
    const bool applying=request->action==Library::Action::Apply || request->action==Library::Action::Refresh;
    if(applying && EditDraft())throw Assets::Error(Assets::ErrorCode::Conflict,"Wait for the pending device edit");
    const auto device=request->target.deviceId.empty()?project.active_device:request->target.deviceId;RequireDevice(device);
    if(applying && DeviceRunning(device))throw Assets::Error(Assets::ErrorCode::Conflict,"Wait for the destination device operation");
    const auto before=library.state;
    auto known=Library::Usages(path,devices);library.Submit(*jobs,std::move(request),Device(device).draft,device,std::move(known),std::move(devices));
    if(!library.Busy() && before!=library.state)RefreshLinkedAssets();
    if(!library.Busy() && !library.dirty.empty()){projectDirty=true;RefreshDirty();}
}
void Workflow::RefreshLinkedAssets() {
    for(const auto& device:project.devices){
        if(!device->draft || std::none_of(device->draft->assetUses.begin(),device->draft->assetUses.end(),[](const auto& use){return use.policy==Assets::UsePolicy::Linked;}))continue;
        try {auto updated=Library::Refresh(*device->draft,*library.state,true);if(CompareDrafts(*device->draft,*updated).Empty())continue;if(DeviceRunning(device->id)){library.message="Linked source changed; refresh the busy device after its operation";continue;}StartOperation({{"operation","edit"}},device->id,std::move(updated));}
        catch(const std::exception& e){library.message=std::string("Linked asset needs review: ")+e.what();status=library.message;}
    }
}
void Workflow::PollLibrary() {
    const bool running=library.Busy();auto [device,draft]=library.Poll(*jobs);if(!running || library.Busy())return;
    status=library.message;
    if(!library.errorCode.empty()) {response={{"ok",false},{"error",{{"code",library.errorCode},{"message",library.message}}}};return;}
    if(draft) {
        if(!HasDevice(device) || DeviceRunning(device) || Device(device).draft!=library.editBase){library.errorCode="conflict";library.message="Destination changed while applying the asset; apply it again";status=library.message;response={{"ok",false},{"error",{{"code",library.errorCode},{"message",library.message}}}};return;}
        // The normal edit job owns validation, document events and the delta undo step.
        StartOperation({{"operation","edit"}},device,std::move(draft));
    }else response={{"ok",true},{"error",nullptr}};
    for(const auto& update:library.updates) {
        if(HasDevice(update.device) && !DeviceRunning(update.device) && Device(update.device).draft==update.before)StartOperation({{"operation","edit"}},update.device,update.after);
        else {library.message="Library updated; a busy or edited device needs Refresh linked assets";status=library.message;}
    }
    library.updates.clear();
    if(!library.dirty.empty())projectDirty=true;RefreshDirty();
}
}
