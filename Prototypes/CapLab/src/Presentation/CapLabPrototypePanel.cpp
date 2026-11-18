#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include <imgui.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::Render() {
    if (!IsVisible()) return;
    bool visible = true;
    const bool content = ImGui::Begin(GetTitle().c_str(), &visible);
    SetVisible(visible);
    if (!content) { ImGui::End(); return; }
    auto &doc = state->document;
    ImGui::Text("%s%s", doc.path.Empty() ? "Untitled" : doc.path.Utf8().c_str(), doc.dirty ? " (unsaved)" : "");
    bool comma = state->decimal == ',';
    if (ImGui::Checkbox("Decimal comma (semicolon/whitespace columns)", &comma)) state->decimal = comma ? ',' : '.';
    static int profile = 1;
    const char *profiles[] = {"Lab AS Cp/Rp (SI)", "Lab AS Cp/Rp (mapped)", "Converted AS C/G", "ProPho DL/CV", "Lab CV", "Lab DLCP raw", "Lab IS Z", "Lab IS C/G", "Lab IS Cp/Rp", "SCAPS .cf", "SCAPS .cv", "ProPho manifest / project"};
    const char *keys[] = {"", "lab_as_cp_rp", "converted_as_c_g", "propho_dl", "lab_cv", "lab_dlcp_raw", "lab_is_z", "lab_is_c_g", "lab_is_cp_rp", "scaps_cf", "scaps_cv", "propho_json"};
    static int mapping[5] = {1,2,3,4,5}, unitChoice[5] = {}, sign = -1, amplitude = 0, asVariant = -1;
    static bool areaKnown = false, temperatureKnown = false, frequencyKnown = false, biasKnown = false, optionalG = false, columnTF = false, maxKnown = false, maxConstant = false;
    static double area = 0, temperature = 0, frequency = 0, bias = 0;
    static char baseDir[1024] = "", biasDefinition[256] = "";
    static int excludeRow = 0;
    static std::vector<int> excluded;
    if (ImGui::Combo("Import profile",&profile,profiles,IM_ARRAYSIZE(profiles))) {
        state->preview.reset();
        for (int i=0;i<5;++i) { mapping[i]=i+1; unitChoice[i]=0; }
        excluded.clear();
    }
    Json options = Json::object();
    if (profile) {
        options["profile"] = keys[profile];
        ImGui::Checkbox("Area known",&areaKnown);
        if (areaKnown) { ImGui::InputDouble("Area (mm2)",&area); options["area_m2"] = area * 1e-6; }
        ImGui::Checkbox("Confirm temperature",&temperatureKnown);
        if (temperatureKnown) { ImGui::InputDouble("Temperature (K)",&temperature); options["temperature_K"] = temperature; }
        ImGui::Checkbox("Confirm frequency",&frequencyKnown);
        if (frequencyKnown) { ImGui::InputDouble("Frequency (Hz)",&frequency); options["frequency_Hz"] = frequency; }
        ImGui::Checkbox("Confirm bias",&biasKnown);
        if (biasKnown) { ImGui::InputDouble("Bias (V)",&bias); options["bias_V"] = bias; }
        if (profile == 11) {
            ImGui::InputText("Manifest base directory (empty = manifest directory)",baseDir,sizeof(baseDir));
            if (baseDir[0]) options["base_dir"] = baseDir;
            ImGui::Combo("AS columns in manifest / project",&asVariant,"Cp / Rp\0C / G\0");
            if (asVariant >= 0) options["as_profile"] = asVariant ? "converted_as_c_g" : "lab_as_cp_rp";
        }
        if (profile == 6) {
            ImGui::Combo("Input imaginary impedance",&sign,"Im Z\0-Im Z\0");
            if (sign >= 0) options["imaginary_sign"] = sign ? "-ImZ" : "ImZ";
        }
        if (profile == 5) {
            ImGui::Combo("Amplitude convention",&amplitude,"Unknown\0peak\0RMS\0peak-to-peak\0");
            if (amplitude) options["amplitude_definition"] = amplitude == 1 ? "peak" : amplitude == 2 ? "RMS" : "peak-to-peak";
            ImGui::InputText("Bias meaning",biasDefinition,sizeof(biasDefinition));
            if (biasDefinition[0]) options["bias_definition"] = biasDefinition;
            ImGui::Checkbox("Maximum voltage condition known",&maxKnown);
            if (maxKnown) { ImGui::Checkbox("Constant maximum voltage",&maxConstant); options["constant_maximum_voltage"] = maxConstant; }
            ImGui::Checkbox("T and f in columns 4 and 5",&columnTF);
        }
        if (profile == 4) ImGui::Checkbox("Map conductance column",&optionalG);
        if (profile <= 8 && profile != 3) {
            std::vector<std::pair<std::string,std::string>> fields;
            if (profile == 4) { fields={{"V","V"},{"C","F"}}; if (optionalG) fields.push_back({"G","S"}); }
            else if (profile == 5) { fields={{"V_bias","V"},{"deltaV","V"},{"C","F"}}; if (columnTF) { fields.push_back({"temperature_K","K"}); fields.push_back({"frequency_Hz","Hz"}); } }
            else if (profile == 6) fields={{"f","Hz"},{"ReZ","Ohm"},{"ImZ","Ohm"}};
            else fields={{"f","Hz"},{"C","F"},{profile == 2 || profile == 7 ? "G" : "Rp",profile == 2 || profile == 7 ? "S" : "Ohm"}};
            for (std::size_t i=0;i<fields.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextUnformatted(fields[i].first.c_str());
                ImGui::InputInt("Source column (1 = first)",&mapping[i]);
                const auto &unit = fields[i].second;
                const std::vector<std::string> choices = unit == "Hz" ? std::vector<std::string>{"Hz","kHz","MHz"} :
                    unit == "F" ? std::vector<std::string>{"F","nF","pF"} : unit == "S" ? std::vector<std::string>{"S","mS","uS"} :
                    unit == "V" ? std::vector<std::string>{"V","mV"} : unit == "Ohm" ? std::vector<std::string>{"Ohm","kOhm"} : std::vector<std::string>{"K"};
                if (unitChoice[i] >= static_cast<int>(choices.size())) unitChoice[i]=0;
                if (ImGui::BeginCombo("Input unit",choices[unitChoice[i]].c_str())) {
                    for (std::size_t u=0;u<choices.size();++u) if (ImGui::Selectable(choices[u].c_str(),unitChoice[i] == static_cast<int>(u))) unitChoice[i]=static_cast<int>(u);
                    ImGui::EndCombo();
                }
                options["mapping"][fields[i].first] = mapping[i]-1;
                options["units"][fields[i].first] = choices[unitChoice[i]];
                ImGui::PopID();
            }
        }
        if (profile != 11) {
            ImGui::InputInt("Source row to exclude",&excludeRow);
            if (ImGui::Button("Exclude row") && excludeRow > 0 && std::find(excluded.begin(),excluded.end(),excludeRow) == excluded.end()) excluded.push_back(excludeRow);
            ImGui::SameLine(); if (ImGui::Button("Clear exclusions")) excluded.clear();
            options["exclude_rows"] = excluded;
            ImGui::Text("Excluded source rows: %s",options["exclude_rows"].dump().c_str());
        }
    }
    if (ImGui::Button("Read import preview...")) {
        state->importOptions = profile ? options.dump() : "";
        (void)commands->Execute(CommandID{"caplab.preview"});
    }
    ImGui::SameLine();
    if (ImGui::Button("Save project")) (void)commands->Execute(CommandID{"project.save"});
    if (state->preview) {
        const auto &preview = *state->preview;
        if (preview.contains("samples")) {
            ImGui::SeparatorText("Import preview");
            ImGui::TextWrapped("%s",preview.at("source").get<std::string>().c_str());
            if (preview.contains("base_dir")) ImGui::TextWrapped("Base directory: %s",preview.at("base_dir").get<std::string>().c_str());
            for (const auto &sample : preview.at("samples")) {
                ImGui::Text("%s; area=%s m2",sample.at("name").get<std::string>().c_str(),sample.at("area_m2").dump().c_str());
                for (const auto &m : sample.at("measurements")) {
                    ImGui::PushID(&m);
                    ImGui::Text("%s / %s; %zu rows; T=%s K; f=%s Hz",m.at("kind").get<std::string>().c_str(),
                        m.at("variant").get<std::string>().c_str(),m.at("source_rows").size(),
                        m.at("temperature_K").dump().c_str(),m.at("frequency_Hz").dump().c_str());
                    ImGui::TextWrapped("Header T=%s; filename T suggestion=%s. Header f=%s; filename f suggestion=%s.",
                        m.at("header_temperature_K").dump().c_str(),m.at("suggested_temperature_K").dump().c_str(),
                        m.at("header_frequency_Hz").dump().c_str(),m.at("suggested_frequency_Hz").dump().c_str());
                    ImGui::TextWrapped("Columns %s; SI units %s",m.at("names").dump().c_str(),m.at("units").dump().c_str());
                    for (const auto &diagnostic : m.at("diagnostics")) ImGui::TextWrapped("Warning: %s",diagnostic.get<std::string>().c_str());
                    if (!m.at("quality").empty()) ImGui::TextWrapped("Invalid values retained with quality flags: %s",m.at("quality").dump().c_str());
                    if (ImGui::TreeNode(m.at("source").get<std::string>().c_str())) {
                        ImGui::TextWrapped("%s",m.dump(2).c_str()); ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            if (!preview.at("historical").empty()) ImGui::TextUnformatted("Saved ProPho fits, results and selections will be imported as historical.");
            ImGui::TextWrapped("To correct metadata, mapping or units, change import fields and read preview again. Filename values are suggestions only.");
            const bool settingsChanged = state->importOptions != options.dump() || preview.value("decimal",std::string(1,state->decimal)) != std::string(1,state->decimal);
            if (ImGui::Button("Refresh preview with current fields")) {
                const Path source{preview.at("source").get<std::string>()};
                state->importOptions = options.dump(); state->Start(Operation::Preview,source);
            }
            ImGui::BeginDisabled(settingsChanged);
            if (ImGui::Button("Confirm import")) (void)commands->Execute(CommandID{"caplab.import"});
            ImGui::EndDisabled();
            ImGui::SameLine(); if (ImGui::Button("Discard preview")) state->preview.reset();
        } else {
        ImGui::SeparatorText("Import preview: lab Cp/Rp (SI)");
        ImGui::TextWrapped("%s", preview.at("source").get<std::string>().c_str());
        ImGui::TextUnformatted("Columns: f [Hz], Cp [F], Rp [Ohm]; additional columns kept as source data.");
        ImGui::Text("Header T: %s K; filename suggestion: %s K", preview.at("temperature_K").dump().c_str(),
            preview.at("suggested_temperature_K").dump().c_str());
        ImGui::TextUnformatted("Confirm temperature below; filename is only a suggestion.");
        bool known = state->temperature.has_value();
        if (ImGui::Checkbox("Temperature known", &known)) state->temperature = known ? std::optional<double>{300} : std::nullopt;
        if (state->temperature) ImGui::InputDouble("Temperature (K)", &*state->temperature, 0, 0, "%.6g");
        if (state->temperature && !preview.at("temperature_K").is_null() && *state->temperature != preview.at("temperature_K").get<double>())
            ImGui::TextWrapped("Temperature conflict: confirmation will override header T.");
        const auto columns = preview.at("columns").get<std::vector<std::vector<double>>>();
        ImGui::Text("%zu points; %zu columns", columns[0].size(), columns.size());
        if (ImGui::BeginTable("preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            for (auto name : {"Source row", "f (Hz)", "Cp (F)", "Rp (Ohm)"}) ImGui::TableSetupColumn(name);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(columns[0].size()));
            while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::Text("%zu", preview.at("source_rows").at(row).get<std::size_t>());
                for (int c = 0; c < 3; ++c) { ImGui::TableNextColumn(); ImGui::Text("%.8g", columns[c][row]); }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Confirm import")) (void)commands->Execute(CommandID{"caplab.import"});
        ImGui::SameLine(); if (ImGui::Button("Discard preview")) state->preview.reset();
    }
    }
    for (const auto &sample : doc.project.samples) {
        ImGui::PushID(static_cast<int>(sample.id));
        if (ImGui::TreeNode(sample.name.c_str())) {
            for (const auto &sweep : doc.project.sweeps) if (sweep.sample_id == sample.id)
                ImGui::Text("Cp/Rp: %zu points; T=%s K", std::get<pl::Worksheet>(doc.project.engine->object(sweep.id).data).rows.size(), Nullable(sweep.temperature_K).dump().c_str());
            ImGui::Text("Area: %s m2",Nullable(sample.area_m2).dump().c_str());
            for (const auto &m : doc.project.measurements) if (m.at("sample_id") == sample.id)
                ImGui::Text("%s / %s: %zu points; T=%s K",m.at("metadata").at("kind").get<std::string>().c_str(),
                    m.at("metadata").at("variant").get<std::string>().c_str(),
                    std::get<pl::Worksheet>(doc.project.engine->object(m.at("id").get<pl::Id>()).data).rows.size(),
                    m.at("metadata").at("temperature_K").dump().c_str());
            for (const auto &h : doc.project.historical) if (h.at("sample_id") == sample.id)
                if (ImGui::TreeNode("Historical ProPho results and selections")) {
                    ImGui::TextWrapped("%s",h.at("data").dump(2).c_str()); ImGui::TreePop();
                }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (doc.project.graph) {
        const auto &p = *doc.project.engine;
        const auto layer = p.children(doc.project.graph).at(0);
        std::vector<sst::ui::plot::SeriesEntryView> series;
        for (auto id : p.children(layer)) series.push_back(p.series(id));
        sst::ui::plot::RenderPlot("##cf", std::get<pl::Layer>(p.object(layer).data).spec,
            series.data(), series.size(), state->view, {}, ImVec2(-1, 260));
        if (ImGui::Button("Export C-f PNG...")) (void)commands->Execute(CommandID{"caplab.export"});
    }
    ImGui::TextWrapped("%s", state->status.c_str());
    if (const auto job = state->jobs->GetJob(state->activeJob)) {
        ImGui::TextUnformatted(job->currentStage.c_str());
        ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
        if (ImGui::Button("Cancel")) (void)state->jobs->RequestCancel(job->id);
    }
    ImGui::End();
}
}
