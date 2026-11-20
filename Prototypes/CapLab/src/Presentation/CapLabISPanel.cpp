#include "Presentation/CapLabPrototypePanel.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <cstdlib>
#include "Presentation/CapLabISPlot.hpp"
#include "Presentation/SelectionLabel.hpp"
#include "Presentation/CircuitDisplay.hpp"
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderIS() {
    if (const auto gallery = std::getenv("SST_CAPLAB_GALLERY_SECTION"))
        ImGui::SetNextItemOpen(std::string_view(gallery) == "IS", ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("IS / CircuitCore schematic")) return;
    ImGui::PushID("IS");
    auto &p=state->document.project;
    if (circuitDocument!=state->document.path.Utf8()) {
        circuitDocument=state->document.path.Utf8();
        circuitAutoReady=circuitRecalc=false;
    }
    if (std::getenv("SST_CAPLAB_GALLERY_SECTION") && !selectedCircuit && !p.circuits.empty())
        selectedCircuit=p.circuits.front().at("id");
    static char topology[512]="R0-p(R1,C1)", name[128]="Circuit";
    static double confirmedTemperature=300, confirmedBias=0;
    auto edit=[&](Json value) {
        CommandContext context; context.Set("edit",std::move(value));
        (void)commands->Execute(CommandID{"caplab.circuit"},std::move(context));
    };
    try {
        ImGui::SeparatorText("Create circuit");
        sst::ui::widgets::Text("Circuit name", name, sizeof(name));
        sst::ui::widgets::Text("Topology", topology, sizeof(topology));
        static pl::Id sample=0;
        if (sst::ui::widgets::BeginCombo("Circuit sample", sample ? std::to_string(sample).c_str() : "Select sample")) {
            for (const auto &s : p.samples) if (ImGui::Selectable(s.name.c_str(),sample==s.id)) sample=s.id;
            sst::ui::widgets::EndCombo();
        }
        if (ImGui::Button("Create circuit")) {
            const auto old=p.circuits.size();
            edit({{"action","create"},{"sample_id",sample},{"name",name},{"topology",topology}});
            if (p.circuits.size()>old) { selectedCircuit=p.circuits.back().at("id"); circuitMeasurement=0; circuitRecalc=false; circuitAutoReady=false; circuitEditor.reset(); }
        }
        ImGui::Spacing(); ImGui::SeparatorText("Schematic and parameters");
        auto selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
        if (sst::ui::widgets::BeginCombo("Schematic", selected==p.circuits.end() ? "Select circuit" : selected->at("name").get<std::string>().c_str())) {
            for (const auto &c : p.circuits) {
                ImGui::PushID(std::to_string(c.at("id").get<pl::Id>()).c_str());
                if (ImGui::Selectable(c.at("name").get<std::string>().c_str(),c.at("id")==selectedCircuit)) { selectedCircuit=c.at("id"); circuitMeasurement=0; circuitRecalc=false; circuitAutoReady=false; circuitEditor.reset(); }
                ImGui::PopID();
            }
            sst::ui::widgets::EndCombo();
        }
        selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
        if (selected!=p.circuits.end()) {
            auto d=ReadCircuit(selected->at("schematic"));
            circuitEditor.uncertainties.clear();
            for (const auto& a : p.analyses) if (a.at("type")=="Circuit" && a.at("status")=="valid" &&
                a.contains("parameters") && a.at("circuit")==*selected)
                for (const auto& v : a.at("parameters")) if (v.contains("sigma") && !v.at("sigma").is_null())
                    circuitEditor.uncertainties[v.at("element").get<std::string>()+"."+v.at("field").get<std::string>()]=v.at("sigma");
            bool changed=circuitEditor.properties(d);
            ImGui::BeginChild("Circuit editor",ImVec2(0,std::max(340.f,ImGui::GetContentRegionAvail().y*.8f)),ImGuiChildFlags_Borders);
            try {changed |= circuitEditor.draw(d);}
            catch (...) {ImGui::EndChild(); throw;}
            ImGui::EndChild();
            if (changed) {
                edit({{"action","replace"},{"circuit_id",selectedCircuit},{"schematic",CircuitJson(d)}});
                circuitRecalc=true; fitCircuit=0;
            }
            // Commands replace the project; never retain JSON iterators across one.
            selected=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==selectedCircuit;});
            ImGui::Spacing(); ImGui::SeparatorText("Measurement and simulation");
            if (sst::ui::widgets::BeginCombo("IS measurement", circuitMeasurement ? std::to_string(circuitMeasurement).c_str() : "Select sweep")) {
                auto item=[&](pl::Id id) {
                    const auto m=AnalysisMeasurement(p,id);
                    if (m.at("kind")!="FrequencySweep" || m.at("sample_id")!=selected->at("sample_id")) return;
                    const auto label=std::to_string(id)+" / "+m.at("temperature_K").dump()+" K";
                    if (ImGui::Selectable(label.c_str(),id==circuitMeasurement)) {circuitMeasurement=id; circuitRecalc=true; circuitAutoReady=false;}
                };
                for (const auto &m : p.sweeps) item(m.id);
                for (const auto &m : p.measurements) item(m.at("id").get<pl::Id>());
                sst::ui::widgets::EndCombo();
            }
            if (circuitMeasurement) {
                const auto m=AnalysisMeasurement(p,circuitMeasurement);
                Json settings{{"type","Circuit"},{"measurement_id",circuitMeasurement},{"circuit_id",selectedCircuit}};
                if (!m.contains("temperature_K") || m.at("temperature_K").is_null()) {
                    if (sst::ui::widgets::Double("Confirm measurement temperature [K]", &confirmedTemperature, .01f, "%.6g")) circuitRecalc=true;
                    settings["temperature_K"]=confirmedTemperature;
                } else ImGui::Text("Measured temperature: %s K",m.at("temperature_K").dump().c_str());
                if (!m.contains("bias_V") || m.at("bias_V").is_null()) {
                    if (sst::ui::widgets::Double("Confirm measurement bias [V]", &confirmedBias, .01f, "%.6g")) circuitRecalc=true;
                    settings["bias_V"]=confirmedBias;
                } else ImGui::Text("Measured bias: %s V",m.at("bias_V").dump().c_str());
                ImGui::TextUnformatted("Exact measured frequencies; edits automatically resimulate the selected sweep in a cancellable Core job.");
                ImGui::BeginDisabled(state->activeJob!=0);
                const bool measuredConditions=m.contains("temperature_K") && !m.at("temperature_K").is_null() &&
                    m.contains("bias_V") && !m.at("bias_V").is_null();
                if (!measuredConditions && !circuitAutoReady) ImGui::TextWrapped("Confirm unknown T / bias with Simulate IS before automatic recalculation.");
                if (circuitRecalc && (measuredConditions || circuitAutoReady) && !state->activeJob && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    circuitRecalc=false; state->analysisOptions=settings;
                    (void)commands->Execute(CommandID{"caplab.analyze"});
                }
                if (ImGui::Button("Simulate IS")) {circuitAutoReady=true; circuitRecalc=false; state->analysisOptions=settings; (void)commands->Execute(CommandID{"caplab.analyze"});}
                ImGui::EndDisabled();
                ImGui::Spacing(); ImGui::SeparatorText("Fit parameters and bounds");
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
                    std::string unit=parameter.at("unit");
                    if (parameter.at("field")=="value") for (const auto& e : d.elements)
                        if (e.name==parameter.at("element").get<std::string>()) unit=ElementUnit(e);
                    const std::string label=parameter.at("element").get<std::string>()+"."+parameter.at("field").get<std::string>()+" ["+unit+"]";
                    ImGui::PushID(label.c_str());
                    bool free=parameter.at("free"); double lo=parameter.at("min"), hi=parameter.at("max");
                    sst::ui::widgets::Checkbox(label.c_str(), &free); parameter["free"]=free;
                    sst::ui::widgets::LogValue(("Lower bound ["+unit+"]###Lower bound").c_str(), &lo); parameter["min"]=lo;
                    sst::ui::widgets::LogValue(("Upper bound ["+unit+"]###Upper bound").c_str(), &hi); parameter["max"]=hi;
                    ImGui::Text("Bounds: %s .. %s",Engineering(lo,unit).c_str(),Engineering(hi,unit).c_str());
                    ImGui::PopID();
                }
                static double floor=1., fmin=0., fmax=1e12;
                sst::ui::widgets::Double("Weighting floor [Ohm]", &floor, .01f, "%.6g");
                sst::ui::widgets::LogValue("Fit minimum frequency [Hz]", &fmin);
                sst::ui::widgets::LogValue("Fit maximum frequency [Hz]", &fmax);
                ImGui::TextUnformatted("Weight = 1/max(|Z|, floor)^2; score is not statistical chi-squared.");
                ImGui::BeginDisabled(state->activeJob!=0);
                if (ImGui::Button("Fit IS draft")) {
                    circuitAutoReady=true; circuitRecalc=false; fitView={};
                    settings["fit"]=true; settings["parameters"]=isParameters; settings["Z_floor_Ohm"]=floor;
                    settings["frequency_min_Hz"]=fmin; settings["frequency_max_Hz"]=fmax;
                    state->analysisOptions=std::move(settings); (void)commands->Execute(CommandID{"caplab.analyze"});
                }
                ImGui::EndDisabled();
            }
            if (ImGui::Button("Delete circuit")) {edit({{"action","delete"},{"circuit_id",selectedCircuit}}); selectedCircuit=0; circuitMeasurement=0; circuitRecalc=false; circuitAutoReady=false; circuitEditor.reset();}
        }
        if (state->analysisDraft && !state->analysisDraft->analyses.empty()) {
            auto &draft=*state->analysisDraft;
            const auto &a=draft.analyses.back();
            if (a.contains("score")) {
                ImGui::Text("Draft score %.9g -> %.9g / %s",a.at("initial_score").get<double>(),a.at("score").get<double>(),a.at("stop_status").get<std::string>().c_str());
                ImGui::TextWrapped("Uncertainty: %s",a.value("covariance_status",std::string("unavailable in saved fit")).c_str());
                const auto fittedCircuit=ReadCircuit(a.at("circuit").at("schematic"));
                for (const auto &v : a.at("parameters")) {
                    std::string unit=v.at("unit");
                    if (v.at("field")=="value") for (const auto& e : fittedCircuit.elements)
                        if (e.name==v.at("element").get<std::string>()) unit=ElementUnit(e);
                    ImGui::Text("%s.%s: %s -> %s%s",v.at("element").get<std::string>().c_str(),v.at("field").get<std::string>().c_str(),
                        Engineering(v.at("start"),unit).c_str(),(Engineering(v.at("fitted"),unit)+(v.contains("sigma") && !v.at("sigma").is_null() ? " \u00b1 "+Engineering(v.at("sigma"),unit) : "")).c_str(),v.at("free").get<bool>() ? "" : " (fixed)");
                }
                ImGui::PushID("Draft plots");
                RenderISTabs(draft,a,fitView);
                ImGui::PopID();
                ImGui::BeginDisabled(state->analysisRevision!=state->document.revision);
                if (ImGui::Button("Accept fit (Undo)")) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
                ImGui::EndDisabled();
                ImGui::SameLine(); if (ImGui::Button("Discard fit draft")) (void)commands->Execute(CommandID{"caplab.cancel"});
            }
        }
        for (const auto &a : p.analyses) if (a.at("type")=="Circuit") {
            ImGui::PushID(std::to_string(a.at("id").get<pl::Id>()).c_str());
            ImGui::Text("IS %llu: %s / T=%s K / bias=%s V",a.at("id").get<pl::Id>(),a.at("status").get<std::string>().c_str(),a.at("temperature_K").dump().c_str(),a.at("bias_V").dump().c_str());
            if (a.contains("score")) ImGui::Text("Score %.9g / %s",a.at("score").get<double>(),a.at("stop_status").get<std::string>().c_str());
            if (ImGui::TreeNode("Open IS view")) {
            for (const auto &g : a.at("graphs")) {
                const auto id=g.get<pl::Id>();
                ImGui::PushID(std::to_string(id).c_str());
                if (ImGui::Button(p.engine->object(id).name.c_str())) {p.graph=id; state->view={};}
                ImGui::PopID();
            }
            ImGui::TreePop();
            }
            for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s: %s",d.at("code").get<std::string>().c_str(),d.at("message").get<std::string>().c_str());
            ImGui::PopID();
        }
    } catch (const std::exception &e) {state->status=e.what();}
    ImGui::PopID();
}
}

namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderISTabs(Project& project, const Json& analysis, sst::ui::plot::PlotViewState& viewState) {
    const auto& graphs=analysis.at("graphs");
    if (graphs.size()<7) return;
    const auto analysisId=analysis.at("id").get<pl::Id>();
    ImGui::PushID(state->document.path.Utf8().c_str());
    ImGui::PushID(std::to_string(analysisId).c_str());
    const auto current=project.engine->object(project.graph).name;
    const bool selectTab=isPlotAnalysis!=analysisId || isPlotGraph!=project.graph;
    isPlotAnalysis=analysisId;
    const auto sample=SelectionLabel(project,analysis.at("sample_id"));
    const auto measurement=analysis.at("inputs").at(0).at("id").get<pl::Id>();
    bool refitPlot=false;
    auto render=[&](std::size_t index, float height) {
        const auto graph=graphs.at(index).get<pl::Id>();
        const auto layer=project.engine->children(graph).at(0);
        auto spec=std::get<pl::Layer>(project.engine->object(layer).data).spec;
        std::vector<sst::ui::plot::SeriesEntryView> series;
        for (auto id : project.engine->children(layer)) {
            auto entry=project.engine->series(id);
            entry.id=project.engine->object(id).name+"###"+entry.id;
            series.push_back(std::move(entry));
        }
        // The two residual components share frequency and units, so overlay them.
        if (index==5) {
            const auto other=project.engine->children(graphs.at(6).get<pl::Id>()).at(0);
            series.front().id="Re residual";
            for (auto id : project.engine->children(other)) {
                auto entry=project.engine->series(id); entry.id="Im residual";
                entry.style.color={.85f,.25f,.12f,1}; series.push_back(std::move(entry));
            }
        }
        std::vector<double> x,y;
        if (residualOverlay && index<3) {
            const auto& re=analysis.at("residual_ReZ_Ohm");
            const auto& im=analysis.at("residual_ImZ_Ohm");
            for (std::size_t i=0;i<re.size();++i) {
                const bool valid=!re[i].is_null() && !im[i].is_null();
                const double r=valid ? re[i].get<double>() : std::numeric_limits<double>::quiet_NaN();
                const double j=valid ? im[i].get<double>() : r;
                x.push_back(index==0 ? r : analysis.at("frequency_Hz").at(i).get<double>());
                y.push_back(index==0 ? -j : index==1 ? std::hypot(r,j) : std::atan2(j,r)*180/std::acos(-1.));
            }
            auto entry=series.front(); entry.id=index==0 ? "residual (model - measured)" : index==1 ? "|delta Z| (model - measured)" : "arg(delta Z) (model - measured)";
            entry.data={x.data(),y.data(),x.size()}; entry.style.color={.3f,.75f,.4f,1};
            series.push_back(std::move(entry));
        }
        ImGui::PushID(static_cast<int>(index));
        if (refitPlot) ImPlot::SetNextAxesToFit();
        const auto scale=RenderISPlot("##IS",spec,series.data(),series.size(),index==2 ? bodePhaseView : viewState,ImVec2(-1,height));
        if (&project==&state->document.project) state->graphViews[graphs.at(index).get<pl::Id>()]=index==2 ? bodePhaseView : viewState;
        if (spec.equalAspect) ImGui::Text("Equal scale: X %.9g px/Ohm | Y %.9g px/Ohm",scale.xPixelsPerOhm,scale.yPixelsPerOhm);
        ImGui::PopID();
    };
    if (ImGui::BeginTabBar("IS views")) {
        struct Tab {const char* label; std::size_t index;};
        for (const auto tab : {Tab{"Nyquist",0},Tab{"Bode",1},Tab{"Residuals",5},Tab{"C-f",3},Tab{"G/omega-f",4}}) {
            const bool selected=tab.index==0 ? current=="Nyquist" : tab.index==1 ? current.starts_with("Bode") :
                tab.index==5 ? current.ends_with("residual") : current==tab.label;
            if (ImGui::BeginTabItem(tab.label,nullptr,selectTab && selected ? ImGuiTabItemFlags_SetSelected : 0)) {
                const auto graph=graphs.at(tab.index).get<pl::Id>();
                if (isPlotGraph!=graph) {viewState={}; fitView={}; bodePhaseView={};}
                project.graph=graph; isPlotGraph=graph;
                auto measurementLabel=SelectionLabel(project,measurement);
                if (const auto temperature=measurementLabel.rfind(", T = "); temperature!=std::string::npos) measurementLabel.resize(temperature);
                const std::string title=(measurementLabel==sample ? measurementLabel : sample+" / "+measurementLabel)+
                    ", T = "+analysis.at("temperature_K").dump()+" K \u2014 "+tab.label;
                if (&project==&state->document.project) {
                    auto window=GetState();
                    if (window.shortName!=title) {window.shortName=title; window.longName=title; ApplyState(window);}
                } else ImGui::TextWrapped("%s — fit draft",title.c_str());
                if (tab.index<3) refitPlot=sst::ui::widgets::Checkbox("Residual overlay (model - measured)",&residualOverlay);
                if (ImGui::Button("Fit plot")) refitPlot=true;
                const float available=std::max(220.f,ImGui::GetContentRegionAvail().y-65);
                if (tab.index==1) {
                    ImGui::TextUnformatted("Magnitude |Z|"); render(1,available*.5f-16);
                    ImGui::TextUnformatted("Phase (deg)"); render(2,available*.5f-16);
                } else render(tab.index,available);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();
    ImGui::PopID();
}
}
