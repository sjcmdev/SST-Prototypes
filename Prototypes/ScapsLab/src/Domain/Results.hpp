#pragma once
// Typed views of the solver result Json (S2). Json stays the saved/contract form; these are built once per result
// (Memo) for typed consumers. Keys not modelled here are kept verbatim in `rest`/`meta`, so ToJson(FromJson(j)) == j
// up to NaN-vs-null (non-finite doubles are written as null, as the Json contract does).
#include "Core/Utils/Memory.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace SST::ScapsLab {
using Json = nlohmann::json;
struct IvPoint {
    std::string bias_id, status, validity_status;
    double voltage_V = NAN, J_A_m2 = NAN, conservation_error_A_m2 = NAN, residual = NAN, internal_voltage_V = NAN, terminal_voltage_V = NAN;
    std::int64_t iterations = 0;
    bool valid = false;
    std::vector<double> x_m, Ec_eV, Ev_eV, EFn_eV, EFp_eV, n_m3, p_m3;
    Json rest = Json::object(); // generation_recombination, defect_previews, occupations, ...
    std::uint32_t has = 0;      // bit per modelled key present in the source Json
};
struct AcPoint {
    std::string point_id, status, validity_status;
    double voltage_V = NAN, terminal_voltage_V = NAN, internal_voltage_V = NAN, frequency_Hz = NAN, temperature_K = NAN, capacitance_F = NAN, conductance_S = NAN,
           G_over_omega_F = NAN, inverse_C2_F_minus2 = NAN, conservation_relative = NAN, EQE = NAN, wavelength_nm = NAN;
    bool valid = false;
    Json rest = Json::object();
    std::uint32_t has = 0;
};
struct IvResult { // run and band (band = a single-bias run)
    std::vector<double> mesh_x_m;
    std::vector<IvPoint> points;
    Json meta = Json::object(); // optics, photovoltaic, provenance, project_snapshot, ...
    std::uint32_t has = 0;
    bool empty() const {return points.empty();}
};
struct AcResult { // cv, cf, qe
    std::vector<double> mesh_x_m;
    std::vector<AcPoint> points;
    std::string kind;
    double area_m2 = NAN;
    Json meta = Json::object();
    std::uint32_t has = 0;
    bool empty() const {return points.empty();}
};
using BandResult = IvResult;
IvResult IvFromJson(const Json &j);
AcResult AcFromJson(const Json &j);
Json ToJson(const IvResult &r);
Json ToJson(const AcResult &r);

// Typed copy of a Json result, rebuilt only when the Json object (shared_ptr) changes. Holds a weak ref so Cow::mut() still edits in place;
// callers that mutate in place must call reset().
template<class T, T (*Convert)(const Json &)> struct Memo {
    mutable WeakRef<const Json> src;
    mutable Ref<const T> val;
    Ref<const T> get(const Ref<const Json> &j) const {
        if (!val || src.lock() != j) {val = CreateRef<const T>(Convert(*j)); src = j;}
        return val;
    }
    void reset() const {val.reset(); src.reset();}
};
} // namespace SST::ScapsLab
