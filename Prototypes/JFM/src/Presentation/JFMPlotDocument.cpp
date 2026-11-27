#include "Presentation/JFMPlotDocument.hpp"
#include "Core/PlotExport/PlotEditing.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <implot.h>
#include <implot_internal.h>
#include <cstdio>

namespace SST::Prototypes::JFM
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    JFMPlotDocument::JFMPlotDocument()
    {
        editCommands->SetUndoStack(editUndo);
        graph = project.reserveId(); layer = project.reserveId(); workbook = project.reserveId();
        project.change("JFM figure", {{graph, project.root(), "Figure", 0, pl::Graph{}},
            {layer, graph, "Axes", 0, pl::Layer{}}, {workbook, project.root(), "Results", 0, pl::Workbook{}}}).apply(project);
    }
    void JFMPlotDocument::BeginFrame() { active.clear(); rendered.clear(); autoFitInterrupted = false; }
    pl::Id JFMPlotDocument::SetSeries(const plot::SeriesEntryView &entry, bool positiveCurrentOnly)
    {
        auto delimiter = entry.id.find("###");
        if (delimiter == std::string::npos) delimiter = entry.id.find("##");
        const auto key = delimiter == std::string::npos ? entry.id : entry.id.substr(delimiter);
        const auto name = entry.id.substr(0, delimiter);
        auto found = series.find(key);
        pl::Plot source;
        pl::Id id;
        std::vector<pl::Object> changes;
        std::vector<pl::Id> erase;
        if (found == series.end())
        {
            id = project.reserveId();
            source.worksheet = project.reserveId(); source.x = project.reserveId(); source.y = project.reserveId();
        }
        else if (!project.objects().contains(found->second)) return 0;
        else { id = found->second; source = std::get<pl::Plot>(project.object(id).data); }
        bool unchanged=false;
        if(found!=series.end() && bool(source.yError)==bool(entry.data.yError)) {
            const auto& oldX=std::get<std::vector<double>>(std::get<pl::Column>(project.object(source.x).data).values);
            const auto& oldY=std::get<std::vector<double>>(std::get<pl::Column>(project.object(source.y).data).values);
            const auto* oldError=source.yError ? &std::get<std::vector<double>>(std::get<pl::Column>(project.object(source.yError).data).values) : nullptr;
            std::size_t row=0;unchanged=true;
            for(std::size_t i=0;i<entry.data.count;++i) {
                const double value=entry.data.y[i];
                if(!std::isfinite(entry.data.x[i]) || !std::isfinite(value) || (positiveCurrentOnly && value<=0)) continue;
                if(row>=oldX.size() || std::memcmp(&oldX[row],entry.data.x+i,sizeof(double)) || std::memcmp(&oldY[row],&value,sizeof(double)) ||
                   (oldError && std::memcmp(&(*oldError)[row],entry.data.yError+i,sizeof(double)))) {unchanged=false;break;}
                ++row;
            }
            unchanged=unchanged && row==oldX.size();
        }
        if(!unchanged) {
        pl::Worksheet sheet;
        if (found != series.end()) sheet = std::get<pl::Worksheet>(project.object(source.worksheet).data);
        std::vector<double> x, y, error;
        for (std::size_t i = 0; i < entry.data.count; ++i)
        {
            const auto value = entry.data.y[i];
            if (!std::isfinite(entry.data.x[i]) || !std::isfinite(value) || (positiveCurrentOnly && value <= 0)) continue;
            x.push_back(entry.data.x[i]); y.push_back(value);
            if (entry.data.yError) error.push_back(entry.data.yError[i]);
        }
        const auto oldRows = sheet.rows.size();
        sheet.rows.resize(x.size());
        for (std::size_t i = oldRows; i < sheet.rows.size(); ++i) sheet.rows[i] = project.reserveId();
        if (found == series.end() || oldRows != sheet.rows.size())
            changes.push_back({source.worksheet, workbook, key, 0, sheet});
        auto column = [&](pl::Id columnId, const char *label, pl::Role role, std::vector<double> values) {
            if (project.objects().contains(columnId)) {
                const auto &previous = std::get<std::vector<double>>(std::get<pl::Column>(project.object(columnId).data).values);
                if (previous.size() == values.size() && (values.empty() || std::memcmp(previous.data(), values.data(), values.size()*sizeof(double)) == 0)) return;
            }
            pl::Column c; c.role = role; c.values = std::move(values);
            if (role == pl::Role::YError) c.errorOf = source.y;
            changes.push_back({columnId, source.worksheet, label, 0, std::move(c)});
        };
        column(source.x, "X", pl::Role::X, std::move(x));
        column(source.y, "Y", pl::Role::Y, std::move(y));
        if (entry.data.yError) {
            if (!source.yError) source.yError = project.reserveId();
            column(source.yError, "YError", pl::Role::YError, std::move(error));
        } else { if (source.yError) erase.push_back(source.yError); source.yError = 0; }
        }
        source.kind = entry.kind;
        const auto previous=lastInput.find(id);
        const bool created=found==series.end();
        if(created || (previous!=lastInput.end() && !(previous->second.style==entry.style))) source.style=entry.style;
        if(created || (previous!=lastInput.end() && previous->second.visible!=entry.visible)) source.visible=entry.visible;
        source.group=entry.group;
        const auto order=created ? static_cast<std::uint64_t>(project.children(layer).size()) : project.object(id).order;
        const auto displayName=created || !source.legendNameOverridden ? name : project.object(id).name;
        lastInput[id]={name,entry.style,entry.visible};
        bool changed = found == series.end();
        if (!changed) {
            const auto &o = project.object(id); const auto &old = std::get<pl::Plot>(o.data);
            changed = o.name != displayName || o.order != order || old.kind != source.kind || old.visible != source.visible
                || old.yError != source.yError || old.group != source.group || !(old.style == source.style);
        }
        if (changed) changes.push_back({id, layer, displayName, 0, source, order});
        if (!changes.empty()) project.change("Refresh JFM results", std::move(changes), std::move(erase)).apply(project);
        series[key] = id; active.insert(id); rendered.emplace_back(id, displayName + "###" + std::to_string(id));
        return id;
    }
    void JFMPlotDocument::EndFrame(plot::PlotSpec spec)
    {
        std::vector<pl::Object> changes;
        view.excluded.clear();
        for (const auto& [key,id]:series) if(project.objects().contains(id) && !active.contains(id)) view.excluded.push_back(std::to_string(id));
        SetSpec(std::move(spec));
        if (!plot::StoredViewEqual(std::get<pl::Layer>(project.object(layer).data).view,view)) {
            auto saved = project.object(layer); std::get<pl::Layer>(saved.data).view = view;
            changes.push_back(std::move(saved));
        }
        if (!changes.empty()) project.change("Refresh JFM figure", std::move(changes)).apply(project);
    }
    void JFMPlotDocument::SetSpec(plot::PlotSpec requested)
    {
        auto object = project.object(layer);
        auto& current = std::get<pl::Layer>(object.data).spec;
        auto spec = current;
        if (!baseSpec && !current.customAxes) spec = requested;
        else if (baseSpec) {
            if (requested.x.label != baseSpec->x.label) spec.x.label = requested.x.label;
            if (requested.y.label != baseSpec->y.label) spec.y.label = requested.y.label;
            if (requested.x.scale != baseSpec->x.scale) { spec.x.scale = requested.x.scale; spec.x.range.reset(); }
            if (requested.y.scale != baseSpec->y.scale) { spec.y.scale = requested.y.scale; spec.y.range.reset(); }
            if (!(requested.legend == baseSpec->legend)) spec.legend = requested.legend;
        }
        if (requested.autoFit == plot::AutoFit::Always) {
            spec.x.range.reset(); spec.y.range.reset(); spec.bounds.reset();
            spec.x.rescale = spec.y.rescale = plot::RescaleMode::Normal;
            spec.autoFit = requested.autoFit;
            spec.x.scale = requested.x.scale; spec.y.scale = requested.y.scale;
        }
        if ((!baseSpec || requested.x.scale != baseSpec->x.scale) && spec.top && !spec.top->linked && spec.top->formula.empty()) {
            spec.top->scale = requested.x.scale; spec.top->range.reset();
        }
        if ((!baseSpec || requested.y.scale != baseSpec->y.scale) && spec.right && !spec.right->linked && spec.right->formula.empty()) {
            spec.right->scale = requested.y.scale; spec.right->range.reset();
        }
        spec.autoFit = requested.autoFit;
        spec.padding = requested.padding; spec.customAxes = true;
        baseSpec = requested;
        if (!(current == spec)) { current = std::move(spec); project.change("Refresh JFM axes", {std::move(object)}).apply(project); }
    }
    void JFMPlotDocument::ApplyEdits()
    {
        auto pending = std::move(editor.pending); editor.pending.clear();
        for (const auto& edit : pending) {
            autoFitInterrupted |= edit.kind == plot::PlotEditKind::Axis;
            const auto result = ExecutePlotEdit(*editCommands, engine, layer, edit,
                [owner=weak_from_this()] { if (auto doc=owner.lock()) { doc->view = std::get<pl::Layer>(doc->project.object(doc->layer).data).view; doc->view.visibility.clear(); } });
            if (!result) editor.error = result.Error().technicalDetails;
        }
    }
    void JFMPlotDocument::Save(const std::filesystem::path& path) const { project.save(path); }
    void JFMPlotDocument::Load(const std::filesystem::path& path)
    {
        auto loaded = pl::Project::load(path);
        pl::Id loadedGraph=0, loadedLayer=0, loadedWorkbook=0;
        for (const auto& [id,object] : loaded.objects()) {
            if (std::holds_alternative<pl::Graph>(object->data)) { if (loadedGraph) throw std::invalid_argument("Expected one JFM figure"); loadedGraph=id; }
            if (std::holds_alternative<pl::Workbook>(object->data)) loadedWorkbook=id;
        }
        if (!loadedGraph || !loadedWorkbook || loaded.children(loadedGraph).size()!=1) throw std::invalid_argument("Expected a JFM figure with its source workbook");
        loadedLayer=loaded.children(loadedGraph).front();
        std::map<std::string,pl::Id> sources;
        for (auto id : loaded.children(loadedLayer)) {
            const auto& plot = std::get<pl::Plot>(loaded.object(id).data);
            sources[loaded.object(plot.worksheet).name]=id;
        }
        for (auto sheet : loaded.children(loadedWorkbook)) if (!sources.contains(loaded.object(sheet).name)) sources[loaded.object(sheet).name]=loaded.reserveId();
        project=std::move(loaded); graph=loadedGraph; layer=loadedLayer; workbook=loadedWorkbook;
        series=std::move(sources); lastInput.clear(); baseSpec.reset(); active.clear(); rendered.clear();
        view=std::get<pl::Layer>(project.object(layer).data).view;
        editor={}; editUndo->Clear();
    }
    std::shared_ptr<const pl::GraphSnapshot> JFMPlotDocument::Snapshot() const
    {
        auto exportView = view;
        const auto &spec = std::get<pl::Layer>(project.object(layer).data).spec;
        if (exportView.viewport) {
            const auto &b = *exportView.viewport;
            auto ticks = [](const auto &axis, double lo, double hi, auto &out) {
                if (!out.empty() || !std::isfinite(lo) || !std::isfinite(hi) || lo >= hi) return;
                const bool log = axis.scale == plot::AxisScale::Log10;
                if (log && lo <= 0) return;
                if (log) { lo = std::log10(lo); hi = std::log10(hi); }
                const double step = ImPlot::NiceNum((hi - lo) / 5, true);
                if (!std::isfinite(step) || step <= 0) return;
                const double start = std::ceil(lo / step) * step;
                for (int i = 0; i < 10 && start + i * step <= hi; ++i) {
                    const double value = log ? std::pow(10., start + i * step) : start + i * step;
                    char label[64]; std::snprintf(label, sizeof(label), "%.6g", value);
                    out.push_back({value, label, true});
                }
            };
            ticks(spec.x, b.minX, b.maxX, exportView.xTicks);
            ticks(spec.y, b.minY, b.maxY, exportView.yTicks);
        }
        return pl::snapshotGraph(project, graph, exportView);
    }
    plot::Bounds PopulateJFMMonteCarloPlot(JFMPlotDocument &doc, const JFMMonteCarloScatterStatistics &statistics,
        const JFMMonteCarloScatterPlotOptions &options, const std::string &xLabel, const std::string &yLabel)
    {
        if (statistics.points.empty()) throw std::invalid_argument("No accepted Monte Carlo samples");
        std::array<std::vector<double>, 5> x, y;
        auto first = statistics.points.front();
        plot::Bounds bounds{first.x,first.x,first.y,first.y};
        for (const auto &point : statistics.points) {
            const auto group = std::min<std::size_t>(point.sigmaClass, 4);
            x[group].push_back(point.x); y[group].push_back(point.y);
            bounds.minX = std::min(bounds.minX, point.x); bounds.maxX = std::max(bounds.maxX, point.x);
            bounds.minY = std::min(bounds.minY, point.y); bounds.maxY = std::max(bounds.maxY, point.y);
        }
        doc.BeginFrame();
        const auto &labels = JFMSigmaLabels;
        for (int group = 4; group >= 0; --group) {
            plot::SeriesStyle style;
            style.color = style.markerFill = style.markerOutline = options.sigmaColors[group];
            style.marker = static_cast<plot::Marker>(1 + std::clamp(options.marker, 0, 5));
            style.markerSizePt = 0.75f;
            style.lineWidthPt = 0.35f;
            doc.SetSeries({labels[group], {x[group].data(), y[group].data(), x[group].size()},
                plot::SeriesKind::Scatter, style, options.showSigmaClasses[group] && !x[group].empty()});
        }
        plot::PlotSpec spec; spec.x.label = xLabel; spec.y.label = yLabel;
        spec.legend.outside = true;spec.padding=options.zoomOut;
        doc.EndFrame(spec);
        auto padded = [&](double lo, double hi) {
            // Physical values can be far below one ampere; never impose an absolute span.
            const double span = hi > lo ? hi-lo : (lo != 0 ? std::abs(lo)*0.1 : 1.0);
            const double padding = span * (hi > lo ? options.zoomOut : std::max(options.zoomOut, 0.5f));
            return std::pair{lo-padding, hi+padding};
        };
        const auto [minX, maxX] = padded(bounds.minX, bounds.maxX);
        const auto [minY, maxY] = padded(bounds.minY, bounds.maxY);
        return {minX, maxX, minY, maxY};
    }

}

