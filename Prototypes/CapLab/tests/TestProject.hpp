#pragma once
#include "Domain/CapLabImport.hpp"
#include <fstream>
#include <sstream>
namespace SST::Prototypes::CapLab {
inline Json FixturePreview() {
    std::ifstream file(std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path() / "tests/fixtures/cf_100K_7.dat");
    if (!file) throw std::runtime_error("Missing PFS I2 fixture");
    std::vector<std::vector<double>> columns(3);
    std::vector<std::size_t> rows;
    std::string line;
    while (std::getline(file,line)) {
        std::istringstream stream(line); double f,c,r;
        if (!(stream >> f >> c >> r)) throw std::runtime_error("Invalid frozen fixture");
        columns[0].push_back(f); columns[1].push_back(c); columns[2].push_back(r); rows.push_back(rows.size()+1);
    }
    return Json{{"columns",columns},{"source_rows",rows},{"profile","lab_as_cp_rp"},
        {"source","cf_100K_7.dat"},{"sha256","b46ce13e84432645850ccf0d3bd5445d3bd5a978da40942bb44e78adb9a479c6"},
        {"header",""},{"decimal","."},{"temperature_K",nullptr},{"suggested_temperature_K",100}};
}
inline Project FixtureProject() {
    Project p; ImportChange change(p,FixturePreview(),100,CFTemplate{}); return p;
}
}
