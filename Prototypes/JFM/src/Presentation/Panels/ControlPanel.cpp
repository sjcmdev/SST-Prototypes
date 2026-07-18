#include "Core/dspch.hpp"

#include "Presentation/Panels/ControlPanel.hpp"

#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace SST::Prototypes::JFM
{
	namespace ControlPanelDetail
	{
		std::array<float, 4> colorFromMetadata(const JFMCharacteristicInfo &info)
		{
			if (const auto it = info.metadata.find("plot_color"); it != info.metadata.end())
			{
				std::array<float, 4> color = {0.95f, 0.45f, 0.08f, 1.0f};
				if (std::sscanf(it->second.c_str(), "%f,%f,%f,%f", &color[0], &color[1], &color[2], &color[3]) >= 3)
					return color;
			}
			return {0.95f, 0.45f, 0.08f, 1.0f};
		}

		std::string colorToMetadata(const std::array<float, 4> &color)
		{
			char buffer[96]{};
			std::snprintf(buffer, sizeof(buffer), "%.6g,%.6g,%.6g,%.6g", color[0], color[1], color[2], color[3]);
			return buffer;
		}

		int indexForVoltage(const std::vector<double> &voltage, double value)
		{
			if (voltage.empty())
				return 0;
			const auto it = std::lower_bound(voltage.begin(), voltage.end(), value);
			if (it == voltage.end())
				return static_cast<int>(voltage.size() - 1);
			return static_cast<int>(std::distance(voltage.begin(), it));
		}
	} // namespace ControlPanelDetail
	using namespace ControlPanelDetail;

	ControlPanel::ControlPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<UndoStack> undoStack)
		: IPanel("Control Panel", true),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_UndoStack(std::move(undoStack))
	{
	}

	Ref<IPanel> ControlPanel::Clone() const
	{
		return CreateRef<ControlPanel>(*this);
	}

	void ControlPanel::Execute(const char *commandId, CommandContext context) const
	{
		if (auto registry = m_CommandRegistry.lock())
		{
			context.SetContextId(ContextID{"jfm.control_panel"});
			context.SetSource("Control Panel");
			(void)registry->Execute(CommandID{commandId}, std::move(context));
		}
	}

	void ControlPanel::FinishRangeUndoGroup(bool cancel)
	{
		if (m_RangeUndoGroupOpen)
		{
			if (auto undoStack = m_UndoStack.lock())
			{
				if (cancel)
					(void)undoStack->CancelGroup();
				else
					(void)undoStack->EndGroup();
			}
		}
		m_RangeUndoGroupOpen = false;
		m_RangeHeldLastFrame = false;
	}

	void ControlPanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			FinishRangeUndoGroup();
			ImGui::End();
			SetVisible(visible);
			return;
		}
		const ImGuiIO &io = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
		{
			const bool redo = (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false);
			const bool undo = !redo && ImGui::IsKeyPressed(ImGuiKey_Z, false);
			if (undo || redo)
			{
				FinishRangeUndoGroup();
				if (auto registry = m_CommandRegistry.lock())
					(void)registry->Execute(CommandID{redo ? "edit.redo" : "edit.undo"});
			}
		}

		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const ProjectHandle project = m_Store->GetActiveProjectHandle();
		const std::vector<CharacteristicId> ids = m_Store->GetSelectedCharacteristicIds();
		const auto activeRecord = m_Store->GetSelectedCharacteristicRecord();
		if (ids.empty() || !activeRecord)
		{
			FinishRangeUndoGroup();
			ImGui::TextDisabled("No characteristic selected");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		ImGui::Text("Selected: %d", static_cast<int>(ids.size()));
		RenderModelControls(project, ids);
		RenderPrefitDisplayControls();
		RenderColorControls(project, ids, activeRecord->info);
		RenderRangeControls(project, ids, *activeRecord);
		RenderSolverControls(project, ids, activeRecord->info);
		RenderParameterTable(activeRecord->info);

		ImGui::End();
		SetVisible(visible);
	}

	void ControlPanel::RenderModelControls(ProjectHandle project, const std::vector<CharacteristicId> &ids)
	{
		std::string model = "SD-RsRsh";
		bool mixed = false;
		for (std::size_t index = 0; index < ids.size(); ++index)
		{
			const auto state = m_Store->GetPrefitState({project, ids[index]});
			const std::string candidate = state ? state->modelId : "SD-RsRsh";
			if (index == 0)
				model = candidate;
			else if (candidate != model)
				mixed = true;
		}

		if (ImGui::BeginCombo("Model", mixed ? "Mixed" : model.c_str()))
		{
			for (const char *candidate : PrefitModels)
			{
				if (ImGui::Selectable(candidate, !mixed && model == candidate))
				{
					CommandContext context;
					context.Set("project", project);
					context.Set("characteristic_ids", ids);
					context.Set("model_id", std::string(candidate));
					Execute(JFMSetPrefitModelCommandId, std::move(context));
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::Button("Prefit") && m_EventBus != nullptr)
			m_EventBus->Queue(PrefitRequested{project, ids});
		ImGui::SameLine();
		if (ImGui::Button("Fit") && m_EventBus != nullptr)
			m_EventBus->Queue(FitRequested{project, ids});
	}

	void ControlPanel::RenderSolverControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo)
	{
		if (m_Settings == nullptr)
			return;
		ImGui::SeparatorText("Fit & Monte Carlo");
		JFMUserSettings settings = m_Settings->GetSnapshot();
		const std::string model = activeInfo.prefit ? activeInfo.prefit->modelId : "SD-RsRsh";
		if (RenderJFMFitMonteCarloSettings(settings.fitMonteCarlo, model, "control_panel"))
			m_Settings->SetFitMonteCarloSettings(std::move(settings.fitMonteCarlo));
		if (ImGui::Button("Run Monte Carlo") && m_EventBus != nullptr)
			m_EventBus->Queue(MonteCarloRequested{project, ids});

		if (activeInfo.fit)
		{
			ImGui::SameLine();
			ImGui::Text("Fit: %s, reduced chi2 %.6g", activeInfo.fit->status.c_str(), activeInfo.fit->reducedChiSquared);
			for (const std::string &warning : activeInfo.fit->warnings)
				ImGui::TextWrapped("Fit warning: %s", warning.c_str());
		}
		if (activeInfo.monteCarlo)
		{
			ImGui::Text("MC: %s, %d/%d converged, %s, %.3f ms",
				activeInfo.monteCarlo->status.c_str(),
				activeInfo.monteCarlo->acceptedCount,
				activeInfo.monteCarlo->sampleCount,
				activeInfo.monteCarlo->backend.c_str(),
				activeInfo.monteCarlo->totalMilliseconds);
			if (!activeInfo.monteCarlo->outputPath.Empty())
				ImGui::TextWrapped("Output: %s", activeInfo.monteCarlo->outputPath.String().c_str());
			for (const std::string &warning : activeInfo.monteCarlo->warnings)
				ImGui::TextWrapped("MC warning: %s", warning.c_str());
		}
		if (Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult({project, activeInfo.id}); result != nullptr)
			RenderJFMCovariancePlot(*result, m_CovarianceX, m_CovarianceY, "control_panel_covariance");
	}

	void ControlPanel::RenderPrefitDisplayControls()
	{
		if (m_Settings == nullptr)
			return;
		JFMUserSettings settings = m_Settings->GetSnapshot();
		bool changed = false;
		changed |= ImGui::Checkbox("Show prefit", &settings.showPrefitCurves);
		ImGui::SameLine();
		changed |= ImGui::ColorEdit4("Prefit color", settings.prefitCurveColor.data(), ImGuiColorEditFlags_NoInputs);
		changed |= ImGui::Checkbox("Fit range only", &settings.prefitCurvesFitRangeOnly);
		ImGui::SetNextItemWidth(160.0f);
		changed |= ImGui::SliderFloat("Prefit thickness", &settings.prefitCurveLineWeight, 0.5f, 8.0f, "%.1f");
		if (changed)
		{
			m_Settings->SetPrefitCurveDefaults(
				settings.showPrefitCurves,
				settings.prefitCurveColor,
				settings.prefitCurveLineWeight,
				settings.prefitCurvesFitRangeOnly,
				settings.hoveredCurveLineWeight);
		}
	}

	void ControlPanel::RenderColorControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo)
	{
		if (ids.size() == 1)
			m_ColorDraft = colorFromMetadata(activeInfo);
		ImGui::ColorEdit4("Color", m_ColorDraft.data(), ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		if (ImGui::Button("Apply color"))
		{
			CommandContext context;
			context.Set("project", project);
			context.Set("characteristic_ids", ids);
			context.Set("plot_color", colorToMetadata(m_ColorDraft));
			Execute(JFMSetPlotColorCommandId, std::move(context));
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear color"))
		{
			CommandContext context;
			context.Set("project", project);
			context.Set("characteristic_ids", ids);
			context.Set("plot_color", std::string{});
			Execute(JFMSetPlotColorCommandId, std::move(context));
		}
	}

	void ControlPanel::RenderRangeControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicRecord &activeRecord)
	{
		if (activeRecord.data == nullptr || activeRecord.data->voltage.empty())
		{
			FinishRangeUndoGroup();
			ImGui::TextDisabled("No voltage data");
			return;
		}

		const std::vector<double> &voltage = activeRecord.data->voltage;
		const int maximumIndex = static_cast<int>(voltage.size() - 1);
		if (m_RangeCharacteristic != activeRecord.info.id)
		{
			FinishRangeUndoGroup();
			m_RangeCharacteristic = activeRecord.info.id;
			m_LowerIndex = 0;
			m_UpperIndex = maximumIndex;
			if (activeRecord.info.prefit)
			{
				const JFMFitRange &range = activeRecord.info.prefit->fitRange;
				if (range.upperVoltage > range.lowerVoltage)
				{
					m_LowerIndex = indexForVoltage(voltage, range.lowerVoltage);
					m_UpperIndex = indexForVoltage(voltage, range.upperVoltage);
				}
				if (activeRecord.info.prefit->manualLowerVoltage && activeRecord.info.prefit->manualUpperVoltage)
				{
					m_LowerIndex = indexForVoltage(voltage, *activeRecord.info.prefit->manualLowerVoltage);
					m_UpperIndex = indexForVoltage(voltage, *activeRecord.info.prefit->manualUpperVoltage);
				}
			}
		}

		ImGui::SeparatorText("Range");
		bool changed = false;
		bool held = false;
		ImGui::SetNextItemWidth(220.0f);
		changed = ImGui::SliderInt("Lower index", &m_LowerIndex, 0, maximumIndex) || changed;
		held = ImGui::IsItemActive() || held;
		ImGui::SameLine();
		ImGui::Text("%.6g V", voltage[std::clamp(m_LowerIndex, 0, maximumIndex)]);
		ImGui::SetNextItemWidth(220.0f);
		changed = ImGui::SliderInt("Upper index", &m_UpperIndex, 0, maximumIndex) || changed;
		held = ImGui::IsItemActive() || held;
		ImGui::SameLine();
		ImGui::Text("%.6g V", voltage[std::clamp(m_UpperIndex, 0, maximumIndex)]);

		m_LowerIndex = std::clamp(m_LowerIndex, 0, maximumIndex);
		m_UpperIndex = std::clamp(m_UpperIndex, 0, maximumIndex);
		if (m_LowerIndex > m_UpperIndex)
			std::swap(m_LowerIndex, m_UpperIndex);

		if (changed && m_LowerIndex != m_UpperIndex)
		{
			if (!m_RangeUndoGroupOpen)
			{
				if (auto undoStack = m_UndoStack.lock())
					m_RangeUndoGroupOpen = undoStack->BeginGroup("Change fit range");
			}
			CommandContext context;
			context.Set("project", project);
			context.Set("characteristic_ids", ids);
			context.Set("lower_voltage", voltage[m_LowerIndex]);
			context.Set("upper_voltage", voltage[m_UpperIndex]);
			Execute(JFMSetFitRangeCommandId, std::move(context));
		}

		if (m_RangeUndoGroupOpen && !held && (m_RangeHeldLastFrame || changed))
			FinishRangeUndoGroup();
		m_RangeHeldLastFrame = held;

		if (ImGui::Button("Automatic range"))
		{
			FinishRangeUndoGroup();
			CommandContext context;
			context.Set("project", project);
			context.Set("characteristic_ids", ids);
			Execute(JFMResetFitRangeCommandId, std::move(context));
		}
	}

	void ControlPanel::RenderParameterTable(const JFMCharacteristicInfo &activeInfo) const
	{
		ImGui::SeparatorText("Parameters");
		if (ImGui::BeginTable("##jfm_control_parameters", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Stage");
			ImGui::TableSetupColumn("Parameter");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Std. error");
			ImGui::TableSetupColumn("Source");
			ImGui::TableHeadersRow();

			if (activeInfo.prefit)
			{
				std::vector<std::string> names;
				names.reserve(activeInfo.prefit->parameters.size());
				for (const auto &[name, estimate] : activeInfo.prefit->parameters)
				{
					(void)estimate;
					names.push_back(name);
				}
				std::sort(names.begin(), names.end());
				for (const std::string &name : names)
				{
					const JFMParameterEstimate &estimate = activeInfo.prefit->parameters.at(name);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("prefit");
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%.8g", estimate.value);
					ImGui::TableSetColumnIndex(3);
					if (estimate.standardError)
						ImGui::Text("%.8g", *estimate.standardError);
					else
						ImGui::TextUnformatted("-");
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(estimate.source.c_str());
				}
			}

			if (activeInfo.fit)
			{
				std::vector<std::string> names;
				names.reserve(activeInfo.fit->parameters.size());
				for (const auto &[name, estimate] : activeInfo.fit->parameters)
				{
					(void)estimate;
					names.push_back(name);
				}
				std::sort(names.begin(), names.end());
				for (const std::string &name : names)
				{
					const JFMParameterEstimate &estimate = activeInfo.fit->parameters.at(name);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("fit");
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%.8g", estimate.value);
					ImGui::TableSetColumnIndex(3);
					if (estimate.standardError)
						ImGui::Text("%.8g", *estimate.standardError);
					else
						ImGui::TextUnformatted("-");
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(estimate.source.c_str());
				}
			}

			for (const char *stage : {"tuned"})
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextDisabled("%s", stage);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("-");
			}
			ImGui::EndTable();
		}
	}
} // namespace SST::Prototypes::JFM
