#include "Core/dspch.hpp"

#include "Presentation/Panels/ControlPanel.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"
#include "Analysis/JFMFitEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Widgets/ParameterBoundsRow.hpp>

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

		std::optional<double> metadataDouble(const JFMCharacteristicInfo &info, const std::string &key)
		{
			const auto found = info.metadata.find(key);
			if (found == info.metadata.end())
				return std::nullopt;
			char *end = nullptr;
			const double value = std::strtod(found->second.c_str(), &end);
			return end != found->second.c_str() && std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
		}

		std::string formatDouble(double value)
		{
			char buffer[96]{};
			std::snprintf(buffer, sizeof(buffer), "%.17g", value);
			return buffer;
		}

		double parameterValue(const JFMCharacteristicInfo &info, std::string_view name)
		{
			const std::string key = "tuned." + std::string(name);
			if (const std::optional<double> tuned = metadataDouble(info, key))
				return *tuned;
			if (info.fit)
			{
				if (const auto found = info.fit->parameters.find(std::string(name)); found != info.fit->parameters.end())
					return found->second.value;
			}
			if (info.prefit)
			{
				if (const auto found = info.prefit->parameters.find(std::string(name)); found != info.prefit->parameters.end())
					return found->second.value;
			}
			return 0.0;
		}

		void decomposeScientific(double value, float &mantissa, int &exponent)
		{
			if (value == 0.0 || !std::isfinite(value))
			{
				mantissa = 0.0f;
				exponent = 0;
				return;
			}
			exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
			mantissa = static_cast<float>(value / std::pow(10.0, exponent));
		}

		float tunedMantissa(const JFMCharacteristicInfo &info, std::string_view name)
		{
			float mantissa = 0.0f;
			int exponent = 0;
			decomposeScientific(parameterValue(info, name), mantissa, exponent);
			return std::clamp(mantissa, 0.0f, 9.999999f);
		}

		int tunedExponent(const JFMCharacteristicInfo &info, std::string_view name)
		{
			float mantissa = 0.0f;
			int exponent = 0;
			decomposeScientific(parameterValue(info, name), mantissa, exponent);
			return std::clamp(exponent, -18, 18);
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

		bool curveSettingsRow(
			const char *label,
			bool &show,
			bool &rangeOnly,
			std::array<float, 4> &color,
			float &lineWeight)
		{
			ImGui::PushID(label);
			bool changed = false;
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			changed |= ImGui::Checkbox("##show", &show);
			ImGui::TableSetColumnIndex(2);
			changed |= ImGui::Checkbox("##range", &rangeOnly);
			ImGui::TableSetColumnIndex(3);
			changed |= ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
			ImGui::TableSetColumnIndex(4);
			ImGui::SetNextItemWidth(-1.0f);
			changed |= ImGui::SliderFloat("##thickness", &lineWeight, 0.5f, 8.0f, "%.1f");
			ImGui::PopID();
			return changed;
		}

		std::optional<double> monteCarloStdError(const JFMMonteCarloResult &result, const std::string &name)
		{
			const auto parameter = std::find(result.parameterNames.begin(), result.parameterNames.end(), name);
			if (parameter == result.parameterNames.end())
				return std::nullopt;
			const std::size_t index = static_cast<std::size_t>(std::distance(result.parameterNames.begin(), parameter));
			double sum = 0.0;
			int count = 0;
			for (const JFMMonteCarloSample &sample : result.samples)
			{
				if (!sample.converged || index >= sample.parameters.size() || !std::isfinite(sample.parameters[index]))
					continue;
				sum += sample.parameters[index];
				++count;
			}
			if (count < 2)
				return std::nullopt;
			const double mean = sum / static_cast<double>(count);
			double variance = 0.0;
			for (const JFMMonteCarloSample &sample : result.samples)
			{
				if (!sample.converged || index >= sample.parameters.size() || !std::isfinite(sample.parameters[index]))
					continue;
				const double delta = sample.parameters[index] - mean;
				variance += delta * delta;
			}
			return std::sqrt(variance / static_cast<double>(count - 1));
		}
	} // namespace ControlPanelDetail
	using namespace ControlPanelDetail;

	ControlPanel::ControlPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<UndoStack> undoStack)
		: EditorWindow("jfm.control", "Control Panel", true, {.duplicable = true}),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_UndoStack(std::move(undoStack))
	{
	}

	WindowDuplicate ControlPanel::Duplicate() const
	{
		return {CreateRef<ControlPanel>(*this), {}};
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

	void ControlPanel::FinishTunedUndoGroup(bool cancel)
	{
		if (m_TunedUndoGroupOpen)
		{
			if (auto undoStack = m_UndoStack.lock())
			{
				if (cancel)
					(void)undoStack->CancelGroup();
				else
					(void)undoStack->EndGroup();
			}
		}
		if (!m_TunedAutoFixedParameter.empty() && m_Settings != nullptr && m_TunedAutoFixedWasFree)
		{
			JFMUserSettings settings = m_Settings->GetSnapshot();
			settings.fitMonteCarlo.parameters[m_TunedAutoFixedParameter].monteCarloFree = true;
			m_Settings->SetFitMonteCarloSettings(settings.fitMonteCarlo);
		}
		m_TunedUndoGroupOpen = false;
		m_TunedHeldLastFrame = false;
		m_TunedAutoFixedParameter.clear();
		m_TunedAutoFixedWasFree = false;
	}

	void ControlPanel::ResetTuning(ProjectHandle project, const std::vector<CharacteristicId> &ids)
	{
		if (m_Store == nullptr)
			return;
		bool groupOpen = false;
		if (auto undoStack = m_UndoStack.lock())
			groupOpen = undoStack->BeginGroup("Reset tuning");
		for (CharacteristicId id : ids)
		{
			const auto record = m_Store->FindCharacteristicRecord({project, id});
			if (!record || !record->info.fit)
				continue;
			for (const auto &[name, estimate] : record->info.fit->parameters)
			{
				float mantissa = 0.0f;
				int exponent = 0;
				decomposeScientific(estimate.value, mantissa, exponent);
				for (const auto &[key, value] : std::array{
						 std::pair{"tuned." + name, formatDouble(estimate.value)},
						 std::pair{"tuned_mantissa." + name, formatDouble(mantissa)},
						 std::pair{"tuned_exponent." + name, std::to_string(exponent)}})
				{
					CommandContext context;
					context.Set("project", project);
					context.Set("characteristic_ids", std::vector<CharacteristicId>{id});
					context.Set("metadata_key", key);
					context.Set("metadata_value", value);
					Execute(JFMSetCharacteristicMetadataCommandId, std::move(context));
				}
			}
		}
		if (groupOpen)
		{
			const auto undoStack = m_UndoStack.lock();
			if (undoStack == nullptr)
				return;
			(void)undoStack->EndGroup();
		}
	}

	void ControlPanel::RenderContent()
	{

		const ImGuiIO &io = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
		{
			const bool redo = (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false);
			const bool undo = !redo && ImGui::IsKeyPressed(ImGuiKey_Z, false);
			if (undo || redo)
			{
				FinishRangeUndoGroup();
				FinishTunedUndoGroup();
				if (auto registry = m_CommandRegistry.lock())
					(void)registry->Execute(CommandID{redo ? "edit.redo" : "edit.undo"});
			}
		}

		if (m_Store == nullptr)
		{
			FinishTunedUndoGroup();
			ImGui::TextDisabled("Workspace unavailable");

			return;
		}

		const ProjectHandle project = m_Store->GetActiveProjectHandle();
		const std::vector<CharacteristicId> ids = m_Store->GetSelectedCharacteristicIds();
		const auto activeRecord = m_Store->GetSelectedCharacteristicRecord();
		if (ids.empty() || !activeRecord)
		{
			FinishRangeUndoGroup();
			FinishTunedUndoGroup();
			ImGui::TextDisabled("No characteristic selected");

			return;
		}

		ImGui::Text("Selected: %d", static_cast<int>(ids.size()));
		RenderModelControls(project, ids);
        RenderSolverControls(project, ids, activeRecord->info);
		RenderPrefitDisplayControls();
		RenderColorControls(project, ids, activeRecord->info);
		RenderRangeControls(project, ids, *activeRecord);
		RenderParameterTable(project, activeRecord->info);
		RenderTunedControls(project, ids, activeRecord->info);
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

		if (sst::ui::widgets::BeginCombo("Model", mixed ? "Mixed" : model.c_str()))
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
			sst::ui::widgets::EndCombo();
		}

		const ImVec2 actionButtonSize{150.0f, 0.0f};
		if (ImGui::Button("Prefit", actionButtonSize) && m_EventBus != nullptr)
			m_EventBus->Queue(PrefitRequested{project, ids});
		ImGui::SameLine();
		if (ImGui::Button("Fit", actionButtonSize) && m_EventBus != nullptr)
			m_EventBus->Queue(FitRequested{project, ids});
		ImGui::SameLine();
		if (ImGui::Button("Reset tuning", actionButtonSize))
			ResetTuning(project, ids);
	}

	void ControlPanel::RenderSolverControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo)
	{
		ImGui::SeparatorText("Fit");
		if (m_Settings != nullptr)
		{
			JFMUserSettings settings = m_Settings->GetSnapshot();
			const bool methodChanged = RenderJFMFitMethod(settings.fitMonteCarlo);
			if ((sst::ui::widgets::Int("Fit iterations", &settings.fitMonteCarlo.fitMaxIterations) || methodChanged)
				&& settings.fitMonteCarlo.fitMaxIterations > 0)
				m_Settings->SetFitMonteCarloSettings(settings.fitMonteCarlo);

			const std::string model = activeInfo.fit ? activeInfo.fit->modelId : activeInfo.prefit ? activeInfo.prefit->modelId : "SD-RsRsh";
			const auto &names = JFMModelParameterNames(model);
			if (!names.empty() && ImGui::BeginTable("##jfm_control_solver_params", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Parameter");
				ImGui::TableSetupColumn("Optimize", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Minimum");
				ImGui::TableSetupColumn("Maximum");
				ImGui::TableHeadersRow();
				bool changed = false;
				for (std::string_view nameView : names)
				{
					const std::string name(nameView);
					JFMParameterSolverSetting &parameter = settings.fitMonteCarlo.parameters[name];
					changed |= sst::ui::widgets::ParameterBoundsRow(name.c_str(),
						JFMParameterLabel(name, &activeInfo).c_str(),
						parameter.monteCarloFree, parameter.minimum, parameter.maximum, name != "A" && name != "alpha");
				}
				ImGui::EndTable();
				if (changed)
					m_Settings->SetFitMonteCarloSettings(settings.fitMonteCarlo);
			}
		}

		if (activeInfo.fit)
		{
			ImGui::Text("Fit: %s, reduced chi2 %.6g", activeInfo.fit->status.c_str(), activeInfo.fit->reducedChiSquared);
            ImGui::Text("Method: %s | Stop: %s | Accepted: %s", activeInfo.fit->fitMethod.c_str(), activeInfo.fit->stopReason.c_str(), activeInfo.fit->accepted ? "yes" : "no");
            ImGui::Text("Converged: %s | Flat directions: %s", activeInfo.fit->converged ? "yes" : "no", activeInfo.fit->flatDirections ? "yes" : "no");
			for (const std::string &warning : activeInfo.fit->warnings)
				ImGui::TextWrapped("Fit warning: %s", warning.c_str());
		}
	}

	void ControlPanel::RenderPrefitDisplayControls()
	{
		if (m_Settings == nullptr)
			return;
		JFMUserSettings settings = m_Settings->GetSnapshot();
		ImGui::SeparatorText("Curve overlays");
		if (ImGui::BeginTable("##jfm_overlay_settings", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Curve");
			ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 52.0f);
			ImGui::TableSetupColumn("Range only", ImGuiTableColumnFlags_WidthFixed, 86.0f);
			ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 58.0f);
			ImGui::TableSetupColumn("Thickness");
			ImGui::TableHeadersRow();
			const bool prefitChanged = curveSettingsRow("Prefit", settings.showPrefitCurves, settings.prefitCurvesFitRangeOnly, settings.prefitCurveColor, settings.prefitCurveLineWeight);
			const bool fitChanged = curveSettingsRow("Fit", settings.showFitCurves, settings.fitCurvesFitRangeOnly, settings.fitCurveColor, settings.fitCurveLineWeight);
			const bool tunedChanged = curveSettingsRow("Tuned", settings.showTunedCurves, settings.tunedCurvesFitRangeOnly, settings.tunedCurveColor, settings.tunedCurveLineWeight);
			ImGui::EndTable();
			if (prefitChanged)
				m_Settings->SetPrefitCurveDefaults(
					settings.showPrefitCurves,
					settings.prefitCurveColor,
					settings.prefitCurveLineWeight,
					settings.prefitCurvesFitRangeOnly,
					settings.hoveredCurveLineWeight);
			if (fitChanged)
				m_Settings->SetFitCurveDefaults(settings.showFitCurves, settings.fitCurveColor, settings.fitCurveLineWeight, settings.fitCurvesFitRangeOnly);
			if (tunedChanged)
				m_Settings->SetTunedCurveDefaults(settings.showTunedCurves, settings.tunedCurveColor, settings.tunedCurveLineWeight, settings.tunedCurvesFitRangeOnly);
		}

		bool autoPrefit = settings.autoPrefitOnRangeChange;
		bool autoFit = settings.autoFitOnRangeChange;
		const bool autoPrefitChanged = sst::ui::widgets::Checkbox("Auto prefit after range change", &autoPrefit);
		const bool autoFitChanged = sst::ui::widgets::Checkbox("Auto fit after range change", &autoFit);
		if (autoPrefitChanged || autoFitChanged)
			m_Settings->SetAutoRangeAnalysis(autoPrefit, autoFit);

		bool changed = false;
		changed |= sst::ui::widgets::SliderFloat("Hovered curve thickness", &settings.hoveredCurveLineWeight, 0.5f, 10.0f, "%.1f");
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
		changed = sst::ui::widgets::SliderInt("Lower index", &m_LowerIndex, 0, maximumIndex) || changed;
		held = ImGui::IsItemActive() || held;
		ImGui::Text("%.6g V", voltage[std::clamp(m_LowerIndex, 0, maximumIndex)]);
		changed = sst::ui::widgets::SliderInt("Upper index", &m_UpperIndex, 0, maximumIndex) || changed;
		held = ImGui::IsItemActive() || held;
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
			if (m_EventBus != nullptr && m_Settings != nullptr)
			{
				const JFMUserSettings settings = m_Settings->GetSnapshot();
				if (settings.autoPrefitOnRangeChange)
					m_EventBus->Queue(PrefitRequested{project, ids});
				if (settings.autoFitOnRangeChange)
					m_EventBus->Queue(FitRequested{project, ids});
			}
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
			if (m_EventBus != nullptr && m_Settings != nullptr)
			{
				const JFMUserSettings settings = m_Settings->GetSnapshot();
				if (settings.autoPrefitOnRangeChange)
					m_EventBus->Queue(PrefitRequested{project, ids});
				if (settings.autoFitOnRangeChange)
					m_EventBus->Queue(FitRequested{project, ids});
			}
		}
	}

	void ControlPanel::RenderTunedControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo)
	{
		const std::string model = activeInfo.fit ? activeInfo.fit->modelId : activeInfo.prefit ? activeInfo.prefit->modelId : std::string{};
		if (model.empty())
		{
			FinishTunedUndoGroup();
			return;
		}
		const auto &names = JFMModelParameterNames(model);
		if (names.empty())
		{
			FinishTunedUndoGroup();
			return;
		}

		ImGui::SeparatorText("Tuned");
		sst::ui::widgets::Checkbox("Fix edited parameter while tuning", &m_AutoFixDuringTuning);
		bool anyHeld = false;
		bool settingsChanged = false;
		JFMUserSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};

		if (ImGui::BeginTable("##jfm_control_tuned_params", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Parameter");
			ImGui::TableSetupColumn("Fix", ImGuiTableColumnFlags_WidthFixed, 44.0f);
			ImGui::TableSetupColumn("Mantissa");
			ImGui::TableSetupColumn("Exponent", ImGuiTableColumnFlags_WidthFixed, 104.0f);
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();
			for (std::string_view nameView : names)
			{
				const std::string name(nameView);
				JFMParameterSolverSetting &parameter = settings.fitMonteCarlo.parameters[name];
				bool fixed = !parameter.monteCarloFree;
				float mantissa = tunedMantissa(activeInfo, nameView);
				int exponent = tunedExponent(activeInfo, nameView);

				ImGui::PushID(name.c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextWrapped("%s", JFMParameterLabel(name, &activeInfo).c_str());
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Checkbox("##fix", &fixed))
				{
					parameter.monteCarloFree = !fixed;
					settingsChanged = true;
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				const bool mantissaChanged = ImGui::DragFloat("##mantissa", &mantissa, 0.0005f, 0.0f, 9.999999f, "%.6g", ImGuiSliderFlags_AlwaysClamp);
				const bool mantissaActive = ImGui::IsItemActive();
				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				const bool exponentChanged = ImGui::SliderInt("##exponent", &exponent, -18, 18);
				const bool exponentActive = ImGui::IsItemActive();
				ImGui::TableSetColumnIndex(4);
				const double tunedValue = static_cast<double>(mantissa) * std::pow(10.0, exponent);
				ImGui::Text("%.8g", tunedValue);
				const bool active = mantissaActive || exponentActive;
				anyHeld = anyHeld || active;
				if ((mantissaChanged || exponentChanged) && std::isfinite(tunedValue))
				{
					if (!m_TunedUndoGroupOpen)
					{
						if (auto undoStack = m_UndoStack.lock())
							m_TunedUndoGroupOpen = undoStack->BeginGroup("Tune parameter");
					}
					if (m_AutoFixDuringTuning && active && m_Settings != nullptr)
					{
						if (m_TunedAutoFixedParameter.empty())
						{
							m_TunedAutoFixedParameter = name;
							m_TunedAutoFixedWasFree = parameter.monteCarloFree;
						}
						if (m_TunedAutoFixedParameter == name && parameter.monteCarloFree)
						{
							parameter.monteCarloFree = false;
							settingsChanged = true;
						}
					}
					CommandContext context;
					context.Set("project", project);
					context.Set("characteristic_ids", ids);
					context.Set("metadata_key", "tuned." + name);
					context.Set("metadata_value", formatDouble(tunedValue));
					Execute(JFMSetCharacteristicMetadataCommandId, std::move(context));
					CommandContext mantissaContext;
					mantissaContext.Set("project", project);
					mantissaContext.Set("characteristic_ids", ids);
					mantissaContext.Set("metadata_key", "tuned_mantissa." + name);
					mantissaContext.Set("metadata_value", formatDouble(mantissa));
					Execute(JFMSetCharacteristicMetadataCommandId, std::move(mantissaContext));
					CommandContext exponentContext;
					exponentContext.Set("project", project);
					exponentContext.Set("characteristic_ids", ids);
					exponentContext.Set("metadata_key", "tuned_exponent." + name);
					exponentContext.Set("metadata_value", std::to_string(exponent));
					Execute(JFMSetCharacteristicMetadataCommandId, std::move(exponentContext));
					if (m_EventBus != nullptr)
						m_EventBus->Queue(FitRequested{project, ids, true});
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (settingsChanged && m_Settings != nullptr)
			m_Settings->SetFitMonteCarloSettings(settings.fitMonteCarlo);
		if (m_TunedUndoGroupOpen && !anyHeld && m_TunedHeldLastFrame)
		{
			const bool restoreUnconstrainedFit = m_TunedAutoFixedWasFree;
			FinishTunedUndoGroup();
			if (restoreUnconstrainedFit && m_EventBus != nullptr)
				m_EventBus->Queue(FitRequested{project, ids});
		}
		m_TunedHeldLastFrame = anyHeld;
	}

	void ControlPanel::RenderParameterTable(ProjectHandle project, const JFMCharacteristicInfo &activeInfo)
	{
		ImGui::SeparatorText("Parameters");
		sst::ui::widgets::Checkbox("Hide prefit parameters", &m_HidePrefitParameters);
		const Ref<const JFMMonteCarloResult> monteCarlo = m_Store != nullptr ? m_Store->GetMonteCarloResult({project, activeInfo.id}) : Ref<const JFMMonteCarloResult>{};
		if (ImGui::BeginTable("##jfm_control_parameters", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Stage");
			ImGui::TableSetupColumn("Parameter");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Std. error");
			ImGui::TableSetupColumn("Source");
			ImGui::TableHeadersRow();

			if (activeInfo.prefit && !m_HidePrefitParameters)
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
					const std::optional<double> mcError = monteCarlo ? monteCarloStdError(*monteCarlo, name) : std::nullopt;
					if (estimate.standardError)
						ImGui::Text("%.8g", *estimate.standardError);
					else if (mcError)
						ImGui::Text("%.8g", *mcError);
					else
						ImGui::TextUnformatted("-");
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(estimate.source.c_str());
				}
			}

			const std::string model = activeInfo.fit ? activeInfo.fit->modelId : activeInfo.prefit ? activeInfo.prefit->modelId : std::string{};
			for (std::string_view nameView : JFMModelParameterNames(model))
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted("tuned");
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(std::string(nameView).c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.8g", parameterValue(activeInfo, nameView));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted("manual");
			}
			ImGui::EndTable();
		}
	}
    void ControlPanel::OnContentSkipped()
    {
        FinishRangeUndoGroup();
        FinishTunedUndoGroup();
    }
} // namespace SST::Prototypes::JFM
