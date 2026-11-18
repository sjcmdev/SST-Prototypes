#include "Presentation/CapLabPrototypePanel.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include <sst/ui/Plot/ImPlotRenderer.hpp>
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
                if (ImGui::Button("Simulate IS")) {state->analysisOptions=settings; (void)commands->Execute(CommandID{"caplab.analyze"});}
                ImGui::EndDisabled();
                if (fitCircuit!=selectedCircuit || ImGui::Button("Reset fit parameters from schematic")) {
                    fitCircuit=selectedCircuit; isParameters=Json::array();
                    for (const auto &e : selected->at("schematic").at("elements")) {
                        const int kind=e.at("kind");
                        std::vector<std::string> fields={"value"};
                        if (kind==3 || kind==5 || kind==6) fields.push_back("secondary");
                        if (kind==5 || kind==6) for (const auto &field : {"Cj0","Vbi","m","FC","tau"}) fields.push_back(field);
                        for (const auto &field : fields) {
                            const double value=e.at(field);
                            const std::string unit=field=="value" ? e.at("value_unit").get<std::string>() : field=="secondary" ? "1" : selected->at("schematic").at("parameter_units").at(field).get<std::string>();
                            isParameters.push_back({{"element",e.at("name")},{"field",field},{"unit",unit},
                                {"free",field=="value"},{"min",value>0 ? value*.1 : 0.},{"max",value>0 ? value*10 : 1.}});
                        }
                    }
                }
                for (auto &parameter : isParameters) {
                    const std::string label=parameter.at("element").get<std::string>()+"."+parameter.at("field").get<std::string>()+" ["+parameter.at("unit").get<std::string>()+"]";
                    ImGui::PushID(label.c_str());
                    bool free=parameter.at("free"); double lo=parameter.at("min"), hi=parameter.at("max");
                    ImGui::Checkbox(label.c_str(),&free); parameter["free"]=free;
                    ImGui::InputDouble("Lower bound",&lo,0,0,"%.9g"); parameter["min"]=lo;
                    ImGui::InputDouble("Upper bound",&hi,0,0,"%.9g"); parameter["max"]=hi;
                    ImGui::PopID();
                }
                static double floor=1., fmin=0., fmax=1e12;
                ImGui::InputDouble("Weighting floor [Ohm]",&floor);
                ImGui::InputDouble("Fit minimum frequency [Hz]",&fmin);
                ImGui::InputDouble("Fit maximum frequency [Hz]",&fmax);
                ImGui::TextUnformatted("Weight = 1/max(|Z|, floor)^2; score is not statistical chi-squared.");
                ImGui::BeginDisabled(state->activeJob!=0);
                if (ImGui::Button("Fit IS draft")) {
                    fitView={};
                    settings["fit"]=true; settings["parameters"]=isParameters; settings["Z_floor_Ohm"]=floor;
                    settings["frequency_min_Hz"]=fmin; settings["frequency_max_Hz"]=fmax;
                    state->analysisOptions=std::move(settings); (void)commands->Execute(CommandID{"caplab.analyze"});
                }
                ImGui::EndDisabled();
            }
            if (ImGui::Button("Delete circuit")) {edit({{"action","delete"},{"circuit_id",selectedCircuit}}); selectedCircuit=0; circuitEditor.reset();}
        }
        if (state->analysisDraft && !state->analysisDraft->analyses.empty()) {
            auto &draft=*state->analysisDraft;
            const auto &a=draft.analyses.back();
            if (a.contains("score")) {
                ImGui::Text("Draft score %.9g -> %.9g / %s",a.at("initial_score").get<double>(),a.at("score").get<double>(),a.at("stop_status").get<std::string>().c_str());
                ImGui::TextUnformatted("Covariance unavailable (Nelder-Mead).");
                for (const auto &v : a.at("parameters")) ImGui::Text("%s.%s: %.9g -> %.9g %s%s",v.at("element").get<std::string>().c_str(),v.at("field").get<std::string>().c_str(),v.at("start").get<double>(),v.at("fitted").get<double>(),v.at("unit").get<std::string>().c_str(),v.at("free").get<bool>() ? "" : " (fixed)");
                for (const auto &g : a.at("graphs")) {
                    const auto id=g.get<pl::Id>();
                    if (ImGui::Button(draft.engine->object(id).name.c_str())) {draft.graph=id; fitView={};}
                }
                const auto layer=draft.engine->children(draft.graph).at(0);
                std::vector<sst::ui::plot::SeriesEntryView> series;
                for (const auto id : draft.engine->children(layer)) {
                    auto entry = draft.engine->series(id);
                    entry.id = draft.engine->object(id).name + "###" + entry.id;
                    series.push_back(std::move(entry));
                }
                sst::ui::plot::RenderPlot("##ISdraft",std::get<pl::Layer>(draft.engine->object(layer).data).spec,series.data(),series.size(),fitView,{},ImVec2(-1,260));
                ImGui::BeginDisabled(state->analysisRevision!=state->document.revision);
                if (ImGui::Button("Accept fit (Undo)")) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
                ImGui::EndDisabled();
                ImGui::SameLine(); if (ImGui::Button("Discard fit draft")) (void)commands->Execute(CommandID{"caplab.cancel"});
            }
        }
        for (const auto &a : p.analyses) if (a.at("type")=="Circuit") {
            ImGui::PushID(std::to_string(a.at("id").get<pl::Id>()).c_str());
            ImGui::Text("IS %llu: %s / T=%s K / bias=%s V",a.at("id").get<pl::Id>(),a.at("status").get<std::string>().c_str(),a.at("temperature_K").dump().c_str(),a.at("bias_V").dump().c_str());
            if (a.contains("score")) ImGui::Text("Score %.9g / %s; covariance unavailable",a.at("score").get<double>(),a.at("stop_status").get<std::string>().c_str());
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
