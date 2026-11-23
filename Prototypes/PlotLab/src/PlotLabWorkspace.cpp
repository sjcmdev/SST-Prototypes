#include "PlotLabWorkspace.hpp"
#include "Core/PlotExport/PlotEditing.hpp"
#include "Core/App/Application.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
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
            if (sst::ui::widgets::BeginCombo(title, label.c_str()))
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
                sst::ui::widgets::EndCombo();
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
        state->document->PollDataOperation();
        const auto dataStatus=nlohmann::json::parse(state->document->WorksheetCommand("{\"action\":\"data_status\"}"));
        if(!dataStatus.at("ok").get<bool>()) state->status=dataStatus.at("error").at("message").get<std::string>();
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
    void WorkspacePanel::EditColumn(pl::Id id)
    {
        const auto& o=state->document->project->object(id);
        const auto& col=std::get<pl::Column>(o.data);
        metadata = o.id;
        name = o.name;
        units = col.units;
        comments = col.comments;
        displayMode = static_cast<int>(col.display.mode);
        displayDigits = static_cast<int>(col.display.digits);
        longName = col.longName;
        role = static_cast<int>(col.role);
        formula = col.formula ? col.formula->expression : "";
        rowFrom=col.formula?static_cast<int>(col.formula->rowFrom):1;
        rowTo=col.formula?static_cast<int>(col.formula->rowTo):0;
        beforeScript=col.formula?col.formula->beforeScript:"";
        excludeFiltered=col.formula && col.formula->excludeFiltered;
        recalculate=col.formula?static_cast<int>(col.formula->recalculate):1;
    }
    void WorkspacePanel::Render()
    {
        windows.BeginFrame();
        auto &p = *state->document->project;
        if(previewColumn && Exists(p,previewColumn,3)) {
            EditColumn(previewColumn); previewColumn=0; previewLibrary=true;
        }
        if (!p.objects().contains(state->selected))
            state->selected = p.root();
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 10, ImGui::GetMainViewport()->WorkPos.y + 10},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({330, 640}, ImGuiCond_FirstUseEver);
        bool explorerOpen = IsVisible();
        windows.Render(GetTitle(), explorerOpen, 0, [&] {
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
        });
        SetVisible(explorerOpen);
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
        windows.EndFrame();
    }
    void WorkspacePanel::DataOperations(pl::Id sheet) {
        namespace widgets=sst::ui::widgets;
        if(!ImGui::CollapsingHeader("Sort, filter and data operations",ImGuiTreeNodeFlags_DefaultOpen)) return;
        auto& doc=*state->document;auto& p=*doc.project;auto columns=p.children(sheet);if(columns.empty()) return;
        ImGui::PushID("data_operations");
        static const char* actions[]={"Sort","Filter","Mask","Find and replace","Transpose","Stack","Unstack","Split","Join","Extract","Reduce rows","Normalize","Interpolate","Linked statistics"};
        widgets::Combo("Operation",&dataAction,actions,IM_ARRAYSIZE(actions));
        dataColumn=std::clamp(dataColumn,0,static_cast<int>(columns.size()-1));
        dataSecondColumn=std::clamp(dataSecondColumn,0,static_cast<int>(columns.size()-1));
        if(widgets::BeginCombo("Column",p.object(columns[dataColumn]).name.c_str())) {for(int i=0;i<static_cast<int>(columns.size());++i) {ImGui::PushID(std::to_string(columns[i]).c_str());if(ImGui::Selectable(p.object(columns[i]).name.c_str(),i==dataColumn)) dataColumn=i;ImGui::PopID();}widgets::EndCombo();}
        if(dataAction==0) {
            widgets::Checkbox("Descending",&descending); widgets::Checkbox("Column only",&columnOnly);
            if(columnOnly) {ImGui::TextWrapped("Warning: sorting one column breaks row correspondence.");widgets::Checkbox("Confirm breaking row correspondence",&confirmBreak);}
            if(widgets::BeginCombo("Next sort key",p.object(columns[dataSecondColumn]).name.c_str())) {for(int i=0;i<static_cast<int>(columns.size());++i) {ImGui::PushID(std::to_string(columns[i]).c_str());if(ImGui::Selectable(p.object(columns[i]).name.c_str(),i==dataSecondColumn)) dataSecondColumn=i;ImGui::PopID();}widgets::EndCombo();}
            if(ImGui::SmallButton("Add nested key")) nestedKeys.emplace_back(columns[dataSecondColumn],descending);
            ImGui::SameLine();if(ImGui::SmallButton("Clear nested keys")) nestedKeys.clear();
            for(const auto& [key,desc]:nestedKeys) if(p.objects().contains(key)) ImGui::Text("Then %s (%s)",p.object(key).name.c_str(),desc?"descending":"ascending");
        }
        if(dataAction==1 || dataAction==9) {
            widgets::Combo("Condition",&filterKind,"Range\0Equality\0Contains\0Top-N\0");
            if(filterKind==0) {widgets::Double("Minimum (column units)",&filterMinimum);widgets::Double("Maximum (column units)",&filterMaximum);}
            else if(filterKind==3) widgets::Int("Top-N rows",&topN,1,1,1000000);
            else if(filterKind==1 && std::get<pl::Column>(p.object(columns[dataColumn]).data).values.index()==0) widgets::Double("Equal value (column units)",&filterMinimum);
            else widgets::Text("Match text",&filterText);
            ImGui::TextWrapped("Filters hide rows in worksheets and graphs. Source values are preserved. Formula exclusion is an explicit formula option.");
        }
        if(dataAction==2) {widgets::Int("First row (zero-based)",&maskFirst,1,0,1000000);widgets::Int("End row (exclusive)",&maskEnd,1,0,1000000);widgets::Checkbox("Whole rows",&maskRows);
            if(widgets::BeginLabelledRow("Mask colour")) {ImGui::ColorEdit4("##colour",&maskTint.x);widgets::EndLabelledRow();}}
        if(dataAction==3) {
            if(std::get<pl::Column>(p.object(columns[dataColumn]).data).values.index()==0) {widgets::Double("Find value (column units)",&filterMinimum);widgets::Double("Replace value (column units)",&replaceNumber);}
            else {widgets::Text("Find text",&filterText);widgets::Text("Replace text",&replacement);}
        }
        if(dataAction==4 || dataAction==5 || dataAction==9 || dataAction==10 || dataAction==13) {
            std::erase_if(operationColumns,[&](auto id){return std::find(columns.begin(),columns.end(),id)==columns.end();});
            if(widgets::BeginCombo("Columns to process",operationColumns.empty()?"All columns":"Selected columns")) {
                if(ImGui::Selectable("Use all columns",operationColumns.empty())) operationColumns.clear();
                for(auto id:columns) {ImGui::PushID(std::to_string(id).c_str());bool selected=operationColumns.contains(id);if(widgets::Checkbox(p.object(id).name.c_str(),&selected)) {if(selected) operationColumns.insert(id);else operationColumns.erase(id);}ImGui::PopID();}widgets::EndCombo();
            }
        }
        if(dataAction>=4 && dataAction!=11) widgets::Text("Output worksheet name",&operationName);
        if(dataAction==6 || dataAction==11 || dataAction==12) {
            if(widgets::BeginCombo(dataAction==6?"Group column":"X column",p.object(columns[dataSecondColumn]).name.c_str())) {for(int i=0;i<static_cast<int>(columns.size());++i) {ImGui::PushID(std::to_string(columns[i]).c_str());if(ImGui::Selectable(p.object(columns[i]).name.c_str(),i==dataSecondColumn)) dataSecondColumn=i;ImGui::PopID();}widgets::EndCombo();}
        }
        if(dataAction==7) widgets::Int("Split before row (zero-based)",&maskEnd,1,1,1000000);
        if(dataAction==8 && widgets::BeginCombo("Append worksheet",joinSheet && p.objects().contains(joinSheet)?p.object(joinSheet).name.c_str():"Choose worksheet")) {for(const auto& [key,o]:p.objects()) if(std::holds_alternative<pl::Worksheet>(o->data) && key!=sheet) {ImGui::PushID(std::to_string(key).c_str());if(ImGui::Selectable(o->name.c_str(),key==joinSheet)) joinSheet=key;ImGui::PopID();}widgets::EndCombo();}
        if(dataAction==12) widgets::Text("New X values (comma-separated)",&filterText);
        if(dataAction==10) {operationMode=std::clamp(operationMode,0,1);widgets::Combo("Reduction",&operationMode,"Every n-th row\0Group average\0");widgets::Int("Every n rows",&everyN,1,1,1000000);}
        if(dataAction==11) {operationMode=std::clamp(operationMode,0,3);widgets::Combo("Normalization",&operationMode,"Max\0Range\0Area\0Value at X\0");if(operationMode==3) widgets::Double("Normalize at X (X units)",&normalizationX);}
        if(dataAction==13) {operationMode=std::clamp(operationMode,0,1);widgets::Combo("Statistics axis",&operationMode,"Column\0Row\0");widgets::Checkbox("Exclude filtered rows",&excludeFiltered);}
        auto submit=[&](bool clear) {
            using Json=nlohmann::json; static const char* ids[]={"sort","filter","mask","find_replace","transpose","stack","unstack","split","join","extract","reduce","normalize","interpolate","statistics"};
            Json request={{"action",ids[dataAction]},{"worksheet_id",sheet},{"column_id",columns[dataColumn]},{"name",operationName}};
            if(!operationColumns.empty()) request["column_ids"]=operationColumns;
            if(dataAction==0) {Json keys=Json::array({{{"column_id",columns[dataColumn]},{"descending",descending}}});for(const auto& [key,desc]:nestedKeys) keys.push_back({{"column_id",key},{"descending",desc}});request["keys"]=keys;request["column_only"]=columnOnly;request["confirm_break_rows"]=confirmBreak;request["wait"]=false;}
            if(dataAction==1 || dataAction==9) {
                static const char* kinds[]={"range","equality","contains","top_n"};Json filter={{"column_id",columns[dataColumn]},{"kind",kinds[filterKind]}};
                if(filterKind==0) {filter["minimum_column_units"]=filterMinimum;filter["maximum_column_units"]=filterMaximum;}
                else if(filterKind==3) filter["count"]=topN;
                else filter["value"]=std::get<pl::Column>(p.object(columns[dataColumn]).data).values.index()==0?Json(filterMinimum):Json(filterText);
                auto filters=clear?Json::array():Json::parse(std::get<pl::Worksheet>(p.object(sheet).data).filters);
                std::erase_if(filters.get_ref<Json::array_t&>(),[&](const auto& f){return f.at("column_id")==columns[dataColumn];});if(!clear) filters.push_back(filter);request["filters"]=filters;request["wait"]=false;
            }
            if(dataAction==2) {request["row_from"]=maskFirst;request["row_to"]=maskEnd;request["masked"]=!clear;request["color_rgba"]=ImGui::ColorConvertFloat4ToU32(maskTint);if(maskRows) request.erase("column_id");}
            if(dataAction==3) {const bool numeric=std::get<pl::Column>(p.object(columns[dataColumn]).data).values.index()==0;request["find"]=numeric?Json(filterMinimum):Json(filterText);request["replace"]=numeric?Json(replaceNumber):Json(replacement);request["column_ids"]={columns[dataColumn]};}
            if(dataAction==6) {request["key_column_id"]=columns[dataSecondColumn];request["value_column_id"]=columns[dataColumn];}
            if(dataAction==7) request["at_row"]=maskEnd;
            if(dataAction==8) request["other_worksheet_id"]=joinSheet;
            if(dataAction==11 || dataAction==12) request["x_column_id"]=columns[dataSecondColumn];
            if(dataAction==11) request["x_value_column_units"]=normalizationX;
            if(dataAction==12) request["new_x_column_units"]=Json::parse("["+filterText+"]");
            if(dataAction==10) {request["every_n"]=everyN;request["mode"]=operationMode==1?"group_average":"every_nth";}
            if(dataAction==11) {static const char* modes[]={"max","range","area","value_at_x"};request["mode"]=modes[std::clamp(operationMode,0,3)];}
            if(dataAction==13) {request["axis"]=operationMode==1?"row":"column";request["exclude_filtered"]=excludeFiltered;}
            auto response=Json::parse(doc.WorksheetCommand(request.dump()));if(!response.at("ok").get<bool>()) throw std::runtime_error(response.at("error").at("message").get<std::string>());
            if(response.at("result").contains("worksheet_ids")) for(const auto& id:response.at("result").at("worksheet_ids")) state->Show(id.get<pl::Id>());
        };
        if(ImGui::Button("Apply operation")) Attempt([&]{submit(false);});
        if(dataAction==1 || dataAction==2) {ImGui::SameLine();if(ImGui::Button(dataAction==1?"Clear all filters":"Unmask range")) Attempt([&]{submit(true);});}
        ImGui::PopID();
    }
    void WorkspacePanel::Worksheet(pl::Id id)
    {
        auto &doc = *state->document;
        auto &p = *doc.project;
        bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 10},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({std::max(900.0f,ImGui::GetMainViewport()->WorkSize.x-370), std::max(600.0f,ImGui::GetMainViewport()->WorkSize.y-60)}, ImGuiCond_FirstUseEver);
        windows.Render(Title("Worksheet: ", p.object(id), p.id()), open, 0, [&] {
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
            sst::ui::widgets::Checkbox("Text", &textColumn);
            if (ImGui::Button("Recompute"))
                (void)p.recompute();
            ImGui::TextUnformatted(
                "Header: column settings. Double-click a number to edit; Enter commits. Right-click to copy full precision.");
            DataOperations(id);
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
            const auto sheetSnapshot = p.objects().at(id);
            const auto& sheet = std::get<pl::Worksheet>(sheetSnapshot->data);
            const auto& rows = sheet.rows;
            if (ImGui::Button("Header rows")) ImGui::OpenPopup("Header rows");
            if (ImGui::BeginPopup("Header rows")) {
                for (const auto* header : pl::headerNames) {
                    bool visible = std::find(sheet.hiddenHeaders.begin(), sheet.hiddenHeaders.end(), header) == sheet.hiddenHeaders.end();
                    if (sst::ui::widgets::Checkbox(header, &visible)) Attempt([&] { doc.Execute(p.worksheetEdit(nlohmann::json{{"action","header_visibility"},{"worksheet_id",id},{"header",header},{"visible",visible}}.dump())); });
                }
                for (const auto& row : sheet.parameters) {
                    ImGui::PushID(static_cast<int>(row.id));
                    bool visible = row.visible;
                    if (sst::ui::widgets::Checkbox(row.name.c_str(), &visible)) Attempt([&] { doc.Execute(p.worksheetEdit(nlohmann::json{{"action","parameter_visibility"},{"worksheet_id",id},{"parameter_id",row.id},{"visible",visible}}.dump())); });
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) Attempt([&] { doc.Execute(p.worksheetEdit(nlohmann::json{{"action","parameter_remove"},{"worksheet_id",id},{"parameter_id",row.id}}.dump())); });
                    ImGui::PopID();
                }
                sst::ui::widgets::Text("Parameter name", &parameterName);
                if (ImGui::Button("Add parameter")) Attempt([&] { doc.Execute(p.worksheetEdit(nlohmann::json{{"action","parameter_add"},{"worksheet_id",id},{"name",parameterName}}.dump())); parameterName.clear(); });
                ImGui::EndPopup();
            }
            if (columns.size() > 0 && ImGui::BeginTable("data", static_cast<int>(columns.size() + 1),
                                                        ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                                        {0, 0}))
            {
                float rowLabelWidth=ImGui::CalcTextSize("Row").x;
                for(const auto* header:pl::headerNames) rowLabelWidth=std::max(rowLabelWidth,ImGui::CalcTextSize(header).x);
                for(const auto& parameter:sheet.parameters) rowLabelWidth=std::max(rowLabelWidth,ImGui::CalcTextSize(parameter.name.c_str()).x);
                ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed,
                                        rowLabelWidth+2*ImGui::GetStyle().CellPadding.x);
                for (auto column : columns)
                {
                    const auto &o = p.object(column);
                    const float padding = 2 * ImGui::GetStyle().FramePadding.x;
                    ImGui::TableSetupColumn(o.name.c_str(), ImGuiTableColumnFlags_WidthFixed,
                        std::max(ImGui::CalcTextSize(o.name.c_str()).x,
                                 ImGui::CalcTextSize("-1.2345678901234567e-308").x) + padding);
                }
                int headerCount = 1;
                for (const auto* h : pl::headerNames) if (std::find(sheet.hiddenHeaders.begin(),sheet.hiddenHeaders.end(),h)==sheet.hiddenHeaders.end()) ++headerCount;
                for (const auto& row : sheet.parameters) if(row.visible) ++headerCount;
                ImGui::TableSetupScrollFreeze(1, headerCount);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Row");
                for (std::size_t c = 0; c < columns.size(); ++c)
                {
                    ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
                    ImGui::PushID(static_cast<int>(c));
                    const auto &o = p.object(columns[c]);
                    const auto &col = std::get<pl::Column>(o.data);
                    if (ImGui::SmallButton((o.name + "(" + pl::roleTag(col.role) + ")").c_str()))
                    {
                        EditColumn(o.id);
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
                auto headerRow = [&](const std::string& label, pl::Id parameter) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(label.c_str());
                    for(std::size_t index=0;index<columns.size();++index) {
                        ImGui::TableSetColumnIndex(static_cast<int>(index+1));
                        ImGui::PushID(static_cast<int>(index)); ImGui::PushID(std::to_string(parameter).c_str()); ImGui::PushID(label.c_str());
                        const auto snapshot=p.objects().at(columns[index]);
                        const auto& col=std::get<pl::Column>(snapshot->data);
                        if(label=="Sparklines" && !parameter) {
                            const auto* values=std::get_if<std::vector<double>>(&col.values);
                            float preview[64]; int count=values?static_cast<int>(std::min<std::size_t>(64,values->size())):0;
                            double low=0, high=0; bool finite=false;
                            for(int n=0;n<count;++n) {
                                const double v=(*values)[count==1?0:std::size_t(n)*(values->size()-1)/(count-1)];
                                if(std::isfinite(v)) { if(!finite) low=high=v; low=std::min(low,v); high=std::max(high,v); finite=true; }
                            }
                            for(int n=0;n<count;++n) {
                                const double v=(*values)[count==1?0:std::size_t(n)*(values->size()-1)/(count-1)];
                                preview[n]=std::isfinite(v) && high>low ? static_cast<float>((v/2-low/2)/(high/2-low/2)) : 0.5f;
                            }
                            if(finite) ImGui::PlotLines("##spark",preview,count,0,nullptr,0,1,{ImGui::GetContentRegionAvail().x,22});
                        } else if(label=="F(x)" && !parameter) ImGui::TextUnformatted(col.formula?col.formula->expression.c_str():"");
                        else {
                            std::string text = parameter ? (col.parameters.contains(parameter)?col.parameters.at(parameter):"") : label=="Long Name"?col.longName:label=="Units"?col.units:col.comments;
                            ImGui::SetNextItemWidth(-1);
                            if(ImGui::InputText("##header",&text,ImGuiInputTextFlags_EnterReturnsTrue)) Attempt([&] {
                                nlohmann::json request={{"column_id",columns[index]}};
                                if(parameter) { request["action"]="parameter_value"; request["parameter_id"]=parameter; request["value"]=text; }
                                else { request["action"]="column_settings"; request[label=="Long Name"?"long_name":label=="Units"?"units":"comments"]=text; }
                                doc.Execute(p.worksheetEdit(request.dump()));
                            });
                        }
                        ImGui::PopID(); ImGui::PopID(); ImGui::PopID();
                    }
                };
                for(const auto* h:pl::headerNames) if(std::find(sheet.hiddenHeaders.begin(),sheet.hiddenHeaders.end(),h)==sheet.hiddenHeaders.end()) headerRow(h,0);
                for(const auto& row:sheet.parameters) if(row.visible) headerRow(row.name,row.id);
                ImGuiListClipper clipper;
                const bool filtering=sheet.filters!="[]";
                clipper.Begin(static_cast<int>(filtering?sheet.visibleRows.size():rows.size()));
                while (clipper.Step())
                    for (int visibleRow = clipper.DisplayStart; visibleRow < clipper.DisplayEnd; ++visibleRow)
                    {
                        const auto r=filtering?sheet.visibleRows[visibleRow]:static_cast<std::size_t>(visibleRow);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(static_cast<int>(r));
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
                            if(auto color=p.maskColor(columns[c],r)) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,color);
                            ImGui::SetNextItemWidth(-1);
                            if (col.values.index() == 0)
                            {
                                auto value = std::get<std::vector<double>>(col.values)[r];
                                const auto rendered=pl::formatValue(value,col.display);
                                ImGui::Selectable(rendered.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick);
                                const bool hovered=ImGui::IsItemHovered();
                                if(hovered && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) ImGui::SetClipboardText(pl::fullPrecision(value).c_str());
                                if(hovered && ImGui::IsMouseDoubleClicked(0) && !col.formula) { cellText=pl::fullPrecision(value); ImGui::OpenPopup("Edit value"); }
                                if(ImGui::BeginPopupContextItem()) {
                                    if(ImGui::MenuItem("Copy full precision")) ImGui::SetClipboardText(pl::fullPrecision(value).c_str());
                                    ImGui::EndPopup();
                                }
                                if(ImGui::BeginPopup("Edit value")) {
                                    ImGui::TextUnformatted("Stored value (full precision)");
                                    if(ImGui::InputText("##value",&cellText,ImGuiInputTextFlags_EnterReturnsTrue)) Attempt([&] {
                                        doc.Execute(p.setCells(columns[c],r,std::vector<double>{ParseNumericCell(cellText)})); ImGui::CloseCurrentPopup();
                                    });
                                    ImGui::EndPopup();
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
        });
        if (!open) state->sheets.erase(id);
    }
    void WorkspacePanel::Graph(pl::Id id)
    {
        auto &doc = *state->document;
        auto &p = *doc.project;
        bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 320},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({850, 330}, ImGuiCond_FirstUseEver);
        windows.Render(Title("Graph: ", p.object(id), p.id()), open, 0, [&] {
            auto graphObject = p.object(id);
            auto &fontSettings = std::get<pl::Graph>(graphObject.data);
            bool fontChanged = false;
            if (sst::ui::widgets::BeginCombo("Font family", fontSettings.fontFamily.c_str()))
            {
                for (const char *family : {"DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono"})
                    if (ImGui::Selectable(family, fontSettings.fontFamily == family && fontSettings.fontFile.empty()))
                    {
                        fontSettings.fontFamily = family;
                        fontSettings.fontFile.clear();
                        fontChanged = true;
                    }
                sst::ui::widgets::EndCombo();
            }
            if (ImGui::Button("Load font (.ttf/.otf)..."))
                Attempt([&] {
                    if (auto file = OpenFileDialog({"Font", "ttf,otf"})) {
                        auto resolved = ResolveFont("", file->Utf8(), state->pythonRoot);
                        fontSettings.fontFile = resolved.first;
                        fontSettings.fontFamily = resolved.second;
                        fontChanged = true;
                    }
                });
            fontChanged |= sst::ui::widgets::Double("Graph font (pt)", &fontSettings.fontSizePt, 1, "%.1f");
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
            bool changed = sst::ui::widgets::Checkbox("Log X", &logX);
            changed |= sst::ui::widgets::Checkbox("Log Y", &logY);
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
            for(auto series:plots) {
                const auto& source=std::get<pl::Plot>(p.object(series).data);auto e=p.series(series);
                auto& mask=maskPreviews[series];
                if(mask.owner.lock()!=doc.project || mask.revision!=p.revision()+1) {
                    mask.x.clear();mask.y.clear();
                    const auto& sx=std::get<pl::Column>(p.object(source.x).data);const auto& sy=std::get<pl::Column>(p.object(source.y).data);
                    const auto& sheet=std::get<pl::Worksheet>(p.object(source.worksheet).data);
                    if(!sx.masks.empty() || !sy.masks.empty() || !sheet.masks.empty()) for(std::size_t r=0;r<e.data.count;++r) if(p.rowVisible(source.worksheet,r) && (p.maskColor(source.x,r) || p.maskColor(source.y,r))) {mask.x.push_back(e.data.x[r]);mask.y.push_back(e.data.y[r]);}
                    mask.owner=doc.project;mask.revision=p.revision()+1;
                }
                if(!mask.x.empty()) {
                    plot::SeriesEntryView overlay{"##masked"+std::to_string(series),{mask.x.data(),mask.y.data(),mask.x.size()}};
                    overlay.kind=plot::SeriesKind::Scatter;overlay.style.color={1,.3f,.3f,1};overlay.style.marker=plot::Marker::Cross;overlay.style.markerSizePt=6;overlay.style.legend=false;
                    overlay.visible=e.visible && (!view.visibility.contains(std::to_string(series)) || view.visibility.at(std::to_string(series)));entries.push_back(overlay);
                }
            }
            if (!view.fitted)
                spec.autoFit = plot::AutoFit::Once;
            // pt to logical pixels at 96 DPI; ImGui applies the display DPI scale.
            const auto preview=plot::PublicationPreview(chosen.widthMm,chosen.heightMm,ImGui::GetContentRegionAvail());
            ImGui::PushFont(graphFont, preview.metrics.ToPixels(static_cast<float>(chosen.fontSizePt)));
            spec.customAxes = true;
            auto& editor = state->plotEditors[id];
            plot::RenderPlot("##graph", spec, entries.data(), entries.size(), view, preview.metrics, preview.size, &editor);
            ImGui::PopFont();
            auto pending = std::move(editor.pending); editor.pending.clear();
            for (const auto& edit : pending) Attempt([&] {
                auto saved = p.object(layer); std::get<pl::Layer>(saved.data).view = view;
                p.change("Graph viewport", {std::move(saved)}).apply(p);
                const auto result = ExecutePlotEdit(*doc.commands, doc.project, layer, edit, [owner=std::weak_ptr<Session>(state), id, layer] {
                    if (auto session=owner.lock()) {
                        session->views[id] = std::get<pl::Layer>(session->document->project->object(layer).data).view;
                        session->views[id].visibility.clear();
                    }
                });
                if (!result) { editor.error = result.Error().technicalDetails; throw std::runtime_error(editor.error); }
            });
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
        });
        if (!open) state->graphs.erase(id);
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
            sst::ui::widgets::Text("Name", &name);
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
        if (metadata) {
            ImGui::SetNextWindowSizeConstraints({600,0},{1000,std::max(300.0f,ImGui::GetMainViewport()->WorkSize.y-40)});
            ImGui::OpenPopup("Column definition");
        }
        if (ImGui::BeginPopupModal("Column definition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            sst::ui::widgets::Text("Short name", &name);
            sst::ui::widgets::Text("Long name", &longName);
            sst::ui::widgets::Text("Units", &units);
            sst::ui::widgets::Text("Comments", &comments);
            sst::ui::widgets::Combo("Role", &role, "None\0X\0Y\0Z\0yErr\0Label\0xErr\0Disregard\0");
            sst::ui::widgets::Combo("Display format", &displayMode, "General\0Decimal places\0Significant digits\0Scientific\0Engineering (SI)\0Text\0Date/time (Unix seconds, UTC)\0Categorical\0");
            sst::ui::widgets::SliderInt("Digits", &displayDigits, displayMode==2?1:0, 17);
            if (ImGui::Button("Apply metadata"))
                Attempt(
                    [&]
                    {
                        static const char* modes[]={"general","decimal","significant","scientific","engineering","text","date_time","categorical"};
                        auto group=doc.undo->ScopedGroup("Column metadata");
                        try {
                            doc.Execute(p.worksheetEdit(nlohmann::json{{"action","column_settings"},{"column_id",metadata},{"long_name",longName},{"units",units},{"comments",comments},{"role",pl::roleTag(static_cast<pl::Role>(role))},{"display",{{"mode",modes[displayMode]},{"digits",displayDigits}}}}.dump()));
                            if(p.object(metadata).name!=name) doc.Rename(metadata,name);
                            auto result=group.Commit(); if(!result) throw std::runtime_error(result.Error().technicalDetails);
                        } catch(...) { (void)group.Cancel(); throw; }
                    });
            ImGui::SeparatorText("Set Column Values");
            if(previewLibrary) ImGui::SetScrollHereY(0.0f);
            const auto& info=p.recomputeInfo(metadata);
            const char* states[]={"Clean","Dirty","Running","Failed","Blocked"};
            ImGui::Text("Result: %s%s",states[static_cast<int>(info.state)],info.stale?" (previous values retained)":"");
            if(!info.diagnostic.empty()) ImGui::TextWrapped("%s",info.diagnostic.c_str());
            sst::ui::widgets::Text("Expression", &formula, 0, "Definition: column formula. Unit: output column units. Range: supported expressions and stable column references.");
            sst::ui::widgets::Checkbox("Exclude filtered rows from this formula", &excludeFiltered);
            sst::ui::widgets::Int("From row (1-based)", &rowFrom, 1, 0, 0, "Definition: first evaluated row. Unit: row index. Range: 1 through the worksheet row count.");
            sst::ui::widgets::Int("To row (0 = last)", &rowTo, 1, 0, 0, "Definition: last evaluated row. Unit: row index. Range: 0 for the last row, or a worksheet row at/after From row.");
            sst::ui::widgets::Combo("Recalculate", &recalculate, "None\0Auto\0Manual\0");
            sst::ui::widgets::MultilineText("Python before formula", &beforeScript, {480,65});
            ImGui::TextDisabled("Scalar assignments; col(stable_id), mean/sum/min/max; no imports or I/O.");
            if(previewLibrary) {ImGui::SetNextItemOpen(true); previewLibrary=false;}
            if(ImGui::TreeNode("Function library / references")) {
                ImGui::TextUnformatted("abs sqrt exp ln log10 pow sin cos tan atan2\nmin max mean sum std (sample); i (1-based), nrows\ndiff(y[,0 forward / 1 central]); integ(y) (unit row spacing)\ninterp(x,xcol,ycol); smooth(y[,0 average / 1 SG quadratic, 5 points])\nifelse(condition,a,b); + - * / ^ > >= < <= == !=\ncol(A), col(\"Long Name\"), [Book]Sheet!col(A), col(B)[T], wcol(1)");
                ImGui::TextUnformatted("Before-script inputs in this worksheet:");
                for(auto id:p.children(p.object(metadata).parent)) if(std::get<pl::Column>(p.object(id).data).values.index()==0)
                    ImGui::BulletText("%s: col(%llu)",p.object(id).name.c_str(),static_cast<unsigned long long>(id));
                ImGui::TreePop();
            }
            if (ImGui::Button("Set Column Values")) Attempt([&] {
                auto response=nlohmann::json::parse(doc.WorksheetCommand(nlohmann::json{{"action","set_column_values"},{"column_id",metadata},{"expression",formula},{"row_from",rowFrom},{"row_to",rowTo},{"before_script",beforeScript},{"exclude_filtered",excludeFiltered},{"recalculate",pl::recalculateModes[recalculate]}}.dump()));
                if(!response.at("ok").get<bool>()) throw std::runtime_error(response.at("error").at("message").get<std::string>());
            });
            ImGui::SameLine();
            if(ImGui::Button("Recalculate now")) Attempt([&] {
                auto result=nlohmann::json::parse(doc.WorksheetCommand("{\"action\":\"recompute\",\"wait\":false}"));
                if(!result.at("ok").get<bool>()) throw std::runtime_error(result.at("error").at("message").get<std::string>());
            });
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
            if (sst::ui::widgets::BeginCombo("Worksheet", label.c_str()))
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
                sst::ui::widgets::EndCombo();
            }
            ColumnPicker("X", p, sheetChoice, xChoice);
            ColumnPicker("Y", p, sheetChoice, yChoice);
            if (Exists(p,yChoice,3) && std::get<pl::Column>(p.object(yChoice).data).role==pl::Role::Y) {
                const auto paired=p.pairedX(yChoice);
                if(paired) { xChoice=paired; ImGui::Text("Nearest X: %s",p.object(paired).name.c_str()); }
            }
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
            bool changed = sst::ui::widgets::Combo("Delimiter", &delimiter, "Detect\0Comma\0Semicolon\0Tab\0Whitespace\0Pipe\0");
            changed |= sst::ui::widgets::Combo("Decimal", &decimal, "Detect\0Point\0Comma\0");
            changed |= sst::ui::widgets::Combo("Header", &header, "Detect\0Present\0Absent\0");
            changed |= sst::ui::widgets::Combo("Units row", &unitRows, "Detect\0Present\0Absent\0");
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
            sst::ui::widgets::Double("Width (mm)", &width, .01f, "%.6g");
            sst::ui::widgets::Double("Height (mm)", &height, .01f, "%.6g");
            sst::ui::widgets::Int("DPI", &dpi);
            sst::ui::widgets::Combo("Format", &format, "SVG\0PDF\0PNG\0");
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

