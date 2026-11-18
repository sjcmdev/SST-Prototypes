#pragma once
#include <cmath>
#include <optional>
#include <set>
#include <nlohmann/json.hpp>
#include <sst/plotlab/Extensions.hpp>
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::CapLab
{
    namespace pl = sst::plotlab;
    using Json = nlohmann::json;
    struct Sample { pl::Id id; std::string name; std::optional<double> area_m2; };
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
        pl::Id graph = 0;
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
        std::set<pl::Id> samples, sweeps;
        for (const auto &s : p.samples)
        {
            if (!samples.insert(s.id).second || !std::holds_alternative<pl::Workbook>(p.engine->object(s.id).data))
                throw std::runtime_error("Invalid sample ID.");
            if (s.name != p.engine->object(s.id).name) throw std::runtime_error("Sample name mismatch.");
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
        if (p.graph && !std::holds_alternative<pl::Graph>(p.engine->object(p.graph).data))
            throw std::runtime_error("Invalid graph reference.");
    }
    inline std::string Serialize(const Project &p)
    {
        Validate(p);
        Json samples = Json::array(), sweeps = Json::array();
        for (const auto &s : p.samples) samples.push_back({{"id",s.id},{"name",s.name},{"area_m2",Nullable(s.area_m2)}});
        for (const auto &s : p.sweeps) sweeps.push_back({{"id",s.id},{"sample_id",s.sample_id},{"variant","Cp_Rp"},
            {"frequency",s.frequency},{"capacitance",s.capacitance},{"resistance",s.resistance},{"temperature_K",Nullable(s.temperature_K)}});
        return Json{{"schema_version",1},{"id",p.engine->id()},{"name",p.engine->name()},
            {"samples",samples},{"sweeps",sweeps},{"graph",p.graph},{"engine",Json::parse(p.engine->toJson())}}.dump();
    }
    inline Project Parse(const std::string &text)
    {
        const auto j = Json::parse(text);
        if (j.at("schema_version") != 1) throw std::runtime_error("Unsupported CapLab schema; file will not be overwritten.");
        Project p;
        p.engine = CreateRef<pl::Project>(pl::Project::fromJson(j.at("engine").dump()));
        if (j.at("id") != p.engine->id() || j.at("name") != p.engine->name()) throw std::runtime_error("Project identity mismatch.");
        p.graph = j.at("graph").get<pl::Id>();
        for (const auto &s : j.at("samples")) p.samples.push_back({s.at("id").get<pl::Id>(),s.at("name").get<std::string>(),Optional(s.at("area_m2"))});
        for (const auto &s : j.at("sweeps"))
        {
            if (s.at("variant") != "Cp_Rp") throw std::runtime_error("Unsupported sweep variant.");
            p.sweeps.push_back({s.at("id").get<pl::Id>(),s.at("sample_id").get<pl::Id>(),s.at("frequency").get<pl::Id>(),
                s.at("capacitance").get<pl::Id>(),s.at("resistance").get<pl::Id>(),Optional(s.at("temperature_K"))});
        }
        Validate(p);
        return p;
    }
    struct CapLabDocument
    {
        Project project;
        Path path;
        bool dirty = false;
        std::uint64_t revision = 0;
        void Changed() { ++revision; dirty = true; }
        void Saved(const Path &target, std::uint64_t expected)
        {
            path = target;
            if (revision == expected) dirty = false;
        }
        bool Opened(Project next, const Path &source, std::uint64_t expected)
        {
            if (revision != expected) return false;
            Validate(next);
            project = std::move(next); path = source; ++revision; dirty = false;
            return true;
        }
    };
}
