#include "Core/dspch.hpp"
#include "TutorialOverlay.hpp"
#include "Core/Presentation/Panels/HostedWindows.hpp"

namespace SST {
void TutorialOverlay::Window(const std::string &id) {
    const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    targets[id] = {pos, end, pos, end, ImGui::GetFrameCount()};
}
void TutorialOverlay::Item(const std::string &window, const std::string &id) {
    if (ImGui::IsItemVisible()) {
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax(), pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        targets[window + "/" + id] = {min, max, ImVec2(pos.x, min.y), ImVec2(pos.x + size.x, max.y), ImGui::GetFrameCount()};
    }
}
bool TutorialOverlay::Draw(Tutorial &tutorial, HostedWindows *windows) {
    highlighted = false;
    geometry = {};
    const auto *step = tutorial.Step();
    if (!step) { placedStep.clear(); return false; }
    const auto &target = step->at("target");
    auto key = target.at("window").get<std::string>();
    if (target.contains("item")) key += "/" + target.at("item").get<std::string>();
    const auto found = targets.find(key);
    const auto *vp = ImGui::GetMainViewport();
    const auto &style = ImGui::GetStyle();
    const float gap = style.ItemSpacing.y + 6;
    const ImVec2 lo(vp->WorkPos.x + gap, vp->WorkPos.y + gap), hi(vp->WorkPos.x + vp->WorkSize.x - gap, vp->WorkPos.y + vp->WorkSize.y - gap);
    highlighted = found != targets.end() && found->second.frame == ImGui::GetFrameCount();
    float width = std::min(24 * ImGui::GetFontSize(), std::max(1.f, hi.x - lo.x));
    if (highlighted) {
        const auto &rect = found->second;
        const float sideRoom = std::max(hi.x - rect.rowMax.x - gap, rect.rowMin.x - gap - lo.x);
        if (sideRoom >= 14 * ImGui::GetFontSize()) width = std::min(width, sideRoom);
    }
    const float height = ImGui::CalcTextSize(step->at("instruction").get_ref<const std::string &>().c_str(), nullptr, false, width - 2 * style.WindowPadding.x).y
        + 5 * ImGui::GetFrameHeightWithSpacing() + 2 * style.WindowPadding.y;
    ImVec2 position(std::max(lo.x, hi.x - width), lo.y);
    if (highlighted) {
        const auto &rect = found->second;
        const float x = std::clamp(rect.min.x, lo.x, std::max(lo.x, hi.x - width));
        const float y = std::clamp(rect.min.y, lo.y, std::max(lo.y, hi.y - height));
        if (rect.rowMax.x + gap + width <= hi.x) position = ImVec2(rect.rowMax.x + gap, y);
        else if (rect.rowMin.x - gap - width >= lo.x) position = ImVec2(rect.rowMin.x - gap - width, y);
        else if (rect.rowMax.y + gap + height <= hi.y) position = ImVec2(x, rect.rowMax.y + gap);
        else position = ImVec2(x, std::max(lo.y, rect.rowMin.y - gap - height));
    }
    const auto report = tutorial.Report();
    const auto stepKey = report.at("tutorial_id").get<std::string>() + "/" + step->at("id").get<std::string>();
    // Placement is only the initial suggestion: once the user moved or resized the coach, their placement wins (and is restored for the next lesson).
    if (userMoved && placedStep.empty()) {
        ImGui::SetNextWindowPos(ImVec2(userRect.x, userRect.y));
        ImGui::SetNextWindowSize(ImVec2(userRect.z, userRect.w));
        placedStep = stepKey;
    } else if (!userMoved && (placedStep != stepKey || reposition)) {
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(ImVec2(width, height));
        baseline = false;
    }
    reposition = false;
    if (highlighted) placedStep = stepKey;
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(width, 12 * ImGui::GetFontSize()), 4 * ImGui::GetFrameHeightWithSpacing()), ImVec2(std::max(1.f, hi.x-lo.x), std::max(1.f, hi.y-lo.y)));
    bool stop = false;
    const auto content = [&] {
        const bool learn = report.at("mode") == "learn";
        ImGui::Text("%s%zu / %zu", learn ? "Step " : (report.at("mode").get<std::string>() + " | ").c_str(), report.at("step_index").get<std::size_t>() + 1, report.at("step_count").get<std::size_t>());
        ImGui::TextWrapped("%s", step->at("instruction").get<std::string>().c_str());
        if (!highlighted) ImGui::TextWrapped("Open %s to see the highlighted control.", target.at("window").get<std::string>().c_str());
        if (learn && tutorial.ShownMe()) ImGui::TextDisabled("Done. Press Next to continue.");
        else if (learn && tutorial.ShowMeBusy()) ImGui::TextDisabled("Watch the highlighted control...");
        else if (learn && tutorial.AlreadyDone()) ImGui::TextDisabled("This is already the case. Press Next.");
        if (learn) {
            ImGui::BeginDisabled(!tutorial.CanGoBack());
            if (ImGui::Button("< Back")) tutorial.Back();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(tutorial.ShowMeBusy());
            if (ImGui::Button(tutorial.AlreadyDone() ? "Next >" : "Skip >")) tutorial.Skip();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(tutorial.ShowMeBusy());
            if (ImGui::Button("Show me")) tutorial.ShowMe();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Performs this one step for you");
            ImGui::SameLine();
        }
        stop = ImGui::Button("Exit");
        if (highlighted) {
            const auto &rect = found->second;
            const ImVec2 min(rect.min.x - 3, rect.min.y - 3), max(rect.max.x + 3, rect.max.y + 3);
            const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            geometry.targetMin = rect.min; geometry.targetMax = rect.max; geometry.coachMin = pos; geometry.coachMax = ImVec2(pos.x + size.x, pos.y + size.y);
            const auto wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
            if (!baseline) { basePos = wp; baseSize = ws; baseline = true; }
            else if (std::abs(wp.x - basePos.x) > 1.5f || std::abs(wp.y - basePos.y) > 1.5f || std::abs(ws.x - baseSize.x) > 1.5f || std::abs(ws.y - baseSize.y) > 1.5f) userMoved = true;
            if (userMoved) userRect = ImVec4(wp.x, wp.y, ws.x, ws.y);
            reposition = !userMoved && !ImGui::IsWindowDocked() &&
                ((pos.x < rect.rowMax.x && pos.x+size.x > rect.rowMin.x && pos.y < rect.rowMax.y && pos.y+size.y > rect.rowMin.y)
                 || pos.x < lo.x || pos.y < lo.y || pos.x+size.x > hi.x || pos.y+size.y > hi.y);
            auto *draw = ImGui::GetForegroundDrawList();
            const auto colour = ImGui::GetColorU32(ImGuiCol_NavHighlight);
            const double anim = tutorial.ShowMeAnim();
            const bool hold = tutorial.ShowMeHold();
            draw->AddRect(min, max, colour, style.FrameRounding, 0, hold ? 4.f : 2.f);
            if (hold) draw->AddRect(ImVec2(min.x - 6, min.y - 6), ImVec2(max.x + 6, max.y + 6), colour, style.FrameRounding + 3, 0, 2);
            if (anim >= 0) { // pointer glides from the coach to the target (ease in-out)
                const float e = float(anim * anim * (3 - 2 * anim));
                const ImVec2 to((min.x + max.x) / 2, (min.y + max.y) / 2), from(pos.x + size.x / 2, pos.y + size.y / 2);
                const ImVec2 p(from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e);
                draw->AddCircleFilled(p, 9, IM_COL32(255, 200, 0, 255));
                draw->AddCircle(p, 9, IM_COL32(0, 0, 0, 255), 0, 2);
                draw->AddTriangleFilled(ImVec2(p.x, p.y), ImVec2(p.x + 4, p.y + 22), ImVec2(p.x + 16, p.y + 14), IM_COL32(255, 255, 255, 255));
            }
            const ImVec2 centre((min.x + max.x) / 2, (min.y + max.y) / 2);
            ImVec2 start(std::clamp(centre.x, pos.x, pos.x + size.x), std::clamp(centre.y, pos.y, pos.y + size.y));
            const float dx = start.x - centre.x, dy = start.y - centre.y;
            const float scale = std::max(std::abs(dx) / ((max.x-min.x)/2), std::abs(dy) / ((max.y-min.y)/2));
            if (scale > 1) {
                ImVec2 end(centre.x + dx/scale, centre.y + dy/scale);
                // A side coach points along the row's top edge, clear of neighbouring button labels.
                const float corner = std::min(style.FrameRounding, (max.y-min.y)/2);
                if (target.contains("item") && pos.x >= rect.rowMax.x) {start=pos;end=ImVec2(max.x,min.y+corner);}
                else if (target.contains("item") && pos.x+size.x <= rect.rowMin.x) {start=ImVec2(pos.x+size.x,pos.y);end=ImVec2(min.x,min.y+corner);}
                const float ax=start.x-end.x, ay=start.y-end.y, length=std::max(1.f,std::sqrt(ax*ax+ay*ay));
                const ImVec2 along(ax/length*10, ay/length*10), side(-ay/length*4, ax/length*4);
                geometry.arrow = true; geometry.tip = end;
                draw->AddLine(start, end, colour, 2);
                draw->AddTriangleFilled(end, ImVec2(end.x+along.x+side.x, end.y+along.y+side.y), ImVec2(end.x+along.x-side.x, end.y+along.y-side.y), colour);
            }
        }
    };
    const auto flags = ImGuiWindowFlags_NoSavedSettings;
    bool open = true;
    if (windows) windows->Render("Tutorial coach###sst-tutorial", open, flags, content, true);
    else {
        coach.SetVisible(true);
        coach.Render(content, flags);
    }
    return stop || !open;
}
}
