#pragma once

#include "Presentation/Panels/EditorWindow.hpp"
#include "Vendor/ImGuiNotify/win32Example/fonts/IconsFontAwesome6.h"
#include <functional>

namespace SST
{
    // Migration adapter: the owning IPanel supplies content synchronously, never retained by the registry.
    class HostedEditorWindow final : public EditorWindow
    {
    public:
        explicit HostedEditorWindow(const std::string &legacyName, bool preserveIdentity = true, bool auxiliary = false)
            : EditorWindow("hosted." + legacyName.substr(legacyName.rfind("###") == std::string::npos ? 0 : legacyName.rfind("###")), legacyName.substr(0, legacyName.rfind("###")), true,
                {.renamable = false, .auxiliary = auxiliary}), m_LegacyName(legacyName), m_PreserveIdentity(preserveIdentity) {}
        void SetLegacyName(const std::string &name)
        {
            m_LegacyName = name;
            SetTitle(name.substr(0, name.rfind("###")));
        }
        void Render() override {} // The owner renders in its existing data/undo scope.
        bool Render(const std::function<void()> &content, int flags = 0)
        {
            m_Content = &content; m_Flags = flags; m_Rendered = false;
            EditorWindow::Render();
            m_Content = nullptr;
            return m_Rendered;
        }
        std::string ImGuiName() const override
        {
            if (!m_PreserveIdentity) return EditorWindow::ImGuiName();
            const auto suffix = m_LegacyName.rfind("###");
            return std::string(ICON_FA_WINDOW_MAXIMIZE " ") + GetTitle() + "###" +
                (suffix == std::string::npos ? m_LegacyName : m_LegacyName.substr(suffix + 3));
        }
        bool PreserveIdentity() const { return m_PreserveIdentity; }
        bool claimed = false;
    protected:
        std::string SaveViewState() const override { return m_PreserveIdentity ? "legacy" : "instance"; }
        bool LoadViewState(const std::string &state) override
        {
            if (state != "legacy" && state != "instance") return false;
            m_PreserveIdentity = state == "legacy"; return true;
        }
    private:
        void RenderContent() override { m_Rendered = true; (*m_Content)(); }
        int WindowFlags() const override { return m_Flags; }
        std::string m_LegacyName;
        const std::function<void()> *m_Content = nullptr;
        bool m_PreserveIdentity = true;
        int m_Flags = 0;
        bool m_Rendered = false;
    };

}
