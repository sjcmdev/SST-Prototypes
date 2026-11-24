#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/physics/materials.hpp>
#include <cstdlib>
#include <cstdio>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderAS() {
    if (!ImGui::CollapsingHeader("AS / defects / Arrhenius",ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto &p=state->Document().project;
    auto &sample=state->Document().selectedSample;
    auto &measurement=state->Document().selectedMeasurement;
    static std::uint64_t selectionProject = 0;
    if (selectionProject != state->workspace.active) { selectionProject=state->workspace.active; state->pendingCandidate=nullptr; }
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
        for (const auto &s : p.samples) if (ImGui::Selectable((SelectionLabel(p,s.id)+"##"+std::to_string(s.id)).c_str(),sample==s.id)) {SelectObject("sample",s.id); selectedDefect=0; state->pendingCandidate=nullptr;}
        sst::ui::widgets::EndCombo();
    }
    sst::ui::widgets::Text("Defect name", name, sizeof(name));
    if (ImGui::Button("Create defect")) {edit({{"action","create"},{"sample_id",sample},{"name",name}}); return;}
    if (sst::ui::widgets::BeginCombo("AS measurement", SelectionLabel(p,measurement).c_str())) {
        for (const auto &m : p.measurements) if (m.at("sample_id")==sample && m.at("metadata").at("kind")=="FrequencySweep") {
            const auto id=m.at("id").get<pl::Id>();
            if (ImGui::Selectable((SelectionLabel(p,id)+"##"+std::to_string(id)).c_str(),measurement==id)) SelectObject("measurement",id);
        }
        for (const auto &m : p.sweeps) if (m.sample_id==sample)
            if (ImGui::Selectable((SelectionLabel(p,m.id)+"##"+std::to_string(m.id)).c_str(),measurement==m.id)) SelectObject("measurement",m.id);
        sst::ui::widgets::EndCombo();
    }
    sst::ui::widgets::Checkbox("Savitzky-Golay smoothing", &smoothing);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sample index; derivative uses ln f.");
    if (smoothing) {sst::ui::widgets::Int("Smoothing window (odd)", &window); sst::ui::widgets::Int("Smoothing degree", &degree);}
    const bool calculate=ImGui::Button("Preview AS derivative / candidates");
    if (measurement && AnalysisMeasurement(p,measurement).metadata.at("kind")=="FrequencySweep" && (state->Document().selectionKind!="defect" || calculate)) PreviewAfterEdit(Json{{"type","AS"},{"measurement_id",measurement},
        {"smoothing",smoothing ? Json{{"method","Savitzky-Golay"},{"window",window},{"degree",degree}} : Json{{"method","none"}}}},calculate);
    if (sst::ui::widgets::BeginCombo("Active defect", SelectionLabel(p,selectedDefect,"no defects yet").c_str())) {
        for (const auto &d : p.defects) if (d.at("sample_id")==sample)
            if (ImGui::Selectable((SelectionLabel(p,d.at("id").get<pl::Id>())+"##"+std::to_string(d.at("id").get<pl::Id>())).c_str(),d.at("id")==selectedDefect)) {selectedDefect=d.at("id").get<pl::Id>(); SelectObject("defect",selectedDefect);}
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
            if (point.contains("moved_from")) ImGui::TextWrapped("Measurement moved; point disabled. Original IDs: %s",point.at("moved_from").dump().c_str());
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
        Json settings{{"type","Arrhenius"},{"defect_id",selectedDefect},
            {"models",compare ? Json{"omega-T2","ProPho-compat"} : Json{compat ? "ProPho-compat" : "omega-T2"}}};
        if (useGamma) {settings["gamma_cm_2_s_K_2"]=gamma; settings["gamma_source"]=gammaSource;}
        const bool calculateArrhenius=ImGui::Button("Preview Arrhenius");
        if (state->Document().selectionKind=="defect" || calculateArrhenius) PreviewAfterEdit(settings,calculateArrhenius);

    }
    ImGui::TextWrapped("Candidates require approval. Select a candidate below or Ctrl+click its marker on the D-f graph (screen distance, 10 px). Default fit uses omega=2*pi*f; sigma needs explicit gamma. ProPho-compat uses Ncv=2e18 cm^-3 and vth=1e7 cm/s.");
    for (const auto &a : p.analyses) if ((a.at("type")=="AS" || a.at("type")=="Arrhenius") && AnalysisSample(a)==sample) {
        ImGui::PushID(static_cast<int>(a.at("id").get<pl::Id>()));
        if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
            if (a.at("type")=="AS") for (const auto &c : a.at("candidates")) {
                ImGui::PushID(static_cast<int>(c.at("row_id").get<pl::Id>()));
                if (a.at("status")=="valid" && ImGui::Selectable((c.at("temperature_K").dump()+" K / "+c.at("frequency_Hz").dump()+" Hz").c_str())) {state->pendingCandidate=c; state->pendingCandidate["analysis_id"]=a.at("id");}
                ImGui::PopID();
            }
            if (a.contains("fits")) for (const auto &fit : a.at("fits")) {
                ImGui::TextWrapped("%s: Ea: %s eV; dof=%d",fit.at("model").get<std::string>().c_str(),
                    WithStandardError(fit,"E_a_eV","E_a_standard_error_eV").c_str(),fit.at("dof").get<int>());
                ImGui::TextWrapped("Prefactor: %s %s",WithStandardError(fit,"prefactor","prefactor_standard_error").c_str(),fit.at("prefactor_unit").get<std::string>().c_str());
                ImGui::TextWrapped("Capture cross-section sigma: %s cm^2. SE is standard error from residual covariance.",fit.at("sigma_cm2").dump().c_str());
            }
            for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s",d.get<std::string>().c_str());
            for (const auto &g : a.at("graphs")) {
                auto id=g.get<pl::Id>();
                if (ImGui::Button((p.engine->object(id).name+"##"+std::to_string(id)).c_str())) {p.graph=id; state->view={}; state->Document().Changed();}
            }
            if (ImGui::TreeNode("AS / fit details and provenance")) {auto detail=a; detail.erase("inputs"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();}
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
}

