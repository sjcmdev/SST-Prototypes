#pragma once
#include <cmath>
#include <algorithm>
#include <optional>
#include <set>
#include <nlohmann/json.hpp>
#include "Domain/CapLabCircuit.hpp"
#include <sst/plotlab/Extensions.hpp>
#include "Core/Utils/Memory.hpp"
#include "Core/App/DocumentState.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::CapLab
{
    namespace pl = sst::plotlab;
    using Json = nlohmann::json;
    struct Sample { pl::Id id; std::string name; std::optional<double> area_m2; Json metadata = Json::object(); };
    struct FrequencySweep
    {
        pl::Id id, sample_id, frequency, capacitance, resistance;
        std::optional<double> temperature_K;
    };
    struct Project
    {
        Ref<pl::Project> engine = CreateRef<pl::Project>("CapLab");
        std::vector<Sample> samples;
        std::vector<FrequencySweep> sweeps;
        Json measurements = Json::array(), historical = Json::array(), imports = Json::array(), analyses = Json::array(), defects = Json::array(), circuits = Json::array(), comparisons = Json::array();
        pl::Id graph = 0;
        pl::ImportProfiles importProfiles;
    };
    inline Json Nullable(std::optional<double> value) { return value ? Json(*value) : Json(nullptr); }
    inline std::optional<double> Optional(const Json &value)
    {
        if (value.is_null()) return {};
        const double v = value.get<double>();
        if (!std::isfinite(v) || v <= 0) throw std::runtime_error("Expected positive finite metadata.");
        return v;
    }
    inline void Validate(const Project &p)
    {
        (void)pl::importProfilesJson(p.importProfiles);
        for (const auto &[name,profile]:p.importProfiles) {
            const auto options=Json::parse(profile.extra.at("options"));
            if (!options.is_object() || (options.contains("profile") && !options.at("profile").is_string()))
                throw std::runtime_error("Invalid CapLab format profile options.");
        }
        std::set<pl::Id> samples, sweeps;
        for (const auto &s : p.samples)
        {
            if (!samples.insert(s.id).second || !std::holds_alternative<pl::Workbook>(p.engine->object(s.id).data))
                throw std::runtime_error("Invalid sample ID.");
            if (s.name != p.engine->object(s.id).name) throw std::runtime_error("Sample name mismatch.");
            if (!s.metadata.is_object()) throw std::runtime_error("Sample metadata must be an object.");
            Optional(Nullable(s.area_m2));
        }
        for (const auto &s : p.sweeps)
        {
            if (!sweeps.insert(s.id).second || !samples.contains(s.sample_id) || p.engine->object(s.id).parent != s.sample_id)
                throw std::runtime_error("Invalid sweep/sample reference.");
            const auto &sheet = std::get<pl::Worksheet>(p.engine->object(s.id).data);
            if (sheet.source.at("profile") != "lab_as_cp_rp") throw std::runtime_error("Unsupported frequency sweep variant.");
            if (sheet.rows.empty()) throw std::runtime_error("Empty sweep.");
            const auto rows = Json::parse(sheet.source.at("source_rows")).get<std::vector<std::size_t>>();
            if (rows.size() != sheet.rows.size() || std::set<std::size_t>(rows.begin(), rows.end()).size() != rows.size() || rows.front() == 0) throw std::runtime_error("Invalid source row mapping.");
            std::set<pl::Id> columns;
            for (const auto &[id, unit] : {std::pair{s.frequency, "Hz"}, {s.capacitance, "F"}, {s.resistance, "Ohm"}})
            {
                const auto &o = p.engine->object(id);
                const auto &c = std::get<pl::Column>(o.data);
                if (!columns.insert(id).second || o.parent != s.id || c.units != unit || c.formula)
                    throw std::runtime_error("Invalid measurement column.");
                const auto &values = std::get<std::vector<double>>(c.values);
                if (values.size() != sheet.rows.size()) throw std::runtime_error("Unequal measurement columns.");
                for (double v : values)
                    if (!std::isfinite(v) || (id != s.capacitance && v <= 0))
                        throw std::runtime_error("Invalid measurement data.");
            }
            Optional(Nullable(s.temperature_K));
        }
        for (const auto &m : p.measurements)
        {
            const auto id = m.at("id").get<pl::Id>(), sample = m.at("sample_id").get<pl::Id>();
            if (!sweeps.insert(id).second || !samples.contains(sample) || p.engine->object(id).parent != sample)
                throw std::runtime_error("Invalid measurement reference.");
            const auto &sheet = std::get<pl::Worksheet>(p.engine->object(id).data);
            const auto &meta = m.at("metadata");
            const auto names = meta.at("names").get<std::vector<std::string>>();
            const auto units = meta.at("units").get<std::vector<std::string>>();
            const auto rows = meta.at("source_rows").get<std::vector<std::size_t>>();
            if (rows.empty() || rows.size() != sheet.rows.size() || std::set<std::size_t>(rows.begin(), rows.end()).size() != rows.size() ||
                std::any_of(rows.begin(), rows.end(), [](auto r) { return r == 0; }) ||
                names.size() != units.size() || names.size() != m.at("columns").size())
                throw std::runtime_error("Invalid measurement columns/row mapping.");
            std::set<pl::Id> columnIds;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                auto cid = m.at("columns").at(i).get<pl::Id>();
                const auto &o = p.engine->object(cid);
                const auto &c = std::get<pl::Column>(o.data);
                const auto &values = std::get<std::vector<double>>(c.values);
                if (!columnIds.insert(cid).second || o.parent != id || c.units != units[i] || c.formula || values.size() != rows.size())
                    throw std::runtime_error("Invalid measurement column.");
                for (std::size_t r = 0; r < values.size(); ++r)
                {
                    const double v = values[r];
                    bool historicalInvalid = false;
                    for (const auto &q : meta.value("quality",Json::array()))
                        if (q.at("column") == i && q.at("source_row") == rows[r])
                            historicalInvalid = (meta.contains("historical_source") && std::isnan(v) && q.at("reason") == "historical_nonfinite" && q.at("value").is_null()) ||
                                (names[i] == "Rp" && v <= 0 && q.at("reason") == "nonpositive_Rp" && q.at("value") == v);
                    if ((!std::isfinite(v) || ((names[i] == "f" || names[i] == "Rp" || names[i] == "temperature_K" || names[i] == "frequency_Hz") && v <= 0)) && !historicalInvalid)
                        throw std::runtime_error("Invalid measurement value.");
                }
                if (names[i] == "f" && std::set<double>(values.begin(), values.end()).size() != values.size())
                    throw std::runtime_error("Duplicate frequencies.");
            }
            const auto variant = meta.at("variant").get<std::string>();
            const auto kindName = meta.at("kind").get<std::string>();
            std::vector<std::pair<std::string,std::string>> required;
            if (kindName == "FrequencySweep")
            {
                required = {{"f","Hz"}};
                if (variant == "Cp_Rp") { required.push_back({"C","F"}); required.push_back({"Rp","Ohm"}); }
                else if (variant == "C_G") { required.push_back({"C","F"}); required.push_back({"G","S"}); }
                else if (variant == "ReZ_ImZ") { required.push_back({"ReZ","Ohm"}); required.push_back({"ImZ","Ohm"}); }
                else throw std::runtime_error("Unsupported frequency variant.");
            }
            else if (kindName == "CVSweep") required = {{"V","V"},{"C","F"}};
            else if (kindName == "DLCPRaw") required = {{"V_bias","V"},{"deltaV","V"},{"C","F"}};
            else required = {{"W","m"},{"N_DL","m^-3"},{"V","V"},{"C0","F"},{"C1","F/V"},{"C2","F/V^2"},{"A2_C02","m^4/F^2"},{"W_CV","m"},{"N_CV","m^-3"}};
            for (const auto &[name,unit] : required)
            {
                const auto it = std::find(names.begin(),names.end(),name);
                if (it == names.end() || units[std::distance(names.begin(),it)] != unit)
                    throw std::runtime_error("Missing measurement column or invalid SI unit.");
            }
            if (std::set<std::string>(names.begin(),names.end()).size() != names.size()) throw std::runtime_error("Duplicate column names.");
            Optional(meta.at("temperature_K")); Optional(meta.at("frequency_Hz"));
            const auto kind = meta.at("kind").get<std::string>();
            if (kind != "FrequencySweep" && kind != "CVSweep" && kind != "DLCPRaw" && kind != "DLCPProfile" && kind != "CV-derived")
                throw std::runtime_error("Unsupported measurement kind.");
        }
        std::set<pl::Id> historicalIds;
        for (const auto &h : p.historical)
        {
            const auto sample = h.at("sample_id").get<pl::Id>();
            if (h.at("status") != "historical" || !samples.contains(sample))
                throw std::runtime_error("Invalid historical result.");
            std::set<pl::Id> inputs, references;
            for (const auto &m : p.measurements) if (m.at("sample_id") == sample) inputs.insert(m.at("id").get<pl::Id>());
            // Historical snapshots may reference removed domain inputs retained by saved plots.
            for (const auto &[id, object] : p.engine->objects())
                if (object->parent==sample && std::holds_alternative<pl::Worksheet>(object->data)) inputs.insert(id);
            references = inputs;
            const auto &data = h.at("data");
            auto entities = [&](const Json &items) {
                if (!items.is_array()) return;
                for (const auto &item : items)
                {
                    const auto id = item.at("id").get<pl::Id>();
                    if (!historicalIds.insert(id).second || p.engine->objects().contains(id) || item.at("status") != "historical")
                        throw std::runtime_error("Invalid historical entity.");
                    references.insert(id);
                }
            };
            entities(data.value("defects",Json::array()));
            if (data.contains("walter") && data.at("walter").is_object()) entities(data.at("walter").value("fits",Json::array()));
            auto reference = [&](const Json &value,const std::set<pl::Id> &allowed) {
                if (!value.is_null() && (!value.is_number_unsigned() || !allowed.contains(value.get<pl::Id>())))
                    throw std::runtime_error("Invalid historical reference.");
            };
            std::function<void(const Json &)> check = [&](const Json &value) {
                if (value.is_array()) { for (const auto &v : value) check(v); return; }
                if (!value.is_object()) return;
                if (value.contains("measurement_id")) reference(value.at("measurement_id"),inputs);
                if (value.contains("row_id") && !value.at("row_id").is_null())
                {
                    if (value.at("measurement_id").is_null()) throw std::runtime_error("Orphan historical row.");
                    const auto &rows = std::get<pl::Worksheet>(p.engine->object(value.at("measurement_id").get<pl::Id>()).data).rows;
                    if (std::find(rows.begin(),rows.end(),value.at("row_id").get<pl::Id>()) == rows.end()) throw std::runtime_error("Invalid historical row.");
                }
                if (value.contains("selections")) for (const auto &[key,id] : value.at("selections").items()) reference(id,references);
                if (value.contains("plot_browser_settings") && value.at("plot_browser_settings").is_object())
                    for (const auto &[key,ids] : value.at("plot_browser_settings").items()) for (const auto &id : ids) reference(id,references);
                for (const auto &[key,v] : value.items()) check(v);
            };
            check(data);
        }
        std::set<pl::Id> defectIds, pointIds;
        for (const auto &d : p.defects) {
            const auto id=d.at("id").get<pl::Id>();
            if (!id || !defectIds.insert(id).second || p.engine->objects().contains(id) || historicalIds.contains(id) ||
                !samples.contains(d.at("sample_id").get<pl::Id>()) || d.at("name").get<std::string>().empty() || !d.at("points").is_array())
                throw std::runtime_error("Invalid defect.");
            for (const auto &point : d.at("points")) {
                const auto pid=point.at("id").get<pl::Id>();
                if (!pid || !pointIds.insert(pid).second || p.engine->objects().contains(pid) || historicalIds.contains(pid)) throw std::runtime_error("Invalid point ID.");
                if (point.at("temperature_K").is_null() || point.at("frequency_Hz").is_null()) throw std::runtime_error("Missing point T/f.");
                Optional(point.at("temperature_K")); Optional(point.at("frequency_Hz"));
                point.at("enabled").get<bool>();
                if (point.at("provenance").get<std::string>().empty()) throw std::runtime_error("Missing point provenance.");
                if (point.at("measurement_id").is_null()) {
                    if (!point.at("row_id").is_null()) throw std::runtime_error("Orphan point row.");
                } else {
                    const auto mid=point.at("measurement_id").get<pl::Id>();
                    for (const auto &m : p.measurements) if (m.at("id")==mid && m.at("sample_id")!=d.at("sample_id")) throw std::runtime_error("Cross-sample defect point.");
                    for (const auto &m : p.sweeps) if (m.id==mid && m.sample_id!=d.at("sample_id")) throw std::runtime_error("Cross-sample defect point.");
                    // Missing measurements remain referenced, never rebound to another row.
                    if (sweeps.contains(mid)) {
                        const auto &rows=std::get<pl::Worksheet>(p.engine->object(mid).data).rows;
                        if (point.at("row_id").is_null() || std::find(rows.begin(),rows.end(),point.at("row_id").get<pl::Id>())==rows.end()) throw std::runtime_error("Invalid defect row.");
                    }
                }
            }
        }
        for (auto id : pointIds) if (defectIds.contains(id)) throw std::runtime_error("Conflicting defect/point ID.");
        if (!p.circuits.is_array()) throw std::runtime_error("Expected circuits array");
        std::set<pl::Id> circuitIds;
        for (const auto &c : p.circuits) {
            const auto id=c.at("id").get<pl::Id>();
            if (!id || !circuitIds.insert(id).second || p.engine->objects().contains(id) || historicalIds.contains(id) ||
                defectIds.contains(id) || pointIds.contains(id) || !samples.contains(c.at("sample_id").get<pl::Id>()) ||
                c.at("name").get<std::string>().empty()) throw std::runtime_error("Invalid circuit identity/sample");
            (void)ReadCircuit(c.at("schematic"));
        }
        std::set<pl::Id> analysisIds;
        for (const auto &a : p.analyses) {
            const auto id=a.at("id").get<pl::Id>();
            if (!id || !analysisIds.insert(id).second || p.engine->objects().contains(id) || historicalIds.contains(id) || defectIds.contains(id) || pointIds.contains(id) || circuitIds.contains(id))
                throw std::runtime_error("Invalid analysis identity.");
            const std::string status=a.at("status");
            if (status!="valid" && status!="conditional" && status!="invalid" && status!="stale" && status!="missing_input")
                throw std::runtime_error("Invalid analysis status.");
            if (a.at("kernel_version")!=1 || (a.at("type")!="CV" && a.at("type")!="DLCPPolynomial" && a.at("type")!="AS" && a.at("type")!="Arrhenius" && a.at("type")!="Circuit" && a.at("type")!="Walter" && a.at("type")!="WalterSaved") ||
                !a.at("inputs").is_array() || (a.at("inputs").empty() && a.at("type")!="Arrhenius" && a.at("type")!="WalterSaved" &&
                    !(a.at("type")=="Circuit" && a.value("grid_source",std::string("measurement"))=="model")) || !a.at("settings").is_object())
                throw std::runtime_error("Invalid analysis contract.");
            if (a.at("type")=="Walter" || a.at("type")=="WalterSaved") {
                if (!samples.contains(a.at("sample_id").get<pl::Id>()) ||
                    (a.at("model")!="Walter1996-linear-field" && a.at("model")!="ProPho-compat") ||
                    !a.at("spectra").is_array() || a.at("spectra").empty()) throw std::runtime_error("Invalid Walter result");
                if (a.at("type")=="Walter") {
                    if (!Optional(a.at("V_b_V")) || !Optional(a.at(a.at("model")=="ProPho-compat" ? "B_s_1" : "zeta_s_1_K_2")) ||
                        !a.at("E_F_eV").is_number() || !std::isfinite(a.at("E_F_eV").get<double>()) || !a.at("source_analyses").is_array()) throw std::runtime_error("Invalid Walter parameters/sources");
                    for (const auto &source : a.at("source_analyses"))
                        if (!source.at("id").is_number_unsigned() || (source.at("type")!="CV" && source.at("type")!="Arrhenius" && source.at("type")!="DLCPPolynomial") || source.at("status")!="valid") throw std::runtime_error("Invalid Walter source snapshot");
                } else if (a.at("compatibility")!="saved-spectrum-integral-only" || a.at("historical").at("sample_id")!=a.at("sample_id")) throw std::runtime_error("Invalid historical Walter scope");
                for (const auto &s : a.at("spectra")) {
                    const auto n=s.at("energy_eV").size();
                    if (n<2 || s.at("density_cm3_eV").size()!=n || s.at("quality").size()!=n) throw std::runtime_error("Unequal Walter arrays");
                    for (std::size_t i=0;i<n;++i) {
                        if (!s.at("energy_eV").at(i).is_number() || !std::isfinite(s.at("energy_eV").at(i).get<double>())) throw std::runtime_error("Invalid Walter energy");
                        const auto &v=s.at("density_cm3_eV").at(i);
                        if (!s.at("quality").at(i).is_string() || s.at("quality").at(i).get<std::string>().empty()) throw std::runtime_error("Missing Walter quality reason");
                        if ((!v.is_null() && (!v.is_number() || !std::isfinite(v.get<double>()))) || (v.is_null() && s.at("quality").at(i)=="valid")) throw std::runtime_error("Invalid Walter quality/value");
                    }
                    const auto &integral=s.at("integral");
                    const auto &range=integral.at("range_eV");
                    if (!range.is_array() || range.size()!=2 || !range.at(0).is_number() || !range.at(1).is_number() ||
                        !std::isfinite(range.at(0).get<double>()) || !std::isfinite(range.at(1).get<double>()) || range.at(0)>=range.at(1) ||
                        (integral.at("semantics")!="ProPho-minus-trapz" && integral.at("semantics")!="ascending-clipped-trapezoids")) throw std::runtime_error("Invalid Walter integration metadata");
                    const auto &v=integral.at("density_cm3");
                    if (!v.is_null() && (!v.is_number() || !std::isfinite(v.get<double>()))) throw std::runtime_error("Invalid Walter integral");
                    if (a.at("type")=="Walter") {
                        const auto &xd=s.at("x_d");
                        if (xd.at("value_m").is_null()) throw std::runtime_error("Missing Walter width");
                        Optional(xd.at("value_m"));
                        if ((xd.at("source")!="CV" && xd.at("source")!="DLCP" && xd.at("source")!="manual") ||
                            (xd.at("policy")!="block" && xd.at("policy")!="linear" && xd.at("policy")!="nearest")) throw std::runtime_error("Invalid Walter width source/policy");
                        if (s.at("row_ids").size()!=n || s.at("frequency_Hz").size()!=n || s.at("D_c_F_m2").size()!=n || s.at("field_factor_V_m_C").size()!=n) throw std::runtime_error("Unequal Walter provenance arrays");
                    }
                }
            }
            if (a.at("type")=="Circuit") {
                const auto n=a.at("frequency_Hz").size();
                const auto grid=a.value("grid_source",std::string("measurement"));
                const bool modelOnly=grid=="model";
                if ((grid!="model" && grid!="measurement") || (modelOnly && a.at("model")=="CircuitCore-AC-fit") ||
                    !samples.contains(a.at("sample_id").get<pl::Id>()) || n==0 || n>1201 || a.at("row_ids").size()!=(modelOnly ? 0 : n) ||
                    (a.at("model")!="CircuitCore-AC" && a.at("model")!="CircuitCore-AC-fit") || !a.at("diagnostics").is_array()) throw std::runtime_error("Invalid circuit result");
                if (a.at("model")=="CircuitCore-AC-fit") {
                    for (const char *key : {"score","initial_score","Z_floor_Ohm"}) {
                        const double value=a.at(key).get<double>();
                        if (!std::isfinite(value) || value<0 || (std::string(key)=="Z_floor_Ohm" && value==0)) throw std::runtime_error("Invalid IS fit score/floor");
                    }
                    if ((a.at("weighting")!="modulus_floor" && a.at("weighting")!="sigma_Ohm") ||
                        (a.at("stop_status")!="score_tolerance" && a.at("stop_status")!="max_iterations") ||
                        (!a.at("covariance").is_null() && !a.at("covariance").is_array()) || !a.at("parameters").is_array() || a.at("parameters").empty() ||
                        a.at("scale_Ohm").size()!=n || a.at("iterations").get<int>()<0 || a.at("invalid_evaluations").get<int>()<0)
                        throw std::runtime_error("Invalid IS fit metadata");
                    for (const auto &value : a.at("scale_Ohm")) if (value.is_null() || !Optional(value)) throw std::runtime_error("Invalid IS weighting scale");
                    (void)ReadCircuit(a.at("initial_circuit").at("schematic"));
                    if (a.at("initial_circuit").at("id")!=a.at("circuit").at("id")) throw std::runtime_error("Mismatched IS fit circuit");
                    std::set<std::pair<std::string,std::string>> parameters;
                    bool hasFree=false;
                    for (const auto &v : a.at("parameters")) {
                        const std::string name=v.at("element"), field=v.at("field");
                        const bool free=v.at("free"); hasFree |= free;
                        const double start=v.at("start"), fitted=v.at("fitted"), lo=v.at("min"), hi=v.at("max");
                        if (!std::isfinite(start) || !std::isfinite(fitted) || !std::isfinite(lo) || !std::isfinite(hi) ||
                            lo>hi || start<lo || start>hi || fitted<lo || fitted>hi || (free && lo==hi) || (!free && start!=fitted) ||
                            !parameters.emplace(name,field).second || v.at("unit").get<std::string>().empty()) throw std::runtime_error("Invalid IS fitted parameter");
                        if (v.contains("sigma") && !v.at("sigma").is_null() &&
                            (!free || !v.at("sigma").is_number() || !std::isfinite(v.at("sigma").get<double>()) || v.at("sigma").get<double>()<0))
                            throw std::runtime_error("Invalid IS parameter uncertainty");
                        bool found=false;
                        for (const auto &e : a.at("circuit").at("schematic").at("elements")) if (e.at("name")==name) {
                            found=true;
                            if (e.at(field)!=v.at("fitted")) throw std::runtime_error("IS fitted parameter differs from schematic");
                            const auto unit=field=="value" ? e.at("value_unit") : field=="secondary" ? e.at("secondary_unit") : a.at("circuit").at("schematic").at("parameter_units").at(field);
                            if (v.at("unit")!=unit) throw std::runtime_error("Invalid IS fit parameter unit");
                            for (const auto &initial : a.at("initial_circuit").at("schematic").at("elements"))
                                if (initial.at("name")==name && initial.at(field)!=v.at("start")) throw std::runtime_error("Invalid IS fit start");
                        }
                        if (!found) throw std::runtime_error("Missing IS fitted element");
                    }
                    if (!hasFree) throw std::runtime_error("No free IS fit parameters");
                    if (!a.at("covariance").is_null()) {
                        std::size_t count=0;
                        for (const auto& v : a.at("parameters")) if (v.at("free").get<bool>()) ++count;
                        const auto& covariance=a.at("covariance");
                        if (covariance.size()!=count) throw std::runtime_error("Invalid covariance dimension");
                        for (std::size_t i=0;i<count;++i) {
                            if (!covariance[i].is_array() || covariance[i].size()!=count) throw std::runtime_error("Invalid covariance row");
                            for (const auto& v : covariance[i]) if (!v.is_number() || !std::isfinite(v.get<double>())) throw std::runtime_error("Nonfinite covariance");
                            if (covariance[i][i].get<double>()<0) throw std::runtime_error("Negative covariance variance");
                        }
                    }
                }
                Optional(a.at("temperature_K"));
                if (a.at("temperature_K").is_null() || !std::isfinite(a.at("bias_V").get<double>())) throw std::runtime_error("Invalid IS T/bias");
                (void)ReadCircuit(a.at("circuit").at("schematic"));
                for (const auto *key : {"measured_ReZ_Ohm","measured_ImZ_Ohm","model_ReZ_Ohm","model_ImZ_Ohm","residual_ReZ_Ohm","residual_ImZ_Ohm"}) {
                    const auto size=modelOnly && !std::string_view(key).starts_with("model_") ? 0 : n;
                    if (!a.at(key).is_array() || a.at(key).size()!=size) throw std::runtime_error("Unequal circuit columns");
                    for (const auto &v : a.at(key)) if (!v.is_null() && (!v.is_number() || !std::isfinite(v.get<double>()))) throw std::runtime_error("Invalid IS value");
                }
                for (const auto &f : a.at("frequency_Hz")) if (f.is_null() || !f.is_number() || !std::isfinite(f.get<double>()) || f.get<double>()<=0) throw std::runtime_error("Invalid IS frequency");
            }
            if (a.at("type")=="AS" || a.at("type")=="Arrhenius") {
                if (!samples.contains(a.at("sample_id").get<pl::Id>())) throw std::runtime_error("Invalid AS sample.");
                auto finiteArray=[](const Json &values) {
                    if (!values.is_array()) throw std::runtime_error("Expected AS array.");
                    for (const auto &value : values) if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::runtime_error("Invalid AS value.");
                };
                if (a.at("type")=="AS") {
                    const auto n=a.at("frequency_Hz").size();
                    for (const auto *key : {"frequency_Hz","capacitance_F","processed_capacitance_F","D_F","raw_D_F"}) {
                        finiteArray(a.at(key)); if (a.at(key).size()!=n) throw std::runtime_error("Unequal AS columns.");
                    }
                    if (n<2 || a.at("row_ids").size()!=n || !a.at("smoothing").is_object() || !a.at("candidates").is_array()) throw std::runtime_error("Invalid AS preprocessing.");
                    for (const auto &c : a.at("candidates")) {
                        if (c.at("frequency_Hz").is_null()) throw std::runtime_error("Missing candidate frequency.");
                        Optional(c.at("frequency_Hz"));
                        if (std::find(a.at("row_ids").begin(),a.at("row_ids").end(),c.at("row_id"))==a.at("row_ids").end()) throw std::runtime_error("Invalid candidate row.");
                    }
                } else {
                    if (!a.at("defect").is_object() || !a.at("fits").is_array() || a.at("fits").empty()) throw std::runtime_error("Invalid Arrhenius result.");
                    for (const auto &fit : a.at("fits")) {
                        if (fit.at("model")!="omega-T2" && fit.at("model")!="ProPho-compat") throw std::runtime_error("Invalid Arrhenius model.");
                        if (!std::isfinite(fit.at("E_a_eV").get<double>()) || fit.at("prefactor").is_null()) throw std::runtime_error("Invalid Arrhenius parameter.");
                        Optional(fit.at("prefactor")); finiteArray(fit.at("residuals"));
                        if (fit.at("dof").get<int>()==0 && (!fit.at("covariance").is_null() || !fit.at("linear_covariance").is_null())) throw std::runtime_error("Unavailable covariance must be null.");
                    }
                }
            }
            if (a.at("type")=="CV" || a.at("type")=="DLCPPolynomial") {
            const auto &xd=a.at("x_d"); Optional(xd.at("value_m"));
            if (xd.at("source")!="manual" && xd.at("source")!="CV" && xd.at("source")!="DLCP") throw std::runtime_error("Invalid x_d source.");
            if (xd.at("policy")!="block" && xd.at("policy")!="linear" && xd.at("policy")!="nearest") throw std::runtime_error("Invalid x_d policy.");
            }
            for (const auto &g : a.at("graphs"))
                if (!std::holds_alternative<pl::Graph>(p.engine->object(g.get<pl::Id>()).data)) throw std::runtime_error("Invalid analysis graph.");
        }
        if (p.graph && !std::holds_alternative<pl::Graph>(p.engine->object(p.graph).data))
            throw std::runtime_error("Invalid graph reference.");
        std::set<pl::Id> comparisons;
        for (const auto &c:p.comparisons) {
            const auto id=c.at("graph_id").get<pl::Id>();
            if (!comparisons.insert(id).second || !std::holds_alternative<pl::Graph>(p.engine->object(id).data) ||
                !c.at("inputs").is_array() || !c.at("analysis_ids").is_array()) throw std::runtime_error("Invalid comparison graph.");
            for (const auto &input:c.at("inputs")) if (!input.at("id").is_number_unsigned()) throw std::runtime_error("Invalid comparison input ID.");
            for (const auto &source:c.at("analysis_ids")) if (!source.is_number_unsigned()) throw std::runtime_error("Invalid comparison analysis ID.");
        }
    }
    inline std::string Serialize(const Project &p)
    {
        Validate(p);
        Json samples = Json::array(), sweeps = Json::array();
        for (const auto &s : p.samples) samples.push_back({{"id",s.id},{"name",s.name},{"area_m2",Nullable(s.area_m2)},{"metadata",s.metadata}});
        for (const auto &s : p.sweeps) sweeps.push_back({{"id",s.id},{"sample_id",s.sample_id},{"variant","Cp_Rp"},
            {"frequency",s.frequency},{"capacitance",s.capacitance},{"resistance",s.resistance},{"temperature_K",Nullable(s.temperature_K)}});
        Json result{{"import_profiles",Json::parse(pl::importProfilesJson(p.importProfiles))},{"circuits",p.circuits},{"schema_version",1},{"id",p.engine->id()},{"name",p.engine->name()},
            {"samples",samples},{"sweeps",sweeps},{"graph",p.graph},{"engine",Json::parse(p.engine->toJson())},{"measurements",p.measurements},{"historical",p.historical},{"imports",p.imports},{"analyses",p.analyses},{"defects",p.defects}};
        if (!p.comparisons.empty()) result["comparisons"]=p.comparisons;
        return result.dump();
    }
    inline Project Parse(const std::string &text)
    {
        const auto j = Json::parse(text);
        if (j.at("schema_version") != 1) throw std::runtime_error("Unsupported CapLab schema; file will not be overwritten.");
        Project p;
        p.importProfiles=pl::importProfilesFromJson(j.value("import_profiles",Json::object()).dump());
        p.engine = CreateRef<pl::Project>(pl::Project::fromJson(j.at("engine").dump()));
        if (j.at("id") != p.engine->id() || j.at("name") != p.engine->name()) throw std::runtime_error("Project identity mismatch.");
        p.graph = j.at("graph").get<pl::Id>();
        for (const auto &s : j.at("samples")) p.samples.push_back({s.at("id").get<pl::Id>(),s.at("name").get<std::string>(),Optional(s.at("area_m2")),s.value("metadata",Json::object())});
        for (const auto &s : j.at("sweeps"))
        {
            if (s.at("variant") != "Cp_Rp") throw std::runtime_error("Unsupported sweep variant.");
            p.sweeps.push_back({s.at("id").get<pl::Id>(),s.at("sample_id").get<pl::Id>(),s.at("frequency").get<pl::Id>(),
                s.at("capacitance").get<pl::Id>(),s.at("resistance").get<pl::Id>(),Optional(s.at("temperature_K"))});
        }
        p.measurements = j.value("measurements",Json::array());
        p.historical = j.value("historical",Json::array());
        p.imports = j.value("imports",Json::array());
        p.analyses = j.value("analyses",Json::array());
        p.defects = j.value("defects",Json::array());
        p.circuits = j.value("circuits",Json::array());
        p.comparisons = j.value("comparisons",Json::array());
        Validate(p);
        return p;
    }
    struct CapLabDocument : DocumentState
    {
        std::uint64_t handle = 0;
        Project project;
        pl::Id selectedSample=0, selectedMeasurement=0, selectedDefect=0, selectedAnalysis=0, selectedCircuit=0;
        std::uint64_t selectionRevision=0;
        std::uint64_t inputRevision=0;
        std::string selectionKind;
        pl::Id identityHighWater=0;
        UndoStack history;
        void Changed() { ++revision; }
        void Saved(const Path &target, std::uint64_t expected) { MarkSaved(target, expected); }
        bool Opened(Project next, const Path &source, std::uint64_t expected)
        {
            if (revision != expected) return false;
            Validate(next);
            project = std::move(next); ++revision; MarkSaved(source, revision);
            return true;
        }
    };
}
