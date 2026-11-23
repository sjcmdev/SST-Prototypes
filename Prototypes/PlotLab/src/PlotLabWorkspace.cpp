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
#include <numeric>
#include <random>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
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
    void WorkspacePanel::SyncProject()
    {
        if (viewProject.lock() == state->document->project) return;
        const bool replaced = projectSeen; projectSeen = true;
        viewProject = state->document->project;
        rename = remove = metadata = exportGraph = operationsSheet = contextColumn = 0;
        targetGraph = sheetChoice = xChoice = errorChoice = 0;
        graphDialog = importDialog = false;
        sheetViews.clear(); activeSheets.clear(); renderedSheets.clear(); maskPreviews.clear(); fontErrors.clear();
        availableColumns.clear(); plottedSeries.clear();
        nestedKeys.clear(); operationColumns.clear(); joinSheet = 0; preview.reset(); importText.clear();
        if (replaced) {
            state->plotEditors.clear(); state->RefreshWindows();
            if (GImGui->OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, true);
        }
    }
    void WorkspacePanel::Render()
    {
        SyncProject();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
        windows.BeginFrame();
        const auto frameProject = state->document->project;
        auto &p = *frameProject;
        if(previewColumn && Exists(p,previewColumn,3)) {
            EditColumn(previewColumn); previewColumn=0; previewLibrary=true; valuesDialog=true;
        }
        if (previewData && !state->sheets.empty()) { operationsSheet = *state->sheets.begin(); previewData = false; }
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
            if (state->document->project != frameProject) return;
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
                    Attempt([&] { NewWorkbook(state->selected); });
            }
            if (type == 1 && ImGui::Button("Add worksheet"))
                Attempt([&] { NewWorksheet(state->selected); });
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
            if (ImGui::Button("Create graph from columns...")) BeginGraphDialog(0, type == 2 ? state->selected : 0);
            ImGui::TextWrapped("%s", state->status.c_str());
            if (state->exporting)
                ImGui::TextUnformatted("Export in progress (see Core Progress Monitor)");
        });
        SetVisible(explorerOpen);
        if (state->document->project != frameProject) {
            windows.EndFrame(); ImGui::PopStyleColor(); ImGui::PopStyleVar(); return;
        }
        std::set<pl::Id> books;
        for (auto sheet : state->sheets) if (Exists(p, sheet, 2)) books.insert(p.object(sheet).parent);
        for (auto book : books) {
            auto& active = activeSheets[book];
            if (!Exists(p, active, 2) || p.object(active).parent != book) active = p.children(book).front();
            if (Exists(p, state->selected, 2) && p.object(state->selected).parent == book) active = state->selected;
            Worksheet(active);
        }
        for (auto it = state->graphs.begin(); it != state->graphs.end();)
        {
            const auto id = *it++;
            if (Exists(p, id, 4))
                Graph(id);
        }
        Dialogs();
        windows.EndFrame();
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
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
    void WorkspacePanel::SelectColumn(pl::Id sheet, pl::Id column, bool extend, bool toggle)
    {
        auto& v = sheetViews[sheet];
        const auto columns = state->document->project->children(sheet);
        auto first = std::find(columns.begin(), columns.end(), v.anchorColumn);
        auto last = std::find(columns.begin(), columns.end(), column);
        if (last == columns.end()) return;
        if (extend && first != columns.end()) {
            v.columns.clear();
            if (first > last) std::swap(first, last);
            v.columns.insert(first, last + 1);
        } else {
            if (!toggle) v.columns.clear();
            if (toggle && v.columns.contains(column)) v.columns.erase(column);
            else v.columns.insert(column);
            v.anchorColumn = column;
        }
        v.column = column;
        v.row = v.anchorRow = 0;
        state->document->project->select({sheet, 0, sheet, 0, {}});
    }
    void WorkspacePanel::SelectCell(pl::Id sheet, pl::Id column, pl::Id row, bool extend)
    {
        auto& p = *state->document->project;
        auto& v = sheetViews[sheet];
        const auto& rows = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        if (!extend || !v.anchorRow) { v.anchorRow = row; v.anchorColumn = column; }
        auto first = std::find(rows.begin(), rows.end(), v.anchorRow);
        auto last = std::find(rows.begin(), rows.end(), row);
        if (first == rows.end() || last == rows.end()) return;
        auto anchor = v.anchorRow;
        SelectColumn(sheet, column, true, false);
        v.anchorRow = anchor; v.row = row;
        if (first > last) std::swap(first, last);
        pl::Selection selection{sheet, 0, sheet};
        for (auto it = first; it <= last; ++it)
            if (p.rowVisible(sheet, static_cast<std::size_t>(it - rows.begin()))) selection.rows.push_back(*it);
        p.select(std::move(selection));
    }
    void WorkspacePanel::MaskSelection(pl::Id sheet, bool masked)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto& v = sheetViews[sheet];
        if (v.columns.empty()) throw std::invalid_argument("Select cells or columns first.");
        const auto& rows = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        const auto& selected = p.selection();
        const auto selectedRow = [&](auto row) { return !v.row || (selected.worksheet == sheet && std::find(selected.rows.begin(), selected.rows.end(), row) != selected.rows.end()); };
        std::map<pl::Id, pl::Object> changes;
        const auto columnMasks = [&](pl::Id column) -> auto& {
            if (!changes.contains(column)) changes.emplace(column, p.object(column));
            return std::get<pl::Column>(changes.at(column).data).masks;
        };
        if (!masked) {
            const auto& rowMasks = std::get<pl::Worksheet>(p.object(sheet).data).masks;
            for (const auto& [row, colour] : rowMasks) if (selectedRow(row)) {
                if (!changes.contains(sheet)) changes.emplace(sheet, p.object(sheet));
                std::get<pl::Worksheet>(changes.at(sheet).data).masks.erase(row);
                for (auto column : p.children(sheet)) if (!v.columns.contains(column)) columnMasks(column).try_emplace(row, colour);
            }
        }
        std::vector<pl::Object> updates;
        for (auto column : v.columns) {
            auto& masks = columnMasks(column);
            for (auto row : rows) if (selectedRow(row)) {
                if (masked) masks[row] = IM_COL32(255, 65, 65, 180);
                else masks.erase(row);
            }
        }
        for (auto& [column, o] : changes) updates.push_back(std::move(o));
        if (!updates.empty()) doc.Execute(p.change(masked ? "Mask selection" : "Unmask selection", std::move(updates)));
    }
    void WorkspacePanel::MoveColumn(pl::Id column, int destination)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        auto columns = p.children(p.object(column).parent);
        const auto it = std::find(columns.begin(), columns.end(), column);
        if (it == columns.end()) return;
        columns.erase(it);
        columns.insert(columns.begin() + std::clamp(destination, 0, static_cast<int>(columns.size())), column);
        std::vector<pl::Object> updates;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            auto o = p.object(columns[i]); o.order = i; updates.push_back(std::move(o));
        }
        doc.Execute(p.change("Move column", std::move(updates)));
    }
    void WorkspacePanel::FillSelection(pl::Id sheet, bool random)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto& rows = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        auto group = doc.undo->ScopedGroup("Fill columns");
        try {
            std::mt19937 generator(std::random_device{}());
            std::uniform_real_distribution<double> uniform(0, 1);
            for (auto column : sheetViews[sheet].columns) {
                std::vector<double> values(rows.size());
                for (std::size_t r = 0; r < rows.size(); ++r) values[r] = random ? uniform(generator) : static_cast<double>(r + 1);
                doc.Execute(p.setCells(column, 0, std::move(values)));
            }
            const auto result = group.Commit(); if (!result) throw std::runtime_error(result.Error().technicalDetails);
        } catch (...) { (void)group.Cancel(); throw; }
    }
    void WorkspacePanel::CopySelection(pl::Id sheet)
    {
        const auto& p = *state->document->project; const auto& v = sheetViews[sheet];
        const auto& rows = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        std::string text;
        bool firstRow = true;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (!p.rowVisible(sheet, r) || (v.row && std::find(p.selection().rows.begin(), p.selection().rows.end(), rows[r]) == p.selection().rows.end())) continue;
            if (!firstRow) text += '\n'; firstRow = false;
            bool first = true;
            for (auto column : p.children(sheet)) if (v.columns.contains(column)) {
                if (!first) text += '\t'; first = false;
                const auto& values = std::get<pl::Column>(p.object(column).data).values;
                if (const auto* numbers = std::get_if<std::vector<double>>(&values)) {
                    if (!std::isnan((*numbers)[r])) text += pl::fullPrecision((*numbers)[r]);
                } else text += std::get<std::vector<std::string>>(values)[r];
            }
        }
        ImGui::SetClipboardText(text.c_str());
    }
    void WorkspacePanel::PasteSelection(pl::Id sheet, const char* clipboard)
    {
        if (!clipboard || !*clipboard) return;
        auto& doc = *state->document; auto& p = *doc.project; const auto& v = sheetViews[sheet];
        auto columns = p.children(sheet);
        auto start = std::find(columns.begin(), columns.end(), v.column);
        const auto columnFrom = start == columns.end() ? 0 : static_cast<std::size_t>(start - columns.begin());
        const auto& rowIds = std::get<pl::Worksheet>(p.object(sheet).data).rows;
        auto row = std::find(rowIds.begin(), rowIds.end(), v.row);
        const auto rowFromIndex = row == rowIds.end() ? 0 : static_cast<std::size_t>(row - rowIds.begin());
        // TSV is an external clipboard edge. Plain paste deliberately does not detect headers.
        std::vector<std::vector<std::string>> cells;
        std::string_view text(clipboard);
        while (!text.empty()) {
            auto end = text.find('\n'); auto line = text.substr(0, end);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            std::vector<std::string> fields;
            for (;;) {
                auto tab = line.find('\t'); fields.emplace_back(line.substr(0, tab));
                if (tab == std::string_view::npos) break;
                line.remove_prefix(tab + 1);
            }
            cells.push_back(std::move(fields));
            if (end == std::string_view::npos) break;
            text.remove_prefix(end + 1);
        }
        std::size_t fieldCount = 0; for (const auto& line : cells) fieldCount = std::max(fieldCount, line.size());
        auto group = doc.undo->ScopedGroup("Paste TSV");
        try {
            while (columns.size() < columnFrom + fieldCount) columns.push_back(doc.AddColumn(sheet));
            while (std::get<pl::Worksheet>(p.object(sheet).data).rows.size() < rowFromIndex + cells.size()) doc.ResizeRows(sheet);
            for (std::size_t c = 0; c < fieldCount; ++c) {
                const auto id = columns[columnFrom + c];
                pl::Values values;
                if (std::get<pl::Column>(p.object(id).data).values.index() == 0) {
                    std::vector<double> numeric; numeric.reserve(cells.size());
                    for (const auto& line : cells) numeric.push_back(ParseNumericCell(c < line.size() ? line[c] : ""));
                    values = std::move(numeric);
                } else {
                    std::vector<std::string> strings; strings.reserve(cells.size());
                    for (const auto& line : cells) strings.push_back(c < line.size() ? line[c] : "");
                    values = std::move(strings);
                }
                doc.Execute(p.setCells(id, rowFromIndex, std::move(values)));
            }
            const auto result = group.Commit(); if (!result) throw std::runtime_error(result.Error().technicalDetails);
        } catch (...) { (void)group.Cancel(); throw; }
    }
    void WorkspacePanel::HeaderMenu(pl::Id sheet)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto snapshot = p.objects().at(sheet); const auto& s = std::get<pl::Worksheet>(snapshot->data);
        for (const auto* header : pl::headerNames) {
            bool visible = std::find(s.hiddenHeaders.begin(), s.hiddenHeaders.end(), header) == s.hiddenHeaders.end();
            if (ImGui::MenuItem(header, nullptr, visible)) Attempt([&] {
                auto o = p.object(sheet); auto& hidden = std::get<pl::Worksheet>(o.data).hiddenHeaders;
                std::erase(hidden, header); if (visible) hidden.push_back(header);
                doc.Execute(p.change("Header rows", {std::move(o)}));
            });
        }
        for (const auto& row : s.parameters) {
            ImGui::PushID(std::to_string(row.id).c_str());
            if (ImGui::MenuItem(row.name.c_str(), nullptr, row.visible)) Attempt([&] {
                auto o = p.object(sheet); for (auto& r : std::get<pl::Worksheet>(o.data).parameters) if (r.id == row.id) r.visible = !r.visible;
                doc.Execute(p.change("Parameter visibility", {std::move(o)}));
            });
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) Attempt([&] {
                auto o = p.object(sheet); auto& parameters = std::get<pl::Worksheet>(o.data).parameters;
                std::erase_if(parameters, [&](const auto& r) { return r.id == row.id; });
                std::vector<pl::Object> updates;
                for (auto column : p.children(sheet)) if (std::get<pl::Column>(p.object(column).data).parameters.contains(row.id)) {
                    auto update = p.object(column); std::get<pl::Column>(update.data).parameters.erase(row.id); updates.push_back(std::move(update));
                }
                updates.push_back(std::move(o)); doc.Execute(p.change("Remove parameter", std::move(updates)));
            });
            ImGui::PopID();
        }
        sst::ui::widgets::Text("Parameter name", &parameterName);
        if (ImGui::Button("Add parameter")) Attempt([&] {
            if (parameterName.empty()) throw std::invalid_argument("Enter a parameter name");
            auto o = p.object(sheet); auto& parameters = std::get<pl::Worksheet>(o.data).parameters;
            if (std::any_of(parameters.begin(), parameters.end(), [&](const auto& r) { return r.name == parameterName; })) throw std::invalid_argument("Parameter name already exists");
            parameters.push_back({p.reserveId(), parameterName, true});
            doc.Execute(p.change("Add parameter", {std::move(o)})); parameterName.clear();
        });
    }
    void WorkspacePanel::ColumnMenu(pl::Id sheet)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto columns = p.children(sheet);
        const auto column = contextColumn;
        if (!Exists(p, column, 3)) return;
        if (ImGui::BeginMenu("Set As")) {
            for (auto designation : {pl::Role::X, pl::Role::Y, pl::Role::Z, pl::Role::YError, pl::Role::XError, pl::Role::Label, pl::Role::Disregard})
                if (ImGui::MenuItem(pl::roleTag(designation).c_str())) Attempt([&] {
                    std::vector<pl::Object> updates;
                    for (auto id : sheetViews[sheet].columns) { auto o = p.object(id); std::get<pl::Column>(o.data).role = designation; updates.push_back(std::move(o)); }
                    doc.Execute(p.change("Plot designation", std::move(updates)));
                });
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Set Column Values...", "Ctrl+Q")) { EditColumn(column); valuesDialog = true; }
        if (ImGui::MenuItem("Column Properties...", "F4")) { EditColumn(column); valuesDialog = false; }
        ImGui::Separator();
        if (ImGui::MenuItem("Insert column")) Attempt([&] { auto added = doc.AddColumn(sheet); MoveColumn(added, static_cast<int>(std::find(columns.begin(), columns.end(), column) - columns.begin())); });
        if (ImGui::MenuItem("Add New Columns", "Ctrl+D")) Attempt([&] { doc.AddColumn(sheet); });
        if (ImGui::MenuItem("Delete column...")) remove = column;
        if (ImGui::BeginMenu("Move")) {
            const int index = static_cast<int>(std::find(columns.begin(), columns.end(), column) - columns.begin());
            if (ImGui::MenuItem("First")) Attempt([&] { MoveColumn(column, 0); });
            if (ImGui::MenuItem("Last")) Attempt([&] { MoveColumn(column, static_cast<int>(columns.size() - 1)); });
            if (ImGui::MenuItem("Left", nullptr, false, index > 0)) Attempt([&] { MoveColumn(column, index - 1); });
            if (ImGui::MenuItem("Right", nullptr, false, index + 1 < static_cast<int>(columns.size()))) Attempt([&] { MoveColumn(column, index + 1); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Sort")) {
            for (int mode = 0; mode < 4; ++mode) {
                const char* labels[] = {"Column ascending...", "Column descending...", "Worksheet ascending...", "Worksheet descending..."};
                if (ImGui::MenuItem(labels[mode])) { operationsSheet = sheet; dataAction = 0; columnOnly = mode < 2; descending = mode % 2; dataColumn = static_cast<int>(std::find(columns.begin(), columns.end(), column) - columns.begin()); }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Mask")) Attempt([&] { MaskSelection(sheet, true); });
        if (ImGui::MenuItem("Unmask")) Attempt([&] { MaskSelection(sheet, false); });
        if (ImGui::BeginMenu("Fill")) {
            if (ImGui::MenuItem("Row numbers")) Attempt([&] { FillSelection(sheet, false); });
            if (ImGui::MenuItem("Uniform random")) Attempt([&] { FillSelection(sheet, true); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Plot")) {
            if (ImGui::MenuItem("Line")) Attempt([&] { PlotSelection(sheet, plot::SeriesKind::Line); });
            if (ImGui::MenuItem("Scatter")) Attempt([&] { PlotSelection(sheet, plot::SeriesKind::Scatter); });
            if (ImGui::MenuItem("Line + Symbol")) Attempt([&] { PlotSelection(sheet, plot::SeriesKind::LineScatter); });
            if (ImGui::MenuItem("Column")) Attempt([&] { PlotSelection(sheet, plot::SeriesKind::Column); });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Header rows")) { HeaderMenu(sheet); ImGui::EndMenu(); }
    }
    void WorkspacePanel::PlotSelection(pl::Id sheet, plot::SeriesKind kind)
    {
        const auto& selected = sheetViews[sheet].columns;
        state->Show(state->document->PlotColumns(sheet, {selected.begin(), selected.end()}, kind));
    }
    void WorkspacePanel::NewWorkbook(pl::Id folder)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto before = p.children(folder);
        pl::Table table; table.names = {"A", "B"}; table.columns.resize(2);
        table.columns[0].role = pl::Role::X; table.columns[1].role = pl::Role::Y;
        for (auto& column : table.columns) column.values = std::vector<double>(30, std::numeric_limits<double>::quiet_NaN());
        doc.Execute(pl::importTable(p, folder, "Book" + std::to_string(before.size() + 1), table));
        for (auto book : p.children(folder)) if (std::find(before.begin(), before.end(), book) == before.end()) state->Show(p.children(book).front());
    }
    void WorkspacePanel::NewWorksheet(pl::Id book)
    {
        auto& doc = *state->document; auto& p = *doc.project;
        const auto sheet = p.reserveId(); pl::Worksheet data;
        for (int r = 0; r < 30; ++r) data.rows.push_back(p.reserveId());
        pl::Column x, y; x.role = pl::Role::X; y.role = pl::Role::Y;
        x.values = y.values = std::vector<double>(30, std::numeric_limits<double>::quiet_NaN());
        doc.Execute(p.change("New worksheet", {{sheet, book, "Sheet" + std::to_string(p.children(book).size() + 1), 0, std::move(data)},
            {p.reserveId(), sheet, "A", 0, std::move(x), 0}, {p.reserveId(), sheet, "B", 0, std::move(y), 1}}));
        activeSheets[book] = sheet; state->Show(sheet);
    }
    void WorkspacePanel::BeginGraphDialog(pl::Id graph, pl::Id sheet)
    {
        auto& p = *state->document->project;
        targetGraph = graph; sheetChoice = sheet; xChoice = errorChoice = 0;
        availableColumns.clear(); plottedSeries.clear(); graphDialog = true;
        if (!sheetChoice && graph) {
            const auto layers = p.children(graph);
            if (!layers.empty()) {
                const auto plots = p.children(layers.front());
                if (!plots.empty()) sheetChoice = std::get<pl::Plot>(p.object(plots.front()).data).worksheet;
            }
        }
        if (!sheetChoice) for (const auto& [id, o] : p.objects()) if (std::holds_alternative<pl::Worksheet>(o->data)) { sheetChoice = id; break; }
        if (sheetViews.contains(sheetChoice)) availableColumns = sheetViews[sheetChoice].columns;
    }
    void WorkspacePanel::AddAvailablePlots()
    {
        static constexpr plot::SeriesKind kinds[] = {plot::SeriesKind::Line, plot::SeriesKind::Scatter, plot::SeriesKind::LineScatter, plot::SeriesKind::Column};
        targetGraph = state->document->PlotColumns(sheetChoice, {availableColumns.begin(), availableColumns.end()}, kinds[plotType], targetGraph, xChoice, errorChoice);
        state->Show(targetGraph); availableColumns.clear();
    }
    void WorkspacePanel::Worksheet(pl::Id id)
    {
        auto& p = *state->document->project;
        bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 10}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({std::max(900.0f, ImGui::GetMainViewport()->WorkSize.x - 370), std::max(600.0f, ImGui::GetMainViewport()->WorkSize.y - 60)}, ImGuiCond_FirstUseEver);
        const auto book = p.object(id).parent;
        windows.Render(Title("Workbook: ", p.object(book), p.id()), open, 0, [&] { WorksheetContents(id); });
        if (!open) for (auto sheet : p.children(book)) state->sheets.erase(sheet);
    }
    void WorkspacePanel::WorksheetContents(pl::Id id)
    {
        SyncProject();
        auto& doc = *state->document; auto& p = *doc.project; auto& v = sheetViews[id];
        auto columns = p.children(id);
        std::erase_if(v.columns, [&](auto column) { return std::find(columns.begin(), columns.end(), column) == columns.end(); });
        ImGui::PushID(std::to_string(id).c_str());
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool keyboard = focused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive();
        if (keyboard) {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) Attempt([&] { doc.AddColumn(id); });
            if ((ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) || ImGui::IsKeyPressed(ImGuiKey_F4)) {
                if (Exists(p, v.column, 3)) { EditColumn(v.column); valuesDialog = !ImGui::IsKeyPressed(ImGuiKey_F4); }
            }
            if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F10) && Exists(p, v.column, 3)) { contextColumn = v.column; ImGui::OpenPopup("Column menu"); }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) Attempt([&] { CopySelection(id); });
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) Attempt([&] { PasteSelection(id, ImGui::GetClipboardText()); });
        }
        const auto button = [&](const char* icon, const char* tooltip, auto action) {
            if (ImGui::Button(icon, {ImGui::GetFrameHeight() + 5, 0})) Attempt(action);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
            ImGui::SameLine();
        };
        ImGui::BeginChild("Toolbar", {0, ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 3}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        button(ICON_FA_BOOK "##new", "New Book", [&] { NewWorkbook(p.root()); });
        button(ICON_FA_CHART_LINE "##graph", "New Graph from selection (Line + Symbol)", [&] { PlotSelection(id, plot::SeriesKind::LineScatter); });
        button(ICON_FA_FILE_IMPORT, "Import CSV / TXT", [&] { BeginImport(); });
        button(ICON_FA_TABLE_COLUMNS, "Add New Columns (Ctrl+D)", [&] { doc.AddColumn(id); });
        button(ICON_FA_CALCULATOR, "Set Column Values (Ctrl+Q)", [&] { if (!v.column) throw std::invalid_argument("Select a column first"); EditColumn(v.column); valuesDialog = true; });
        button(ICON_FA_SORT, "Sort / Filter", [&] { operationsSheet = id; dataAction = 0; });
        button(ICON_FA_MASK, "Mask selected cells", [&] { MaskSelection(id, true); });
        button(ICON_FA_CHART_SIMPLE "##statistics", "Statistics", [&] { operationsSheet = id; dataAction = 13; });
        button(ICON_FA_CHART_LINE "##line", "Line", [&] { PlotSelection(id, plot::SeriesKind::Line); });
        button(ICON_FA_BRAILLE, "Scatter", [&] { PlotSelection(id, plot::SeriesKind::Scatter); });
        button(ICON_FA_BEZIER_CURVE, "Line + Symbol", [&] { PlotSelection(id, plot::SeriesKind::LineScatter); });
        button(ICON_FA_CHART_COLUMN, "Column", [&] { PlotSelection(id, plot::SeriesKind::Column); });
        if (ImGui::Button(ICON_FA_ELLIPSIS)) ImGui::OpenPopup("Sheet actions");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Worksheet actions and header rows");
        if (ImGui::BeginPopup("Sheet actions")) {
            if (ImGui::MenuItem("Add row")) Attempt([&] { doc.ResizeRows(id); });
            if (ImGui::MenuItem("Delete selected rows")) Attempt([&] {
                const auto rows = std::get<pl::Worksheet>(p.object(id).data).rows;
                const auto selection = p.selection();
                if (selection.worksheet != id || selection.rows.empty()) throw std::invalid_argument("Select rows first");
                auto group = doc.undo->ScopedGroup("Delete rows");
                try {
                    for (std::size_t r = rows.size(); r-- > 0;) if (std::find(selection.rows.begin(), selection.rows.end(), rows[r]) != selection.rows.end()) doc.ResizeRows(id, r);
                    const auto result = group.Commit(); if (!result) throw std::runtime_error(result.Error().technicalDetails);
                } catch (...) { (void)group.Cancel(); throw; }
            });
            if (ImGui::MenuItem("Add text column")) Attempt([&] { doc.AddColumn(id, true); });
            if (ImGui::MenuItem("Recalculate")) (void)p.recompute();
            if (ImGui::MenuItem("Data operations...")) operationsSheet = id;
            if (ImGui::BeginMenu("Header rows")) { HeaderMenu(id); ImGui::EndMenu(); }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        bool columnMenuRequested = false;
        const auto snapshot = p.objects().at(id); const auto& sheet = std::get<pl::Worksheet>(snapshot->data); const auto& rows = sheet.rows;
        const auto& io = ImGui::GetIO();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.KeyCtrl && io.MouseWheel != 0) v.zoom = std::clamp(v.zoom + io.MouseWheel * .1f, .5f, 2.5f);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * v.zoom);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
        // shortcut: ImGui tables allow 510 data columns; use horizontal virtualization if real workbooks exceed that.
        const auto count = std::min<std::size_t>(columns.size(), 510);
        if (columns.size() > count) ImGui::TextWrapped("Showing the first 510 columns. Additional columns remain in the project.");
        bool navigate = false;
        if (keyboard && !rows.empty() && !columns.empty()) {
            auto c = std::find(columns.begin(), columns.end(), v.column); auto r = std::find(rows.begin(), rows.end(), v.row);
            int ci = c == columns.end() ? 0 : static_cast<int>(c - columns.begin()); int ri = r == rows.end() ? 0 : static_cast<int>(r - rows.begin());
            int dc = ImGui::IsKeyPressed(ImGuiKey_RightArrow) - ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
            int dr = ImGui::IsKeyPressed(ImGuiKey_DownArrow) - ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            if (dc || dr) {
                if (r == rows.end() && dr > 0) ri = -1;
                ci = std::clamp(ci + dc, 0, static_cast<int>(columns.size() - 1));
                ri = std::clamp(ri + dr, 0, static_cast<int>(rows.size() - 1));
                while (dr && !p.rowVisible(id, ri) && ri + dr >= 0 && ri + dr < static_cast<int>(rows.size())) ri += dr;
                if (p.rowVisible(id, ri)) { SelectCell(id, columns[ci], rows[ri], io.KeyShift); navigate = true; }
            }
        }
        if (count && ImGui::BeginTable("data", static_cast<int>(count + 1), ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit, {0, std::max(60.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing())})) {
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Long Name").x + 15);
            for (std::size_t c = 0; c < count; ++c) ImGui::TableSetupColumn(p.object(columns[c]).name.c_str(), ImGuiTableColumnFlags_WidthFixed, 112 * v.zoom);
            int headers = 1;
            for (const auto* h : pl::headerNames) if (std::find(sheet.hiddenHeaders.begin(), sheet.hiddenHeaders.end(), h) == sheet.hiddenHeaders.end()) ++headers;
            for (const auto& row : sheet.parameters) if (row.visible) ++headers;
            ImGui::TableSetupScrollFreeze(1, headers);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Row");
            if (ImGui::BeginPopupContextItem("Header rows")) { HeaderMenu(id); ImGui::EndPopup(); }
            for (std::size_t c = 0; c < count; ++c) {
                if (!ImGui::TableSetColumnIndex(static_cast<int>(c + 1))) continue;
                const auto column = columns[c]; const auto& o = p.object(column); const auto& col = std::get<pl::Column>(o.data);
                ImGui::PushID(std::to_string(column).c_str());
                const auto label = o.name + "(" + pl::roleTag(col.role) + ")";
                if (ImGui::Selectable(label.c_str(), v.columns.contains(column), ImGuiSelectableFlags_AllowDoubleClick)) SelectColumn(id, column, io.KeyShift, io.KeyCtrl);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { EditColumn(column); valuesDialog = false; }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    if (!v.columns.contains(column)) SelectColumn(id, column, false, false);
                    contextColumn = column;
                }
                const bool context = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                ImGui::PopID();
                if (context) columnMenuRequested = true;
            }
            const auto headerRow = [&](const std::string& label, pl::Id parameter) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label == "F(x)" ? "F(x)=" : label.c_str());
                if (ImGui::BeginPopupContextItem((label + " rows").c_str())) { HeaderMenu(id); ImGui::EndPopup(); }
                ImGui::PushID(label.c_str()); ImGui::PushID(std::to_string(parameter).c_str());
                for (std::size_t c = 0; c < count; ++c) {
                    if (!ImGui::TableSetColumnIndex(static_cast<int>(c + 1))) continue;
                    const auto column = columns[c]; const auto columnSnapshot = p.objects().at(column); const auto& col = std::get<pl::Column>(columnSnapshot->data);
                    ImGui::PushID(std::to_string(column).c_str());
                    if (label == "Sparklines" && !parameter) {
                        if (const auto* values = std::get_if<std::vector<double>>(&col.values); values && !values->empty()) {
                            float sample[64]; const auto n = static_cast<int>(std::min<std::size_t>(64, values->size()));
                            for (int i = 0; i < n; ++i) sample[i] = static_cast<float>((*values)[n == 1 ? 0 : std::size_t(i) * (values->size() - 1) / (n - 1)]);
                            ImGui::PlotLines("##spark", sample, n, 0, nullptr, FLT_MAX, FLT_MAX, {ImGui::GetContentRegionAvail().x, 20 * v.zoom});
                        }
                    } else {
                        std::string text = parameter ? (col.parameters.contains(parameter) ? col.parameters.at(parameter) : "") : label == "Long Name" ? col.longName : label == "Units" ? col.units : label == "F(x)" ? (col.formula ? col.formula->expression : "") : col.comments;
                        ImGui::SetNextItemWidth(-1);
                        const bool formulaHeader = label == "F(x)" && !parameter;
                        if (ImGui::InputText("##header", &text, ImGuiInputTextFlags_EnterReturnsTrue)) Attempt([&] {
                            if (formulaHeader) {
                                if (text.empty()) { if (col.formula) doc.Execute(p.detach(column)); }
                                else {
                                    auto definition = col.formula.value_or(pl::Formula{}); definition.expression = text;
                                    definition.bindings.clear(); definition.parameterBindings.clear(); definition.bindingDiagnostic.clear(); definition.excludeMasked = true;
                                    doc.SetColumnValues(column, std::move(definition));
                                }
                                return;
                            }
                            auto o = p.object(column); auto& update = std::get<pl::Column>(o.data);
                            if (parameter) update.parameters[parameter] = text;
                            else if (label == "Long Name") update.longName = text;
                            else if (label == "Units") update.units = text;
                            else update.comments = text;
                            doc.Execute(p.change("Column header", {std::move(o)}));
                        });
                        if (formulaHeader && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { EditColumn(column); valuesDialog = true; }
                    }
                    ImGui::PopID();
                }
                ImGui::PopID(); ImGui::PopID();
            };
            for (const auto* h : pl::headerNames) if (std::find(sheet.hiddenHeaders.begin(), sheet.hiddenHeaders.end(), h) == sheet.hiddenHeaders.end()) headerRow(h, 0);
            for (const auto& row : sheet.parameters) if (row.visible) headerRow(row.name, row.id);
            const bool filtering = sheet.filters != "[]";
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(filtering ? sheet.visibleRows.size() : rows.size()));
            if (navigate) {
                const auto ri = static_cast<std::size_t>(std::find(rows.begin(), rows.end(), v.row) - rows.begin());
                if (!filtering) clipper.IncludeItemByIndex(static_cast<int>(ri));
            }
            while (clipper.Step()) for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
                const auto r = filtering ? sheet.visibleRows[visible] : static_cast<std::size_t>(visible);
                const bool rowSelected = p.selection().worksheet == id && std::find(p.selection().rows.begin(), p.selection().rows.end(), rows[r]) != p.selection().rows.end();
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushID(std::to_string(rows[r]).c_str());
                const auto selectRow = [&](bool extend) {
                    if (io.KeyCtrl && !extend) {
                        doc.SelectRow(id, r, id, true); v.anchorRow = v.row = rows[r]; v.anchorColumn = v.column = columns.front();
                    } else SelectCell(id, columns.front(), rows[r], extend);
                    v.columns = std::set<pl::Id>(columns.begin(), columns.end());
                };
                if (ImGui::Selectable(std::to_string(r + 1).c_str(), rowSelected, ImGuiSelectableFlags_SelectOnClick)) selectRow(io.KeyShift);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0)) selectRow(true);
                if (navigate && rows[r] == v.row) ImGui::SetScrollHereY();
                for (std::size_t c = 0; c < count; ++c) {
                    if (!ImGui::TableSetColumnIndex(static_cast<int>(c + 1))) continue;
                    const auto column = columns[c]; const auto columnSnapshot = p.objects().at(column); const auto& col = std::get<pl::Column>(columnSnapshot->data);
                    ImGui::PushID(std::to_string(column).c_str());
                    const bool selected = v.columns.contains(column) && (!v.row || rowSelected);
                    if (auto color = p.maskColor(column, r)) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, color);
                    const auto text = col.values.index() == 0 ? pl::formatValue(std::get<std::vector<double>>(col.values)[r], col.display) : std::get<std::vector<std::string>>(col.values)[r];
                    const auto label = text + "##cell";
                    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick | static_cast<ImGuiSelectableFlags>(ImGuiSelectableFlags_SelectOnClick))) SelectCell(id, column, rows[r], io.KeyShift);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) SelectCell(id, column, rows[r], true);
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        if (col.formula) { EditColumn(column); valuesDialog = true; }
                        else { cellText = col.values.index() == 0 ? pl::fullPrecision(std::get<std::vector<double>>(col.values)[r]) : text; ImGui::OpenPopup("Edit cell"); }
                    }
                    if (ImGui::BeginPopupContextItem("Cell menu")) {
                        if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelection(id);
                        if (ImGui::MenuItem("Paste", "Ctrl+V")) Attempt([&] { PasteSelection(id, ImGui::GetClipboardText()); });
                        if (ImGui::MenuItem("Mask")) Attempt([&] { MaskSelection(id, true); });
                        if (ImGui::MenuItem("Unmask")) Attempt([&] { MaskSelection(id, false); });
                        ImGui::EndPopup();
                    }
                    if (ImGui::BeginPopup("Edit cell")) {
                        ImGui::TextUnformatted("Value (column units)");
                        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                        if (ImGui::InputText("##value", &cellText, ImGuiInputTextFlags_EnterReturnsTrue)) Attempt([&] {
                            doc.Execute(p.setCells(column, r, col.values.index() == 0 ? pl::Values{std::vector<double>{ParseNumericCell(cellText)}} : pl::Values{std::vector<std::string>{cellText}})); ImGui::CloseCurrentPopup();
                        });
                        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(); ImGui::PopStyleColor(); ImGui::PopFont();
        if (columnMenuRequested) ImGui::OpenPopup("Column menu");
        if (ImGui::BeginPopup("Column menu")) { ColumnMenu(id); ImGui::EndPopup(); }
        ImGui::PopID();
        const auto book = p.object(id).parent;
        ImGui::PushID(("book" + std::to_string(book)).c_str());
        if (ImGui::BeginTabBar("Sheets")) {
            for (auto tabSheet : p.children(book)) {
                const auto label = p.object(tabSheet).name + "##" + std::to_string(tabSheet);
                if (ImGui::BeginTabItem(label.c_str(), nullptr, tabSheet == id && renderedSheets[book] != id ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (tabSheet != id) { activeSheets[book] = tabSheet; state->Show(tabSheet); }
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) Attempt([&] { NewWorksheet(book); });
            ImGui::EndTabBar();
        }
        renderedSheets[book] = id;
        ImGui::PopID();
    }
    void WorkspacePanel::Graph(pl::Id id)
    {
        auto& p = *state->document->project; bool open = true;
        ImGui::SetNextWindowPos({ImGui::GetMainViewport()->WorkPos.x + 350, ImGui::GetMainViewport()->WorkPos.y + 320}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({900, 650}, ImGuiCond_FirstUseEver);
        windows.Render(Title("Graph: ", p.object(id), p.id()), open, 0, [&] { GraphContents(id); });
        if (!open) state->graphs.erase(id);
    }
    void WorkspacePanel::GraphContents(pl::Id id)
    {
        SyncProject();
        auto& doc = *state->document; auto& p = *doc.project;
        auto& editor = state->plotEditors[id];
        const auto plotsInLayer = p.children(p.children(id).front());
        const auto details = [&] {
            if (editor.selected.empty() && !plotsInLayer.empty()) editor.Select(std::to_string(plotsInLayer.front()), false);
            editor.details = true;
        };
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2)) details();
        if (ImGui::Button("Fit")) state->views[id] = {};
        ImGui::SameLine(); if (ImGui::Button("Layer Contents...")) BeginGraphDialog(id);
        ImGui::SameLine(); if (ImGui::Button("Plot Details (F2)")) details();
        ImGui::SameLine(); if (ImGui::Button("Axes")) ImGui::OpenPopup("Axes");
        if (ImGui::BeginPopup("Axes")) {
            const char* sides[] = {"Bottom", "Left", "Top", "Right"};
            for (int side = 0; side < 4; ++side) if (ImGui::MenuItem(sides[side])) editor.axisDialog = static_cast<plot::AxisSide>(side);
            ImGui::EndPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Legend")) editor.legendDialog = true;
        ImGui::SameLine(); if (ImGui::Button("Font")) ImGui::OpenPopup("Graph font");
        ImGui::SameLine(); if (ImGui::Button("Export...")) {
            exportGraph = id; const auto& g = std::get<pl::Graph>(p.object(id).data); width = g.widthMm; height = g.heightMm;
        }
            auto graphObject = p.object(id);
            auto &fontSettings = std::get<pl::Graph>(graphObject.data);
            bool fontChanged = false;
            ImGui::SetNextWindowSize({520, 240}, ImGuiCond_Appearing);
            if (ImGui::BeginPopup("Graph font")) {
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
            if (ImGui::Button("Retry")) fontFailures.erase(fontSettings.fontFamily + "\n" + fontSettings.fontFile);
            if (fontErrors.contains(id)) ImGui::TextWrapped("%s", fontErrors.at(id).c_str());
            ImGui::EndPopup();
            }
            if (fontChanged)
                Attempt([&] { doc.Execute(p.change("Graph font", {graphObject})); });
            const auto &chosen = std::get<pl::Graph>(p.object(id).data);
            ImFont *graphFont = nullptr;
            const auto key = chosen.fontFamily + "\n" + chosen.fontFile;
            try {
                if (fontFailures.contains(key)) throw std::runtime_error(fontFailures.at(key));
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
            } catch (const std::exception &e) {
                if (!fontFailures.contains(key)) fontFailures[key] = FriendlyFontError(e.what());
                fontErrors[id] = fontFailures.at(key);
            }
            if (!graphFont) {
                ImGui::TextWrapped("%s", fontErrors[id].c_str());
                graphFont = ImGui::GetFont();
            }
            const auto layer = p.children(id).at(0);
            auto spec = std::get<pl::Layer>(p.object(layer).data).spec;
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
                    const auto& sx=std::get<pl::Column>(p.object(source.x ? source.x : source.y).data);const auto& sy=std::get<pl::Column>(p.object(source.y).data);
                    const auto& sheet=std::get<pl::Worksheet>(p.object(source.worksheet).data);
                    if(!sx.masks.empty() || !sy.masks.empty() || !sheet.masks.empty()) for(std::size_t r=0;r<e.data.count;++r) if(p.rowVisible(source.worksheet,r) && ((source.x && p.maskColor(source.x,r)) || p.maskColor(source.y,r))) {mask.x.push_back(e.data.x[r]);mask.y.push_back(e.data.y[r]);}
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
            const auto publication=plot::PublicationPreview(chosen.widthMm,chosen.heightMm,ImGui::GetContentRegionAvail());
            ImGui::PushFont(graphFont, publication.metrics.ToPixels(static_cast<float>(chosen.fontSizePt)));
            spec.customAxes = true;
            plot::RenderPlot("##graph", spec, entries.data(), entries.size(), view, publication.metrics, publication.size, &editor);
            ImGui::PopFont();
            if (auto* rendered = ImPlot::GetPlot("##graph"); rendered && rendered->PlotRect.Contains(ImGui::GetMousePos()) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(1) && !plot::IsPlotLegendHovered(editor)) ImGui::OpenPopup("Graph actions");
            if (ImGui::BeginPopup("Graph actions")) {
                if (ImGui::MenuItem("Add Plot to Layer...")) BeginGraphDialog(id);
                if (ImGui::MenuItem("Layer Contents...")) BeginGraphDialog(id);
                if (ImGui::MenuItem("Plot Details", "F2")) details();
                if (ImGui::MenuItem("Legend properties")) editor.legendDialog = true;
                if (ImGui::BeginMenu("Axes")) {
                    const char* sides[] = {"Bottom", "Left", "Top", "Right"};
                    for (int side = 0; side < 4; ++side) if (ImGui::MenuItem(sides[side])) editor.axisDialog = static_cast<plot::AxisSide>(side);
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
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
    }
    void WorkspacePanel::Dialogs()
    {
        SyncProject();
        auto &doc = *state->document;
        const auto dialogProject = doc.project;
        auto &p = *dialogProject;
        if (operationsSheet && Exists(p, operationsSheet, 2)) {
            ImGui::SetNextWindowSize({620, 530}, ImGuiCond_Appearing);
            ImGui::OpenPopup("Worksheet operations");
        }
        if (ImGui::BeginPopupModal("Worksheet operations", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            if (Exists(p, operationsSheet, 2)) DataOperations(operationsSheet);
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { operationsSheet = 0; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
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
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                state->pending = FileAction::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (doc.project != dialogProject) { SyncProject(); return; }
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
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
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
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                remove = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        if (metadata && !Exists(p, metadata, 3)) metadata = 0;
        const char* columnDialog = valuesDialog ? "Set Values" : "Column Properties";
        if (metadata) {
            ImGui::SetNextWindowSize(valuesDialog ? ImVec2(720, 660) : ImVec2(650, 400), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints({560, 300}, {1100, std::max(400.0f, ImGui::GetMainViewport()->WorkSize.y - 40)});
            ImGui::OpenPopup(columnDialog);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
        if (ImGui::BeginPopupModal(columnDialog, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            const auto current = p.objects().at(metadata);
            const auto columns = p.children(current->parent);
            ImGui::Text("%s / %s", p.object(current->parent).name.c_str(), current->name.c_str());
            const auto close = [&] { metadata = 0; ImGui::CloseCurrentPopup(); };
            if (valuesDialog) {
                const auto& info = p.recomputeInfo(metadata);
                const char* states[] = {"Clean", "Dirty", "Running", "Failed", "Blocked"};
                ImGui::Text("Result: %s%s", states[static_cast<int>(info.state)], info.stale ? " (previous values retained)" : "");
                if (!info.diagnostic.empty()) ImGui::TextWrapped("%s", info.diagnostic.c_str());
                sst::ui::widgets::MultilineText("Column formula", &formula, {-1, 100});
                if (sst::ui::widgets::BeginCombo("Functions", "Insert function / reference")) {
                    for (const auto* function : {"abs()", "sqrt()", "exp()", "ln()", "log10()", "sin()", "cos()", "min()", "max()", "mean()", "sum()", "std()", "diff()", "integ()", "smooth()", "interp()", "ifelse()", "i", "nrows"})
                        if (ImGui::Selectable(function)) formula += function;
                    ImGui::Separator();
                    for (auto column : columns) if (std::get<pl::Column>(p.object(column).data).values.index() == 0) {
                        const auto reference = "col(" + p.object(column).name + ")";
                        if (ImGui::Selectable(reference.c_str())) formula += reference;
                    }
                    sst::ui::widgets::EndCombo();
                }
                sst::ui::widgets::Int("Row (i) From", &rowFrom, 1, 1, 1000000000);
                sst::ui::widgets::Int("Row (i) To (0 = last)", &rowTo, 1, 0, 1000000000);
                sst::ui::widgets::Combo("Recalculate", &recalculate, "None\0Auto\0Manual\0");
                sst::ui::widgets::Checkbox("Exclude filtered rows", &excludeFiltered);
                sst::ui::widgets::MultilineText("Before Formula Scripts (Python)", &beforeScript, {-1, 80});
                ImGui::TextDisabled("Scalar assignments; col(stable_id), mean/sum/min/max. No imports or I/O.");
                if (previewLibrary) { ImGui::SetNextItemOpen(true); previewLibrary = false; }
                if (ImGui::TreeNode("Function library / references")) {
                    ImGui::TextWrapped("col(A), col(\"Long Name\"), [Book]Sheet!col(A), col(B)[T], wcol(1). i starts at 1.\n+ - * / ^ > >= < <= == !=; ifelse(condition,a,b). diff(y[,0 forward / 1 central]); integ(y); smooth(y[,0 average / 1 SG quadratic]).");
                    for (auto column : columns) ImGui::BulletText("%s: col(%llu)", p.object(column).name.c_str(), static_cast<unsigned long long>(column));
                    ImGui::TreePop();
                }
                const auto apply = [&] {
                    if (rowFrom < 1 || rowTo < 0 || (rowTo && rowTo < rowFrom)) throw std::invalid_argument("Row range must start at 1 or later and end at or after From.");
                    pl::Formula definition; definition.expression = formula; definition.rowFrom = rowFrom; definition.rowTo = rowTo;
                    definition.beforeScript = beforeScript; definition.excludeFiltered = excludeFiltered;
                    definition.excludeMasked = true; definition.recalculate = static_cast<pl::RecalculateMode>(recalculate);
                    doc.SetColumnValues(metadata, std::move(definition));
                };
                if (ImGui::Button("OK")) Attempt([&] { apply(); close(); });
                ImGui::SameLine(); if (ImGui::Button("Apply")) Attempt(apply);
                ImGui::SameLine(); if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
                ImGui::SameLine(); if (ImGui::Button("Recalculate now")) (void)p.recompute();
                ImGui::SameLine();
                ImGui::BeginDisabled(!std::get<pl::Column>(current->data).formula.has_value());
                if (ImGui::Button("Detach formula")) Attempt([&] { doc.Execute(p.detach(metadata)); });
                ImGui::EndDisabled();
            } else {
                sst::ui::widgets::Text("Short Name", &name);
                sst::ui::widgets::Text("Long Name", &longName);
                sst::ui::widgets::Text("Units", &units);
                sst::ui::widgets::Text("Comments", &comments);
                sst::ui::widgets::Combo("Plot Designation", &role, "None\0X\0Y\0Z\0yErr\0Label\0xErr\0Disregard\0");
                sst::ui::widgets::Combo("Format", &displayMode, "Numeric: General\0Numeric: Decimal\0Numeric: Significant digits\0Numeric: Scientific\0Numeric: Engineering (SI)\0Text\0Date/time (Unix seconds, UTC)\0Categorical\0");
                sst::ui::widgets::SliderInt("Display digits", &displayDigits, displayMode == 2 ? 1 : 0, 17);
                const auto apply = [&] {
                    auto o = p.object(metadata); o.name = name; auto& column = std::get<pl::Column>(o.data);
                    column.longName = longName; column.units = units; column.comments = comments;
                    column.role = static_cast<pl::Role>(role); column.display = {static_cast<pl::DisplayMode>(displayMode), static_cast<unsigned>(displayDigits)};
                    doc.Execute(p.change("Column Properties", {std::move(o)}));
                };
                const auto at = std::find(columns.begin(), columns.end(), metadata);
                ImGui::BeginDisabled(at == columns.begin());
                if (ImGui::Button("Previous")) Attempt([&] { apply(); EditColumn(*(at - 1)); });
                ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(at + 1 == columns.end());
                if (ImGui::Button("Next")) Attempt([&] { apply(); EditColumn(*(at + 1)); });
                ImGui::EndDisabled(); ImGui::SameLine();
                if (ImGui::Button("OK")) Attempt([&] { apply(); close(); });
                ImGui::SameLine(); if (ImGui::Button("Apply")) Attempt(apply);
                ImGui::SameLine(); if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) close();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
        if (graphDialog) {
            ImGui::SetNextWindowSize({840, 550}, ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints({680, 420}, {1400, 1000});
            ImGui::OpenPopup("Layer Contents");
        }
        if (ImGui::BeginPopupModal("Layer Contents", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            if (targetGraph && !Exists(p, targetGraph, 4)) { targetGraph = 0; plottedSeries.clear(); }
            ImGui::TextUnformatted(targetGraph ? p.object(targetGraph).name.c_str() : "New graph");
            const auto label = Exists(p, sheetChoice, 2) ? p.object(sheetChoice).name : "Choose worksheet";
            if (sst::ui::widgets::BeginCombo("Worksheet", label.c_str())) {
                for (const auto& [id, o] : p.objects()) if (std::holds_alternative<pl::Worksheet>(o->data)) {
                    const auto text = p.object(o->parent).name + "." + o->name + " [" + std::to_string(id) + "]";
                    if (ImGui::Selectable(text.c_str(), sheetChoice == id)) { sheetChoice = id; xChoice = errorChoice = 0; availableColumns.clear(); }
                }
                sst::ui::widgets::EndCombo();
            }
            ColumnPicker("X", p, sheetChoice, xChoice, true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("None uses the nearest designated X column, or row numbers.");
            ColumnPicker("yErr", p, sheetChoice, errorChoice, true);
            sst::ui::widgets::Combo("Plot type", &plotType, "Line\0Scatter\0Line + Symbol\0Column\0");
            if (ImGui::BeginTable("Contents", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableNextColumn(); ImGui::TextUnformatted("Available columns");
                ImGui::BeginChild("Available", {0, std::max(100.0f, ImGui::GetContentRegionAvail().y - 95)}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                if (Exists(p, sheetChoice, 2)) for (auto column : p.children(sheetChoice)) {
                    const auto& o = p.object(column); const auto& c = std::get<pl::Column>(o.data);
                    if (c.values.index() != 0 || c.role == pl::Role::X || c.role == pl::Role::YError || c.role == pl::Role::XError || c.role == pl::Role::Label || c.role == pl::Role::Disregard) continue;
                    const auto text = o.name + (c.longName.empty() ? "" : " — " + c.longName) + "##" + std::to_string(column);
                    if (ImGui::Selectable(text.c_str(), availableColumns.contains(column))) {
                        if (!availableColumns.erase(column)) availableColumns.insert(column);
                    }
                }
                ImGui::EndChild();
                ImGui::BeginDisabled(availableColumns.empty());
                if (ImGui::Button("Add Plot to Layer >>")) Attempt([&] { AddAvailablePlots(); });
                ImGui::EndDisabled();
                ImGui::TableNextColumn(); ImGui::TextUnformatted("Plotted in Layer1");
                ImGui::BeginChild("Plotted", {0, std::max(100.0f, ImGui::GetContentRegionAvail().y - 95)}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                if (targetGraph) for (auto series : p.children(p.children(targetGraph).front())) {
                    const auto& o = p.object(series); const auto& source = std::get<pl::Plot>(o.data);
                    const auto text = p.object(source.worksheet).name + "!" + o.name + "##" + std::to_string(series);
                    if (ImGui::Selectable(text.c_str(), plottedSeries.contains(series))) {
                        if (!plottedSeries.erase(series)) plottedSeries.insert(series);
                    }
                }
                ImGui::EndChild();
                ImGui::BeginDisabled(plottedSeries.empty());
                if (ImGui::Button("<< Remove")) Attempt([&] {
                    doc.Execute(p.change("Remove plots from layer", {}, {plottedSeries.begin(), plottedSeries.end()}));
                    plottedSeries.clear();
                });
                ImGui::EndDisabled(); ImGui::EndTable();
            }
            if (ImGui::Button("OK")) Attempt([&] {
                if (!availableColumns.empty()) AddAvailablePlots();
                graphDialog = false; ImGui::CloseCurrentPopup();
            });
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { graphDialog = false; ImGui::CloseCurrentPopup(); }
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
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
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
                        auto snapshot = pl::snapshotGraph(p, exportGraph, state->views.at(exportGraph), std::array<double,2>{width, height});
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
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                exportGraph = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextWrapped("%s", state->status.c_str());
            ImGui::EndPopup();
        }
    }
} // namespace SST::Prototypes::PlotLab

