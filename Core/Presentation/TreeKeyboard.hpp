#pragma once
#include <imgui.h>

namespace SST {
inline bool TreeKeyboardFocused() {
    const auto& io=ImGui::GetIO();
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
}
inline bool TreeDeletePressed() {
    const auto& io=ImGui::GetIO();
    return TreeKeyboardFocused() && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.KeySuper &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete,false) || ImGui::IsKeyPressed(ImGuiKey_Backspace,false));
}
}
