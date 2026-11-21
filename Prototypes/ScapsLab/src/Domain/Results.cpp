#include "Domain/Results.hpp"
#include <cmath>
#include <variant>
namespace SST::ScapsLab {
namespace {
template<class T> using Member = std::variant<double T::*, std::vector<double> T::*, std::string T::*, std::int64_t T::*, bool T::*>;
template<class T> struct Field {const char *key; Member<T> m;};
template<class T> bool Accepts(const Json &v, const Member<T> &m) {
    switch (m.index()) {
    case 0: return v.is_number() || v.is_null();
    case 1: return v.is_array() && std::all_of(v.begin(), v.end(), [](const Json &e) {return e.is_number() || e.is_null();});
    case 2: return v.is_string();
    case 3: return v.is_number_integer();
    default: return v.is_boolean();
    }
}
double Num(const Json &v) {return v.is_number() ? v.get<double>() : double(NAN);}
Json Out(double v) {return std::isfinite(v) ? Json(v) : Json(nullptr);}
// Moves modelled keys of `src` into `t` (bit i of `has` = key i present); everything else is copied to `rest`.
template<class T, size_t N> void Read(const Json &src, T &t, std::uint32_t &has, Json &rest, const Field<T> (&fields)[N]) {
    rest = Json::object();
    for (auto it = src.begin(); it != src.end(); ++it) {
        bool taken = false;
        for (size_t i = 0; i < N && !taken; ++i) if (it.key() == fields[i].key && Accepts<T>(it.value(), fields[i].m)) {
            taken = true; has |= 1u << i;
            std::visit([&](auto p) {
                using V = std::remove_reference_t<decltype(t.*p)>;
                if constexpr (std::is_same_v<V, double>) t.*p = Num(it.value());
                else if constexpr (std::is_same_v<V, std::vector<double>>) {for (const auto &e : it.value()) (t.*p).push_back(Num(e));}
                else (t.*p) = it.value().template get<V>();
            }, fields[i].m);
        }
        if (!taken) rest[it.key()] = it.value();
    }
}
template<class T, size_t N> void Write(const T &t, std::uint32_t has, Json &o, const Field<T> (&fields)[N]) {
    for (size_t i = 0; i < N; ++i) if (has >> i & 1u) std::visit([&](auto p) {
        using V = std::remove_reference_t<decltype(t.*p)>;
        if constexpr (std::is_same_v<V, double>) o[fields[i].key] = Out(t.*p);
        else if constexpr (std::is_same_v<V, std::vector<double>>) {auto a = Json::array(); for (double e : t.*p) a.push_back(Out(e)); o[fields[i].key] = std::move(a);}
        else o[fields[i].key] = t.*p;
    }, fields[i].m);
}
const Field<IvPoint> ivFields[] = {{"bias_id", &IvPoint::bias_id}, {"status", &IvPoint::status}, {"validity_status", &IvPoint::validity_status}, {"voltage_V", &IvPoint::voltage_V},
    {"J_A_m2", &IvPoint::J_A_m2}, {"conservation_error_A_m2", &IvPoint::conservation_error_A_m2}, {"residual", &IvPoint::residual}, {"internal_voltage_V", &IvPoint::internal_voltage_V},
    {"terminal_voltage_V", &IvPoint::terminal_voltage_V}, {"iterations", &IvPoint::iterations}, {"valid", &IvPoint::valid}, {"x_m", &IvPoint::x_m}, {"Ec_eV", &IvPoint::Ec_eV},
    {"Ev_eV", &IvPoint::Ev_eV}, {"EFn_eV", &IvPoint::EFn_eV}, {"EFp_eV", &IvPoint::EFp_eV}, {"n_m3", &IvPoint::n_m3}, {"p_m3", &IvPoint::p_m3}};
const Field<AcPoint> acFields[] = {{"point_id", &AcPoint::point_id}, {"status", &AcPoint::status}, {"validity_status", &AcPoint::validity_status}, {"voltage_V", &AcPoint::voltage_V},
    {"terminal_voltage_V", &AcPoint::terminal_voltage_V}, {"internal_voltage_V", &AcPoint::internal_voltage_V}, {"frequency_Hz", &AcPoint::frequency_Hz}, {"temperature_K", &AcPoint::temperature_K},
    {"capacitance_F", &AcPoint::capacitance_F}, {"conductance_S", &AcPoint::conductance_S}, {"G_over_omega_F", &AcPoint::G_over_omega_F}, {"inverse_C2_F_minus2", &AcPoint::inverse_C2_F_minus2},
    {"conservation_relative", &AcPoint::conservation_relative}, {"EQE", &AcPoint::EQE}, {"wavelength_nm", &AcPoint::wavelength_nm}, {"valid", &AcPoint::valid}};
const Field<IvResult> ivTop[] = {{"mesh_x_m", &IvResult::mesh_x_m}};
const Field<AcResult> acTop[] = {{"mesh_x_m", &AcResult::mesh_x_m}, {"kind", &AcResult::kind}, {"area_m2", &AcResult::area_m2}};
template<class R, class P, size_t N, size_t M> R FromJson(const Json &j, const Field<R> (&top)[N], const Field<P> (&pf)[M]) {
    R r;
    if (!j.is_object()) return r;
    Json rest = j; // `points` handled separately, the rest goes through the field table
    if (rest.contains("points")) r.has |= 1u << 31;
    Json pts = rest.contains("points") && rest["points"].is_array() ? std::move(rest["points"]) : Json::array();
    rest.erase("points");
    Read(rest, r, r.has, r.meta, top);
    r.points.resize(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        if (!pts[i].is_object()) {r.points[i].rest = pts[i]; continue;} // ponytail: non-object point kept verbatim in rest (rest is object-typed by Read otherwise)
        Read(pts[i], r.points[i], r.points[i].has, r.points[i].rest, pf);
    }
    return r;
}
template<class R, size_t N, class P, size_t M> Json To(const R &r, const Field<R> (&top)[N], const Field<P> (&pf)[M]) {
    Json o = r.meta.is_object() ? r.meta : Json::object();
    Write(r, r.has, o, top);
    auto pts = Json::array();
    for (const auto &p : r.points) {
        if (!p.rest.is_object()) {pts.push_back(p.rest); continue;}
        Json po = p.rest; Write(p, p.has, po, pf); pts.push_back(std::move(po));
    }
    if (r.has >> 31 & 1u) o["points"] = std::move(pts);
    return o;
}
} // namespace
IvResult IvFromJson(const Json &j) {return FromJson<IvResult>(j, ivTop, ivFields);}
AcResult AcFromJson(const Json &j) {return FromJson<AcResult>(j, acTop, acFields);}
Json ToJson(const IvResult &r) {return To(r, ivTop, ivFields);}
Json ToJson(const AcResult &r) {return To(r, acTop, acFields);}
} // namespace SST::ScapsLab
