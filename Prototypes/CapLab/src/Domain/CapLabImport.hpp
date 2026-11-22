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
    inline void ValidatePreview(const Json &preview, std::optional<double> temperature)
    {
        Optional(Nullable(temperature));
        const auto &values = preview.at("columns");
        const auto rows = preview.at("source_rows").get<std::vector<std::size_t>>();
        if (!values.is_array() || values.size() < 3 || values.front().empty() || rows.size() != values.front().size() ||
            std::set<std::size_t>(rows.begin(),rows.end()).size() != rows.size() || (!rows.empty() && rows.front() == 0))
            throw std::runtime_error("Expected nonempty f, Cp, Rp columns and source rows.");
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (!values[i].is_array() || values[i].size() != rows.size()) throw std::runtime_error("Unequal AS columns.");
            for (const auto &cell : values[i]) {
                const double v=cell.get<double>();
                if (!std::isfinite(v) || ((i == 0 || i == 2) && v <= 0)) throw std::runtime_error("Invalid AS value.");
            }
        }
        if (preview.at("profile") != "lab_as_cp_rp") throw std::runtime_error("Select the lab Cp/Rp profile explicitly.");
    }
    inline pl::Table PreviewTable(const Json &preview, std::optional<double> temperature)
    {
        ValidatePreview(preview,temperature);
        auto values=preview.at("columns").get<std::vector<std::vector<double>>>();
        pl::Table table;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            pl::Column c;
            c.units = i == 0 ? "Hz" : i == 1 ? "F" : i == 2 ? "Ohm" : "";
            c.role = i == 0 ? pl::Role::X : i == 1 ? pl::Role::Y : pl::Role::None;
            c.values = std::move(values[i]);
            table.columns.push_back(std::move(c));
            table.names.push_back(i == 0 ? "Frequency" : i == 1 ? "Cp" : i == 2 ? "Rp" : "Source" + std::to_string(i+1));
        }
        table.source = {{"profile","lab_as_cp_rp"},{"version","1"},{"path",preview.at("source").get<std::string>()},
            {"sha256",preview.at("sha256").get<std::string>()},
            {"header",preview.at("header").get<std::string>()},{"decimal",preview.at("decimal").get<std::string>()},
            {"temperature_source","preview_confirmed"},
            {"units","Hz,F,Ohm"},{"conversions","none (SI)"}};
        return table;
    }
    class CFTemplate final
    {
    public:
        std::vector<pl::ColumnRequirement> requirements() const { return {{"x",pl::Role::X,true,"Hz"},{"y",pl::Role::Y,true,"F"}}; }
        pl::Operation instantiate(Project &project, pl::Id sheet, const std::map<std::string,pl::Id> &columns, pl::Id graph) const
        {
            auto &p=*project.engine;
            for (const auto &r : requirements())
            {
                const auto &o = p.object(columns.at(r.name));
                const auto &c = std::get<pl::Column>(o.data);
                if (o.parent != sheet || c.units != r.units || c.role != r.role) throw std::runtime_error("C-f requires Hz/F columns.");
            }
            pl::Id layer;
            std::vector<pl::Object> objects;
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
                objects = {{id,p.root(),"C-f",0,pl::Graph{}},{layer,id,"C-f",0,l}};
            }
            pl::Plot series{sheet,columns.at("x"),columns.at("y")};
            series.kind = sst::ui::plot::SeriesKind::LineScatter;
            series.style.color = {0.12f,0.45f,0.78f,1}; series.style.lineWidthPt = 1;
            series.style.marker = sst::ui::plot::Marker::Circle; series.style.markerSizePt = 4;
            const auto &source = project.sources.at(sheet);
            std::string name = p.object(p.object(sheet).parent).name;
            Json temperature = nullptr;
            if (source.temperatures.contains("temperature_K")) temperature = Nullable(source.temperatures.at("temperature_K"));
            if (source.provenance) {
                const auto &metadata = *source.provenance;
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
        ImportChange(Project &p, const Json &preview, std::optional<double> temperature, const CFTemplate &preset)
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
                source.rows=preview.at("source_rows").get<std::vector<std::size_t>>(); source.hasRows=true;
                source.temperatures={{"temperature_K",temperature},{"temperature_header_K",Optional(preview.at("temperature_K"))},
                    {"temperature_filename_K",Optional(preview.at("suggested_temperature_K"))}};
                p.sources[sheet]=source;
                graph = preset.instantiate(p,sheet,{{"x",sweep.frequency},{"y",sweep.capacitance}},oldGraph);
                graph.apply(*p.engine);
            }
            catch (...) { if (sweep.id) p.sources.erase(sweep.id); data.undo(*p.engine); throw; }
            newGraph = MeasurementGraph(*p.engine,sweep.id);
            Finish();
        }
        void Redo() { data.apply(*target.engine); graph.apply(*target.engine); target.sources[sweep.id]=source; Finish(); }
        void Undo()
        {
            graph.undo(*target.engine); data.undo(*target.engine);
            target.sources.erase(sweep.id);
            target.samples.pop_back(); target.sweeps.pop_back(); target.graph = oldGraph;
        }
    private:
        Project &target;
        pl::Operation data, graph;
        Sample sample{}; FrequencySweep sweep{};
        WorksheetSource source;
        pl::Id oldGraph = 0, newGraph = 0;
        void Finish() { target.samples.push_back(sample); target.sweeps.push_back(sweep); target.graph = newGraph; }
    };
}
