#include "Core/dspch.hpp"
#include "CommandMenu.hpp"
#include <imgui.h>
#include <set>

namespace SST {
namespace {
std::function<void(const std::string &, bool)> g_MenuHook;
void Branch(CommandRegistry &registry, const std::vector<CommandMeta> &commands, const std::string &prefix) {
    std::set<std::string> submenus;
    for (const auto &command : commands) {
        if (!command.menuPath.starts_with(prefix)) continue;
        const auto rest = command.menuPath.substr(prefix.size());
        const auto slash = rest.find('/');
        if (slash != std::string::npos) {
            const auto name = rest.substr(0, slash);
            if (submenus.insert(name).second && ImGui::BeginMenu(name.c_str())) {
                Branch(registry, commands, prefix + name + "/"); ImGui::EndMenu();
            }
        } else {
            ImGui::PushID(command.id.value.c_str());
            if (ImGui::MenuItem(rest.c_str(), command.shortcut.c_str(), command.checked && command.checked(), registry.CanExecute(command.id).HasValue()))
                (void)registry.Execute(command.id);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !command.description.empty())ImGui::SetTooltip("%s",command.description.c_str());
            ImGui::PopID();
        }
    }
}
}
void SetCommandMenuHook(std::function<void(const std::string &, bool)> hook) { g_MenuHook = std::move(hook); }
void RenderCommandMenu(CommandRegistry &registry, const std::string &appMenu, const std::function<void()> &windowMenu) {
    auto commands = registry.ListCommands();
    std::sort(commands.begin(), commands.end(), [](const auto &a, const auto &b) { return a.menuPath < b.menuPath; });
    for (const auto &root : {std::string("File"), std::string("Edit"), std::string("View"), appMenu,
                             std::string("Tools"), std::string("Tutorials"), std::string("Window"), std::string("Help")}) {
        if (root == "Window" && windowMenu) { windowMenu(); continue; }
        const bool open = ImGui::BeginMenu(root.c_str());
        if (g_MenuHook) g_MenuHook(root, open);
        if (open) { Branch(registry, commands, root + "/"); ImGui::EndMenu(); }
    }
}
}
