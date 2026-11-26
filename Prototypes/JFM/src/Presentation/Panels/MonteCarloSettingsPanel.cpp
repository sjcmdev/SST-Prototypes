#include "Core/dspch.hpp"

#include "Presentation/Panels/MonteCarloSettingsPanel.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include <algorithm>
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <cmath>
#include <optional>
#include <limits>
#include <random>

namespace SST::Prototypes::JFM
{
	namespace
	{
		std::optional<double> EstimateValue(const std::optional<JFMPrefitState> &prefit, const std::optional<JFMFitState> &fit, std::string_view name)
		{
			if (fit)
			{
				if (const auto value = fit->parameters.find(std::string(name)); value != fit->parameters.end() && std::isfinite(value->second.value))
					return value->second.value;
			}
			if (prefit)
			{
				if (const auto value = prefit->parameters.find(std::string(name)); value != prefit->parameters.end() && std::isfinite(value->second.value))
					return value->second.value;
			}
			return std::nullopt;
		}

		JFMParameterSolverSetting SuggestedBounds(std::string_view name, double value)
		{
			if (!std::isfinite(value))
				return {};
			if (name == "A")
				return {std::max(0.0, value - 0.5), std::min(5.0, value + 0.5), true};
			if (name == "alpha")
				return {std::max(0.1, value - 0.5), std::min(10.0, value + 0.5), true};
			const double magnitude = std::max(std::abs(value), 1.0e-30);
			if (name == "I0")
				return {std::max(1.0e-300, magnitude * 0.2), std::min(1.0, magnitude * 5.0), true};
			if (name == "Rs")
				return {std::max(0.0, value * 0.2), std::max(value * 5.0, 1.0e-9), true};
			if (name == "Rsh" || name == "Rsh2")
				return {std::max(1.0e-6, value * 0.2), std::min(1.0e15, std::max(value * 5.0, 1.0e-6)), true};
			if (name == "I_ph")
				return {0.0, std::max(magnitude * 5.0, 1.0e-12), true};
			return {value - magnitude, value + magnitude, true};
		}

		bool HasDefaultishBounds(std::string_view name, const JFMParameterSolverSetting &setting)
		{
			if (name == "I0")
				return setting.minimum <= 1.0e-299 && setting.maximum >= 1.0;
			if (name == "A")
				return setting.minimum <= 0.0 && setting.maximum >= 5.0;
			if (name == "Rs")
				return setting.minimum <= 0.0 && setting.maximum >= 1.0e9;
			if (name == "Rsh" || name == "Rsh2")
				return setting.minimum <= 1.0e-6 && setting.maximum >= 1.0e15;
			if (name == "alpha")
				return setting.minimum <= 0.1 && setting.maximum >= 10.0;
			if (name == "I_ph")
				return setting.minimum <= 0.0 && setting.maximum >= 1.0e6;
			return false;
		}

		bool ApplySuggestedBounds(JFMFitMonteCarloSettings &settings, const JFMCharacteristicInfo &info, std::string_view model, bool onlyDefaultish)
		{
			bool changed = false;
			for (std::string_view nameView : JFMModelParameterNames(model))
			{
				const std::string name{nameView};
				if (const std::optional<double> value = EstimateValue(info.prefit, info.fit, nameView))
				{
					JFMParameterSolverSetting &current = settings.parameters[name];
					if (onlyDefaultish && !HasDefaultishBounds(nameView, current))
						continue;
					JFMParameterSolverSetting suggested = SuggestedBounds(nameView, *value);
					suggested.monteCarloFree = current.monteCarloFree;
					if (current.minimum != suggested.minimum || current.maximum != suggested.maximum)
					{
						current = suggested;
						changed = true;
					}
				}
			}
			return changed;
		}

		std::uint32_t RandomSeed()
		{
			std::random_device device;
			return (static_cast<std::uint32_t>(device()) << 1U) ^ static_cast<std::uint32_t>(device());
		}
	} // namespace

	MonteCarloSettingsPanel::MonteCarloSettingsPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		std::string title,
		bool visibleByDefault)
		: EditorWindow("jfm.mc-settings", std::move(title), visibleByDefault, {.duplicable = true}),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry))
	{
	}

	WindowDuplicate MonteCarloSettingsPanel::Duplicate() const
	{
		return {CreateRef<MonteCarloSettingsPanel>(m_Store, m_Settings, m_EventBus, m_CommandRegistry, GetTitle(), IsVisible()), {}};
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

	void MonteCarloSettingsPanel::RunMonteCarlo(ProjectHandle project, const std::vector<CharacteristicId> &ids)
	{
		if (ids.empty() || m_EventBus == nullptr)
			return;
		m_Draft.monteCarloSeed = RandomSeed();
		ApplyDraft();
		m_EventBus->Queue(MonteCarloRequested{project, ids});
		m_Status = "MC queued, seed " + std::to_string(m_Draft.monteCarloSeed);
	}

	void MonteCarloSettingsPanel::RenderContent()
	{
		if (m_Store == nullptr || m_Settings == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");

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
		const ImVec2 buttonSize{std::max(150.0f, ImGui::CalcTextSize("Perform MC selected").x + 2 * ImGui::GetStyle().FramePadding.x), 0.0f};
		const auto runButton = [&](const char *label, const std::vector<CharacteristicId> &ids) {
			int limit = std::numeric_limits<int>::max();
			for (const auto &info : snapshot.characteristics)
				if (std::find(ids.begin(), ids.end(), info.id) != ids.end())
					limit = std::min(limit, JFMMonteCarloSampleLimit(info.fit ? info.fit->modelId : info.prefit ? info.prefit->modelId : "SD-RsRsh"));
			const bool invalid = m_Draft.monteCarloSamples < 1 || m_Draft.monteCarloSamples > limit;
			ImGui::BeginDisabled(ids.empty() || invalid);
			if (ImGui::Button(label, buttonSize)) RunMonteCarlo(project, ids);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip(ids.empty() ? "Select characteristics first." : "Sample count: 1 to %d per characteristic (64 MiB memory budget).", limit);
		};
		runButton("Perform MC selected", selectedIds);
		sst::ui::widgets::SameLineIfFits(buttonSize.x);
		runButton("Perform MC all", allIds);
		sst::ui::widgets::SameLineIfFits(buttonSize.x);
		if (ImGui::Button("Save MC config", buttonSize))
		{
			ApplyDraft();
			m_Settings->Save();
			m_Status = "MC config saved";
		}
		sst::ui::widgets::SameLineIfFits(buttonSize.x);
		if (ImGui::Button("Update", buttonSize))
			ApplyDraft();
		sst::ui::widgets::SameLineIfFits(buttonSize.x);
		if (ImGui::Button("Restore", buttonSize))
		{
			m_Draft = m_Settings->GetSnapshot().fitMonteCarlo;
			m_Status = "MC config restored";
		}

		if (ImGui::Button("All selected", buttonSize))
			m_BulkMode = true;
		sst::ui::widgets::SameLineIfFits(buttonSize.x);
		if (ImGui::Button("Select all", buttonSize) && !allIds.empty())
		{
			m_Store->SetSelectedCharacteristics(project, allIds, allIds.front());
			selectedIds = allIds;
			m_BulkMode = true;
		}
		const std::string selectedLabel = "Selected: " + std::to_string(selectedIds.size()) + " / " + std::to_string(allIds.size());
		sst::ui::widgets::SameLineIfFits(ImGui::CalcTextSize(selectedLabel.c_str()).x);
		ImGui::TextUnformatted(selectedLabel.c_str());

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
		if (sst::ui::widgets::BeginCombo("Model", mixed ? "Mixed" : model.c_str()))
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
				sst::ui::widgets::EndCombo();
		}

		if (!mixed)
		{
			for (CharacteristicId id : selectedIds)
			{
				if (const auto record = m_Store->FindCharacteristicRecord({project, id}))
					(void)ApplySuggestedBounds(m_Draft, record->info, model, true);
			}
		}

		if (m_BulkMode || selectedIds.size() > 1)
		{
			ImGui::SeparatorText("Bulk settings");
			if (RenderJFMFitMonteCarloSettings(m_Draft, model, "mc_settings_bulk"))
				m_Status = "MC config changed";
		}
		else
		{
			ImGui::SeparatorText("Characteristics");
			for (CharacteristicId id : selectedIds)
			{
				const auto record = m_Store->FindCharacteristicRecord({project, id});
				if (!record)
					continue;
				const std::string header = record->info.name + "##mc_char_" + std::to_string(id);
				if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					const std::string itemModel = record->info.fit ? record->info.fit->modelId : record->info.prefit ? record->info.prefit->modelId : model;
					if (RenderJFMFitMonteCarloSettings(m_Draft, itemModel, ("mc_char_settings_" + std::to_string(id)).c_str(), true, &record->info))
						m_Status = "MC config changed";
				}
			}
		}

		ImGui::SeparatorText("Scatter defaults");
		(void)RenderJFMMonteCarloScatterDefaults(m_Draft, model, "mc_scatter_defaults");

		if (ImGui::CollapsingHeader("Characteristic list", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const JFMCharacteristicInfo &info : snapshot.characteristics)
			{
				const bool selected = std::find(selectedIds.begin(), selectedIds.end(), info.id) != selectedIds.end();
				const std::string label = std::string(selected ? "[x] " : "[ ] ") + info.name + "##mc_list_" + std::to_string(info.id);
				if (ImGui::CollapsingHeader(label.c_str()))
				{
					const std::string itemModel = info.fit ? info.fit->modelId : info.prefit ? info.prefit->modelId : "SD-RsRsh";
					ImGui::Text("Model: %s", itemModel.c_str());
					if (info.prefit)
						ImGui::Text("Prefit: %s", info.prefit->status.c_str());
					if (info.fit)
						ImGui::Text("Fit: %s", info.fit->status.c_str());
					if (!m_BulkMode && selected)
						(void)RenderJFMFitMonteCarloSettings(m_Draft, itemModel, ("mc_list_settings_" + std::to_string(info.id)).c_str(), true, &info);
				}
			}
		}
		if (!m_Status.empty())
			ImGui::TextUnformatted(m_Status.c_str());
		ImGui::TextWrapped("%s", m_Store->GetStatusMessage(project).c_str());
	}
} // namespace SST::Prototypes::JFM
