#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/physics/materials.hpp>
#include <cstdlib>
#include <cstdio>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderWalter() {
    if (const auto gallery = std::getenv("SST_CAPLAB_GALLERY_SECTION"))
        ImGui::SetNextItemOpen(std::string_view(gallery) == "Walter", ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Walter apparent DOS / integration")) return;
    ImGui::PushID("Walter");
    auto &p=state->Document().project;
    auto &sample=state->Document().selectedSample;
    static pl::Id cv=0, arrhenius=0, dlcp=0, xdMeasurement=0, previousSample=0;
    static std::set<pl::Id> selected;
    static std::uint64_t selectionProject = 0;
    if (selectionProject != state->workspace.active) { selectionProject=state->workspace.active; cv=arrhenius=dlcp=xdMeasurement=0; selected.clear(); }
    if (previousSample!=sample) {previousSample=sample; selected.clear(); cv=arrhenius=dlcp=xdMeasurement=0;}
    static int model=0, source=0, policy=0;
    static double vb=0, ef=0, prefactor=0, width=1, bias=0, tlo=1, thi=500, flo=100, fhi=1e6, elo=0, ehi=1, ilo=.1, ihi=.5;
    static char provenance[256]="", emissionSource[256]="", description[256]="";
    if (sst::ui::widgets::BeginCombo("Sample", std::to_string(sample).c_str())) {
        for (const auto &s : p.samples) if (ImGui::Selectable(s.name.c_str(),s.id==sample)) {SelectObject("sample",s.id); selected.clear(); cv=arrhenius=dlcp=xdMeasurement=0;}
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
    const bool calculate=ImGui::Button("Preview Walter");
    if (!selected.empty()) PreviewAfterEdit(options,calculate);
    for (const auto &h : p.historical) if (h.at("sample_id")==sample) {
        if (ImGui::Button(("Integrate saved ProPho slice##"+h.at("sample_id").dump()).c_str())) {
            state->analysisOptions={{"type","WalterSaved"},{"sample_id",h.at("sample_id")}};
            (void)commands->Execute(CommandID{"caplab.analyze"});
        }
    }
    ImGui::TextWrapped("ProPho compatibility: saved W1-W3 integrals only. Full historical spectrum reconstruction unsupported.");
    for (const auto &a : p.analyses) if ((a.at("type")=="Walter" || a.at("type")=="WalterSaved") && AnalysisSample(a)==sample) {
        ImGui::PushID(a.at("id").dump().c_str());
        if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
            ImGui::TextWrapped("CL-04: apparent DOS under the selected emission and field model.");
            ImGui::TextWrapped("W1-W3 check saved-slice integrals only. Full historical spectra and parabolic-field reconstruction are unsupported.");
            for (const auto &s : a.at("spectra")) ImGui::TextWrapped("Integrated apparent DOS: %s cm^-3; %s",s.at("integral").at("density_cm3").dump().c_str(),s.at("integral").at("semantics").get<std::string>().c_str());
            for (const auto &g : a.at("graphs")) if (ImGui::Button(("Show spectrum##"+g.dump()).c_str())) {p.graph=g; state->view={}; state->Document().Changed();}
            if (ImGui::TreeNode("Parameters / invalid points / provenance")) {auto detail=a; detail.erase("settings"); detail.erase("historical"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();}
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}
}
