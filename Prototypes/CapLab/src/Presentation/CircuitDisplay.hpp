#pragma once
#include "CircuitDocument.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace SST::Prototypes::CapLab {
inline std::string Engineering(double value, std::string unit, int precision=12) {
    if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite circuit value");
    const char* prefixes[]{"a","f","p","n","\u00B5","m","","k","M","G","T"};
    const int exponent=value==0 || unit=="1" ? 0 : std::clamp(int(std::floor(std::log10(std::abs(value))/3))*3,-18,12);
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(precision) << value/std::pow(10.,exponent) << ' ' << prefixes[exponent/3+6] << unit;
    return out.str();
}
inline double ParseEngineering(const std::string& text, const std::string& unit) {
    std::istringstream in(text); in.imbue(std::locale::classic());
    double value; std::string suffix, extra;
    if (!(in >> value >> suffix) || (in >> extra) || !std::isfinite(value)) throw std::invalid_argument("Invalid engineering value");
    const std::pair<const char*,double> prefixes[]={{"",1},{"a",1e-18},{"f",1e-15},{"p",1e-12},{"n",1e-9},{"µ",1e-6},{"u",1e-6},{"m",1e-3},{"k",1e3},{"M",1e6},{"G",1e9},{"T",1e12}};
    for (const auto& [prefix,scale] : prefixes) if (suffix==std::string(prefix)+unit) {
        value*=scale;
        if (std::isfinite(value)) return value;
    }
    throw std::invalid_argument("Invalid engineering unit");
}
inline std::string ElementUnit(const ccui::Element& e) {
    if (e.kind==ccui::Kind::R) return "Ω";
    if (e.kind==ccui::Kind::W) return "Ω·s^-½";
    if (e.kind==ccui::Kind::CPE) {
        std::ostringstream out; out.imbue(std::locale::classic());
        out << "S·s^" << std::setprecision(6) << e.secondary;
        return out.str();
    }
    if (e.kind==ccui::Kind::PN || e.kind==ccui::Kind::Schottky) return "A";
    return ccui::unit(e.kind);
}
}
