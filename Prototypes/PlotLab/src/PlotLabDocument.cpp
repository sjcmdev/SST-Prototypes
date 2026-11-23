#include "PlotLabDocument.hpp"
#include "WorksheetPython.hpp"
#include <sst/plotlab/edges/CommandJson.hpp>
#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <cctype>
#include <cmath>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <sst/ui/Theme/ScientificPlotPreset.hpp>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include "Core/PlotExport/PlotEditingPython.hpp"
#include <nanobind/stl/string.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/vector.h>
#endif
namespace SST::Prototypes::PlotLab
{
    namespace
    {
        class DataJob final : public IJob {
        public:
            std::unique_ptr<pl::Project> snapshot;
            pl::DataRequest request;
            std::optional<pl::Operation> operation;
            Ref<pl::Project> base;
            std::uint64_t revision = 0;
            double elapsedMs = 0;
            std::string GetName() const override {return "PlotLab data operation";}
            std::string GetType() const override {return "plotlab.data";}
            void Execute(JobContext& context) override {
                context.ThrowIfCancellationRequested();
                // shortcut: published jobs need a fresh owner-thread snapshot; resubmit commands before supporting Core Retry.
                if(!snapshot) throw std::invalid_argument("Resubmit the worksheet command to retry with a fresh snapshot");
                const auto start=std::chrono::steady_clock::now();
                operation=snapshot->dataOperation(request);
                snapshot->prepareOperation(*operation);
                elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                context.ThrowIfCancellationRequested();context.SetProgress(1,1);
            }
        };
        void StopWorker(pl::Project& project) {
#if DS_PYTHON_CAPI_AVAILABLE
            std::optional<nanobind::gil_scoped_release> release;
            if(PythonInterpreter::IsRuntimeInitialized() && PyGILState_Check()) release.emplace();
#endif
            // A Python caller may own the GIL while the cancelled worker is acquiring it.
            project.setBeforeFormulaRunner({});
        }
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
    void OpenJob::Execute(JobContext &context)
    {
        context.ThrowIfCancellationRequested();
        auto extension = path.Native().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension == ".plotlab") project = CreateRef<pl::Project>(pl::Project::load(path.Native()));
        else if (extension == ".csv" || extension == ".txt" || extension == ".dat") {
            std::string text, error;
            if (!TextFileIO::Load(path, text, error)) throw std::runtime_error(error);
            table = pl::CsvImporter{}.read(text);
            table->source["path"] = path.Utf8();
        } else throw std::invalid_argument("Expected .plotlab, .csv, .txt or .dat");
    }
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
    std::vector<double> ParseNumericList(const std::string& text) {
        std::vector<double> values;if(text.empty())return values;
        for(std::size_t first=0;;) {
            auto end=text.find(',',first);auto token=text.substr(first,end==std::string::npos?end:end-first);
            const auto begin=token.find_first_not_of(" \t\r\n"),last=token.find_last_not_of(" \t\r\n");
            if(begin==std::string::npos)throw std::invalid_argument("Expected comma-separated numbers");
            values.push_back(ParseNumericCell(token.substr(begin,last-begin+1)));
            if(end==std::string::npos)return values;first=end+1;
        }
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
    Document::Document(Ref<CommandRegistry> registry, Ref<UndoStack> history, Ref<EventBus> events, Ref<JobSystem> jobs)
        : commands(std::move(registry)), undo(std::move(history)), bus(std::move(events)), dataJobs(std::move(jobs))
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
        if(ownsDataJobs) dataJobs->Shutdown(true);
        else if(dataJob) (void)dataJobs->RequestCancel(dataJob);
        StopWorker(*project);
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
            StopWorker(*project);
            project->unsubscribe(*this);
        }
        undo->Clear();
        project = std::move(next);
        project->setBeforeFormulaRunner([](const std::string& script,const pl::Project::FormulaInputs& data) {
#if DS_PYTHON_CAPI_AVAILABLE
            if(!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
            nanobind::gil_scoped_acquire gil;
            nanobind::dict inputs;
            for(const auto& [id,values]:data) {
                nanobind::list column;
                for(double value:values) {if(std::isfinite(value))column.append(value);else column.append(nanobind::none());}
                inputs[nanobind::str(std::to_string(id).c_str())]=std::move(column);
            }
            return nanobind::cast<std::map<std::string,double>>(nanobind::module_::import_("sst_plotlab.formula_script").attr("run_typed")(script,inputs));
#else
            (void)script; (void)data;
            throw std::runtime_error("Embedded Python is unavailable");
            return std::map<std::string,double>{};
#endif
        });
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
        Open(std::move(next), source);
    }
    void Document::Open(Ref<pl::Project> next, const std::filesystem::path &source)
    {
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
                    if (c->formula) {
                        for (const auto &[name, input] : c->formula->bindings) dependent |= remove.contains(input);
                        for (auto input:c->formula->scriptInputs) dependent |= remove.contains(input);
                    }
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
        l.spec.x.label = x ? project->object(x).name : "Row";
        l.spec.y.label = project->object(y).name;
        l.spec.autoFit = plot::AutoFit::Once;
        l.spec.legend.outside = true; l.spec.legend.position = plot::LegendPosition::OutsideRight;
        pl::Plot p{sheet, x, y, 0, error};
        p.kind = plot::SeriesKind::LineScatter;
        p.style.color = plot::Color{0.12f, 0.45f, 0.78f, 1};
        p.style.lineWidthPt = 1;
        p.style.markerSizePt = 4;
        p.style.marker = plot::Marker::Circle;
        Execute(project->change("Create graph", {{graph, folder, NextName(4, "Graph"), 0, pl::Graph{}},
                                                 {layer, graph, "Layer1", 0, l},
                                                 {series, layer, project->object(y).name, 0, p}}));
        return graph;
    }
    void Document::ClearCells(pl::Id sheet, const std::vector<pl::Id>& columns, const std::vector<pl::Id>& rowIds) {
        auto& p = *project; const auto& rows = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        if (columns.empty()) throw std::invalid_argument("Select cells or columns first.");
        for (auto column : columns) if (p.object(column).parent != sheet || !std::holds_alternative<pl::Column>(p.object(column).data)) throw std::invalid_argument("Selected column belongs to another worksheet.");
        const std::set<pl::Id> selectedRows(rowIds.begin(), rowIds.end());
        std::vector<std::pair<std::size_t,std::size_t>> runs;
        std::size_t found = 0;
        for (std::size_t row = 0; row < rows.size(); ++row) if (rowIds.empty() || selectedRows.contains(rows[row])) {
            ++found;
            if (p.rowVisible(sheet, row)) { if (runs.empty() || runs.back().second != row) runs.emplace_back(row, row + 1); else ++runs.back().second; }
        }
        if (!rowIds.empty() && found != selectedRows.size()) throw std::invalid_argument("Unknown worksheet row ID.");
        auto group = undo->ScopedGroup("Clear cells");
        try {
            for (auto column : columns) for (const auto& [first,end] : runs) {
                const auto& c = std::get<pl::Column>(p.object(column).data);
                Execute(p.setCells(column, first, c.values.index() == 0 ? pl::Values{std::vector<double>(end-first, std::numeric_limits<double>::quiet_NaN())} : pl::Values{std::vector<std::string>(end-first)}));
            }
            const auto result = group.Commit(); if (!result) throw std::runtime_error(result.Error().technicalDetails);
        } catch (...) { (void)group.Cancel(); throw; }
    }
    std::string Document::NextName(std::size_t type, std::string_view prefix, pl::Id parent) const {
        for (unsigned i = 1;; ++i) {
            auto name = std::string(prefix) + std::to_string(i);
            if (std::none_of(project->objects().begin(), project->objects().end(), [&](const auto& entry) {
                const auto& o = *entry.second; return o.data.index() == type && (!parent || o.parent == parent) && o.name == name;
            })) return name;
        }
    }
    pl::Id Document::AddSeries(pl::Id graph, pl::Id sheet, pl::Id x, pl::Id y, pl::Id error)
    {
        const auto layer = project->children(graph).at(0);
        pl::Plot p{sheet, x, y, 0, error};
        p.kind = plot::SeriesKind::LineScatter;
        const auto id = project->reserveId();
        Execute(project->change("Add series", {{id, layer, project->object(y).name, 0, p}}));
        return id;
    }
    pl::Id Document::PlotColumns(pl::Id sheet, const std::vector<pl::Id>& selected, plot::SeriesKind kind,
                                 pl::Id graph, pl::Id explicitX, pl::Id error)
    {
        if (kind != plot::SeriesKind::Line && kind != plot::SeriesKind::Scatter && kind != plot::SeriesKind::LineScatter && kind != plot::SeriesKind::Column)
            throw std::invalid_argument("Choose Line, Scatter, Line + Symbol or Column");
        auto& p = *project; const auto columns = p.children(sheet);
        for (auto column : selected) if (std::find(columns.begin(), columns.end(), column) == columns.end()) throw std::invalid_argument("Selected column belongs to another worksheet");
        for (auto column : selected) if (std::get<pl::Column>(p.object(column).data).role == pl::Role::X) explicitX = column;
        auto group = undo->ScopedGroup(graph ? "Add Plot to Layer" : "Plot selected columns");
        bool added = false;
        try {
            for (auto column : columns) if (std::find(selected.begin(), selected.end(), column) != selected.end()) {
                const auto& c = std::get<pl::Column>(p.object(column).data);
                if (c.values.index() != 0 || c.role == pl::Role::X || c.role == pl::Role::YError || c.role == pl::Role::XError || c.role == pl::Role::Label || c.role == pl::Role::Disregard) continue;
                const auto x = explicitX ? explicitX : p.pairedX(column);
                pl::Id addedSeries = 0;
                if (!graph) graph = AddGraph(p.root(), sheet, x, column, error);
                else addedSeries = AddSeries(graph, sheet, x, column, error);
                const auto layer = p.children(graph).front();
                auto o = p.object(addedSeries ? addedSeries : p.children(layer).front()); auto& series = std::get<pl::Plot>(o.data);
                series.kind = kind; series.style.color = plot::ScientificColor(p.children(layer).size() - 1);
                series.style.marker = kind == plot::SeriesKind::Line || kind == plot::SeriesKind::Column ? plot::Marker::None : plot::Marker::Circle;
                series.style.markerAuto = false; series.group = "Worksheet selection";
                Execute(p.change("Plot type", {std::move(o)})); added = true;
            }
            if (!added) throw std::invalid_argument("Select at least one numeric Y column first.");
            const auto result = group.Commit(); if (!result) throw std::runtime_error(result.Error().technicalDetails);
            return graph;
        } catch (...) { (void)group.Cancel(); throw; }
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

    void Document::PollDataOperation() {
        if(!dataJob) return;
        const auto status=dataJobs->GetJob(dataJob);
        if(!status || status->status==JobStatus::Queued || status->status==JobStatus::Running) return;
        auto work=std::static_pointer_cast<DataJob>(dataWork);
        try {
            if(status->status!=JobStatus::Completed) throw std::runtime_error(status->errorMessage);
            if(project!=work->base || project->revision()!=work->revision) throw std::runtime_error("Project changed while data job was running; retry operation");
            std::vector<pl::Id> created;
            const auto parent=project->object(work->request.worksheet).parent;
            const auto before=project->children(parent);
            project->acceptOperationSnapshot(*work->snapshot);
            Execute(std::move(*work->operation));
            for(auto id:project->children(parent)) if(std::find(before.begin(),before.end(),id)==before.end()) created.push_back(id);
            dataResult={DataState::Completed,dataJob,work->elapsedMs,std::move(created)};
        } catch(const std::exception& e) {dataResult={};dataResult.error=status->status==JobStatus::Completed?WorksheetError::Conflict:WorksheetError::Validation;dataResult.message=e.what();}
        work->operation.reset();work->snapshot.reset();work->base.reset();work->request={};
        dataJob=0;dataWork.reset();
    }
    void Document::SetColumnValues(pl::Id column, pl::Formula definition) {
        WorksheetCommand(pl::SetColumnValues{column, std::move(definition)});
    }
    DataResult Document::DataStatus() {
        PollDataOperation();return dataJob?DataResult{DataState::Running,dataJob}:dataResult;
    }
    DataResult Document::WorksheetCommand(pl::DataRequest request,bool wait) {
        PollDataOperation();if(dataJob)throw std::invalid_argument("A data operation is running");
        if(!dataJobs){dataJobs=CreateRef<JobSystem>(bus,1);ownsDataJobs=true;}
        auto work=CreateRef<DataJob>();work->snapshot=project->operationSnapshot();work->base=project;work->revision=project->revision();work->request=std::move(request);
        dataWork=work;dataResult={};dataJob=dataJobs->Submit(work);
        if(!dataJob){dataWork.reset();throw std::runtime_error("Could not submit data job");}
        if(!wait)return {DataState::Running,dataJob};
        while(dataJob){PollDataOperation();if(dataJob)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return dataResult;
    }
    void Document::WorksheetCommand(pl::WorksheetEdit request) {
        if(auto* values=std::get_if<pl::SetColumnValues>(&request)) {
            auto& f=values->formula;f.variables.clear();f.scriptInputs.clear();
            if(!f.beforeScript.empty()) {
#if DS_PYTHON_CAPI_AVAILABLE
                if(!PythonInterpreter::IsRuntimeInitialized())throw std::invalid_argument("Embedded Python is unavailable");
                nanobind::gil_scoped_acquire gil;
                auto validator=nanobind::module_::import_("sst_plotlab.formula_script");
                f.variables=nanobind::cast<std::vector<std::string>>(validator.attr("declared")(f.beforeScript));
                f.scriptInputs=nanobind::cast<std::vector<pl::Id>>(validator.attr("inputs")(f.beforeScript));
#else
                throw std::invalid_argument("Embedded Python is unavailable");
#endif
            }
        }
        Execute(project->worksheetEdit(request));
    }
    namespace PythonBinding {
    nlohmann::json DataResponse(const DataResult& r) {
        using Json=nlohmann::json;
        if(r.error)return Json{{"ok",false},{"error",{{"type",*r.error==WorksheetError::Conflict?"conflict":"validation"},{"message",r.message}}}};
        const char* states[]={"idle","running","completed"};Json result={{"state",states[static_cast<unsigned>(r.state)]}};
        if(r.state!=DataState::Idle)result["job_id"]=r.job;
        if(r.state==DataState::Completed){result["elapsed_ms"]=r.elapsedMs;result["worksheet_ids"]=r.worksheets;}
        return Json{{"ok",true},{"result",std::move(result)}};
    }
    std::string WorksheetCommandJson(Document& doc,const std::string& requestJson)
    {
        using Json = nlohmann::json;
        auto& project=doc.project;auto& undo=doc.undo;
        try {
            auto request = Json::parse(requestJson);
            const auto action = request.at("action").get<std::string>();
            for(const auto* key : {"column_id","worksheet_id","parameter_id"}) if(request.contains(key)) {
                const auto& id=request.at(key);
                if(!(id.is_number_unsigned() ? id.get<std::uint64_t>()>0 : id.is_number_integer() && id.get<std::int64_t>()>0))
                    throw std::invalid_argument("Expected positive integer ID");
            }
            Json result = Json::object();
            if(action=="data_status")return DataResponse(doc.DataStatus()).dump();
            if(std::find(std::begin(pl::dataActionNames),std::end(pl::dataActionNames),action)!=std::end(pl::dataActionNames))
                return DataResponse(doc.WorksheetCommand(pl::edges::dataRequest(request),request.value("wait",true))).dump();
            if(action=="analysis_column") {
                const auto id=request.at("column_id").get<pl::Id>();
                result["values_column_units"]=project->analysisValues(id,request.value("exclude_filtered",false));
                const auto& o=project->object(id);result["units"]=std::get<pl::Column>(o.data).units;
                result["row_ids"]=std::get<pl::Worksheet>(project->object(o.parent).data).rows;
            } else if (action == "snapshot") result = Json::parse(project->toJson());
            else if (action == "load_project") doc.Open(CreateRef<pl::Project>(pl::Project::fromJson(request.at("project").dump())), {});
            else if (action == "undo" || action == "redo") {
                auto changed = action == "undo" ? undo->Undo() : undo->Redo();
                if (!changed) throw std::runtime_error(changed.Error().technicalDetails);
            } else if (action == "clear_cells") {
                const auto ids = [&](const char* key) {
                    std::vector<pl::Id> values;
                    if (!request.contains(key) && std::string_view(key) == "row_ids") return values;
                    const auto& input = request.at(key); if (!input.is_array()) throw std::invalid_argument("Expected ID array");
                    for (const auto& value : input) values.push_back(pl::edges::id(value));
                    return values;
                };
                doc.ClearCells(pl::edges::id(request.at("worksheet_id")), ids("column_ids"), ids("row_ids"));
            } else if (action == "paired_x") result["column_id"] = project->pairedX(request.at("column_id").get<pl::Id>());
            else if (action == "render_cell" || action == "copy_cell") {
                const auto& c = std::get<pl::Column>(project->object(request.at("column_id").get<pl::Id>()).data);
                const auto& index = request.at("row_index");
                if (!index.is_number_unsigned() && !(index.is_number_integer() && index.get<std::int64_t>() >= 0)) throw std::invalid_argument("Expected nonnegative row_index");
                const auto row = index.get<std::size_t>();
                if (const auto* values = std::get_if<std::vector<double>>(&c.values))
                    result["text"] = action == "copy_cell" ? pl::fullPrecision(values->at(row)) : pl::formatValue(values->at(row), c.display);
                else result["text"] = std::get<std::vector<std::string>>(c.values).at(row);
            } else if(action=="set_column_values") {
                doc.WorksheetCommand(pl::edges::worksheetEdit(request));
            } else if(action=="recompute") {
#if DS_PYTHON_CAPI_AVAILABLE
                std::optional<nanobind::gil_scoped_release> release;
                if(PythonInterpreter::IsRuntimeInitialized() && PyGILState_Check()) release.emplace();
#endif
                auto job=project->recompute();
                if(request.value("wait",true)) job.wait();
            } else if(action=="recompute_status") {
                const auto& info=project->recomputeInfo(request.at("column_id").get<pl::Id>());
                const char* states[]={"Clean","Dirty","Running","Failed","Blocked"};
                result={{"state",states[static_cast<int>(info.state)]},{"stale",info.stale},{"diagnostic",info.diagnostic},{"runs",info.runs}};
            } else doc.WorksheetCommand(pl::edges::worksheetEdit(request));
            return Json{{"ok",true},{"result",result}}.dump();
        } catch (const std::out_of_range& e) {
            return Json{{"ok",false},{"error",{{"type","not_found"},{"message",e.what()}}}}.dump();
        } catch (const std::exception& e) {
            std::string message=e.what(), type="validation";
            if(message.find("Missing column")!=std::string::npos) type="missing_column";
            else if(message.find("Formula dependency cycle")!=std::string::npos) type="cycle";
            else if(message.find("Domain error at row")!=std::string::npos) type="domain";
            else if(message.find("Recompute column")!=std::string::npos) type="recompute";
            return Json{{"ok",false},{"error",{{"type",type},{"message",message}}}}.dump();
        }
    }
    } // namespace PythonBinding
    void InstallWorksheetPython(Document* active, const std::filesystem::path& pythonRoot)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        namespace nb = nanobind;
        if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
        nb::gil_scoped_acquire gil;
        auto sys=nb::module_::import_("sys");
        auto paths=sys.attr("path");
        const auto raw=(pythonRoot.empty()?std::filesystem::path(SST_PLOTLAB_PYTHON_ROOT):pythonRoot).u8string();
        const auto path=nb::str(reinterpret_cast<const char*>(raw.c_str()));
        if(!nb::cast<bool>(paths.attr("__contains__")(path))) paths.attr("append")(path);
        auto module = nb::module_::import_("_sst_plotlab");
        if (active) module.attr("active_document") = nb::cast(active, nb::rv_policy::reference);
#else
        (void)active; (void)pythonRoot;
        throw std::runtime_error("Rebuild with embedded Python");
#endif
    }
} // namespace SST::Prototypes::PlotLab

#if DS_PYTHON_CAPI_AVAILABLE
NB_MODULE(_sst_plotlab, module)
{
    using namespace SST;
    using namespace SST::Prototypes::PlotLab;
    SST::PlotEditingPython::Bind<Document>(module);
    nanobind::class_<Document>(module, "WorksheetSession")
        .def("__init__", [](Document* self) {
            new (self) Document(CreateRef<CommandRegistry>(), CreateRef<UndoStack>(), CreateRef<EventBus>());
        })
        .def("command_json", [](Document& doc,const std::string& request){return PythonBinding::WorksheetCommandJson(doc,request);})
        .def("plot_columns", [](Document& doc, pl::Id sheet, const std::vector<pl::Id>& columns, int kind, pl::Id graph, pl::Id x, pl::Id error) {
            try {
                const auto result = doc.PlotColumns(sheet, columns, static_cast<plot::SeriesKind>(kind), graph, x, error);
                return SST::PlotEditingPython::Response(nlohmann::json{{"ok",true},{"graph_id",result},{"error",nullptr}});
            } catch (const std::exception& e) {
                return SST::PlotEditingPython::Response(nlohmann::json{{"ok",false},{"error",{{"code","plot.columns"},{"message",e.what()}}}});
            }
        }, nanobind::arg("worksheet_id"), nanobind::arg("column_ids"), nanobind::arg("kind") = 2,
           nanobind::arg("graph_id") = 0, nanobind::arg("x_column_id") = 0, nanobind::arg("y_error_column_id") = 0)
        .def("plot_edit", [](Document& doc, pl::Id layer, nanobind::dict request) {
            auto edit=SST::PlotEditingPython::Decode(SST::PlotEditingPython::Request(request),*doc.project,layer);
            auto result=SST::ExecutePlotEdit(*doc.commands,doc.project,layer,edit);
            return SST::PlotEditingPython::Response(result ? nlohmann::json{{"ok",true},{"error",nullptr}} : nlohmann::json{{"ok",false},{"error",{{"code",result.Error().code},{"message",result.Error().technicalDetails}}}});
        });
}
namespace {
    const bool worksheetModuleRegistered = [] {
        if(PyImport_AppendInittab("_sst_plotlab", PyInit__sst_plotlab)!=0) std::terminate();
        return true;
    }();
}
#endif
