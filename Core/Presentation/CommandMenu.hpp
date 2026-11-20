#pragma once
#include "Core/Commands/CommandRegistry.hpp"

namespace SST {
// Optional observer, called right after each top-level BeginMenu (last ImGui item = the menu title).
void SetCommandMenuHook(std::function<void(const std::string &root, bool open)> hook);
// Called inside BeginMainMenuBar. The Window extension retains the shared workspace actions.
void RenderCommandMenu(CommandRegistry &registry, const std::string &appMenu,
                       const std::function<void()> &windowMenu = {});
}
