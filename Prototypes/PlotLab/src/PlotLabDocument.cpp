#include "PlotLabDocument.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
namespace SST::Prototypes::PlotLab
{
    namespace
    {
        StructuredError Error(const std::exception &e)
        {
            return {ErrorCategory::Validation,
                    Severity::Error,
                    "PlotLab operation failed",
                    e.what(),
                    "Check the data and referenced objects.",
                    "PlotLab",
                    "plotlab.operation"};
        }
    } // namespace
    double ParseNumericCell(const std::string &text)
    {
        if (text.empty())
            return std::numeric_limits<double>::quiet_NaN();
        double value;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            throw std::invalid_argument("Use a number with decimal point, or leave the cell blank");
        return value;
    }
    Result<void> EngineCommand::Execute(CommandContext &)
    {
        try
        {
            operation.apply(*project);
            return {};
        }
        catch (const std::exception &e)
        {
            return Error(e);
        }
    }
    Result<void> EngineCommand::Undo(CommandContext &)
    {
        try
        {
            operation.undo(*project);
            return {};
        }
        catch (const std::exception &e)
        {
            return Error(e);
        }
    }
    Document::Document(Ref<CommandRegistry> registry, Ref<UndoStack> history, Ref<EventBus> events)
        : commands(std::move(registry)), undo(std::move(history)), bus(std::move(events))
    {
        commands->SetUndoStack(undo);
        if (!commands->HasCommand(CommandID{"plotlab.edit"}))
        {
            auto registered = commands->Register(
                {CommandID{"plotlab.edit"}, "Edit PlotLab project", "PlotLab", "", {}, CommandFlags::Undoable},
                [](CommandContext &context) -> Unique<ICommand>
                {
                    auto *p = context.TryGet<Ref<pl::Project>>("project");
                    auto *op = context.TryGet<pl::Operation>("operation");
                    if (!p || !op)
                        return {};
                    return CreateUnique<EngineCommand>(*p, *op);
                });
            if (!registered)
                throw std::runtime_error(registered.Error().technicalDetails);
        }
        Replace(CreateRef<pl::Project>("Untitled"));
    }
    Document::~Document()
    {
        project->unsubscribe(*this);
    }
    void Document::changed(const pl::Event &e) noexcept
    {
        // Observers cannot throw into an already committed engine transaction.
        try
        {
            ModelChanged event;
            event.change = e;
            bus->Queue(event);
        }
        catch (...)
        {
        }
    }
    void Document::Replace(Ref<pl::Project> next)
    {
        if (project)
        {
            project->cancelRecompute();
            project->unsubscribe(*this);
        }
        undo->Clear();
        project = std::move(next);
        project->subscribe(*this);
        undo->MarkClean();
    }
    void Document::Execute(pl::Operation operation)
    {
        CommandContext context;
        context.Set("project", project);
        context.Set("operation", std::move(operation));
        auto result = commands->Execute(CommandID{"plotlab.edit"}, std::move(context));
        if (!result)
            throw std::runtime_error(result.Error().technicalDetails);
    }
    void Document::New()
    {
        Replace(CreateRef<pl::Project>("Untitled"));
        path.clear();
    }
    void Document::Open(const std::filesystem::path &source)
    {
        auto next = CreateRef<pl::Project>(pl::Project::load(source));
        Replace(std::move(next));
        path = source;
    }
    void Document::Save(const std::filesystem::path &destination)
    {
        project->save(destination);
        path = destination;
        undo->MarkClean();
    }
    pl::Id Document::Create(pl::Id parent, int type, std::string name)
    {
        auto id = project->reserveId();
        pl::Object object{id, parent, std::move(name)};
        switch (type)
        {
        case 0:
            object.data = pl::Folder{};
            break;
        case 1:
            object.data = pl::Workbook{};
            break;
        case 2:
            object.data = pl::Worksheet{};
            break;
        default:
            throw std::invalid_argument("Unsupported explorer object");
        }
        Execute(project->change("Create object", {std::move(object)}));
        return id;
    }
    void Document::Rename(pl::Id id, std::string name)
    {
        auto object = project->object(id);
        object.name = std::move(name);
        Execute(project->change("Rename object", {std::move(object)}));
    }
    std::vector<pl::Id> Document::RemovalIds(pl::Id id) const
    {
        if (id == project->root())
            throw std::invalid_argument("The project root cannot be deleted");
        (void)project->object(id);
        std::set<pl::Id> remove{id};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto &[key, o] : project->objects())
            {
                bool dependent = remove.contains(o->parent);
                if (const auto *p = std::get_if<pl::Plot>(&o->data))
                    for (auto input : {p->worksheet, p->x, p->y, p->z, p->yError, p->label})
                        dependent |= input && remove.contains(input);
                if (const auto *c = std::get_if<pl::Column>(&o->data))
                {
                    dependent |= c->errorOf && remove.contains(c->errorOf);
                    if (c->formula)
                        for (const auto &[name, input] : c->formula->bindings)
                            dependent |= remove.contains(input);
                }
                if (dependent)
                    changed |= remove.insert(key).second;
            }
        }
        return {remove.begin(), remove.end()};
    }
    void Document::Delete(pl::Id id)
    {
        Execute(project->change("Delete objects and confirmed dependents", {}, RemovalIds(id)));
    }
    void Document::ResizeRows(pl::Id id, std::optional<std::size_t> eraseRow)
    {
        auto sheet = project->object(id);
        auto &rows = std::get<pl::Worksheet>(sheet.data).rows;
        if (eraseRow)
        {
            if (*eraseRow >= rows.size())
                throw std::out_of_range("Select a row to delete");
            rows.erase(rows.begin() + *eraseRow);
        }
        else
            rows.push_back(project->reserveId());
        std::vector<pl::Object> updates{sheet};
        for (auto column : project->children(id))
        {
            auto o = project->object(column);
            auto &c = std::get<pl::Column>(o.data);
            std::visit(
                [&](auto &values)
                {
                    if (eraseRow)
                        values.erase(values.begin() + *eraseRow);
                    else
                    {
                        using T = typename std::decay_t<decltype(values)>::value_type;
                        if constexpr (std::is_same_v<T, double>)
                            values.push_back(std::numeric_limits<double>::quiet_NaN());
                        else
                            values.emplace_back();
                    }
                },
                c.values);
            updates.push_back(std::move(o));
        }
        Execute(project->change(eraseRow ? "Delete row" : "Add row", std::move(updates)));
    }
    pl::Id Document::AddColumn(pl::Id sheet, bool text)
    {
        const auto count = std::get<pl::Worksheet>(project->object(sheet).data).rows.size();
        pl::Column column;
        if (text)
            column.values = std::vector<std::string>(count);
        else
            column.values = std::vector<double>(count, std::numeric_limits<double>::quiet_NaN());
        const auto id = project->reserveId();
        Execute(project->change("Add column", {{id, sheet, "C" + std::to_string(id), 0, std::move(column), id}}));
        return id;
    }
    pl::Id Document::AddGraph(pl::Id folder, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error)
    {
        const auto graph = project->reserveId(), layer = project->reserveId(), series = project->reserveId();
        pl::Layer l;
        l.spec.x.label = project->object(x).name;
        l.spec.y.label = project->object(y).name;
        l.spec.autoFit = plot::AutoFit::Once;
        pl::Plot p{sheet, x, y, 0, error};
        p.kind = plot::SeriesKind::LineScatter;
        p.style.color = plot::Color{0.12f, 0.45f, 0.78f, 1};
        p.style.lineWidthPt = 1;
        p.style.markerSizePt = 4;
        p.style.marker = plot::Marker::Circle;
        Execute(project->change("Create graph", {{graph, folder, "Graph" + std::to_string(graph), 0, pl::Graph{}},
                                                 {layer, graph, "Layer1", 0, l},
                                                 {series, layer, project->object(y).name, 0, p}}));
        return graph;
    }
    void Document::AddSeries(pl::Id graph, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error)
    {
        const auto layer = project->children(graph).at(0);
        pl::Plot p{sheet, x, y, 0, error};
        p.kind = plot::SeriesKind::LineScatter;
        const auto id = project->reserveId();
        Execute(project->change("Add series", {{id, layer, project->object(y).name, 0, p}}));
    }
    void Document::SelectRow(pl::Id sheet, std::size_t row, pl::Id view, bool toggle, pl::Id series)
    {
        const auto &rows = std::get<pl::Worksheet>(project->object(sheet).data).rows;
        auto selected = project->selection();
        if (!toggle || selected.worksheet != sheet)
            selected.rows.clear();
        const auto id = rows.at(row);
        const auto it = std::find(selected.rows.begin(), selected.rows.end(), id);
        if (it == selected.rows.end())
            selected.rows.push_back(id);
        else
            selected.rows.erase(it);
        selected.worksheet = sheet;
        selected.plot = series;
        selected.sourceView = view;
        selected.correlation = ++correlation;
        project->select(std::move(selected));
    }
} // namespace SST::Prototypes::PlotLab
