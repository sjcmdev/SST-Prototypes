#include "Core/dspch.hpp"

#include "Presentation/Panels/ImportStatusPanel.hpp"

#include <imgui.h>

#include "IO/JFMPythonImportBridge.hpp"

namespace SST::Prototypes::JFM
{
	ImportStatusPanel::ImportStatusPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: EditorWindow("jfm.import-status", std::move(title), visibleByDefault, {.duplicable = true}), m_Store(std::move(store))
	{
	}

	WindowDuplicate ImportStatusPanel::Duplicate() const
	{
		return {CreateRef<ImportStatusPanel>(*this), {}};
	}

	void ImportStatusPanel::RenderContent()
	{
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

	}
} // namespace SST::Prototypes::JFM
