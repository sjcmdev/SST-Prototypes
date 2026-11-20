#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include "Domain/CapLabCompare.hpp"
#include <imgui.h>
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <cstdlib>
#include <cstdio>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderImport() {
    if (sst::ui::widgets::BeginCombo("Target sample",SelectionLabel(state->Document().project,state->importTargetSample,"Create new samples").c_str())) {
        if (ImGui::Selectable("Create new samples",!state->importTargetSample)) state->importTargetSample=0;
        for (const auto &s:state->Document().project.samples) {
            ImGui::PushID(std::to_string(s.id).c_str());
            if (ImGui::Selectable(s.name.c_str(),s.id==state->importTargetSample)) state->importTargetSample=s.id;
            ImGui::PopID();
        }
        sst::ui::widgets::EndCombo();
    }
    auto &doc = state->Document();
    ImGui::Text("%s%s", !doc.path.has_value() ? "Untitled" : doc.path->Utf8().c_str(), doc.IsDirty() ? " (unsaved)" : "");
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
void CapLabPrototypePanel::RenderExport() {
    if (!ImGui::CollapsingHeader("Export data and figures",ImGuiTreeNodeFlags_DefaultOpen)) return;
    const char* formats[]={"PNG","SVG","PDF"};
    sst::ui::widgets::Combo("Figure format",&exportFormat,formats,IM_ARRAYSIZE(formats));
    if (exportFormat==0) sst::ui::widgets::Double("Resolution (dpi)",&state->exportDpi,1,"%.0f");
    auto exportToFile=[&](bool batch, bool data=false) {
        state->exportFormat=data ? "csv" : exportFormat==0 ? "png" : exportFormat==1 ? "svg" : "pdf";
        state->exportGraphs.clear();
        if (batch) state->exportGraphs.assign(selectedExportGraphs.begin(),selectedExportGraphs.end());
        (void)commands->Execute(CommandID{"caplab.export"});
    };
    ImGui::BeginDisabled(state->activeJob || !state->view.viewport);
    if (ImGui::Button("Export selected graph...")) exportToFile(false);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state->activeJob!=0);
    ImGui::SameLine(); if (ImGui::Button("Export CSV / XLSX tables...")) exportToFile(false,true);
    ImGui::EndDisabled();
    if (ImGui::TreeNode("Batch graphs")) {
        ImGui::TextWrapped("Exports use the saved or current ranges and visibility of each graph.");
        const auto& engine=*state->Document().project.engine;
        bool ready=!selectedExportGraphs.empty();
        std::set<pl::Id> graphs;
        for (auto id : engine.children(engine.root())) if (std::holds_alternative<pl::Graph>(engine.object(id).data)) {
            graphs.insert(id);
            ImGui::PushID(std::to_string(id).c_str());
            bool selected=selectedExportGraphs.contains(id);
            if (sst::ui::widgets::Checkbox(engine.object(id).name.c_str(),&selected)) {
                if (selected) selectedExportGraphs.insert(id); else selectedExportGraphs.erase(id);
            }
            ImGui::PopID();
        }
        std::erase_if(selectedExportGraphs,[&](auto id) { return !graphs.contains(id); });
        for (auto id : selectedExportGraphs) ready &= state->GraphView(id).viewport.has_value();
        ImGui::BeginDisabled(state->activeJob || !ready || selectedExportGraphs.empty());
        if (ImGui::Button("Export selected graphs...")) exportToFile(true);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
}
void CapLabPrototypePanel::RenderBrowser() {
    auto &p=state->Document().project;
    ImGui::BeginDisabled(state->activeJob!=0 || state->analysisDraft.has_value());
    if (sst::ui::widgets::BeginCombo("Graph",p.graph ? p.engine->object(p.graph).name.c_str() : "Select graph")) {
        for (auto id:p.engine->children(p.engine->root())) if (std::holds_alternative<pl::Graph>(p.engine->object(id).data)) {
            ImGui::PushID(std::to_string(id).c_str());
            if (ImGui::Selectable(p.engine->object(id).name.c_str(),id==p.graph)) SelectObject("graph",id);
            ImGui::PopID();
        }
        sst::ui::widgets::EndCombo();
    }
    if (const auto section=std::getenv("SST_CAPLAB_GALLERY_SECTION"))
        ImGui::SetNextItemOpen(std::string_view(section)=="Compare",ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Compare stored data (2D)")) {
        const char *types[]={"C-V","Mott-Schottky","N_CV(W)","N_DLCP(W)","C-f","G/omega-f","Nyquist","Bode magnitude","Bode phase"};
        sst::ui::widgets::Combo("Measurement plot",&compareType,types,IM_ARRAYSIZE(types));
        sst::ui::widgets::Text("Filter sample / source / result",&compareFilter);
        ImGui::BeginChild("Compare sources",ImVec2(0,180),ImGuiChildFlags_Borders);
        auto choose=[&](pl::Id id,const std::string &label,std::set<pl::Id> &selection) {
            if (!compareFilter.empty() && label.find(compareFilter)==std::string::npos) return;
            ImGui::PushID(std::to_string(id).c_str());
            bool selected=selection.contains(id);
            if (sst::ui::widgets::Checkbox(label.c_str(),&selected)) {if (selected) selection.insert(id); else selection.erase(id);}
            ImGui::PopID();
        };
        std::set<pl::Id> measurements,graphs;
        for (const auto &sample:p.samples) {
            auto measurement=[&](pl::Id id) {
                measurements.insert(id);
                choose(id,sample.name+" / "+SelectionLabel(p,id)+" ["+std::to_string(id)+"]",compareMeasurements);
            };
            for (const auto &m:p.sweeps) if (m.sample_id==sample.id) measurement(m.id);
            for (const auto &m:p.measurements) if (m.at("sample_id")==sample.id) measurement(m.at("id"));
            for (const auto &a:p.analyses) if (AnalysisSample(a)==sample.id) for (const auto &g:a.at("graphs")) {
                const auto id=g.get<pl::Id>(); graphs.insert(id);
                choose(id,sample.name+" / "+p.engine->object(id).name+" / "+a.at("status").get<std::string>()+" ["+std::to_string(id)+"]",compareGraphs);
            }
        }
        std::erase_if(compareMeasurements,[&](auto id) {return !measurements.contains(id);});
        std::erase_if(compareGraphs,[&](auto id) {return !graphs.contains(id);});
        ImGui::EndChild();
        ImGui::TextWrapped("Analysis graphs include Arrhenius defects, Walter spectra and IS. Only identical quantities and scales can share axes.");
        ImGui::BeginDisabled(compareMeasurements.empty() && compareGraphs.empty());
        if (ImGui::Button("Build comparison (Undo)")) {
            state->analysisOptions={{"plot_type",types[compareType]},{"measurement_ids",compareMeasurements},{"graph_ids",compareGraphs}};
            (void)commands->Execute(CommandID{"caplab.plot"});
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Clear sources")) {compareMeasurements.clear(); compareGraphs.clear();}
    }
    if (p.graph && ImGui::CollapsingHeader("Axes / view")) {
        const auto &layer=std::get<pl::Layer>(p.engine->object(p.engine->children(p.graph).at(0)).data);
        int x=layer.spec.x.scale==sst::ui::plot::AxisScale::Log10, y=layer.spec.y.scale==sst::ui::plot::AxisScale::Log10;
        const char *scales[]={"Linear","Log10"};
        bool changed=sst::ui::widgets::Combo("X scale",&x,scales,2);
        changed |= sst::ui::widgets::Combo("Y scale",&y,scales,2);
        if (changed) {
            state->analysisOptions={{"action","view"},{"graph_id",p.graph},{"x_scale",x ? "log10" : "linear"},{"y_scale",y ? "log10" : "linear"}};
            (void)commands->Execute(CommandID{"caplab.plot"});
        }
        if (ImGui::Button("Fit all data")) {
            state->analysisOptions={{"action","view"},{"graph_id",p.graph},{"refit",true}};
            (void)commands->Execute(CommandID{"caplab.plot"});
        }
        ImGui::TextWrapped("Drag to pan; wheel to zoom; click legend to hide a curve. Engineering units and logarithmic tick labels are retained in export.");
    }
    ImGui::EndDisabled();
}
void CapLabPrototypePanel::RenderPlot() {
    RenderBrowser();
    auto &doc = state->Document();
    auto &project=state->analysisDraft && state->analysisRevision==doc.revision ? *state->analysisDraft : doc.project;
    if (project.graph) {
        if (!state->analysisDraft) RenderExport();
        else ImGui::TextUnformatted("Preview plot; accepted results remain in the project tree.");
        for (const auto &a:project.analyses) if (std::find(a.at("graphs").begin(),a.at("graphs").end(),Json(project.graph))!=a.at("graphs").end())
            ImGui::Text("%s result: %s",state->analysisDraft ? "Preview" : "Accepted",a.at("status").get<std::string>().c_str());
        for (const auto &c:project.comparisons) if (c.at("graph_id")==project.graph)
            ImGui::Text("Stored comparison: %s",CompareStatus(project,c).c_str());
        for (const auto& a : project.analyses) if (a.at("type")=="Circuit") {
            const auto& graphs=a.at("graphs");
            if (std::find(graphs.begin(),graphs.end(),Json(project.graph))!=graphs.end()) {
                RenderISTabs(project,a,state->view);
                state->graphViews[project.graph]=state->view;
                if (!state->analysisDraft) state->StoreGraphView(project.graph,state->view);
                return;
            }
        }
        const auto &p = *project.engine;
        auto window=GetState();
        const auto& title=p.object(project.graph).name;
        if (window.shortName!=title) {window.shortName=title; window.longName=title; ApplyState(window);}
        const auto layer = p.children(project.graph).at(0);
        if (!state->view.viewport) state->view=std::get<pl::Layer>(p.object(layer).data).view;
        std::vector<sst::ui::plot::SeriesEntryView> series;
        for (auto id : p.children(layer)) {
            auto entry = p.series(id);
            entry.id = p.object(id).name + "###" + entry.id;
            series.push_back(std::move(entry));
        }
        ImGui::PushID(std::to_string(project.graph).c_str());
        sst::ui::plot::RenderPlot("##cf", std::get<pl::Layer>(p.object(layer).data).spec,
            series.data(), series.size(), state->view, {}, ImVec2(-1, std::max(200.0f, ImGui::GetContentRegionAvail().y - 40)));
        ImGui::PopID();
        if (state->view.picked && state->view.picked->series==0) {
            for (const auto &a : project.analyses) if (a.at("type")=="AS" && a.at("status")=="valid" &&
                a.at("graphs").at(1)==project.graph && state->view.picked->point<a.at("candidates").size())
                {state->pendingCandidate=a.at("candidates").at(state->view.picked->point); state->pendingCandidate["analysis_id"]=a.at("id");}
            state->view.picked.reset();
        }
        state->graphViews[project.graph]=state->view;
        if (!state->analysisDraft) state->StoreGraphView(project.graph,state->view);
    }
}
void CapLabPrototypePanel::RenderContent() {
    if (shownProject!=state->workspace.active) {
        shownProject=state->workspace.active; areaDraft.clear(); treeMeasurements.clear(); compareMeasurements.clear(); compareGraphs.clear(); selectedExportGraphs.clear(); detailsSample=moveTarget=0; selectionSeen=UINT64_MAX;
        selectedCircuit=circuitMeasurement=selectedDefect=fitCircuit=isPlotAnalysis=isPlotGraph=0;
        circuitDocument.clear(); circuitEditor.reset(); modelAutoReady=circuitAutoReady=circuitRecalc=false; isParameters=Json::array(); fitView={}; bodePhaseView={};
    }
    state->ReconcileSelection();
    auto &document=state->Document();
    if (selectionSeen!=document.selectionRevision) {
        selectionSeen=document.selectionRevision;
        if (selectedCircuit!=document.selectedCircuit) {modelAutoReady=false; circuitEditor.reset(); fitCircuit=0;}
        selectedCircuit=document.selectedCircuit; selectedDefect=document.selectedDefect; circuitMeasurement=document.selectedMeasurement;
        previousPreviewOptions.clear(); previewPending.clear();
    }
    ImGui::PushID(std::to_string(state->workspace.active).c_str());

    switch (view) {
    case View::Project: RenderTree(); break;
    case View::Plot: RenderPlot(); break;
    case View::CV: RenderAnalysis(); break;
    case View::AS: RenderAS(); break;
    case View::Walter: ImGui::SetNextItemOpen(true,ImGuiCond_Once); RenderWalter(); break;
    case View::IS: ImGui::SetNextItemOpen(true,ImGuiCond_Once); RenderIS(); break;
    case View::Import: RenderImport(); break;
    }
    ImGui::Spacing(); ImGui::SeparatorText("Status");
    if (state->failure) ImGui::TextWrapped("%s",state->failure->code.c_str());
    ImGui::TextWrapped("%s", state->status.c_str());
    if (state->analysisDraft) {
        const bool stale=state->analysisRevision!=document.revision;
        ImGui::TextWrapped("Preview: %s. Accepted results retain their own status.",stale ? "stale" : "ready");
        ImGui::BeginDisabled(stale || state->activeJob!=0);
        if (ImGui::Button("Accept preview (Undo)")) (void)commands->Execute(CommandID{"caplab.accept_analysis"});
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Discard preview")) (void)commands->Execute(CommandID{"caplab.cancel"});
    }
    if (const auto job = state->jobs->GetJob(state->activeJob)) {
        ImGui::TextUnformatted(job->currentStage.c_str());
        ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
        if (ImGui::Button("Cancel")) (void)commands->Execute(CommandID{"caplab.cancel"});
    }
    ImGui::PopID();
}

void CapLabPrototypePanel::PreviewAfterEdit(const Json &options, bool calculate) {
    const std::string type=options.at("type");
    const Json inputs{{"options",options},{"revision",state->Document().inputRevision}};
    if (previousPreviewOptions[type]!=inputs) {previousPreviewOptions[type]=inputs; previewPending.insert(type);}
    if (calculate) previewPending.insert(type);
    if (state->analysisQueued) return;
    if (!previewPending.contains(type) || state->closeRequested || ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    if (state->activeJob) {
        if (state->analysisOptions.value("preview",false) && state->analysisOptions.value("type",std::string{})==options.value("type",std::string{}))
            (void)state->jobs->RequestCancel(state->activeJob);
        return;
    }
    previewPending.erase(type); state->analysisOptions=options; state->analysisOptions["preview"]=true;
    state->analysisQueued=bool(commands->Execute(CommandID{"caplab.analyze"}));
}

}
