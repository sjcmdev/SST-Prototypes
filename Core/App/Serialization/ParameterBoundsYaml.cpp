#include "Core/dspch.hpp"
#include "Core/Parameters/ParameterBounds.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <charconv>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>

namespace SST {
std::string Bounds::Error(std::string_view field,double value) const {
    char numbers[180];std::snprintf(numbers,sizeof(numbers),": %.9g; allowed [%.9g, %.9g] %s%s",value,hardMin,hardMax,unit.c_str(),integer?" (integer)":"");
    return std::string(field)+numbers;
}
void Bounds::Require(std::string_view field,double value) const {if(!Contains(value))throw std::invalid_argument(Error(field,value));}
void Bounds::Validate(std::string_view key) const {
    if(!std::isfinite(hardMin) || !std::isfinite(hardMax) || !std::isfinite(softMin) || !std::isfinite(softMax) ||
        !(hardMin< hardMax && softMin<softMax && hardMin<=softMin && softMax<=hardMax) ||
        !std::isfinite(step) || step<=0 || !std::isfinite(displayScale) || displayScale<=0 ||
        !std::isfinite(hardMin*displayScale) || !std::isfinite(hardMax*displayScale) ||
        (scale==ParameterScale::Log && (softMin<=0 || hardMin<0)) || unit.empty() || displayUnit.empty() || source.empty())
        throw std::invalid_argument("Invalid parameter bounds for "+std::string(key)+": require hard >= soft, min < max, positive step/scale and log soft min > 0");
    // A config format is passed to printf/ImGui: accept one floating-point conversion only.
    unsigned precision=0;
    const auto parsed=format.size()>=4?std::from_chars(format.data()+2,format.data()+format.size()-1,precision):std::from_chars_result{};
    if(format.size()<4 || format.size()>5 || !format.starts_with("%.") || std::string("eEfFgG").find(format.back())==std::string::npos ||
        parsed.ec!=std::errc{} || parsed.ptr!=format.data()+format.size()-1 || precision>17)
        throw std::invalid_argument("Unsafe number format for "+std::string(key));
    if(integer && (hardMin!=std::floor(hardMin) || hardMax!=std::floor(hardMax) || step!=std::floor(step) ||
        softMin!=std::floor(softMin) || softMax!=std::floor(softMax) || hardMin<std::numeric_limits<int>::min() || hardMax>std::numeric_limits<int>::max()))throw std::invalid_argument("Integer bounds require representable integral endpoints and step: "+std::string(key));
}
ParameterBounds ReadParameterBoundsYaml(std::string_view text,ParameterBounds base,bool userOverride) {
    const auto root=YAML::Load(std::string(text));
    if(!root.IsMap() || !root["schema_version"] || root["schema_version"].as<int>()!=1 || !root["parameters"].IsMap())
        throw std::invalid_argument("Parameter bounds require schema_version: 1 and parameters: mapping");
    std::set<std::string> rootKeys;
    for(const auto& field:root){const auto key=field.first.as<std::string>();if(!rootKeys.insert(key).second || (key!="schema_version" && key!="parameters"))throw std::invalid_argument("Unknown/duplicate bounds root key: "+key);}
    std::set<std::string> keys;
    for(const auto& entry:root["parameters"]) {
        const auto key=entry.first.as<std::string>();const auto node=entry.second;
        if(!keys.insert(key).second || !node.IsMap())throw std::invalid_argument("Duplicate key or non-mapping bounds: "+key);
        if(userOverride && !base.contains(key))throw std::invalid_argument("Unknown parameter bounds key: "+key);
        const bool existing=base.contains(key);const auto previous=existing?base.at(key):Bounds{};auto& b=base[key];std::set<std::string> fields;
        for(const auto& field:node) {
            const auto name=field.first.as<std::string>();if(!fields.insert(name).second)throw std::invalid_argument("Duplicate field: "+key+"."+name);
            const auto value=field.second;
            if(name=="hard_min")b.hardMin=value.as<double>();else if(name=="hard_max")b.hardMax=value.as<double>();
            else if(name=="soft_min")b.softMin=value.as<double>();else if(name=="soft_max")b.softMax=value.as<double>();
            else if(name=="step")b.step=value.as<double>();else if(name=="unit")b.unit=value.as<std::string>();
            else if(name=="format")b.format=value.as<std::string>();else if(name=="display_unit")b.displayUnit=value.as<std::string>();
            else if(name=="display_scale")b.displayScale=value.as<double>();else if(name=="integer")b.integer=value.as<bool>();
            else if(name=="allow_infinity")b.allowInfinity=value.as<bool>();else if(name=="source")b.source=value.as<std::string>();
            else if(name=="note")b.note=value.as<std::string>();
            else if(name=="scale"){const auto scale=value.as<std::string>();if(scale!="linear" && scale!="log")throw std::invalid_argument("Unknown scale: "+key);b.scale=scale=="log"?ParameterScale::Log:ParameterScale::Linear;}
            else throw std::invalid_argument("Unknown bounds field: "+key+"."+name);
        }
        if(!existing)for(const auto* required:{"hard_min","hard_max","soft_min","soft_max","scale","step","unit","format","source"})
            if(!fields.contains(required))throw std::invalid_argument("Missing bounds field: "+key+"."+required);
        if(existing && (b.unit!=previous.unit || b.displayUnit!=previous.displayUnit || b.displayScale!=previous.displayScale || b.integer!=previous.integer || b.allowInfinity!=previous.allowInfinity))
            throw std::invalid_argument("Units, integer domain and sentinel semantics are fixed by the typed model: "+key);
        b.userOverride=userOverride;b.Validate(key);
    }
    return base;
}
std::string WriteParameterBoundsYaml(const ParameterBounds& table) {
    YAML::Emitter out;out<<YAML::BeginMap<<YAML::Key<<"schema_version"<<YAML::Value<<1<<YAML::Key<<"parameters"<<YAML::Value<<YAML::BeginMap;
    out.SetDoublePrecision(17);
    for(const auto& [key,b]:table) {
        out<<YAML::Key<<key<<YAML::Value<<YAML::BeginMap;
        out<<YAML::Key<<"hard_min"<<YAML::Value<<b.hardMin<<YAML::Key<<"hard_max"<<YAML::Value<<b.hardMax;
        out<<YAML::Key<<"soft_min"<<YAML::Value<<b.softMin<<YAML::Key<<"soft_max"<<YAML::Value<<b.softMax;
        out<<YAML::Key<<"scale"<<YAML::Value<<(b.scale==ParameterScale::Log?"log":"linear")<<YAML::Key<<"step"<<YAML::Value<<b.step;
        out<<YAML::Key<<"unit"<<YAML::Value<<b.unit<<YAML::Key<<"format"<<YAML::Value<<b.format;
        out<<YAML::Key<<"display_unit"<<YAML::Value<<b.displayUnit<<YAML::Key<<"display_scale"<<YAML::Value<<b.displayScale;
        out<<YAML::Key<<"integer"<<YAML::Value<<b.integer<<YAML::Key<<"allow_infinity"<<YAML::Value<<b.allowInfinity;
        out<<YAML::Key<<"source"<<YAML::Value<<b.source<<YAML::Key<<"note"<<YAML::Value<<b.note<<YAML::EndMap;
    }
    out<<YAML::EndMap<<YAML::EndMap;if(!out.good())throw std::runtime_error(out.GetLastError());return out.c_str();
}
ParameterBounds LoadParameterBounds(const ParameterBounds& builtins,const std::string& defaultsFile,const std::string& userFile,std::string& diagnostic) {
    diagnostic.clear();auto table=builtins;
    for(const auto& [file,user]:{std::pair{defaultsFile,false},std::pair{userFile,true}}) {
        if(file.empty())continue;
        try {
            if(!std::filesystem::exists(std::filesystem::u8path(file))) {if(user)continue;throw std::runtime_error("File not found");}
            std::string text,error;if(!TextFileIO::Load(Path{file},text,error))throw std::runtime_error(error);
            table=ReadParameterBoundsYaml(text,std::move(table),user);
        }catch(const std::exception& e){diagnostic="Parameter bounds: "+file+": "+e.what()+". Using built-in defaults.";return builtins;}
    }
    return table;
}
}
