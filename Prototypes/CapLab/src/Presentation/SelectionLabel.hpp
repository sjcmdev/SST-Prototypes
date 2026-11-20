#pragma once
#include "Storage/CapLabDocument.hpp"
#include <cstdio>

namespace SST::Prototypes::CapLab {
inline std::string WithStandardError(const Json& result, const char* value, const char* error) {
    if (!result.contains(value) || !result.at(value).is_number()) return "unavailable";
    char number[64]; std::snprintf(number,sizeof(number),"%.6g",result.at(value).get<double>());
    std::string text=number;
    if (!result.contains(error) || !result.at(error).is_number()) return text+" (SE unavailable)";
    std::snprintf(number,sizeof(number),"%.3g",result.at(error).get<double>());
    return text+" +/- "+number+" (SE)";
}

inline std::string MeasurementSourceName(std::string source) {
    // Conversion annotations may contain slashes; they are not part of the filename.
    const auto annotation=source.find(" (Cp/Rp -> C/G)");
    if (annotation!=std::string::npos) source.resize(annotation);
    return Path::FromResolved(std::filesystem::u8path(source).stem()).Utf8();
}
inline std::string SelectionLabel(const Project &p, pl::Id id,
    const char *empty = "none — select") {
    if (!id) return empty;
    for (const auto &s : p.samples) if (s.id == id) {
        for (const auto& m : p.measurements) if (m.at("sample_id")==id) {
            const auto source=m.at("metadata").at("source").get<std::string>();
            if (source.find(" (Cp/Rp -> C/G)")!=std::string::npos &&
                s.name==Path::FromResolved(std::filesystem::u8path(source).stem()).Utf8())
                return MeasurementSourceName(source);
        }
        return s.name;
    }
    for (const auto &d : p.defects) if (d.at("id") == id) return d.at("name").get<std::string>();
    std::string name;
    Json temperature = nullptr;
    for (const auto &m : p.measurements) if (m.at("id") == id) {
        const auto &meta = m.at("metadata");
        name = MeasurementSourceName(meta.at("source").get<std::string>());
        temperature = meta.at("temperature_K");
        break;
    }
    for (const auto &m : p.sweeps) if (m.id == id) {
        name = p.engine->object(m.sample_id).name;
        const auto& source=std::get<pl::Worksheet>(p.engine->object(m.id).data).source;
        if (const auto path=source.find("path"); path!=source.end() && !path->second.empty())
            name = Path::FromResolved(std::filesystem::u8path(path->second).stem()).Utf8();
        temperature = Nullable(m.temperature_K);
        break;
    }
    if (name.empty()) return empty;
    if (!temperature.is_null()) name += ", T = " + temperature.dump() + " K";
    return name;
}
}
