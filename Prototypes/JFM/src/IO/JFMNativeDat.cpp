#include "Core/dspch.hpp"
#include "IO/JFMNativeDat.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <sst/plotlab/Extensions.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <regex>

namespace SST::Prototypes::JFM {
bool CanImportNativeDat(const std::vector<Path>& paths, const JFMImportOptions& options) {
    if (options.parser != "auto" && options.parser != "dat") return false;
    // Custom naming/variable rules remain with the scripted parser until a shared typed metadata evaluator exists.
    if ((!options.namePattern.empty() && options.namePattern != "{stem}") || !options.shortNamePattern.empty()
        || !options.customVariables.empty() || !options.customScriptPath.empty()) return false;
    return std::all_of(paths.begin(), paths.end(), [](const auto& path) {
        auto extension = path.extension().Utf8();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        return extension == ".dat";
    });
}
Result<JFMImportBatchResult> ImportNativeDat(const std::vector<Path>& paths, const JFMImportOptions& options,
                                           const JFMPythonImportBridge::ProgressCallback& progress) {
    JFMImportBatchResult batch;
    for (const auto& path : paths) try {
        std::string text, error;
        if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
        std::string numeric, header, columnHeader;
        for (std::size_t begin = 0; begin < text.size();) {
            const auto end = text.find('\n', begin);
            auto line = text.substr(begin, end == std::string::npos ? end : end - begin);
            begin = end == std::string::npos ? text.size() : end + 1;
            const auto first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos) continue;
            const char c = line[first];
            if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') numeric += line + '\n';
            else {
                header += line + '\n';
                if (numeric.empty() && c != '#' && c != '%' && line.compare(first,2,"//") != 0) columnHeader = line;
                else if (!numeric.empty() && c != '#' && c != '%' && line.compare(first,2,"//") != 0)
                    throw std::runtime_error("Non-numeric line inside data. Choose dat_python for permissive scripted parsing.");
            }
        }
        sst::plotlab::ImportOptions dialect;
        if (!options.delimiter.empty()) {
            if (options.delimiter.size() != 1) throw std::invalid_argument("Delimiter must be one character.");
            dialect.delimiter = options.delimiter.front();
        } else dialect.delimiter = numeric.find(';') != std::string::npos ? ';' : ' ';
        if (options.decimalSeparator != "auto") {
            if (options.decimalSeparator != "." && options.decimalSeparator != ",") throw std::invalid_argument("Invalid decimal separator.");
            dialect.decimal = options.decimalSeparator.front();
        }
        dialect.header = columnHeader.empty() ? sst::plotlab::RowMode::Absent : sst::plotlab::RowMode::Present;
        dialect.units = sst::plotlab::RowMode::Absent;
        auto table = sst::plotlab::CsvImporter{}.read(columnHeader.empty() ? numeric : columnHeader + '\n' + numeric, dialect, {});
        if (table.columns.size() < 2) throw std::runtime_error("Expected voltage and current columns.");
        for (const auto& column : table.columns)
            if (!std::holds_alternative<std::vector<double>>(column.values)) throw std::runtime_error("Non-numeric values in a data column.");
        JFMCharacteristic curve;
        auto& info = curve.info;
        info.name = Path::FromResolved(path.Native().stem()).Utf8(); info.sourcePath = path; info.sourceRecordKey = "0"; info.parser = "dat";
        info.importOptions = options; info.importOptions.parser = "dat";
        info.metadata = {{"stem", info.name}, {"file_name", path.filename().Utf8()}, {"source_path", path.Utf8()},
                         {"folder", path.parent_path().filename().Utf8()}, {"folder_path", path.parent_path().Utf8()}, {"parser", "dat"}};
        static const std::regex temperature(R"((\d+(?:[.,]\d+)?)\s*K\b|T(?:emp(?:erature)?)?\s*[_:= -]?(\d+(?:[.,]\d+)?))", std::regex::icase);
        const auto detect = [&](std::string value, const char* source) {
            std::smatch match;
            if (!std::regex_search(value, match, temperature)) return false;
            auto number = match[1].matched ? match[1].str() : match[2].str();
            std::replace(number.begin(), number.end(), ',', '.');
            double kelvin = 0;
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), kelvin);
            if (parsed.ec != std::errc{} || !std::isfinite(kelvin)) return false;
            info.temperatureK = kelvin; info.temperatureSource = source;
            info.metadata["T"] = info.metadata["temperature_K"] = number;
            return true;
        };
        if (options.temperatureExtraction != "off") {
            if (options.temperatureExtraction != "header") {
                auto part = path;
                for (int depth = 0; depth < 9 && !part.Empty(); ++depth) {
                    if (detect(part.filename().Utf8(), depth == 0 ? "filename" : "path")) break;
                    const auto parent = part.parent_path(); if (part == parent) break; part = parent;
                }
            }
            if (!info.temperatureK && options.temperatureExtraction != "filename") detect(header, "header");
        }
        info.qc.hasMissingTemperature = !info.temperatureK;
        if (!info.temperatureK) info.qc.warnings.push_back("temperature_missing");
        std::optional<std::size_t> currentRole, densityRole;
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            auto name = columnHeader.empty() ? "col_" + std::to_string(i) : table.names[i];
            auto lower = name; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            std::string role = "value";
            if (i == 0 || lower.find("volt") != std::string::npos || lower == "v" || lower == "u") { role = "voltage"; }
            else if (lower.find("density") != std::string::npos || lower == "j") { role = "current_density"; if (!densityRole) densityRole = i; }
            else if (lower.find("current") != std::string::npos || lower == "i" || lower == "a") { role = "current"; if (!currentRole) currentRole = i; }
            info.columns.push_back({std::move(name), table.columns[i].units, std::move(role)});
        }
        const auto currentColumn = currentRole.value_or(1), densityColumn = densityRole.value_or(currentColumn);
        curve.data.voltage = std::move(std::get<std::vector<double>>(table.columns[0].values));
        curve.data.current = std::move(std::get<std::vector<double>>(table.columns[currentColumn].values));
        if (densityColumn == currentColumn) curve.data.currentDensity = curve.data.current;
        else curve.data.currentDensity = std::move(std::get<std::vector<double>>(table.columns[densityColumn].values));
        info.sweep.pointCount = curve.data.voltage.size();
        if (curve.data.voltage.empty()) throw std::runtime_error("No numeric rows found.");
        info.sweep.vStart = curve.data.voltage.front(); info.sweep.vStop = curve.data.voltage.back();
        const bool ascending = std::is_sorted(curve.data.voltage.begin(), curve.data.voltage.end());
        const bool descending = std::is_sorted(curve.data.voltage.rbegin(), curve.data.voltage.rend());
        info.sweep.direction = ascending ? "increasing" : descending ? "decreasing" : "non_monotonic";
        info.qc.nonMonotonicSweep = !ascending && !descending;
        if (info.qc.nonMonotonicSweep) info.qc.warnings.push_back("non_monotonic_sweep");
        batch.warnings.insert(batch.warnings.end(), info.qc.warnings.begin(), info.qc.warnings.end());
        batch.characteristics.push_back(std::move(curve));
        if (progress) progress(batch.characteristics.size(), paths.size(), path.filename().Utf8());
    } catch (const std::exception& error) {
        return StructuredError{ErrorCategory::IO, Severity::Error, "Could not import " + path.filename().Utf8(),
            path.Utf8() + ": " + error.what(), "Check the source path, numeric columns and delimiter; choose dat_python for scripted parsing.",
            "JFMNativeDat", "jfm.import.dat"};
    }
    return batch;
}
}
