#pragma once
#include "Core/Commands/CommandRegistry.hpp"

namespace SST {
// Called inside BeginMainMenuBar. The Window extension retains the shared workspace actions.
void RenderCommandMenu(CommandRegistry &registry, const std::string &appMenu,
                       const std::function<void()> &windowMenu = {});
}
