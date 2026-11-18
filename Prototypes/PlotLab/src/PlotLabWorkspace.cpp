#include "PlotLabWorkspace.hpp"
#include "Core/App/Application.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <imgui.h>
#include <limits>
#include <misc/cpp/imgui_stdlib.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Theme/ScientificPlotPreset.hpp>

namespace SST::Prototypes::PlotLab
{
    namespace
    {
        class ExportJob final : public IJob
        {
          public:
            std::shared_ptr<const pl::GraphSnapshot> snapshot;
            std::filesystem::path destination, pythonRoot;
            std::string format;
            int dpi = 300;
            std::string GetName() const override
            {
                return "Export PlotLab graph";
            }
            std::string GetType() const override
            {
                return "plotlab.export";
            }
            void Execute(JobContext &context) override
            {
                context.SetProgress(0, 1);
                context.SetStage("Rendering publication snapshot");
                context.ThrowIfCancellationRequested();
                ExportGraph(*snapshot, destination, format, dpi, pythonRoot);
                context.SetProgress(1, 1); // Replacement has committed; a late cancel cannot undo it.
            }
        };
        std::string Title(const char *prefix, const pl::Object &object, pl::Id project)
        {
            return std::string(prefix) + object.name + "###" + prefix + std::to_string(project) + ":" +
                   std::to_string(object.id);
        }
        bool Exists(const pl::Project &p, pl::Id id, std::size_t type)
        {
            auto it = p.objects().find(id);
            return it != p.objects().end() && it->second->data.index() == type;
        }
        bool ColumnPicker(const char *title, const pl::Project &p, pl::Id sheet, pl::Id &selected,
                          bool optional = false)
        {
            bool changed = false;
            const auto label =
                Exists(p, selected, 3) ? p.object(selected).name + " [" + std::to_string(selected) + "]" : "None";
            if (ImGui::BeginCombo(title, label.c_str()))
            {
                if (optional && ImGui::Selectable("None", !selected))
                {
                    selected = 0;
                    changed = true;
                }
                if (Exists(p, sheet, 2))
                    for (auto id : p.children(sheet))
                    {
                        const auto &c = std::get<pl::Column>(p.object(id).data);
                        if (c.values.index() != 0)
                            continue;
                        auto name = p.object(id).name + " [" + std::to_string(id) + "]";
                        if (ImGui::Selectable(name.c_str(), selected == id))
                        {
                            selected = id;
                            changed = true;
                        }
                    }
                ImGui::EndCombo();
            }
            return changed;
        }
    } // namespace
    bool Session::Save(bool saveAs)
    {
        auto destination = document->path;
        if (saveAs || destination.empty())
        {
            auto result = SaveFileDialog({"PlotLab project", "plotlab"}, "project.plotlab");
            if (!result)
                return false;
            destination = result->Native();
        }
        document->Save(destination);
        status = "Saved.";
        return true;
    }
    void Session::Show(pl::Id id)
    {
        const auto &o = document->project->object(id);
        if (std::holds_alternative<pl::Worksheet>(o.data))
            sheets.insert(id);
        if (std::holds_alternative<pl::Graph>(o.data))
            graphs.insert(id);
        selected = id;
    }
    void Session::RefreshWindows()
    {
        sheets.clear();
        graphs.clear();
        views.clear();
        selected = document->project->root();
        for (const auto &[id, o] : document->project->objects())
            if (o->data.index() == 2 || o->data.index() == 4)
                Show(id);
    }
    void Session::File(FileAction action, Path path)
    {
        if (openJob) throw std::runtime_error("A file is already opening.");
        pendingPath = std::move(path);
        pending = action;
        if (!document->Dirty())
            CompleteFile();
    }
    void Session::CompleteFile()
    {
        const auto action = pending;
        if (action == FileAction::Open)
        {
            auto source = pendingPath.Empty() ? OpenFileDialog({"PlotLab project / data", "plotlab,csv,txt,dat"}) : std::optional<Path>{pendingPath};
            if (!source)
            {
                pending = FileAction::None;
                return;
            }
            openingBase = document->project;
            openingRevision = document->project->revision();
            opening = CreateRef<OpenJob>(*source);
            openJob = jobs->Submit(opening);
            if (!openJob) { opening.reset(); throw std::runtime_error("Could not submit open job."); }
            status = "Opening " + source->Utf8();
        }
        else if (action == FileAction::New)
        {
            document->New();
            RefreshWindows();
        }
        else if (action == FileAction::Close)
        {
            allowClose = true;
            Application::EmitEvent(WindowCloseEvent{});
        }
        pending = FileAction::None;
    }
    void WorkspaceLayer::OnUpdate(float)
    {
        state->document->project->pollRecompute();
        if (state->openJob) {
            const auto job = state->jobs->GetJob(state->openJob);
            if (job && job->status != JobStatus::Queued && job->status != JobStatus::Running) {
                try {
                    if (job->status != JobStatus::Completed) throw std::runtime_error(job->errorMessage);
                    if (state->opening->project) {
                        if (state->document->project != state->openingBase || state->document->project->revision() != state->openingRevision)
                            throw std::runtime_error("Project changed while opening; save edits and retry.");
                        state->document->Open(state->opening->project, state->opening->path.Native());
                    } else {
                        auto &doc = *state->document;
                        doc.Execute(pl::importTable(*doc.project, doc.project->root(),
                            Path(state->opening->path.Native().stem()).Utf8(), *state->opening->table));
                    }
                    state->RefreshWindows(); state->status = "Ready.";
                } catch (const std::exception &e) { state->status = e.what(); }
                state->opening.reset(); state->openingBase.reset(); state->openJob = 0;
            }
        }
        if (state->exporting)
        {
            auto job = state->jobs->GetJob(state->exporting);
            if (job && job->status != JobStatus::Queued && job->status != JobStatus::Running)
            {
                state->status = job->status == JobStatus::Completed ? "Export saved." : job->errorMessage;
                state->exporting = 0;
            }
        }
        if (smoke)
        {
            ++frames;
            if (frames == 3)
            {
                if (state->views.size() != 3 || std::any_of(state->views.begin(), state->views.end(),
                    [](const auto &entry) { return !entry.second.viewport; }))
                    throw std::runtime_error("Smoke: graph font preview did not render");
                auto &doc = *state->document;
                for (const auto &[id, o] : doc.project->objects())
                    if (o->data.index() == 3)
                    {
                        doc.Execute(doc.project->setCells(id, 0, std::vector<double>{1.5}));
                        break;
                    }
                Application::EmitEvent(WindowCloseEvent{});
            }
            if (frames == 4)
            {
                if (state->pending != FileAction::Close)
                    throw std::runtime_error("Smoke: unsaved close guard did not defer closing");
                state->pending = FileAction::None;
                auto result = state->document->undo->Undo();
                if (!result || state->document->Dirty())
                    throw std::runtime_error("Smoke: undo did not restore clean state");
            }
            if (frames == 8)
                Application::EmitEvent(WindowCloseEvent{});
        }
    }
    void WorkspacePanel::Tree(pl::Id id)
    {
        auto &p = *state->document->project;
        const auto &o = p.object(id);
        const auto type = o.data.index();
        if (type == 3 || type == 5 || type == 6)
            return;
        const char *tags[] = {"Folder", "Workbook", "Worksheet", "Column", "Graph"};
        const auto label = std::string(tags[type]) + ": " + o.name + "##" + std::to_string(id);
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (state->selected == id)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (type == 2 || type == 4)
            flags |= ImGuiTreeNodeFlags_Leaf;
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked())
            state->Show(id);
        if (open)
        {
            for (auto child : p.children(id))
                Tree(child);
            ImGui::TreePop();
        }
    }
    void WorkspacePanel::BeginImport()
    {
        auto source = OpenFileDialog({"Delimited data", "csv,txt,dat"});
        if (!source)
            return;
        std::string error;
        if (!TextFileIO::Load(*source, importText, error))
            throw std::runtime_error(error);
        importPath = source->Utf8();
        importName = Path::FromResolved(source->Native().stem()).Utf8();
        importOptions = {};
        importDialog = true;
        PreviewImport();
    }
    void WorkspacePanel::PreviewImport()
    {
        preview.reset();
        try
        {
            preview = pl::CsvImporter{}.preview(importText, importOptions);
        }
        catch (const std::exception &e)
        {
            state->status = e.what();
        }
    }
    void WorkspacePanel::Render()
    {
        auto &p = *state->document->project;
        if (!p.objects().contains(state->selected))
            state->selected = p.root();
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 10, ImGui::GetMainViewport()->WorkPos.y + 10},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({330, 640}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(GetTitle().c_str()))
        {
            ImGui::TextUnformatted(state->document->Dirty() ? "Unsaved changes" : "Saved / new project");
            ImGui::TextWrapped("%s",
                               state->document->path.empty() ? "Untitled" : Path(state->document->path).Utf8().c_str());
            if (ImGui::Button("New"))
                Attempt([&] { state->File(FileAction::New); });
            ImGui::SameLine();
            if (ImGui::Button("Open"))
                Attempt([&] { state->File(FileAction::Open); });
            ImGui::SameLine();
            if (ImGui::Button("Save"))
                Attempt([&] { state->Save(); });
            ImGui::SameLine();
            if (ImGui::Button("Save as"))
                Attempt([&] { state->Save(true); });
            if (ImGui::Button("Import CSV / TXT"))
                Attempt([&] { BeginImport(); });
            if (ImGui::Button("Undo (Ctrl+Z)"))
                Attempt(
                    [&]
                    {
                        auto r = state->document->undo->Undo();
                        if (!r)
                            throw std::runtime_error(r.Error().technicalDetails);
                    });
            ImGui::SameLine();
            if (ImGui::Button("Redo (Ctrl+Y)"))
                Attempt(
                    [&]
                    {
                        auto r = state->document->undo->Redo();
                        if (!r)
                            throw std::runtime_error(r.Error().technicalDetails);
                    });
            ImGui::Separator();
            Tree(p.root());
            ImGui::Separator();
            const auto type = p.object(state->selected).data.index();
            if (type == 0)
            {
                if (ImGui::Button("Add folder"))
                    Attempt([&] { state->selected = state->document->Create(state->selected, 0, "Folder"); });
                ImGui::SameLine();
                if (ImGui::Button("Add workbook"))
                    Attempt([&] { state->selected = state->document->Create(state->selected, 1, "Workbook"); });
            }
            if (type == 1 && ImGui::Button("Add worksheet"))
                Attempt([&] { state->Show(state->document->Create(state->selected, 2, "Sheet")); });
            if (ImGui::Button("Rename"))
            {
                rename = state->selected;
                name = p.object(rename).name;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(state->selected == p.root());
            if (ImGui::Button("Delete..."))
                remove = state->selected;
            ImGui::EndDisabled();
            if (ImGui::Button("Create graph from columns..."))
            {
                targetGraph = 0;
                graphDialog = true;
                sheetChoice = type == 2 ? state->selected : 0;
                xChoice = yChoice = errorChoice = 0;
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            if (state->exporting)
                ImGui::TextUnformatted("Export in progress (see Core Progress Monitor)");
        }
        ImGui::End();
        for (auto it = state->sheets.begin(); it != state->sheets.end();)
        {
            const auto id = *it++;
            if (Exists(p, id, 2))
                Worksheet(id);
        }
        for (auto it = state->graphs.begin(); it != state->graphs.end();)
        {
            const auto id = *it++;
            if (Exists(p, id, 4))
                Graph(id);
        }
        Dialogs();
    }
    void WorkspacePanel::Worksheet(pl::Id id)
    {
        auto &doc = *state->document;
        auto &p = *doc.project;
        bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 10},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({850, 300}, ImGuiCond_FirstUseEver);
        const bool expanded = ImGui::Begin(Title("Worksheet: ", p.object(id), p.id()).c_str(), &open);
        if (!open)
            state->sheets.erase(id);
        if (expanded)
        {
            if (ImGui::Button("Add row"))
                Attempt([&] { doc.ResizeRows(id); });
            ImGui::SameLine();
            if (ImGui::Button("Delete selected rows"))
                Attempt(
                    [&]
                    {
                        auto rows = std::get<pl::Worksheet>(p.object(id).data).rows;
                        auto selection = p.selection();
                        if (selection.worksheet != id || selection.rows.empty())
                            throw std::runtime_error("Select rows first");
                        auto group = doc.undo->ScopedGroup("Delete rows");
                        try
                        {
                            for (std::size_t r = rows.size(); r-- > 0;)
                                if (std::find(selection.rows.begin(), selection.rows.end(), rows[r]) !=
                                    selection.rows.end())
                                    doc.ResizeRows(id, r);
                            auto result = group.Commit();
                            if (!result)
                                throw std::runtime_error(result.Error().technicalDetails);
                        }
                        catch (...)
                        {
                            (void)group.Cancel();
                            throw;
                        }
                    });
            if (ImGui::Button("Add column"))
                Attempt([&] { doc.AddColumn(id, textColumn); });
            ImGui::SameLine();
            ImGui::Checkbox("Text", &textColumn);
            ImGui::SameLine();
            if (ImGui::Button("Recompute"))
                (void)p.recompute();
            ImGui::TextUnformatted(
                "Click a column header for metadata/formula. Enter commits cells. Ctrl+click row toggles selection.");
            auto columns = p.children(id);
            if (columns.size() > 64)
            {
                auto &page = columnPages[id];
                const int pages = static_cast<int>((columns.size() + 63) / 64);
                page = std::clamp(page, 0, pages - 1);
                ImGui::BeginDisabled(page == 0);
                if (ImGui::SmallButton("Previous columns"))
                    --page;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::Text("Columns page %d / %d", page + 1, pages);
                ImGui::SameLine();
                ImGui::BeginDisabled(page == pages - 1);
                if (ImGui::SmallButton("Next columns"))
                    ++page;
                ImGui::EndDisabled();
                const auto first = std::size_t(page) * 64, last = std::min(first + 64, columns.size());
                columns = std::vector<pl::Id>(columns.begin() + first, columns.begin() + last);
            }
            const auto rows = std::get<pl::Worksheet>(p.object(id).data).rows;
            if (columns.size() > 0 && ImGui::BeginTable("data", static_cast<int>(columns.size() + 1),
                                                        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                                        {0, 0}))
            {
                ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed,
                                        ImGui::CalcTextSize("000000").x);
                for (auto column : columns)
                {
                    const auto &o = p.object(column);
                    const float padding = 2 * ImGui::GetStyle().FramePadding.x;
                    ImGui::TableSetupColumn(o.name.c_str(), ImGuiTableColumnFlags_WidthFixed,
                        std::max(ImGui::CalcTextSize(o.name.c_str()).x,
                                 ImGui::CalcTextSize("-1.2345678901234567e-308").x) + padding);
                }
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Row");
                for (std::size_t c = 0; c < columns.size(); ++c)
                {
                    ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
                    ImGui::PushID(static_cast<int>(c));
                    const auto &o = p.object(columns[c]);
                    const auto &col = std::get<pl::Column>(o.data);
                    if (ImGui::SmallButton(o.name.c_str()))
                    {
                        metadata = o.id;
                        name = o.name;
                        units = col.units;
                        longName = col.longName;
                        role = static_cast<int>(col.role);
                        formula = col.formula ? col.formula->expression : "";
                    }
                    if (col.formula)
                    {
                        const auto &info = p.recomputeInfo(o.id);
                        const char *names[] = {"clean", "dirty", "running", "failed", "blocked"};
                        ImGui::Text("%s%s", names[static_cast<int>(info.state)], info.stale ? " (stale)" : "");
                        if (!info.diagnostic.empty() && ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", info.diagnostic.c_str());
                    }
                    ImGui::PopID();
                }
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rows.size()));
                while (clipper.Step())
                    for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(r);
                        auto selection = p.selection();
                        bool selected =
                            selection.worksheet == id &&
                            std::find(selection.rows.begin(), selection.rows.end(), rows[r]) != selection.rows.end();
                        if (selected)
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(45, 100, 160, 100));
                        if (ImGui::Selectable(std::to_string(r + 1).c_str(), selected))
                            Attempt([&] { doc.SelectRow(id, r, 1, ImGui::GetIO().KeyCtrl); });
                        for (std::size_t c = 0; c < columns.size(); ++c)
                        {
                            ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
                            ImGui::PushID(static_cast<int>(c));
                            const auto &col = std::get<pl::Column>(p.object(columns[c]).data);
                            ImGui::SetNextItemWidth(-1);
                            if (col.values.index() == 0)
                            {
                                auto value = std::get<std::vector<double>>(col.values)[r];
                                if (col.formula)
                                    ImGui::Text("%.8g", value);
                                else
                                {
                                    char buffer[64];
                                    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
                                    std::string text = error == std::errc{} ? std::string(buffer, end) : "nan";
                                    if (ImGui::InputText("##cell", &text, ImGuiInputTextFlags_EnterReturnsTrue))
                                        Attempt(
                                            [&]
                                            {
                                                doc.Execute(p.setCells(columns[c], r,
                                                                       std::vector<double>{ParseNumericCell(text)}));
                                            });
                                }
                            }
                            else
                            {
                                auto value = std::get<std::vector<std::string>>(col.values)[r];
                                if (ImGui::InputText("##cell", &value, ImGuiInputTextFlags_EnterReturnsTrue))
                                    Attempt(
                                        [&]
                                        { doc.Execute(p.setCells(columns[c], r, std::vector<std::string>{value})); });
                            }
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    void WorkspacePanel::Graph(pl::Id id)
    {
        auto &doc = *state->document;
        auto &p = *doc.project;
        bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 320},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({850, 330}, ImGuiCond_FirstUseEver);
        const bool expanded = ImGui::Begin(Title("Graph: ", p.object(id), p.id()).c_str(), &open);
        if (!open)
            state->graphs.erase(id);
        if (expanded)
        {
            auto graphObject = p.object(id);
            auto &fontSettings = std::get<pl::Graph>(graphObject.data);
            bool fontChanged = false;
            if (ImGui::BeginCombo("Font family", fontSettings.fontFamily.c_str()))
            {
                for (const char *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"})
                    if (ImGui::Selectable(family, fontSettings.fontFamily == family && fontSettings.fontFile.empty()))
                    {
                        fontSettings.fontFamily = family;
                        fontSettings.fontFile.clear();
                        fontChanged = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load font (.ttf/.otf)..."))
                Attempt([&] {
                    if (auto file = OpenFileDialog({"Font", "ttf,otf"})) {
                        auto resolved = ResolveFont("", file->Utf8(), state->pythonRoot);
                        fontSettings.fontFile = resolved.first;
                        fontSettings.fontFamily = resolved.second;
                        fontChanged = true;
                    }
                });
            fontChanged |= ImGui::InputDouble("Graph font (pt)", &fontSettings.fontSizePt, 1, 2, "%.1f");
            if (fontChanged)
                Attempt([&] { doc.Execute(p.change("Graph font", {graphObject})); });
            const auto &chosen = std::get<pl::Graph>(p.object(id).data);
            ImFont *graphFont = nullptr;
            try {
                const auto key = chosen.fontFamily + "\n" + chosen.fontFile;
                auto &cached = graphFonts[key];
                auto *atlas = ImGui::GetIO().Fonts;
                const auto tag = "PlotLab:" + std::to_string(std::hash<std::string>{}(key));
                if (!cached || !atlas->Fonts.contains(cached) || cached->GetDebugName() != tag) {
                    auto resolved = ResolveFont(chosen.fontFamily, chosen.fontFile, state->pythonRoot);
                    ImFontConfig config;
                    config.Flags |= ImFontFlags_NoLoadError;
                    std::snprintf(config.Name, sizeof(config.Name), "%s", tag.c_str());
                    cached = atlas->AddFontFromFileTTF(resolved.first.c_str(), 0, &config);
                    if (!cached) throw std::runtime_error("Cannot load graph font: " + resolved.first);
                }
                graphFont = cached;
                fontErrors.erase(id);
            } catch (const std::exception &e) { fontErrors[id] = e.what(); }
            if (!graphFont) {
                ImGui::TextWrapped("Graph font unavailable: %s", fontErrors[id].c_str());
                ImGui::End();
                return;
            }
            const auto layer = p.children(id).at(0);
            auto spec = std::get<pl::Layer>(p.object(layer).data).spec;
            if (ImGui::Button("Fit"))
                state->views[id] = {};
            ImGui::SameLine();
            if (ImGui::Button("Export..."))
            {
                exportGraph = id;
                const auto &g = std::get<pl::Graph>(p.object(id).data);
                width = g.widthMm;
                height = g.heightMm;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add series..."))
            {
                targetGraph = id;
                graphDialog = true;
                sheetChoice = xChoice = yChoice = errorChoice = 0;
            }
            bool logX = spec.x.scale == plot::AxisScale::Log10, logY = spec.y.scale == plot::AxisScale::Log10;
            bool changed = ImGui::Checkbox("Log X", &logX);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Log Y", &logY);
            if (changed)
                Attempt(
                    [&]
                    {
                        spec.x.scale = logX ? plot::AxisScale::Log10 : plot::AxisScale::Linear;
                        spec.y.scale = logY ? plot::AxisScale::Log10 : plot::AxisScale::Linear;
                        auto o = p.object(layer);
                        std::get<pl::Layer>(o.data).spec = spec;
                        doc.Execute(p.change("Axis scale", {o}));
                        state->views[id] = {};
                    });
            ImGui::TextUnformatted(
                "Ctrl+click a point selects its worksheet row. Pan/zoom and legend are preserved in export.");
            auto plots = p.children(layer);
            bool stale = false;
            for (auto series : plots)
            {
                const auto &source = std::get<pl::Plot>(p.object(series).data);
                for (auto column : {source.x, source.y, source.yError})
                    if (column)
                    {
                        const auto &info = p.recomputeInfo(column);
                        stale |= info.stale || info.state != pl::RecomputeState::Clean;
                    }
            }
            if (stale)
                ImGui::TextColored({1, .65f, .15f, 1}, "Stale results: waiting for recompute (see worksheet status)");
            std::vector<plot::SeriesEntryView> entries;
            for (std::size_t i = 0; i < plots.size(); ++i)
            {
                auto e = p.series(plots[i]);
                e.id = p.object(plots[i]).name + "###" + std::to_string(plots[i]);
                if (e.style.color[3] < 0)
                    e.style.color = plot::ScientificColor(i);
                if (e.style.lineWidthPt < 0)
                    e.style.lineWidthPt = 1;
                if (e.style.markerSizePt < 0)
                    e.style.markerSizePt = 4;
                if (e.style.errorCapPt < 0)
                    e.style.errorCapPt = 4;
                entries.push_back(e);
                if (e.data.yError && e.kind != plot::SeriesKind::ErrorBars)
                {
                    auto error = e;
                    error.id = "##error" + std::to_string(plots[i]);
                    error.kind = plot::SeriesKind::ErrorBars;
                    error.style.legend = false;
                    entries.push_back(error);
                }
            }
            // Mapping stays by engine plot ID; overlays are preview only and excluded from snapshots.
            std::vector<pl::Id> entryPlots;
            for (auto series : plots)
            {
                entryPlots.push_back(series);
                if (p.series(series).data.yError && p.series(series).kind != plot::SeriesKind::ErrorBars)
                    entryPlots.push_back(series);
            }
            auto &view = state->views[id];
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                const auto engineId = std::to_string(entryPlots[i]);
                if (view.visibility.contains(engineId))
                    view.visibility[entries[i].id] = view.visibility.at(engineId);
            }
            std::vector<std::vector<double>> highlightX(plots.size()), highlightY(plots.size());
            for (std::size_t i = 0; i < plots.size(); ++i)
            {
                auto e = p.series(plots[i]);
                for (auto point : p.selectedPoints(plots[i]))
                {
                    highlightX[i].push_back(e.data.x[point]);
                    highlightY[i].push_back(e.data.y[point]);
                }
                if (!highlightX[i].empty())
                {
                    plot::SeriesEntryView h{"##selected" + std::to_string(plots[i]),
                                            {highlightX[i].data(), highlightY[i].data(), highlightX[i].size()}};
                    h.kind = plot::SeriesKind::Scatter;
                    h.style.color = {1, 0.6f, 0, 1};
                    h.style.marker = plot::Marker::Circle;
                    h.style.markerSizePt = 6;
                    h.style.legend = false;
                    h.visible = e.visible && (!view.visibility.contains(std::to_string(plots[i])) ||
                                              view.visibility.at(std::to_string(plots[i])));
                    entries.push_back(h);
                }
            }
            if (!view.fitted)
                spec.autoFit = plot::AutoFit::Once;
            // pt to logical pixels at 96 DPI; ImGui applies the display DPI scale.
            ImGui::PushFont(graphFont, static_cast<float>(chosen.fontSizePt * 96.0 / 72.0));
            plot::RenderPlot("##graph", spec, entries.data(), entries.size(), view, {}, ImVec2(-1, -1));
            ImGui::PopFont();
            for (std::size_t i = 0; i < entryPlots.size(); ++i)
                if (entries[i].kind != plot::SeriesKind::ErrorBars || entries[i].style.legend)
                    if (view.visibility.contains(entries[i].id))
                        view.visibility[std::to_string(entryPlots[i])] = view.visibility[entries[i].id];
            if (view.picked && view.picked->series < entryPlots.size())
                Attempt(
                    [&]
                    {
                        const auto series = entryPlots[view.picked->series];
                        const auto &source = std::get<pl::Plot>(p.object(series).data);
                        doc.SelectRow(source.worksheet, view.picked->point, 2, ImGui::GetIO().KeyShift, series);
                        state->sheets.insert(source.worksheet);
                    });
        }
        ImGui::End();
    }
    void WorkspacePanel::Dialogs()
    {
        auto &doc = *state->document;
        auto &p = *doc.project;
        if (state->pending != FileAction::None)
            ImGui::OpenPopup("Unsaved changes");
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save project changes before continuing?");
            if (ImGui::Button("Save and continue"))
                Attempt(
                    [&]
                    {
                        if (state->Save())
                        {
                            state->CompleteFile();
                            ImGui::CloseCurrentPopup();
                        }
                    });
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
                Attempt(
                    [&]
                    {
                        state->CompleteFile();
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                state->pending = FileAction::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (rename)
            ImGui::OpenPopup("Rename object");
        if (ImGui::BeginPopupModal("Rename object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", &name);
            if (ImGui::Button("Apply"))
                Attempt(
                    [&]
                    {
                        doc.Rename(rename, name);
                        rename = 0;
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                rename = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (remove)
            ImGui::OpenPopup("Confirm deletion");
        if (ImGui::BeginPopupModal("Confirm deletion", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Delete these objects and dependents? Undo restores them.");
            if (p.objects().contains(remove))
                for (auto id : doc.RemovalIds(remove))
                    ImGui::BulletText("%s [ID %llu]", p.object(id).name.c_str(), static_cast<unsigned long long>(id));
            if (ImGui::Button("Delete all listed"))
                Attempt(
                    [&]
                    {
                        doc.Delete(remove);
                        remove = 0;
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                remove = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (metadata)
            ImGui::OpenPopup("Column definition");
        if (ImGui::BeginPopupModal("Column definition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Short name", &name);
            ImGui::InputText("Long name", &longName);
            ImGui::InputText("Units", &units);
            ImGui::Combo("Role", &role, "None\0X\0Y\0Z\0YError\0Label\0");
            if (ImGui::Button("Apply metadata"))
                Attempt(
                    [&]
                    {
                        auto o = p.object(metadata);
                        o.name = name;
                        auto &c = std::get<pl::Column>(o.data);
                        c.units = units;
                        c.longName = longName;
                        c.role = static_cast<pl::Role>(role);
                        doc.Execute(p.change("Column metadata", {o}));
                    });
            ImGui::InputText("Formula", &formula);
            ImGui::TextUnformatted(
                "Engine grammar: + - * / ^, abs/sqrt/exp/log/log10/sin/cos, pow, min/max/mean.\nReferences: Column, "
                "Sheet.Column, Workbook.Sheet.Column, [qualified name].");
            if (ImGui::Button("Set formula"))
                Attempt([&] { doc.Execute(p.setFormula(metadata, formula)); });
            ImGui::SameLine();
            if (ImGui::Button("Detach formula"))
                Attempt([&] { doc.Execute(p.detach(metadata)); });
            if (ImGui::Button("Delete column..."))
            {
                remove = metadata;
                metadata = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                metadata = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (graphDialog)
            ImGui::OpenPopup("Worksheet columns to graph");
        if (ImGui::BeginPopupModal("Worksheet columns to graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto label = Exists(p, sheetChoice, 2) ? p.object(sheetChoice).name : "Choose worksheet";
            if (ImGui::BeginCombo("Worksheet", label.c_str()))
            {
                for (const auto &[id, o] : p.objects())
                    if (o->data.index() == 2)
                    {
                        const auto text = p.object(o->parent).name + "." + o->name + " [" + std::to_string(id) + "]";
                        if (ImGui::Selectable(text.c_str(), sheetChoice == id))
                        {
                            sheetChoice = id;
                            xChoice = yChoice = errorChoice = 0;
                        }
                    }
                ImGui::EndCombo();
            }
            ColumnPicker("X", p, sheetChoice, xChoice);
            ColumnPicker("Y", p, sheetChoice, yChoice);
            ColumnPicker("yErr", p, sheetChoice, errorChoice, true);
            if (ImGui::Button("Create / add"))
                Attempt(
                    [&]
                    {
                        if (targetGraph)
                            doc.AddSeries(targetGraph, sheetChoice, xChoice, yChoice, errorChoice);
                        else
                            state->Show(doc.AddGraph(p.root(), sheetChoice, xChoice, yChoice, errorChoice));
                        graphDialog = false;
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                graphDialog = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (importDialog)
            ImGui::OpenPopup("Import preview");
        if (ImGui::BeginPopupModal("Import preview", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", importPath.c_str());
            const char delimiters[] = {0, ',', ';', '\t', ' ', '|'}, decimals[] = {0, '.', ','};
            int delimiter = 0, decimal = 0, header = static_cast<int>(importOptions.header),
                unitRows = static_cast<int>(importOptions.units);
            for (int i = 0; i < 6; ++i)
                if (delimiters[i] == importOptions.delimiter)
                    delimiter = i;
            for (int i = 0; i < 3; ++i)
                if (decimals[i] == importOptions.decimal)
                    decimal = i;
            bool changed = ImGui::Combo("Delimiter", &delimiter, "Detect\0Comma\0Semicolon\0Tab\0Whitespace\0Pipe\0");
            changed |= ImGui::Combo("Decimal", &decimal, "Detect\0Point\0Comma\0");
            changed |= ImGui::Combo("Header", &header, "Detect\0Present\0Absent\0");
            changed |= ImGui::Combo("Units row", &unitRows, "Detect\0Present\0Absent\0");
            if (changed)
            {
                importOptions.delimiter = delimiters[delimiter];
                importOptions.decimal = decimals[decimal];
                importOptions.header = static_cast<pl::RowMode>(header);
                importOptions.units = static_cast<pl::RowMode>(unitRows);
                PreviewImport();
            }
            if (preview)
            {
                for (const auto &[key, value] : preview->source)
                    ImGui::Text("%s: %s", key.c_str(), value == "\t" ? "tab" : value.c_str());
                for (const auto &diagnostic : preview->diagnostics)
                    ImGui::TextWrapped("%s", diagnostic.c_str());
                const auto visibleColumns = std::min<std::size_t>(8, preview->columns.size());
                if (visibleColumns < preview->columns.size())
                    ImGui::Text("Preview: first 8 of %zu columns", preview->columns.size());
                if (ImGui::BeginTable("preview", static_cast<int>(visibleColumns), ImGuiTableFlags_Borders))
                {
                    for (std::size_t c = 0; c < visibleColumns; ++c)
                        ImGui::TableSetupColumn((preview->names[c] + " [" + preview->columns[c].units + "]").c_str());
                    ImGui::TableHeadersRow();
                    auto count = std::min<std::size_t>(
                        5, std::visit([](const auto &v) { return v.size(); }, preview->columns[0].values));
                    for (std::size_t r = 0; r < count; ++r)
                    {
                        ImGui::TableNextRow();
                        for (std::size_t c = 0; c < visibleColumns; ++c)
                        {
                            ImGui::TableSetColumnIndex(static_cast<int>(c));
                            std::visit(
                                [&](const auto &v)
                                {
                                    using T = typename std::decay_t<decltype(v)>::value_type;
                                    if constexpr (std::is_same_v<T, double>)
                                        ImGui::Text("%.8g", v[r]);
                                    else
                                        ImGui::TextUnformatted(v[r].c_str());
                                },
                                preview->columns[c].values);
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::BeginDisabled(!preview.has_value());
            if (ImGui::Button("Import"))
                Attempt(
                    [&]
                    {
                        auto table = pl::CsvImporter{}.read(importText, importOptions);
                        table.source["path"] = importPath;
                        doc.Execute(pl::importTable(p, p.root(), importName, table));
                        state->RefreshWindows();
                        importDialog = false;
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                importDialog = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (exportGraph)
            ImGui::OpenPopup("Export publication figure");
        if (ImGui::BeginPopupModal("Export publication figure", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputDouble("Width (mm)", &width);
            ImGui::InputDouble("Height (mm)", &height);
            ImGui::InputInt("DPI", &dpi);
            ImGui::Combo("Format", &format, "SVG\0PDF\0PNG\0");
            ImGui::TextUnformatted("Exports current ranges, ticks and visible series. Recompute must be clean.\n"
                                   "Uses graph font settings; SVG outlines, PDF embedded TrueType.");
            ImGui::BeginDisabled(state->exporting != 0);
            if (ImGui::Button("Export"))
                Attempt(
                    [&]
                    {
                        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || dpi <= 0)
                            throw std::runtime_error("Size and DPI must be positive");
                        const char *formats[] = {"svg", "pdf", "png"};
                        auto snapshot = std::make_shared<pl::GraphSnapshot>(
                            *pl::snapshotGraph(p, exportGraph, state->views.at(exportGraph)));
                        snapshot->widthMm = width;
                        snapshot->heightMm = height;
                        const auto filename = std::string("figure.") + formats[format];
                        auto destination = SaveFileDialog({"Publication figure", formats[format]}, filename.c_str());
                        if (!destination)
                            return;
                        auto job = CreateRef<ExportJob>();
                        job->snapshot = snapshot;
                        job->destination = destination->Native();
                        job->format = formats[format];
                        job->dpi = dpi;
                        job->pythonRoot = state->pythonRoot;
                        state->exporting = state->jobs->Submit(job);
                        if (!state->exporting)
                            throw std::runtime_error("Export job submission failed");
                        exportGraph = 0;
                        ImGui::CloseCurrentPopup();
                    });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                exportGraph = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
    }
} // namespace SST::Prototypes::PlotLab

