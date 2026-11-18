#pragma once
#include "Domain/CapLabImport.hpp"
namespace SST::Prototypes::CapLab
{
    inline pl::Table MeasurementTable(const Json &m)
    {
        const auto names = m.at("names").get<std::vector<std::string>>();
        const auto units = m.at("units").get<std::vector<std::string>>();
        std::vector<std::vector<double>> values;
        for (const auto &column : m.at("columns"))
        {
            std::vector<double> v;
            for (const auto &cell : column) v.push_back(cell.is_null() ? std::numeric_limits<double>::quiet_NaN() : cell.get<double>());
            values.push_back(std::move(v));
        }
        const auto rows = m.at("source_rows").get<std::vector<std::size_t>>();
        if (names.empty() || names.size() != units.size() || names.size() != values.size() || rows.empty())
            throw std::runtime_error("Invalid import columns.");
        pl::Table table;
        table.names = names;
        table.source = {{"profile",m.at("profile").get<std::string>()},{"provenance",m.dump()}};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].size() != rows.size()) throw std::runtime_error("Unequal import columns.");
            pl::Column c;
            c.units = units[i]; c.longName = names[i]; c.values = values[i];
            c.role = i == 0 ? pl::Role::X : i == 1 ? pl::Role::Y : pl::Role::None;
            table.columns.push_back(std::move(c));
        }
        return table;
    }

    // A bundle is prepared on a detached project; one Core Undo entry publishes all of it.
    class BundleChange
    {
    public:
        BundleChange(Project &p, const Json &preview) : target(p), before(p)
        {
            auto draft = Parse(Serialize(p));
            std::vector<pl::Id> sampleIds;
            std::vector<std::map<std::string,std::vector<pl::Id>>> legacy;
            for (const auto &s : preview.at("samples"))
            {
                const auto book = draft.engine->reserveId();
                const auto name = s.at("name").get<std::string>();
                auto bookChange = draft.engine->change("Import sample",{{book,draft.engine->root(),name,0,pl::Workbook{}}});
                bookChange.apply(*draft.engine);
                draft.samples.push_back({book,name,Optional(s.at("area_m2")),s.at("metadata")});
                sampleIds.push_back(book); legacy.emplace_back();
                for (const auto &m : s.at("measurements"))
                {
                    const auto table = MeasurementTable(m);
                    const auto sheet = draft.engine->reserveId();
                    pl::Worksheet worksheet; worksheet.source = table.source;
                    for (std::size_t r = 0; r < m.at("source_rows").size(); ++r) worksheet.rows.push_back(draft.engine->reserveId());
                    const auto rowIds = worksheet.rows;
                    std::vector<pl::Object> objects{{sheet,book,m.at("kind").get<std::string>(),0,std::move(worksheet)}};
                    Json cols = Json::array();
                    for (std::size_t c = 0; c < table.columns.size(); ++c)
                    {
                        const auto id = draft.engine->reserveId(); cols.push_back(id);
                        objects.push_back({id,sheet,table.names[c],0,table.columns[c],c});
                    }
                    auto op = draft.engine->change("Import measurement",std::move(objects)); op.apply(*draft.engine);
                    auto meta = m; meta.erase("columns");
                    for (auto key : {"groups","sweep_segments"}) if (meta.contains(key)) for (auto &g : meta[key])
                    {
                        Json ids = Json::array();
                        for (const auto &r : g.at("rows")) ids.push_back(rowIds.at(r.get<std::size_t>()));
                        g.erase("rows"); g["row_ids"] = std::move(ids);
                    }
                    draft.measurements.push_back({{"id",sheet},{"sample_id",book},{"columns",cols},{"metadata",meta}});
                    if (m.contains("legacy_collection")) legacy.back()[m.at("legacy_collection").get<std::string>()].push_back(sheet);
                    if (m.at("kind") == "FrequencySweep" && m.at("variant") != "ReZ_ImZ")
                    {
                        auto opGraph = CFTemplate{}.instantiate(*draft.engine,draft.engine->root(),sheet,
                            {{"x",cols.at(0).get<pl::Id>()},{"y",cols.at(1).get<pl::Id>()}},{{"graph",std::to_string(draft.graph)}});
                        opGraph.apply(*draft.engine);
                        if (!draft.graph) for (const auto &[id,o] : draft.engine->objects())
                            if (std::holds_alternative<pl::Graph>(o->data)) draft.graph = id;
                    }
                }
            }
            for (auto h : preview.at("historical"))
            {
                const auto si = h.at("sample_index").get<std::size_t>();
                h["sample_id"] = sampleIds.at(si); h.erase("sample_index");
                auto &refs = legacy.at(si);
                auto &data = h.at("data");
                h["source_data"] = data;
                for (const auto &key : {"defects","fits"})
                {
                    auto &items = std::string(key) == "fits" ? data["walter"]["fits"] : data["defects"];
                    if (items.is_array()) for (auto &item : items)
                    {
                        auto id = draft.engine->reserveId(); item["id"] = id; refs[key].push_back(id);
                        item["status"] = "historical"; item["model"] = "ProPho-compat";
                    }
                }
                Resolve(data,refs);
                if (data["defects"].is_array()) for (auto &defect : data["defects"])
                {
                    Json points = Json::array();
                    for (const auto &pair : defect.value("ft_points",Json::array()))
                    {
                        Json point{{"temperature_K",pair.at(0)},{"frequency_Hz",pair.at(1)},
                            {"origin","historical"},{"measurement_id",nullptr},{"row_id",nullptr}};
                        std::vector<std::pair<pl::Id,pl::Id>> matches;
                        for (const auto &measurement : draft.measurements)
                        {
                            const auto &meta = measurement.at("metadata");
                            if (measurement.at("sample_id") != sampleIds[si] || meta.at("kind") != "FrequencySweep" ||
                                meta.at("temperature_K") != pair.at(0)) continue;
                            auto mid = measurement.at("id").get<pl::Id>();
                            const auto &freq = std::get<std::vector<double>>(std::get<pl::Column>(draft.engine->object(measurement.at("columns").at(0).get<pl::Id>()).data).values);
                            const auto &rowIds = std::get<pl::Worksheet>(draft.engine->object(mid).data).rows;
                            for (std::size_t i = 0; i < freq.size(); ++i)
                                if (freq[i] == pair.at(1).get<double>()) matches.emplace_back(mid,rowIds[i]);
                        }
                        if (matches.size() == 1) { point["measurement_id"] = matches[0].first; point["row_id"] = matches[0].second; }
                        point["reference_status"] = matches.size() == 1 ? "resolved" : "unresolved";
                        points.push_back(std::move(point));
                    }
                    defect.erase("ft_points"); defect["points"] = points;
                }
                if (data["walter"].is_object()) data["walter"]["status"] = "historical";
                if (data["walter"]["dist"].is_array()) for (auto &result : data["walter"]["dist"]) result["status"] = "historical";
                draft.historical.push_back(std::move(h));
            }
            auto origin = preview; origin.erase("samples"); origin.erase("historical");
            draft.imports.push_back(std::move(origin));
            Validate(draft);
            after = draft;
            target = std::move(draft);
        }
        void Undo() { target = before; }
        void Redo() { target = after; }
    private:
        Project &target;
        // Existing engine operations belong to an instance, so retain it across Undo/Redo.
        Project before, after;
        static Json Reference(const Json &index, const std::vector<pl::Id> &ids)
        {
            if (!index.is_number_integer()) return nullptr;
            const auto i = index.get<long long>();
            return i >= 0 && static_cast<std::size_t>(i) < ids.size() ? Json(ids[i]) : Json(nullptr);
        }
        static void Resolve(Json &value, std::map<std::string,std::vector<pl::Id>> &refs)
        {
            if (value.is_array()) { for (auto &v : value) Resolve(v,refs); return; }
            if (!value.is_object()) return;
            if ((value.contains("E_w") && value.contains("N_T")) || value.contains("saved_coeff"))
            { value["status"] = "historical"; value["model"] = "ProPho-compat"; }
            for (const auto &[key,collection] : {std::pair{"dl_idx","dl_data"},std::pair{"as_idx","as_data"}})
                if (value.contains(key))
                {
                    value["measurement_id"] = Reference(value.at(key),refs[collection]);
                    if (value["measurement_id"].is_null()) value["input_status"] = "missing_input";
                    value.erase(key);
                }
            if (value.contains("saved_idx"))
            {
                Json selections = Json::object();
                for (const auto &[key,v] : value.at("saved_idx").items())
                {
                    const std::string collection = key == "as_for_int" ? "as_data" : key;
                    selections[key] = v.is_array() && !v.empty() ? Reference(v.at(0),refs[collection]) : Json(nullptr);
                }
                value["selections"] = selections; value.erase("saved_idx");
            }
            if (value.contains("plot_browser_settings") && value["plot_browser_settings"].is_object())
                for (auto &[key,v] : value["plot_browser_settings"].items())
                {
                    Json ids = Json::array();
                    if (v.is_array() && !v.empty() && v.at(0).is_array())
                        for (const auto &i : v.at(0)) ids.push_back(Reference(i,refs[key == "dlcp_cv" ? "dl_data" : key]));
                    v = ids;
                }
            for (auto &[key,v] : value.items()) Resolve(v,refs);
        }
    };
}
