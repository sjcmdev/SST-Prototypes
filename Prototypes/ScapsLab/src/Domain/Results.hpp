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
    bool DropIdleView(std::chrono::steady_clock::time_point idleSince) const; // true if a view was dropped
    bool HasView() const {std::lock_guard g(lock); return bool(view);}
    const Json &Provenance() const {static const Json none; const auto &m = MetaOf().prim; return m.is_object() && m.contains("provenance") ? m.at("provenance") : none;}
private:
    mutable std::mutex lock;
    mutable Ref<const Json> view;
    mutable std::chrono::steady_clock::time_point used{};
};
struct IvPoint {
    std::string bias_id, status, validity_status;
    double voltage_V = NAN, J_A_m2 = NAN, conservation_error_A_m2 = NAN, residual = NAN, internal_voltage_V = NAN, terminal_voltage_V = NAN;
    std::int64_t iterations = 0;
    bool valid = false;
    Column x_m, Ec_eV, Ev_eV, EFn_eV, EFp_eV, n_m3, p_m3;
    PJson rest;                 // generation_recombination, defect_previews, occupations, ...
    std::uint32_t has = 0;      // bit per modelled key present in the source Json
    bool verbatim = false;      // the source point was not an object: `rest.prim` holds it as is
};
struct AcPoint {
    std::string point_id, status, validity_status;
    double voltage_V = NAN, terminal_voltage_V = NAN, internal_voltage_V = NAN, frequency_Hz = NAN, temperature_K = NAN, capacitance_F = NAN, conductance_S = NAN,
           G_over_omega_F = NAN, inverse_C2_F_minus2 = NAN, conservation_relative = NAN, EQE = NAN, wavelength_nm = NAN;
    bool valid = false;
    PJson rest;
    std::uint32_t has = 0;
    bool verbatim = false;
};
struct IvResult final : ResultBase { // run and band (band = a single-bias run)
    Column mesh_x_m;
    std::vector<IvPoint> points;
    PJson meta;                 // optics, photovoltaic, provenance, project_snapshot, ...
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
    PJson body;
    Json Dump() const override {return Unpack(body);}
    const PJson &MetaOf() const override {return body;}
};
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
const PJson *ChildOf(const PJson &obj, const std::string &key);                         // packed object member (e.g. rest -> generation_recombination)
bool NumbersOf(const PJson &obj, const std::string &key, std::vector<double> &out);    // numeric array member of a packed object (Column or short plain array)
Ref<const IvResult> DropProfiles(const IvResult &r); // spatial profiles removed (mesh, per-point arrays/objects); curve and scalar metrics stay
bool HasProfiles(const IvResult &r);                 // mesh_x_m present and points present (what DropProfiles needs)
} // namespace SST::ScapsLab
