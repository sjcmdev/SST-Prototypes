#include "Core/dspch.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include "Presentation/SelectionLabel.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/physics/materials.hpp>
#include <cstdlib>
#include <cstdio>
namespace SST::Prototypes::CapLab {
void CapLabPrototypePanel::RenderAnalysis() {
    if (!ImGui::CollapsingHeader("CV / raw DLCP analysis",ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto &p = state->Document().project;
    auto &selected=state->Document().selectedMeasurement;
    static pl::Id xdSelected = 0, maskMeasurement = 0;
    static std::set<pl::Id> masked;
    static std::uint64_t selectionProject = 0;
    if (selectionProject != state->workspace.active) { selectionProject=state->workspace.active; xdSelected=maskMeasurement=0; masked.clear(); }
    auto select = [&](const char *label, pl::Id &id, const char *empty = "none — select") {
        if (sst::ui::widgets::BeginCombo(label, SelectionLabel(p,id,empty).c_str())) {
            for (const auto &m : p.measurements) {
                const auto mid=m.at("id").get<pl::Id>();
                const std::string title=SelectionLabel(p,mid)+"##"+std::to_string(mid);
                if (ImGui::Selectable(title.c_str(),id==mid)) {if (&id==&selected) SelectObject("measurement",mid); else id=mid;}
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
        const bool calculate=ImGui::Button("Preview CV / DLCP");
        if (materialChoice>=0) PreviewAfterEdit(options,calculate);
        for (const auto &a : p.analyses) {
            if ((a.at("type")!="CV" && a.at("type")!="DLCPPolynomial") || a.at("inputs").at(0).at("id")!=selected) continue;
            ImGui::PushID(static_cast<int>(a.at("id").get<pl::Id>()));
            if (ImGui::TreeNode((a.at("model").get<std::string>()+" / "+a.at("status").get<std::string>()).c_str())) {
                for (const auto &d : a.at("diagnostics")) ImGui::TextWrapped("%s",d.get<std::string>().c_str());
                if (a.contains("V_b_V")) {
                    ImGui::TextWrapped("V_b: %s V",WithStandardError(a,"V_b_V","V_b_standard_error_V").c_str());
                    ImGui::TextWrapped("N: %s m^-3; E_F: %s eV",WithStandardError(a,"N_m3","N_standard_error_m3").c_str(),a.at("E_F_eV").dump().c_str());
                }
                if (a.contains("x_d_fit")) ImGui::TextWrapped("CV fit x_d at %s V: %s m",a.at("x_d_fit").at("bias_V").dump().c_str(),WithStandardError(a.at("x_d_fit"),"value_m","standard_error_m").c_str());
                if (a.contains("groups")) for (const auto& group : a.at("groups")) {
                    ImGui::TextWrapped("DLCP at %s V: x_e: %s m; N_DL: %s m^-3",group.at("bias_V").dump().c_str(),
                        WithStandardError(group,"x_e_m","x_e_standard_error_m").c_str(),WithStandardError(group,"N_DL_m3","N_DL_standard_error_m3").c_str());
                }
                ImGui::TextWrapped("Selected x_d: %s m",WithStandardError(a.at("x_d"),"value_m","standard_error_m").c_str());
                ImGui::TextWrapped("SE: residual covariance only; area, material and calibration held fixed. Null SE is unavailable.");
                if (ImGui::TreeNode("Coefficients / covariance / residuals / provenance")) {
                    auto detail=a; detail.erase("inputs"); ImGui::TextWrapped("%s",detail.dump(2).c_str()); ImGui::TreePop();
                }
                for (const auto &g : a.at("graphs")) {
                    const auto id=g.get<pl::Id>();
                    if (ImGui::Button((p.engine->object(id).name+"##"+std::to_string(id)).c_str())) { p.graph=id; state->view={}; state->Document().Changed(); }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    } catch (const std::exception &e) { ImGui::TextWrapped("%s",e.what()); }
}
}

