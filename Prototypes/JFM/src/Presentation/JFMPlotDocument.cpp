#include "Presentation/JFMPlotDocument.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <sst/ui/Plot/Axis.hpp>

namespace SST::Prototypes::JFM
{
    namespace pl = sst::plotlab;
    namespace plot = sst::ui::plot;
    JFMPlotDocument::JFMPlotDocument()
    {
        graph = project.reserveId(); layer = project.reserveId(); workbook = project.reserveId();
        project.change("JFM figure", {{graph, project.root(), "Figure", 0, pl::Graph{}},
            {layer, graph, "Axes", 0, pl::Layer{}}, {workbook, project.root(), "Results", 0, pl::Workbook{}}}).apply(project);
    }
    void JFMPlotDocument::BeginFrame() { active.clear(); rendered.clear(); }
    pl::Id JFMPlotDocument::SetSeries(const plot::SeriesEntryView &entry, bool absoluteCurrent)
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
        else { id = found->second; source = std::get<pl::Plot>(project.object(id).data); }
        pl::Worksheet sheet;
        if (found != series.end()) sheet = std::get<pl::Worksheet>(project.object(source.worksheet).data);
        std::vector<double> x, y, error;
        for (std::size_t i = 0; i < entry.data.count; ++i)
        {
            const auto value = absoluteCurrent ? std::abs(entry.data.y[i]) : entry.data.y[i];
            if (!std::isfinite(entry.data.x[i]) || !std::isfinite(value) || (absoluteCurrent && value <= 0)) continue; // Preserve JFM's historical abs(I) convention.
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
        source.kind = entry.kind; source.style = entry.style; source.visible = entry.visible;
        const auto order = static_cast<std::uint64_t>(rendered.size());
        auto styleKey = [](const plot::SeriesStyle &s) { return std::tie(s.color,s.markerFill,s.markerOutline,s.marker,s.lineWidthPt,s.markerSizePt,s.errorCapPt,s.legend); };
        bool changed = found == series.end();
        if (!changed) {
            const auto &o = project.object(id); const auto &old = std::get<pl::Plot>(o.data);
            changed = o.name != name || o.order != order || old.kind != source.kind || old.visible != source.visible
                || old.yError != source.yError || styleKey(old.style) != styleKey(source.style);
        }
        if (changed) changes.push_back({id, layer, name, 0, source, order});
        if (!changes.empty()) project.change("Refresh JFM results", std::move(changes), std::move(erase)).apply(project);
        series[key] = id; active.insert(id); rendered.emplace_back(id, entry.id);
        return id;
    }
    void JFMPlotDocument::EndFrame(plot::PlotSpec spec)
    {
        std::vector<pl::Object> changes;
        for (const auto &[key, id] : series) if (!active.contains(id)) {
            auto o = project.object(id); auto &p = std::get<pl::Plot>(o.data);
            if (p.visible) { p.visible = false; changes.push_back(std::move(o)); }
        }
        const auto &old = std::get<pl::Layer>(project.object(layer).data).spec;
        if (old.x.label != spec.x.label || old.y.label != spec.y.label || old.x.scale != spec.x.scale
            || old.y.scale != spec.y.scale || old.x.labels.format != spec.x.labels.format || old.y.labels.format != spec.y.labels.format
            || old.x.labels.formatString != spec.x.labels.formatString || old.y.labels.formatString != spec.y.labels.formatString
            || old.legend.visible != spec.legend.visible
            || old.legend.outside != spec.legend.outside || old.legend.interactive != spec.legend.interactive)
            changes.push_back({layer, graph, "Axes", 0, pl::Layer{std::move(spec)}});
        if (!changes.empty()) project.change("Refresh JFM figure", std::move(changes)).apply(project);
    }
    std::shared_ptr<const pl::GraphSnapshot> JFMPlotDocument::Snapshot() const
    {
        auto exportView = view;
        const auto &spec = std::get<pl::Layer>(project.object(layer).data).spec;
        if (exportView.viewport) {
            const auto &b = *exportView.viewport;
            auto ticks = [](const auto &axis, double lo, double hi, auto &out) {
                if (out.empty()) for (const auto &tick : plot::BuildTickPlan(axis, lo, hi).ticks)
                    out.push_back({tick.value, tick.label, tick.major});
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
        spec.legend.outside = true;
        spec.x.labels.format = spec.y.labels.format = plot::LabelFormat::Custom;
        spec.x.labels.formatString = spec.y.labels.formatString = "%.6g";
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
