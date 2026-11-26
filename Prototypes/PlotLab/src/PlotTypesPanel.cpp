#include "PlotTypesPanel.hpp"
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace SST::Prototypes::PlotLab {
PlotTypesPanelResult DrawPlotTypesPanel(const sst::plotlab::Project& project,sst::plotlab::MapPlotRequest& draft,const std::string& diagnostic,bool sourceEditable) {
    namespace pl=sst::plotlab;namespace plot=sst::ui::plot;
    PlotTypesPanelResult result;
    const auto sheet=project.objects().find(draft.worksheet);
    if(sheet==project.objects().end() || !std::holds_alternative<pl::Worksheet>(sheet->second->data)) {
        ImGui::TextDisabled("Select a worksheet to create a map.");result.cancel=ImGui::Button("Cancel##PlotTypesNoSheet");return result;
    }
    std::string zUnit;
    if(auto it=project.objects().find(draft.z);it!=project.objects().end()) if(const auto* c=std::get_if<pl::Column>(&it->second->data)) zUnit=" ["+c->units+"]";
    ImGui::PushID("PlotTypesPanel");
    const auto row=[](const char* label){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);};
    const auto tip=[](const char* text){if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",text);};
    if(ImGui::BeginTable("##map-controls",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthFixed,100);ImGui::TableSetupColumn("Control");
        const auto column=[&](const char* label,pl::Id& id) {
            row(label);ImGui::PushID(label);
            const auto found=project.objects().find(id);const char* name=found==project.objects().end()?"Select numeric column":found->second->name.c_str();
            if(ImGui::BeginCombo("##column",name)) {
                for(auto key:project.children(draft.worksheet)) {
                    const auto& object=project.object(key);const auto* c=std::get_if<pl::Column>(&object.data);if(!c || c->values.index()!=0) continue;
                    ImGui::PushID(static_cast<int>(key));const auto caption=object.name+" ["+c->units+"]";
                    if(ImGui::Selectable(caption.c_str(),key==id)) {id=key;result.changed=true;}
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            tip("Explicit measured column; coordinates keep their source order. No sorting or interpolation.");ImGui::PopID();
        };
        ImGui::BeginDisabled(!sourceEditable);
        row("Plot type");int kind=draft.kind==plot::SeriesKind::Surface?1:draft.kind==plot::SeriesKind::Scatter3D?2:draft.kind==plot::SeriesKind::Line3D?3:0;
        if(ImGui::Combo("##kind",&kind,"Heatmap / contour\0Surface\0Scatter 3D\0Line 3D\0")){const plot::SeriesKind kinds[]={plot::SeriesKind::Heatmap,plot::SeriesKind::Surface,plot::SeriesKind::Scatter3D,plot::SeriesKind::Line3D};draft.kind=kinds[kind];result.changed=true;}tip("Shared 3D renderer; rotate and zoom directly in the preview. Camera is saved with the project.");
        column("X",draft.x);column("Y",draft.y);column("Z / intensity",draft.z);
        row("Grid columns");int count=static_cast<int>(std::min<std::size_t>(draft.gridColumns,std::numeric_limits<int>::max()));
        if(ImGui::DragInt("##columns",&count,1,2,std::numeric_limits<int>::max(),"%d pixels",ImGuiSliderFlags_AlwaysClamp)) {draft.gridColumns=count;result.changed=true;}
        tip("X varies fastest, then Y. Complete rectilinear grid, at least 2 by 2. Ctrl+click for exact typing.");result.editing|=ImGui::IsItemActive();
        ImGui::EndDisabled();
        if(!sourceEditable) {row("Source");ImGui::TextDisabled("Fixed for this map; create a new map to change source columns/grid.");}
        row("Colour scale");int scale=plot::LogColor(draft.spec)?1:0;
        if(ImGui::Combo("##scale",&scale,"Linear\0Logarithmic\0")) {draft.spec.z.scale=scale?plot::AxisScale::Log10:plot::AxisScale::Linear;result.changed=true;}
        tip("Logarithmic intensity uses positive physical Z values; no epsilon is substituted.");
        row("Nonpositive Z");int policy=static_cast<int>(draft.spec.nonpositiveColor);
        if(ImGui::Combo("##policy",&policy,"Mask\0Reject\0")) {draft.spec.nonpositiveColor=static_cast<plot::NonpositiveColorPolicy>(policy);result.changed=true;}
        tip("Mask leaves nonpositive cells empty on a log colour scale. Reject reports a validation error.");
        row("Colormap");if(ImGui::BeginCombo("##colormap",draft.spec.colormap.c_str())) {
            for(const auto* name:{"Viridis","Plasma","Hot","Cool","Jet","Greys"}) if(ImGui::Selectable(name,draft.spec.colormap==name)) {draft.spec.colormap=name;result.changed=true;}
            ImGui::EndCombo();
        }tip("Shared viewport and publication palette.");
        row("Colour bar");result.changed|=ImGui::Checkbox("##bar",&draft.spec.colorScale);tip("Show intensity values and Z units beside the map.");
        row("Colour range");bool fixed=draft.spec.colorRange.has_value();
        if(ImGui::Checkbox("##fixed",&fixed)) {draft.spec.colorRange=fixed?std::optional<std::array<double,2>>{std::array<double,2>{scale?1.:0.,10.}}:std::nullopt;result.changed=true;}
        tip("Off: range from unmasked measured intensities. On: explicit physical bounds.");
        if(draft.spec.colorRange) for(int k=0;k<2;++k) {
            row((std::string(k?"Maximum Z":"Minimum Z")+zUnit).c_str());ImGui::PushID(k);
            auto& value=(*draft.spec.colorRange)[k];
            if(scale) {
                result.changed|=sst::ui::widgets::LogControl("##value",&value,-300,300,"Physical intensity in the Z column unit. Ctrl+click for exact typing.");result.editing|=ImGui::IsItemActive();
            } else {result.changed|=ImGui::DragScalar("##value",ImGuiDataType_Double,&value,.01f,nullptr,nullptr,"%.6g");result.editing|=ImGui::IsItemActive();tip("Physical intensity in the Z column unit; Ctrl+click for exact typing.");}
            ImGui::PopID();
        }
        row("Contour levels");int levels=static_cast<int>(draft.spec.contours.levels.size());
        if(ImGui::DragInt("##count",&levels,1,0,32,"%d",ImGuiSliderFlags_AlwaysClamp)) {draft.spec.contours.levels.resize(levels);result.changed=true;}
        tip("Explicit increasing physical Z levels. Zero disables contours.");
        for(std::size_t i=0;i<draft.spec.contours.levels.size();++i) {
            row(("Level Z"+zUnit).c_str());ImGui::PushID(static_cast<int>(i));result.changed|=ImGui::DragScalar("##level",ImGuiDataType_Double,&draft.spec.contours.levels[i],.01f,nullptr,nullptr,"%.6g");result.editing|=ImGui::IsItemActive();tip("Physical Z value in the selected column unit; Ctrl+click for exact typing.");ImGui::PopID();
        }
        row("Contour width");result.changed|=ImGui::DragFloat("##contour-width",&draft.spec.contours.lineWidthPt,.05f,.1f,10,"%.2f pt",ImGuiSliderFlags_AlwaysClamp);result.editing|=ImGui::IsItemActive();tip("Physical line width in points. Ctrl+click for exact typing.");
        row("Contour colour");result.changed|=ImGui::ColorEdit4("##contour-color",draft.spec.contours.color.data(),ImGuiColorEditFlags_NoInputs);tip("Shared contour stroke and label colour.");
        row("Contour labels");result.changed|=ImGui::Checkbox("##labels",&draft.spec.contours.labels);tip("One visible physical-value label per contour level.");
        ImGui::EndTable();
    }
    if(!diagnostic.empty()) ImGui::TextWrapped("%s",diagnostic.c_str());
    const bool ready=draft.x && draft.y && draft.z && (!plot::IsGrid(draft.kind) || draft.gridColumns>=2) && diagnostic.empty();
    ImGui::BeginDisabled(!ready);result.apply=ImGui::Button("Apply");tip("Select X/Y/Z and a grid width; resolve preview validation errors before applying.");ImGui::EndDisabled();
    ImGui::SameLine();result.cancel=ImGui::Button("Cancel");tip("Cancel preview and keep the published graph.");
    ImGui::PopID();return result;
}
}
