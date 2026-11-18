#pragma once
#include "Storage/CapLabDocument.hpp"

namespace SST::Prototypes::CapLab {
inline std::string SelectionLabel(const Project &p, pl::Id id,
    const char *empty = "none — select") {
    if (!id) return empty;
    for (const auto &s : p.samples) if (s.id == id) return s.name;
    for (const auto &d : p.defects) if (d.at("id") == id) return d.at("name").get<std::string>();
    std::string name;
    Json temperature = nullptr;
    for (const auto &m : p.measurements) if (m.at("id") == id) {
        const auto &meta = m.at("metadata");
        name = Path::FromResolved(std::filesystem::u8path(meta.at("source").get<std::string>()).stem()).Utf8();
        temperature = meta.at("temperature_K");
        break;
    }
    for (const auto &m : p.sweeps) if (m.id == id) {
        name = p.engine->object(m.sample_id).name;
        temperature = Nullable(m.temperature_K);
        break;
    }
    if (name.empty()) return empty;
    if (!temperature.is_null()) name += ", T = " + temperature.dump() + " K";
    return name;
}
}
