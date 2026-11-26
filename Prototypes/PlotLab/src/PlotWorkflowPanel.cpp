#include "PlotWorkflowPanel.hpp"
#include "PlotLabWorkspace.hpp"
#include "PlotTypesPanel.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <imgui.h>
#include <numeric>

namespace SST::Prototypes::PlotLab {
namespace w=sst::ui::widgets;
PlotWorkflowPanel::PlotWorkflowPanel(Session& s):session(s),analysisRevision(s.document->Workflow().panel.StateRevision()) {calibration.xUnit=calibration.yUnit="px";calibration.zUnit="counts";calibration.spec.colorScale=true;}
void PlotWorkflowPanel::OpenMap(pl::Id sheet) {
    mapLayer=0;map={};map.worksheet=sheet;map.spec.colorScale=true;map.spec.colormap="Viridis";
    const auto columns=session.document->project->children(sheet);if(columns.size()>=3) {map.x=columns[0];map.y=columns[1];map.z=columns[2];}
    map.gridColumns=2;mapOpen=true;session.document->Workflow().PreviewMap(map);
}
void PlotWorkflowPanel::EditMap(pl::Id graph) {
    const auto& p=*session.document->project;auto layer=p.children(graph).front();const auto plots=p.children(layer);
    if(plots.size()!=1)throw std::invalid_argument("Select a single-series heatmap layer");const auto& source=std::get<pl::Plot>(p.object(plots.front()).data);
    if(source.kind!=plot::SeriesKind::Heatmap)throw std::invalid_argument("Select a heatmap graph");
    map={};map.worksheet=source.worksheet;map.x=source.x;map.y=source.y;map.z=source.z;map.gridColumns=source.gridColumns;map.mask=source.mask;map.spec=std::get<pl::Layer>(p.object(layer).data).spec;map.name=p.object(graph).name;mapLayer=layer;mapOpen=true;session.document->Workflow().PreviewMap(map);
}
void PlotWorkflowPanel::OpenAnalysis(pl::Id sheet) {
    auto& panel=session.document->Workflow().panel;auto recipe=panel.Recipe();
    if(sheet && recipe.worksheet!=sheet) {recipe={};recipe.worksheet=sheet;recipe.id="analysis-"+std::to_string(sheet);recipe.name="Scientific analysis";
        const auto columns=session.document->project->children(sheet);if(columns.size()>=2){recipe.xColumn=columns[0];recipe.yColumn=columns[1];}}
    panel.SetRecipe(std::move(recipe));panel.open=true;
}
void PlotWorkflowPanel::Render() {
    if(frame==ImGui::GetFrameCount())return;frame=ImGui::GetFrameCount();
    auto& workflow=session.document->Workflow();workflow.PollBatch();workflow.panel.Draw();Maps();Templates();Data();Image();
    if(workflow.panel.StateRevision()!=analysisRevision || workflow.panel.open!=session.document->workflowState.analysisOpen) {session.document->workflowDirty=true;analysisRevision=workflow.panel.StateRevision();session.document->workflowState.analysisOpen=workflow.panel.open;}
}
void PlotWorkflowPanel::Maps() {
    if(!mapOpen)return;ImGui::SetNextWindowSize({620,780},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Map / contour",&mapOpen)) {
        auto& workflow=session.document->Workflow();const auto& status=workflow.PollMap();
        auto edit=DrawPlotTypesPanel(*session.document->project,map,{},!mapLayer);
        if(edit.changed)Attempt([&]{workflow.PreviewMap(map);});
        if(edit.apply)Attempt([&]{if(mapLayer){session.document->Execute(pl::editMapPlot(*session.document->project,mapLayer,map.spec));session.views[session.document->project->object(mapLayer).parent].dataChanged=true;}else {auto graph=workflow.CreateMap(map);session.Show(graph);}mapOpen=false;});
        if(edit.cancel)mapOpen=false;
        ImGui::TextWrapped("%s",message.empty()?status.message.c_str():message.c_str());
        if(status.project && status.graph) {
            const auto layer=status.project->children(status.graph).front();const auto& spec=std::get<pl::Layer>(status.project->object(layer).data).spec;
            std::vector<plot::SeriesEntryView> entries;for(auto id:status.project->children(layer))entries.push_back(status.project->series(id));
            mapView.dataChanged=edit.changed;plot::PreviewMetrics metrics;metrics.interactive=true;
            plot::RenderPlot("Map preview",spec,entries.data(),entries.size(),mapView,metrics,{0,260});
        }
    }ImGui::End();
}
void PlotWorkflowPanel::Templates() {
    if(!templatesOpen)return;ImGui::SetNextWindowSize({660,600},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Graph templates / batch",&templatesOpen)) {
        if(ImGui::Button("Capture selected graph"))Attempt([&]{graphTemplate=std::make_shared<const pl::GraphTemplate>(pl::captureGraphTemplate(*session.document->project,session.selected));templateBindings.clear();plan.reset();});
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Select a graph in Project Explorer, then capture its construction and style without copying data.");
        ImGui::SameLine();if(ImGui::Button("Load template..."))Attempt([&]{if(auto path=OpenFileDialog({"Graph template","plottemplate"})){graphTemplate=std::make_shared<const pl::GraphTemplate>(pl::loadGraphTemplate(path->Native()));templateBindings.clear();plan.reset();}});
        if(graphTemplate) {
            ImGui::Text("Template: %s (v%u)",graphTemplate->name.c_str(),graphTemplate->version());
            if(ImGui::Button("Save template..."))Attempt([&]{if(auto path=SaveFileDialog({"Graph template","plottemplate"},"graph.plottemplate"))pl::saveGraphTemplate(*graphTemplate,path->Native());});
            if(w::Combo("Export format",&batchFormat,"PNG\0JPEG\0SVG\0PDF\0"))plan.reset();
            if(ImGui::Button("Output directory..."))Attempt([&]{if(auto path=SaveFileDialog({"Choose a filename in output directory","png"},"batch.png")){outputDirectory=path->Native().parent_path();plan.reset();}});
            if(!outputDirectory.empty())ImGui::TextWrapped("Output: %s",Path(outputDirectory.filename()).Utf8().c_str());
            for(const auto& [id,o]:session.document->project->objects())if(std::holds_alternative<pl::Worksheet>(o->data)) {ImGui::PushID(static_cast<int>(id));bool selected=batchSheets.contains(id);if(w::Checkbox(o->name.c_str(),&selected,"Select this stable worksheet as a template batch input.")){if(selected)batchSheets.insert(id);else batchSheets.erase(id);plan.reset();}ImGui::PopID();}
            for(auto sheet:batchSheets)if(session.document->project->objects().contains(sheet)) {
                ImGui::PushID(static_cast<int>(sheet));ImGui::Text("Mapping: %s",session.document->project->object(sheet).name.c_str());
                for(const auto& ref:graphTemplate->columns) {ImGui::PushID(ref.key.c_str());auto& id=templateBindings[sheet][ref.key];const auto& p=*session.document->project;const auto caption=p.objects().contains(id)?p.object(id).name:"Automatic by name / role / units";
                    if(w::BeginCombo(ref.name.c_str(),caption.c_str(),0,"Explicit stable column binding; required roles and units are validated at preflight.")) {
                        if(ImGui::Selectable("Automatic",!id)){id=0;plan.reset();}
                        for(auto column:p.children(sheet)){ImGui::PushID(static_cast<int>(column));if(ImGui::Selectable(p.object(column).name.c_str(),id==column)){id=column;plan.reset();}ImGui::PopID();}w::EndCombo();}
                    ImGui::PopID();
                }ImGui::PopID();
            }
            if(ImGui::Button("Preflight mapping / destinations"))Attempt([&]{
                static const char* formats[]={"png","jpg","svg","pdf"};std::vector<pl::BatchPlotInput> inputs;
                for(auto sheet:batchSheets) {std::map<std::string,pl::Id> overrides;for(const auto& [key,id]:templateBindings[sheet])if(id)overrides.emplace(key,id);inputs.push_back({sheet,outputDirectory.empty()?std::filesystem::path{}:outputDirectory/("sheet-"+std::to_string(sheet)+"."+formats[batchFormat]),std::move(overrides)});}
                plan=pl::planBatchPlots(*session.document->project,graphTemplate,inputs);
            });
            if(plan) {
                for(const auto& item:plan->items) {ImGui::Text("Sheet %llu -> %s",static_cast<unsigned long long>(item.worksheet),Path(item.destination.filename()).Utf8().c_str());for(const auto& [key,id]:item.mapping.columns)ImGui::Text("  %s = %llu",key.c_str(),static_cast<unsigned long long>(id));for(const auto& issue:item.mapping.issues)ImGui::TextWrapped("%s",issue.message.c_str());}
                auto& workflow=session.document->Workflow();const auto& status=workflow.PollBatch();
                ImGui::BeginDisabled(!plan->valid() || plan->items.empty() || status.running);
                if(ImGui::Button("Run template batch"))Attempt([&]{static const char* formats[]={"png","jpg","svg","pdf"};workflow.StartBatch(*plan,formats[batchFormat],session.pythonRoot);});ImGui::EndDisabled();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Resolve required mappings and output collisions; wait for the current batch to finish.");
                ImGui::SameLine();if(ImGui::Button("Cancel batch"))workflow.CancelBatch();
                ImGui::Text("Published graphs: %zu",status.graphs.size());for(auto graph:status.graphs){ImGui::PushID(static_cast<int>(graph));if(ImGui::SmallButton("Show graph"))session.Show(graph);ImGui::PopID();}
                for(const auto& error:status.errors)ImGui::TextWrapped("%s",error.c_str());
            }
        }
        ImGui::TextWrapped("%s",message.c_str());
    }ImGui::End();
}
void PlotWorkflowPanel::Data() {
    if(!dataOpen)return;ImGui::SetNextWindowSize({560,540},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Export worksheet data",&dataOpen)) {
        auto& p=*session.document->project;
        if(p.objects().contains(dataSheet)) {
            for(auto id:p.children(dataSheet)){ImGui::PushID(static_cast<int>(id));bool selected=dataColumns.contains(id);if(w::Checkbox(p.object(id).name.c_str(),&selected,"Export this column with its name and units.")){if(selected)dataColumns.insert(id);else dataColumns.erase(id);}ImGui::PopID();}
            w::Combo("Format",&dataFormat,"CSV\0TSV\0DAT\0");w::Combo("Rows",&rowScope,"All rows\0Visible rows\0Selected rows\0");
            w::Combo("Masks",&maskPolicy,"Preserve flags\0Write missing\0Omit masked rows\0");w::Combo("Missing values",&missingPolicy,"Empty\0NaN\0");
            if(ImGui::Button("Export data..."))Attempt([&]{
                pl::DataExportSelection selection;selection.worksheet=dataSheet;selection.columns={dataColumns.begin(),dataColumns.end()};selection.masks=static_cast<pl::DataMaskPolicy>(maskPolicy);selection.missing=static_cast<pl::DataMissingPolicy>(missingPolicy);
                const auto& sheet=std::get<pl::Worksheet>(p.object(dataSheet).data);
                if(rowScope==0 || (rowScope==1 && sheet.filters.empty())){selection.rows.resize(sheet.rows.size());std::iota(selection.rows.begin(),selection.rows.end(),0);}
                else if(rowScope==1)selection.rows=sheet.visibleRows;
                else {if(p.selection().worksheet!=dataSheet || p.selection().rows.empty())throw std::invalid_argument("Select rows in this worksheet first");for(auto row:p.selection().rows){auto it=std::find(sheet.rows.begin(),sheet.rows.end(),row);if(it!=sheet.rows.end())selection.rows.push_back(it-sheet.rows.begin());}}
                static const char* formats[]={"csv","tsv","dat"};if(auto path=SaveFileDialog({"Worksheet data",formats[dataFormat]},("data."+std::string(formats[dataFormat])).c_str())) {
                    ExportRequest request;request.data=pl::snapshotData(p,selection);request.destination=path->Native();request.format=formats[dataFormat];request.pythonRoot=session.pythonRoot;session.exporting=session.document->Workflow().Export(std::move(request));dataOpen=false;
                }
            });
        }ImGui::TextWrapped("%s",message.c_str());
    }ImGui::End();
}
void PlotWorkflowPanel::Image() {
    if(!imageOpen)return;ImGui::SetNextWindowSize({620,540},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Calibrated image map",&imageOpen)) {
        if(ImGui::Button("Choose intensity image..."))Attempt([&]{if(auto path=OpenFileDialog({"Intensity image","png,tif,tiff,jpg,jpeg"}))imageFile=path->Native();});
        ImGui::TextUnformatted(Path(imageFile.filename()).Utf8().c_str());
        w::Double(("First X centre ["+calibration.xUnit+"]").c_str(),&calibration.firstX);w::Double(("First Y centre ["+calibration.yUnit+"]").c_str(),&calibration.firstY);w::Double(("Pixel step X ["+calibration.xUnit+"]").c_str(),&calibration.stepX);w::Double(("Pixel step Y ["+calibration.yUnit+"]").c_str(),&calibration.stepY);
        w::Text("X units",&calibration.xUnit);w::Text("Y units",&calibration.yUnit);w::Text("Intensity units",&calibration.zUnit);
        ImGui::TextWrapped("Rows retain image order. Set negative Y step for a downward image axis. Multichannel images require an explicit intensity channel before import.");
        if(ImGui::Button("Create image map"))Attempt([&]{auto grid=LoadIntensityImage(imageFile,calibration);auto graph=session.document->Workflow().CreateImage(std::move(grid));session.Show(graph);imageOpen=false;});
        ImGui::TextWrapped("%s",message.c_str());
    }ImGui::End();
}
}
