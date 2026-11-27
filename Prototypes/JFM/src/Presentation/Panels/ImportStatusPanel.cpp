#include "Core/dspch.hpp"

#include "Presentation/Panels/ImportStatusPanel.hpp"

#include <imgui.h>

#include "ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Events/JFMEvents.hpp"

namespace SST::Prototypes::JFM
{
	ImportStatusPanel::ImportStatusPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault, Ref<EventBus> eventBus)
		: EditorWindow("jfm.import-status", std::move(title), visibleByDefault, {.duplicable = true}), m_Store(std::move(store)), m_EventBus(eventBus)
    {
        if (eventBus) {
            AddSubscription(eventBus->Subscribe<ImportFailed>([this](const auto&) { SetVisible(true); }));
            AddSubscription(eventBus->Subscribe<ImportCompleted>([this](const auto& event) { if (!event.warnings.empty()) SetVisible(true); }));
            AddSubscription(eventBus->Subscribe<ProjectDataMaterializationCompleted>([this](const auto& event) { if (!event.warnings.empty()) SetVisible(true); }));
        }
    }

	WindowDuplicate ImportStatusPanel::Duplicate() const
	{
		return {CreateRef<ImportStatusPanel>(m_Store, GetTitle(), IsVisible(), m_EventBus), {}};
	}

	void ImportStatusPanel::RenderContent()
	{
		const bool running = m_Store != nullptr && m_Store->IsImportRunning();
		const std::string status = m_Store != nullptr ? m_Store->GetStatusMessage() : std::string{};
		const std::vector<std::string> warnings = m_Store != nullptr ? m_Store->GetLastImportWarnings() : std::vector<std::string>{};

        ImGui::TextUnformatted("Basic .dat import: native C++");
		ImGui::Text("Python runtime: %s", PythonInterpreter::IsRuntimeInitialized() ? "ready" : "unavailable");
		ImGui::Text("State: %s", running ? "running" : "idle");
		ImGui::TextWrapped("%s", status.c_str());

		if (!warnings.empty())
		{
			ImGui::Separator();
			for (const std::string &warning : warnings) {
                ImGui::Bullet(); ImGui::TextWrapped("%s", warning.c_str());
            }
		}

	}
} // namespace SST::Prototypes::JFM
