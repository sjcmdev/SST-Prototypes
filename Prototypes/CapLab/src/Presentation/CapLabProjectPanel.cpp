#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include "Domain/CapLabProjectTree.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <imgui.h>
#include <tuple>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::SelectObject(const char *kind, pl::Id id) {
    CommandContext c; c.Set("edit",Json{{"kind",kind},{"id",id}});
    (void)commands->Execute(CommandID{"caplab.select"},std::move(c));
}
void CapLabPrototypePanel::RenderTree() {
    auto &doc=state->Document(); auto &p=doc.project;
    Json edit;
    ImGui::BeginDisabled(state->activeJob!=0 || state->closeRequested);
    if (sst::ui::widgets::BeginCombo("Project",doc.path ? doc.path->filename().Utf8().c_str() : "Untitled")) {
        for (const auto &project:state->workspace.projects) {
            ImGui::PushID(std::to_string(project.handle).c_str());
            const auto label=(project.path ? project.path->filename().Utf8() : "Untitled")+(project.IsDirty() ? " *" : "");
            if (ImGui::Selectable(label.c_str(),project.handle==state->workspace.active)) {
                CommandContext c; c.Set("request",Json{{"operation","activate"},{"project_id",project.handle}});
                (void)commands->Execute(CommandID{"caplab.workspace"},std::move(c));
            }
            ImGui::PopID();
        }
        sst::ui::widgets::EndCombo();
    }
    ImGui::EndDisabled();
    if (doc.handle!=state->workspace.active) return;
    if (ImGui::Button("New sample")) edit={{"action","create_sample"},{"name","Sample"}};
    ImGui::SameLine(); if (ImGui::Button("Save")) (void)commands->Execute(CommandID{"project.save"});
    auto item=[&](const char *kind,pl::Id id,const std::string &label,bool selected) {
        ImGui::PushID(std::to_string(id).c_str());
        if (ImGui::Selectable(label.c_str(),selected)) {
            if (std::string_view(kind)=="measurement") {
                if (!ImGui::GetIO().KeyCtrl) treeMeasurements.clear();
                if (treeMeasurements.contains(id)) treeMeasurements.erase(id); else treeMeasurements.insert(id);
            }
            SelectObject(kind,id);
        }
        if (ImGui::BeginPopupContextItem("Actions")) {
            if (ImGui::MenuItem("Select / show details")) SelectObject(kind,id);
            if (ImGui::MenuItem("Delete (Undo)")) {
                if (std::string_view(kind)=="measurement") edit={{"action","delete_measurement"},{"measurement_id",id}};
                else if (std::string_view(kind)=="defect") edit={{"action","delete_defect"},{"defect_id",id}};
                else edit={{"action",std::string("delete_")+kind},{std::string(kind)+"_id",id}};
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    if (ImGui::BeginChild("Objects",ImVec2(0,ImGui::GetContentRegionAvail().y*.53f),ImGuiChildFlags_Borders)) {
        for (const auto &sample:p.samples) {
            ImGui::PushID(std::to_string(sample.id).c_str());
            ImGui::SetNextItemOpen(true,ImGuiCond_Once);
            const auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|(doc.selectedSample==sample.id ? ImGuiTreeNodeFlags_Selected : 0);
            const bool open=ImGui::TreeNodeEx("sample",flags,"%s",sample.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {treeMeasurements.clear(); SelectObject("sample",sample.id);}
            if (ImGui::BeginPopupContextItem("Sample actions")) {
                if (ImGui::MenuItem("Edit name / metadata")) SelectObject("sample",sample.id);
                if (ImGui::MenuItem("Duplicate sample (Undo)")) edit={{"action","duplicate_sample"},{"sample_id",sample.id},{"name",sample.name+" copy"}};
                if (ImGui::MenuItem("Delete sample (Undo)")) edit={{"action","delete_sample"},{"sample_id",sample.id}};
                ImGui::EndPopup();
            }
            if (open) {
                if (ImGui::TreeNodeEx("Measurements",ImGuiTreeNodeFlags_DefaultOpen)) {
                    using Group=std::tuple<std::string,double,double,double>;
                    std::map<Group,std::vector<pl::Id>> groups;
                    auto group=[&](pl::Id id) {
                        const auto m=AnalysisMeasurement(p,id,false);
                        auto value=[&](const char *key) {return m.metadata.contains(key) && m.metadata.at(key).is_number() ? m.metadata.at(key).get<double>() : -std::numeric_limits<double>::infinity();};
                        groups[{m.metadata.at("kind").get<std::string>(),value("temperature_K"),value("frequency_Hz"),value("bias_V")}].push_back(id);
                    };
                    for (const auto &m:p.measurements) if (m.at("sample_id")==sample.id) group(m.at("id"));
                    for (const auto &m:p.sweeps) if (m.sample_id==sample.id) group(m.id);
                    for (const auto &[key,ids]:groups) {
                        const auto &[kind,t,f,bias]=key;
                        auto number=[](double v,const char *unit) {if (!std::isfinite(v)) return std::string("\u2014"); char text[40]; std::snprintf(text,sizeof(text),"%.6g %s",v,unit); return std::string(text);};
                        const auto label=kind+" | "+number(t,"K")+" | "+number(f,"Hz")+" | "+number(bias,"V");
                        ImGui::PushID(std::to_string(ids.front()).c_str());
                        ImGui::SetNextItemOpen(true,ImGuiCond_Once);
                        if (ImGui::TreeNode(label.c_str())) {
                            for (auto id:ids) item("measurement",id,SelectionLabel(p,id)+" ["+std::to_string(id)+"]",doc.selectedMeasurement==id || treeMeasurements.contains(id));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                for (const auto &[label,kind,items]:{std::tuple{"Defects","defect",&p.defects},{"Analyses","analysis",&p.analyses},{"Circuits","circuit",&p.circuits}}) {
                    if (ImGui::TreeNode(label)) {
                        for (const auto &v:*items) if ((items==&p.analyses ? AnalysisSample(v) : v.at("sample_id").get<pl::Id>())==sample.id) {
                            const auto id=v.at("id").get<pl::Id>();
                            item(kind,id,items==&p.analyses ? v.at("type").get<std::string>()+" / "+v.at("status").get<std::string>()+" ["+std::to_string(id)+"]" : v.at("name").get<std::string>(),
                                id==doc.selectedDefect || id==doc.selectedAnalysis || id==doc.selectedCircuit);
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SeparatorText("Selection details");
    if (doc.selectedSample) {
        const auto &sample=FindSample(p,doc.selectedSample);
        auto metadataText=[&](const char *key) {
            if (!sample.metadata.contains(key)) return std::string{};
            const auto &value=sample.metadata.at(key);
            return value.is_string() ? value.get<std::string>() : value.dump();
        };
        if (detailsSample!=sample.id) {detailsSample=sample.id; sampleName=sample.name; sampleMaterial=metadataText("material"); sampleNotes=metadataText("notes");}
        auto textEdit=[&](const char *label,std::string &value,const char *key,const std::string &saved) {
            sst::ui::widgets::Text(label,&value);
            if (ImGui::IsItemDeactivatedAfterEdit()) edit={{"action",std::string_view(key)=="name" ? "rename_sample" : "sample_metadata"},{"sample_id",sample.id},{key,value}};
            if (!ImGui::IsItemActive()) value=saved;
        };
        textEdit("Name",sampleName,"name",sample.name);
        textEdit("Material",sampleMaterial,"material",metadataText("material"));
        textEdit("Notes",sampleNotes,"notes",metadataText("notes"));
        auto &area=areaDraft.try_emplace(sample.id,sample.area_m2.value_or(0)*1e6).first->second;
        sst::ui::widgets::Double("Area [mm2]",&area,.01f,"%.9g");
        if (ImGui::IsItemDeactivatedAfterEdit()) edit={{"action","sample_metadata"},{"sample_id",sample.id},{"area_m2",area*1e-6}};
        if (!ImGui::IsItemActive()) area=sample.area_m2.value_or(0)*1e6;
        if (!sample.area_m2) ImGui::TextDisabled("Area unknown");
        if (doc.selectedMeasurement) {
            const auto m=AnalysisMeasurement(p,doc.selectedMeasurement);
            ImGui::TextWrapped("%s; %zu rows",SelectionLabel(p,doc.selectedMeasurement).c_str(),m.rowIds->size());
            ImGui::BeginDisabled(state->activeJob!=0);
            const std::string kind=m.metadata.at("kind");
            const auto types=kind=="DLCPProfile" ? std::vector<const char*>{"C-V","Mott-Schottky","N_CV(W)","N_DLCP(W)"} :
                kind=="CV-derived" ? std::vector<const char*>{"C-V","Mott-Schottky","N_CV(W)"} :
                kind=="CVSweep" ? std::vector<const char*>{"C-V","Mott-Schottky"} :
                kind=="FrequencySweep" ? std::vector<const char*>{"C-f","G/omega-f","Nyquist","Bode magnitude","Bode phase"} : std::vector<const char*>{};
            for (auto type:types) if (ImGui::Button((std::string("Plot ")+type).c_str())) {
                state->analysisOptions={{"plot_type",type},{"measurement_ids",Json::array({doc.selectedMeasurement})}};
                (void)commands->Execute(CommandID{"caplab.plot"});
            }
            ImGui::EndDisabled();
        }
        if (sst::ui::widgets::BeginCombo("Move / merge into",SelectionLabel(p,moveTarget).c_str())) {
            for (const auto &s:p.samples) if (s.id!=sample.id) {
                ImGui::PushID(std::to_string(s.id).c_str());
                if (ImGui::Selectable(s.name.c_str(),s.id==moveTarget)) moveTarget=s.id;
                ImGui::PopID();
            }
            sst::ui::widgets::EndCombo();
        }
        ImGui::BeginDisabled(!moveTarget || moveTarget==sample.id);
        if (ImGui::Button("Move selected measurements")) {
            std::vector<pl::Id> ids(treeMeasurements.begin(),treeMeasurements.end());
            if (ids.empty() && doc.selectedMeasurement) ids.push_back(doc.selectedMeasurement);
            edit={{"action","move_measurements"},{"measurement_ids",ids},{"target_sample_id",moveTarget}};
        }
        if (ImGui::Button("Merge all measurements")) edit={{"action","merge_samples"},{"sample_id",sample.id},{"target_sample_id",moveTarget}};
        ImGui::EndDisabled();
        for (const auto &a:p.analyses) if (a.at("id")==doc.selectedAnalysis) {
            ImGui::TextWrapped("Accepted result: %s",a.at("status").get<std::string>().c_str());
            if (ImGui::TreeNode("Result / provenance")) {ImGui::TextWrapped("%s",a.dump(2).c_str()); ImGui::TreePop();}
        }
    } else {detailsSample=0; ImGui::TextWrapped("Select a sample, measurement or result. Ctrl+click selects measurements to move together.");}
    if (!edit.is_null()) {
        CommandContext c;
        const std::string action=edit.at("action");
        if (action=="delete_measurement") {c.Set("edit",edit); (void)commands->Execute(CommandID{"caplab.measurement"},std::move(c));}
        else if (action=="delete_defect") {edit["action"]="delete"; c.Set("edit",edit); (void)commands->Execute(CommandID{"caplab.defect"},std::move(c));}
        else {c.Set("edit",edit); (void)commands->Execute(CommandID{"caplab.tree"},std::move(c));}
        treeMeasurements.clear();
    }
}
}
