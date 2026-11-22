#pragma once
#include "IO/PrototypePythonBindings.hpp"
#include "Domain/CapLabImport.hpp"
#include <algorithm>
#include <cctype>
namespace SST::Prototypes::CapLab {
class LabASImporter {
public:
    virtual ~LabASImporter() = default;
    virtual bool recognizes(std::string_view filename, std::string_view) const {
        auto extension=Path::FromResolved(std::filesystem::u8path(filename)).extension().Utf8();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension==".dat" || extension==".txt" || extension==".csv" || extension==".cf" || extension==".cv" || extension==".json";
    }
    virtual Json preview(std::string_view text, const Path &source, char decimal, const Json &options, JobContext &context) const {
        return ReadLabAS(std::string(text),source,decimal,context,options);
    }
};
}
