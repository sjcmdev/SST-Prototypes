#include "AnalysisPanel.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace SST::Prototypes::PlotLab {
namespace w=sst::ui::widgets;
namespace {
bool ParameterControl(const char* label,double& value,const std::string& units) {
    const auto tip="Physical value ["+units+"]. Drag mantissa/exponent; Ctrl+click for exact entry.";
    return w::LogValue(label,&value,-100,100,tip.c_str());
}
}
void AnalysisPanel::Run(bool preview) {
    try {
        if(recipe.id.empty())recipe.id="analysis-"+std::to_string(recipe.yColumn);
        recipe.settings.includeCurves=true;
        (void)controller.Submit(recipe,userModel,preview);message.clear();
    } catch(const std::exception& error) {message=error.what();}
}
void AnalysisPanel::Draw() {
    DrawBatch();
    if(!open)return;
    ImGui::PushID(this);
    ImGui::SetNextWindowSize({800,900},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Scientific analysis",&open)) {ImGui::End();ImGui::PopID();return;}
    controller.Poll();bool changed=false;auto& project=*controller.document.project;
    auto picker=[&](const char* label,pl::Id& selected,bool sheet) {
        const auto found=project.objects().find(selected);const auto caption=found==project.objects().end()?"Choose source":found->second->name.c_str();
        if(w::BeginCombo(label,caption,0,"Stable source object ID; raw worksheet values remain unchanged.")) {
            if(!sheet && ImGui::Selectable("None",selected==0)) {selected=0;changed=true;}
            for(const auto& [id,object]:project.objects()) {
                const auto* column=std::get_if<pl::Column>(&object->data);
                if(sheet?!std::holds_alternative<pl::Worksheet>(object->data):!column || object->parent!=recipe.worksheet || column->values.index()!=0)continue;
                ImGui::PushID(static_cast<int>(id));
                if(ImGui::Selectable(object->name.c_str(),selected==id)) {selected=id;changed=true;if(sheet)recipe.xColumn=recipe.yColumn=recipe.sigmaColumn=0;}
                ImGui::PopID();
            }
            w::EndCombo();
        }
    };
    picker("Worksheet",recipe.worksheet,true);picker("Physical X",recipe.xColumn,false);picker("Signal Y",recipe.yColumn,false);
    const char* names[]={"Linear fit","Exponential + baseline","Gaussian + baseline","User model","Interpolate","Mean (3 points)","Savitzky-Golay (5 points)","Constant baseline","Linear baseline","Derivative dY/dX","Cumulative integral","Peak locations / FWHM"};
    auto operation=static_cast<int>(recipe.settings.operation);
    if(w::Combo("Operation",&operation,names,static_cast<int>(std::size(names)),-1,"Choose the scientific operation; physical X and units are retained.")) {
        recipe.settings.operation=static_cast<pl::ScientificOperation>(operation);recipe.settings.parameters.clear();recipe.settings.anchors.clear();recipe.settings.targetX.clear();recipe.settings.weights=pl::WeightSemantics::Unweighted;changed=true;
    }
    auto units=[&](pl::Id id) {auto it=project.objects().find(id);return it!=project.objects().end() && std::holds_alternative<pl::Column>(it->second->data)?std::get<pl::Column>(it->second->data).units:std::string{};};
    const bool fit=recipe.settings.operation<=pl::ScientificOperation::UserFit;
    if(fit) {
        auto weights=static_cast<int>(recipe.settings.weights);
        changed|=w::Combo("Weights",&weights,"Unweighted\0Absolute Y sigma\0Relative Y sigma\0",-1,"Sigma is a positive standard deviation in Y units. Relative covariance is scaled by objective / degrees of freedom.");
        recipe.settings.weights=static_cast<pl::WeightSemantics>(weights);
        if(weights)picker("Sigma Y",recipe.sigmaColumn,false);
        if(recipe.settings.parameters.empty())recipe.settings.parameters=pl::defaultAnalysisParameters(recipe.settings.operation,units(recipe.xColumn),units(recipe.yColumn));
        if(recipe.settings.operation==pl::ScientificOperation::UserFit) {
            ImGui::Text("User model: %s (v%u)",recipe.settings.userModelId.c_str(),recipe.settings.modelVersion);
            ImGui::BeginDisabled(!controller.document.PythonTrusted());
            if(ImGui::Button("Load Python model..."))try{if(auto path=OpenFileDialog({"Named Python model","py"})){auto next=recipe;auto model=PythonBinding::LoadUserAnalysisModel(controller.document,path->Native(),next);recipe=std::move(next);userModel=std::move(model);changed=true;message.clear();}}catch(const std::exception& e){message=e.what();}
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Trust this project in the worksheet Python tools first. The file defines a NamedModel called model and explicit parameters. It runs only when you load it.");
        }
        for(std::size_t i=0;i<recipe.settings.parameters.size();++i) {
            auto& p=recipe.settings.parameters[i];ImGui::PushID(static_cast<int>(i));
            changed|=ParameterControl((p.name+" ["+p.units+"]").c_str(),p.start,p.units);
            changed|=w::Checkbox("Fixed",&p.fixed,"Keep this named parameter at its start value.");
            if(!p.fixed) {changed|=ParameterControl("Lower bound",p.lower,p.units);changed|=ParameterControl("Upper bound",p.upper,p.units);}
            ImGui::PopID();
        }
    }
    for(int side=0;side<2;++side) {
        auto& bound=side?recipe.settings.xTo:recipe.settings.xFrom;bool enabled=bound.has_value();
        const char* label=side?"Limit X to":"Limit X from";
        if(w::Checkbox(label,&enabled,"Inclusive physical X range; all samples are used when disabled.")) {bound=enabled?std::optional<double>{0}:std::nullopt;changed=true;}
        if(enabled) {ImGui::PushID(side);changed|=ParameterControl("X [physical units]",*bound,units(recipe.xColumn));ImGui::PopID();}
    }
    const auto op=recipe.settings.operation;
    if(op==pl::ScientificOperation::Peaks) {recipe.settings.anchors.resize(1);changed|=ParameterControl("Peak baseline Y",recipe.settings.anchors[0].second,units(recipe.yColumn));}
    if(op==pl::ScientificOperation::ConstantBaseline || op==pl::ScientificOperation::LinearBaseline) {
        recipe.settings.anchors.resize(op==pl::ScientificOperation::ConstantBaseline?1:2);
        for(std::size_t i=0;i<recipe.settings.anchors.size();++i) {
            ImGui::PushID(static_cast<int>(i));changed|=ParameterControl("Anchor X",recipe.settings.anchors[i].first,units(recipe.xColumn));changed|=ParameterControl("Anchor Y",recipe.settings.anchors[i].second,units(recipe.yColumn));ImGui::PopID();
        }
    }
    if(op==pl::ScientificOperation::Interpolate) {
        int count=static_cast<int>(recipe.settings.targetX.size());
        if(w::Int("Target points",&count,1,2,100000,"Evenly spaced target grid spanning the selected physical X range.")) {
            const auto it=project.objects().find(recipe.xColumn);
            if(it!=project.objects().end())if(const auto* c=std::get_if<pl::Column>(&it->second->data))if(const auto* x=std::get_if<std::vector<double>>(&c->values))if(x->size()>1) {
                const double low=recipe.settings.xFrom.value_or(x->front()),high=recipe.settings.xTo.value_or(x->back());
                recipe.settings.targetX.resize(std::max(2,count));for(std::size_t i=0;i<recipe.settings.targetX.size();++i)recipe.settings.targetX[i]=low+(high-low)*i/(recipe.settings.targetX.size()-1);changed=true;
            }
        }
    }
    changed|=w::Checkbox("Exclude filtered rows",&recipe.excludeFiltered,"Exclude filtered rows as well as masked/nonfinite samples.");
    if(w::Checkbox("Auto preview",&recipe.autoPreview,"Recalculate while editing; exact result replaces the coarse preview after release.")) {++stateRevision;if(recipe.autoPreview)changed=true;else exactPending=false;}
    if(w::Checkbox("Show residuals",&recipe.showResiduals,"Show Y minus model in Y units; the fit report also contains sigma-normalized residuals."))++stateRevision;
    if(changed)++stateRevision;
    if(changed && recipe.autoPreview) {exactPending=ImGui::IsAnyItemActive();Run(exactPending);}
    if(exactPending && !ImGui::IsAnyItemActive()) {exactPending=false;Run(false);}
    const bool valid=recipe.worksheet && recipe.xColumn && recipe.yColumn;
    ImGui::BeginDisabled(!valid);if(ImGui::Button("Recalculate"))Run(false);ImGui::EndDisabled();
    if(!valid && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Choose a worksheet, physical X and signal Y first.");
    ImGui::SameLine();const auto& status=controller.Status();const bool canAccept=status.stage==AnalysisStage::PreviewReady && status.exact;
    ImGui::BeginDisabled(!canAccept);if(ImGui::Button("Accept")) {try{controller.Accept();message.clear();}catch(const std::exception& e){message=e.what();}}ImGui::EndDisabled();
    if(!canAccept && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Wait for an exact successful result. Preview is never saved.");
    ImGui::SameLine();ImGui::BeginDisabled(status.stage!=AnalysisStage::Running);if(ImGui::Button("Cancel"))controller.Cancel();ImGui::EndDisabled();
    if(status.stage!=AnalysisStage::Running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("No analysis job is running.");
    ImGui::SameLine();if(ImGui::Button("Batch..."))OpenBatch();
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Apply these versioned settings to explicitly mapped worksheets, with a per-input report and one Accept undo.");
    ImGui::TextUnformatted(message.empty()?controller.Status().message.c_str():message.c_str());
    if(status.preview && !status.preview->items.empty()) {
        const auto& result=*status.preview->items.front().result;
        ImGui::Text("Used: %zu   Rejected: %zu   Objective: %.6g   DoF: %d",result.usedPoints,result.excludedPoints,result.objective,result.degreesOfFreedom);
        if(!result.message.empty())ImGui::TextWrapped("%s",result.message.c_str());
        if(!result.peakX.empty() && ImGui::BeginTable("Peak report",3,ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn(("Position ["+result.xUnits+"]").c_str());ImGui::TableSetupColumn(("Height ["+result.yUnits+"]").c_str());ImGui::TableSetupColumn(("FWHM ["+result.xUnits+"]").c_str());ImGui::TableHeadersRow();
            for(std::size_t i=0;i<result.peakX.size();++i){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%.8g",result.peakX[i]);ImGui::TableNextColumn();ImGui::Text("%.8g",result.peakHeight[i]);ImGui::TableNextColumn();if(std::isfinite(result.peakFwhm[i]))ImGui::Text("%.8g",result.peakFwhm[i]);else ImGui::TextUnformatted("Unavailable");}ImGui::EndTable();
        }
        if(!result.parameters.empty() && ImGui::BeginTable("Fit parameters",3,ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Parameter / unit");ImGui::TableSetupColumn("Value");ImGui::TableSetupColumn("Standard error");ImGui::TableHeadersRow();
            for(std::size_t i=0;i<result.parameters.size();++i) {ImGui::TableNextRow();ImGui::TableNextColumn();const auto& p=result.parameterDefinitions[i];ImGui::Text("%s [%s]",p.name.c_str(),p.units.c_str());ImGui::TableNextColumn();ImGui::Text("%.8g",result.parameters[i]);ImGui::TableNextColumn();if(std::isfinite(result.standardErrors[i]))ImGui::Text("%.5g",result.standardErrors[i]);else ImGui::TextUnformatted("Unavailable");}
            ImGui::EndTable();
        }
    }
    DrawPreview();ImGui::End();ImGui::PopID();
}
void AnalysisPanel::DrawBatch() {
    if(!batchOpen)return;auto& project=*controller.document.project;controller.Poll();
    std::erase_if(batchInputs,[&](const auto& item){return !project.objects().contains(item.first);});
    ImGui::SetNextWindowSize({650,650},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Batch scientific analysis",&batchOpen)) {
        ImGui::Text("Recipe: %s (v%u) / %s",recipe.id.c_str(),recipe.version,pl::scientificOperationIds[static_cast<unsigned>(recipe.settings.operation)]);
        for(const auto& [id,o]:project.objects())if(std::holds_alternative<pl::Worksheet>(o->data)) {
            ImGui::PushID(static_cast<int>(id));bool selected=batchInputs.contains(id);
            if(w::Checkbox(o->name.c_str(),&selected,"Include this stable worksheet. Map physical X, signal Y and optional sigma explicitly.")) {if(selected)batchInputs[id]=id==recipe.worksheet?std::array<pl::Id,3>{recipe.xColumn,recipe.yColumn,recipe.sigmaColumn}:std::array<pl::Id,3>{};else batchInputs.erase(id);batchChecked=false;}
            if(selected)for(int axis=0;axis<(recipe.settings.weights==pl::WeightSemantics::Unweighted?2:3);++axis) {
                const char* label=axis==0?"Physical X":axis==1?"Signal Y":"Sigma Y";auto& chosen=batchInputs[id][axis];auto found=project.objects().find(chosen);
                if(w::BeginCombo(label,found==project.objects().end()?"Choose numeric column":found->second->name.c_str(),0,"Stable numeric column ID; units and range use the main recipe's settings.")) {
                    for(auto column:project.children(id)) {const auto& object=project.object(column);const auto& c=std::get<pl::Column>(object.data);if(c.values.index()!=0)continue;ImGui::PushID(static_cast<int>(column));if(ImGui::Selectable((object.name+" ["+c.units+"]").c_str(),chosen==column)){chosen=column;batchChecked=false;}ImGui::PopID();}w::EndCombo();
                }
            }ImGui::PopID();
        }
        const bool fit=recipe.settings.operation<=pl::ScientificOperation::UserFit;
        if(fit && w::Checkbox("Keep fitted curves",&batchCurves,"Off: retain scalar fit reports only, without full curves/residual arrays."))batchChecked=false;
        auto requests=[&]{std::vector<std::pair<std::string,pl::AnalysisRecipe>> items;for(const auto& [sheet,columns]:batchInputs){auto item=recipe;item.id=recipe.id+"-sheet-"+std::to_string(sheet);item.worksheet=sheet;item.xColumn=columns[0];item.yColumn=columns[1];item.sigmaColumn=columns[2];item.settings.includeCurves=fit?batchCurves:true;items.emplace_back(item.id,std::move(item));}return items;};
        if(ImGui::Button("Check source mapping"))try{if(batchInputs.empty())throw std::invalid_argument("Select at least one worksheet");for(const auto& [id,r]:requests())(void)CaptureAnalysisSource(project,r);checkedProjectRevision=project.revision();checkedSettingsRevision=stateRevision;batchChecked=true;message="Source mapping valid; scientific status is reported separately for every item.";}catch(const std::exception& e){message=e.what();batchChecked=false;}
        const auto& status=controller.Status();const bool ready=batchChecked && checkedProjectRevision==project.revision() && checkedSettingsRevision==stateRevision && status.stage!=AnalysisStage::Running;
        ImGui::SameLine();ImGui::BeginDisabled(!ready);if(ImGui::Button("Run batch"))try{controller.SubmitBatch(requests(),userModel);message.clear();}catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Check every source mapping after changing the recipe or worksheet, and wait for the running analysis.");
        ImGui::SameLine();ImGui::BeginDisabled(status.stage!=AnalysisStage::PreviewReady || !status.exact);if(ImGui::Button("Accept successful items"))try{controller.Accept();}catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Cancel batch"))controller.Cancel();
        if(status.preview)for(const auto& item:status.preview->items)ImGui::TextWrapped("%s: %s; used %zu, rejected %zu; objective %.6g. %s",item.id.c_str(),item.result->ok()?"Ready":"Failed",item.result->usedPoints,item.result->excludedPoints,item.result->objective,item.result->message.c_str());
        ImGui::TextWrapped("%s",message.c_str());
    }ImGui::End();
}
void AnalysisPanel::DrawPreview() {
    const auto& status=controller.Status();std::vector<sst::ui::plot::SeriesEntryView> series;
    const bool changed=drawnPreview.lock()!=status.preview;
    previewView.dataChanged|=changed;residualView.dataChanged|=changed;drawnPreview=status.preview;
    auto append=[&](const std::shared_ptr<const AnalysisJobResult>& results,const char* suffix,bool previous) {
        if(!results)return;
        for(const auto& item:results->items) {
            const auto& r=*item.result;if(!r.ok() || r.x.size()!=r.values.size() || r.x.empty())continue;
            if(previous && status.preview && !status.preview->items.empty() && r.outputUnits!=status.preview->items.front().result->outputUnits)continue;
            plot::SeriesEntryView view;view.id=item.id+suffix;view.data={r.x.data(),r.values.data(),r.x.size()};
            view.style.color=previous?plot::Color{.6f,.6f,.6f,.7f}:plot::Color{.15f,.65f,.95f,1};
            if(previous)view.style.lineStyle=plot::LineStyle::Dashed;
            series.push_back(std::move(view));
        }
    };
    append(status.accepted," accepted",true);append(status.preview," preview",false);
    plot::PlotSpec spec;spec.x.label="Physical X";spec.y.label="Analysis";spec.autoFit=previewView.dataChanged?plot::AutoFit::Always:plot::AutoFit::Once;
    if(status.preview && !status.preview->items.empty()) {
        const auto& item=status.preview->items.front();const auto& r=*item.result;
        spec.x.unit=r.xUnits;spec.y.unit=r.outputUnits;
        if(r.observed.size()==r.x.size() && !r.x.empty() && r.yUnits==r.outputUnits) {
            plot::SeriesEntryView raw;raw.id=item.id+" data";raw.kind=plot::SeriesKind::Scatter;raw.data={r.x.data(),r.observed.data(),r.x.size()};series.push_back(std::move(raw));
        }
        if(!r.peakX.empty()) {plot::SeriesEntryView peaks;peaks.id=item.id+" peaks";peaks.kind=plot::SeriesKind::Scatter;peaks.data={r.peakX.data(),r.peakY.data(),r.peakX.size()};peaks.style.marker=plot::Marker::Diamond;series.push_back(std::move(peaks));}
    }
    plot::PreviewMetrics metrics;metrics.interactive=true;
    if(!series.empty()) {plot::RenderPlot("analysis-preview",spec,series.data(),series.size(),previewView,metrics,{0,220});}
    if(recipe.showResiduals && status.preview && !status.preview->items.empty()) {
        const auto& item=status.preview->items.front();const auto& r=*item.result;
        if(r.residuals.size()==r.x.size() && !r.x.empty()) {plot::SeriesEntryView residual;residual.id=item.id+" residual";residual.data={r.x.data(),r.residuals.data(),r.x.size()};spec.y.label="Residual";spec.y.unit=r.yUnits;spec.autoFit=residualView.dataChanged?plot::AutoFit::Always:plot::AutoFit::Once;plot::RenderPlot("analysis-residuals",spec,&residual,1,residualView,metrics,{0,140});}
    }
}
}
