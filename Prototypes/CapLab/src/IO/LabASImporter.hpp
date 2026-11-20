#pragma once
#include "IO/PrototypePythonBindings.hpp"
#include "Domain/CapLabImport.hpp"
#include <algorithm>
#include <cctype>
namespace SST::Prototypes::CapLab {
class LabASImporter final : public pl::Importer {
public:
    std::string typeId() const override { return "caplab.lab_as_cp_rp"; }
    unsigned version() const override { return 1; }
    bool recognizes(std::string_view filename, std::string_view) const override {
        auto extension=Path::FromResolved(std::filesystem::u8path(filename)).extension().Utf8();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension==".dat" || extension==".txt" || extension==".csv" || extension==".cf" || extension==".cv" || extension==".json";
    }
    pl::Table preview(std::string_view text, const pl::ImportOptions &options, const pl::WorkContext &work) const override {
        JobContext context([&](float done,float total) {if (work.progress) work.progress(total>0 ? done/total : 0);},
            {}, {}, {}, work.cancelled);
        const auto source=options.extra.find("path");
        const auto result=ReadLabAS(std::string(text),Path::FromResolved(std::filesystem::u8path(source==options.extra.end() ? "" : source->second)),
            options.decimal ? options.decimal : '.',context,options.extra.contains("options") ? options.extra.at("options") : "");
        if (result.contains("samples")) { pl::Table bundle; bundle.source["preview_json"] = result.dump(); return bundle; }
        auto table=PreviewTable(result,Optional(result.at("temperature_K")));
        table.source["preview_json"]=result.dump();
        return table;
    }
    pl::Table read(std::string_view text,const pl::ImportOptions &options,const pl::WorkContext &work) const override {
        return preview(text,options,work);
    }
};
}
