#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Domain/CapLabPlotStyle.hpp"
namespace SST::Prototypes::CapLab
{
    inline bool IsCFGraph(const pl::Project &p, pl::Id graph)
    {
        if (!graph || !std::holds_alternative<pl::Graph>(p.object(graph).data)) return false;
        const auto layers=p.children(graph);
        if (layers.size()!=1) return false;
        const auto plots=p.children(layers.front());
        if (plots.empty()) return false;
        for (auto id:plots) {
            const auto *plot=std::get_if<pl::Plot>(&p.object(id).data);
            if (!plot || std::get<pl::Column>(p.object(plot->x).data).units!="Hz" ||
                std::get<pl::Column>(p.object(plot->y).data).units!="F") return false;
        }
        const auto &spec=std::get<pl::Layer>(p.object(layers.front()).data).spec;
        return spec.x.label=="Frequency (Hz)" && spec.y.label=="Capacitance (F)" &&
            spec.x.scale==sst::ui::plot::AxisScale::Log10 && spec.y.scale==sst::ui::plot::AxisScale::Linear;
    }
    inline pl::Id MeasurementGraph(const pl::Project &p, pl::Id sheet)
    {
        for (const auto &[id,o]:p.objects())
            if (const auto *plot=std::get_if<pl::Plot>(&o->data); plot && plot->worksheet==sheet)
                return p.object(o->parent).parent;
        throw std::runtime_error("Imported measurement has no graph.");
    }
    inline pl::Table PreviewTable(const Json &preview, std::optional<double> temperature)
    {
        Optional(Nullable(temperature));
        const auto values = preview.at("columns").get<std::vector<std::vector<double>>>();
        const auto rows = preview.at("source_rows").get<std::vector<std::size_t>>();
        if (values.size() < 3 || values.front().empty() || rows.size() != values.front().size() ||
            std::set<std::size_t>(rows.begin(),rows.end()).size() != rows.size() || (!rows.empty() && rows.front() == 0))
            throw std::runtime_error("Expected nonempty f, Cp, Rp columns and source rows.");
        pl::Table table;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (values[i].size() != rows.size()) throw std::runtime_error("Unequal AS columns.");
            for (double v : values[i])
                if (!std::isfinite(v) || ((i == 0 || i == 2) && v <= 0)) throw std::runtime_error("Invalid AS value.");
            pl::Column c;
            c.units = i == 0 ? "Hz" : i == 1 ? "F" : i == 2 ? "Ohm" : "";
            c.role = i == 0 ? pl::Role::X : i == 1 ? pl::Role::Y : pl::Role::None;
            c.values = values[i];
            table.columns.push_back(std::move(c));
            table.names.push_back(i == 0 ? "Frequency" : i == 1 ? "Cp" : i == 2 ? "Rp" : "Source" + std::to_string(i+1));
        }
        if (preview.at("profile") != "lab_as_cp_rp") throw std::runtime_error("Select the lab Cp/Rp profile explicitly.");
        table.source = {{"profile","lab_as_cp_rp"},{"version","1"},{"path",preview.at("source").get<std::string>()},
            {"sha256",preview.at("sha256").get<std::string>()},{"source_rows",preview.at("source_rows").dump()},
            {"header",preview.at("header").get<std::string>()},{"decimal",preview.at("decimal").get<std::string>()},
            {"temperature_K",Nullable(temperature).dump()},{"temperature_header_K",preview.at("temperature_K").dump()},
            {"temperature_filename_K",preview.at("suggested_temperature_K").dump()},{"temperature_source","preview_confirmed"},
            {"units","Hz,F,Ohm"},{"conversions","none (SI)"}};
        return table;
    }
    class CFTemplate final : public pl::PlotTemplate
    {
    public:
        std::string typeId() const override { return "caplab.cf"; }
        unsigned version() const override { return 1; }
        std::vector<pl::ColumnRequirement> requirements() const override { return {{"x",pl::Role::X,true,"Hz"},{"y",pl::Role::Y,true,"F"}}; }
        pl::Operation instantiate(pl::Project &p, pl::Id folder, pl::Id sheet, const std::map<std::string,pl::Id> &columns,
                                  const pl::Parameters &parameters) const override
        {
            for (const auto &r : requirements())
            {
                const auto &o = p.object(columns.at(r.name));
                const auto &c = std::get<pl::Column>(o.data);
                if (o.parent != sheet || c.units != r.units || c.role != r.role) throw std::runtime_error("C-f requires Hz/F columns.");
            }
            pl::Id layer;
            std::vector<pl::Object> objects;
            pl::Id graph = std::stoull(parameters.at("graph"));
            if (!IsCFGraph(p,graph)) {
                graph=0;
                for (const auto &[id,o]:p.objects()) if (std::holds_alternative<pl::Graph>(o->data) && IsCFGraph(p,id)) {graph=id; break;}
            }
            if (graph) layer = p.children(graph).at(0);
            else
            {
                const auto id = p.reserveId(); layer = p.reserveId();
                pl::Layer l; l.spec.x.label = "Frequency (Hz)"; l.spec.y.label = "Capacitance (F)";
                l.spec.x.scale = sst::ui::plot::AxisScale::Log10;
                FormatCompareAxes(l.spec);
                l.spec.autoFit = sst::ui::plot::AutoFit::Once;
                objects = {{id,folder,"C-f",0,pl::Graph{}},{layer,id,"C-f",0,l}};
            }
            pl::Plot series{sheet,columns.at("x"),columns.at("y")};
            series.kind = sst::ui::plot::SeriesKind::LineScatter;
            series.style.color = {0.12f,0.45f,0.78f,1}; series.style.lineWidthPt = 1;
            series.style.marker = sst::ui::plot::Marker::Circle; series.style.markerSizePt = 4;
            const auto &source = std::get<pl::Worksheet>(p.object(sheet).data).source;
            std::string name = p.object(p.object(sheet).parent).name;
            Json temperature = nullptr;
            if (source.contains("temperature_K")) temperature = Json::parse(source.at("temperature_K"));
            if (source.contains("provenance")) {
                const auto metadata = Json::parse(source.at("provenance"));
                const auto path = Path::FromResolved(std::filesystem::u8path(metadata.at("source").get<std::string>()).stem());
                if (!path.Empty()) name = path.Utf8();
                temperature = metadata.at("temperature_K");
            }
            if (!temperature.is_null()) name += ", T = " + temperature.dump() + " K";
            series.style.color=SampleTemperatureColor(p.object(sheet).parent,temperature);
            objects.push_back({p.reserveId(),layer,name,0,series});
            return p.change("C-f graph",std::move(objects));
        }
    };
    // One Core undo entry owns the imported raw data and its graph definition.
    class ImportChange
    {
    public:
        ImportChange(Project &p, const Json &preview, std::optional<double> temperature, const pl::PlotTemplate &preset)
            : target(p), oldGraph(p.graph)
        {
            auto table = PreviewTable(preview,temperature);
            const auto name = Path::FromResolved(std::filesystem::u8path(table.source.at("path")).stem()).Utf8();
            data = pl::importTable(*p.engine,p.engine->root(),name,table);
            p.samples.reserve(p.samples.size()+1); p.sweeps.reserve(p.sweeps.size()+1);
            data.apply(*p.engine);
            try
            {
                const auto book = p.engine->children(p.engine->root()).back();
                const auto sheet = p.engine->children(book).at(0);
                const auto cols = p.engine->children(sheet);
                sample = {book,name,{}};
                sweep = {sheet,book,cols.at(0),cols.at(1),cols.at(2),temperature};
                graph = preset.instantiate(*p.engine,p.engine->root(),sheet,{{"x",sweep.frequency},{"y",sweep.capacitance}},{{"graph",std::to_string(oldGraph)}});
                graph.apply(*p.engine);
            }
            catch (...) { data.undo(*p.engine); throw; }
            newGraph = MeasurementGraph(*p.engine,sweep.id);
            Finish();
        }
        void Redo() { data.apply(*target.engine); graph.apply(*target.engine); Finish(); }
        void Undo()
        {
            graph.undo(*target.engine); data.undo(*target.engine);
            target.samples.pop_back(); target.sweeps.pop_back(); target.graph = oldGraph;
        }
    private:
        Project &target;
        pl::Operation data, graph;
        Sample sample{}; FrequencySweep sweep{};
        pl::Id oldGraph = 0, newGraph = 0;
        void Finish() { target.samples.push_back(sample); target.sweeps.push_back(sweep); target.graph = newGraph; }
    };
}
