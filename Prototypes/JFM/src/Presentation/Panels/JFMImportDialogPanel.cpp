#include "Core/dspch.hpp"
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>

#include "Presentation/Panels/JFMImportDialogPanel.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "App/JFMCommands.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <implot.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <limits>
#include <map>
#include <sstream>

namespace SST::Prototypes::JFM
{
	namespace ImportDialogDetail
	{
		constexpr NativeFileDialogFilter PythonScriptFilter{"Python", "py"};

		const char *ParserOptions[] = {"auto", "dat", "scaps_iv", "custom"};
		const char *DecimalOptions[] = {"auto", ".", ","};
		const char *TemperatureOptions[] = {"auto", "filename", "header", "off"};
		const char *VariableSources[] = {
			"stem",
			"file_name",
			"file_stem",
			"folder",
			"parent_folder",
			"parent_folder_1",
			"parent_folder_2",
			"parent_folder_3",
			"parent_folder_4",
			"parent_folder_5",
			"source_path",
			"folder_path",
			"parent_path",
			"parent_path_1",
			"parent_path_2",
			"parent_path_3",
			"parent_path_4",
			"parent_path_5"};
		bool ContainsIndex(const std::vector<int> &indices, int index)
		{
			return std::find(indices.begin(), indices.end(), index) != indices.end();
		}

		bool ContainsId(const std::vector<CharacteristicId> &ids, CharacteristicId id)
		{
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}

		ImVec4 PreviewColor(std::size_t index)
		{
			return ImColor::HSV(static_cast<float>(std::fmod(index * 0.173, 1.0)), 0.74f, 0.95f).Value;
		}

		void ApplyPreviewScale(int scale, std::vector<double> &x, std::vector<double> &y)
		{
			if (scale == 0)
				return;

			std::vector<double> nextX;
			std::vector<double> nextY;
			nextX.reserve(std::min(x.size(), y.size()));
			nextY.reserve(std::min(x.size(), y.size()));
			for (std::size_t index = 0; index < x.size() && index < y.size(); ++index)
			{
				const double value = std::abs(y[index]);
				if (value <= 0.0)
					continue;
				nextX.push_back(x[index]);
				nextY.push_back(value);
			}
			x = std::move(nextX);
			y = std::move(nextY);
		}

		const std::vector<double> &AnalysisCurrent(const JFMCharacteristicData &data)
		{
			return data.currentDensity.empty() ? data.current : data.currentDensity;
		}

		std::string SeriesIdForCharacteristic(const JFMProject &project, CharacteristicId id)
		{
			for (const JFMSeries &series : project.series)
			{
				if (ContainsId(series.characteristicIds, id))
					return series.id;
			}
			return {};
		}

		template <typename Curve>
		void CopyFitRange(const Curve &curve, const JFMFitRange &range, std::vector<double> &x, std::vector<double> &y)
		{
			const std::size_t count = std::min(curve.voltage.size(), curve.current.size());
			const double lower = std::min(range.lowerVoltage, range.upperVoltage);
			const double upper = std::max(range.lowerVoltage, range.upperVoltage);
			x.reserve(count);
			y.reserve(count);
			for (std::size_t index = 0; index < count; ++index)
			{
				const double voltage = curve.voltage[index];
				if (voltage < lower || voltage > upper)
					continue;
				x.push_back(voltage);
				y.push_back(curve.current[index]);
			}
		}

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

		bool ApplySuggestedBounds(JFMFitMonteCarloSettings &settings, const std::optional<JFMPrefitState> &prefit, const std::optional<JFMFitState> &fit, std::string_view model, bool onlyDefaultish)
		{
			bool changed = false;
			for (std::string_view nameView : JFMModelParameterNames(model))
			{
				const std::string name{nameView};
				if (const std::optional<double> value = EstimateValue(prefit, fit, nameView))
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
	} // namespace ImportDialogDetail
	using namespace ImportDialogDetail;

	void JFMImportDialogState::Open(JFMImportDialogRequest request)
	{
		std::scoped_lock lock(m_Mutex);
		m_Request = std::move(request);
	}

	std::optional<JFMImportDialogRequest> JFMImportDialogState::ConsumeOpenRequest()
	{
		std::scoped_lock lock(m_Mutex);
		std::optional<JFMImportDialogRequest> request = std::move(m_Request);
		m_Request.reset();
		return request;
	}

	JFMImportDialogPanel::JFMImportDialogPanel(
		Ref<EventBus> eventBus,
		Ref<JFMWorkspaceStore> store,
		Ref<JFMImportDialogState> state,
		Ref<JFMUserSettingsStore> settings,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<UndoStack> undoStack)
		: IPanel("Import Dialog", false),
		  m_EventBus(std::move(eventBus)),
		  m_Store(std::move(store)),
		  m_State(std::move(state)),
		  m_Settings(std::move(settings)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_UndoStack(std::move(undoStack))
	{
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
		std::snprintf(m_VariableDelimiter.data(), m_VariableDelimiter.size(), "%s", "_");
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<ImportCompleted>([this](const ImportCompleted &event) {
				handleImportCompleted(event);
			}));
			AddSubscription(m_EventBus->Subscribe<ImportFailed>([this](const ImportFailed &event) {
				handleImportFailed(event);
			}));
			AddSubscription(m_EventBus->Subscribe<ImportPreviewCompleted>([this](const ImportPreviewCompleted &event) {
				handlePreviewCompleted(event);
			}));
			AddSubscription(m_EventBus->Subscribe<ImportPreviewFailed>([this](const ImportPreviewFailed &event) {
				handlePreviewFailed(event);
			}));
		}
	}

	Ref<IPanel> JFMImportDialogPanel::Clone() const
	{
		auto copy = CreateRef<JFMImportDialogPanel>(m_EventBus, m_Store, m_State, m_Settings, m_CommandRegistry, m_UndoStack);
        copy->m_Windows.UseInstanceIdentity();
        return copy;
	}

	void JFMImportDialogPanel::Render()
	{
        m_Windows.BeginFrame();
		if (m_State != nullptr)
		{
			if (auto request = m_State->ConsumeOpenRequest())
			{
				loadRequest(std::move(*request));
				m_Open = true;
				m_PreviewOpen = true;
                m_Windows.Activate("Import Data");
                m_Windows.Activate("Import Preview");
			}
		}

		if (!m_Open) { m_Windows.EndFrame(); return; }

		ImGui::SetNextWindowSize(ImVec2(1180.0f, 760.0f), ImGuiCond_FirstUseEver);
        const bool rendered = m_Windows.Render("Import Data", m_Open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings, [&] {

		const ImGuiIO &io = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive())
		{
			const bool redo = (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false);
			const bool undo = !redo && ImGui::IsKeyPressed(ImGuiKey_Z, false);
			if (undo || redo)
			{
				finishRangeUndoGroup();
				if (auto registry = m_CommandRegistry.lock())
					(void)registry->Execute(CommandID{redo ? "edit.redo" : "edit.undo"});
			}
		}

		if (ImGui::BeginTabBar("##jfm_import_tabs"))
		{
			const ImGuiTabItemFlags importFlags = m_SelectImportTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem("Import", nullptr, importFlags))
			{
				m_SelectImportTab = false;
		ImGui::Text("Files: %d", static_cast<int>(m_Request.paths.size()));
		if (!m_Request.paths.empty())
			ImGui::TextWrapped("%s", m_Request.paths.front().Utf8().c_str());
		ImGui::Text("Previewable files: %d", static_cast<int>(m_PreviewFiles.size()));

		const JFMProject targetProject = m_Store != nullptr ? m_Store->GetProjectSnapshot(m_Request.project) : JFMProject{};
		ImGui::SeparatorText("Destination");
		ImGui::Text("Project: %s", targetProject.name.c_str());
		int placement = static_cast<int>(m_Request.placement);
		const char *placementLabels[] = {"New series", "Existing series", "Preserve folder tree"};
		if (ImGui::Combo("Placement", &placement, placementLabels, IM_ARRAYSIZE(placementLabels)))
		{
			m_Request.placement = static_cast<JFMImportPlacement>(placement);
			if (m_Request.placement != JFMImportPlacement::ExistingSeries)
				m_Request.targetSeriesId.clear();
		}
		if (m_Request.placement == JFMImportPlacement::ExistingSeries)
		{
			std::string seriesLabel = "Select series";
			for (const JFMSeries &series : targetProject.series)
			{
				if (series.id == m_Request.targetSeriesId)
					seriesLabel = series.name;
			}
			if (ImGui::BeginCombo("Series", seriesLabel.c_str()))
			{
				for (const JFMSeries &series : targetProject.series)
				{
					if (ImGui::Selectable(series.name.c_str(), series.id == m_Request.targetSeriesId))
					{
						m_Request.targetSeriesId = series.id;
						m_Request.targetGroupId = series.groupId;
					}
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			std::string groupLabel = "Project root";
			for (const JFMGroup &group : targetProject.groups)
			{
				if (group.id == m_Request.targetGroupId)
					groupLabel = group.name;
			}
			if (ImGui::BeginCombo("Parent group", groupLabel.c_str()))
			{
				if (ImGui::Selectable("Project root", m_Request.targetGroupId.empty()))
					m_Request.targetGroupId.clear();
				for (const JFMGroup &group : targetProject.groups)
				{
					if (ImGui::Selectable(group.name.c_str(), group.id == m_Request.targetGroupId))
						m_Request.targetGroupId = group.id;
				}
				ImGui::EndCombo();
			}
		}

		if (ImGui::BeginTable("##jfm_import_options_layout", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, 0.46f);
			ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthStretch, 0.54f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Combo("Parser", &m_Parser, ParserOptions, IM_ARRAYSIZE(ParserOptions));
			ImGui::InputText("Name pattern", m_NamePattern.data(), m_NamePattern.size());
			ImGui::InputText("Short name", m_ShortNamePattern.data(), m_ShortNamePattern.size());
			ImGui::InputText("Variable delimiter", m_VariableDelimiter.data(), m_VariableDelimiter.size());
			ImGui::TextDisabled("Variables: {stem}, {file_name}, {folder}, {parent_folder}, {parent_folder_2}, {source_path}, {T}, {stem_0}, {folder_0}");
			renderCustomVariables();
			ImGui::InputText("Delimiter", m_Delimiter.data(), m_Delimiter.size());
			ImGui::Combo("Decimal", &m_DecimalSeparator, DecimalOptions, IM_ARRAYSIZE(DecimalOptions));
			ImGui::Combo("Temperature", &m_TemperatureExtraction, TemperatureOptions, IM_ARRAYSIZE(TemperatureOptions));

			if (m_Parser == 3)
			{
				ImGui::InputText("Script", m_CustomScriptPath.data(), m_CustomScriptPath.size());
				ImGui::SameLine();
				if (ImGui::Button("..."))
				{
					if (auto script = OpenFileDialog(PythonScriptFilter))
						std::snprintf(m_CustomScriptPath.data(), m_CustomScriptPath.size(), "%s", script->Utf8().c_str());
				}
				if (m_Store != nullptr)
				{
					const JFMProject project = m_Store->GetProjectSnapshot(m_Request.project);
					for (const JFMScriptInfo &script : project.scripts)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton(script.name.c_str()))
							std::snprintf(m_CustomScriptPath.data(), m_CustomScriptPath.size(), "%s", script.path.Utf8().c_str());
					}
				}
			}

			ImGui::TableSetColumnIndex(1);
			renderVariablePreviewTable();
			ImGui::EndTable();
		}
		const std::string currentPreviewOptionsKey = previewOptionsKey();
		if (currentPreviewOptionsKey != m_LastPreviewOptionsKey)
		{
			m_LastPreviewOptionsKey = currentPreviewOptionsKey;
			requestPreview();
		}

		const std::string name = previewName();
		ImGui::Text("Preview name: %s", name.c_str());
		const std::string shortName = previewShortName();
		if (!shortName.empty())
			ImGui::Text("Preview short: %s", shortName.c_str());
		if (m_PreviewLoading)
			ImGui::TextDisabled("Refreshing preview...");
		else if (!m_PreviewError.empty())
			ImGui::TextWrapped("Preview failed: %s", m_PreviewError.c_str());
		ImGui::Checkbox("Preview window", &m_PreviewOpen);
		ImGui::Checkbox("Plot after add", &m_PlotAfterImport);

		const bool validDestination = m_Request.placement != JFMImportPlacement::ExistingSeries || !m_Request.targetSeriesId.empty();
		if (!validDestination)
			ImGui::TextDisabled("Select target series before staging.");
		else if (m_ImportSubmitted)
			ImGui::TextDisabled("Preparing data...");
		if (ImGui::Button("Cancel"))
		{
			m_Open = false;
			m_PreviewOpen = false;
		}
				ImGui::EndTabItem();
			}

			const ImGuiTabItemFlags analysisFlags = m_SelectAnalysisTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem("Data range & prefit", nullptr, analysisFlags))
			{
				m_SelectAnalysisTab = false;
				renderAnalysisTab(false);
				ImGui::EndTabItem();
			}
			else
			{
				finishRangeUndoGroup();
			}
			const ImGuiTabItemFlags fitFlags = m_SelectFitTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem("Fit & Monte Carlo", nullptr, fitFlags))
			{
				m_SelectFitTab = false;
				renderAnalysisTab(true);
				ImGui::EndTabItem();
			}
			else
			{
				finishRangeUndoGroup();
			}
			ImGui::EndTabBar();
		}

        });
        if (!rendered || !m_Open) finishRangeUndoGroup();
        if (!m_Open) m_PreviewOpen = false;
        renderPreviewWindow();
        m_Windows.EndFrame();
	}

	void JFMImportDialogPanel::renderAnalysisTab(bool fitMode)
	{
		if (m_Store == nullptr || m_Request.project == InvalidProjectHandle)
		{
			ImGui::TextDisabled("No project selected.");
			return;
		}
		if (m_StagingProject == InvalidProjectHandle)
		{
			submitStagingImport(fitMode);
			ImGui::TextDisabled(m_ImportSubmitted ? "Preparing data..." : "Cannot prepare data.");
			return;
		}

		const ProjectHandle analysisProject = m_StagingProject;
		const JFMProject project = m_Store->GetProjectSnapshot(analysisProject);
		std::vector<CharacteristicId> allIds;
		std::vector<CharacteristicId> visibleIds;
		for (const JFMSeries &series : project.series)
		{
			allIds.insert(allIds.end(), series.characteristicIds.begin(), series.characteristicIds.end());
			if (m_AnalysisOnlySeriesId.empty() || series.id == m_AnalysisOnlySeriesId)
				visibleIds.insert(visibleIds.end(), series.characteristicIds.begin(), series.characteristicIds.end());
		}
		if (!fitMode && m_AutoDetectAppliedFor != analysisProject)
		{
			// First look at "Data range & prefit" for this staged batch --
			// auto-detect any characteristic nobody has touched yet instead of
			// leaving it on the hardcoded default model until someone notices
			// and clicks "Auto-detect model" by hand.
			m_AutoDetectAppliedFor = analysisProject;
			std::vector<CharacteristicId> freshIds;
			for (CharacteristicId id : allIds)
			{
				const auto state = m_Store->GetPrefitState({analysisProject, id});
				if (!state || state->status == "not_started")
					freshIds.push_back(id);
			}
			if (!freshIds.empty())
			{
				if (auto registry = m_CommandRegistry.lock())
				{
					CommandContext context;
					context.Set("project", analysisProject);
					context.Set("characteristic_ids", freshIds);
					(void)registry->Execute(CommandID{JFMAutoDetectPrefitCommandId}, std::move(context));
				}
			}
		}
		if (!m_AnalysisOnlySeriesId.empty() && visibleIds.empty())
		{
			m_AnalysisOnlySeriesId.clear();
			visibleIds = allIds;
		}
		if (visibleIds.empty())
		{
			ImGui::TextDisabled("No imported characteristics.");
			return;
		}

		auto projectState = m_Store->CaptureProjectState(analysisProject);
		if (!projectState)
			return;
		std::vector<CharacteristicId> selectedIds;
		for (CharacteristicId id : projectState->selectedCharacteristicIds)
		{
			if (ContainsId(allIds, id))
				selectedIds.push_back(id);
		}
		CharacteristicId active = projectState->selectedCharacteristicId;
		if (!ContainsId(selectedIds, active))
			active = selectedIds.empty() ? allIds.front() : selectedIds.front();
		if (selectedIds.empty())
		{
			selectedIds = {active};
			m_Store->SetSelectedCharacteristics(analysisProject, selectedIds, active);
		}
		handleAnalysisKeyboard(project, visibleIds, active);

		std::vector<CharacteristicId> missingPrefits;
		for (CharacteristicId id : allIds)
		{
			const std::optional<JFMPrefitState> state = m_Store->GetPrefitState({analysisProject, id});
			const bool curveMissing = m_Store->GetPrefitCurve({analysisProject, id}) == nullptr;
			if (!state || state->status == "not_started" || (state->status == "completed" && curveMissing))
				missingPrefits.push_back(id);
		}
		requestPrefit(analysisProject, std::move(missingPrefits));
		if (fitMode)
		{
			std::vector<CharacteristicId> missingFits;
			for (CharacteristicId id : allIds)
			{
				const auto prefit = m_Store->GetPrefitState({analysisProject, id});
				if (!prefit || prefit->status != "completed")
					continue;
				const auto state = m_Store->GetFitState({analysisProject, id});
				const bool curveMissing = m_Store->GetFitCurve({analysisProject, id}) == nullptr;
				if (!state || state->status == "not_started" || (state->status == "completed" && curveMissing))
					missingFits.push_back(id);
			}
			requestFit(analysisProject, std::move(missingFits));
		}

		const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.27f, 250.0f, 340.0f);
		if (ImGui::BeginChild("##jfm_analysis_tree", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
			renderAnalysisTree(project);
		ImGui::EndChild();
		if (m_AnalysisTreeOpenRequest != 0)
			m_AnalysisTreeOpenRequest = 0;
		ImGui::SameLine();

		if (ImGui::BeginChild("##jfm_analysis_workspace", ImVec2(0.0f, 0.0f)))
		{
			const bool canAdd = !m_DataAddedToProject && !m_ImportSubmitted && !m_StagedSeries.empty();
			ImGui::BeginDisabled(!canAdd);
			if (ImGui::Button("Add Data to Project"))
			{
				if (auto registry = m_CommandRegistry.lock())
				{
					CommandContext context(ContextID{"jfm.import.stage.apply"});
					context.SetSource("JFM import staging");
					context.Set("project", m_Request.project);
					context.Set("batches", buildApplyBatchesFromStaging());
					context.Set("target_group_id", m_Request.targetGroupId);
					context.Set("plot_after_import", m_PlotAfterImport);
					if (auto result = registry->Execute(CommandID{JFMImportApplyCommandId}, std::move(context)))
					{
						m_DataAddedToProject = true;
						finishRangeUndoGroup();
						m_Open = false;
						m_PreviewOpen = false;
					}
				}
			}
			ImGui::EndDisabled();
			ImGui::SeparatorText("Control Panel");
			int scale = m_AnalysisScale;
			ImGui::RadioButton("Linear", &scale, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Semi-log", &scale, 1);
			m_AnalysisScale = scale;
			ImGui::SameLine();
			ImGui::Checkbox("Show only active", &m_ShowOnlyActive);
			ImGui::SameLine();
			ImGui::TextDisabled("Selected: %d", static_cast<int>(selectedIds.size()));

			std::string selectedModel = "SD-RsRsh";
			bool mixedModels = false;
			for (std::size_t index = 0; index < selectedIds.size(); ++index)
			{
				const auto state = m_Store->GetPrefitState({analysisProject, selectedIds[index]});
				const std::string model = state ? state->modelId : "SD-RsRsh";
				if (index == 0)
					selectedModel = model;
				else if (model != selectedModel)
					mixedModels = true;
			}
			const char *modelPreview = mixedModels ? "Mixed" : selectedModel.c_str();
			ImGui::SetNextItemWidth(250.0f);
			if (ImGui::BeginCombo("Model", modelPreview))
			{
				for (const char *model : PrefitModels)
				{
					if (ImGui::Selectable(model, !mixedModels && selectedModel == model) && (mixedModels || selectedModel != model))
					{
						if (auto registry = m_CommandRegistry.lock())
						{
							CommandContext context;
							context.Set("project", analysisProject);
							context.Set("characteristic_ids", selectedIds);
							context.Set("model_id", std::string(model));
							(void)registry->Execute(CommandID{JFMSetPrefitModelCommandId}, std::move(context));
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			const auto activePrefit = m_Store->GetPrefitState({analysisProject, active});
			const bool hasManualRange = activePrefit && activePrefit->manualLowerVoltage && activePrefit->manualUpperVoltage;
			ImGui::BeginDisabled(!hasManualRange);
			if (ImGui::Button("Automatic range"))
			{
				finishRangeUndoGroup();
				if (auto registry = m_CommandRegistry.lock())
				{
					CommandContext context;
					context.Set("project", analysisProject);
					context.Set("characteristic_id", active);
					(void)registry->Execute(CommandID{JFMResetFitRangeCommandId}, std::move(context));
				}
				m_RangeInitialized = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Auto-detect model"))
			{
				finishRangeUndoGroup();
				if (auto registry = m_CommandRegistry.lock())
				{
					CommandContext context;
					context.Set("project", analysisProject);
					context.Set("characteristic_ids", selectedIds);
					(void)registry->Execute(CommandID{JFMAutoDetectPrefitCommandId}, std::move(context));
				}
				m_RangeInitialized = false;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Auto fit", &m_AnalysisAutoFit);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::SliderFloat("Scroll out", &m_AnalysisFitPadding, 0.0f, 0.5f, "%.2f");
			ImGui::SameLine();
			ImGui::Checkbox("Legend", &m_AnalysisShowLegend);
			ImGui::SameLine();
			ImGui::Checkbox("Prefit curves", &m_AnalysisShowPrefitCurves);
			if (fitMode)
			{
				ImGui::SameLine();
				ImGui::Checkbox("Fit curves", &m_AnalysisShowFitCurves);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Expand all"))
				m_AnalysisTreeOpenRequest = 1;
			ImGui::SameLine();
			if (ImGui::SmallButton("Collapse all"))
				m_AnalysisTreeOpenRequest = -1;
			ImGui::SameLine();
			if (ImGui::SmallButton("Hide all"))
				m_ShowOnlyActive = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("Unhide all"))
				m_ShowOnlyActive = false;
			ImGui::SameLine();
			ImGui::Checkbox("Curves", &m_AnalysisShowCurves);
			if (!m_AnalysisOnlySeriesId.empty())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("All series"))
					m_AnalysisOnlySeriesId.clear();
			}

			const std::vector<CharacteristicId> plotIds = m_ShowOnlyActive
				? std::vector<CharacteristicId>{active}
				: visibleIds;
			renderAnalysisPlot(analysisProject, plotIds, active, fitMode && m_AnalysisShowFitCurves);
			renderParameterTable(analysisProject, active);
			if (fitMode)
				renderFitMonteCarloResults(analysisProject, active, selectedIds);
		}
		ImGui::EndChild();
	}

	void JFMImportDialogPanel::renderAnalysisTree(const JFMProject &project)
	{
		std::vector<CharacteristicId> visibleIds;
		for (const JFMSeries &series : project.series)
			visibleIds.insert(visibleIds.end(), series.characteristicIds.begin(), series.characteristicIds.end());

		const std::string projectLabel = project.name + "##jfm_analysis_project";
		if (m_AnalysisTreeOpenRequest != 0)
			ImGui::SetNextItemOpen(m_AnalysisTreeOpenRequest > 0, ImGuiCond_Always);
		if (!ImGui::TreeNodeEx(projectLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
			return;
		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId.empty())
				renderAnalysisGroup(project, group, visibleIds);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId.empty())
				renderAnalysisSeries(series, visibleIds);
		}
		ImGui::TreePop();
	}

	void JFMImportDialogPanel::renderAnalysisGroup(const JFMProject &project, const JFMGroup &group, const std::vector<CharacteristicId> &visibleIds)
	{
		const std::string label = group.name + "##jfm_analysis_group_" + group.id;
		if (m_AnalysisTreeOpenRequest != 0)
			ImGui::SetNextItemOpen(m_AnalysisTreeOpenRequest > 0, ImGuiCond_Always);
		const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		{
			std::vector<CharacteristicId> ids = m_Store->GetGroupCharacteristicIds(m_StagingProject, group.id);
			ids.erase(std::remove_if(ids.begin(), ids.end(), [&visibleIds](CharacteristicId id) { return !ContainsId(visibleIds, id); }), ids.end());
			if (!ids.empty())
				selectAnalysisIds(std::move(ids), ids.front());
		}
		if (!open)
			return;
		for (const JFMGroup &child : project.groups)
		{
			if (child.parentId == group.id)
				renderAnalysisGroup(project, child, visibleIds);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId == group.id)
				renderAnalysisSeries(series, visibleIds);
		}
		ImGui::TreePop();
	}

	void JFMImportDialogPanel::renderAnalysisSeries(const JFMSeries &series, const std::vector<CharacteristicId> &visibleIds)
	{
		if (series.characteristicIds.empty())
			return;
		const std::string label = series.name + "##jfm_analysis_series_" + series.id;
		if (m_AnalysisTreeOpenRequest != 0)
			ImGui::SetNextItemOpen(m_AnalysisTreeOpenRequest > 0, ImGuiCond_Always);
		const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		{
			std::vector<CharacteristicId> ids = series.characteristicIds;
			ids.erase(std::remove_if(ids.begin(), ids.end(), [&visibleIds](CharacteristicId id) { return !ContainsId(visibleIds, id); }), ids.end());
			if (!ids.empty())
				selectAnalysisIds(std::move(ids), ids.front(), series.id);
		}
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Show only this series"))
			{
				m_AnalysisOnlySeriesId = series.id;
				std::vector<CharacteristicId> ids = series.characteristicIds;
				if (!ids.empty())
					selectAnalysisIds(std::move(ids), ids.front(), series.id);
			}
			if (ImGui::MenuItem("Show all series", nullptr, false, !m_AnalysisOnlySeriesId.empty()))
				m_AnalysisOnlySeriesId.clear();
			ImGui::EndPopup();
		}
		if (!open)
			return;
		const auto snapshot = m_Store->CaptureProjectState(m_StagingProject);
		const std::vector<CharacteristicId> selected = snapshot ? snapshot->selectedCharacteristicIds : std::vector<CharacteristicId>{};
		for (CharacteristicId id : series.characteristicIds)
		{
			const auto record = m_Store->FindCharacteristicRecord({m_StagingProject, id});
			if (!record)
				continue;
			std::string status;
			if (record->info.prefit && (record->info.prefit->status == "running" || record->info.prefit->status == "queued"))
				status = " [running]";
			else if (record->info.prefit && record->info.prefit->status == "failed")
				status = " [failed]";
			const std::string itemLabel = record->info.name + status + "##jfm_analysis_characteristic_" + std::to_string(id);
			const bool itemSelected = ContainsId(selected, id);
			if (itemSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.70f, 0.48f, 0.26f, 0.95f));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.82f, 0.58f, 0.30f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.95f, 0.64f, 0.32f, 1.00f));
			}
			if (ImGui::Selectable(itemLabel.c_str(), itemSelected))
				selectAnalysisCharacteristic(series, id, visibleIds);
			if (itemSelected)
				ImGui::PopStyleColor(3);
		}
		ImGui::TreePop();
	}

	void JFMImportDialogPanel::handleAnalysisKeyboard(const JFMProject &project, const std::vector<CharacteristicId> &visibleIds, CharacteristicId active)
	{
		if (visibleIds.empty() || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())
			return;
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;
		const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
		const bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
		if (!down && !up)
			return;
		const auto current = std::find(visibleIds.begin(), visibleIds.end(), active);
		std::size_t index = current == visibleIds.end() ? 0 : static_cast<std::size_t>(std::distance(visibleIds.begin(), current));
		if (down && index + 1 < visibleIds.size())
			++index;
		else if (up && index > 0)
			--index;
		else
			return;
		const CharacteristicId next = visibleIds[index];
		selectAnalysisIds({next}, next, SeriesIdForCharacteristic(project, next));
	}

	void JFMImportDialogPanel::selectAnalysisIds(std::vector<CharacteristicId> ids, CharacteristicId primary, std::string seriesId)
	{
		if (m_Store == nullptr || ids.empty())
			return;
		finishRangeUndoGroup();
		if (!ContainsId(ids, primary))
			primary = ids.front();
		m_Store->SetSelectedCharacteristics(m_StagingProject, std::move(ids), primary, std::move(seriesId));
		m_RangeCharacteristic = primary;
		m_RangeInitialized = false;
	}

	void JFMImportDialogPanel::selectAnalysisCharacteristic(const JFMSeries &series, CharacteristicId id, const std::vector<CharacteristicId> &visibleIds)
	{
		if (m_Store == nullptr)
			return;
		finishRangeUndoGroup();
		const auto snapshot = m_Store->CaptureProjectState(m_StagingProject);
		std::vector<CharacteristicId> selected = snapshot ? snapshot->selectedCharacteristicIds : std::vector<CharacteristicId>{};
		CharacteristicId primary = id;
		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyShift && snapshot && snapshot->selectedCharacteristicId != InvalidCharacteristicId)
		{
			const auto first = std::find(visibleIds.begin(), visibleIds.end(), snapshot->selectedCharacteristicId);
			const auto last = std::find(visibleIds.begin(), visibleIds.end(), id);
			if (first != visibleIds.end() && last != visibleIds.end())
			{
				const auto begin = std::min(first, last);
				const auto end = std::max(first, last);
				selected.assign(begin, end + 1);
			}
			else
			{
				selected = {id};
			}
		}
		else if (io.KeyCtrl)
		{
			if (ContainsId(selected, id))
				selected.erase(std::remove(selected.begin(), selected.end(), id), selected.end());
			else
				selected.push_back(id);
			if (selected.empty())
				selected.push_back(id);
			primary = ContainsId(selected, id) ? id : selected.back();
		}
		else
		{
			selected = {id};
		}
		selectAnalysisIds(std::move(selected), primary, series.id);
	}

	void JFMImportDialogPanel::renderAnalysisPlot(ProjectHandle project, const std::vector<CharacteristicId> &ids, CharacteristicId active, bool showFit)
	{
		if (m_Store == nullptr)
			return;
		const auto activeRecord = m_Store->FindCharacteristicRecord({project, active});
		if (!activeRecord || activeRecord->data == nullptr || activeRecord->data->voltage.empty())
		{
			ImGui::TextDisabled("Active characteristic has no data.");
			return;
		}

		double minimumVoltage = std::numeric_limits<double>::infinity();
		double maximumVoltage = -std::numeric_limits<double>::infinity();
		for (double voltage : activeRecord->data->voltage)
		{
			if (std::isfinite(voltage))
			{
				minimumVoltage = std::min(minimumVoltage, voltage);
				maximumVoltage = std::max(maximumVoltage, voltage);
			}
		}
		if (!std::isfinite(minimumVoltage) || !std::isfinite(maximumVoltage) || minimumVoltage >= maximumVoltage)
		{
			ImGui::TextDisabled("Active characteristic has no finite voltage range.");
			return;
		}
		if (m_RangeCharacteristic != active)
		{
			finishRangeUndoGroup();
			m_RangeCharacteristic = active;
			m_RangeInitialized = false;
		}
		const auto activePrefit = m_Store->GetPrefitState({project, active});
		if (!m_RangeInitialized)
		{
			m_RangeLower = minimumVoltage;
			m_RangeUpper = maximumVoltage;
			if (activePrefit && activePrefit->fitRange.upperVoltage > activePrefit->fitRange.lowerVoltage)
			{
				m_RangeLower = activePrefit->fitRange.lowerVoltage;
				m_RangeUpper = activePrefit->fitRange.upperVoltage;
			}
			if (activePrefit && activePrefit->manualLowerVoltage && activePrefit->manualUpperVoltage)
			{
				m_RangeLower = *activePrefit->manualLowerVoltage;
				m_RangeUpper = *activePrefit->manualUpperVoltage;
			}
			m_RangeInitialized = true;
		}
		else if (!m_RangeUndoGroupOpen && activePrefit && activePrefit->status == "completed")
		{
			if (activePrefit->manualLowerVoltage && activePrefit->manualUpperVoltage)
			{
				m_RangeLower = *activePrefit->manualLowerVoltage;
				m_RangeUpper = *activePrefit->manualUpperVoltage;
			}
			else if (activePrefit->fitRange.upperVoltage > activePrefit->fitRange.lowerVoltage)
			{
				m_RangeLower = activePrefit->fitRange.lowerVoltage;
				m_RangeUpper = activePrefit->fitRange.upperVoltage;
			}
		}

		ImGui::Text("Fit range: %.6g V to %.6g V", m_RangeLower, m_RangeUpper);
		const ImVec2 plotSize{-1.0f, std::max(300.0f, ImGui::GetContentRegionAvail().y * 0.62f)};
		const ImPlotFlags plotFlags = m_AnalysisShowLegend ? ImPlotFlags_None : ImPlotFlags_NoLegend;
		if (!ImPlot::BeginPlot(showFit ? "##jfm_fit_mc_plot" : "##jfm_data_range_prefit_plot", plotSize, plotFlags))
		{
			finishRangeUndoGroup();
			return;
		}
		ImPlot::SetupAxes("Voltage (V)", "Current / current density");
		if (m_AnalysisShowLegend)
			ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside | ImPlotLegendFlags_NoButtons);
		if (m_AnalysisScale == 1)
			ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

		if (m_AnalysisAutoFit)
		{
			const double fitLower = std::min(m_RangeLower, m_RangeUpper);
			const double fitUpper = std::max(m_RangeLower, m_RangeUpper);
			bool hasBounds = false;
			double minX = fitLower;
			double maxX = fitUpper;
			double minY = 0.0;
			double maxY = 0.0;
			for (CharacteristicId id : ids)
			{
				const auto record = m_Store->FindCharacteristicRecord({project, id});
				if (!record || record->data == nullptr)
					continue;
				const std::vector<double> &x = record->data->voltage;
				const std::vector<double> &y = AnalysisCurrent(*record->data);
				for (std::size_t point = 0; point < x.size() && point < y.size(); ++point)
				{
					if (x[point] < fitLower || x[point] > fitUpper)
						continue;
					const double value = m_AnalysisScale == 1 ? std::abs(y[point]) : y[point];
					if (m_AnalysisScale == 1 && value <= 0.0)
						continue;
					sst::ui::plot::IncludeBounds(x[point], value, hasBounds, minX, maxX, minY, maxY);
				}
			}
			if (hasBounds)
			{
				minX = fitLower;
				maxX = fitUpper;
				double dx = maxX - minX;
				double dy = maxY - minY;
				if (dx <= 0.0)
					dx = std::max(1.0, std::abs(maxX) * 0.1);
				if (dy <= 0.0)
					dy = std::max(1.0, std::abs(maxY) * 0.1);
				minX -= dx * m_AnalysisFitPadding;
				maxX += dx * m_AnalysisFitPadding;
				if (m_AnalysisScale == 1)
				{
					minY = std::max(1.0e-30, minY / (1.0 + m_AnalysisFitPadding * 4.0));
					maxY *= 1.0 + m_AnalysisFitPadding * 4.0;
				}
				else
				{
					minY -= dy * m_AnalysisFitPadding;
					maxY += dy * m_AnalysisFitPadding;
				}
				ImPlot::SetupAxesLimits(minX, maxX, minY, maxY, ImPlotCond_Always);
			}
		}

		if (m_AnalysisShowCurves || m_AnalysisShowPrefitCurves || showFit)
		{
			const JFMUserSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};
			for (std::size_t index = 0; index < ids.size(); ++index)
			{
				const auto record = m_Store->FindCharacteristicRecord({project, ids[index]});
				if (!record || record->data == nullptr)
					continue;
				const std::vector<double> &current = AnalysisCurrent(*record->data);
				const int count = static_cast<int>(std::min(record->data->voltage.size(), current.size()));
				if (m_AnalysisShowCurves && count > 0)
				{
					ImPlotSpec rawSpec;
					rawSpec.LineColor = PreviewColor(index);
					if (ids[index] == active)
						rawSpec.LineWeight = settings.hoveredCurveLineWeight;
					const std::string label = record->info.name + "##jfm_analysis_raw_" + std::to_string(ids[index]);
					if (m_AnalysisScale == 0)
						sst::ui::plot::RenderSeries({label.c_str(), {record->data->voltage.data(), current.data(), static_cast<std::size_t>(count)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(rawSpec)});
					else
					{
						std::vector<double> x(record->data->voltage.begin(), record->data->voltage.begin() + count);
						std::vector<double> y(current.begin(), current.begin() + count);
						ApplyPreviewScale(m_AnalysisScale, x, y);
						if (!x.empty())
							sst::ui::plot::RenderSeries({label.c_str(), {x.data(), y.data(), static_cast<std::size_t>(x.size())}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(rawSpec)});
					}
				}
				const auto curve = m_Store->GetPrefitCurve({project, ids[index]});
				if (m_AnalysisShowPrefitCurves && curve)
				{
					std::optional<JFMPrefitState> prefitState;
					if (settings.prefitCurvesFitRangeOnly)
						prefitState = m_Store->GetPrefitState({project, ids[index]});
					std::vector<double> fitRangeX;
					std::vector<double> fitRangeY;
					const std::vector<double> *prefitX = &curve->voltage;
					const std::vector<double> *prefitY = &curve->current;
					if (prefitState && prefitState->fitRange.upperVoltage > prefitState->fitRange.lowerVoltage)
					{
						CopyFitRange(*curve, prefitState->fitRange, fitRangeX, fitRangeY);
						prefitX = &fitRangeX;
						prefitY = &fitRangeY;
					}
					const int curveCount = static_cast<int>(std::min(prefitX->size(), prefitY->size()));
					if (curveCount > 0)
					{
						ImPlotSpec prefitSpec;
						prefitSpec.LineColor = ImVec4(settings.prefitCurveColor[0], settings.prefitCurveColor[1], settings.prefitCurveColor[2], settings.prefitCurveColor[3]);
						prefitSpec.LineWeight = settings.prefitCurveLineWeight;
						prefitSpec.Flags = ImPlotItemFlags_NoLegend;
						const std::string label = "##jfm_analysis_prefit_" + std::to_string(ids[index]);
						if (m_AnalysisScale == 0)
							sst::ui::plot::RenderSeries({label.c_str(), {prefitX->data(), prefitY->data(), static_cast<std::size_t>(curveCount)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(prefitSpec)});
						else
						{
							std::vector<double> x(prefitX->begin(), prefitX->begin() + curveCount);
							std::vector<double> y(prefitY->begin(), prefitY->begin() + curveCount);
							ApplyPreviewScale(m_AnalysisScale, x, y);
							if (!x.empty())
								sst::ui::plot::RenderSeries({label.c_str(), {x.data(), y.data(), static_cast<std::size_t>(x.size())}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(prefitSpec)});
						}
					}
				}
				const auto fitCurve = showFit ? m_Store->GetFitCurve({project, ids[index]}) : Ref<const JFMFitCurve>{};
				if (fitCurve)
				{
					std::vector<double> fitRangeX;
					std::vector<double> fitRangeY;
					const std::vector<double> *fitX = &fitCurve->voltage;
					const std::vector<double> *fitY = &fitCurve->current;
					if (settings.fitCurvesFitRangeOnly && record->info.fit && record->info.fit->fitRange.upperVoltage > record->info.fit->fitRange.lowerVoltage)
					{
						CopyFitRange(*fitCurve, record->info.fit->fitRange, fitRangeX, fitRangeY);
						fitX = &fitRangeX;
						fitY = &fitRangeY;
					}
					const int fitCount = static_cast<int>(std::min(fitX->size(), fitY->size()));
					if (fitCount > 0)
					{
						ImPlotSpec fitSpec;
						fitSpec.LineColor = ImVec4(settings.fitCurveColor[0], settings.fitCurveColor[1], settings.fitCurveColor[2], settings.fitCurveColor[3]);
						fitSpec.LineWeight = settings.fitCurveLineWeight;
						fitSpec.Flags = ImPlotItemFlags_NoLegend;
						const std::string label = "##jfm_analysis_fit_" + std::to_string(ids[index]);
						if (m_AnalysisScale == 0)
							sst::ui::plot::RenderSeries({label.c_str(), {fitX->data(), fitY->data(), static_cast<std::size_t>(fitCount)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(fitSpec)});
						else
						{
							std::vector<double> x(fitX->begin(), fitX->begin() + fitCount);
							std::vector<double> y(fitY->begin(), fitY->begin() + fitCount);
							ApplyPreviewScale(m_AnalysisScale, x, y);
							if (!x.empty())
								sst::ui::plot::RenderSeries({label.c_str(), {x.data(), y.data(), static_cast<std::size_t>(x.size())}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(fitSpec)});
						}
					}
				}
			}
		}

		sst::ui::plot::RangeStyle rangeStyle;
		rangeStyle.minimum = minimumVoltage;
		rangeStyle.maximum = maximumVoltage;
		const auto interaction = sst::ui::plot::DragRangeX(3101, 3102, m_RangeLower, m_RangeUpper, m_RangeInteraction, rangeStyle);
		const bool rangeCursor = interaction.hovered;
		const bool changed = interaction.changed;
		if (changed)
		{
			if (!m_RangeUndoGroupOpen)
			{
				if (auto undoStack = m_UndoStack.lock())
				{
					if (undoStack->BeginGroup("Change fit range"))
						m_RangeUndoGroupOpen = true;
				}
			}
			if (m_RangeLower != m_RangeUpper)
			{
				if (auto registry = m_CommandRegistry.lock())
				{
					CommandContext context;
					context.Set("project", project);
					context.Set("characteristic_id", active);
					context.Set("lower_voltage", m_RangeLower);
					context.Set("upper_voltage", m_RangeUpper);
					(void)registry->Execute(CommandID{JFMSetFitRangeCommandId}, std::move(context));
				}
				if (m_EventBus != nullptr && m_Settings != nullptr)
				{
					const JFMUserSettings settings = m_Settings->GetSnapshot();
					if (settings.autoPrefitOnRangeChange)
						m_EventBus->Queue(PrefitRequested{project, {active}});
					if (settings.autoFitOnRangeChange)
						m_EventBus->Queue(FitRequested{project, {active}});
				}
			}
		}
		ImPlot::EndPlot();
		if (rangeCursor)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

		if (m_RangeUndoGroupOpen && interaction.ended)
			finishRangeUndoGroup();
	}

	void JFMImportDialogPanel::renderParameterTable(ProjectHandle project, CharacteristicId active)
	{
		const auto record = m_Store != nullptr ? m_Store->FindCharacteristicRecord({project, active}) : std::nullopt;
		if (!record)
			return;
		const std::optional<JFMPrefitState> &prefit = record->info.prefit;
		const std::optional<JFMFitState> &fit = record->info.fit;
		if (!prefit && !fit)
			return;
		ImGui::SeparatorText("Parameters");
		if (prefit)
			ImGui::Text("Prefit: %s", prefit->status.c_str());
		if (fit)
		{
			ImGui::SameLine();
			ImGui::Text("Fit: %s, reduced chi2 %.6g", fit->status.c_str(), fit->reducedChiSquared);
		}
		JFMUserSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};
		const std::string model = fit ? fit->modelId : prefit ? prefit->modelId : "SD-RsRsh";
		bool settingsChanged = ApplySuggestedBounds(settings.fitMonteCarlo, prefit, fit, model, true);
		if (ImGui::Button("Suggest bounds"))
		{
			settingsChanged |= ApplySuggestedBounds(settings.fitMonteCarlo, prefit, fit, model, false);
		}
		if (ImGui::BeginTable("##jfm_import_parameters", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Parameter");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Std. error");
			ImGui::TableSetupColumn("Source");
			ImGui::TableSetupColumn("Minimum");
			ImGui::TableSetupColumn("Maximum");
			ImGui::TableHeadersRow();
			auto renderRow = [&](const char *stage, const std::string &name, const JFMParameterEstimate &estimate) {
				JFMParameterSolverSetting &parameter = settings.fitMonteCarlo.parameters[name];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(stage);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.8g", estimate.value);
				ImGui::TableSetColumnIndex(3);
				if (estimate.standardError)
					ImGui::Text("%.8g", *estimate.standardError);
				else
					ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(estimate.source.c_str());
				ImGui::TableSetColumnIndex(5);
				ImGui::SetNextItemWidth(-1.0f);
				settingsChanged |= ImGui::InputDouble(("##min_" + std::string(stage) + name).c_str(), &parameter.minimum, 0.0, 0.0, "%.6g");
				ImGui::TableSetColumnIndex(6);
				ImGui::SetNextItemWidth(-1.0f);
				settingsChanged |= ImGui::InputDouble(("##max_" + std::string(stage) + name).c_str(), &parameter.maximum, 0.0, 0.0, "%.6g");
			};
			if (prefit)
			{
				std::vector<std::string> names;
				names.reserve(prefit->parameters.size());
				for (const auto &[name, estimate] : prefit->parameters)
				{
					(void)estimate;
					names.push_back(name);
				}
				std::sort(names.begin(), names.end());
				for (const std::string &name : names)
					renderRow("prefit", name, prefit->parameters.at(name));
			}
			if (fit)
			{
				std::vector<std::string> names;
				names.reserve(fit->parameters.size());
				for (const auto &[name, estimate] : fit->parameters)
				{
					(void)estimate;
					names.push_back(name);
				}
				std::sort(names.begin(), names.end());
				for (const std::string &name : names)
					renderRow("fit", name, fit->parameters.at(name));
			}
			ImGui::EndTable();
		}
		if (settingsChanged && m_Settings != nullptr)
			m_Settings->SetFitMonteCarloSettings(settings.fitMonteCarlo);
		if (prefit)
			for (const std::string &warning : prefit->warnings)
			ImGui::TextWrapped("Warning: %s", warning.c_str());
		if (fit)
			for (const std::string &warning : fit->warnings)
				ImGui::TextWrapped("Fit warning: %s", warning.c_str());
	}

	void JFMImportDialogPanel::renderFitMonteCarloResults(ProjectHandle project, CharacteristicId active, const std::vector<CharacteristicId> &selectedIds)
	{
		const auto record = m_Store != nullptr ? m_Store->FindCharacteristicRecord({project, active}) : std::nullopt;
		if (!record)
			return;
		ImGui::SeparatorText("Fit & Monte Carlo");
		if (m_Settings != nullptr)
		{
			JFMUserSettings settings = m_Settings->GetSnapshot();
			const std::string model = record->info.prefit ? record->info.prefit->modelId : "SD-RsRsh";
			if (RenderJFMFitMonteCarloSettings(settings.fitMonteCarlo, model, "import_fit_mc", false))
				m_Settings->SetFitMonteCarloSettings(std::move(settings.fitMonteCarlo));
		}
		if (ImGui::Button("Run Monte Carlo") && m_EventBus != nullptr)
			m_EventBus->Queue(MonteCarloRequested{project, selectedIds});

		if (record->info.fit)
		{
			const JFMFitState &fit = *record->info.fit;
			ImGui::SameLine();
			ImGui::Text("Fit: %s, reduced chi2 %.6g, %d iterations", fit.status.c_str(), fit.reducedChiSquared, fit.iterations);
		}

		if (record->info.monteCarlo)
		{
			const JFMMonteCarloState &state = *record->info.monteCarlo;
			ImGui::Text("MC: %s, %d/%d accepted, %d converged, %s, %.3f ms", state.status.c_str(), state.acceptedCount, state.sampleCount, state.convergedCount, state.backend.c_str(), state.totalMilliseconds);
			if (!state.outputPath.Empty())
				ImGui::TextWrapped("Output: %s", state.outputPath.Utf8().c_str());
			for (const std::string &warning : state.warnings)
				ImGui::TextWrapped("MC warning: %s", warning.c_str());
		}
	}

	void JFMImportDialogPanel::requestPrefit(ProjectHandle project, std::vector<CharacteristicId> ids) const
	{
		if (m_EventBus != nullptr && !ids.empty())
			m_EventBus->Queue(PrefitRequested{project, std::move(ids)});
	}

	void JFMImportDialogPanel::requestFit(ProjectHandle project, std::vector<CharacteristicId> ids) const
	{
		if (m_EventBus != nullptr && !ids.empty())
			m_EventBus->Queue(FitRequested{project, std::move(ids)});
	}

	void JFMImportDialogPanel::finishRangeUndoGroup(bool cancel)
	{
		(void)m_RangeInteraction.Finish();
		if (!m_RangeUndoGroupOpen)
			return;
		if (auto undoStack = m_UndoStack.lock())
		{
			if (cancel)
				(void)undoStack->CancelGroup();
			else
				(void)undoStack->EndGroup();
		}
		m_RangeUndoGroupOpen = false;
	}

	void JFMImportDialogPanel::resetStaging()
	{
		finishRangeUndoGroup();
		m_StagingProject = InvalidProjectHandle;
		m_StagedImportBatches.clear();
		m_StagedSeries.clear();
		m_DataAddedToProject = false;
		m_SelectImportTab = false;
		m_SelectAnalysisTab = false;
		m_SelectFitTab = false;
		m_PrepareTargetFitTab = false;
		m_RangeCharacteristic = InvalidCharacteristicId;
		m_RangeInitialized = false;
	}

	void JFMImportDialogPanel::submitStagingImport(bool selectFitTabAfterImport)
	{
		const bool validDestination = m_Request.placement != JFMImportPlacement::ExistingSeries || !m_Request.targetSeriesId.empty();
		if (m_ImportSubmitted || !validDestination)
			return;
		resetStaging();
		m_PrepareTargetFitTab = selectFitTabAfterImport;
		m_ImportOperation = m_NextImportOperation++;
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportRequested{
				m_Request.paths,
				m_Request.targetSeriesId,
				m_Request.recursive,
				buildOptions(),
				m_PlotAfterImport,
				m_Request.project,
				m_ImportOperation,
				m_Request.targetGroupId,
				m_Request.placement,
				true});
		m_ImportSubmitted = true;
		m_PreviewOpen = false;
	}

	void JFMImportDialogPanel::handleImportCompleted(const ImportCompleted &event)
	{
		if (event.operation != m_ImportOperation || event.project != m_Request.project)
			return;
		m_StagedImportBatches.push_back(event);
		if (!event.finalBatch)
			return;

		m_ImportSubmitted = false;
		buildStagingProject();
		m_SelectAnalysisTab = !m_PrepareTargetFitTab;
		m_SelectFitTab = m_PrepareTargetFitTab;
		m_PrepareTargetFitTab = false;
	}

	void JFMImportDialogPanel::handleImportFailed(const ImportFailed &event)
	{
		if (event.operation != m_ImportOperation || event.project != m_Request.project)
			return;
		m_ImportSubmitted = false;
		m_PreviewError = event.error;
	}

	void JFMImportDialogPanel::buildStagingProject()
	{
		if (m_Store == nullptr)
			return;

		m_StagingProject = m_Store->CreateTransientProject("Import staging");
		m_StagedSeries.clear();
		const JFMProject targetProject = m_Store->GetProjectSnapshot(m_Request.project);
		std::vector<CharacteristicId> allIds;

		for (const ImportCompleted &batch : m_StagedImportBatches)
		{
			if (batch.characteristics.empty())
				continue;

			std::string groupId;
			if (!batch.groupPath.empty())
				groupId = m_Store->FindOrCreateGroupPath(m_StagingProject, batch.groupPath);

			std::string seriesName = batch.seriesName;
			if (seriesName.empty() && !batch.targetSeriesId.empty())
			{
				const auto targetSeries = std::find_if(targetProject.series.begin(), targetProject.series.end(), [&batch](const JFMSeries &series) {
					return series.id == batch.targetSeriesId;
				});
				if (targetSeries != targetProject.series.end())
					seriesName = targetSeries->name;
			}
			if (seriesName.empty())
				seriesName = "Series";

			const JFMSeries series = m_Store->AddSeries(m_StagingProject, seriesName, groupId);
			std::vector<JFMCharacteristicRecord> records = batch.characteristics;
			for (JFMCharacteristicRecord &record : records)
				record.info.id = InvalidCharacteristicId;
			std::vector<CharacteristicId> ids = m_Store->AddCharacteristicRecords(m_StagingProject, std::move(records), series.id);
			allIds.insert(allIds.end(), ids.begin(), ids.end());
			m_StagedSeries.push_back(StagedSeries{batch.targetSeriesId, batch.groupPath, seriesName, std::move(ids)});
		}

		if (!allIds.empty())
		{
			m_Store->SetSelectedCharacteristics(m_StagingProject, {allIds.front()}, allIds.front());
			requestPrefit(m_StagingProject, std::move(allIds));
		}
	}

	std::vector<ImportCompleted> JFMImportDialogPanel::buildApplyBatchesFromStaging() const
	{
		std::vector<ImportCompleted> batches;
		if (m_Store == nullptr || m_StagingProject == InvalidProjectHandle)
			return batches;

		batches.reserve(m_StagedSeries.size());
		for (const StagedSeries &series : m_StagedSeries)
		{
			std::vector<JFMCharacteristicRecord> records;
			records.reserve(series.characteristicIds.size());
			for (CharacteristicId id : series.characteristicIds)
			{
				auto record = m_Store->FindCharacteristicRecord({m_StagingProject, id});
				if (!record)
					continue;
				record->info.id = InvalidCharacteristicId;
				records.push_back(std::move(*record));
			}
			if (!records.empty())
				batches.emplace_back(std::move(records), std::vector<std::string>{}, series.targetSeriesId, series.groupPath, series.seriesName);
		}
		return batches;
	}

	void JFMImportDialogPanel::loadRequest(JFMImportDialogRequest request)
	{
		finishRangeUndoGroup();
		m_Request = std::move(request);
		if (m_Request.project == InvalidProjectHandle && m_Store != nullptr)
			m_Request.project = m_Store->GetActiveProjectHandle();
		m_Parser = 0;
		m_DecimalSeparator = 0;
		m_TemperatureExtraction = 0;
		m_Delimiter.fill('\0');
		m_ShortNamePattern.fill('\0');
		m_VariableDelimiter.fill('\0');
		m_CustomScriptPath.fill('\0');
		m_PreviewFileIndex = 0;
		m_PreviewScale = 0;
		m_PreviewFolderIndex = 0;
		m_PlotAfterImport = true;
		m_ImportSubmitted = false;
		m_SelectImportTab = true;
		m_SelectAnalysisTab = false;
		m_ShowOnlyActive = false;
		m_DataAddedToProject = false;
		m_AnalysisAutoFit = true;
		m_AnalysisShowLegend = false;
		m_AnalysisShowCurves = true;
		m_AnalysisShowPrefitCurves = true;
		m_AnalysisShowFitCurves = true;
		m_AnalysisScale = 0;
		m_AnalysisTreeOpenRequest = 0;
		m_AnalysisFitPadding = 0.08f;
		m_AnalysisOnlySeriesId.clear();
		m_ImportOperation = InvalidJFMOperationId;
		m_StagingProject = InvalidProjectHandle;
		m_StagedImportBatches.clear();
		m_StagedSeries.clear();
		m_RangeCharacteristic = InvalidCharacteristicId;
		m_RangeInitialized = false;
		(void)m_RangeInteraction.Finish();
		m_SelectedPreviewFileIndices.clear();
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
		std::snprintf(m_VariableDelimiter.data(), m_VariableDelimiter.size(), "%s", "_");
		m_PreviewPayload.reset();
		m_PreviewError.clear();
		m_LastPreviewOptionsKey = previewOptionsKey();
		requestPreview();
	}

	void JFMImportDialogPanel::requestPreview()
	{
		if (m_EventBus == nullptr || m_Request.paths.empty())
			return;
		m_PreviewLoading = true;
		m_PreviewError.clear();
		m_EventBus->Queue(ImportPreviewRequested{m_PreviewGeneration.Next(), m_Request.paths, m_Request.recursive, buildOptions()});
	}

	void JFMImportDialogPanel::handlePreviewCompleted(const ImportPreviewCompleted &event)
	{
		if (!m_PreviewGeneration.IsCurrent(event.generation))
			return;
		m_PreviewPayload = event.payload;
		m_PreviewLoading = false;
		m_PreviewError.clear();
		refreshPreviewFiles();
	}

	void JFMImportDialogPanel::handlePreviewFailed(const ImportPreviewFailed &event)
	{
		if (!m_PreviewGeneration.IsCurrent(event.generation))
			return;
		m_PreviewLoading = false;
		m_PreviewError = event.error;
	}

	void JFMImportDialogPanel::refreshPreviewFiles()
	{
		m_AllPreviewFiles.clear();
		m_PreviewFolders.clear();
		m_PreviewFolderLabels.clear();
		m_PreviewFolders.push_back("All folders");
		m_PreviewFolderLabels.push_back("All folders");
		if (m_PreviewPayload != nullptr)
		{
			std::map<std::string, std::string> folders;
			for (const JFMImportPreviewItem &item : m_PreviewPayload->items)
			{
				m_AllPreviewFiles.emplace_back(item.path);
				folders.try_emplace(item.folderKey, item.folderLabel);
			}
			for (const auto &[key, label] : folders)
			{
				m_PreviewFolders.emplace_back(key);
				m_PreviewFolderLabels.emplace_back(label);
			}
		}
		applyPreviewFolderFilter();
	}

	void JFMImportDialogPanel::applyPreviewFolderFilter()
	{
		const std::string folder = m_PreviewFolderIndex > 0 && m_PreviewFolderIndex < static_cast<int>(m_PreviewFolders.size())
			? m_PreviewFolders[static_cast<std::size_t>(m_PreviewFolderIndex)]
			: std::string{};
		m_PreviewFiles.clear();
		for (const Path &path : m_AllPreviewFiles)
		{
			if (folder.empty() || path.parent_path().Utf8() == folder)
				m_PreviewFiles.push_back(path);
		}
		if (m_PreviewFileIndex >= static_cast<int>(m_PreviewFiles.size()))
			m_PreviewFileIndex = 0;
		m_SelectedPreviewFileIndices.erase(
			std::remove_if(
				m_SelectedPreviewFileIndices.begin(),
				m_SelectedPreviewFileIndices.end(),
				[this](int index) { return index < 0 || index >= static_cast<int>(m_PreviewFiles.size()); }),
			m_SelectedPreviewFileIndices.end());
		if (m_SelectedPreviewFileIndices.empty() && !m_PreviewFiles.empty())
			m_SelectedPreviewFileIndices.push_back(0);
	}

	void JFMImportDialogPanel::renderCustomVariables()
	{
		if (!ImGui::TreeNodeEx("Custom short-name variables", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::TextDisabled("Use custom variables in patterns as {variable_name}; duplicate names override built-ins.");
		if (ImGui::BeginTable("##jfm_custom_variables", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.28f);
			ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.22f);
			ImGui::TableSetupColumn("Token", ImGuiTableColumnFlags_WidthFixed, 78.0f);
			ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 78.0f);
			ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 78.0f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableHeadersRow();
			for (std::size_t index = 0; index < m_CustomVariables.size();)
			{
				VariableRuleDraft &rule = m_CustomVariables[index];
				ImGui::PushID(static_cast<int>(index));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputText("##name", rule.name.data(), rule.name.size());
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::Combo("##source", &rule.source, VariableSources, IM_ARRAYSIZE(VariableSources));
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputInt("##token", &rule.tokenIndex, 1, 1);
				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputInt("##start", &rule.substringStart, 1, 1);
				ImGui::TableSetColumnIndex(4);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputInt("##length", &rule.substringLength, 1, 1);
				ImGui::TableSetColumnIndex(5);
				if (ImGui::SmallButton("-"))
				{
					m_CustomVariables.erase(m_CustomVariables.begin() + static_cast<std::ptrdiff_t>(index));
					ImGui::PopID();
					continue;
				}
				rule.tokenIndex = std::max(-1, rule.tokenIndex);
				rule.substringStart = std::max(0, rule.substringStart);
				rule.substringLength = std::max(-1, rule.substringLength);
				ImGui::PopID();
				++index;
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("+ Variable"))
		{
			VariableRuleDraft rule;
			std::snprintf(rule.name.data(), rule.name.size(), "var%d", static_cast<int>(m_CustomVariables.size() + 1));
			m_CustomVariables.push_back(rule);
		}
		ImGui::TreePop();
	}

	void JFMImportDialogPanel::renderVariablePreviewTable() const
	{
		const Path path = selectedPreviewPath();
		const std::string sampleName = path.Empty() ? std::string{"<none>"} : path.filename().Utf8();
		ImGui::TextDisabled("Sample: %s", sampleName.c_str());
		const JFMImportPreviewItem *item = previewItem(path);
		const JFMCharacteristicInfo *info = item != nullptr && !item->characteristics.empty() ? &item->characteristics.front().info : nullptr;
		if (!ImGui::BeginChild("##jfm_variable_preview_scroll", ImVec2(0.0f, 430.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
			return;
		if (!ImGui::BeginTable("##jfm_variable_preview", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
		{
			ImGui::EndChild();
			return;
		}

		ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 0.22f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.38f);
		ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.40f);
		ImGui::TableHeadersRow();
		if (info != nullptr)
		{
			std::vector<std::pair<std::string, std::string>> variables(info->metadata.begin(), info->metadata.end());
			std::sort(variables.begin(), variables.end());
			for (const auto &[name, value] : variables)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("{%s}", name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextWrapped("%s", value.c_str());
				ImGui::TableSetColumnIndex(2);
				if (name == "T" || name == "temperature_K")
					ImGui::TextUnformatted("parser temperature extraction");
				else if (name == "source_path")
					ImGui::TextUnformatted("full source path");
				else if (name == "file_name" || name == "file_stem")
					ImGui::TextUnformatted("source file");
				else if (name == "folder" || name == "parent_folder" || name.starts_with("parent_folder_"))
					ImGui::TextUnformatted("source folder");
				else if (name == "folder_path" || name == "parent_path" || name.starts_with("parent_path_"))
					ImGui::TextUnformatted("source path");
				else if (name.starts_with("stem_"))
					ImGui::TextUnformatted("file stem token");
				else if (name.starts_with("folder_"))
					ImGui::TextUnformatted("folder token");
				else
					ImGui::TextUnformatted("parser metadata");
			}
		}
		ImGui::EndTable();
		ImGui::EndChild();
	}

	void JFMImportDialogPanel::renderPreviewPlot(const char *plotId, float plotHeight) const
	{
		const std::vector<Path> previewPaths = selectedPreviewPaths();
		if (previewPaths.empty() || !ImPlot::BeginPlot(plotId, ImVec2(-1.0f, plotHeight)))
		{
			ImGui::TextDisabled("Mini plot unavailable for this file.");
			return;
		}

		ImPlot::SetupAxes("Voltage", "Current");
		if (m_PreviewScale == 1)
			ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
		for (std::size_t index = 0; index < previewPaths.size(); ++index)
		{
			const JFMImportPreviewItem *item = previewItem(previewPaths[index]);
			if (item == nullptr)
				continue;
			for (std::size_t curveIndex = 0; curveIndex < item->characteristics.size(); ++curveIndex)
			{
				const JFMCharacteristicRecord &characteristic = item->characteristics[curveIndex];
				if (!characteristic.data)
					continue;
				std::vector<double> x = characteristic.data->voltage;
				std::vector<double> y = characteristic.data->currentDensity.empty()
					? characteristic.data->current
					: characteristic.data->currentDensity;
				ApplyPreviewScale(m_PreviewScale, x, y);
				if (x.empty() || y.empty())
					continue;
				const ImPlotSpec spec(ImPlotProp_LineColor, PreviewColor(index + curveIndex));
				const std::string label = characteristic.info.name + "##preview_plot_" + std::to_string(index) + "_" + std::to_string(curveIndex);
				sst::ui::plot::RenderSeries({label.c_str(), {x.data(), y.data(), static_cast<std::size_t>(std::min(x.size(), y.size()))}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(spec)});
			}
		}
		ImPlot::EndPlot();
	}

	void JFMImportDialogPanel::renderPreviewWindow()
	{
		if (!m_PreviewOpen)
			return;

		ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);
        m_Windows.Render("Import Preview", m_PreviewOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings, [&] {

		if (m_PreviewFiles.empty())
		{
			ImGui::TextDisabled("No previewable data files.");
			return;
		}

		std::vector<ImportSourceRow> rows;
		for (const auto &path : m_PreviewFiles) {
			const auto *item = previewItem(path);
			rows.push_back({item && !item->relativePath.empty() ? item->relativePath : path.filename().Utf8(),
				path.Utf8(), item ? item->warning : std::string{}});
		}
		RenderImportSources(rows, m_SelectedPreviewFileIndices, m_PreviewFileIndex, [&](int) {
		if (m_PreviewFolders.size() > 1)
		{
			ImGui::SetNextItemWidth(320.0f);
			if (sst::ui::widgets::BeginCombo("Folder", m_PreviewFolderLabels[static_cast<std::size_t>(m_PreviewFolderIndex)].c_str()))
			{
				for (std::size_t index = 0; index < m_PreviewFolderLabels.size(); ++index)
				{
					const bool selected = static_cast<int>(index) == m_PreviewFolderIndex;
					if (ImGui::Selectable(m_PreviewFolderLabels[index].c_str(), selected))
					{
						m_PreviewFolderIndex = static_cast<int>(index);
						m_SelectedPreviewFileIndices.clear();
						applyPreviewFolderFilter();
					}
					if (ImGui::IsItemHovered() && index < m_PreviewFolders.size())
						ImGui::SetTooltip("%s", m_PreviewFolders[index].c_str());
				}
				sst::ui::widgets::EndCombo();
			}
		}
		ImGui::RadioButton("Linear", &m_PreviewScale, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Semi-log", &m_PreviewScale, 1);
		ImGui::SameLine();
		if (ImGui::Button("All"))
		{
			m_SelectedPreviewFileIndices.clear();
			m_SelectedPreviewFileIndices.reserve(m_PreviewFiles.size());
			for (std::size_t index = 0; index < m_PreviewFiles.size(); ++index)
				m_SelectedPreviewFileIndices.push_back(static_cast<int>(index));
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			m_SelectedPreviewFileIndices.clear();
		const float plotHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y - 250.0f);
		renderPreviewPlot("Preview Plot##import_preview_window", plotHeight);

		const JFMImportPreviewItem *selectedItem = previewItem(selectedPreviewPath());
		ImGui::SeparatorText("Text");
		ImGui::BeginChild("##jfm_import_text_preview", ImVec2(0.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
		if (selectedItem != nullptr && !selectedItem->warning.empty())
			ImGui::TextWrapped("Warning: %s", selectedItem->warning.c_str());
		ImGui::TextUnformatted(selectedItem == nullptr || selectedItem->text.empty() ? "No preview" : selectedItem->text.c_str());
		ImGui::EndChild();
		});
        });
	}

	JFMImportOptions JFMImportDialogPanel::buildOptions() const
	{
		JFMImportOptions options;
		options.parser = ParserOptions[static_cast<std::size_t>(m_Parser)];
		options.namePattern = m_NamePattern.data();
		options.shortNamePattern = m_ShortNamePattern.data();
		options.variableDelimiter = m_VariableDelimiter.data();
		options.customVariables = buildVariableRules();
		options.delimiter = m_Delimiter.data();
		options.decimalSeparator = DecimalOptions[static_cast<std::size_t>(m_DecimalSeparator)];
		options.temperatureExtraction = TemperatureOptions[static_cast<std::size_t>(m_TemperatureExtraction)];
		options.customScriptPath = m_CustomScriptPath.data();
		return options;
	}

	std::vector<JFMNameVariableRule> JFMImportDialogPanel::buildVariableRules() const
	{
		std::vector<JFMNameVariableRule> rules;
		rules.reserve(m_CustomVariables.size());
		for (const VariableRuleDraft &draft : m_CustomVariables)
		{
			if (draft.name[0] == '\0')
				continue;
			JFMNameVariableRule rule;
			rule.name = draft.name.data();
			rule.source = VariableSources[static_cast<std::size_t>(std::clamp(draft.source, 0, static_cast<int>(IM_ARRAYSIZE(VariableSources)) - 1))];
			rule.tokenIndex = std::max(-1, draft.tokenIndex);
			rule.substringStart = std::max(0, draft.substringStart);
			rule.substringLength = std::max(-1, draft.substringLength);
			rules.push_back(std::move(rule));
		}
		return rules;
	}

	std::string JFMImportDialogPanel::previewName() const
	{
		const JFMImportPreviewItem *item = previewItem(selectedPreviewPath());
		if (item != nullptr && !item->characteristics.empty())
			return item->characteristics.front().info.name;
		return {};
	}

	std::string JFMImportDialogPanel::previewShortName() const
	{
		const JFMImportPreviewItem *item = previewItem(selectedPreviewPath());
		if (item == nullptr || item->characteristics.empty())
			return {};
		const auto value = item->characteristics.front().info.metadata.find("short_name");
		return value == item->characteristics.front().info.metadata.end() ? std::string{} : value->second;
	}

	Path JFMImportDialogPanel::selectedPreviewPath() const
	{
		if (!m_PreviewFiles.empty())
			return m_PreviewFiles[static_cast<std::size_t>(std::clamp(m_PreviewFileIndex, 0, static_cast<int>(m_PreviewFiles.size()) - 1))];
		return m_Request.paths.empty() ? Path{} : m_Request.paths.front();
	}

	std::vector<Path> JFMImportDialogPanel::selectedPreviewPaths() const
	{
		std::vector<Path> paths;
		paths.reserve(m_SelectedPreviewFileIndices.size());
		for (int index : m_SelectedPreviewFileIndices)
		{
			if (index >= 0 && index < static_cast<int>(m_PreviewFiles.size()))
				paths.push_back(m_PreviewFiles[static_cast<std::size_t>(index)]);
		}
		if (paths.empty() && !m_PreviewFiles.empty())
			paths.push_back(selectedPreviewPath());
		return paths;
	}

	const JFMImportPreviewItem *JFMImportDialogPanel::previewItem(const Path &path) const
	{
		if (m_PreviewPayload == nullptr)
			return nullptr;
		const auto item = std::find_if(m_PreviewPayload->items.begin(), m_PreviewPayload->items.end(), [&path](const JFMImportPreviewItem &candidate) {
			return candidate.path == path;
		});
		return item == m_PreviewPayload->items.end() ? nullptr : &*item;
	}

	std::string JFMImportDialogPanel::previewOptionsKey() const
	{
		const JFMImportOptions options = buildOptions();
		std::ostringstream key;
		key << options.parser << '\x1f'
			<< options.namePattern << '\x1f'
			<< options.shortNamePattern << '\x1f'
			<< options.variableDelimiter << '\x1f'
			<< options.delimiter << '\x1f'
			<< options.decimalSeparator << '\x1f'
			<< options.temperatureExtraction << '\x1f'
			<< options.customScriptPath;
		for (const JFMNameVariableRule &rule : options.customVariables)
			key << '\x1e' << rule.name << '\x1f' << rule.source << '\x1f' << rule.tokenIndex << '\x1f' << rule.substringStart << '\x1f' << rule.substringLength;
		return key.str();
	}
} // namespace SST::Prototypes::JFM
