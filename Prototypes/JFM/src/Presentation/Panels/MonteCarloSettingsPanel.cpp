#include "Core/dspch.hpp"

#include "Presentation/Panels/MonteCarloSettingsPanel.hpp"

#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include <algorithm>
#include <imgui.h>

namespace SST::Prototypes::JFM
{
	MonteCarloSettingsPanel::MonteCarloSettingsPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry))
	{
	}

	Ref<IPanel> MonteCarloSettingsPanel::Clone() const
	{
		return CreateRef<MonteCarloSettingsPanel>(m_Store, m_Settings, m_EventBus, m_CommandRegistry, GetTitle(), IsVisible());
	}

	void MonteCarloSettingsPanel::Execute(const char *commandId, CommandContext context) const
	{
		if (auto registry = m_CommandRegistry.lock())
		{
			context.SetContextId(ContextID{"jfm.mc_settings"});
			context.SetSource("MC Settings");
			(void)registry->Execute(CommandID{commandId}, std::move(context));
		}
	}

	void MonteCarloSettingsPanel::ApplyDraft()
	{
		if (m_Settings != nullptr)
		{
			m_Settings->SetFitMonteCarloSettings(m_Draft);
			m_Draft = m_Settings->GetSnapshot().fitMonteCarlo;
		}
		m_Status = "MC config updated";
	}

	void MonteCarloSettingsPanel::Render()
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

		if (m_Store == nullptr || m_Settings == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}
		if (!m_DraftInitialized)
		{
			m_Draft = m_Settings->GetSnapshot().fitMonteCarlo;
			m_DraftInitialized = true;
		}

		const ProjectHandle project = m_Store->GetActiveProjectHandle();
		const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
		std::vector<CharacteristicId> allIds;
		allIds.reserve(snapshot.characteristics.size());
		for (const JFMCharacteristicInfo &info : snapshot.characteristics)
			allIds.push_back(info.id);
		std::vector<CharacteristicId> selectedIds = m_Store->GetSelectedCharacteristicIds();
		bool selectAll = !allIds.empty() && selectedIds.size() == allIds.size();
		if (ImGui::Checkbox("Select all", &selectAll))
		{
			if (selectAll)
				m_Store->SetSelectedCharacteristics(project, allIds, allIds.front());
			else
			{
				const CharacteristicId primary = m_Store->GetSelectedCharacteristicId();
				m_Store->SetSelectedCharacteristics(project,
					primary == InvalidCharacteristicId ? std::vector<CharacteristicId>{} : std::vector<CharacteristicId>{primary}, primary);
			}
			selectedIds = m_Store->GetSelectedCharacteristicIds();
		}
		ImGui::SameLine();
		ImGui::Text("Selected: %d / %d", static_cast<int>(selectedIds.size()), static_cast<int>(allIds.size()));

		std::string model = "SD-RsRsh";
		bool mixed = false;
		for (std::size_t index = 0; index < selectedIds.size(); ++index)
		{
			const auto state = m_Store->GetPrefitState({project, selectedIds[index]});
			const std::string candidate = state ? state->modelId : "SD-RsRsh";
			if (index == 0)
				model = candidate;
			else if (candidate != model)
				mixed = true;
		}
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::BeginCombo("Model", mixed ? "Mixed" : model.c_str()))
		{
			for (const char *candidate : PrefitModels)
			{
				if (ImGui::Selectable(candidate, !mixed && model == candidate) && !selectedIds.empty())
				{
					CommandContext context;
					context.Set("project", project);
					context.Set("characteristic_ids", selectedIds);
					context.Set("model_id", std::string(candidate));
					Execute(JFMSetPrefitModelCommandId, std::move(context));
					model = candidate;
					mixed = false;
				}
			}
			ImGui::EndCombo();
		}

		(void)RenderJFMFitMonteCarloSettings(m_Draft, model, "mc_settings");
		ImGui::SeparatorText("Scatter defaults");
		(void)RenderJFMMonteCarloScatterDefaults(m_Draft, "mc_scatter_defaults");

		if (ImGui::Button("Update"))
			ApplyDraft();
		ImGui::SameLine();
		if (ImGui::Button("Restore"))
		{
			m_Draft = m_Settings->GetSnapshot().fitMonteCarlo;
			m_Status = "MC config restored";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save MC config"))
		{
			ApplyDraft();
			m_Settings->Save();
			m_Status = "MC config saved";
		}

		ImGui::BeginDisabled(selectedIds.empty());
		if (ImGui::Button("Perform MC on selected"))
		{
			ApplyDraft();
			if (m_EventBus != nullptr)
				m_EventBus->Queue(MonteCarloRequested{project, selectedIds});
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(allIds.empty());
		if (ImGui::Button("Perform MC on all"))
		{
			ApplyDraft();
			if (m_EventBus != nullptr)
				m_EventBus->Queue(MonteCarloRequested{project, allIds});
		}
		ImGui::EndDisabled();
		if (!m_Status.empty())
			ImGui::TextUnformatted(m_Status.c_str());

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
