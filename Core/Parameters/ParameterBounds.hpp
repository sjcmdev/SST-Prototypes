#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace SST {
enum class ParameterScale { Linear, Log };
struct Bounds {
    double hardMin=0,hardMax=1,softMin=0,softMax=1;
    ParameterScale scale=ParameterScale::Linear;
    double step=.01;
    std::string unit="1",format="%.4g",displayUnit="1";
    double displayScale=1;
    bool integer=false,allowInfinity=false,userOverride=false;
    std::string source,note;
    bool Contains(double value) const {
        return (allowInfinity && value==INFINITY) ||
            (std::isfinite(value) && value>=hardMin && value<=hardMax && (!integer || value==std::floor(value)));
    }
    bool Typical(double value) const {return value>=softMin && value<=softMax;}
    std::pair<double,double> SliderRange(double value) const {
        return {Contains(value) && std::isfinite(value) && (scale!=ParameterScale::Log || value>0)?std::min(softMin,value):softMin,
            Contains(value) && std::isfinite(value)?std::max(softMax,value):softMax};
    }
    std::pair<double,double> ExponentRange(double value) const {
        const auto [lo,hi]=SliderRange(value);return {std::floor(std::log10(lo)),std::floor(std::log10(hi))};
    }
    std::string Error(std::string_view field,double value) const;
    void Require(std::string_view field,double value) const;
    void Validate(std::string_view key) const;
};
using ParameterBounds = std::map<std::string,Bounds,std::less<>>;
// Configuration file adapters only. Runtime users share the resulting immutable typed table.
ParameterBounds ReadParameterBoundsYaml(std::string_view text,ParameterBounds base={},bool userOverride=false);
std::string WriteParameterBoundsYaml(const ParameterBounds&);
ParameterBounds LoadParameterBounds(const ParameterBounds& builtins,const std::string& defaultsFile,
    const std::string& userFile,std::string& diagnostic);
}
