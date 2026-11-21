#include "Domain/Results.hpp"
#include <algorithm>
#include <cmath>
#include <variant>
namespace SST::ScapsLab {
namespace {
template<class T> using Member = std::variant<double T::*, Column T::*, std::string T::*, std::int64_t T::*, bool T::*>;
template<class T> struct Field {const char *key; Member<T> m;};
// Only the exact stored types are modelled: an integer where a double is expected (or an int in a float array) stays in `rest`, so the written Json is identical.
template<class T> bool Accepts(const Json &v, const Member<T> &m) {
    switch (m.index()) {
    case 0: return v.is_number_float() || v.is_null();
    case 1: return v.is_array() && std::all_of(v.begin(), v.end(), [](const Json &e) {return e.is_number_float() || e.is_null();});
    case 2: return v.is_string();
    case 3: return v.is_number_integer();
    default: return v.is_boolean();
    }
}
double Num(const Json &v) {return v.is_number_float() ? v.get<double>() : double(NAN);}
// Moves modelled keys of `src` into `t` (bit i of `has` = key i present); everything else goes to `rest` (skipping `skip`).
template<class J> decltype(auto) Take(J &v) {if constexpr (std::is_const_v<J>) return (v); else return std::move(v);} // consuming conversion moves, const conversion copies
template<class J, class T, size_t N> void Read(J &src, T &t, std::uint32_t &has, Json &rest, const Field<T> (&fields)[N], const char *skip = nullptr) {
    rest = Json::object();
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (skip && it.key() == skip) continue;
        bool taken = false;
        for (size_t i = 0; i < N && !taken; ++i) if (it.key() == fields[i].key && Accepts<T>(it.value(), fields[i].m)) {
            taken = true; has |= 1u << i;
            std::visit([&](auto p) {
                using V = std::remove_reference_t<decltype(t.*p)>;
                if constexpr (std::is_same_v<V, double>) t.*p = Num(it.value());
                else if constexpr (std::is_same_v<V, Column>) {std::vector<double> v; v.reserve(it.value().size()); for (const auto &e : it.value()) v.push_back(Num(e)); t.*p = Column::Intern(std::move(v));}
                else (t.*p) = it.value().template get<V>();
            }, fields[i].m);
        }
        if (!taken) rest[it.key()] = Take(it.value());
    }
}
template<class T, size_t N> void Write(const T &t, std::uint32_t has, Json &o, const Field<T> (&fields)[N]) {
    for (size_t i = 0; i < N; ++i) if (has >> i & 1u) std::visit([&](auto p) {
        using V = std::remove_cvref_t<decltype(t.*p)>;
        if constexpr (std::is_same_v<V, double>) o[fields[i].key] = FiniteOrNull(t.*p);
        else if constexpr (std::is_same_v<V, Column>) {auto a = Json::array(); for (double e : (t.*p).get()) a.push_back(FiniteOrNull(e)); o[fields[i].key] = std::move(a);}
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
template<class J, class R, class P, size_t N, size_t M> R FromJson(J &j, const Field<R> (&top)[N], const Field<P> (&pf)[M]) {
    R r;
    if (!j.is_object()) return r;
    Json meta;
    Read(j, r, r.has, meta, top, "points");
    r.meta = Pack(meta);
    if (j.contains("points")) r.has |= 1u << 31;
    J *pts = j.contains("points") && j["points"].is_array() ? &j["points"] : nullptr;
    const size_t n = pts ? pts->size() : 0;
    r.points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        auto &p = r.points[i]; J &src = (*pts)[i];
        if (!src.is_object()) {p.verbatim = true; p.rest.prim = src; continue;} // ponytail: non-object point kept verbatim
        Json rest; Read(src, p, p.has, rest, pf); p.rest = Pack(rest);
        if constexpr (!std::is_const_v<J>) src = Json(); // consumed: free the source point right away
    }
    return r;
}
template<class R, size_t N, class P, size_t M> Json To(const R &r, const Field<R> (&top)[N], const Field<P> (&pf)[M]) {
    Json o = r.meta.kind == PJson::Kind::Plain && !r.meta.prim.is_object() ? Json::object() : Unpack(r.meta);
    Write(r, r.has, o, top);
    auto pts = Json::array();
    for (const auto &p : r.points) {
        if (p.verbatim) {pts.push_back(p.rest.prim); continue;}
        Json po = Unpack(p.rest); Write(p, p.has, po, pf); pts.push_back(std::move(po));
    }
    if (r.has >> 31 & 1u) o["points"] = std::move(pts);
    return o;
}
template<class T, size_t N> void ClearVecs(T &t, std::uint32_t &has, const Field<T> (&fields)[N]) {
    using M = Column T::*;
    for (size_t i = 0; i < N; ++i) if (auto p = std::get_if<M>(&fields[i].m)) {t.**p = Column(); has &= ~(1u << i);}
}
template<class T, size_t N> bool Scalar(const T &t, const Field<T> (&f)[N], const std::string &key, double &out, bool &found) {
    found = false;
    for (size_t i = 0; i < N; ++i) if (key == f[i].key && (t.has >> i & 1u)) {
        found = true;
        if (auto d = std::get_if<double T::*>(&f[i].m)) {out = t.**d; return std::isfinite(out);}
        if (auto n = std::get_if<std::int64_t T::*>(&f[i].m)) {out = double(t.**n); return true;}
        return false;
    }
    return false;
}
bool RestNumber(const PJson &rest, const std::string &key, double &out) {
    if (!rest.prim.is_object() || !rest.prim.contains(key) || !rest.prim.at(key).is_number()) return false;
    out = rest.prim.at(key).get<double>(); return true;
}
} // namespace
bool PointNumber(const IvPoint &p, const std::string &key, double &out) {bool found; const bool ok = Scalar(p, ivFields, key, out, found); return found ? ok : (!p.verbatim && RestNumber(p.rest, key, out));}
bool PointNumber(const AcPoint &p, const std::string &key, double &out) {bool found; const bool ok = Scalar(p, acFields, key, out, found); return found ? ok : (!p.verbatim && RestNumber(p.rest, key, out));}
bool PointArray(const IvPoint &p, const std::string &key, std::vector<double> &out) {
    using M = Column IvPoint::*;
    for (size_t i = 0; i < std::size(ivFields); ++i) if (key == ivFields[i].key && (p.has >> i & 1u))
        if (auto c = std::get_if<M>(&ivFields[i].m)) {out = (p.**c).get(); return true;}
    return !p.verbatim && NumbersOf(p.rest, key, out);
}
const PJson *ChildOf(const PJson &obj, const std::string &key) {
    if (obj.kind != PJson::Kind::Obj) return nullptr;
    for (const auto &k : obj.kids) if (k.first == key) return &k.second;
    return nullptr;
}
bool NumbersOf(const PJson &obj, const std::string &key, std::vector<double> &out) {
    if (const auto *k = ChildOf(obj, key)) {if (k->kind != PJson::Kind::Floats) return false; out = k->f.get(); return true;}
    if (!obj.prim.is_object() || !obj.prim.contains(key) || !obj.prim.at(key).is_array()) return false;
    out.clear(); for (const auto &e : obj.prim.at(key)) {if (!e.is_number()) return false; out.push_back(e.get<double>());}
    return true;
}
IvResult IvFromJson(const Json &j) {return FromJson(j, ivTop, ivFields);}
IvResult IvFromJson(Json &&j) {return FromJson(j, ivTop, ivFields);}
AcResult AcFromJson(const Json &j) {return FromJson(j, acTop, acFields);}
AcResult AcFromJson(Json &&j) {return FromJson(j, acTop, acFields);}
BatchResult BatchFromJson(const Json &j) {BatchResult r; r.body = Pack(j); return r;}
Json IvResult::Dump() const {return To(*this, ivTop, ivFields);}
Json AcResult::Dump() const {return To(*this, acTop, acFields);}
bool HasProfiles(const IvResult &r) {return (r.has >> 31 & 1u) && ((r.has & 1u) || (r.meta.prim.is_object() && r.meta.prim.contains("mesh_x_m")));}
Ref<const IvResult> DropProfiles(const IvResult &r) {
    auto out = CreateRef<IvResult>(r);
    ClearVecs(*out, out->has, ivTop);
    if (out->meta.prim.is_object()) out->meta.prim.erase("mesh_x_m");
    for (auto &p : out->points) {if (p.verbatim) continue; ClearVecs(p, p.has, ivFields); DropStructured(p.rest);}
    return out;
}
const Json &ResultBase::View() const {
    std::lock_guard g(lock);
    if (!view) view = CreateRef<const Json>(Dump());
    used = std::chrono::steady_clock::now();
    return *view;
}
bool ResultBase::DropIdleView(std::chrono::steady_clock::time_point idleSince) const {
    std::lock_guard g(lock);
    if (!view || used > idleSince) return false;
    view.reset(); return true;
}
} // namespace SST::ScapsLab
