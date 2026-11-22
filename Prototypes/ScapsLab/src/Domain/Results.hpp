#pragma once
// Typed solver results (S4): the stored form of I-V / band / AC / batch results and of every history run. Json is produced only for the saved
// file (ResultsIO / save job), Snapshot() and the Python adapter, plus a lazily built, idle-trimmed read-only view (ResultBase::View) for panels
// that still read Json. ToJson(FromJson(j)) dumps identically to j: modelled keys keep their presence bit, unmodelled keys live in PJson
// `rest`/`meta`, non-finite doubles are written as null (what Json::dump does anyway), integers are never turned into doubles.
#include "Domain/Packed.hpp"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <scaps_core/batch.hpp>
namespace SST::ScapsLab {
class ResultBase {
public:
    ResultBase() = default;
    ResultBase(const ResultBase &) {}                            // the cached view is never copied
    ResultBase &operator=(const ResultBase &) {return *this;}
    virtual ~ResultBase() = default;
    virtual Json Dump() const = 0;                               // typed -> fresh Json (save, Snapshot, Python)
    virtual const PJson &MetaOf() const = 0;
    const Json &View() const;                                    // cached Json for read-only UI/test access; build cost = one Dump
    bool DropIdleView(std::chrono::steady_clock::time_point idleSince,Ref<const Json> *dropped=nullptr) const; // true if a view was dropped (handed to `dropped` when given, so the caller can free the big Json off the UI thread)
    bool HasView() const {std::lock_guard g(lock); return bool(view);}
    const Json &Provenance() const {static const Json none; const auto &m = MetaOf().prim; return m.is_object() && m.contains("provenance") ? m.at("provenance") : none;}
private:
    mutable std::mutex lock;
    mutable Ref<const Json> view;
    mutable std::chrono::steady_clock::time_point used{};
};
struct DefectPreview {
    std::string id, label, layer_id;
    bool sheet=false, single=false, amphoteric=false, correlated=true;
    double Ev_eV=0, Ec_eV=0, level_energy_eV=0, Nt=0;
    std::optional<double> second_level_energy_eV;
    int left_layer_index=-1;
    std::vector<double> energy_eV, density, occupation_energy_eV, occupation, band_edges_eV;
};
struct LayerCurrent {std::string layer_id;double current_A_m2=0;};
struct RecombinationLayer {
    std::string layer_id;
    double generation_current_A_m2=0, recombination_current_A_m2=0;
    std::map<std::string,double> mechanisms;
};
struct RecombinationChannel {
    std::string id,engine_id,mechanism,validity_status;
    Column x_m,R_m3_s,R_sheet_m2_s;
    double integrated_current_A_m2=0,layer_sum_relative_error=0;
    std::vector<LayerCurrent> by_layer;
    bool valid=false;
};
struct GenerationRecombinationResult {
    Column x_m,generation_m3_s;
    std::vector<RecombinationLayer> layers;
    std::vector<RecombinationChannel> channels;
    std::vector<scaps_core::ContactRecombination> contact_recombination;
    double generation_current_A_m2=0,recombination_current_A_m2=0,net_generation_current_A_m2=0,
        contact_loss_current_A_m2=0,shunt_current_A_m2=0,series_current_A_m2=0,predicted_terminal_current_A_m2=0,terminal_budget_relative_error=NAN;
    bool valid=false,terminal_budget_valid=false,has_terminal_budget=true;
};
struct IvDetails {
    bool preserve_original = false; // unknown historical fields are retained only at the file edge
    GenerationRecombinationResult generation_recombination;
    std::vector<DefectPreview> defect_previews;
    std::vector<std::vector<double>> trap_occupation;
    std::vector<double> interface_occupation;
};
struct AbsorptionCurve {std::string layer_id;Column wavelength_nm,alpha_m1,without_tail_m1;};
struct OpticsResult {Column x_m,generation_m3_s;double incident_power_W_m2=0;std::vector<AbsorptionCurve> absorption;};
struct IvPoint {
    std::string bias_id, status, validity_status;
    double voltage_V = NAN, J_A_m2 = NAN, conservation_error_A_m2 = NAN, residual = NAN, internal_voltage_V = NAN, terminal_voltage_V = NAN;
    std::int64_t iterations = 0;
    bool valid = false;
    Column x_m, Ec_eV, Ev_eV, EFn_eV, EFp_eV, n_m3, p_m3;
    PJson rest;                 // generation_recombination, defect_previews, occupations, ...
    Ref<const IvDetails> details;
    std::uint32_t has = 0;      // bit per modelled key present in the source Json
    bool verbatim = false;      // the source point was not an object: `rest.prim` holds it as is
};
struct AcPoint {
    std::string point_id, status, validity_status;
    double voltage_V = NAN, terminal_voltage_V = NAN, internal_voltage_V = NAN, frequency_Hz = NAN, temperature_K = NAN, capacitance_F = NAN, conductance_S = NAN,
           G_over_omega_F = NAN, inverse_C2_F_minus2 = NAN, conservation_relative = NAN, EQE = NAN, wavelength_nm = NAN,
           incident_photon_flux_m2_s = NAN, probe_power_W_m2 = NAN;
    bool valid = false;
    PJson rest;
    std::uint32_t has = 0;
    bool verbatim = false;
};
struct IvResult final : ResultBase { // run and band (band = a single-bias run)
    Column mesh_x_m;
    std::vector<IvPoint> points;
    PJson meta;                 // optics, photovoltaic, provenance, project_snapshot, ...
    std::optional<OpticsResult> optics;
    std::optional<scaps_core::PhotovoltaicResult> photovoltaic;
    bool preserve_optics=false, preserve_photovoltaic=false;
    std::uint32_t has = 0;
    bool empty() const {return points.empty();}
    Json Dump() const override;
    const PJson &MetaOf() const override {return meta;}
};
struct AcResult final : ResultBase { // cv, cf, qe
    Column mesh_x_m;
    std::vector<AcPoint> points;
    std::string kind;
    double area_m2 = NAN;
    PJson meta;
    std::uint32_t has = 0;
    bool empty() const {return points.empty();}
    Json Dump() const override;
    const PJson &MetaOf() const override {return meta;}
};
struct BatchResult final : ResultBase { // sweep/batch table (rows, curves, spec, columns) and single-row batch runs of the history
    PJson body; // metadata; legacy files retain their lossless packed representation
    std::vector<Ref<const scaps_core::BatchRow>> rows;
    Json legacySchema;
    Ref<const BatchResult> legacyBase;
    std::vector<std::string> replacedRows;
    bool native = false;
    const Json& Schema() const {return native?body.prim:legacySchema;}
    Json Dump() const override;
    const PJson &MetaOf() const override {return body;}
};
Json BatchRowJson(const scaps_core::BatchRow& row);
using BandResult = IvResult;
IvResult IvFromJson(const Json &j);
IvResult IvFromJson(Json &&j);   // consumes j (its points are moved out): no second copy of big results
AcResult AcFromJson(const Json &j);
AcResult AcFromJson(Json &&j);
BatchResult BatchFromJson(const Json &j);
inline Json ToJson(const IvResult &r) {return r.Dump();}
inline Json ToJson(const AcResult &r) {return r.Dump();}
inline Json ToJson(const BatchResult &r) {return r.Dump();}
// Typed point readers for plot caches (no Json). Numbers: modelled scalar (finite) or numeric member of `rest`; false if absent/null.
bool PointNumber(const IvPoint &p, const std::string &key, double &out);
bool PointNumber(const AcPoint &p, const std::string &key, double &out);
bool PointArray(const IvPoint &p, const std::string &key, std::vector<double> &out);   // modelled profile column (x_m, Ec_eV, ...)
const Column& PointColumn(const IvPoint&,const std::string& key);
const IvPoint& PointAtBias(const IvResult&,const IvResult&,int selected);
const PJson *ChildOf(const PJson &obj, const std::string &key);                         // packed object member (e.g. rest -> generation_recombination)
bool NumbersOf(const PJson &obj, const std::string &key, std::vector<double> &out);    // numeric array member of a packed object (Column or short plain array)
Ref<const IvResult> DropProfiles(const IvResult &r); // spatial profiles removed (mesh, per-point arrays/objects); curve and scalar metrics stay
struct BatchCurve {std::vector<double> v,j;std::vector<char> converged;};
std::vector<BatchCurve> BatchIvCurves(const BatchResult &r);   // rows[*].curves.iv as voltage_V / J_Am2 / status=="Converged" columns, read straight from the packed table (no Json is built); a row without curves gives an empty curve
bool HasProfiles(const IvResult &r);                 // mesh_x_m present and points present (what DropProfiles needs)
} // namespace SST::ScapsLab
