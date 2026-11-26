#include "AnalysisPanel.hpp"
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
    if(!open)return;
    ImGui::PushID(this);
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
    const char* names[]={"Linear fit","Exponential + baseline","Gaussian + baseline","User model","Interpolate","Mean (3 points)","Savitzky-Golay (5 points)","Constant baseline","Linear baseline","Derivative dY/dX","Cumulative integral"};
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
        if(recipe.settings.operation==pl::ScientificOperation::UserFit)ImGui::TextWrapped("User model: %s (v%u). Select its trusted named callable through the project runtime.",recipe.settings.userModelId.c_str(),recipe.settings.modelVersion);
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
    if(w::Checkbox("Auto preview",&recipe.autoPreview,"Recalculate while editing; exact result replaces the coarse preview after release.")) {if(recipe.autoPreview)changed=true;else exactPending=false;}
    w::Checkbox("Show residuals",&recipe.showResiduals,"Show Y minus model in Y units; the fit report also contains sigma-normalized residuals.");
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
    ImGui::TextUnformatted(message.empty()?controller.Status().message.c_str():message.c_str());
    DrawPreview();ImGui::End();ImGui::PopID();
}
void AnalysisPanel::DrawPreview() {
    const auto& status=controller.Status();std::vector<sst::ui::plot::SeriesEntryView> series;
    const bool changed=drawnPreview.lock()!=status.preview;
    previewView.dataChanged=changed;residualView.dataChanged=changed;drawnPreview=status.preview;
    auto append=[&](const std::shared_ptr<const AnalysisJobResult>& results,const char* suffix,bool previous) {
        if(!results)return;
        for(const auto& item:results->items) {
            const auto& r=*item.result;if(!r.ok() || r.x.size()!=r.values.size() || r.x.empty())continue;
            plot::SeriesEntryView view;view.id=item.id+suffix;view.data={r.x.data(),r.values.data(),r.x.size()};
            view.style.color=previous?plot::Color{.6f,.6f,.6f,.7f}:plot::Color{.15f,.65f,.95f,1};
            if(previous)view.style.lineStyle=plot::LineStyle::Dashed;
            series.push_back(std::move(view));
        }
    };
    append(status.accepted," accepted",true);append(status.preview," preview",false);
    plot::PlotSpec spec;spec.x.label="Physical X";spec.y.label="Analysis";spec.autoFit=plot::AutoFit::Once;
    if(status.preview && !status.preview->items.empty()) {
        const auto& item=status.preview->items.front();const auto& r=*item.result;
        spec.x.unit=r.xUnits;spec.y.unit=r.outputUnits;
        if(r.observed.size()==r.x.size() && !r.x.empty()) {
            plot::SeriesEntryView raw;raw.id=item.id+" data";raw.kind=plot::SeriesKind::Scatter;raw.data={r.x.data(),r.observed.data(),r.x.size()};series.push_back(std::move(raw));
        }
    }
    plot::PreviewMetrics metrics;metrics.interactive=true;
    if(!series.empty()) {plot::RenderPlot("analysis-preview",spec,series.data(),series.size(),previewView,metrics,{0,220});}
    if(recipe.showResiduals && status.preview && !status.preview->items.empty()) {
        const auto& item=status.preview->items.front();const auto& r=*item.result;
        if(r.residuals.size()==r.x.size() && !r.x.empty()) {plot::SeriesEntryView residual;residual.id=item.id+" residual";residual.data={r.x.data(),r.residuals.data(),r.x.size()};spec.y.label="Residual ["+r.yUnits+"]";plot::RenderPlot("analysis-residuals",spec,&residual,1,residualView,metrics,{0,140});}
    }
}
}
