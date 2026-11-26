#include "Library/LibraryUi.hpp"
#include "Workflow.hpp"
#include "Panel.hpp"
#include "PlotUi.hpp"
#include "Commands/Commands.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <implot.h>
#include <imgui_internal.h>
#include <cstdio>

namespace SST::ScapsLab::Library {
namespace {
namespace widgets=sst::ui::widgets;
bool Text(const char* label,std::string& value,const char* tip="") {return widgets::Text(label,&value,0,tip);}
bool Check(const char* label,bool& value,const char* tip) {widgets::BeginLabelledRow(label);const bool changed=ImGui::Checkbox("##value",&value);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tip);widgets::EndLabelledRow();return changed;}
void Help(const std::string& text) {if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text.c_str());}
bool Button(const char* label,bool enabled,const char* reason) {ImGui::BeginDisabled(!enabled);const bool clicked=ImGui::Button(label);ImGui::EndDisabled();Help(reason);return clicked;}
template<class F> auto Dialog(Workflow& w,F open)->decltype(open()) {try{return open();}catch(const std::exception& e){w.library.message=e.what();return {};}}
void Send(Workflow& w,Request r) {const auto reply=w.Exec(Cmd::LibraryOperation::Make(std::move(r)));if(!reply.value("ok",false))w.library.message=reply.at("error").at("message").get<std::string>();}
class BufferEdit final:public ICommand {
    EditBuffer& buffer;EditValue before,after;
public:
    BufferEdit(EditBuffer& b,EditValue a,EditValue z):buffer(b),before(std::move(a)),after(std::move(z)) {}
    Result<void> Execute(CommandContext&) override {buffer.value=after;buffer.dirty=true;buffer.Rebuild();return {};}
    Result<void> Undo(CommandContext&) override {buffer.value=before;buffer.dirty=true;buffer.Rebuild();return {};}
    std::string Description() const override {return "Edit asset parameter";}
    bool IsUndoable() const noexcept override {return true;}
    std::size_t ByteSize() const noexcept override {return sizeof(*this);}
};
RevisionRef Find(const State& s,const std::string& library,const std::string& item,std::uint32_t version=0){const auto c=s.libraries.find(library);if(c==s.libraries.end() || !c->second.catalog.Items().contains(item))return {};return c->second.catalog.Find(item,version);}
void PlotLine(const char* name,const Preview& p) {if(p.x.size()==1)ImPlot::PlotScatter(name,p.x.data(),p.y.data(),1);else if(!p.x.empty())ImPlot::PlotLine(name,p.x.data(),p.y.data(),int(p.x.size()));if(p.y2.size()==p.x.size() && !p.y2.empty())ImPlot::PlotLine((std::string(name)+" / "+p.secondLabel).c_str(),p.x.data(),p.y2.data(),int(p.x.size()));}
bool Combo(const char* label,std::string& value,const std::vector<std::string>& choices,const char* tooltip) {
    bool changed=false;widgets::BeginLabelledRow(label);if(ImGui::BeginCombo("##value",value.c_str())){for(std::size_t i=0;i<choices.size();++i){const auto& c=choices[i];ImGui::PushID(int(i));if(ImGui::Selectable(c.empty()?"(any)":c.c_str(),c==value)){value=c;changed=true;}ImGui::PopID();}ImGui::EndCombo();}Help(tooltip);widgets::EndLabelledRow();return changed;
}
std::vector<std::string> WritableLibraries(const State& s){std::vector<std::string> out;for(const auto& [id,m]:s.libraries)if(!m.catalog.ReadOnly())out.push_back(id);return out;}
std::string LabelPath(const std::string& path){const auto last=path.find_last_of('.');return path.substr(last==path.npos?0:last+1);}
}
void EditBuffer::Reset(RevisionRef r) {source=std::move(r);value={source->meta,source->payload};value.metadata.changeNote="Edited parameters";dirty=false;gesture.reset();undo.Clear();Rebuild();reference=preview;}
void EditBuffer::Rebuild() {
    try {carrier=Carrier(*value.payload);numbers=EditableNumbers(*value.payload,&carrier);auto candidate=MakePreview(*value.payload);preview=std::move(candidate);++previewBuilds;ValidateRevision({value.metadata,source->version,value.payload});diagnostic.clear();}
    catch(const std::exception& e){diagnostic=e.what();}
}
namespace {
std::shared_ptr<EditBuffer> FocusedBuffer(const Session& session) {
    if(!GImGui || !GImGui->NavWindow || GImGui->NavWindow->RootWindow->ID!=session.editorWindow)return {};
    return session.activeEditor.lock();
}
}
std::optional<bool> EditorCanStep(const Session& session,bool undo) {
    const auto buffer=FocusedBuffer(session);if(!buffer)return {};
    return undo?(buffer->undo.CanUndo() || session.undo.CanUndo()):(buffer->undo.CanRedo() || session.undo.CanRedo());
}
std::optional<Result<void>> EditorStep(Session& session,bool undo) {
    const auto buffer=FocusedBuffer(session);if(!buffer)return {};
    const bool item=undo?buffer->undo.CanUndo():buffer->undo.CanRedo();
    auto result=item?(undo?buffer->undo.Undo():buffer->undo.Redo()):(undo?session.undo.Undo():session.undo.Redo());
    if(item && result){auto drafts=std::make_shared<DraftMap>(*session.drafts);(*drafts)[session.activeEditorKey]={buffer->value.metadata,buffer->value.payload,buffer->source->version};session.drafts=std::move(drafts);}
    return result;
}
unsigned Editor::PreviewBuilds() const {unsigned n=0;for(const auto& [key,b]:buffers)n+=b->previewBuilds;return n;}
void Editor::PreviewPlot(Workflow& w,const EditBuffer& b,const std::string& key,const RevisionRef& compare) {
    if(BeginSavedPlot(w.plotSession,key.c_str(),ImVec2(-1,195))) {
        ImPlot::SetupAxes(b.preview.xLabel.c_str(),b.preview.yLabel.c_str());ImPlot::SetupLegend(ImPlotLocation_East,ImPlotLegendFlags_Outside);SetupSavedPlot(w.plotSession,key.c_str());
        if(b.gesture)PlotLine("Before",b.reference);
        PlotLine(b.preview.title.c_str(),b.preview);
        if(compare && compare->payload->index()==b.value.payload->index()){auto found=thumbnails.find(compare.get());if(found==thumbnails.end())found=thumbnails.emplace(compare.get(),MakePreview(*compare->payload)).first;PlotLine("Compare",found->second);}
        if(EndSavedPlot(w.plotSession,key.c_str())){w.projectDirty=true;w.RefreshDirty();}
    }
}
void Editor::Inspector(Workflow& w,EditBuffer& b,const std::string& library) {
    auto& s=w.library;const auto& mounted=s.state->libraries.at(library);const bool editable=!mounted.catalog.ReadOnly() && !b.source->meta.locked;const bool busy=s.Busy();
    ImGui::Text("%s  /  v%u  /  %s",b.value.metadata.name.c_str(),b.source->version,Kind(*b.value.payload));
    if(const auto* interface=std::get_if<Interface>(b.value.payload.get());interface && interface->referenceReviewRequired)ImGui::TextColored({1,.75f,.2f,1},"Derive and review the SCAPS interface energy reference before use.");
    if(!editable)ImGui::TextDisabled("Read-only. Derive into User or Project to edit.");
    RevisionRef compare;
    if(compareVersion>0){try{compare=mounted.catalog.Find(b.source->meta.id,std::uint32_t(compareVersion));}catch(const Assets::Error&){compareVersion=0;}}
    else if(!s.browser.compareItem.empty())compare=Find(*s.state,library,s.browser.compareItem);
    if(compare && compare->payload->index()!=b.value.payload->index())compare.reset();
    PreviewPlot(w,b,"Asset preview##library/"+library+"/"+b.source->meta.id,compare);
    ImGui::BeginChild("Item controls",{0,std::max(150.f,ImGui::GetContentRegionAvail().y-80)},ImGuiChildFlags_Borders);
    if(Button("Export table...",!busy && (std::holds_alternative<Profile>(*b.value.payload) || std::holds_alternative<Optical>(*b.value.payload) || std::holds_alternative<Illumination>(*b.value.payload)),"Available for profile, spectrum and absorption data")){
        if(auto path=Dialog(w,[]{return SaveFileDialog({"CSV table","csv"},"asset.csv");})){Request r;r.action=Action::Export;r.libraryId=library;r.itemId=b.source->meta.id;r.path=path->Native();Send(w,std::move(r));}
    }
    ImGui::SameLine();if(Button("Undo edit",b.undo.CanUndo() && !busy,"Undo one completed drag or field edit")){(void)b.undo.Undo();}
    ImGui::SameLine();if(Button("Redo edit",b.undo.CanRedo() && !busy,"Redo the last item edit")){(void)b.undo.Redo();}
    const auto before=b.value;bool changed=false;
    ImGui::BeginDisabled(!editable || busy);
    changed|=Text("Name",b.value.metadata.name,"Display name; stable ID is unchanged");
    changed|=Text("Folder",b.value.metadata.collection,"Relative collection path, for example Materials/Absorbers");
    if(ImGui::TreeNode("Tags")){for(std::size_t i=0;i<b.value.metadata.tags.size();++i){ImGui::PushID(int(i));changed|=Text("Tag",b.value.metadata.tags[i]);ImGui::PopID();}if(ImGui::Button("Add tag")){b.value.metadata.tags.push_back("new-tag");changed=true;}ImGui::TreePop();}
    changed|=widgets::MultilineText("Description",&b.value.metadata.description,ImVec2(-1,45),0,"Scientific context and limitations");
    if(ImGui::CollapsingHeader("Parameters",ImGuiTreeNodeFlags_DefaultOpen)) {
        std::optional<scaps_core::DeviceProject> carrier;bool numeric=false;
        for(auto& [path,value]:b.numbers){
            if(path.find("][")!=path.npos || path.find(".coordinate[")!=path.npos || path.find(".value[")!=path.npos)continue;
            const auto key=ParameterBoundsKey(path);const auto bounds=BoundsTable();const auto found=bounds->find(key);if(found==bounds->end())continue;
            ImGui::PushID(path.c_str());const auto label=LabelPath(path)+" ["+found->second.displayUnit+"]";widgets::BeginLabelledRow(label.c_str());
            double edited=value;const double displayScale=found->second.displayScale*ParameterToSI(key);
            if(DrawBoundValue("##asset-number",edited,key,displayScale,nullptr,label.c_str())){if(!carrier)carrier=b.carrier;SetProjectParameter(*carrier,path,edited);value=edited;numeric=true;}
            if(ImGui::IsItemHovered()){std::string citation=b.value.metadata.provenance.source=="paper"?"Not individually cited in this item; default/unspecified values must be verified.":b.value.metadata.provenance.citation+" "+b.value.metadata.provenance.locator;for(const auto& range:b.value.metadata.parameters)if(range.path==path){citation=range.citation;break;}ImGui::SetTooltip("%s\n%s\nCtrl+click: exact value",path.c_str(),citation.c_str());}
            widgets::EndLabelledRow();ImGui::PopID();
        }
        if(numeric){try {const auto* p=std::get_if<Profile>(b.value.payload.get());auto payload=FromCarrier(Kind(*b.value.payload),*carrier,b.value.payload.get(),p?p->target:"Na_m3");ValidatePayload(payload);b.value.payload=std::make_shared<const Payload>(std::move(payload));b.carrier=std::move(*carrier);changed=true;}catch(const std::exception& e){b.diagnostic=e.what();b.numbers=EditableNumbers(*b.value.payload);}}
        if(auto profile=std::get_if<Profile>(b.value.payload.get())) {
            std::string law=scaps_core::grading_law_name(profile->value.law);
            if(Combo("Profile law",law,{"uniform","linear","parabolic2","gaussian","exponential","erfc_diffusion","gauss_diffusion","table"},"Shape evaluated live; table import supplies sample coordinates")){try{auto v=*profile;v.value.law=scaps_core::grading_law_from_string(law);ValidatePayload(v);b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}catch(const std::exception& e){b.diagnostic=e.what();}}
        }
        if(auto defect=std::get_if<Defect>(b.value.payload.get())){
            auto v=*defect;std::string charge=scaps_core::to_string(v.value.charge_type);
            if(Combo("Charge type",charge,{"neutral","donor_like","acceptor_like"},"Charge state used in Poisson and SRH")){v.value.charge_type=scaps_core::trap_charge_type_from_string(charge);b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}
            v=*std::get_if<Defect>(b.value.payload.get());if(Combo("Distribution",v.value.energy_distribution,{"single","uniform","gaussian","cb_tail","vb_tail"},"Energy distribution; characteristic energy is in eV")){b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}
            v=*std::get_if<Defect>(b.value.payload.get());std::string reference=scaps_core::to_string(v.value.energy_reference);if(Combo("Energy reference",reference,{"relative_to_Ei","relative_to_Ec","relative_to_Ev"},"Explicit energy reference and offset sign convention")){v.value.energy_reference=scaps_core::defect_energy_reference_from_string(reference);b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}
        }
        if(const auto* illumination=std::get_if<Illumination>(b.value.payload.get())){auto v=*illumination;bool edited=Check("Illumination enabled",v.value.enabled,"Apply this illumination when the item is used");edited|=Combo("Illumination mode",v.value.mode,{"uniform","spectrum","monochromatic","generation_file"},"Spectrum/generation modes require embedded tables; import CSV before saving");if(edited){b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}}
        if(const auto* interface=std::get_if<Interface>(b.value.payload.get())){bool reviewed=!interface->referenceReviewRequired;if(Check("Energy reference reviewed",reviewed,"Confirm the numerical SCAPS interface reference codes and source convention; derive read-only items before editing")){auto v=*interface;v.referenceReviewRequired=!reviewed;b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}}
        if(const auto* optical=std::get_if<Optical>(b.value.payload.get())){auto v=*optical;if(Combo("Absorption model",v.absorption.model,{"none","file","sqrt","scaps"},"n,k remains source data; this solver applies explicit absorption coefficients")){b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}}
        if(const auto* material=std::get_if<Material>(b.value.payload.get())){
            auto v=*material;bool edited=false,law=bool(v.bandgapTemperature),mass=bool(v.effectiveMass);
            edited|=Combo("Absorption model",v.absorption.model,{"none","file","sqrt","scaps"},"File models retain embedded absorption data; analytic coefficients appear when their model is enabled");
            if(Check("Bandgap temperature law",law,"Varshni law is evaluated explicitly at the destination temperature when applied")){v.bandgapTemperature=law?std::optional<BandgapTemperature>({v.values.Eg_eV,0,300}):std::nullopt;edited=true;}
            const auto number=[&](const char* label,double& value,const char* key){widgets::BeginLabelledRow(label);const bool result=DrawBoundValue(label,value,key,1,nullptr,label);widgets::EndLabelledRow();return result;};
            if(v.bandgapTemperature){edited|=number("Eg(0) [eV]",v.bandgapTemperature->Eg0_eV,"Eg_eV");edited|=number("Varshni alpha [eV/K]",v.bandgapTemperature->alpha_eV_K,"varshni_alpha_eV_K");edited|=number("Varshni beta [K]",v.bandgapTemperature->beta_K,"temperature_K");}
            if(Check("Effective mass metadata",mass,"m*/m0 is retained as source metadata; supply Nc/Nv for solver use")){v.effectiveMass=mass?std::optional<EffectiveMass>({1,1}):std::nullopt;edited=true;}
            if(v.effectiveMass){edited|=number("Electron m*/m0 [1]",v.effectiveMass->electronRatio,"effective_mass_ratio");edited|=number("Hole m*/m0 [1]",v.effectiveMass->holeRatio,"effective_mass_ratio");}
            if(edited){b.value.payload=std::make_shared<const Payload>(std::move(v));changed=true;}
        }
        if(const auto* contact=std::get_if<Contact>(b.value.payload.get())){auto v=*contact;bool enabled=bool(v.reflectance);if(Check("Contact reflectance",enabled,"Apply an explicit front/back reflectance fraction")){v.reflectance=enabled?std::optional<double>(0):std::nullopt;b.value.payload=std::make_shared<const Payload>(v);changed=true;}if(v.reflectance){widgets::BeginLabelledRow("Reflectance [1]");if(DrawBoundValue("##reflectance",*v.reflectance,"front_transmission",1)){b.value.payload=std::make_shared<const Payload>(v);changed=true;}widgets::EndLabelledRow();}bool sn=std::isinf(v.value.Sn_mps),sp=std::isinf(v.value.Sp_mps);if(Check("Ideal electron SRV",sn,"Infinite surface recombination velocity; disable to enter a finite value [m/s]")){v.value.Sn_mps=sn?INFINITY:1e5;b.value.payload=std::make_shared<const Payload>(v);changed=true;}if(Check("Ideal hole SRV",sp,"Infinite surface recombination velocity; disable to enter a finite value [m/s]")){v.value.Sp_mps=sp?INFINITY:1e5;b.value.payload=std::make_shared<const Payload>(v);changed=true;}}
    }
    if(ImGui::CollapsingHeader("Provenance / validity / uncertainty")) {
        auto& p=b.value.metadata.provenance;changed|=Text("Source type",p.source,"paper, measurement, fit or example");changed|=Text("Citation",p.citation,"Full citation; do not label examples as verified literature");changed|=Text("DOI",p.doi,"Persistent source identifier");changed|=Text("Page / table",p.locator,"Location of the source values");changed|=Text("Author",p.author);changed|=Text("Date",p.date,"ISO date");changed|=Text("Fit run ID",p.runId,"Stable ID of the producing fit or simulation");changed|=Text("Source file",p.sourcePath,"Provenance path; no file is loaded by editing this field");
        const auto optional=[&](const char* label,std::optional<double>& value,const char* key){bool enabled=bool(value);if(Check(label,enabled,"Enable an explicit validity limit")){value=enabled?std::optional<double>(key==std::string_view("temperature_K")?300.:0.):std::nullopt;changed=true;}if(value){widgets::BeginLabelledRow("Value");double v=*value;if(DrawBoundValue(label,v,key,1)){*value=v;changed=true;}widgets::EndLabelledRow();}};
        ImGui::PushID("Tmin");optional("Minimum T [K]",b.value.metadata.validity.temperatureMin_K,"temperature_K");ImGui::PopID();ImGui::PushID("Tmax");optional("Maximum T [K]",b.value.metadata.validity.temperatureMax_K,"temperature_K");ImGui::PopID();
        ImGui::PushID("composition-min");optional("Minimum composition [1]",b.value.metadata.validity.compositionMin,"front_transmission");ImGui::PopID();ImGui::PushID("composition-max");optional("Maximum composition [1]",b.value.metadata.validity.compositionMax,"front_transmission");ImGui::PopID();
        if(ImGui::TreeNode("Parameter ranges")){
            std::vector<std::string> fields;for(const auto& [path,value]:b.numbers)fields.push_back(path);
            if(newRangePath.empty() && !fields.empty())newRangePath=fields.front();Combo("Parameter",newRangePath,fields,"Parameter receiving a cited range or uncertainty");
            if(ImGui::Button("Add range") && !newRangePath.empty()){if(std::none_of(b.value.metadata.parameters.begin(),b.value.metadata.parameters.end(),[&](const auto& p){return p.path==newRangePath;})){b.value.metadata.parameters.push_back({newRangePath,NativeUnit(newRangePath)});changed=true;}}
            int remove=-1;
            for(std::size_t i=0;i<b.value.metadata.parameters.size();++i){auto& p=b.value.metadata.parameters[i];ImGui::PushID(int(i));ImGui::TextWrapped("%s [%s]",p.path.c_str(),p.unit.c_str());changed|=Text("Citation",p.citation);
                for(auto pair:{std::pair{"Lower",&p.lower},std::pair{"Upper",&p.upper},std::pair{"Uncertainty",&p.uncertainty}}){ImGui::PushID(pair.first);bool enabled=bool(*pair.second);if(Check(pair.first,enabled,"Enable an explicit value in the parameter's stated unit")){*pair.second=enabled?std::optional<double>(b.numbers.contains(p.path)?b.numbers.at(p.path):0):std::nullopt;changed=true;}if(*pair.second){double v=**pair.second;if(widgets::LogValue((std::string("Value [")+p.unit+"]").c_str(),&v,-300,300,"Mantissa and exponent; Ctrl+click for an exact value in the stated unit")){*pair.second=v;changed=true;}}ImGui::PopID();}
                if(ImGui::SmallButton("Remove range"))remove=int(i);ImGui::PopID();
            }
            if(remove>=0){b.value.metadata.parameters.erase(b.value.metadata.parameters.begin()+remove);changed=true;}ImGui::TreePop();
        }
    }
    changed|=Text("Change note",b.value.metadata.changeNote,"Required when publishing a new revision");
    ImGui::EndDisabled();
    if(changed){if(!b.gesture){b.gesture=before;b.reference=b.preview;}b.dirty=true;b.Rebuild();}
    if(b.gesture && !ImGui::IsAnyItemActive()) {(void)b.undo.PushExecuted(std::make_unique<BufferEdit>(b,*b.gesture,b.value));b.gesture.reset();}
    if(!b.diagnostic.empty())ImGui::TextColored({1,.5f,.25f,1},"%s",b.diagnostic.c_str());
    if(Button("Save revision",editable && b.dirty && b.diagnostic.empty() && !busy,"Publish validated item changes as one new version; enter a change note")){Request r;r.action=Action::Revise;r.libraryId=library;r.metadata=b.value.metadata;r.payload=b.value.payload;r.expectedVersion=b.source->version;Send(w,std::move(r));}
    ImGui::SameLine();if(Button("Reset edits",b.dirty && !busy,"Discard the unpublished buffer; published history is retained"))b.Reset(b.source);
    ImGui::Separator();
    Combo("Destination",destination,WritableLibraries(*s.state),"Library receiving a derived editable item");Text("New stable ID",deriveId,"Unique ID in the destination library");
    if(Button("Derive / duplicate",!busy && Assets::ValidId(deriveId),"Copies values with a based-on link; source history stays intact")){Request r;r.action=Action::Derive;r.libraryId=destination;r.sourceLibraryId=library;r.itemId=b.source->meta.id;r.version=b.source->version;r.newId=deriveId;Send(w,std::move(r));}
    if(ImGui::CollapsingHeader("History / compare / where used")) {
        widgets::BeginLabelledRow("Compare version");if(ImGui::BeginCombo("##version",compareVersion?std::to_string(compareVersion).c_str():"None")){if(ImGui::Selectable("None",compareVersion==0))compareVersion=0;for(const auto& r:mounted.catalog.Items().at(b.source->meta.id))if(ImGui::Selectable(("v"+std::to_string(r->version)+" — "+r->meta.changeNote).c_str(),compareVersion==int(r->version)))compareVersion=int(r->version);ImGui::EndCombo();}widgets::EndLabelledRow();
        widgets::BeginLabelledRow("Compare item");if(ImGui::BeginCombo("##compare",s.browser.compareItem.empty()?"None":s.browser.compareItem.c_str())){if(ImGui::Selectable("None"))s.browser.compareItem.clear();for(const auto& [id,h]:mounted.catalog.Items())if(h.back()->payload->index()==b.value.payload->index() && ImGui::Selectable(h.back()->meta.name.c_str(),id==s.browser.compareItem)){s.browser.compareItem=id;compareVersion=0;}ImGui::EndCombo();}widgets::EndLabelledRow();
        if(compare){
            if(diffSource!=compare.get() || diffBuild!=b.previewBuilds){differences=Compare(*compare->payload,*b.value.payload);diffSource=compare.get();diffBuild=b.previewBuilds;}
            if(ImGui::BeginTable("Differences",3,ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollX)){
                ImGui::TableSetupColumn("Parameter");ImGui::TableSetupColumn("Reference");ImGui::TableSetupColumn("Current");ImGui::TableHeadersRow();
                for(const auto& d:differences){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s [%s]",d.path.c_str(),d.unit.c_str());ImGui::TableNextColumn();if(d.beforeText.empty())ImGui::Text("%.7g",d.before);else ImGui::TextUnformatted(d.beforeText.c_str());ImGui::TableNextColumn();if(d.afterText.empty())ImGui::Text("%.7g",d.after);else ImGui::TextUnformatted(d.afterText.c_str());}
                ImGui::EndTable();
            }
            if(differences.empty())ImGui::TextDisabled("Values match.");
        }
        if(ImGui::Button("Project folder...")){const auto folders=Dialog(w,[]{return PickFoldersDialog(false);});if(!folders.empty())usageFolder=folders.front().Native();}
        ImGui::SameLine();ImGui::TextDisabled("%s",usageFolder.empty()?"Current project":usageFolder.filename().string().c_str());Help(usageFolder.string());
        if(Button("Find uses",!busy,"Find link-backs in the current project and, optionally, saved projects in the selected folder")){Request r;r.action=Action::WhereUsed;r.libraryId=library;r.itemId=b.source->meta.id;r.path=usageFolder;Send(w,std::move(r));}
        for(const auto& use:s.whereUsed)ImGui::TextWrapped("%s / %s / %s — v%u (%s)",use.project.empty()?"Unsaved project":std::filesystem::path(use.project).filename().string().c_str(),use.name.c_str(),use.use.target.c_str(),use.use.source.version,use.use.policy==Assets::UsePolicy::Linked?"linked":"copy");
        ImGui::TextDisabled("Copies retain link-backs. Saved simulation arrays are skipped while indexing.");
    }
    ImGui::EndChild();
}
void Editor::Draw(Workflow& w) try {
    auto& s=w.library;auto& browser=s.browser;const bool busy=s.Busy();
    if(epoch!=w.epoch){buffers.clear();epoch=w.epoch;}
    if(source!=s.state){thumbnails.clear();lastLibrary.clear();source=s.state;if(s.selected && s.selected!=selectedNotification){for(const auto& [id,m]:s.state->libraries)if(const auto item=m.catalog.Items().find(s.selected->meta.id);item!=m.catalog.Items().end() && item->second.back()==s.selected){browser.library=id;browser.item=s.selected->meta.id;browser.collection.clear();browser.search.clear();}selectedNotification=s.selected;}}
    if(!s.state->libraries.contains(browser.library))browser.library="sst-examples";
    bool browserChanged=Text("Search",browser.search,"Search names, tags, descriptions and citations");
    if(ImGui::BeginTable("Browser filters",2,ImGuiTableFlags_SizingStretchSame)){
        ImGui::TableNextColumn();browserChanged|=Check("Grid view",browser.grid,"List or grid with live-shape thumbnails");
        ImGui::TableNextColumn();browserChanged|=Check("Favorites only",browser.favoritesOnly,"Show starred items in the selected library");ImGui::EndTable();
    }
    if(Button("Import...",!busy,"Preview a library, SCAPS definition/material or CSV table before committing")){if(auto path=OpenFileDialog({"Scientific assets","yaml,yml,json,def,material,csv,tsv"})){importPath=path->Utf8();ImGui::OpenPopup("Import assets");}}
    ImGui::SameLine();if(Button("Mount...",!busy,"Mount a user or shared library file")){if(auto path=OpenFileDialog({"SST library","yaml,yml,json"})){Request r;r.action=Action::Mount;r.path=path->Native();r.readOnly=mountReadOnly;Send(w,std::move(r));}}
    ImGui::SameLine();if(Button("Save library...",!busy && !s.state->libraries.at(browser.library).catalog.ReadOnly(),"Save to a versioned library file; project Save also retains all catalog snapshots")){if(auto path=SaveFileDialog({"SST library","yaml"},"library.sstlibrary.yaml")){Request r;r.action=Action::Save;r.libraryId=browser.library;r.path=path->Native();Send(w,std::move(r));}}
    ImGui::SameLine();if(ImGui::Button("New library"))ImGui::OpenPopup("Create library");
    ImGui::SameLine();if(ImGui::Button("Capture current..."))ImGui::OpenPopup("Capture current item");
    if(ImGui::BeginPopup("Capture current item")){
        Combo("Kind",captureKind,{"material","layer","device","defect","profile","interface","contact","illumination","optical"},"Capture typed inputs from the active device, selected layer or interface");Text("Stable ID",captureId);Text("Name",captureName);Combo("Destination",destination,WritableLibraries(*s.state),"Store an editable item with project provenance");if(captureKind=="profile")Text("Profile target",profileTarget,"The selected layer must contain this grading profile");
        if(Button("Capture",!busy && Assets::ValidId(captureId),"Capture the current input snapshot, without running or serializing a simulation"))try{
            const auto& draft=*w.Current().draft;const auto layer=std::clamp(w.Current().selectedLayer,0,int(draft.layers.size())-1);const auto& l=draft.project.layers[layer];Payload payload;
            if(captureKind=="device")payload=Device{draft.project};else if(captureKind=="illumination")payload=Illumination{draft.project.illumination};else if(captureKind=="contact")payload=Contact{draft.project.left_contact};else if(captureKind=="optical")payload=Optical{l.absorption};
            else if(captureKind=="defect"){Assets::Require(!l.defects.empty(),"Select a layer containing a bulk defect");payload=Defect{l.defects[std::clamp(w.pendingDefect,0,int(l.defects.size())-1)]};}
            else if(captureKind=="interface"){Interface v;const auto boundary=w.Current().selectedInterface>=0?w.Current().selectedInterface:layer;for(auto d:draft.project.interface_defects)if(d.left_layer_index==boundary){d.left_layer_index=0;v.defects.push_back(std::move(d));}Assets::Require(!v.defects.empty(),"Select an interface containing defects");payload=std::move(v);}
            else if(captureKind=="profile"){Assets::Require(bool(l.grading),"Selected layer has no grading");const auto& profile=GradingProfile(*l.grading,profileTarget);Assets::Require(bool(profile),"Selected grading profile is absent");payload=Profile{profileTarget,*profile};}
            else {auto carrier=scaps_core::make_default_device_project();carrier.layers.front()=l;payload=FromCarrier(captureKind,carrier);}
            Request r;r.action=Action::Create;r.libraryId=destination;r.metadata.id=captureId;r.metadata.name=captureName;r.metadata.collection="Captured";r.metadata.changeNote="Captured from current device inputs";r.metadata.provenance.source="project";r.metadata.provenance.citation=w.project.name+" / "+w.Current().name;r.metadata.provenance.locator=w.Current().id;r.metadata.provenance.sourcePath=std::filesystem::path(w.path).filename().string();r.payload=std::make_shared<const Payload>(std::move(payload));Send(w,std::move(r));ImGui::CloseCurrentPopup();
        }catch(const std::exception& e){s.message=e.what();}
        if(!s.message.empty())ImGui::TextWrapped("%s",s.message.c_str());ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Create library")){Text("Stable ID",newLibraryId);Text("Name",newLibraryName);Combo("Scope",newScope,{"project","user","shared"},"Shared catalogs use an explicitly selected disk file");if(Button("Create",!busy && Assets::ValidId(newLibraryId),"Create a writable project, user or shared catalog")){Request r;r.action=Action::CreateLibrary;r.libraryId=newLibraryId;r.metadata.name=newLibraryName;r.scope=newScope=="user"?Assets::Scope::User:newScope=="shared"?Assets::Scope::Shared:Assets::Scope::Project;Send(w,std::move(r));ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    if(ImGui::BeginPopup("Import assets")){ImGui::TextWrapped("%s",std::filesystem::path(importPath).filename().string().c_str());Combo("CSV kind",importKind,{"material","profile","optical","illumination"},"CSV supports profile, optical and illumination targets");Text("Profile target",profileTarget,"Engine grading field, e.g. Na_m3 or composition");Combo("Destination",destination,WritableLibraries(*s.state),"Imported content is staged before it is committed");if(Button("Preview",!busy,"Read and validate on a background job")){Request r;r.action=Action::PreviewImport;r.path=importPath;r.kind=importKind;r.profileTarget=profileTarget;Send(w,std::move(r));}
        if(s.preview){for(const auto& [id,h]:s.preview->Items()){const auto& r=*h.back();ImGui::Text("%s [%s]",r.meta.name.c_str(),Kind(*r.payload));ImGui::TextWrapped("%s %s",r.meta.provenance.citation.c_str(),r.meta.provenance.locator.c_str());}if(Button("Import all",!busy,"Commit every validated preview item atomically; duplicate IDs reject the whole import")){Request r;r.action=Action::CommitImport;r.libraryId=destination;Send(w,std::move(r));ImGui::CloseCurrentPopup();}}
        if(!s.message.empty())ImGui::TextWrapped("%s",s.message.c_str());if(ImGui::Button("Cancel")){s.preview.reset();ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    if(busy){ImGui::TextDisabled("Working... previous item and preview remain visible.");ImGui::SameLine();if(Button("Cancel asset operation",s.cancellable,"File replacement completes atomically; reads and edits can be cancelled"))(void)w.Exec(Cmd::Cancel{});}else if(!s.message.empty())ImGui::TextWrapped("%s",s.message.c_str());
    for(const auto& warning:s.state->warnings)ImGui::TextColored({1,.6f,.3f,1},"%s",warning.c_str());
    bool updates=false;
    for(const auto& use:w.Current().draft->assetUses){
        const auto library=s.state->libraries.find(use.source.libraryId);bool missing=library==s.state->libraries.end();RevisionRef latest;if(!missing){try{latest=library->second.catalog.Find(use.source.itemId);}catch(const Assets::Error&){missing=true;}}
        const bool changed=latest && (use.policy==Assets::UsePolicy::Linked?latest->version!=use.source.version:latest->version>use.source.version);
        if(changed){ImGui::TextColored({1,.75f,.2f,1},"Update available: %s v%u",use.source.itemId.c_str(),use.source.version);updates=true;}
        if(use.policy==Assets::UsePolicy::Linked && (missing || changed)){
            ImGui::PushID(use.target.c_str());if(missing)ImGui::TextColored({1,.6f,.3f,1},"Linked source unavailable: %s",use.source.itemId.c_str());
            if(Button("Keep current copy",!busy && !w.Busy(),"Detach this linked use while preserving its current parameters and provenance link-back")){auto draft=std::make_shared<ProjectDraft>(*w.Current().draft);for(auto& link:draft->assetUses)if(link.target==use.target)link.policy=Assets::UsePolicy::Copy;(void)w.Exec(Cmd::EditDocument::Make(std::move(draft)));}ImGui::PopID();
        }
    }
    if(updates && Button("Apply available asset updates",!busy && !w.Busy(),"Explicitly refresh copied link-backs and linked assets on the current device; device undo restores the previous values")){Request r;r.action=Action::Refresh;Send(w,std::move(r));}
    if(ImGui::BeginTable("Library browser",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Libraries / folders",ImGuiTableColumnFlags_WidthFixed,155);ImGui::TableSetupColumn("Items",ImGuiTableColumnFlags_WidthStretch,.8f);ImGui::TableSetupColumn("Item editor",ImGuiTableColumnFlags_WidthStretch,1.6f);ImGui::TableNextRow();ImGui::TableNextColumn();
        for(const auto& [id,m]:s.state->libraries){ImGui::PushID(id.c_str());if(ImGui::Selectable((m.catalog.name+(m.catalog.ReadOnly()?" [R]":"")+(s.dirty.contains(id)?" *":"")).c_str(),browser.library==id)){browser.library=id;browser.collection.clear();browser.item.clear();browserChanged=true;}ImGui::PopID();}
        ImGui::Separator();if(ImGui::Selectable("All folders",browser.collection.empty())){browser.collection.clear();browserChanged=true;}
        std::set<std::string> folders,tags;for(const auto& [id,h]:s.state->libraries.at(browser.library).catalog.Items()){const auto& m=h.back()->meta;folders.insert(m.collection);for(const auto& tag:m.tags)tags.insert(tag);}
        const auto folderTree=[&](auto&& self,const std::string& prefix)->void{
            std::set<std::string> children;for(const auto& path:folders)if(path.starts_with(prefix) && path.size()>prefix.size()){const auto end=path.find('/',prefix.size());children.insert(path.substr(0,end));}
            for(const auto& path:children){const bool branch=std::any_of(folders.begin(),folders.end(),[&](const auto& p){return p.starts_with(path+"/");});const auto name=path.substr(prefix.size());auto flags=ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_OpenOnArrow;if(browser.collection==path)flags|=ImGuiTreeNodeFlags_Selected;if(!branch)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;const bool open=ImGui::TreeNodeEx(path.c_str(),flags,"%s",name.c_str());if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()){browser.collection=path;browserChanged=true;}if(open && branch){self(self,path+"/");ImGui::TreePop();}}
        };folderTree(folderTree,"");
        std::vector<std::string> tagChoices{""};tagChoices.insert(tagChoices.end(),tags.begin(),tags.end());browserChanged|=Combo("Tag",browser.tag,tagChoices,"Filter by one assigned tag");
        Check("R/O",mountReadOnly,"Read-only mount: prevent writes to the shared team catalog");
        ImGui::TableNextColumn();const auto& catalog=s.state->libraries.at(browser.library).catalog;
        Assets::Filter filter{browser.search,browser.collection,browser.tag,{},browser.favoritesOnly};for(const auto& favorite:browser.favorites){const auto prefix=browser.library+"/";if(favorite.starts_with(prefix))filter.favorites.push_back(favorite.substr(prefix.size()));}
        if(lastLibrary!=browser.library || lastFilter!=filter){filtered=catalog.Search(filter);lastLibrary=browser.library;lastFilter=filter;}
        const auto& items=filtered;const float width=ImGui::GetContentRegionAvail().x;const int columns=browser.grid?std::max(1,int(width/140)):1;
        ImGui::BeginChild("Asset entries",{0,std::max(160.f,ImGui::GetContentRegionAvail().y)},ImGuiChildFlags_None);
        ImGuiListClipper clipper;clipper.Begin((int(items.size())+columns-1)/columns,browser.grid?82.f:34.f);
        while(clipper.Step())for(int at=clipper.DisplayStart*columns;at<std::min(int(items.size()),clipper.DisplayEnd*columns);++at){const auto& r=items[at];ImGui::PushID(r->meta.id.c_str());if(browser.grid && at%columns)ImGui::SameLine();ImGui::BeginGroup();
            if(browser.grid){auto found=thumbnails.find(r.get());if(found==thumbnails.end())found=thumbnails.emplace(r.get(),MakePreview(*r->payload)).first;const auto& values=found->second.y;const auto [low,high]=std::minmax_element(values.begin(),values.end());const auto pad=std::max(1.,std::max(std::abs(*low),std::abs(*high)))*.1;ImGui::PlotLines("##thumbnail",[](void* data,int i){const auto& v=*static_cast<const std::vector<double>*>(data);return float(v[std::min(std::size_t(i),v.size()-1)]);},const_cast<std::vector<double>*>(&values),std::max(2,int(values.size())),0,nullptr,float(*low-pad),float(*high+pad),{125,42});}
            const bool selected=browser.item==r->meta.id;const auto size=browser.grid?ImVec2(130,36):ImVec2(0,30);
            if(ImGui::Selectable((r->meta.name+"##item").c_str(),selected,ImGuiSelectableFlags_AllowDoubleClick,size)){browser.item=r->meta.id;browserChanged=true;}
            Help(r->meta.description+"\n"+r->meta.provenance.citation+" "+r->meta.provenance.locator);
            if(ImGui::BeginDragDropSource()){AssetDrag drag;std::snprintf(drag.library,sizeof(drag.library),"%s",browser.library.c_str());std::snprintf(drag.item,sizeof(drag.item),"%s",r->meta.id.c_str());drag.version=r->version;ImGui::SetDragDropPayload("SST_SCIENTIFIC_ASSET",&drag,sizeof(drag));ImGui::Text("%s → device/layer",r->meta.name.c_str());ImGui::EndDragDropSource();}
            if(ImGui::BeginPopupContextItem("Asset actions")){const auto favorite=browser.library+"/"+r->meta.id;const bool starred=std::find(browser.favorites.begin(),browser.favorites.end(),favorite)!=browser.favorites.end();if(ImGui::MenuItem(starred?"Remove favorite":"Add favorite")){if(starred)std::erase(browser.favorites,favorite);else browser.favorites.push_back(favorite);browserChanged=true;}
                if(ImGui::MenuItem("Compare with selection")){browser.compareItem=r->meta.id;compareVersion=0;browserChanged=true;}
                if(ImGui::MenuItem("Lock / unlock",nullptr,r->meta.locked,!catalog.ReadOnly() && !busy)){Request q;q.action=Action::SetLocked;q.libraryId=browser.library;q.itemId=r->meta.id;q.locked=!r->meta.locked;Send(w,std::move(q));}
                if(catalog.ReadOnly())ImGui::TextDisabled("Read-only: derive to edit.");
                if(ImGui::MenuItem("Export item...",nullptr,false,!busy)){if(auto path=Dialog(w,[]{return SaveFileDialog({"SST asset / SCAPS","yaml,json,material,def,csv"},"asset.yaml");})){Request q;q.action=Action::Export;q.libraryId=browser.library;q.itemId=r->meta.id;q.path=path->Native();Send(w,std::move(q));}}ImGui::EndPopup();}
            ImGui::EndGroup();ImGui::PopID();
        }
        if(items.empty())ImGui::TextWrapped("No matching assets. Clear search/filters or import a library.");
        ImGui::EndChild();
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive() && !items.empty()){const bool down=ImGui::IsKeyPressed(ImGuiKey_DownArrow),up=ImGui::IsKeyPressed(ImGuiKey_UpArrow);if(down || up){auto i=std::find_if(items.begin(),items.end(),[&](const auto& r){return r->meta.id==browser.item;});int n=i==items.end()?0:int(i-items.begin());n=std::clamp(n+(down?1:-1),0,int(items.size())-1);browser.item=items[n]->meta.id;browserChanged=true;}}
        ImGui::TableNextColumn();if(auto selected=Find(*s.state,browser.library,browser.item)){
            const auto key=browser.library+"/"+browser.item;auto& buffer=buffers[key];
            if(!buffer){
                buffer=std::make_shared<EditBuffer>();buffer->Reset(selected);
                if(const auto saved=s.drafts->find(key);saved!=s.drafts->end()){
                    buffer->source=s.state->libraries.at(browser.library).catalog.Find(browser.item,saved->second.sourceVersion);
                    buffer->value={saved->second.metadata,saved->second.payload};buffer->dirty=true;buffer->Rebuild();
                }
            }else if(buffer->source!=selected){if(!buffer->dirty || buffer->value.payload==selected->payload)buffer->Reset(selected);else buffer->diagnostic="Source changed; unpublished edits are retained. Reset or resolve before saving.";}
            Inspector(w,*buffer,browser.library);
            s.activeEditor=buffer;s.activeEditorKey=key;s.editorWindow=ImGui::GetCurrentWindow()->RootWindow->ID;
            const auto saved=s.drafts->find(key);
            if((buffer->dirty && (saved==s.drafts->end() || saved->second.payload!=buffer->value.payload || saved->second.metadata!=buffer->value.metadata)) || (!buffer->dirty && saved!=s.drafts->end())){
                auto drafts=std::make_shared<DraftMap>(*s.drafts);
                if(buffer->dirty)(*drafts)[key]={buffer->value.metadata,buffer->value.payload,buffer->source->version};else drafts->erase(key);
                s.drafts=std::move(drafts);w.projectDirty=true;w.RefreshDirty();
            }
            ImGui::Separator();const auto& draft=*w.Current().draft;Target target;if(!draft.layers.empty())target.layerId=draft.layers[std::clamp(w.Current().selectedLayer,0,int(draft.layers.size())-1)].id;target.interfaceAfter=w.Current().selectedInterface;
            const auto* interface=std::get_if<Interface>(selected->payload.get());const bool reviewed=!interface || !interface->referenceReviewRequired;
            if(Button("Use in selected layer/device...",!busy && !w.ReplacingProject() && reviewed,reviewed?"Choose copied snapshot or linked use before applying; Ctrl+Z restores the device":"Derive this item and review its SCAPS interface energy reference before use")){Request r;r.action=Action::Apply;r.libraryId=browser.library;r.itemId=selected->meta.id;r.version=selected->version;r.target=target;w.libraryDrop=r;}
        }else ImGui::TextWrapped("Select an item to inspect its source, preview, versions and parameters.");
        ImGui::EndTable();
    }
    if(browserChanged){w.projectDirty=true;w.RefreshDirty();}
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)){ImGui::SetKeyboardFocusHere();}
}catch(const std::exception& e){w.library.message=e.what();ImGui::TextWrapped("%s",e.what());}
void DropTarget(Workflow& w,const Target& target) {
    if(!ImGui::BeginDragDropTarget())return;
    if(const auto* p=ImGui::AcceptDragDropPayload("SST_SCIENTIFIC_ASSET")){if(p->DataSize==sizeof(AssetDrag)){const auto& d=*static_cast<const AssetDrag*>(p->Data);Request r;r.action=Action::Apply;r.libraryId=d.library;r.itemId=d.item;r.version=d.version;r.target=target;w.libraryDrop=std::move(r);}}
    ImGui::EndDragDropTarget();
}
void DrawDropChoice(Workflow& w) {
    if(w.libraryDrop && !w.libraryDropShown){ImGui::OpenPopup("Use library asset");w.libraryDropShown=true;}
    if(w.libraryDrop){const auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowViewport(viewport->ID);ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Appearing,{.5f,.5f});ImGui::SetNextWindowSize({std::min(540.f,viewport->WorkSize.x-40),0},ImGuiCond_Appearing);}
    if(ImGui::BeginPopup("Use library asset")){if(w.libraryDrop){auto& r=*w.libraryDrop;ImGui::Text("Use %s",r.itemId.c_str());ImGui::TextWrapped("Copied snapshots keep their current values and show available updates. Linked items use the current mounted revision when refreshed.");
        bool linked=r.policy==Assets::UsePolicy::Linked;if(Check("Linked use",linked,"Opt in to library updates; copied with link-back is the default"))r.policy=linked?Assets::UsePolicy::Linked:Assets::UsePolicy::Copy;
        if(const auto item=Find(*w.library.state,r.libraryId,r.itemId);item && (item->meta.validity.compositionMin || item->meta.validity.compositionMax)){
            const auto& v=item->meta.validity;if(!r.target.composition && v.compositionMin && v.compositionMax && *v.compositionMin==*v.compositionMax)r.target.composition=*v.compositionMin;
            bool explicitValue=bool(r.target.composition);if(Check("Set uniform composition",explicitValue,"Confirm the composition of this material snapshot [1]"))r.target.composition=explicitValue?std::optional<double>(v.compositionMin.value_or(0)):std::nullopt;
            if(r.target.composition){widgets::BeginLabelledRow("Composition [1]");DrawBoundValue("##composition",*r.target.composition,"front_transmission",1);widgets::EndLabelledRow();}
        }
        if(const auto item=Find(*w.library.state,r.libraryId,r.itemId);item && std::holds_alternative<Layer>(*item->payload))Check("Append layer",r.target.appendLayer,"Add the template as a new layer; otherwise replace the selected layer");
        if(const auto item=Find(*w.library.state,r.libraryId,r.itemId);item && std::holds_alternative<Contact>(*item->payload))Check("Right contact",r.target.rightContact,"Choose the back/right contact instead of the front/left contact");
        if(Button("Apply asset",!w.library.Busy(),"Apply through the normal typed edit job; undo includes the link-back")){auto request=std::move(*w.libraryDrop);w.libraryDrop.reset();Send(w,std::move(request));ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Cancel")){w.libraryDrop.reset();ImGui::CloseCurrentPopup();}}
        ImGui::EndPopup();
    }else if(w.libraryDrop && !ImGui::IsPopupOpen("Use library asset"))w.libraryDrop.reset();
    if(!w.libraryDrop)w.libraryDropShown=false;
}
}
