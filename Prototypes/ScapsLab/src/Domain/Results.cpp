#include <scaps_core/boundary_probe.hpp>
#include "Domain/Results.hpp"
#include <algorithm>
#include <cmath>
#include <variant>
namespace SST::ScapsLab {
namespace {
Json DetailsJson(const IvDetails& details) {
    const auto& g=details.generation_recombination;
    Json gr={{"x_m",g.x_m.get()},{"generation_m3_s",g.generation_m3_s.get()},{"layers",Json::array()},{"channels",Json::array()},
        {"generation_current_A_m2",g.generation_current_A_m2},{"recombination_current_A_m2",g.recombination_current_A_m2},{"net_generation_current_A_m2",g.net_generation_current_A_m2},
        {"contact_loss_current_A_m2",g.contact_loss_current_A_m2},{"shunt_current_A_m2",g.shunt_current_A_m2},{"series_current_A_m2",g.series_current_A_m2},{"predicted_terminal_current_A_m2",g.predicted_terminal_current_A_m2},
        {"terminal_budget_relative_error",FiniteOrNull(g.terminal_budget_relative_error)},{"terminal_budget_valid",g.terminal_budget_valid},{"valid",g.valid},
        {"terminal_budget_identity","J = G - R - right.electron - left.hole + J_shunt (native current sign)"},{"sheet_layer_convention","interface assigned to left layer"},{"contact_recombination",Json::array()}};
    for(const auto& l:g.layers)gr["layers"].push_back({{"layer_id",l.layer_id},{"generation_current_A_m2",l.generation_current_A_m2},{"recombination_current_A_m2",l.recombination_current_A_m2},{"mechanisms",l.mechanisms}});
    for(const auto& c:g.contact_recombination)gr["contact_recombination"].push_back({{"id",c.id},{"electron_current_A_m2",c.electron_current_A_m2},{"hole_current_A_m2",c.hole_current_A_m2}});
    for(const auto& c:g.channels) {
        Json layers=Json::array();for(const auto& l:c.by_layer)layers.push_back({{"layer_id",l.layer_id},{"current_A_m2",l.current_A_m2}});
        gr["channels"].push_back({{"id",c.id},{"engine_id",c.engine_id},{"mechanism",c.mechanism},{"x_m",c.x_m.get()},{"R_m3_s",c.R_m3_s.get()},{"R_sheet_m2_s",c.R_sheet_m2_s.get()},
            {"integrated_current_A_m2",c.integrated_current_A_m2},{"by_layer",std::move(layers)},{"layer_sum_relative_error",c.layer_sum_relative_error},{"valid",c.valid},{"validity_status",c.validity_status}});
    }
    Json defects=Json::array();
    for(const auto& d:details.defect_previews) {
        Json curve={{"id",d.id},{"label",d.label},{"sheet",d.sheet},{"Ev_eV",d.Ev_eV},{"Ec_eV",d.Ec_eV},{"energy_eV",d.energy_eV},{d.sheet?"density_m2_eV":"density_m3_eV",d.density},
            {"occupation_energy_eV",d.occupation_energy_eV},{"occupation",Json::array()},{"single",d.single},{"level_energy_eV",d.level_energy_eV},{d.sheet?"Nt_m2":"Nt_m3",d.Nt},{"amphoteric",d.amphoteric},{"correlated",d.correlated},{"band_edges_eV",d.band_edges_eV}};
        for(double x:d.occupation)curve["occupation"].push_back(FiniteOrNull(x));
        if(d.second_level_energy_eV)curve["second_level_energy_eV"]=*d.second_level_energy_eV;
        if(d.sheet)curve["left_layer_index"]=d.left_layer_index;else curve["layer_id"]=d.layer_id;
        defects.push_back(std::move(curve));
    }
    return {{"generation_recombination",std::move(gr)},{"defect_previews",std::move(defects)},{"trap_occupation",details.trap_occupation},{"interface_occupation",details.interface_occupation}};
}
double Number(const Json& n,const char* key,double fallback=NAN) {return n.contains(key) && n.at(key).is_number()?n.at(key).get<double>():fallback;}
std::vector<double> Numbers(const Json& n) {std::vector<double> out;for(const auto& value:n)out.push_back(value.is_number()?value.get<double>():double(NAN));return out;}
Ref<const IvDetails> ReadDetails(const Json& point) {
    if(!point.contains("generation_recombination") || !point.contains("defect_previews"))return {};
    try {
        auto out=CreateRef<IvDetails>();auto& g=out->generation_recombination;const auto& n=point.at("generation_recombination");
        g.x_m=Column::Intern(Numbers(n.at("x_m")));g.generation_m3_s=Column::Intern(Numbers(n.at("generation_m3_s")));
        for(const auto& l:n.at("layers"))g.layers.push_back({l.at("layer_id"),Number(l,"generation_current_A_m2"),Number(l,"recombination_current_A_m2"),l.at("mechanisms").get<std::map<std::string,double>>()});
        for(const auto& c:n.at("channels")) {
            RecombinationChannel channel;channel.id=c.at("id");channel.engine_id=c.at("engine_id");channel.mechanism=c.at("mechanism");channel.x_m=Column::Intern(Numbers(c.at("x_m")));channel.R_m3_s=Column::Intern(Numbers(c.at("R_m3_s")));channel.R_sheet_m2_s=Column::Intern(Numbers(c.at("R_sheet_m2_s")));channel.integrated_current_A_m2=Number(c,"integrated_current_A_m2");channel.layer_sum_relative_error=Number(c,"layer_sum_relative_error");channel.valid=c.value("valid",false);channel.validity_status=c.value("validity_status",std::string("unknown"));
            for(const auto& l:c.at("by_layer"))channel.by_layer.push_back({l.at("layer_id"),Number(l,"current_A_m2")});g.channels.push_back(std::move(channel));
        }
        for(const auto& c:n.value("contact_recombination",Json::array())){scaps_core::ContactRecombination contact;contact.id=c.at("id");contact.electron_current_A_m2=Number(c,"electron_current_A_m2");contact.hole_current_A_m2=Number(c,"hole_current_A_m2");g.contact_recombination.push_back(std::move(contact));}
#define READ(field) g.field=Number(n,#field)
        READ(generation_current_A_m2);READ(recombination_current_A_m2);READ(net_generation_current_A_m2);READ(contact_loss_current_A_m2);READ(shunt_current_A_m2);READ(series_current_A_m2);READ(predicted_terminal_current_A_m2);READ(terminal_budget_relative_error);
#undef READ
        g.valid=n.value("valid",false);g.terminal_budget_valid=n.value("terminal_budget_valid",false);g.has_terminal_budget=n.contains("predicted_terminal_current_A_m2");
        for(const auto& c:point.at("defect_previews")) {
            DefectPreview d;d.id=c.at("id");d.label=c.at("label");d.sheet=c.at("sheet");d.single=c.at("single");d.Ev_eV=Number(c,"Ev_eV");d.Ec_eV=Number(c,"Ec_eV");d.level_energy_eV=Number(c,"level_energy_eV");d.Nt=Number(c,d.sheet?"Nt_m2":"Nt_m3");d.amphoteric=c.value("amphoteric",false);d.correlated=c.value("correlated",true);d.layer_id=c.value("layer_id",std::string{});d.left_layer_index=c.value("left_layer_index",-1);
            if(c.contains("second_level_energy_eV"))d.second_level_energy_eV=Number(c,"second_level_energy_eV");
            d.energy_eV=Numbers(c.at("energy_eV"));d.density=Numbers(c.at(d.sheet?"density_m2_eV":"density_m3_eV"));d.occupation_energy_eV=Numbers(c.at("occupation_energy_eV"));d.occupation=Numbers(c.at("occupation"));d.band_edges_eV=Numbers(c.at("band_edges_eV"));out->defect_previews.push_back(std::move(d));
        }
        out->trap_occupation=point.value("trap_occupation",std::vector<std::vector<double>>{});out->interface_occupation=point.value("interface_occupation",std::vector<double>{});
        Json original;for(const auto* key:{"generation_recombination","defect_previews","trap_occupation","interface_occupation"})if(point.contains(key))original[key]=point.at(key);
        out->preserve_original=DetailsJson(*out).dump()!=original.dump();return out;
    } catch(const Json::exception&){return {};}
}
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
        if (!taken) {
            // Historical integer spellings stay verbatim at the file edge; plots still receive native numbers.
            for(const auto& field:fields)if(it.key()==field.key) {
                if(const auto member=std::get_if<double T::*>(&field.m);member && it.value().is_number())t.**member=it.value().template get<double>();
                if(const auto member=std::get_if<Column T::*>(&field.m);member && it.value().is_array() && std::all_of(it.value().begin(),it.value().end(),[](const Json& v){return v.is_number() || v.is_null();}))t.**member=Column::Intern(Numbers(it.value()));
            }
            rest[it.key()] = Take(it.value());
        }
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
    {"conservation_relative", &AcPoint::conservation_relative}, {"EQE", &AcPoint::EQE}, {"wavelength_nm", &AcPoint::wavelength_nm}, {"valid", &AcPoint::valid},
    {"incident_photon_flux_m2_s", &AcPoint::incident_photon_flux_m2_s}, {"probe_power_W_m2", &AcPoint::probe_power_W_m2}};
const Field<IvResult> ivTop[] = {{"mesh_x_m", &IvResult::mesh_x_m}};
const Field<AcResult> acTop[] = {{"mesh_x_m", &AcResult::mesh_x_m}, {"kind", &AcResult::kind}, {"area_m2", &AcResult::area_m2}};
template<class J, class R, class P, size_t N, size_t M> R FromJson(J &j, const Field<R> (&top)[N], const Field<P> (&pf)[M]) {
    R r;
    if (!j.is_object()) return r;
    Json meta;
    Read(j, r, r.has, meta, top, "points");
    if constexpr(std::is_same_v<R,IvResult>) {
        if(meta.contains("optics"))try {const auto& o=meta.at("optics");OpticsResult optics;optics.x_m=Column::Intern(Numbers(o.at("x_m")));optics.generation_m3_s=Column::Intern(Numbers(o.at("generation_m3_s")));optics.incident_power_W_m2=Number(o,"incident_power_W_m2");for(const auto& c:o.at("absorption"))optics.absorption.push_back({c.at("layer_id"),Column::Intern(Numbers(c.at("wavelength_nm"))),Column::Intern(Numbers(c.at("alpha_m1"))),Column::Intern(Numbers(c.at("without_tail_m1")))});IvResult check;check.optics=std::move(optics);r.preserve_optics=check.Dump().at("optics").dump()!=o.dump();r.optics=std::move(check.optics);if(!r.preserve_optics)meta.erase("optics");}catch(const Json::exception&){}
        if(meta.contains("photovoltaic"))try {const auto& o=meta.at("photovoltaic");scaps_core::PhotovoltaicResult pv;for(auto v:{scaps_core::PhotovoltaicStatus::Converged,scaps_core::PhotovoltaicStatus::SolveFailed,scaps_core::PhotovoltaicStatus::OpenCircuitNotBracketed,scaps_core::PhotovoltaicStatus::NoPhotovoltaicPower})if(o.at("status")==scaps_core::to_string(v))pv.status=v;pv.message=o.value("message",std::string{});pv.Voc_V=-Number(o,"Voc_V");pv.Jsc_Am2=-Number(o,"Jsc_A_m2");pv.FF=Number(o,"FF_fraction");pv.eta=Number(o,"efficiency_fraction");pv.Pmax_Wm2=Number(o,"Pmax_W_m2");pv.Vmpp_V=-Number(o,"Vmpp_V");pv.Jmpp_Am2=Number(o,"Jmpp_A_m2");IvResult check;check.photovoltaic=std::move(pv);r.preserve_photovoltaic=check.Dump().at("photovoltaic").dump()!=o.dump();r.photovoltaic=std::move(check.photovoltaic);if(!r.preserve_photovoltaic)meta.erase("photovoltaic");}catch(const Json::exception&){}
    }
    r.meta.prim = std::move(meta);
    if (j.contains("points")) r.has |= 1u << 31;
    J *pts = j.contains("points") && j["points"].is_array() ? &j["points"] : nullptr;
    const size_t n = pts ? pts->size() : 0;
    r.points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        auto &p = r.points[i]; J &src = (*pts)[i];
        if (!src.is_object()) {p.verbatim = true; p.rest.prim = src; continue;} // ponytail: non-object point kept verbatim
        if constexpr(std::is_same_v<P,IvPoint>)p.details=ReadDetails(src);
        Json rest; Read(src, p, p.has, rest, pf);
        if constexpr(std::is_same_v<P,IvPoint>) {if(p.details && !p.details->preserve_original)for(const auto* key:{"generation_recombination","defect_previews","trap_occupation","interface_occupation"})rest.erase(key);}
        p.rest = Pack(rest);
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
        Json po = Unpack(p.rest);
        if constexpr(std::is_same_v<std::decay_t<decltype(p)>,IvPoint>)if(p.details && !p.details->preserve_original){if(!po.is_object())po=Json::object();po.update(DetailsJson(*p.details));}
        if(!po.is_object())po=Json::object();
        Write(p, p.has, po, pf); pts.push_back(std::move(po));
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
BatchResult BatchFromJson(const Json &j) {
    BatchResult r;r.native=true;r.body.prim=j;r.body.prim.erase("rows");
    const auto number=[](const Json& n,const char* key){return n.contains(key) && n.at(key).is_number()?n.at(key).get<double>():double(NAN);};
    const auto status=[](const Json& n){for(auto v:{scaps_core::SolverStatus::NotAttempted,scaps_core::SolverStatus::Converged,scaps_core::SolverStatus::MaxIterationsReached,scaps_core::SolverStatus::DivergedNaN,scaps_core::SolverStatus::LinearSolveFailed})if(n==scaps_core::to_string(v))return v;return scaps_core::SolverStatus::NotAttempted;};
    try {for(const auto& n:j.at("rows")) {
        auto row=CreateRef<scaps_core::BatchRow>();row->id=n.at("id");row->status=n.at("status");row->error_type=n.value("error_type",std::string{});row->message=n.value("message",std::string{});row->project_hash=n.value("project_hash",std::string{});row->elapsed_s=n.value("elapsed_s",0.);row->attempt=n.value("attempt",1);row->thermal_voltage_V=number(n,"thermal_voltage_V");
        for(const auto& [key,value]:n.at("parameters").items()) {
            if(value.is_string())row->parameters[key]=value.get<std::string>();else if(value.is_boolean())row->parameters[key]=value.get<bool>();else if(value.is_number_float())row->parameters[key]=value.get<double>();else if(value.is_number_integer())row->parameters[key]=value.get<std::int64_t>();else row->parameters[key]=nullptr;
        }
        for(const auto& [id,value]:n.at("values").items())row->values[id]={value.is_number()?value.get<double>():double(NAN),n.at("statuses").at(id).get<std::string>()};
        if(n.contains("source")){const auto& source=n.at("source");row->source=scaps_core::BatchSource{source.value("path",std::string{}),source.value("file_hash",std::string{}),source.value("action_hash",std::string{})};}
        row->curves_retained=n.contains("curves");
        if(row->curves_retained) {
            for(const auto& p:n.at("curves").at("iv")) {
                scaps_core::IVSample point;point.voltage_V=number(p,"voltage_V");point.J_Am2=number(p,"J_Am2");point.J_left_contact_Am2=number(p,"J_left_contact_Am2");point.status=status(p.at("status"));point.iterations=p.value("iterations",0);point.residual=number(p,"residual");point.current_conservation_error_Am2=number(p,"current_conservation_error_Am2");
                if(p.contains("profiles")){auto profile=CreateRef<scaps_core::BatchProfile>();profile->x_m=p.at("profiles").at("x_m").get<std::vector<double>>();for(const auto& [field,values]:p.at("profiles").items())if(field!="x_m"){auto& out=profile->fields[field];for(const auto& value:values)out.push_back(value.is_number()?value.get<double>():double(NAN));}point.profiles=std::move(profile);}
                row->iv.push_back(std::move(point));
            }
            for(const auto& p:n.at("curves").at("ac"))row->ac.push_back({number(p,"voltage_V"),number(p,"frequency_Hz"),number(p,"C_F"),number(p,"G_S"),status(p.at("status"))});
        }
        if(n.contains("photovoltaic")) {
            row->photovoltaic_requested=true;const auto& pv=n.at("photovoltaic");row->incident_power_W_m2=number(pv,"incident_power_W_m2");
            if(pv.at("status")!="missing_incident_power") {scaps_core::PhotovoltaicResult p;for(auto v:{scaps_core::PhotovoltaicStatus::Converged,scaps_core::PhotovoltaicStatus::SolveFailed,scaps_core::PhotovoltaicStatus::OpenCircuitNotBracketed,scaps_core::PhotovoltaicStatus::NoPhotovoltaicPower})if(pv.at("status")==scaps_core::to_string(v))p.status=v;p.message=pv.value("message",std::string{});p.Voc_V=-number(pv,"Voc_V");p.Jsc_Am2=number(pv,"Jsc_A_m2");p.FF=number(pv,"FF");p.eta=number(pv,"eta");p.Pmax_Wm2=number(pv,"Pmax_W_m2");p.Vmpp_V=-number(pv,"Vmpp_V");row->photovoltaic=std::move(p);}
        }
        if(n.contains("metastable")) {
            auto m=CreateRef<scaps_core::BatchConditioningMetadata>();const auto& input=n.at("metastable");m->contact_work_functions_eV=input.value("contact_work_functions_eV",std::map<std::string,double>{});m->frozen=input.contains("iterations");
            if(m->frozen) {
                m->iterations=input.at("iterations");m->relative_error=number(input,"relative_error");m->relative_rate_residual=number(input,"relative_rate_residual");m->initial_temperature_K=number(input,"initial_temperature_K");m->initial_voltage_V=number(input,"initial_voltage_V");
                for(const auto& p:input.at("profiles")) {scaps_core::BatchMetastableProfile profile;profile.id=p.at("id");profile.transition_id=p.at("transition_id");profile.layer_index=p.at("layer_index");profile.x_m=number(p,"x_m");profile.donor_density_m3=number(p,"donor_density_m3");profile.acceptor_density_m3=number(p,"acceptor_density_m3");profile.EC_eV=number(p,"EC_eV");profile.HE_eV=number(p,"HE_eV");profile.EE_eV=number(p,"EE_eV");profile.HC_eV=number(p,"HC_eV");profile.TR_above_Ev_eV=number(p,"TR_above_Ev_eV");m->profiles.push_back(std::move(profile));}
            }
            row->metastable=std::move(m);
        }
        r.rows.push_back(std::move(row));
    }} catch(const Json::exception&) {r.rows.clear();}
    // Preserve unknown historical fields and numeric representations at the file import edge.
    if(r.Dump().dump()!=j.dump()){r.legacySchema=std::move(r.body.prim);r.body=Pack(j);r.native=false;}
    if(j.contains("project_snapshot") && j.contains("spec")) {
        // Failed members retain rejected input as file evidence, never as a replayable live draft.
        if(std::any_of(r.rows.begin(),r.rows.end(),[](const auto& row){return row->status=="Converged";}))r.projectSnapshot=DraftFromDocument(j.at("project_snapshot"));
        r.batchDraft=ReadBatchDraft(j.at("spec"));
        r.recorderDraft=ReadRecorderDraft(j.value("columns",Json::array()));
    }
    return r;
}
const Column& PointColumn(const IvPoint& p,const std::string& key) {
    for(const auto& field:ivFields)if(key==field.key)if(const auto column=std::get_if<Column IvPoint::*>(&field.m))return p.**column;
    static const Column none;return none;
}
const IvPoint& PointAtBias(const IvResult& sweep,const IvResult& band,int selected) {
    if(sweep.points.empty())return band.points.at(0);
    const auto& point=sweep.points.at(std::clamp(selected,0,int(sweep.points.size())-1));
    return !band.points.empty() && band.points[0].voltage_V==point.voltage_V?band.points[0]:point;
}
Json BatchRowJson(const scaps_core::BatchRow& row) {SCAPS_BOUNDARY_RESULT();
    Json parameters=Json::object();for(const auto& [key,value]:row.parameters)std::visit([&](const auto& v){parameters[key]=v;},value);
    Json out={{"id",row.id},{"parameters",std::move(parameters)},{"status",row.status},{"error_type",row.error_type},{"message",row.message},{"elapsed_s",row.elapsed_s},{"attempt",row.attempt},{"values",Json::object()},{"statuses",Json::object()}};
    if(!row.project_hash.empty())out["project_hash"]=row.project_hash;
    if(row.source){out["source"]={{"path",row.source->path}};if(!row.source->file_hash.empty())out["source"]["file_hash"]=row.source->file_hash;if(!row.source->action_hash.empty())out["source"]["action_hash"]=row.source->action_hash;}
    if(row.initial_state) {
        const auto& s=*row.initial_state;const auto& l=s.illumination;
        const auto optical=[](const auto& table){auto a=Json::array();for(const auto& point:table)a.push_back({point.first,point.second});return a;};
        out["initial_state"]={{"temperature_K",s.temperature_K},{"voltage_V",s.voltage_V},{"number_of_points",s.number_of_points},{"generation_total_m2_s",s.generation_total_m2_s?Json(*s.generation_total_m2_s):Json(nullptr)},
            {"illumination",{{"enabled",l.enabled},{"uniform_generation_m3_s",l.uniform_generation_m3s},{"mode",l.mode},{"generation_quadrature",l.generation_quadrature},{"intensity_multiplier",l.intensity_multiplier},{"from_right",l.from_right},{"spectrum_file",l.spectrum_file},{"generation_file",l.generation_file},
                {"spectrum_nm_W_m2",optical(l.spectrum_nm_W_m2)},{"generation_m_m3s",optical(l.generation_m_m3s)},{"wavelength_nm",l.wavelength_nm},{"monochromatic_power_W_m2",l.monochromatic_power_W_m2},{"wavelength_min_nm",l.wavelength_min_nm},{"wavelength_max_nm",l.wavelength_max_nm},
                {"front_transmission",l.front_transmission},{"back_reflection",l.back_reflection},{"front_internal_reflection",l.front_internal_reflection},{"front_filter_file",l.front_filter_file},{"back_filter_file",l.back_filter_file},{"front_filter_nm_fraction",optical(l.front_filter_nm_fraction)},{"back_filter_nm_fraction",optical(l.back_filter_nm_fraction)}}}};
        out["initial_state"]["illumination"]["wavelength_max_nm"]=FiniteOrNull(l.wavelength_max_nm);
        out["initial_state_hash"]=s.hash;out["initial_state_status"]=row.initial_state_status;
        if(!s.wp2.empty())out["initial_state_wp2"]=s.wp2;
        if(s.source)out["initial_state_source"]={{"path",s.source->path},{"file_hash",s.source->file_hash}};
    }
    if(std::isfinite(row.thermal_voltage_V))out["thermal_voltage_V"]=row.thermal_voltage_V;
    for(const auto& [id,value]:row.values){out["values"][id]=FiniteOrNull(value.value);out["statuses"][id]=value.status;}
    if(row.curves_retained) {
        auto iv=Json::array(),ac=Json::array();
        for(const auto& p:row.iv) {
            Json point={{"voltage_V",p.voltage_V},{"J_Am2",FiniteOrNull(p.J_Am2)},{"J_left_contact_Am2",FiniteOrNull(p.J_left_contact_Am2)},{"status",scaps_core::to_string(p.status)},{"iterations",p.iterations},{"residual",FiniteOrNull(p.residual)},{"current_conservation_error_Am2",FiniteOrNull(p.current_conservation_error_Am2)}};
            if(p.profiles){point["profiles"]["x_m"]=p.profiles->x_m;for(const auto& [key,value]:p.profiles->fields)point["profiles"][key]=value;}
            iv.push_back(std::move(point));
        }
        for(const auto& p:row.ac)ac.push_back({{"voltage_V",p.voltage_V},{"frequency_Hz",p.frequency_Hz},{"status",scaps_core::to_string(p.status)},{"C_F",FiniteOrNull(p.C_F)},{"G_S",FiniteOrNull(p.G_S)}});
        out["curves"]={{"iv",std::move(iv)},{"ac",std::move(ac)}};
    }
    if(row.photovoltaic_requested) {
        if(!row.photovoltaic)out["photovoltaic"]={{"status","missing_incident_power"}};
        else {const auto& p=*row.photovoltaic;out["photovoltaic"]={{"status",scaps_core::to_string(p.status)},{"message",p.message},{"Voc_V",FiniteOrNull(-p.Voc_V)},{"Jsc_A_m2",FiniteOrNull(p.Jsc_Am2)},{"FF",FiniteOrNull(p.FF)},{"eta",FiniteOrNull(p.eta)},{"Pmax_W_m2",FiniteOrNull(p.Pmax_Wm2)},{"Vmpp_V",FiniteOrNull(-p.Vmpp_V)},{"incident_power_W_m2",FiniteOrNull(row.incident_power_W_m2)}};}
    }
    if(row.metastable) {
        const auto& m=*row.metastable;auto& metadata=out["metastable"];metadata=Json::object();
        if(!m.contact_work_functions_eV.empty())metadata["contact_work_functions_eV"]=m.contact_work_functions_eV;
        if(m.frozen) {
            metadata["iterations"]=m.iterations;metadata["relative_error"]=m.relative_error;metadata["relative_rate_residual"]=m.relative_rate_residual;metadata["initial_temperature_K"]=m.initial_temperature_K;metadata["initial_voltage_V"]=m.initial_voltage_V;metadata["profiles"]=Json::array();
            for(const auto& p:m.profiles)metadata["profiles"].push_back({{"id",p.id},{"transition_id",p.transition_id},{"layer_index",p.layer_index},{"x_m",p.x_m},{"donor_density_m3",p.donor_density_m3},{"acceptor_density_m3",p.acceptor_density_m3},{"EC_eV",p.EC_eV},{"HE_eV",p.HE_eV},{"EE_eV",p.EE_eV},{"HC_eV",p.HC_eV},{"TR_above_Ev_eV",p.TR_above_Ev_eV}});
        }
    }
    return out;
}
Json BatchResult::Dump() const {SCAPS_BOUNDARY_RESULT();
    auto out=legacyBase?legacyBase->Dump():Unpack(body);
    if(legacyBase) {
        // Preserve opaque file fields until the save/Python edge; internal Recorder/retry keeps shared typed rows.
        for(const auto& [key,value]:body.prim.items())out[key]=value;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& row=*rows[i];auto& saved=out["rows"][i];
            if(std::find(replacedRows.begin(),replacedRows.end(),row.id)!=replacedRows.end()){saved=BatchRowJson(row);continue;}
            saved["values"]=Json::object();saved["statuses"]=Json::object();
            for(const auto& [id,value]:row.values){saved["values"][id]=FiniteOrNull(value.value);saved["statuses"][id]=value.status;}
            if(saved.contains("curves") && saved["curves"].contains("iv"))for(auto& point:saved["curves"]["iv"])for(const auto& sample:row.iv)
                if(sample.profiles && point.value("voltage_V",double(NAN))==sample.voltage_V) {
                    auto& profiles=point["profiles"];if(!profiles.contains("x_m"))profiles["x_m"]=sample.profiles->x_m;
                    for(const auto& [field,values]:sample.profiles->fields)if(!profiles.contains(field))profiles[field]=values;
                }
        }
    }else if(native){out["rows"]=Json::array();for(const auto& row:rows)out["rows"].push_back(BatchRowJson(*row));}
    return out;
}
Json IvResult::Dump() const {SCAPS_BOUNDARY_RESULT();
    auto out=To(*this,ivTop,ivFields);
    if(optics && !preserve_optics) {
        Json curves=Json::array();for(const auto& c:optics->absorption)curves.push_back({{"layer_id",c.layer_id},{"wavelength_nm",c.wavelength_nm.get()},{"alpha_m1",c.alpha_m1.get()},{"without_tail_m1",c.without_tail_m1.get()}});
        out["optics"]={{"x_m",optics->x_m.get()},{"generation_m3_s",optics->generation_m3_s.get()},{"absorption",std::move(curves)},{"incident_power_W_m2",optics->incident_power_W_m2}};
    }
    if(photovoltaic && !preserve_photovoltaic) {const auto& p=*photovoltaic;out["photovoltaic"]={{"status",scaps_core::to_string(p.status)},{"message",p.message},{"Voc_V",FiniteOrNull(-p.Voc_V)},{"Jsc_A_m2",FiniteOrNull(-p.Jsc_Am2)},{"FF_fraction",FiniteOrNull(p.FF)},{"efficiency_fraction",FiniteOrNull(p.eta)},{"Pmax_W_m2",FiniteOrNull(p.Pmax_Wm2)},{"Vmpp_V",FiniteOrNull(-p.Vmpp_V)},{"Jmpp_A_m2",FiniteOrNull(p.Jmpp_Am2)}};}
    return out;
}
Json AcResult::Dump() const {SCAPS_BOUNDARY_RESULT();return To(*this, acTop, acFields);}
namespace {
struct Node {const PJson *p = nullptr; const Json *j = nullptr;}; // a packed node or a plain Json value
Node Step(Node n, const std::string &key) {
    if (n.p) {
        if (const auto *k = ChildOf(*n.p, key)) return {k, nullptr};
        if (n.p->prim.is_object() && n.p->prim.contains(key)) return {nullptr, &n.p->prim.at(key)};
        return {};
    }
    if (n.j && n.j->is_object() && n.j->contains(key)) return {nullptr, &n.j->at(key)};
    return {};
}
} // namespace
static void CurvePoint(BatchCurve &c, const Json &o) {
    if (!o.is_object() || !o.contains("voltage_V") || !o.at("voltage_V").is_number()) return;
    const bool num = o.contains("J_Am2") && o.at("J_Am2").is_number();
    c.v.push_back(o.at("voltage_V").get<double>());
    c.j.push_back(num ? o.at("J_Am2").get<double>() : double(NAN));
    c.converged.push_back(o.contains("status") && o.at("status") == "Converged");
}
static BatchCurve CurveOf(const Node &iv) {
    BatchCurve c;
    if (iv.p && iv.p->kind == PJson::Kind::Arr) for (const auto &k : iv.p->kids) CurvePoint(c, k.second.prim);   // scalars of a packed point stay in prim
    else if (iv.p && iv.p->prim.is_array()) for (const auto &e : iv.p->prim) CurvePoint(c, e);
    else if (iv.j && iv.j->is_array()) for (const auto &e : *iv.j) CurvePoint(c, e);
    return c;
}
std::vector<BatchCurve> BatchIvCurves(const BatchResult &r) {
    std::vector<BatchCurve> out;
    if(r.native) {for(const auto& row:r.rows){BatchCurve curve;for(const auto& p:row->iv){curve.v.push_back(p.voltage_V);curve.j.push_back(p.J_Am2);curve.converged.push_back(p.status==scaps_core::SolverStatus::Converged);}out.push_back(std::move(curve));}return out;}
    const Node rows = Step({&r.body, nullptr}, "rows");
    if (rows.p && rows.p->kind == PJson::Kind::Arr) {
        for (const auto &k : rows.p->kids) out.push_back(CurveOf(Step(Step({&k.second, nullptr}, "curves"), "iv")));
    } else if (rows.j && rows.j->is_array()) {
        for (const auto &row : *rows.j) out.push_back(CurveOf(Step(Step({nullptr, &row}, "curves"), "iv")));
    } else if (rows.p && rows.p->prim.is_array()) {
        for (const auto &row : rows.p->prim) out.push_back(CurveOf(Step(Step({nullptr, &row}, "curves"), "iv")));
    }
    return out;
}
bool HasProfiles(const IvResult &r) {return (r.has >> 31 & 1u) && ((r.has & 1u) || (r.meta.prim.is_object() && r.meta.prim.contains("mesh_x_m")));}
Ref<const IvResult> DropProfiles(const IvResult &r) {
    auto out = CreateRef<IvResult>(r);
    ClearVecs(*out, out->has, ivTop);
    if (out->meta.prim.is_object()) out->meta.prim.erase("mesh_x_m");
    for (auto &p : out->points) {if (p.verbatim) continue; ClearVecs(p, p.has, ivFields);p.details.reset(); DropStructured(p.rest);}
    return out;
}
const Json &ResultBase::View() const {
    std::lock_guard g(lock);
    if (!view) view = CreateRef<const Json>(Dump());
    used = std::chrono::steady_clock::now();
    return *view;
}
bool ResultBase::DropIdleView(std::chrono::steady_clock::time_point idleSince,Ref<const Json> *dropped) const {
    std::lock_guard g(lock);
    if (!view || used > idleSince) return false;
    if (dropped) *dropped = std::move(view); else view.reset();
    view.reset(); return true;
}
} // namespace SST::ScapsLab
