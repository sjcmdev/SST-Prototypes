#pragma once
#include <nlohmann/json.hpp>
#include "CircuitDocument.h"

namespace SST::Prototypes::CapLab {
// In-memory adapter for CircuitCore-UI's version-1 JSON; Core owns project IO.
inline nlohmann::json CircuitJson(const ccui::CircuitDocument &d) {
    d.validate();
    nlohmann::json j{{"version",1},{"nodes",d.nodes},{"ground",d.ground},{"port",d.port},
        {"parameter_units",{{"Cj0","F"},{"Vbi","V"},{"m","1"},{"FC","1"},{"tau","s"}}},{"elements",nlohmann::json::array()},{"wires",nlohmann::json::array()}};
    for (const auto &e : d.elements) j["elements"].push_back({{"kind",static_cast<int>(e.kind)},
        {"name",e.name},{"nodes",e.nodes},{"x",e.position.x},{"y",e.position.y},{"rotation",e.rotation},
        {"value",e.value},{"value_unit",ccui::unit(e.kind)},{"secondary",e.secondary},{"secondary_unit","1"},{"Cj0",e.cj0},{"Vbi",e.vbi},{"m",e.m},{"FC",e.fc},{"tau",e.tau}});
    for (const auto &w : d.wires) j["wires"].push_back({w.a,w.b});
    return j;
}
inline ccui::CircuitDocument ReadCircuit(const nlohmann::json &j) {
    if (j.dump().size()>1024*1024 || j.at("version")!=1) throw std::runtime_error("Unsupported/oversized circuit JSON");
    ccui::CircuitDocument d;
    d.nodes=j.at("nodes").get<std::vector<int>>(); d.ground=j.at("ground"); d.port=j.at("port").get<std::array<int,2>>();
    for (const auto &e : j.at("elements")) d.elements.push_back({static_cast<ccui::Kind>(e.at("kind").get<int>()),
        e.at("name"),e.at("nodes").get<std::array<int,2>>(),{e.at("x"),e.at("y")},e.at("rotation"),e.at("value"),e.at("secondary"),
        e.value("Cj0",0.),e.value("Vbi",.7),e.value("m",.5),e.value("FC",.5),e.value("tau",0.)});
    for (const auto &w : j.at("wires")) {
        if (!w.is_array() || w.size()!=2) throw std::runtime_error("Invalid circuit wire");
        d.wires.push_back({w.at(0),w.at(1)});
    }
    d.validate();
    for (std::size_t i=0;i<d.elements.size();++i) {
        const auto &e=j.at("elements").at(i);
        if (e.value("value_unit",std::string(ccui::unit(d.elements[i].kind)))!=ccui::unit(d.elements[i].kind) ||
            e.value("secondary_unit",std::string("1"))!="1") throw std::runtime_error("Invalid circuit parameter units");
    }
    if (j.contains("parameter_units") && j.at("parameter_units")!=CircuitJson(d).at("parameter_units"))
        throw std::runtime_error("Invalid diode parameter units");
    return d;
}
}
