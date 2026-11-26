#include "Library/Library.hpp"

namespace SST::ScapsLab {const scaps_core::OpticalTable& AM15SpectrumValues();}
namespace SST::ScapsLab::Library {
std::shared_ptr<const State> Builtins() {
    static const auto state=[] {
        auto out=std::make_shared<State>();Catalog catalog;catalog.id="sst-examples";catalog.name="SST examples";
        const auto add=[&](std::string id,std::string name,Payload payload,std::string collection,std::string source,std::string locator) {
            Assets::Metadata m;m.id=std::move(id);m.name=std::move(name);m.collection=std::move(collection);m.changeNote="Initial example seed";m.tags={"example",Kind(payload)};m.description="Simulation example. Verify provenance and validity before using it as measured material data.";
            m.provenance.source="example";m.provenance.citation=source;m.provenance.sourcePath=source;m.provenance.locator=locator;m.provenance.date="2026-11-25";
            // Shipped examples remain inspectable when user bounds exclude their values; Apply validates effective bounds.
            catalog.Create(std::move(m),std::make_shared<const Payload>(std::move(payload)));
        };
        auto p=scaps_core::make_default_device_project();
        add("si-default","Silicon — engine example",FromCarrier("material",p),"Materials","scaps_core/src/device_project.cpp","make_default_device_project / make_default_layer; units checked against frozen references/PyScaps/pysc/model/device.py");
        add("pn-default","Silicon p-n device",Device{p},"Devices","scaps_core/src/device_project.cpp","make_default_device_project");
        add("si-layer","Silicon layer template",Layer{p.layers.front()},"Layers","scaps_core/src/device_project.cpp","make_default_layer");
        Material cigs;cigs.values.eps_r=13.4;cigs.values.chi_eV=3.89;cigs.values.Eg_eV=1.2726;cigs.values.Nc_ref_m3=2.2e24;cigs.values.Nv_ref_m3=1.8e25;cigs.values.mu_n_m2Vs=.03;cigs.values.mu_p_m2Vs=.001;cigs.Na_m3=8e22;
        add("pyscaps-cigs","CIGS — frozen PyScaps example",cigs,"Materials","references/PyScaps/mydefs/CIGS-ZnO-CdS-Aissat2018.def","lines 24–41; material and doping only, example input (not a verified literature extraction)");
        Defect defect;defect.value.Nt_m3=1e20;defect.value.label="Neutral bulk SRH example";
        add("neutral-srh","Neutral bulk SRH",defect,"Defects/Bulk","Prototypes/ScapsLab/src/Domain/ProjectDraft.cpp","AddDraftDefect defaults; SCAPS manual bulk single-level convention");
        Interface sheet;sheet.defects.push_back({});sheet.defects.front().Nt_m2=1e12;
        add("neutral-sheet","Neutral interface SRH",sheet,"Defects/Interface","Prototypes/ScapsLab/src/Domain/ProjectDraft.cpp","AddDraftDefect interface defaults");
        add("contact-default","Left contact template",Contact{p.left_contact},"Contacts","scaps_core/src/device_project.cpp","make_default_device_project, left_contact");
        Profile constant;constant.value.left=1e22;
        add("doping-constant","Constant acceptor profile",constant,"Profiles/Doping","Prototypes/ScapsLab/examples/profiles/doping.grd","illustrative constant profile, density m^-3");
        Profile linear;linear.target="composition";linear.value.law=scaps_core::GradingLaw::Linear;linear.value.left=.2;linear.value.right=.6;
        add("composition-linear","Linear composition grading",linear,"Profiles/Composition","scaps_core/include/scaps_core/grading.hpp","illustrative composition law, dimensionless");
        Profile notch=linear;notch.value.law=scaps_core::GradingLaw::Parabolic2;notch.value.left=.6;notch.value.right=.6;notch.value.middle=.2;notch.value.midpoint=.5;
        add("composition-notch","Composition notch",notch,"Profiles/Composition","scaps_core/include/scaps_core/grading.hpp","illustrative two-segment parabolic notch, dimensionless");
        Illumination light;light.value.enabled=true;light.value.mode="spectrum";light.value.spectrum_nm_W_m2=AM15SpectrumValues();
        add("am15-example","AM1.5G — bundled spectrum",std::move(light),"Illumination","Prototypes/ScapsLab/src/AM15.inc","bundled AM1.5G sample table; wavelength nm, spectral irradiance W m^-2 nm^-1");
        Optical optical;optical.absorption.model="file";optical.absorption.table_nm_m1={{300,1e6},{600,5e5},{1200,1e2}};
        add("alpha-demo","Absorption table example",optical,"Optical","Prototypes/ScapsLab/examples/profiles/absorption.abs","illustrative preview table, not literature data");
        catalog.scope=Assets::Scope::Builtin;catalog.readOnly=true;out->libraries.emplace("sst-examples",Mounted{std::move(catalog),{}});
        auto user=Empty("user","User library");out->libraries.emplace("user",Mounted{std::move(user),{}});
        auto project=Empty("project","Project library",Assets::Scope::Project);out->libraries.emplace("project",Mounted{std::move(project),{}});
        try {auto literature=LoadShippedLiterature();const auto id=literature.catalog.id;literature.catalog.scope=Assets::Scope::Builtin;literature.catalog.readOnly=true;out->libraries.emplace(id,std::move(literature));}
        catch(const std::exception& e){out->warnings.push_back(std::string("Literature library: ")+e.what());}
        return std::shared_ptr<const State>(out);
    }();return state;
}
}
