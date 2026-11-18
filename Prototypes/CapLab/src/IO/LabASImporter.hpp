#pragma once
#include "IO/PrototypePythonBindings.hpp"
#include "Domain/CapLabImport.hpp"
namespace SST::Prototypes::CapLab {
class LabASImporter final : public pl::Importer {
public:
    std::string typeId() const override { return "caplab.lab_as_cp_rp"; }
    unsigned version() const override { return 1; }
    bool recognizes(std::string_view filename, std::string_view) const override {
        const auto extension=std::filesystem::path(filename).extension();
        return extension==".dat" || extension==".txt" || extension==".csv";
    }
    pl::Table preview(std::string_view text, const pl::ImportOptions &options, const pl::WorkContext &work) const override {
        JobContext context([&](float done,float total) {if (work.progress) work.progress(total>0 ? done/total : 0);},
            {}, {}, {}, work.cancelled);
        const auto source=options.extra.find("path");
        const auto result=ReadLabAS(std::string(text),Path{source==options.extra.end() ? "" : source->second},
            options.decimal ? options.decimal : '.',context);
        auto table=PreviewTable(result,Optional(result.at("temperature_K")));
        table.source["preview_json"]=result.dump();
        return table;
    }
    pl::Table read(std::string_view text,const pl::ImportOptions &options,const pl::WorkContext &work) const override {
        return preview(text,options,work);
    }
};
}
