#include "AnalysisPanel.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace SST::Prototypes::PlotLab {
namespace w=sst::ui::widgets;
namespace {
bool ParameterControl(const char* label,double& value,const std::string& units) {
    const auto tip="Physical value ["+units+"]. Drag mantissa/exponent; Ctrl+click for exact entry.";
    return w::LogValue(label,&value,-100,100,tip.c_str());
}
plot::Color IdentityColor(const std::string& id) {
    std::uint32_t hash=2166136261u;for(unsigned char c:id)hash=(hash^c)*16777619u;
    constexpr plot::Color colors[]={{.15f,.65f,.95f,1},{.95f,.5f,.15f,1},{.3f,.75f,.4f,1},{.7f,.4f,.85f,1},{.85f,.3f,.5f,1}};
    return colors[hash%std::size(colors)];
}
bool SourcePicker(const pl::Project& project,const char* label,pl::Id& selected,std::optional<pl::Id> worksheet={}) {
    bool changed=false;const auto found=project.objects().find(selected);const auto caption=found==project.objects().end()?"Choose source":found->second->name.c_str();
    const bool noSheet=worksheet && !*worksheet;ImGui::BeginDisabled(noSheet);
    if(w::BeginCombo(label,caption,0,noSheet?"Choose a worksheet first.":"Choose a source with its physical units. Source data remains unchanged.")) {
        if(worksheet && ImGui::Selectable("None",selected==0)){selected=0;changed=true;}
        for(const auto& [id,object]:project.objects()) {
            const auto* column=std::get_if<pl::Column>(&object->data);
            if(!worksheet?!std::holds_alternative<pl::Worksheet>(object->data):!column || object->parent!=*worksheet || column->values.index()!=0)continue;
            ImGui::PushID(static_cast<int>(id));
            if(ImGui::Selectable((object->name+(column?" ["+column->units+"]":"")).c_str(),selected==id)){selected=id;changed=true;}
            ImGui::PopID();
        }w::EndCombo();
    }ImGui::EndDisabled();return changed;
}
bool ParameterRows(std::vector<pl::AnalysisParameter>& parameters,bool local=false) {
    bool changed=false;
    const float height=(parameters.size()+1)*(ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeightWithSpacing())+ImGui::GetStyle().ScrollbarSize;
    if(ImGui::BeginTable("Parameters",6,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollX,{0,height},1000)) {
        ImGui::TableSetupColumn("Parameter [unit]",ImGuiTableColumnFlags_WidthFixed,170);
        ImGui::TableSetupColumn("Start",ImGuiTableColumnFlags_WidthStretch,1,0);
        ImGui::TableSetupColumn("Fixed",ImGuiTableColumnFlags_WidthFixed,40);
        ImGui::TableSetupColumn("Shared",ImGuiTableColumnFlags_WidthFixed,48);
        ImGui::TableSetupColumn("Lower",ImGuiTableColumnFlags_WidthStretch,1,0);
        ImGui::TableSetupColumn("Upper",ImGuiTableColumnFlags_WidthStretch,1,0);ImGui::TableHeadersRow();
        for(auto& p:parameters) {
            ImGui::PushID(p.name.c_str());ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s [%s]",p.name.c_str(),p.units.c_str());
            ImGui::BeginDisabled(local && p.shared);
            ImGui::TableNextColumn();changed|=w::LogControl("##start",&p.start,-100,100,"Start in parameter units. Drag mantissa/exponent; Ctrl+click for exact entry.");
            ImGui::TableNextColumn();changed|=ImGui::Checkbox("##fixed",&p.fixed);if(ImGui::IsItemHovered())ImGui::SetTooltip("Keep this parameter fixed at its start.");
            ImGui::TableNextColumn();ImGui::BeginDisabled(local);changed|=ImGui::Checkbox("##shared",&p.shared);if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Shared scope and shared values are edited in the main parameter table. Local parameters may have different starts and bounds here.");ImGui::EndDisabled();
            ImGui::BeginDisabled(p.fixed);ImGui::TableNextColumn();changed|=w::LogControl("##lower",&p.lower,-100,100,"Lower bound in parameter units. Disabled for a fixed parameter.");
            ImGui::TableNextColumn();changed|=w::LogControl("##upper",&p.upper,-100,100,"Upper bound in parameter units. Disabled for a fixed parameter.");ImGui::EndDisabled();ImGui::EndDisabled();ImGui::PopID();
        }ImGui::EndTable();
    }return changed;
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
    ImGui::SetNextWindowSize({1180,680},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Scientific analysis",&open)) {ImGui::End();ImGui::PopID();return;}
    controller.Poll();bool changed=false;auto& project=*controller.document.project;
    if(!ImGui::BeginTable("Analysis layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp)){ImGui::End();ImGui::PopID();return;}
    ImGui::TableSetupColumn("Settings",ImGuiTableColumnFlags_WidthStretch,.58f);ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch,.42f);
    ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::BeginChild("Analysis settings",{0,0});
    auto picker=[&](const char* label,pl::Id& selected,bool sheet) {
        if(SourcePicker(project,label,selected,sheet?std::nullopt:std::optional<pl::Id>{recipe.worksheet})){changed=true;if(sheet)recipe.xColumn=recipe.yColumn=recipe.sigmaColumn=0;}
    };
    picker("Worksheet",recipe.worksheet,true);picker("Physical X",recipe.xColumn,false);picker("Signal Y",recipe.yColumn,false);
    const char* names[]={"Linear fit","Exponential + baseline","Gaussian + baseline","User model","Interpolate","Mean (3 points)","Savitzky-Golay (5 points)","Constant baseline","Linear baseline","Derivative dY/dX","Cumulative integral","Peak locations / FWHM","Extrapolate (linear endpoints)","Multi-peak fit"};
    auto operation=static_cast<int>(recipe.settings.operation);
    if(w::Combo("Operation",&operation,names,static_cast<int>(std::size(names)),-1,"Choose the scientific operation; physical X and units are retained.")) {
        recipe.settings.operation=static_cast<pl::ScientificOperation>(operation);recipe.settings.parameters.clear();recipe.settings.anchors.clear();recipe.settings.targetX.clear();recipe.settings.weights=pl::WeightSemantics::Unweighted;recipe.settings.acknowledgeExtrapolation=false;recipe.globalInputs.clear();changed=true;
    }
    auto units=[&](pl::Id id) {auto it=project.objects().find(id);return it!=project.objects().end() && std::holds_alternative<pl::Column>(it->second->data)?std::get<pl::Column>(it->second->data).units:std::string{};};
    if(recipe.settings.operation==pl::ScientificOperation::MultiPeakFit) {
        if(recipe.settings.peaks.empty())recipe.settings.peaks={{"peak1",pl::PeakShape::Gaussian}};
        ImGui::BeginDisabled(recipe.settings.peaks.size()>=32);if(ImGui::SmallButton("Add peak")){
            std::set<std::string> ids;for(const auto& peak:recipe.settings.peaks)ids.insert(peak.id);std::size_t index=1;while(ids.contains("peak"+std::to_string(index)))++index;
            recipe.settings.peaks.push_back({"peak"+std::to_string(index),pl::PeakShape::Gaussian});recipe.settings.parameters.clear();changed=true;
        }ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Add a named component; a fit supports at most 32 peaks.");
        ImGui::SameLine();ImGui::BeginDisabled(recipe.settings.peaks.size()<=1);if(ImGui::SmallButton("Remove last peak")){recipe.settings.peaks.pop_back();recipe.settings.parameters.clear();changed=true;}ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Remove the last component; at least one component is required.");
        for(auto& peak:recipe.settings.peaks) {ImGui::PushID(peak.id.c_str());auto shape=static_cast<int>(peak.shape);if(w::Combo(peak.id.c_str(),&shape,"Gaussian\0Lorentzian\0Voigt\0Asymmetric (split Gaussian)\0",-1,"Height-normalized component; widths are sigma or Lorentzian HWHM in physical X units.")){peak.shape=static_cast<pl::PeakShape>(shape);recipe.settings.parameters.clear();changed=true;}ImGui::PopID();}
    }
    const bool fit=pl::isAnalysisFit(recipe.settings.operation);
    if(fit) {
        auto weights=static_cast<int>(recipe.settings.weights);
        changed|=w::Combo("Weights",&weights,"Unweighted\0Absolute Y sigma\0Relative Y sigma\0",-1,"Sigma is a positive standard deviation in Y units. Relative covariance is scaled by objective / degrees of freedom.");
        recipe.settings.weights=static_cast<pl::WeightSemantics>(weights);
        if(weights)picker("Sigma Y",recipe.sigmaColumn,false);
        if(recipe.settings.parameters.empty())try{recipe.settings.parameters=pl::defaultAnalysisParameters(recipe.settings,units(recipe.xColumn),units(recipe.yColumn));}catch(const std::exception& e){message=e.what();}
        if(recipe.settings.operation==pl::ScientificOperation::UserFit) {
            ImGui::Text("User model: %s (v%u)",recipe.settings.userModelId.c_str(),recipe.settings.modelVersion);
            ImGui::BeginDisabled(!controller.document.PythonTrusted());
            if(ImGui::Button("Load Python model..."))try{if(auto path=OpenFileDialog({"Named Python model","py"})){auto next=recipe;auto model=PythonBinding::LoadUserAnalysisModel(controller.document,path->Native(),next);recipe=std::move(next);userModel=std::move(model);changed=true;message.clear();}}catch(const std::exception& e){message=e.what();}
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Trust this project in the worksheet Python tools first. The file defines a NamedModel called model and explicit parameters. It runs only when you load it.");
        }
        changed|=ParameterRows(recipe.settings.parameters);
        if(ImGui::Button("Global datasets..."))OpenGlobal();
        ImGui::SameLine();ImGui::Text("%zu additional datasets; Shared column selects common parameters",recipe.globalInputs.size());
        if(!recipe.globalInputs.empty() && ImGui::SmallButton("Use single dataset")){recipe.globalInputs.clear();changed=true;}
    }
    for(int side=0;side<2;++side) {
        auto& bound=side?recipe.settings.xTo:recipe.settings.xFrom;bool enabled=bound.has_value();
        const char* label=side?"Limit X to":"Limit X from";
        if(w::Checkbox(label,&enabled,"Inclusive physical X range; all samples are used when disabled.")) {bound=enabled?std::optional<double>{0}:std::nullopt;changed=true;}
        if(enabled) {ImGui::PushID(side);changed|=ParameterControl("X [physical units]",*bound,units(recipe.xColumn));ImGui::PopID();}
    }
    const auto op=recipe.settings.operation;
    const bool peakBaseline=op==pl::ScientificOperation::Peaks || op==pl::ScientificOperation::MultiPeakFit;
    if(peakBaseline) {bool linear=recipe.settings.anchors.size()==2;if(w::Checkbox("Linear peak baseline",&linear,"Off uses a constant anchor; on uses two physical X/Y anchors.")){recipe.settings.anchors.resize(linear?2:1);changed=true;}if(recipe.settings.anchors.empty())recipe.settings.anchors.resize(1);}
    if(op==pl::ScientificOperation::ConstantBaseline || op==pl::ScientificOperation::LinearBaseline || peakBaseline) {
        if(!peakBaseline)recipe.settings.anchors.resize(op==pl::ScientificOperation::ConstantBaseline?1:2);
        for(std::size_t i=0;i<recipe.settings.anchors.size();++i) {
            ImGui::PushID(static_cast<int>(i));changed|=ParameterControl("Anchor X",recipe.settings.anchors[i].first,units(recipe.xColumn));changed|=ParameterControl("Anchor Y",recipe.settings.anchors[i].second,units(recipe.yColumn));ImGui::PopID();
        }
    }
    if(op==pl::ScientificOperation::Interpolate || op==pl::ScientificOperation::Extrapolate) {
        bool gridChanged=false;
        if(op==pl::ScientificOperation::Extrapolate) {
            ImGui::TextWrapped("Warning: outside the measured domain, linear endpoint slopes predict unmeasured values. Choose an explicit output range.");
            if(!recipe.settings.outputFrom)recipe.settings.outputFrom=0;if(!recipe.settings.outputTo)recipe.settings.outputTo=1;
            gridChanged|=ParameterControl("Output from X",*recipe.settings.outputFrom,units(recipe.xColumn));gridChanged|=ParameterControl("Output to X",*recipe.settings.outputTo,units(recipe.xColumn));
            changed|=w::Checkbox("Acknowledge extrapolation warning",&recipe.settings.acknowledgeExtrapolation,"Explicit opt-in; interpolation alone never extends data outside its domain.");
        }
        int count=static_cast<int>(recipe.settings.targetX.size());
        gridChanged|=w::Int("Target points",&count,1,2,100000,"Evenly spaced target grid spanning the selected output range.");
        if(gridChanged) {
            const auto it=project.objects().find(recipe.xColumn);
            if(it!=project.objects().end())if(const auto* c=std::get_if<pl::Column>(&it->second->data))if(const auto* x=std::get_if<std::vector<double>>(&c->values))if(x->size()>1) {
                const double low=op==pl::ScientificOperation::Extrapolate?*recipe.settings.outputFrom:recipe.settings.xFrom.value_or(x->front()),high=op==pl::ScientificOperation::Extrapolate?*recipe.settings.outputTo:recipe.settings.xTo.value_or(x->back());
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
    ImGui::EndChild();ImGui::TableNextColumn();
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
    if(DrawPreview()) {
        ++stateRevision;
        if(recipe.autoPreview){exactPending=ImGui::IsAnyItemActive();Run(exactPending);}
    }
    ImGui::BeginChild("Analysis reports",{0,0});
    if(status.preview && !status.preview->items.empty()) {
        const auto& result=*status.preview->items.front().result;
        ImGui::Text("Used: %zu   Rejected: %zu",result.usedPoints,result.excludedPoints);
        if(pl::isAnalysisFit(result.settings.operation)){w::SameLineIfFits(350);ImGui::Text("%sobjective: %.6g   DoF: %d",result.globalFit?"Global ":"",result.objective,result.degreesOfFreedom);}
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
    ImGui::EndChild();ImGui::EndTable();ImGui::End();ImGui::PopID();
}
void AnalysisPanel::DrawBatch() {
    if(!batchOpen)return;auto& project=*controller.document.project;controller.Poll();
    std::erase_if(batchInputs,[&](const auto& item){return !project.objects().contains(item.first);});
    ImGui::SetNextWindowSize({650,650},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Batch scientific analysis",&batchOpen)) {
        ImGui::Text("Recipe: %s (v%u) / %s",recipe.id.c_str(),recipe.version,pl::scientificOperationIds[static_cast<unsigned>(recipe.settings.operation)]);
        if(pl::isAnalysisFit(recipe.settings.operation) && w::Checkbox("Global fit (one objective)",&globalMode,"Shared parameters occur once; local parameters are fitted independently for each dataset in the same solve."))batchChecked=false;
        if(!pl::isAnalysisFit(recipe.settings.operation))globalMode=false;
        if(globalMode){DrawGlobal();ImGui::End();return;}
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
        const bool fit=pl::isAnalysisFit(recipe.settings.operation);
        if(fit && w::Checkbox("Keep fitted curves",&batchCurves,"Off: retain scalar fit reports only, without full curves/residual arrays."))batchChecked=false;
        auto requests=[&]{std::vector<std::pair<std::string,pl::AnalysisRecipe>> items;for(const auto& [sheet,columns]:batchInputs){auto item=recipe;item.globalInputs.clear();item.id=recipe.id+"-sheet-"+std::to_string(sheet);item.worksheet=sheet;item.xColumn=columns[0];item.yColumn=columns[1];item.sigmaColumn=columns[2];item.settings.includeCurves=fit?batchCurves:true;items.emplace_back(item.id,std::move(item));}return items;};
        if(ImGui::Button("Check source mapping"))try{if(batchInputs.empty())throw std::invalid_argument("Select at least one worksheet");for(const auto& [id,r]:requests())(void)CaptureAnalysisSource(project,r);checkedProjectRevision=project.revision();checkedSettingsRevision=stateRevision;batchChecked=true;message="Source mapping valid; scientific status is reported separately for every item.";}catch(const std::exception& e){message=e.what();batchChecked=false;}
        const auto& status=controller.Status();const bool ready=batchChecked && checkedProjectRevision==project.revision() && checkedSettingsRevision==stateRevision && status.stage!=AnalysisStage::Running;
        ImGui::SameLine();ImGui::BeginDisabled(!ready);if(ImGui::Button("Run batch"))try{
            controller.SubmitBatch(requests(),userModel);message.clear();
        }catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Check every source mapping after changing the recipe or worksheet, and wait for the running analysis.");
        ImGui::SameLine();ImGui::BeginDisabled(status.stage!=AnalysisStage::PreviewReady || !status.exact);if(ImGui::Button("Accept successful items"))try{controller.Accept();}catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Cancel batch"))controller.Cancel();
        if(status.preview)for(const auto& item:status.preview->items)ImGui::TextWrapped("%s: %s; used %zu, rejected %zu; objective %.6g. %s",item.id.c_str(),item.result->ok()?"Ready":"Failed",item.result->usedPoints,item.result->excludedPoints,item.result->objective,item.result->message.c_str());
        ImGui::TextWrapped("%s",message.c_str());
    }ImGui::End();
}
void AnalysisPanel::DrawGlobal() {
    auto& project=*controller.document.project;bool changed=false;
    auto source=[&](pl::Id& sheet,pl::Id& x,pl::Id& y,pl::Id& sigma) {
        if(SourcePicker(project,"Worksheet",sheet)){x=y=sigma=0;changed=true;}
        changed|=SourcePicker(project,"Physical X",x,sheet);changed|=SourcePicker(project,"Signal Y",y,sheet);
        if(recipe.settings.weights!=pl::WeightSemantics::Unweighted)changed|=SourcePicker(project,"Sigma Y",sigma,sheet);
    };
    ImGui::TextUnformatted("Primary dataset");ImGui::PushID("primary");source(recipe.worksheet,recipe.xColumn,recipe.yColumn,recipe.sigmaColumn);ImGui::PopID();
    for(std::size_t i=0;i<recipe.globalInputs.size();++i) {
        auto& input=recipe.globalInputs[i];ImGui::PushID(input.id.c_str());ImGui::SeparatorText(("Additional dataset "+std::to_string(i+1)).c_str());
        source(input.worksheet,input.xColumn,input.yColumn,input.sigmaColumn);
        if(!input.parameters.empty()) {
            if(input.parameters.size()!=recipe.settings.parameters.size()){input.parameters=recipe.settings.parameters;changed=true;}
            for(std::size_t j=0;j<input.parameters.size();++j){auto& p=input.parameters[j];const auto& main=recipe.settings.parameters[j];if(p.name!=main.name || p.units!=main.units || p.shared!=main.shared || (main.shared && p!=main)){p=main;changed=true;}}
        }
        if(ImGui::CollapsingHeader("Local parameter starts / bounds")) {
            if(input.parameters.empty()){input.parameters=recipe.settings.parameters;changed=true;}
            changed|=ParameterRows(input.parameters,true);
        }
        if(ImGui::SmallButton("Remove dataset")){recipe.globalInputs.erase(recipe.globalInputs.begin()+i);changed=true;ImGui::PopID();break;}
        ImGui::PopID();
    }
    if(ImGui::Button("Add dataset")) {
        std::set<std::string> ids{recipe.id};for(const auto& input:recipe.globalInputs)ids.insert(input.id);
        std::size_t index=1;while(ids.contains("dataset-"+std::to_string(index)))++index;
        recipe.globalInputs.push_back({"dataset-"+std::to_string(index),recipe.worksheet,recipe.xColumn,0,recipe.sigmaColumn,{}});changed=true;
    }
    changed|=w::Checkbox("Keep fitted curves",&batchCurves,"Off keeps scalar reports only. On also retains curves, components and residuals for preview and export.");
    if(changed){++stateRevision;batchChecked=false;}
    if(ImGui::Button("Check source mapping"))try {
        if(recipe.globalInputs.empty())throw std::invalid_argument("Add at least one additional dataset");
        auto r=recipe;r.globalInputs.clear();auto mainSource=CaptureAnalysisSource(project,r);auto main=mainSource.View();std::set<std::string> ids{recipe.id};
        for(const auto& input:recipe.globalInputs){if(input.id.empty() || !ids.insert(input.id).second)throw std::invalid_argument("Dataset IDs must be unique");r.worksheet=input.worksheet;r.xColumn=input.xColumn;r.yColumn=input.yColumn;r.sigmaColumn=input.sigmaColumn;auto captured=CaptureAnalysisSource(project,r);auto view=captured.View();if(view.xUnits!=main.xUnits || view.yUnits!=main.yUnits)throw std::invalid_argument("Every dataset must have the same physical X/Y units");}
        checkedProjectRevision=project.revision();checkedSettingsRevision=stateRevision;batchChecked=true;message="Global source mapping valid; shared and local parameter values use one objective.";
    }catch(const std::exception& e){batchChecked=false;message=e.what();}
    const auto& status=controller.Status();const bool ready=batchChecked && checkedProjectRevision==project.revision() && checkedSettingsRevision==stateRevision && status.stage!=AnalysisStage::Running;
    ImGui::SameLine();ImGui::BeginDisabled(!ready);if(ImGui::Button("Run global fit"))try{auto r=recipe;r.settings.includeCurves=batchCurves;controller.Submit(std::move(r),userModel);message.clear();}catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Check source mapping after changing settings and wait for any running fit.");
    const bool globalReady=status.preview && !status.preview->items.empty() && status.preview->items.back().result->globalFit;
    ImGui::BeginDisabled(status.stage!=AnalysisStage::PreviewReady || !status.exact || !globalReady);if(ImGui::Button("Accept global fit"))try{controller.Accept();}catch(const std::exception& e){message=e.what();}ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("An exact successful global fit is required; Accept publishes all datasets in one undo step.");
    ImGui::SameLine();ImGui::BeginDisabled(status.stage!=AnalysisStage::Running);if(ImGui::Button("Cancel fit"))controller.Cancel();ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Cancellation is available while a fit is running.");
    if(status.preview)for(const auto& item:status.preview->items)ImGui::TextWrapped("%s: %s; objective %.6g. %s",item.id.c_str(),item.result->ok()?"Ready":"Failed",item.result->objective,item.result->message.c_str());
    ImGui::TextWrapped("%s",message.c_str());
}
bool AnalysisPanel::DrawPreview() {
    bool parametersChanged=false;
    const auto& status=controller.Status();std::vector<sst::ui::plot::SeriesEntryView> series;
    const bool changed=drawnPreview.lock()!=status.preview;
    previewView.dataChanged|=changed;residualView.dataChanged|=changed;drawnPreview=status.preview;
    auto append=[&](const std::shared_ptr<const AnalysisJobResult>& results,const char* suffix,bool previous) {
        if(!results)return;
        for(const auto& item:results->items) {
            const auto& r=*item.result;if(!r.ok() || r.x.size()!=r.values.size() || r.x.empty())continue;
            if(previous && status.preview && !status.preview->items.empty() && r.outputUnits!=status.preview->items.front().result->outputUnits)continue;
            plot::SeriesEntryView view;view.id=item.id+suffix;view.data={r.x.data(),r.values.data(),r.x.size()};
            view.style.color=previous?plot::Color{.6f,.6f,.6f,.7f}:IdentityColor(item.id);
            if(previous)view.style.lineStyle=plot::LineStyle::Dashed;
            series.push_back(std::move(view));
            if(!previous)for(std::size_t j=0;j<r.components.size();++j) {
                plot::SeriesEntryView component;component.id=item.id+" "+r.settings.peaks[j].id;component.data={r.x.data(),r.components[j].data(),r.x.size()};component.style.color=IdentityColor(item.id+"."+r.settings.peaks[j].id);component.style.lineStyle=plot::LineStyle::Dashed;series.push_back(std::move(component));
            }
            if(!previous && r.baseline.size()==r.x.size()){plot::SeriesEntryView base;base.id=item.id+" baseline";base.data={r.x.data(),r.baseline.data(),r.x.size()};base.style.color={.65f,.65f,.65f,1};base.style.lineStyle=plot::LineStyle::Dotted;series.push_back(std::move(base));}
            const auto& observedX=r.observedX.empty()?r.x:r.observedX;
            if(!previous && r.observed.size()==observedX.size() && r.yUnits==r.outputUnits){plot::SeriesEntryView raw;raw.id=item.id+" data";raw.kind=plot::SeriesKind::Scatter;raw.style.color=IdentityColor(item.id);raw.data={observedX.data(),r.observed.data(),observedX.size()};series.push_back(std::move(raw));}
            if(!previous && !r.peakX.empty()){plot::SeriesEntryView peaks;peaks.id=item.id+" peaks";peaks.kind=plot::SeriesKind::Scatter;peaks.data={r.peakX.data(),r.peakY.data(),r.peakX.size()};peaks.style.marker=plot::Marker::Diamond;series.push_back(std::move(peaks));}
        }
    };
    append(status.accepted," accepted",true);append(status.preview," preview",false);
    plot::PlotSpec spec;spec.x.label="Physical X";spec.y.label="Analysis";spec.autoFit=previewView.dataChanged?plot::AutoFit::Always:plot::AutoFit::Once;
    if(status.preview && !status.preview->items.empty()) {
        const auto& item=status.preview->items.front();const auto& r=*item.result;
        spec.x.unit=r.xUnits;spec.y.unit=r.outputUnits;
    }
    plot::PreviewMetrics metrics;metrics.interactive=true;
    if(!series.empty()) {plot::RenderPlot("analysis-preview",spec,series.data(),series.size(),previewView,metrics,{0,220},nullptr,[&] {
        if(recipe.settings.operation!=pl::ScientificOperation::MultiPeakFit)return;
        for(std::size_t i=0;i<recipe.settings.peaks.size();++i) {
            const auto& peak=recipe.settings.peaks[i];auto& parameters=recipe.settings.parameters;
            const auto find=[&](const std::string& name){return std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.name==peak.id+name;});};
            const auto center=find("_center"),height=find("_height");if(center==parameters.end() || height==parameters.end())continue;
            const auto baseline=[&](double x){const auto& a=recipe.settings.anchors;if(a.empty())return 0.;if(a.size()<2 || a[1].first==a[0].first)return a[0].second;return a[0].second+(x-a[0].first)*(a[1].second-a[0].second)/(a[1].first-a[0].first);};
            double x=center->start,y=height->start+baseline(x);bool hovered=false;
            const auto color=IdentityColor(peak.id);
            if(ImPlot::DragPoint(static_cast<int>(1000+i),&x,&y,{color[0],color[1],color[2],1},6,ImPlotDragToolFlags_None,nullptr,&hovered)) {
                center->start=std::clamp(x,center->lower,center->upper);height->start=std::clamp(y-baseline(center->start),height->lower,height->upper);parametersChanged=true;
            }
            if(hovered)ImGui::SetTooltip("%s: drag start center [%s] and height [%s]. Fixed parameters stay at the dragged value; free parameters are fitted on Recalculate.",peak.id.c_str(),center->units.c_str(),height->units.c_str());
        }
    });}
    if(recipe.showResiduals && status.preview && !status.preview->items.empty()) {
        std::vector<plot::SeriesEntryView> residuals;
        for(const auto& item:status.preview->items){const auto& r=*item.result;if(r.residuals.size()==r.x.size() && !r.x.empty()){plot::SeriesEntryView view;view.id=item.id+" residual";view.data={r.x.data(),r.residuals.data(),r.x.size()};view.style.color=IdentityColor(item.id);residuals.push_back(std::move(view));}}
        if(!residuals.empty()){spec.y.label="Residual";spec.y.unit=status.preview->items.front().result->yUnits;spec.autoFit=residualView.dataChanged?plot::AutoFit::Always:plot::AutoFit::Once;plot::RenderPlot("analysis-residuals",spec,residuals.data(),residuals.size(),residualView,metrics,{0,140});}
    }
    return parametersChanged;
}
}
