#include "Presentation/CapLabPrototypePanel.hpp"
#include "Domain/CapLabAnalysis.hpp"
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderIS() {
    if (!ImGui::CollapsingHeader("IS / CircuitCore schematic")) return;
    ImGui::PushID("IS");
    auto &p=state->document.project;
    static char topology[512]="R0-p(R1,C1)", name[128]="Circuit";
    static double confirmedTemperature=300, confirmedBias=0;
    auto edit=[&](Json value) {
        CommandContext context; context.Set("edit",std::move(value));
        (void)commands->Execute(CommandID{"caplab.circuit"},std::move(context));
    };
    try {
        ImGui::InputText("Circuit name",name,sizeof(name));
        ImGui::InputText("Topology",topology,sizeof(topology));
        static pl::Id sample=0;
        if (ImGui::BeginCombo("Circuit sample",sample ? std::to_string(sample).c_str() : "Select sample")) {
            for (const auto &s : p.samples) if (ImGui::Selectable(s.name.c_str(),sample==s.id)) sample=s.id;
            ImGui::EndCombo();
        }
        if (ImGui::Button("Create circuit")) {
            const auto old=p.circuits.size();
            edit({{"action","create"},{"sample_id",sample},{"name",name},{"topology",topology}});
            if (p.circuits.size()>old) { selectedCircuit=p.circuits.back().at("id"); circuitEditor.reset(); }
        }
        auto selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
        if (ImGui::BeginCombo("Schematic",selected==p.circuits.end() ? "Select circuit" : selected->at("name").get<std::string>().c_str())) {
            for (const auto &c : p.circuits) {
                ImGui::PushID(std::to_string(c.at("id").get<pl::Id>()).c_str());
                if (ImGui::Selectable(c.at("name").get<std::string>().c_str(),c.at("id")==selectedCircuit)) { selectedCircuit=c.at("id"); circuitEditor.reset(); }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
        if (selected!=p.circuits.end()) {
            auto d=ReadCircuit(selected->at("schematic"));
            bool changed=circuitEditor.properties(d);
            ImGui::BeginChild("Circuit editor",ImVec2(0,320),ImGuiChildFlags_Borders);
            try {changed |= circuitEditor.draw(d);}
            catch (...) {ImGui::EndChild(); throw;}
            ImGui::EndChild();
            if (changed) edit({{"action","replace"},{"circuit_id",selectedCircuit},{"schematic",CircuitJson(d)}});
            // Commands replace the project; never retain JSON iterators across one.
            selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
            if (ImGui::BeginCombo("IS measurement",circuitMeasurement ? std::to_string(circuitMeasurement).c_str() : "Select sweep")) {
                auto item=[&](pl::Id id) {
                    const auto m=AnalysisMeasurement(p,id);
                    if (m.at("kind")!="FrequencySweep" || m.at("sample_id")!=selected->at("sample_id")) return;
                    const auto label=std::to_string(id)+" / "+m.at("temperature_K").dump()+" K";
                    if (ImGui::Selectable(label.c_str(),id==circuitMeasurement)) circuitMeasurement=id;
                };
                for (const auto &m : p.sweeps) item(m.id);
                for (const auto &m : p.measurements) item(m.at("id").get<pl::Id>());
                ImGui::EndCombo();
            }
            if (circuitMeasurement) {
                const auto m=AnalysisMeasurement(p,circuitMeasurement);
                Json settings{{"type","Circuit"},{"measurement_id",circuitMeasurement},{"circuit_id",selectedCircuit}};
                if (!m.contains("temperature_K") || m.at("temperature_K").is_null()) {
                    ImGui::InputDouble("Confirm measurement temperature [K]",&confirmedTemperature);
                    settings["temperature_K"]=confirmedTemperature;
                } else ImGui::Text("Measured temperature: %s K",m.at("temperature_K").dump().c_str());
                if (!m.contains("bias_V") || m.at("bias_V").is_null()) {
                    ImGui::InputDouble("Confirm measurement bias [V]",&confirmedBias); settings["bias_V"]=confirmedBias;
                } else ImGui::Text("Measured bias: %s V",m.at("bias_V").dump().c_str());
                ImGui::TextUnformatted("Exact measured frequencies; simulation runs in a cancellable Core job.");
                ImGui::BeginDisabled(state->activeJob!=0);
                if (ImGui::Button("Simulate IS")) {state->analysisOptions=std::move(settings); (void)commands->Execute(CommandID{"caplab.analyze"});}
                ImGui::EndDisabled();
            }
            if (ImGui::Button("Delete circuit")) {edit({{"action","delete"},{"circuit_id",selectedCircuit}}); selectedCircuit=0; circuitEditor.reset();}
        }
        for (const auto &a : p.analyses) if (a.at("type")=="Circuit") {
            ImGui::PushID(std::to_string(a.at("id").get<pl::Id>()).c_str());
            ImGui::Text("IS %llu: %s / T=%s K / bias=%s V",a.at("id").get<pl::Id>(),a.at("status").get<std::string>().c_str(),a.at("temperature_K").dump().c_str(),a.at("bias_V").dump().c_str());
            for (const auto &g : a.at("graphs")) {
                const auto id=g.get<pl::Id>();
                ImGui::PushID(std::to_string(id).c_str());
                if (ImGui::Button(p.engine->object(id).name.c_str())) {p.graph=id; state->view={};}
                ImGui::PopID();
            }
            for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s: %s",d.at("code").get<std::string>().c_str(),d.at("message").get<std::string>().c_str());
            ImGui::PopID();
        }
    } catch (const std::exception &e) {state->status=e.what();}
    ImGui::PopID();
}
}
