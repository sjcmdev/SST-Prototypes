#include "Core/dspch.hpp"

#include "Presentation/Panels/ImportStatusPanel.hpp"

#include <imgui.h>

#include "IO/JFMPythonImportBridge.hpp"

namespace SST::Prototypes::JFM
{
	ImportStatusPanel::ImportStatusPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
	{
	}

	Ref<IPanel> ImportStatusPanel::Clone() const
	{
		return CreateRef<ImportStatusPanel>(*this);
	}

	void ImportStatusPanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const bool running = m_Store != nullptr && m_Store->IsImportRunning();
		const std::string status = m_Store != nullptr ? m_Store->GetStatusMessage() : std::string{};
		const std::vector<std::string> warnings = m_Store != nullptr ? m_Store->GetLastImportWarnings() : std::vector<std::string>{};

		ImGui::Text("Python bridge: %s", JFMPythonImportBridge::IsAvailable() ? "available" : "unavailable");
		ImGui::Text("State: %s", running ? "running" : "idle");
		ImGui::TextWrapped("%s", status.c_str());

		if (!warnings.empty())
		{
			ImGui::Separator();
			for (const std::string &warning : warnings)
				ImGui::BulletText("%s", warning.c_str());
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
