#pragma once

#include "Core/App/Application.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Presentation/Panels/HostedEditorWindow.hpp"
#include <functional>
#include <map>

namespace SST
{
    class HostedWindows
    {
    public:
        HostedWindows() = default;
        HostedWindows(const HostedWindows &) : m_PreserveIdentity(false) {} // A duplicated owner gets independent window identities.
        void Activate(const std::string &name)
        {
            const HostedEditorWindow candidate(name, m_PreserveIdentity);
            auto workspace = Application::Get().GetEditorLayer().GetWindows();
            for (auto &window : workspace->Windows())
                if (auto hosted = std::dynamic_pointer_cast<HostedEditorWindow>(window);
                    hosted && hosted->GetType() == candidate.GetType() && hosted->PreserveIdentity() == m_PreserveIdentity)
                {
                    (void)workspace->Execute({WindowAction::Activate, hosted->GetId()});
                    return;
                }
        }
        void UseInstanceIdentity() { m_PreserveIdentity = false; }
        void BeginFrame() { for (auto &[key, entry] : m_Windows) entry.used = false; }
        void EndFrame()
        {
            for (auto it = m_Windows.begin(); it != m_Windows.end();)
                if (!it->second.used)
                {
                    Application::Get().GetEditorLayer().UnregisterWindow(it->second.window->GetId());
                    it = m_Windows.erase(it);
                }
                else ++it;
        }
        ~HostedWindows() { for (auto &[key, entry] : m_Windows) entry.window->SetVisible(false); }
        bool Render(const std::string &name, bool &open, int flags, const std::function<void()> &content)
        {
            auto &editor = Application::Get().GetEditorLayer();
            const auto suffix = name.rfind("###");
            auto &entry = m_Windows[suffix == std::string::npos ? name : name.substr(suffix)];
            auto &window = entry.window;
            if (!window)
            {
                auto candidate = CreateRef<HostedEditorWindow>(name, m_PreserveIdentity);
                for (auto &saved : editor.GetWindows()->Windows())
                    if (auto hosted = std::dynamic_pointer_cast<HostedEditorWindow>(saved);
                        hosted && !hosted->claimed && hosted->GetType() == candidate->GetType() &&
                        hosted->PreserveIdentity() == m_PreserveIdentity)
                    {
                        window = hosted; break;
                    }
                if (!window)
                {
                    window = std::move(candidate);
                    editor.RegisterWindow(window);
                    window->SetVisible(open);
                }
                window->claimed = true;
            }
            else if (open != entry.lastOpen) window->SetVisible(open);
            if (editor.FindPanel(window->GetId()).expired())
                editor.RegisterWindow(window, window->GetId()); // Undo may have removed a host's child entries.
            entry.used = true;
            window->SetLegacyName(name);
            const bool wasOpen = open;
            const bool rendered = window->Render(content, flags);
            if (open != wasOpen) window->SetVisible(open);
            entry.lastOpen = open = window->IsVisible();
            return rendered;
        }
    private:
        bool m_PreserveIdentity = true;
        struct Entry { Ref<HostedEditorWindow> window; bool used = false, lastOpen = true; };
        std::map<std::string, Entry> m_Windows;
    };
}
