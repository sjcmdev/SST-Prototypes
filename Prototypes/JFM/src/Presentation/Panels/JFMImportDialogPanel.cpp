#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMImportDialogPanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <imgui.h>
#include <implot.h>
#include <regex>
#include <sstream>
#include <set>
#include <system_error>
#include <unordered_map>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr NativeFileDialogFilter PythonScriptFilter{"Python", "py"};

		const char *ParserOptions[] = {"auto", "dat", "scaps_iv", "custom"};
		const char *DecimalOptions[] = {"auto", ".", ","};
		const char *TemperatureOptions[] = {"auto", "filename", "header", "off"};
		const char *VariableSources[] = {"stem", "folder", "parent_folder"};

		struct NameVariablePreview
		{
			std::string name;
			std::string value;
			std::string source;
		};

		std::string DetectTemperatureToken(const Path &path);

		std::string ReplaceAll(std::string text, const std::string &from, const std::string &to)
		{
			std::size_t position = 0;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				text.replace(position, from.size(), to);
				position += to.size();
			}
			return text;
		}

		std::vector<std::string> SplitTokens(const std::string &text, const std::string &delimiter)
		{
			if (delimiter.empty())
				return {text};
			std::vector<std::string> tokens;
			std::size_t start = 0;
			while (start <= text.size())
			{
				const std::size_t end = text.find(delimiter, start);
				tokens.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
				if (end == std::string::npos)
					break;
				start = end + delimiter.size();
			}
			return tokens;
		}

		std::string ApplySubstring(std::string text, int start, int length)
		{
			const std::size_t safeStart = static_cast<std::size_t>(std::clamp(start, 0, static_cast<int>(text.size())));
			if (length < 0)
				return text.substr(safeStart);
			return text.substr(safeStart, static_cast<std::size_t>(length));
		}

		void AddVariable(
			std::vector<NameVariablePreview> &previews,
			std::unordered_map<std::string, std::string> &variables,
			std::string name,
			std::string value,
			std::string source)
		{
			variables[name] = value;
			previews.push_back(NameVariablePreview{std::move(name), std::move(value), std::move(source)});
		}

		std::vector<NameVariablePreview> BuildNameVariables(
			const Path &path,
			const std::string &delimiter,
			const std::vector<JFMNameVariableRule> &rules)
		{
			std::vector<NameVariablePreview> previews;
			std::unordered_map<std::string, std::string> variables;
			if (path.Empty())
				return previews;

			const std::string stem = path.Native().stem().string();
			const std::string folder = path.parent_path().filename().String();
			const std::string parentFolder = path.parent_path().parent_path().filename().String();
			AddVariable(previews, variables, "stem", stem, "file name without extension");
			AddVariable(previews, variables, "folder", folder, "parent folder name");
			AddVariable(previews, variables, "parent_folder", parentFolder, "parent of parent folder");
			AddVariable(previews, variables, "source_path", path.String(), "full source path");
			const std::string temperature = DetectTemperatureToken(path);
			AddVariable(previews, variables, "T", temperature, "temperature parsed from file name");
			AddVariable(previews, variables, "temperature_K", temperature, "temperature parsed from file name");

			for (const auto &[key, text] : std::vector<std::pair<std::string, std::string>>{{"stem", stem}, {"folder", folder}})
			{
				const std::vector<std::string> tokens = SplitTokens(text, delimiter);
				for (std::size_t index = 0; index < tokens.size(); ++index)
					AddVariable(previews, variables, key + "_" + std::to_string(index), tokens[index], key + " split by '" + delimiter + "'");
			}

			for (const JFMNameVariableRule &rule : rules)
			{
				if (rule.name.empty())
					continue;
				std::string value;
				if (const auto it = variables.find(rule.source); it != variables.end())
					value = it->second;
				std::string source = rule.source;
				if (rule.tokenIndex >= 0)
				{
					const std::vector<std::string> tokens = SplitTokens(value, delimiter);
					value = rule.tokenIndex < static_cast<int>(tokens.size()) ? tokens[static_cast<std::size_t>(rule.tokenIndex)] : std::string{};
					source += " token " + std::to_string(rule.tokenIndex);
				}
				value = ApplySubstring(std::move(value), rule.substringStart, rule.substringLength);
				if (rule.substringStart > 0 || rule.substringLength >= 0)
					source += " substring " + std::to_string(rule.substringStart) + ":" + (rule.substringLength < 0 ? std::string{"end"} : std::to_string(rule.substringLength));
				AddVariable(previews, variables, rule.name, std::move(value), std::move(source));
			}

			return previews;
		}

		std::string DetectTemperatureToken(const Path &path)
		{
			const std::string name = path.filename().String();
			const std::regex kelvinPattern(R"((\d+(?:[.,]\d+)?)\s*K\b)", std::regex::icase);
			const std::regex tPattern(R"(T(?:emp(?:erature)?)?[_:= -]?(\d+(?:[.,]\d+)?))", std::regex::icase);
			std::smatch match;
			if ((!std::regex_search(name, match, kelvinPattern) && !std::regex_search(name, match, tPattern)) || match.size() < 2)
				return {};
			std::string value = match[1].str();
			std::replace(value.begin(), value.end(), ',', '.');
			return value;
		}

		std::string BuildNameFromPattern(
			const std::string &pattern,
			const Path &path,
			const std::string &delimiter,
			const std::vector<JFMNameVariableRule> &customVariables)
		{
			if (path.Empty())
				return pattern;

			std::string text = pattern;
			for (const NameVariablePreview &variable : BuildNameVariables(path, delimiter, customVariables))
				text = ReplaceAll(text, "{" + variable.name + "}", variable.value);
			return text.empty() ? path.Native().stem().string() : text;
		}

		bool IsDataFile(const Path &path)
		{
			std::string extension = path.extension().String();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			});
			return extension == ".dat" || extension == ".iv" || extension == ".txt" || extension == ".csv";
		}

		bool IsDirectory(const Path &path)
		{
			std::error_code error;
			return std::filesystem::is_directory(path.Native(), error);
		}

		bool IsRegularFile(const Path &path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path.Native(), error);
		}

		void AddFilesFromDirectory(const Path &directory, bool recursive, std::vector<Path> &files)
		{
			std::error_code error;
			if (!recursive)
			{
				for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
				{
					if (error)
						break;
					const Path path = Path::FromResolved(entry.path());
					std::error_code fileError;
					if (entry.is_regular_file(fileError) && IsDataFile(path))
						files.push_back(path);
				}
				return;
			}

			const auto options = std::filesystem::directory_options::skip_permission_denied;
			for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(directory.Native(), options, error))
			{
				if (error)
					break;
				const Path path = Path::FromResolved(entry.path());
				std::error_code fileError;
				if (entry.is_regular_file(fileError) && IsDataFile(path))
					files.push_back(path);
			}
		}

		bool ContainsIndex(const std::vector<int> &indices, int index)
		{
			return std::find(indices.begin(), indices.end(), index) != indices.end();
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
	}

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

	JFMImportDialogPanel::JFMImportDialogPanel(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMImportDialogState> state, Ref<JFMUserSettingsStore> settings)
		: IPanel("Import Dialog", false), m_EventBus(std::move(eventBus)), m_Store(std::move(store)), m_State(std::move(state)), m_Settings(std::move(settings))
	{
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
		std::snprintf(m_VariableDelimiter.data(), m_VariableDelimiter.size(), "%s", "_");
	}

	Ref<IPanel> JFMImportDialogPanel::Clone() const
	{
		return CreateRef<JFMImportDialogPanel>(*this);
	}

	void JFMImportDialogPanel::Render()
	{
		if (m_State != nullptr)
		{
			if (auto request = m_State->ConsumeOpenRequest())
			{
				loadRequest(std::move(*request));
				m_Open = true;
				m_PreviewOpen = true;
			}
		}

		if (!m_Open)
			return;

		bool open = true;
		if (!ImGui::Begin("Import Data", &open, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			renderPreviewWindow();
			return;
		}

		ImGui::Text("Files: %d", static_cast<int>(m_Request.paths.size()));
		if (!m_Request.paths.empty())
			ImGui::TextWrapped("%s", m_Request.paths.front().String().c_str());
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
			ImGui::TextDisabled("Variables: {stem}, {folder}, {parent_folder}, {T}, {stem_0}, {folder_0}");
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
						std::snprintf(m_CustomScriptPath.data(), m_CustomScriptPath.size(), "%s", script->String().c_str());
				}
				if (m_Store != nullptr)
				{
					const JFMProject project = m_Store->GetProjectSnapshot(m_Request.project);
					for (const JFMScriptInfo &script : project.scripts)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton(script.name.c_str()))
							std::snprintf(m_CustomScriptPath.data(), m_CustomScriptPath.size(), "%s", script.path.String().c_str());
					}
				}
			}

			ImGui::TableSetColumnIndex(1);
			renderVariablePreviewTable();
			ImGui::EndTable();
		}

		const std::string name = previewName();
		ImGui::Text("Preview name: %s", name.c_str());
		const std::string shortName = previewShortName();
		if (!shortName.empty())
			ImGui::Text("Preview short: %s", shortName.c_str());
		ImGui::Checkbox("Preview window", &m_PreviewOpen);
		ImGui::Checkbox("Plot after import", &m_PlotAfterImport);

		const bool validDestination = m_Request.placement != JFMImportPlacement::ExistingSeries || !m_Request.targetSeriesId.empty();
		ImGui::BeginDisabled(!validDestination);
		if (ImGui::Button("Import"))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportRequested{
					m_Request.paths,
					m_Request.targetSeriesId,
					m_Request.recursive,
					buildOptions(),
					m_PlotAfterImport,
					m_Request.project,
					InvalidJFMOperationId,
					m_Request.targetGroupId,
					m_Request.placement});
			m_Open = false;
			m_PreviewOpen = false;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_Open = false;
			m_PreviewOpen = false;
		}

		if (!open)
		{
			m_Open = false;
			m_PreviewOpen = false;
		}
		ImGui::End();
		renderPreviewWindow();
	}

	void JFMImportDialogPanel::loadRequest(JFMImportDialogRequest request)
	{
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
		m_PlotAfterImport = m_Settings != nullptr && m_Settings->GetSnapshot().autoPlotAfterImport;
		m_SelectedPreviewFileIndices.clear();
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
		std::snprintf(m_VariableDelimiter.data(), m_VariableDelimiter.size(), "%s", "_");
		refreshPreviewFiles();
	}

	void JFMImportDialogPanel::refreshPreviewFiles()
	{
		m_AllPreviewFiles.clear();
		std::set<std::string> folders;
		for (const Path &path : m_Request.paths)
		{
			if (IsDirectory(path))
				AddFilesFromDirectory(path, m_Request.recursive, m_AllPreviewFiles);
			else if ((IsRegularFile(path) || !FileSystem::Exists(path.Native())) && IsDataFile(path))
				m_AllPreviewFiles.push_back(path);
		}
		std::sort(m_AllPreviewFiles.begin(), m_AllPreviewFiles.end(), [](const Path &left, const Path &right) {
			return left.String() < right.String();
		});
		for (const Path &path : m_AllPreviewFiles)
			folders.insert(path.parent_path().String());

		m_PreviewFolders.clear();
		m_PreviewFolderLabels.clear();
		m_PreviewFolders.push_back("All folders");
		m_PreviewFolderLabels.push_back("All folders");
		m_PreviewFolders.insert(m_PreviewFolders.end(), folders.begin(), folders.end());
		for (auto it = std::next(m_PreviewFolders.begin()); it != m_PreviewFolders.end(); ++it)
			m_PreviewFolderLabels.push_back(relativeLabel(Path::FromResolved(*it)));
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
			if (folder.empty() || path.parent_path().String() == folder)
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

		ImGui::TextDisabled("Use custom variables in patterns as {variable_name}.");
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
		const std::string sampleName = path.Empty() ? std::string{"<none>"} : path.filename().String();
		ImGui::TextDisabled("Sample: %s", sampleName.c_str());
		const std::vector<NameVariablePreview> variables = BuildNameVariables(path, m_VariableDelimiter.data(), buildVariableRules());
		if (!ImGui::BeginTable("##jfm_variable_preview", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			return;

		ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 0.22f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.38f);
		ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.40f);
		ImGui::TableHeadersRow();
		for (const NameVariablePreview &variable : variables)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("{%s}", variable.name.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", variable.value.c_str());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextWrapped("%s", variable.source.c_str());
		}
		ImGui::EndTable();
	}

	void JFMImportDialogPanel::renderPreviewWindow()
	{
		if (!m_PreviewOpen)
			return;

		if (!ImGui::Begin("Import Preview", &m_PreviewOpen, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (m_PreviewFiles.empty())
		{
			ImGui::TextDisabled("No previewable data files.");
			ImGui::End();
			return;
		}

		ImGui::BeginChild("##jfm_preview_files", ImVec2(360.0f, 0.0f), true);
		ImGui::Separator();
		for (std::size_t index = 0; index < m_PreviewFiles.size(); ++index)
		{
			const int itemIndex = static_cast<int>(index);
			const bool selected = ContainsIndex(m_SelectedPreviewFileIndices, itemIndex);
			const std::string label = m_PreviewFiles[index].filename().String() + "##preview_file_" + std::to_string(index);
			if (ImGui::Selectable(label.c_str(), selected))
			{
				const bool append = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
				if (!append)
					m_SelectedPreviewFileIndices.clear();
				if (selected && append)
					m_SelectedPreviewFileIndices.erase(std::remove(m_SelectedPreviewFileIndices.begin(), m_SelectedPreviewFileIndices.end(), itemIndex), m_SelectedPreviewFileIndices.end());
				else if (!ContainsIndex(m_SelectedPreviewFileIndices, itemIndex))
					m_SelectedPreviewFileIndices.push_back(itemIndex);
				m_PreviewFileIndex = itemIndex;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", m_PreviewFiles[index].String().c_str());
		}
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##jfm_preview_plot_and_text", ImVec2(0.0f, 0.0f), false);
		if (m_PreviewFolders.size() > 1)
		{
			ImGui::SetNextItemWidth(320.0f);
			if (ImGui::BeginCombo("Folder", m_PreviewFolderLabels[static_cast<std::size_t>(m_PreviewFolderIndex)].c_str()))
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
				ImGui::EndCombo();
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
		const std::vector<Path> previewPaths = selectedPreviewPaths();
		const float plotHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y * 0.58f);
		if (!previewPaths.empty() && ImPlot::BeginPlot("Preview Plot", ImVec2(-1.0f, plotHeight)))
		{
			ImPlot::SetupAxes("x", "y");
			ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside | ImPlotLegendFlags_NoButtons);
			if (m_PreviewScale == 1)
				ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
			for (std::size_t index = 0; index < previewPaths.size(); ++index)
			{
				std::vector<double> x;
				std::vector<double> y;
				if (!buildPreviewCurve(previewPaths[index], x, y))
					continue;
				ApplyPreviewScale(m_PreviewScale, x, y);
				if (x.empty() || y.empty())
					continue;
				const ImPlotSpec spec(ImPlotProp_LineColor, PreviewColor(index));
				const std::string label = previewPaths[index].filename().String() + "##preview_plot_" + std::to_string(index);
				ImPlot::PlotLine(label.c_str(), x.data(), y.data(), static_cast<int>(std::min(x.size(), y.size())), spec);
			}
			ImPlot::EndPlot();
		}
		else
		{
			ImGui::TextDisabled("Mini plot unavailable for this file.");
		}

		const std::string preview = previewFileText();
		ImGui::SeparatorText("Text");
		ImGui::BeginChild("##jfm_import_text_preview", ImVec2(0.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(preview.empty() ? "No preview" : preview.c_str());
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::End();
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
		const std::string pattern = m_NamePattern.data();
		if (m_Request.paths.empty())
			return pattern;

		return BuildNameFromPattern(pattern, selectedPreviewPath(), m_VariableDelimiter.data(), buildVariableRules());
	}

	std::string JFMImportDialogPanel::previewShortName() const
	{
		const std::string pattern = m_ShortNamePattern.data();
		if (pattern.empty())
			return {};
		return BuildNameFromPattern(pattern, selectedPreviewPath(), m_VariableDelimiter.data(), buildVariableRules());
	}

	std::string JFMImportDialogPanel::previewFileText() const
	{
		const Path path = selectedPreviewPath();
		if (path.Empty())
			return {};

		std::string text;
		std::string error;
		if (!TextFileIO::Load(path, text, error))
			return error;

		std::istringstream stream(text);
		std::ostringstream preview;
		std::string line;
		for (int index = 0; index < 12 && std::getline(stream, line); ++index)
			preview << line << '\n';
		return preview.str();
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

	bool JFMImportDialogPanel::buildPreviewCurve(const Path &path, std::vector<double> &x, std::vector<double> &y) const
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(path, text, error))
			return false;

		std::istringstream lines(text);
		std::string line;
		const bool commaDecimal = std::string(DecimalOptions[static_cast<std::size_t>(m_DecimalSeparator)]) == ",";
		const std::string delimiter = m_Delimiter.data();
		while (x.size() < 512 && std::getline(lines, line))
		{
			if (commaDecimal)
				std::replace(line.begin(), line.end(), ',', '.');
			if (!delimiter.empty())
			{
				for (char &character : line)
				{
					if (delimiter.find(character) != std::string::npos)
						character = ' ';
				}
			}

			std::istringstream values(line);
			double first = 0.0;
			double second = 0.0;
			if (values >> first >> second)
			{
				x.push_back(first);
				y.push_back(second);
			}
		}
		return x.size() > 1 && x.size() == y.size();
	}

	Path JFMImportDialogPanel::importRoot() const
	{
		if (m_Request.paths.empty())
			return {};
		const Path first = m_Request.paths.front();
		if (IsDirectory(first))
			return first;
		return first.parent_path();
	}

	std::string JFMImportDialogPanel::relativeLabel(const Path &path) const
	{
		const Path root = importRoot();
		if (root.Empty())
			return path.String();

		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(path.Native(), root.Native(), error);
		if (error || relative.empty() || relative == ".")
			return path.filename().String().empty() ? path.String() : path.filename().String();
		return relative.string();
	}
} // namespace SST::Prototypes::JFM
