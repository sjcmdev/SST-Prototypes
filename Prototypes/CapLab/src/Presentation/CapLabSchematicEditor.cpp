// Adapted from CircuitCore-UI SchematicEditor for CapLab presentation.
#include "Presentation/CapLabSchematicEditor.hpp"
#include "Presentation/CircuitDisplay.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ccui {
namespace {
float snap(float x) { return std::round(x/20)*20; }
template<class Transform>
void symbol(ImDrawList* draw,Kind kind,Transform at,float scale,ImU32 color) {
        auto line=[&](float x,float y,float a,float b) { draw->AddLine(at(x,y),at(a,b),color,2); };
        line(-40,0,-18,0); line(18,0,40,0);
        switch (kind) {
        case Kind::R:
            line(-18,-9,18,-9); line(18,-9,18,9); line(18,9,-18,9); line(-18,9,-18,-9); break;
        case Kind::C: case Kind::CPE:
            line(-18,0,-5,0); line(5,0,18,0);
            line(-5,-16,kind==Kind::CPE?-11.f:-5.f,16); line(5,-16,kind==Kind::CPE?11.f:5.f,16); break;
        case Kind::L:
            for (int coil=0;coil<4;++coil) { float center=-13.5f+9*coil;
                ImVec2 previous=at(center-4.5f,0);
                for (int a=1;a<=12;++a) { float angle=3.14159265f*a/12; auto next=at(center-4.5f*std::cos(angle),-9*std::sin(angle)); draw->AddLine(previous,next,color,2); previous=next; }
            } break;
        case Kind::W:
            for (int j=0;j<3;++j) { float x=-18.f+j*12; line(x,-10,x+6,10); line(x+6,10,x+12,-10); } break;
        case Kind::PN: case Kind::Schottky:
            line(-18,-13,-18,13); line(-18,-13,14,0); line(-18,13,14,0); line(14,-15,14,15); line(14,0,18,0);
            if (kind==Kind::Schottky) { line(14,-15,20,-15); line(14,15,8,15); } break;
        case Kind::V: case Kind::I:
            draw->AddCircle(at(0,0),18*scale,color,32,2);
            if (kind==Kind::V) { line(-10,-5,-10,5); line(-15,0,-5,0); line(6,0,14,0); }
            else { line(12,0,-12,0); line(-12,0,-5,-6); line(-12,0,-5,6); } break;
        }
}
bool logValue(const char* label,double& value,double minimum,double maximum) {
    bool changed=sst::ui::widgets::LogValue(label,&value,-18,12,"Ctrl+click either slider for exact typing; values are stored in SI.");
    if (changed) value=std::clamp(value,minimum,maximum);
    return changed;
}
bool parameters(Element& e) {
    using namespace SST::Prototypes::CapLab;
    bool changed=logValue(("Value ["+ElementUnit(e)+"]").c_str(),e.value,e.kind==Kind::V || e.kind==Kind::I ? -1e9 : 1e-18,e.kind==Kind::V || e.kind==Kind::I ? 1e9 : 1e12);
    auto scalar=[&](const char* label,double& value,double minimum,double maximum) {
        ImGui::PushID(label);
        if (!sst::ui::widgets::BeginLabelledRow(label)) { ImGui::PopID(); return false; }
        bool edited=ImGui::DragScalar("##value",ImGuiDataType_Double,&value,.001f,&minimum,&maximum,"%.6g",ImGuiSliderFlags_AlwaysClamp);
        sst::ui::widgets::EndLabelledRow(); ImGui::PopID(); return edited;
    };
    if (e.kind==Kind::CPE) changed|=scalar("Exponent n [1]",e.secondary,.001,1);
    if (e.kind==Kind::PN || e.kind==Kind::Schottky) {
        changed|=scalar("Ideality n [1]",e.secondary,.1,10);
        changed|=logValue("Cj0 [F]",e.cj0,0,1);
        changed|=logValue("Vbi [V]",e.vbi,1e-6,1e3);
        changed|=scalar("Grading m [1]",e.m,0,1);
        changed|=scalar("FC [1]",e.fc,0,.999);
        changed|=logValue("Lifetime [s]",e.tau,0,1);
    }
    return changed;
}

}
bool CapLabSchematicEditor::properties(CircuitDocument& d) {
    bool changed=false;
    if (ImGui::CollapsingHeader("All element parameters",ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("Element parameters",2,ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Element"); ImGui::TableSetupColumn("Value (SI internally)"); ImGui::TableHeadersRow();
            for (size_t i=0;i<d.elements.size();++i) {
                const auto& item=d.elements[i]; ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                if (ImGui::Selectable(item.name.c_str(),selected==static_cast<int>(i),ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected=static_cast<int>(i); terminal=-1;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openValue=true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to edit. Uncertainty unavailable (Nelder-Mead has no covariance).");
                ImGui::TableNextColumn();
                auto value=SST::Prototypes::CapLab::Engineering(item.value,SST::Prototypes::CapLab::ElementUnit(item));
                ImGui::TextUnformatted(value.c_str());
                if (item.kind==Kind::CPE) ImGui::Text("n = %.6g",item.secondary);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    if (selected<0 || selected>=static_cast<int>(d.elements.size())) { ImGui::TextWrapped("Select an element to edit its parameters."); return false; }
    auto& e=d.elements[selected];
    ImGui::SeparatorText("Selected element"); ImGui::Text("%s (%s)",e.name.c_str(),typeName(e.kind));
    changed|=parameters(e);
    if (ImGui::Button("Rotate 90 (R)")) { e.rotation=(e.rotation+1)%4; changed=true; }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) { d.erase(selected); reset(); return true; }
    if (terminal>=0) {
        ImGui::Text("Terminal N%d",terminal);
        if (ImGui::Button("Remove wires at terminal")) { std::erase_if(d.wires,[&](auto w) { return w.a==terminal || w.b==terminal; }); changed=true; }
    }
    return changed;
}
bool CapLabSchematicEditor::draw(CircuitDocument& d) {
    if (selected>=static_cast<int>(d.elements.size())) reset();
    bool changed=false;
    for (int k=0;k<=8;++k) {
        if (k) sst::ui::widgets::SameLineIfFits(56);
        ImGui::PushID(k);
        auto p=ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##element",palette==k,0,ImVec2(56,32))) { palette=k; tool=Tool::Select; pending=-1; }
        symbol(ImGui::GetWindowDrawList(),static_cast<Kind>(k),[&](float x,float y) {return ImVec2(p.x+28+x*.55f,p.y+16+y*.55f);},.55f,ImGui::GetColorU32(ImGuiCol_Text));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: click, then place on canvas",typeName(static_cast<Kind>(k)));
        ImGui::PopID();
    }
    const char* tools[]{"Select","Wire","Ground","Port +","Port -"};
    for (int i=0;i<5;++i) {
        if (i) sst::ui::widgets::SameLineIfFits(65);
        if (ImGui::Selectable(tools[i],palette<0 && static_cast<int>(tool)==i,0,ImVec2(65,20))) { tool=static_cast<Tool>(i); palette=-1; pending=-1; }
    }
    if (ImGui::Button("Fit schematic")) fitPending=true;
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click: value. Click terminals to connect / assign. Wheel: zoom, middle/right drag: pan. Esc: cancel.");
    auto origin=ImGui::GetCursorScreenPos(); auto size=ImGui::GetContentRegionAvail();
    size.x=std::max(50.f,size.x); size.y=std::max(50.f,size.y);
    if (fitPending) {
        Point lo{-40,-45},hi{40,55};
        if (!d.elements.empty()) {
            lo={d.elements.front().position.x-60,d.elements.front().position.y-40};
            hi={lo.x+140,lo.y+90};
            for (const auto& e : d.elements) {
                lo.x=std::min(lo.x,e.position.x-60); lo.y=std::min(lo.y,e.position.y-40);
                hi.x=std::max(hi.x,e.position.x+80); hi.y=std::max(hi.y,e.position.y+50);
            }
        }
        zoom=std::clamp(std::min((size.x-30)/(hi.x-lo.x),(size.y-30)/(hi.y-lo.y)),.75f,4.f);
        pan={size.x*.5f-(lo.x+hi.x)*.5f*zoom,size.y*.5f-(lo.y+hi.y)*.5f*zoom};
        fitPending=false;
    }
    ImGui::InvisibleButton("schematic",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle|ImGuiButtonFlags_MouseButtonRight);
    bool hovered=ImGui::IsItemHovered(); const bool canvasActive=ImGui::IsItemActive(); auto& io=ImGui::GetIO();
    auto local=[&](ImVec2 p) -> Point { return {(p.x-origin.x-pan.x)/zoom,(p.y-origin.y-pan.y)/zoom}; };
    auto screen=[&](Point p) -> ImVec2 { return {origin.x+pan.x+p.x*zoom,origin.y+pan.y+p.y*zoom}; };
    if (hovered && io.MouseWheel!=0) {
        auto before=local(io.MousePos); zoom=std::clamp(zoom*std::pow(1.15f,io.MouseWheel),.75f,4.f);
        pan={io.MousePos.x-origin.x-before.x*zoom,io.MousePos.y-origin.y-before.y*zoom};
    }
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle,0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right,0))) {
        pan.x+=io.MouseDelta.x; pan.y+=io.MouseDelta.y;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { palette=-1; pending=-1; tool=Tool::Select; }
    int hitNode=-1,hitElement=-1; Point mouse=local(io.MousePos);
    for (size_t i=0;i<d.elements.size();++i) {
        const auto& e=d.elements[i]; float dx=mouse.x-e.position.x,dy=mouse.y-e.position.y;
        if (std::abs(dx)<(e.rotation%2?24:42) && std::abs(dy)<(e.rotation%2?42:24)) hitElement=static_cast<int>(i);
        for (int n : e.nodes) { auto p=d.terminal(n); if (std::hypot(p.x-mouse.x,p.y-mouse.y)<10/zoom) { hitNode=n; hitElement=static_cast<int>(i); } }
    }
    if (hovered && hitElement>=0 && tool==Tool::Select && palette<0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selected=hitElement; terminal=-1; ImGui::OpenPopup("Edit element value");
    }
    if (hovered && hitElement>=0 && !ImGui::IsPopupOpen("Edit element value")) {
        const auto& e=d.elements[hitElement];
        auto value=SST::Prototypes::CapLab::Engineering(e.value,SST::Prototypes::CapLab::ElementUnit(e));
        ImGui::BeginTooltip(); ImGui::Text("%s: %s",e.name.c_str(),value.c_str());
        if (e.kind==Kind::CPE) ImGui::Text("Exponent n = %.6g",e.secondary);
        ImGui::TextDisabled("Uncertainty unavailable (Nelder-Mead has no covariance)."); ImGui::EndTooltip();
    }
    if (hovered && !ImGui::IsPopupOpen("Edit element value") && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (palette>=0) {
            if (d.elements.size()<128) { selected=d.add(static_cast<Kind>(palette),{snap(mouse.x),snap(mouse.y)}); terminal=-1; moveStart=d.elements[selected].position; moveMouse=io.MousePos; changed=true; }
            palette=-1;
        } else if (hitNode>=0 && tool!=Tool::Select) {
            switch (tool) {
            case Tool::Wire:
                if (pending<0) pending=hitNode;
                else { if (pending!=hitNode) { d.wires.push_back({pending,hitNode}); changed=true; } pending=-1; }
                break;
            case Tool::Ground: d.ground=hitNode; changed=true; break;
            case Tool::Positive: d.port[0]=hitNode; changed=true; break;
            case Tool::Negative: d.port[1]=hitNode; changed=true; break;
            default: break;
            }
        } else {
            selected=hitElement; terminal=hitNode;
            if (selected>=0) { moveStart=d.elements[selected].position; moveMouse=io.MousePos; }
        }
    }
    if (tool==Tool::Select && palette<0 && selected>=0 && terminal<0 && canvasActive && !ImGui::IsPopupOpen("Edit element value") && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        auto& p=d.elements[selected].position;
        Point next{snap(moveStart.x+(io.MousePos.x-moveMouse.x)/zoom),snap(moveStart.y+(io.MousePos.y-moveMouse.y)/zoom)};
        if (p.x!=next.x || p.y!=next.y) { p=next; changed=true; }
    }
    if (hovered && selected>=0 && !io.WantTextInput && !ImGui::IsPopupOpen("Edit element value")) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) { auto& r=d.elements[selected].rotation; r=(r+1)%4; changed=true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) { d.erase(selected); reset(); changed=true; }
    }
    auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,25,33,255));
    float step=20*zoom;
    for (float x=std::fmod(pan.x,step);x<size.x;x+=step) for (float y=std::fmod(pan.y,step);y<size.y;y+=step)
        draw->AddCircleFilled({origin.x+x,origin.y+y},1,IM_COL32(52,60,72,255));
    for (auto w : d.wires) {
        auto a=screen(d.terminal(w.a)),b=screen(d.terminal(w.b)); float middle=(a.x+b.x)/2;
        ImVec2 points[]{a,{middle,a.y},{middle,b.y},b}; draw->AddPolyline(points,4,IM_COL32(100,180,220,255),0,2);
    }
    for (size_t i=0;i<d.elements.size();++i) {
        const auto& e=d.elements[i]; auto color=selected==static_cast<int>(i)?IM_COL32(255,205,90,255):IM_COL32(215,227,235,255);
        auto at=[&](float x,float y) { Point p{x,y}; for (int r=0;r<e.rotation;++r) p={-p.y,p.x}; return screen({e.position.x+p.x,e.position.y+p.y}); };
        symbol(draw,e.kind,at,zoom,color);
        const float labelScale=std::clamp(zoom,.85f,1.2f);
        const float fontSize=ImGui::GetFontSize()*labelScale;
        auto text=[&](const std::string& content,ImVec2 position,ImU32 textColor,bool centered) {
            if (centered) position.x-=ImGui::CalcTextSize(content.c_str()).x*labelScale*.5f;
            draw->AddText(ImGui::GetFont(),fontSize,position,textColor,content.c_str());
        };
        const auto center=screen(e.position);
        const bool vertical=e.rotation%2!=0;
        auto label=vertical ? ImVec2(center.x+23*zoom,center.y-fontSize-2) : ImVec2(center.x,center.y-21*zoom-fontSize);
        auto valuePosition=vertical ? ImVec2(center.x+23*zoom,center.y+2) : ImVec2(center.x,center.y+21*zoom);
        text(e.name,label,color,!vertical);
        // Canvas labels stay compact; the table and tooltip retain full precision.
        auto value=SST::Prototypes::CapLab::Engineering(e.value,SST::Prototypes::CapLab::ElementUnit(e),4);
        text(value,valuePosition,IM_COL32(160,180,200,255),!vertical);
        if (e.kind==Kind::CPE) {
            char exponent[40]; std::snprintf(exponent,sizeof(exponent),"n = %.3g",e.secondary);
            text(exponent,{valuePosition.x,valuePosition.y+fontSize+2},IM_COL32(160,180,200,255),!vertical);
        }
        for (int n : e.nodes) {
            auto p=screen(d.terminal(n)); draw->AddCircleFilled(p,4,hitNode==n?IM_COL32(255,220,110,255):IM_COL32(110,195,235,255));
            if (d.ground==n) { draw->AddLine(p,{p.x,p.y+15},color,2); for (int j=0;j<3;++j) draw->AddLine({p.x-10.f+3*j,p.y+15.f+4*j},{p.x+10.f-3*j,p.y+15.f+4*j},color,2); }
            for (int port=0;port<2;++port) if (d.port[port]==n) { auto pc=port?IM_COL32(250,150,100,255):IM_COL32(100,240,170,255);
                draw->AddCircle(p,8,pc,20,2); draw->AddText({p.x-10,p.y-24},pc,port?"P-":"P+"); }
        }
    }
    if (pending>=0) draw->AddLine(screen(d.terminal(pending)),io.MousePos,IM_COL32(255,220,110,255),2);
    if (palette>=0 && hovered) draw->AddText(io.MousePos,IM_COL32(255,220,110,255),typeName(static_cast<Kind>(palette)));
    draw->PopClipRect();
    if (openValue) { ImGui::OpenPopup("Edit element value"); openValue=false; }
    ImGui::SetNextWindowSize(ImVec2(420,0),ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Edit element value")) {
        if (selected>=0 && selected<static_cast<int>(d.elements.size())) {
            ImGui::TextUnformatted(d.elements[selected].name.c_str());
            changed|=parameters(d.elements[selected]);
        }
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return changed;
}
}
