#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include <imgui.h>
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <cstdlib>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderImport() {
    auto &doc = state->document;
    ImGui::Text("%s%s", doc.path.Empty() ? "Untitled" : doc.path.Utf8().c_str(), doc.dirty ? " (unsaved)" : "");
    ImGui::SeparatorText("Import settings and metadata");
    bool comma = state->decimal == ',';
    if (sst::ui::widgets::Checkbox("Decimal comma (semicolon/whitespace columns)", &comma)) state->decimal = comma ? ',' : '.';
    static int profile = 1;
    const char *profiles[] = {"Lab AS Cp/Rp (SI)", "Lab AS Cp/Rp (mapped)", "Converted AS C/G", "ProPho DL/CV", "Lab CV", "Lab DLCP raw", "Lab IS Z", "Lab IS C/G", "Lab IS Cp/Rp", "SCAPS .cf", "SCAPS .cv", "ProPho manifest / project"};
    const char *keys[] = {"", "lab_as_cp_rp", "converted_as_c_g", "propho_dl", "lab_cv", "lab_dlcp_raw", "lab_is_z", "lab_is_c_g", "lab_is_cp_rp", "scaps_cf", "scaps_cv", "propho_json"};
    if (state->preview && !state->importOptions.empty()) {
        const auto importedProfile=Json::parse(state->importOptions).value("profile",std::string{});
        for (int i=1;i<IM_ARRAYSIZE(keys);++i) if (importedProfile==keys[i]) profile=i;
    }
    static int mapping[5] = {1,2,3,4,5}, unitChoice[5] = {}, sign = -1, amplitude = 0, asVariant = -1;
    static bool areaKnown = false, temperatureKnown = false, frequencyKnown = false, biasKnown = false, optionalG = false, columnTF = false, maxKnown = false, maxConstant = false;
    static double area = 0, temperature = 0, frequency = 0, bias = 0;
    static char baseDir[1024] = "", biasDefinition[256] = "";
    static int excludeRow = 0;
    static std::vector<int> excluded;
    if (sst::ui::widgets::Combo("Import profile", &profile, profiles, IM_ARRAYSIZE(profiles))) {
        state->ClearPreview();
        for (int i=0;i<5;++i) { mapping[i]=i+1; unitChoice[i]=0; }
        excluded.clear();
    }
    Json options = Json::object();
    if (profile) {
        options["profile"] = keys[profile];
        sst::ui::widgets::Checkbox("Area known", &areaKnown);
        if (areaKnown) { sst::ui::widgets::Double("Area (mm2)", &area, .01f, "%.6g"); options["area_m2"] = area * 1e-6; }
        sst::ui::widgets::Checkbox("Confirm temperature", &temperatureKnown);
        if (temperatureKnown) { sst::ui::widgets::Double("Temperature (K)", &temperature, .01f, "%.6g"); options["temperature_K"] = temperature; }
        sst::ui::widgets::Checkbox("Confirm frequency", &frequencyKnown);
        if (frequencyKnown) { sst::ui::widgets::LogValue("Frequency (Hz)", &frequency); options["frequency_Hz"] = frequency; }
        sst::ui::widgets::Checkbox("Confirm bias", &biasKnown);
        if (biasKnown) { sst::ui::widgets::Double("Bias (V)", &bias, .01f, "%.6g"); options["bias_V"] = bias; }
        if (profile == 11) {
            sst::ui::widgets::Text("Manifest base directory (empty = manifest directory)", baseDir, sizeof(baseDir));
            if (baseDir[0]) options["base_dir"] = baseDir;
            sst::ui::widgets::Combo("AS columns in manifest / project", &asVariant, "Cp / Rp\0C / G\0");
            if (asVariant >= 0) options["as_profile"] = asVariant ? "converted_as_c_g" : "lab_as_cp_rp";
        }
        if (profile == 6) {
            sst::ui::widgets::Combo("Input imaginary impedance", &sign, "Im Z\0-Im Z\0");
            if (sign >= 0) options["imaginary_sign"] = sign ? "-ImZ" : "ImZ";
        }
        if (profile == 5) {
            sst::ui::widgets::Combo("Amplitude convention", &amplitude, "Unknown\0peak\0RMS\0peak-to-peak\0");
            if (amplitude) options["amplitude_definition"] = amplitude == 1 ? "peak" : amplitude == 2 ? "RMS" : "peak-to-peak";
            sst::ui::widgets::Text("Bias meaning", biasDefinition, sizeof(biasDefinition));
            if (biasDefinition[0]) options["bias_definition"] = biasDefinition;
            sst::ui::widgets::Checkbox("Maximum voltage condition known", &maxKnown);
            if (maxKnown) { sst::ui::widgets::Checkbox("Constant maximum voltage", &maxConstant); options["constant_maximum_voltage"] = maxConstant; }
            sst::ui::widgets::Checkbox("T and f in columns 4 and 5", &columnTF);
        }
        if (profile == 4) sst::ui::widgets::Checkbox("Map conductance column", &optionalG);
        if (profile <= 8 && profile != 3) {
            std::vector<std::pair<std::string,std::string>> fields;
            if (profile == 4) { fields={{"V","V"},{"C","F"}}; if (optionalG) fields.push_back({"G","S"}); }
            else if (profile == 5) { fields={{"V_bias","V"},{"deltaV","V"},{"C","F"}}; if (columnTF) { fields.push_back({"temperature_K","K"}); fields.push_back({"frequency_Hz","Hz"}); } }
            else if (profile == 6) fields={{"f","Hz"},{"ReZ","Ohm"},{"ImZ","Ohm"}};
            else fields={{"f","Hz"},{"C","F"},{profile == 2 || profile == 7 ? "G" : "Rp",profile == 2 || profile == 7 ? "S" : "Ohm"}};
            for (std::size_t i=0;i<fields.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Spacing(); ImGui::SeparatorText(fields[i].first.c_str());
                sst::ui::widgets::Int("Source column (1 = first)", &mapping[i]);
                const auto &unit = fields[i].second;
                const std::vector<std::string> choices = unit == "Hz" ? std::vector<std::string>{"Hz","kHz","MHz"} :
                    unit == "F" ? std::vector<std::string>{"F","nF","pF"} : unit == "S" ? std::vector<std::string>{"S","mS","uS"} :
                    unit == "V" ? std::vector<std::string>{"V","mV"} : unit == "Ohm" ? std::vector<std::string>{"Ohm","kOhm"} : std::vector<std::string>{"K"};
                if (unitChoice[i] >= static_cast<int>(choices.size())) unitChoice[i]=0;
                auto unitLabel = [](const std::string &value) { return value == "Ohm" ? "Ω" : value == "kOhm" ? "kΩ" : value.c_str(); };
                if (sst::ui::widgets::BeginCombo("Input unit", unitLabel(choices[unitChoice[i]]))) {
                    for (std::size_t u=0;u<choices.size();++u) if (ImGui::Selectable(unitLabel(choices[u]),unitChoice[i] == static_cast<int>(u))) unitChoice[i]=static_cast<int>(u);
                    sst::ui::widgets::EndCombo();
                }
                options["mapping"][fields[i].first] = mapping[i]-1;
                options["units"][fields[i].first] = choices[unitChoice[i]];
                ImGui::PopID();
            }
        }
        if (profile != 11) {
            sst::ui::widgets::Int("Source row to exclude", &excludeRow);
            if (ImGui::Button("Exclude row") && excludeRow > 0 && std::find(excluded.begin(),excluded.end(),excludeRow) == excluded.end()) excluded.push_back(excludeRow);
            ImGui::SameLine(); if (ImGui::Button("Clear exclusions")) excluded.clear();
            options["exclude_rows"] = excluded;
            ImGui::Text("Excluded source rows: %s",options["exclude_rows"].dump().c_str());
        }
    }
    if (ImGui::Button("Read import preview...")) {
        state->importOptions = profile ? options.dump() : "";
        CommandContext c; c.Set("recursive",state->recursiveImport);
        (void)commands->Execute(CommandID{"caplab.preview"},std::move(c));
    }
    ImGui::SameLine();
    if (ImGui::Button("Read folders...")) {
        state->importOptions=profile ? options.dump() : "";
        CommandContext c; c.Set("recursive",state->recursiveImport);
        (void)commands->Execute(CommandID{"caplab.preview_folder"},std::move(c));
    }
    sst::ui::widgets::Checkbox("Include subfolders", &state->recursiveImport);
    ImGui::TextUnformatted("Ctrl/Shift + click selects several sources. Confirm imports the selected sources as one Undo step.");
    if (ImGui::Button("Save project")) (void)commands->Execute(CommandID{"project.save"});
    auto renderDetails = [&] {
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
            auto previousOptions=Json::parse(state->importOptions.empty() ? "{}" : state->importOptions);
            if (profile!=11 && !previousOptions.contains("exclude_rows")) previousOptions["exclude_rows"]=Json::array();
            const bool settingsChanged = previousOptions != options || preview.value("decimal",std::string(1,state->decimal)) != std::string(1,state->decimal);
            if (ImGui::Button("Refresh preview with current fields")) {
                const Path source{preview.at("source").get<std::string>()};
                state->importOptions = options.dump(); state->Start(Operation::Preview,{},state->previewInputs.empty() ? std::vector<Path>{source} : state->previewInputs,state->previewRecursive);
            }
            ImGui::BeginDisabled(settingsChanged);
            if (ImGui::Button("Confirm import")) (void)commands->Execute(CommandID{"caplab.import"});
            ImGui::EndDisabled();
            ImGui::SameLine(); if (ImGui::Button("Discard preview")) state->ClearPreview();
        } else {
        ImGui::SeparatorText("Import preview: lab Cp/Rp (SI)");
        ImGui::TextWrapped("%s", preview.at("source").get<std::string>().c_str());
        ImGui::TextUnformatted("Columns: f [Hz], Cp [F], Rp [Ω]; additional columns kept as source data.");
        ImGui::Text("Header T: %s K; filename suggestion: %s K", preview.at("temperature_K").dump().c_str(),
            preview.at("suggested_temperature_K").dump().c_str());
        ImGui::TextUnformatted("Confirm temperature below; filename is only a suggestion.");
        bool known = state->temperature.has_value();
        if (sst::ui::widgets::Checkbox("Temperature known", &known)) state->temperature = known ? std::optional<double>{300} : std::nullopt;
        if (state->temperature) sst::ui::widgets::Double("Temperature (K)", &*state->temperature, .01f, "%.6g");
        if (state->temperature && !preview.at("temperature_K").is_null() && *state->temperature != preview.at("temperature_K").get<double>())
            ImGui::TextWrapped("Temperature conflict: confirmation will override header T.");
        const auto columns = preview.at("columns").get<std::vector<std::vector<double>>>();
        ImGui::Text("%zu points; %zu columns", columns[0].size(), columns.size());
        if (ImGui::BeginTable("preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            for (auto name : {"Source row", "f (Hz)", "Cp (F)", "Rp (Ω)"}) ImGui::TableSetupColumn(name);
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
        ImGui::SameLine(); if (ImGui::Button("Discard preview")) state->ClearPreview();
    }
    }
    };
    if (state->sourcePreviews.empty()) renderDetails();
    else {
        std::vector<ImportSourceRow> rows;
        for (const auto &item : state->sourcePreviews)
            rows.push_back({ImportSourceLabel(item.source),item.source.path.Utf8(),item.error ? item.error->technicalDetails : std::string{}});
        int focused=state->previewSourceIndex;
        RenderImportSources(rows,state->selectedSources,focused,[&](int index) {
            state->SelectPreview(index);
            if (state->sourcePreviews[index].error) ImGui::TextWrapped("%s",state->sourcePreviews[index].error->technicalDetails.c_str());
            renderDetails();
        });
    }
}
void CapLabPrototypePanel::RenderTree() {
    auto &doc = state->document;
    ImGui::TextWrapped("%s", doc.path.Empty() ? "Untitled" : doc.path.filename().Utf8().c_str());
    if (ImGui::Button("Save project")) (void)commands->Execute(CommandID{"project.save"});
    for (const auto &sample : doc.project.samples) {
        ImGui::PushID(static_cast<int>(sample.id));
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNode(sample.name.c_str())) {
            for (const auto &sweep : doc.project.sweeps) if (sweep.sample_id == sample.id)
                ImGui::TextWrapped("Cp (F) / Rp (Ω): %zu points; T=%s K", std::get<pl::Worksheet>(doc.project.engine->object(sweep.id).data).rows.size(), Nullable(sweep.temperature_K).dump().c_str());
            auto &areaMm2 = areaDraft.try_emplace(sample.id, sample.area_m2.value_or(0)*1e6).first->second;
            sst::ui::widgets::Double("Sample area (mm2)", &areaMm2, .01f, "%.9g");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                CommandContext c; c.Set("sample_id",sample.id); c.Set("area_m2",areaMm2*1e-6);
                (void)commands->Execute(CommandID{"caplab.area"},std::move(c));
                ImGui::TreePop(); ImGui::PopID(); return;
            }
            if (!ImGui::IsItemActive()) areaMm2 = sample.area_m2.value_or(0)*1e6;
            if (!sample.area_m2) ImGui::TextDisabled("unknown - enter sample area");
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
    if (ImGui::CollapsingHeader("Delete measurement (Undo)")) {
        std::vector<pl::Id> ids;
        for (const auto &m : doc.project.measurements) ids.push_back(m.at("id").get<pl::Id>());
        for (const auto &m : doc.project.sweeps) ids.push_back(m.id);
        for (auto id : ids) if (ImGui::Button(("Delete measurement "+std::to_string(id)).c_str())) {
            CommandContext c; c.Set("edit",Json{{"measurement_id",id}});
            (void)commands->Execute(CommandID{"caplab.measurement"},std::move(c)); return;
        }
    }
}
void CapLabPrototypePanel::RenderPlot() {
    auto &doc = state->document;
    if (doc.project.graph) {
        for (const auto& a : doc.project.analyses) if (a.at("type")=="Circuit") {
            const auto& graphs=a.at("graphs");
            if (std::find(graphs.begin(),graphs.end(),Json(doc.project.graph))!=graphs.end()) {
                RenderISTabs(doc.project,a,state->view);
                if (ImGui::Button("Export CSV / XLSX / PNG...")) (void)commands->Execute(CommandID{"caplab.export"});
                return;
            }
        }
        const auto &p = *doc.project.engine;
        auto window=GetState();
        const auto& title=p.object(doc.project.graph).name;
        if (window.shortName!=title) {window.shortName=title; window.longName=title; ApplyState(window);}
        const auto layer = p.children(doc.project.graph).at(0);
        std::vector<sst::ui::plot::SeriesEntryView> series;
        for (auto id : p.children(layer)) {
            auto entry = p.series(id);
            entry.id = p.object(id).name + "###" + entry.id;
            series.push_back(std::move(entry));
        }
        sst::ui::plot::RenderPlot("##cf", std::get<pl::Layer>(p.object(layer).data).spec,
            series.data(), series.size(), state->view, {}, ImVec2(-1, std::max(200.0f, ImGui::GetContentRegionAvail().y - 40)));
        if (state->view.picked && state->view.picked->series==0) {
            for (const auto &a : doc.project.analyses) if (a.at("type")=="AS" && a.at("status")=="valid" &&
                a.at("graphs").at(1)==doc.project.graph && state->view.picked->point<a.at("candidates").size())
                {state->pendingCandidate=a.at("candidates").at(state->view.picked->point); state->pendingCandidate["analysis_id"]=a.at("id");}
            state->view.picked.reset();
        }
        if (ImGui::Button("Export CSV / XLSX / PNG...")) (void)commands->Execute(CommandID{"caplab.export"});
    }
}
void CapLabPrototypePanel::RenderContent() {
    switch (view) {
    case View::Project: RenderTree(); break;
    case View::Plot: RenderPlot(); break;
    case View::Analysis:
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8,6));
        RenderWalter(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        RenderIS(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        RenderAS(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); RenderAnalysis();
        ImGui::PopStyleVar(); break;
    case View::Import: RenderImport(); break;
    }
    ImGui::Spacing(); ImGui::SeparatorText("Status");
    if (state->failure) ImGui::TextWrapped("%s",state->failure->code.c_str());
    ImGui::TextWrapped("%s", state->status.c_str());
    if (const auto job = state->jobs->GetJob(state->activeJob)) {
        ImGui::TextUnformatted(job->currentStage.c_str());
        ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
        if (ImGui::Button("Cancel")) (void)commands->Execute(CommandID{"caplab.cancel"});
    }
}

}
#include <sst/physics/materials.hpp>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderAnalysis() {
    if (!ImGui::CollapsingHeader("CV / raw DLCP analysis",ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto &p = state->document.project;
    static pl::Id selected = 0, xdSelected = 0;
    auto select = [&](const char *label, pl::Id &id, const char *empty = "none — select") {
        if (sst::ui::widgets::BeginCombo(label, SelectionLabel(p,id,empty).c_str())) {
            for (const auto &m : p.measurements) {
                const auto mid=m.at("id").get<pl::Id>();
                const std::string title=SelectionLabel(p,mid)+"##"+std::to_string(mid);
                if (ImGui::Selectable(title.c_str(),id==mid)) id=mid;
            }
            sst::ui::widgets::EndCombo();
        }
    };
    select("Measurement",selected);
    if (!selected) return;
    try {
        const auto m = AnalysisMeasurement(p,selected);
        static int type=0, degree=1, materialChoice=-1, band=0, source=0, policy=0;
        static double lo=-.3, hi=.35, ampLo=0, ampHi=1, manual=1, bias=0, eps=0, ref=0;
        static char description[256]="", materialSource[256]="";
        static std::set<pl::Id> masked;
        static pl::Id maskMeasurement=0;
        if (maskMeasurement != selected) { masked.clear(); maskMeasurement=selected; }
        sst::ui::widgets::Combo("Analysis", &type, "CV\0Raw DLCP\0");
        Json options{{"measurement_id",selected},{"type",type ? "DLCPPolynomial" : "CV"}};
        if (!type) {
            sst::ui::widgets::Double("Fit minimum voltage (V)", &lo, .01f, "%.6g"); sst::ui::widgets::Double("Fit maximum voltage (V)", &hi, .01f, "%.6g");
            options["range_V"]={lo,hi};
        } else {
            sst::ui::widgets::Combo("Polynomial degree", &degree, "1\0 2\0"); options["degree"]=degree+1;
            sst::ui::widgets::Double("Minimum peak amplitude (V)", &ampLo, .01f, "%.6g"); sst::ui::widgets::Double("Maximum peak amplitude (V)", &ampHi, .01f, "%.6g");
            options["range_amplitude_V"]={ampLo,ampHi};
        }
        const sst::physics::MaterialLibrary library;
        std::vector<std::string> names{"ProPho-compat","project"};
        for (const auto &v : library.list()) if (v.eps_r) names.push_back(v.name);
        if (sst::ui::widgets::BeginCombo("Material", materialChoice < 0 ? "Select material" : names.at(materialChoice).c_str())) {
            for (std::size_t i=0;i<names.size();++i) if (ImGui::Selectable(names[i].c_str(),materialChoice==static_cast<int>(i))) materialChoice=static_cast<int>(i);
            sst::ui::widgets::EndCombo();
        }
        if (materialChoice>=0) {
            options["material"]=names.at(materialChoice);
            sst::ui::widgets::Combo("Reference band", &band, "Conduction\0Valence\0"); options["reference_band"]=band ? "valence" : "conduction";
            if (materialChoice==0) ImGui::TextUnformatted("Owner preset: eps_r=10; N_ref=1.24e19 cm^-3 (constant)");
            if (materialChoice==1) {
                sst::ui::widgets::Double("Relative permittivity [1]###Relative permittivity", &eps, .01f, "%.6g"); sst::ui::widgets::LogValue("N_ref (cm^-3, 0 = unavailable)", &ref);
                sst::ui::widgets::Text("Material source", materialSource, sizeof(materialSource));
                options["project_material"]={{"eps_r",eps},{"N_ref_m3",ref>0 ? Json(ref*1e6) : Json(nullptr)},
                    {"source",materialSource},{"reference_band",band ? "valence" : "conduction"}};
            }
        }
        sst::ui::widgets::Combo("x_d source", &source, "CV at AS bias\0DLCP at V=0\0Manual\0");
        sst::ui::widgets::Combo("Missing point policy", &policy, "Block\0Linear interpolation (no extrapolation)\0Nearest point (warning)\0");
        options["xd_source"]=source==0 ? "CV" : source==1 ? "DLCP" : "manual";
        options["xd_policy"]=policy==0 ? "block" : policy==1 ? "linear" : "nearest";
        if (source==2) {
            sst::ui::widgets::Double("Manual x_d (um)", &manual, .01f, "%.6g"); sst::ui::widgets::Text("x_d description", description, sizeof(description));
            options["xd_manual_m"]=manual*1e-6; options["xd_description"]=description;
        } else {
            select("x_d measurement (0 = current)",xdSelected,"current measurement");
            if (ImGui::Button("Use current measurement for x_d")) xdSelected=0;
            if (xdSelected) options["xd_measurement_id"]=xdSelected;
            if (source==0) { sst::ui::widgets::Double("AS bias (V)", &bias, .01f, "%.6g"); options["as_bias_V"]=bias; }
        }
        if (ImGui::TreeNode("Point mask (unchecked = excluded)")) {
            for (const auto &row : m.at("row_ids")) {
                const auto id=row.get<pl::Id>(); bool included=!masked.contains(id);
                if (sst::ui::widgets::Checkbox(std::to_string(id).c_str(), &included)) { if (included) masked.erase(id); else masked.insert(id); }
            }
            ImGui::TreePop();
        }
        options["excluded_row_ids"]=masked;
        if (ImGui::Button("Fit CV / DLCP")) {
            state->analysisOptions=options; (void)commands->Execute(CommandID{"caplab.analyze"});
        }
        for (const auto &a : p.analyses) {
            if (a.at("type")!="CV" && a.at("type")!="DLCPPolynomial") continue;
            ImGui::PushID(static_cast<int>(a.at("id").get<pl::Id>()));
            if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
                for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s",d.get<std::string>().c_str());
                if (a.contains("V_b_V")) ImGui::Text("V_b=%s V; N=%s m^-3; E_F=%s eV",a.at("V_b_V").dump().c_str(),a.at("N_m3").dump().c_str(),a.at("E_F_eV").dump().c_str());
                ImGui::TextWrapped("x_d: %s",a.at("x_d").dump().c_str());
                if (ImGui::TreeNode("Coefficients / covariance / residuals / provenance")) {
                    auto detail=a; detail.erase("inputs"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();
                }
                for (const auto &g : a.at("graphs")) {
                    const auto id=g.get<pl::Id>();
                    if (ImGui::Button((p.engine->object(id).name+"##"+std::to_string(id)).c_str())) { p.graph=id; state->view={}; state->document.Changed(); }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    } catch (const std::exception &e) { ImGui::TextWrapped("%s",e.what()); }
}
}

namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderAS() {
    if (!ImGui::CollapsingHeader("AS / defects / Arrhenius",ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto &p=state->document.project;
    static pl::Id sample=0, measurement=0;
    static char name[128]="Defect", provenance[256]="Manual point";
    static double manualT=300, manualF=1000, gamma=0;
    static char gammaSource[256]="";
    static bool useGamma=false;
    static bool smoothing=false, compare=true, compat=false;
    static int window=5, degree=3;
    auto edit=[&](Json value) {
        CommandContext context; context.Set("edit",std::move(value));
        (void)commands->Execute(CommandID{"caplab.defect"},std::move(context));
    };
    if (sst::ui::widgets::BeginCombo("AS sample", SelectionLabel(p,sample).c_str())) {
        for (const auto &s : p.samples) if (ImGui::Selectable((SelectionLabel(p,s.id)+"##"+std::to_string(s.id)).c_str(),sample==s.id)) {sample=s.id; selectedDefect=0; state->pendingCandidate=nullptr;}
        sst::ui::widgets::EndCombo();
    }
    sst::ui::widgets::Text("Defect name", name, sizeof(name));
    if (ImGui::Button("Create defect")) {edit({{"action","create"},{"sample_id",sample},{"name",name}}); return;}
    if (sst::ui::widgets::BeginCombo("AS measurement", SelectionLabel(p,measurement).c_str())) {
        for (const auto &m : p.measurements) if (m.at("sample_id")==sample && m.at("metadata").at("kind")=="FrequencySweep") {
            const auto id=m.at("id").get<pl::Id>();
            if (ImGui::Selectable((SelectionLabel(p,id)+"##"+std::to_string(id)).c_str(),measurement==id)) measurement=id;
        }
        for (const auto &m : p.sweeps) if (m.sample_id==sample)
            if (ImGui::Selectable((SelectionLabel(p,m.id)+"##"+std::to_string(m.id)).c_str(),measurement==m.id)) measurement=m.id;
        sst::ui::widgets::EndCombo();
    }
    sst::ui::widgets::Checkbox("Savitzky-Golay smoothing", &smoothing);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sample index; derivative uses ln f.");
    if (smoothing) {sst::ui::widgets::Int("Smoothing window (odd)", &window); sst::ui::widgets::Int("Smoothing degree", &degree);}
    if (ImGui::Button("Calculate AS derivative / candidates")) {
        state->analysisOptions={{"type","AS"},{"measurement_id",measurement},
            {"smoothing",smoothing ? Json{{"method","Savitzky-Golay"},{"window",window},{"degree",degree}} : Json{{"method","none"}}}};
        (void)commands->Execute(CommandID{"caplab.analyze"});
    }
    if (sst::ui::widgets::BeginCombo("Active defect", SelectionLabel(p,selectedDefect,"no defects yet").c_str())) {
        for (const auto &d : p.defects) if (d.at("sample_id")==sample)
            if (ImGui::Selectable((SelectionLabel(p,d.at("id").get<pl::Id>())+"##"+std::to_string(d.at("id").get<pl::Id>())).c_str(),d.at("id")==selectedDefect)) selectedDefect=d.at("id").get<pl::Id>();
        sst::ui::widgets::EndCombo();
    }
    if (selectedDefect) {
        if (ImGui::Button("Rename defect")) {edit({{"action","rename"},{"defect_id",selectedDefect},{"name",name}}); return;}
        ImGui::SameLine();
        if (ImGui::Button("Delete defect")) {edit({{"action","delete"},{"defect_id",selectedDefect}}); selectedDefect=0; return;}
        sst::ui::widgets::Double("Manual T (K)", &manualT, .01f, "%.6g"); sst::ui::widgets::LogValue("Manual f (Hz)", &manualF);
        sst::ui::widgets::Text("Point provenance", provenance, sizeof(provenance));
        if (ImGui::Button("Add manual point")) {
            edit({{"action","add"},{"defect_id",selectedDefect},{"point",{{"measurement_id",nullptr},{"row_id",nullptr},
                {"temperature_K",manualT},{"frequency_Hz",manualF},{"provenance",provenance}}}}); return;
        }
        if (!state->pendingCandidate.is_null()) {
            ImGui::Text("Candidate: T=%s K, f=%s Hz",state->pendingCandidate.at("temperature_K").dump().c_str(),state->pendingCandidate.at("frequency_Hz").dump().c_str());
            if (ImGui::Button("Assign candidate to active defect")) {
                edit({{"action","add"},{"defect_id",selectedDefect},{"point",state->pendingCandidate}}); state->pendingCandidate=nullptr; return;
            }
        }
        // Edits replace the document; return immediately to avoid invalidating iterators.
        for (const auto &d : p.defects) if (d.at("id")==selectedDefect) for (const auto &point : d.at("points")) {
            const auto id=point.at("id").get<pl::Id>(); ImGui::PushID(static_cast<int>(id));
            bool enabled=point.at("enabled");
            if (sst::ui::widgets::Checkbox("Enabled", &enabled)) {edit({{"action","enable"},{"defect_id",selectedDefect},{"point_id",id},{"enabled",enabled}}); ImGui::PopID(); return;}
            ImGui::Text("%s K / %s Hz",point.at("temperature_K").dump().c_str(),point.at("frequency_Hz").dump().c_str());
            ImGui::SameLine(); if (ImGui::Button("Remove")) {edit({{"action","remove"},{"defect_id",selectedDefect},{"point_id",id}}); ImGui::PopID(); return;}
            ImGui::PopID();
        }
        sst::ui::widgets::Checkbox("Compare both Arrhenius models", &compare);
        if (!compare) sst::ui::widgets::Checkbox("Use ProPho-compat ln(f)", &compat);
        sst::ui::widgets::Checkbox("Explicit gamma for default-model sigma", &useGamma);
        if (useGamma) {
            sst::ui::widgets::LogValue("gamma (cm^-2 s^-1 K^-2)", &gamma);
            sst::ui::widgets::Text("gamma source", gammaSource, sizeof(gammaSource));
        }
        if (ImGui::Button("Fit Arrhenius")) {
            state->analysisOptions={{"type","Arrhenius"},{"defect_id",selectedDefect},
                {"models",compare ? Json{"omega-T2","ProPho-compat"} : Json{compat ? "ProPho-compat" : "omega-T2"}}};
            if (useGamma) {state->analysisOptions["gamma_cm_2_s_K_2"]=gamma; state->analysisOptions["gamma_source"]=gammaSource;}
            (void)commands->Execute(CommandID{"caplab.analyze"});
        }
    }
    ImGui::TextWrapped("Candidates require approval. Select a candidate below or Ctrl+click its marker on the D-f graph (screen distance, 10 px). Default fit uses omega=2*pi*f; sigma needs explicit gamma. ProPho-compat uses Ncv=2e18 cm^-3 and vth=1e7 cm/s.");
    for (const auto &a : p.analyses) if (a.at("type")=="AS" || a.at("type")=="Arrhenius") {
        ImGui::PushID(static_cast<int>(a.at("id").get<pl::Id>()));
        if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
            if (a.at("type")=="AS") for (const auto &c : a.at("candidates")) {
                ImGui::PushID(static_cast<int>(c.at("row_id").get<pl::Id>()));
                if (a.at("status")=="valid" && ImGui::Selectable((c.at("temperature_K").dump()+" K / "+c.at("frequency_Hz").dump()+" Hz").c_str())) {state->pendingCandidate=c; state->pendingCandidate["analysis_id"]=a.at("id");}
                ImGui::PopID();
            }
            if (a.contains("fits")) for (const auto &fit : a.at("fits"))
                ImGui::TextWrapped("%s: Ea=%s eV; prefactor=%s %s; sigma=%s cm^2; dof=%d",fit.at("model").get<std::string>().c_str(),
                    fit.at("E_a_eV").dump().c_str(),fit.at("prefactor").dump().c_str(),fit.at("prefactor_unit").get<std::string>().c_str(),fit.at("sigma_cm2").dump().c_str(),fit.at("dof").get<int>());
            for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s",d.get<std::string>().c_str());
            for (const auto &g : a.at("graphs")) {
                auto id=g.get<pl::Id>();
                if (ImGui::Button((p.engine->object(id).name+"##"+std::to_string(id)).c_str())) {p.graph=id; state->view={}; state->document.Changed();}
            }
            if (ImGui::TreeNode("AS / fit details and provenance")) {auto detail=a; detail.erase("inputs"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();}
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
}

namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderWalter() {
    if (const auto gallery = std::getenv("SST_CAPLAB_GALLERY_SECTION"))
        ImGui::SetNextItemOpen(std::string_view(gallery) == "Walter", ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Walter spectra / integration")) return;
    ImGui::PushID("Walter");
    auto &p=state->document.project;
    static pl::Id sample=0, cv=0, arrhenius=0, dlcp=0, xdMeasurement=0;
    static std::set<pl::Id> selected;
    static int model=0, source=0, policy=0;
    static double vb=0, ef=0, prefactor=0, width=1, bias=0, tlo=1, thi=500, flo=100, fhi=1e6, elo=0, ehi=1, ilo=.1, ihi=.5;
    static char provenance[256]="", emissionSource[256]="", description[256]="";
    if (sst::ui::widgets::BeginCombo("Sample", std::to_string(sample).c_str())) {
        for (const auto &s : p.samples) if (ImGui::Selectable(s.name.c_str(),s.id==sample)) {sample=s.id; selected.clear(); cv=arrhenius=dlcp=xdMeasurement=0;}
        sst::ui::widgets::EndCombo();
    }
    sst::ui::widgets::Combo("Model", &model, "Walter1996-linear-field\0ProPho-compat\0");
    Json options{{"type","Walter"},{"model",model ? "ProPho-compat" : "Walter1996-linear-field"}};
    for (const auto &m : p.measurements) if (m.at("sample_id")==sample && m.at("metadata").at("kind")=="FrequencySweep") {
        const auto id=m.at("id").get<pl::Id>(); bool enabled=selected.contains(id);
        const std::string label=std::to_string(id)+" / "+m.at("metadata").at("temperature_K").dump()+" K";
        if (sst::ui::widgets::Checkbox(label.c_str(), &enabled)) {if (enabled) selected.insert(id); else selected.erase(id);}
    }
    for (const auto &m : p.sweeps) if (m.sample_id==sample) {
        bool enabled=selected.contains(m.id);
        if (sst::ui::widgets::Checkbox((std::to_string(m.id)+" / AS").c_str(), &enabled)) {if (enabled) selected.insert(m.id); else selected.erase(m.id);}
    }
    auto chooseFit=[&](const char *label, const char *type, pl::Id &value) {
        if (sst::ui::widgets::BeginCombo(label, value ? std::to_string(value).c_str() : "Manual")) {
            if (ImGui::Selectable("Manual",value==0)) value=0;
            for (const auto &a : p.analyses) if (a.at("type")==type && a.at("status")=="valid") {
                const auto sid=a.contains("sample_id") ? a.at("sample_id") : a.at("inputs").at(0).at("sample_id");
                if (sid==sample && ImGui::Selectable(std::to_string(a.at("id").get<pl::Id>()).c_str(),a.at("id")==value)) value=a.at("id");
            }
            sst::ui::widgets::EndCombo();
        }
    };
    chooseFit("CV fit", "CV",cv);
    if (cv) options["cv_fit_id"]=cv;
    else {sst::ui::widgets::Double("V_b (V)", &vb, .01f, "%.6g"); sst::ui::widgets::Double("E_F infinity (eV)", &ef, .01f, "%.6g"); options["V_b_V"]=vb; options["E_F_eV"]=ef;}
    if (!model) chooseFit("Arrhenius / defect fit", "Arrhenius",arrhenius);
    if (!model && arrhenius) options["arrhenius_fit_id"]=arrhenius;
    else {sst::ui::widgets::LogValue(model ? "B (s^-1)" : "zeta (s^-1 K^-2)", &prefactor); options[model ? "B_s_1" : "zeta_s_1_K_2"]=prefactor;}
    sst::ui::widgets::Text("Parameter source", provenance, sizeof(provenance)); options["parameter_source"]=provenance;
    if (model) {sst::ui::widgets::Text("B source (required)", emissionSource, sizeof(emissionSource)); options["emission_source"]=emissionSource;}
    sst::ui::widgets::Combo("x_d source", &source, "CV at AS bias\0DLCP at V=0\0Manual\0");
    sst::ui::widgets::Combo("Missing point policy", &policy, "Block\0Linear interpolation\0Nearest point (warning)\0");
    options["xd_source"]=source==0 ? "CV" : source==1 ? "DLCP" : "manual";
    options["xd_policy"]=policy==0 ? "block" : policy==1 ? "linear" : "nearest";
    if (source==1) {
        chooseFit("Raw DLCP width fit", "DLCPPolynomial",dlcp);
        if (dlcp) options["dlcp_fit_id"]=dlcp;
    }
    if (source==2) {
        sst::ui::widgets::Double("x_d (um)", &width, .01f, "%.6g"); sst::ui::widgets::Text("x_d description", description, sizeof(description));
        options["xd_manual_m"]=width*1e-6; options["xd_description"]=description;
    } else {
        if (sst::ui::widgets::BeginCombo("Width measurement", std::to_string(xdMeasurement).c_str())) {
            for (const auto &m : p.measurements) if (m.at("sample_id")==sample && m.at("metadata").at("kind")!="FrequencySweep")
                if (ImGui::Selectable(std::to_string(m.at("id").get<pl::Id>()).c_str(),m.at("id")==xdMeasurement)) xdMeasurement=m.at("id");
            sst::ui::widgets::EndCombo();
        }
        if (xdMeasurement) options["xd_measurement_id"]=xdMeasurement;
        if (source==0) {
            sst::ui::widgets::Double("Confirmed AS bias (V)", &bias, .01f, "%.6g"); options["as_bias_V"]=bias;
            static int mat=-1;
            std::vector<std::string> names{"ProPho-compat"};
            const sst::physics::MaterialLibrary library;
            for (const auto &m : library.list()) if (m.eps_r) names.push_back(m.name);
            if (sst::ui::widgets::BeginCombo("Width material", mat<0 ? "Select material" : names.at(mat).c_str())) {
                for (std::size_t i=0;i<names.size();++i) if (ImGui::Selectable(names[i].c_str(),mat==int(i))) mat=int(i);
                sst::ui::widgets::EndCombo();
            }
            if (mat>=0) options["material"]=names.at(mat);
        }
    }
    sst::ui::widgets::Double("Minimum T (K)", &tlo, .01f, "%.6g"); sst::ui::widgets::Double("Maximum T (K)", &thi, .01f, "%.6g");
    sst::ui::widgets::LogValue("Minimum f (Hz)", &flo); sst::ui::widgets::LogValue("Maximum f (Hz)", &fhi);
    sst::ui::widgets::Double("Minimum E (eV)", &elo, .01f, "%.6g"); sst::ui::widgets::Double("Maximum E (eV)", &ehi, .01f, "%.6g");
    sst::ui::widgets::Double("Integral minimum E (eV)", &ilo, .01f, "%.6g"); sst::ui::widgets::Double("Integral maximum E (eV)", &ihi, .01f, "%.6g");
    options["temperature_range_K"]={tlo,thi}; options["frequency_range_Hz"]={flo,fhi};
    options["energy_range_eV"]={elo,ehi}; options["integration_range_eV"]={ilo,ihi}; options["measurement_ids"]=selected;
    if (ImGui::Button("Calculate Walter")) {state->analysisOptions=options; (void)commands->Execute(CommandID{"caplab.analyze"});}
    for (const auto &h : p.historical) if (h.at("sample_id")==sample) {
        if (ImGui::Button(("Integrate saved ProPho slice##"+h.at("sample_id").dump()).c_str())) {
            state->analysisOptions={{"type","WalterSaved"},{"sample_id",h.at("sample_id")}};
            (void)commands->Execute(CommandID{"caplab.analyze"});
        }
    }
    ImGui::TextWrapped("ProPho compatibility: saved W1-W3 integrals only. Full historical spectrum reconstruction unsupported.");
    for (const auto &a : p.analyses) if (a.at("type")=="Walter" || a.at("type")=="WalterSaved") {
        ImGui::PushID(a.at("id").dump().c_str());
        if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
            for (const auto &s : a.at("spectra")) ImGui::TextWrapped("Integral: %s cm^-3; %s",s.at("integral").at("density_cm3").dump().c_str(),s.at("integral").at("semantics").get<std::string>().c_str());
            for (const auto &g : a.at("graphs")) if (ImGui::Button(("Show spectrum##"+g.dump()).c_str())) {p.graph=g; state->view={}; state->document.Changed();}
            if (ImGui::TreeNode("Parameters / invalid points / provenance")) {auto detail=a; detail.erase("settings"); detail.erase("historical"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();}
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}
}
