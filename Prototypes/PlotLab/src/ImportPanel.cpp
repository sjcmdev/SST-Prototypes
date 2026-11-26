#include "ImportPanel.hpp"
#include "PlotLabWorkspace.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <algorithm>

namespace SST::Prototypes::PlotLab {
namespace {
namespace widgets=sst::ui::widgets;
std::string utf8(const std::filesystem::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
void tablePreview(const char* id,const pl::Table& t) {
    if(t.columns.empty())return;
    const auto columns=std::min<std::size_t>(16,t.columns.size());
    if(columns<t.columns.size())ImGui::Text("First %zu of %zu columns; mapping and validation cover the complete schema.",columns,t.columns.size());
    if(ImGui::BeginTable(id,static_cast<int>(columns),ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX,ImVec2(0,160))) {
        for(std::size_t c=0;c<columns;++c)ImGui::TableSetupColumn((t.names[c]+" ["+t.columns[c].units+"]").c_str());
        ImGui::TableHeadersRow();
        const auto count=std::min<std::size_t>(5,std::visit([](const auto& v){return v.size();},t.columns.front().values));
        for(std::size_t row=0;row<count;++row){ImGui::TableNextRow();for(std::size_t c=0;c<columns;++c){ImGui::TableSetColumnIndex(static_cast<int>(c));std::visit([&](const auto& v){using T=typename std::decay_t<decltype(v)>::value_type;if constexpr(std::is_same_v<T,double>)ImGui::Text("%.8g",v[row]);else ImGui::TextUnformatted(v[row].c_str());},t.columns[c].values);}}
        ImGui::EndTable();
    }
}
}
ImportPanel::ImportPanel(Session& session):state(session) {
    const auto& ui=state.document->importUi;recipe=ui.recipe;paths=ui.paths;open=ui.open;
    if(auto it=state.document->importPresets.find(recipe.name);it!=state.document->importPresets.end() && it->second==recipe)selectedPreset=recipe.name;
    if(open)message="Import settings restored. Preview explicitly to validate files; Python hooks require trust.";
}
void ImportPanel::Open(std::vector<std::filesystem::path> sources) {
    paths=std::move(sources);
    lastValid.clear();lastPreview.reset();lastResult.reset();
    open=true;Changed();
}
void ImportPanel::Changed() {stale=true;refresh=true;due=ImGui::GetTime()+.25;auto& doc=*state.document;doc.CancelImport();doc.importUi={recipe,paths,open};doc.importPresetsDirty=true;}
void ImportPanel::Preview() {
    ImportRequest request;request.recipe=recipe;request.paths=paths;
    state.document->ImportCommand(std::move(request));committing=false;refresh=false;message.clear();
}
void ImportPanel::Render() {
    // Explorer clones share the session's one import window.
    if(!open || renderedFrame==ImGui::GetFrameCount())return;
    renderedFrame=ImGui::GetFrameCount();
    auto& doc=*state.document;
    auto result=doc.PollImport();const bool running=result && result->running;
    if(!running && refresh && ImGui::GetTime()>=due)try{Preview();result=doc.PollImport();}catch(const std::exception& e){message=e.what();refresh=false;}
    if(result && !result->running && result!=lastResult) {
        lastResult=result;
        for(const auto& item:result->items)if(item.plan && item.plan->ready() && item.diagnostics.empty())lastValid[item.plan->path]=item.plan->table;
        if(!committing && !refresh && !result->cancelled && result->items.size()==result->total &&
           !result->items.empty() && result->items.front().plan && result->items.front().plan->recipe==recipe) {lastPreview=result;stale=false;}
        if(committing)message="Import finished. Open results from Project Explorer; active worksheet unchanged.";
    }
    ImGui::SetNextWindowSize(ImVec2(950,750),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Import data",&open)){ImGui::End();if(!open){doc.CancelImport();refresh=false;doc.importUi.open=false;doc.importPresetsDirty=true;}return;}
    ImGui::PushID(this);
    try {
        auto checkPresetName=[&] {
            if(recipe.name.find_first_not_of(" \t\r\n")==std::string::npos)throw std::invalid_argument("Preset name cannot be empty");
            if(selectedPreset!=recipe.name && doc.importPresets.contains(recipe.name))throw std::invalid_argument("Preset name already exists; select it to update, or choose a different name");
        };
        if(!paths.empty() && ImGui::BeginTable("inputs",2,ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Input files");ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthFixed,70);ImGui::TableHeadersRow();
            for(std::size_t i=0;i<paths.size();++i){ImGui::PushID(static_cast<int>(i));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                auto relative=paths[i].lexically_relative(doc.path.empty()?std::filesystem::current_path():doc.path.parent_path());ImGui::TextUnformatted(utf8(relative.empty()?paths[i].filename():relative).c_str());
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",utf8(paths[i]).c_str());ImGui::TableSetColumnIndex(1);
                const bool remove=ImGui::SmallButton("Remove");ImGui::PopID();if(remove){lastValid.erase(utf8(paths[i]));paths.erase(paths.begin()+i);Changed();break;}
            }ImGui::EndTable();
        }
        if(ImGui::Button("Add files...")) {
            auto selected=OpenFilesDialog({"Delimited data","csv,tsv,txt,dat"});
            for(const auto& path:selected)paths.push_back(path.Native());if(!selected.empty())Changed();
        }
        if(widgets::BeginCombo("Preset",recipe.name.c_str(),0,"Named dialect, mapping and five hook stages; selecting does not grant trust.")) {
            int presetIndex=0;for(const auto& [name,preset]:doc.importPresets){ImGui::PushID(presetIndex++);if(ImGui::Selectable(name.c_str(),name==recipe.name)){recipe=preset;selectedPreset=name;Changed();}ImGui::PopID();}
            widgets::EndCombo();
        }
        if(widgets::Text("Preset name",&recipe.name,0,"Name used to select and store this recipe."))Changed();
        if(widgets::Text("Recipe version",&recipe.version,0,"Change version when the scientific meaning changes."))Changed();
        if(!doc.ImportRecipeHistory().empty() && ImGui::CollapsingHeader("Accepted recipe history")) {
            const auto selectedFingerprint=pl::importRecipeFingerprint(recipe);
            for(const auto& [fingerprint,accepted]:doc.ImportRecipeHistory()){ImGui::PushID(fingerprint.c_str());
                if(ImGui::Selectable((accepted->name+" @ "+accepted->version).c_str(),selectedFingerprint==fingerprint)){recipe=*accepted;selectedPreset=doc.importPresets.contains(recipe.name)?recipe.name:"";Changed();}
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\nSelect to inspect/reuse; loading does not grant Python trust.",fingerprint.c_str());ImGui::PopID();
            }
        }
        if(ImGui::Button("Store / rename preset")){checkPresetName();if(!selectedPreset.empty() && selectedPreset!=recipe.name)doc.importPresets.erase(selectedPreset);doc.importPresets[recipe.name]=recipe;selectedPreset=recipe.name;doc.importPresetsDirty=true;message="Preset stored; Save project or Save presets file to persist.";}
        ImGui::SameLine();if(ImGui::Button("Duplicate preset")) {auto base=recipe.name+" copy";auto candidate=base;for(unsigned i=2;doc.importPresets.contains(candidate);++i)candidate=base+" "+std::to_string(i);recipe.name=candidate;selectedPreset=candidate;doc.importPresets[candidate]=recipe;doc.importPresetsDirty=true;Changed();}
        ImGui::SameLine();if(ImGui::Button("Save presets..."))if(auto path=SaveFileDialog({"Import presets","json"},"presets.plotimport.json")){checkPresetName();auto presets=doc.importPresets;presets[recipe.name]=recipe;SaveImportPresets(path->Native(),presets);doc.importPresets=std::move(presets);selectedPreset=recipe.name;doc.importPresetsDirty=true;message="Preset file saved.";}
        ImGui::SameLine();if(ImGui::Button("Load presets..."))if(auto path=OpenFileDialog({"Import presets","json"})){auto loaded=LoadImportPresets(path->Native());for(auto& [name,preset]:loaded)doc.importPresets[name]=std::move(preset);doc.importPresetsDirty=true;message="Definitions loaded. Select a preset; hook trust must be granted explicitly.";}
        const char separators[]={0,',',';','\t',' ','|'},decimals[]={0,'.',','};int delimiter=0,decimal=0,header=static_cast<int>(recipe.options.header),units=static_cast<int>(recipe.options.units);
        for(int i=0;i<6;++i)if(separators[i]==recipe.options.delimiter)delimiter=i;
        for(int i=0;i<3;++i)if(decimals[i]==recipe.options.decimal)decimal=i;
        bool changed=widgets::Combo("Delimiter",&delimiter,"Detect\0Comma\0Semicolon\0Tab\0Whitespace\0Pipe\0",-1,"Detected settings are shown in each parsed preview; override ambiguous dialects.");
        changed|=widgets::Combo("Decimal",&decimal,"Detect\0Point\0Comma\0",-1,"Decimal comma requires a different delimiter, or quoted comma values.");
        changed|=widgets::Combo("Header",&header,"Detect\0Present\0Absent\0");changed|=widgets::Combo("Units row",&units,"Detect\0Present\0Absent\0");
        int preamble=static_cast<int>(recipe.preambleLines);changed|=widgets::Int("Preamble (lines)",&preamble,1,0,100000,"Physical lines removed after pre_parse. Ctrl+click for exact count.");
        if(changed){recipe.options.delimiter=separators[delimiter];recipe.options.decimal=decimals[decimal];recipe.options.header=static_cast<pl::RowMode>(header);recipe.options.units=static_cast<pl::RowMode>(units);recipe.preambleLines=static_cast<std::size_t>(std::max(0,preamble));Changed();}
        if(lastPreview && !lastPreview->items.empty() && lastPreview->items.front().plan && ImGui::Button("Use displayed column names / roles")) {
            const auto& table=lastPreview->items.front().plan->parsedPreview;recipe.mapping.clear();
            for(std::size_t c=0;c<table.columns.size();++c)recipe.mapping.push_back({table.names[c],table.names[c],table.columns[c].units,table.columns[c].role,true,table.columns[c].values.index()==0});Changed();
        }
        if(ImGui::CollapsingHeader("Column mapping",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Match exact source names, never positions. Required columns and units are checked for every input. Optional columns may be absent; conversion requires an explicit hook.");
            if(ImGui::Button("Add mapping")){recipe.mapping.emplace_back();Changed();}
            if(!recipe.mapping.empty() && ImGui::BeginTable("mapping",7,ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
                for(auto label:{"Source column","Output name","Expected units","Role","Required","Numeric","Action"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
                for(std::size_t i=0;i<recipe.mapping.size();++i){ImGui::PushID(static_cast<int>(i));auto& m=recipe.mapping[i];ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);ImGui::SetNextItemWidth(-1);bool edit=ImGui::InputText("##source",&m.sourceName);
                    ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);edit|=ImGui::InputText("##name",&m.outputName);
                    ImGui::TableSetColumnIndex(2);ImGui::SetNextItemWidth(-1);edit|=ImGui::InputText("##units",&m.units);
                    ImGui::TableSetColumnIndex(3);ImGui::SetNextItemWidth(-1);int role=static_cast<int>(m.role);edit|=ImGui::Combo("##role",&role,"None\0X\0Y\0Z\0yErr\0Label\0xErr\0Disregard\0");m.role=static_cast<pl::Role>(role);
                    ImGui::TableSetColumnIndex(4);edit|=ImGui::Checkbox("##required",&m.required);ImGui::TableSetColumnIndex(5);edit|=ImGui::Checkbox("##numeric",&m.numeric);
                    ImGui::TableSetColumnIndex(6);const bool remove=ImGui::SmallButton("Remove##mapping");
                    if(edit)Changed();ImGui::PopID();
                    if(remove){recipe.mapping.erase(recipe.mapping.begin()+i);Changed();break;}
                }ImGui::EndTable();
            }
        }
        if(ImGui::CollapsingHeader("Python hooks")) {
            ImGui::TextWrapped("Preview runs the enabled stages below on complete input. Import publishes that accepted result; it does not rerun hooks or post-actions. Python has your worksheet permissions; no sandbox.");
            ImGui::TextWrapped("Column/post_import numeric arrays are writable. Validation/naming receive read-only NumPy views; transformations belong in column/post_import.");
            const char* signatures[]={"hook(data: bytes, context, settings) -> bytes | UTF-8 str","hook(column: dict, context, settings) -> column dict","hook(table: dict, context, settings) -> table dict","hook(table, context, settings) -> None; raise to reject","hook(table, name, context, settings) -> str"};
            for(int i=0;i<5;++i){ImGui::PushID(i);if(ImGui::TreeNode(pl::importStageNames[i])){auto& h=recipe.hooks[i];ImGui::TextWrapped("def %s",signatures[i]);
                ImGui::TextWrapped("context: path, fingerprint, recipe, recipe_version, preview. settings: numbers, strings. column: name, units, role (0..7), numeric, values. table: columns, source. A raised exception rejects only this item.");
                bool edit=widgets::Text("Source reference",&h.reference);edit|=widgets::Text("Hook version",&h.version);edit|=widgets::MultilineText("Python source",&h.source,ImVec2(-1,110));
                ImGui::PushID("numeric_settings");int numberIndex=0;for(auto& [name,value]:h.numbers){ImGui::PushID(numberIndex++);edit|=widgets::Double(name.c_str(),&value,.001f,"%.8g","Typed numeric hook setting; Ctrl+click for exact value.");ImGui::PopID();}ImGui::PopID();
                ImGui::PushID("text_settings");int stringIndex=0;for(auto& [name,value]:h.strings){ImGui::PushID(stringIndex++);edit|=widgets::Text(name.c_str(),&value);ImGui::PopID();}ImGui::PopID();
                if(ImGui::Button("Add numeric setting")){std::string name="value";for(unsigned n=2;h.numbers.contains(name);++n)name="value"+std::to_string(n);h.numbers[name]=1;edit=true;}
                ImGui::SameLine();if(ImGui::Button("Add text setting")){std::string name="text";for(unsigned n=2;h.strings.contains(name);++n)name="text"+std::to_string(n);h.strings[name]="";edit=true;}
                if(edit)Changed();ImGui::TreePop();}ImGui::PopID();}
            if(pl::hasImportHooks(recipe) && !doc.ImportRecipeTrusted(recipe) && ImGui::Button("Trust this recipe's Python hooks")){doc.TrustImportRecipe(recipe);Changed();}
        }
        const auto current=doc.PollImport();const bool busy=current && current->running;
        ImGui::Text("%s | %zu / %zu inputs | %s",busy?(committing?"Importing":"Previewing"):"Idle",current?current->items.size():0,current?current->total:0,stale?"Preview stale; last result retained":"Preview current");
        if(busy){if(ImGui::Button("Stop")){doc.CancelImport();refresh=false;stale=true;}}
        else {if(ImGui::Button("Preview")){refresh=true;due=0;}}
        ImGui::SameLine();
        const bool canImport=lastPreview && !stale && !busy && lastPreview->project==doc.project->id() && lastPreview->revision==doc.project->revision() &&
            std::any_of(lastPreview->items.begin(),lastPreview->items.end(),[](const auto& item){return item.plan && item.plan->ready() && item.diagnostics.empty();});
        ImGui::BeginDisabled(!canImport);if(ImGui::Button("Import accepted items")){doc.CommitImport(lastPreview);committing=true;stale=true;}ImGui::EndDisabled();
        if(!canImport && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Preview explicit mapping and resolve trust/units/name errors first. Any source/settings/project change requires a fresh preview.");
        if(lastPreview)for(std::size_t i=0;i<lastPreview->items.size();++i){const auto& item=lastPreview->items[i];if(!item.plan)continue;const auto& p=*item.plan;ImGui::PushID(static_cast<int>(i));
            auto path=std::filesystem::u8path(p.path);auto relative=path.lexically_relative(doc.path.empty()?std::filesystem::current_path():doc.path.parent_path());
            if(ImGui::TreeNodeEx((utf8(relative.empty()?path.filename():relative)+" -> "+p.outputName).c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",p.path.c_str());ImGui::TextUnformatted(p.fingerprint.c_str());
                bool first=true;for(auto key:{"delimiter","decimal","header","units"})if(auto it=p.parsedPreview.source.find(key);it!=p.parsedPreview.source.end()){if(!first)ImGui::SameLine();first=false;ImGui::Text("%s: %s",key,it->second=="\t"?"Tab":it->second==" "?"Whitespace":it->second.c_str());}
                for(const auto& note:p.parsedPreview.diagnostics)ImGui::TextWrapped("%s",note.c_str());
                auto diagnostic=[](const pl::ImportDiagnostic& e){ImGui::TextWrapped("%s / %s / %s: %s",pl::importErrorNames[static_cast<unsigned>(e.code)],pl::importStageNames[static_cast<unsigned>(e.stage)],e.column.c_str(),e.message.c_str());};
                for(const auto& e:p.diagnostics)diagnostic(e);for(const auto& e:item.diagnostics)diagnostic(e);
                for(auto stage:p.executedHooks){ImGui::Text("Executed: %s",pl::importStageNames[static_cast<unsigned>(stage)]);}
                if(ImGui::TreeNode("Raw text")){ImGui::TextUnformatted(p.rawPreview.c_str());ImGui::TreePop();}
                ImGui::TextUnformatted("Parsed (first 5 rows; full file validated)");tablePreview("parsed",p.parsedPreview);
                auto previous=lastValid.find(p.path);const auto table=p.table?p.table:previous==lastValid.end()?nullptr:previous->second;
                if(table){ImGui::TextUnformatted(p.table?"Transformed (first 5 rows)":"Last valid transformed preview (stale)");tablePreview("transformed",*table);}ImGui::TreePop();
            }ImGui::PopID();}
        auto summary=busy?current:lastResult;
        if(committing && summary)for(const auto& item:summary->items){if(item.worksheet)ImGui::Text("Worksheet %llu%s",static_cast<unsigned long long>(item.worksheet),item.reused?" (existing result)":"");for(const auto& e:item.diagnostics)ImGui::TextWrapped("%s: %s",pl::importErrorNames[static_cast<unsigned>(e.code)],e.message.c_str());}
        if(!message.empty())ImGui::TextWrapped("%s",message.c_str());
    } catch(const std::exception& e){message=e.what();ImGui::TextWrapped("%s",message.c_str());}
    ImGui::PopID();ImGui::End();
    if(!open){doc.CancelImport();refresh=false;doc.importUi.open=false;doc.importPresetsDirty=true;}
}
}
