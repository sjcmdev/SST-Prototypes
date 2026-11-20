#include "Core/dspch.hpp"
#include "Presentation/LabWindows.hpp"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <sst/ui/Widgets/LabelledControls.hpp>

namespace SST::Prototypes::WindowChromeLab
{
    LabWindow::LabWindow(Kind kind, Ref<LabSelection> selection)
        : EditorWindow(kind == Kind::Plot ? "lab.plot" : kind == Kind::Table ? "lab.table" : "lab.tools",
            kind == Kind::Plot ? "Plot A" : kind == Kind::Table ? "Table A" : "Project Explorer", true,
            {.duplicable = kind != Kind::Tools, .projectItem = kind != Kind::Tools}),
          m_Kind(kind), m_Selection(std::move(selection)) {}

    void LabWindow::RenderContent()
    {
        ImGui::Text("Stable ID: %llu | Type: %s", static_cast<unsigned long long>(GetId()), GetType().c_str());
        if (m_Kind == Kind::Tools)
        {
            ImGui::Text("Revealed window: %llu", static_cast<unsigned long long>(m_Selection->selected));
            ImGui::TextWrapped("Right-click a title or dock tab for actions. Title buttons pin, maximize and minimize. Ctrl+Tab switches windows. Use Window for arrangements and named workspaces.");
            return;
        }
        if (m_Kind == Kind::Plot)
        {
            sst::ui::widgets::SliderFloat("View scale", &m_Scale, .1f, 5);
            auto scaled = m_Samples;
            for (auto &value : scaled) value *= m_Scale;
            ImGui::PlotLines("Samples", scaled.data(), int(scaled.size()), 0, nullptr, 0, 250, ImVec2(-1, 160));
        }
        else if (ImGui::BeginTable("samples", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Index"); ImGui::TableSetupColumn("Value"); ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < m_Samples.size(); ++i)
            {
                ImGui::PushID(int(i)); ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%zu", i);
                ImGui::TableNextColumn(); m_Dirty |= ImGui::InputFloat("##value", &m_Samples[i]); ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextWrapped("Duplicate owns an independent sample vector. Closing hides the view; dirty data is retained. The template Document window demonstrates real document saving.");
    }
    void LabWindow::RenderContextMenu()
    {
        ImGui::Separator();
        ImGui::TextDisabled("Lab extension: %zu independent samples", m_Samples.size());
        if (ImGui::MenuItem("Reveal this sample view", nullptr, false, Capabilities().projectItem)) Request({WindowAction::Reveal});
    }
    std::string LabWindow::SaveViewState() const
    {
        return nlohmann::json{{"scale", m_Scale}}.dump();
    }
    bool LabWindow::LoadViewState(const std::string &text)
    {
        try
        {
            const auto j = nlohmann::json::parse(text);
            const auto scale = j.at("scale").get<float>();
            if (!std::isfinite(scale) || scale < .1f || scale > 5) return false;
            m_Scale = scale; return true;
        }
        catch (const std::exception &) { return false; }
    }
}
