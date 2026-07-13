#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMImportDialogPanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <implot.h>
#include <sstream>
#include <set>
#include <system_error>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr NativeFileDialogFilter PythonScriptFilter{"Python", "py"};

		const char *ParserOptions[] = {"auto", "dat", "scaps_iv", "custom"};
		const char *DecimalOptions[] = {"auto", ".", ","};
		const char *TemperatureOptions[] = {"auto", "filename", "header", "off"};

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

		std::string BuildNameFromPattern(const std::string &pattern, const Path &path)
		{
			if (path.Empty())
				return pattern;
			std::string text = ReplaceAll(pattern, "{stem}", path.Native().stem().string());
			text = ReplaceAll(text, "{source_path}", path.String());
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
				if (scale == 1)
				{
					const double value = std::abs(y[index]);
					if (value <= 0.0)
						continue;
					nextX.push_back(x[index]);
					nextY.push_back(value);
				}
				else
				{
					const double sign = y[index] < 0.0 ? -1.0 : 1.0;
					nextX.push_back(x[index]);
					nextY.push_back(sign * std::log10(1.0 + std::abs(y[index])));
				}
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

	JFMImportDialogPanel::JFMImportDialogPanel(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMImportDialogState> state)
		: IPanel("Import Dialog", false), m_EventBus(std::move(eventBus)), m_Store(std::move(store)), m_State(std::move(state))
	{
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
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

		ImGui::Combo("Parser", &m_Parser, ParserOptions, IM_ARRAYSIZE(ParserOptions));
		ImGui::InputText("Name pattern", m_NamePattern.data(), m_NamePattern.size());
		ImGui::InputText("Short name", m_ShortNamePattern.data(), m_ShortNamePattern.size());
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
				const JFMProject project = m_Store->GetProjectSnapshot();
				for (const JFMScriptInfo &script : project.scripts)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton(script.name.c_str()))
						std::snprintf(m_CustomScriptPath.data(), m_CustomScriptPath.size(), "%s", script.path.String().c_str());
				}
			}
		}

		const std::string name = previewName();
		ImGui::Text("Preview name: %s", name.c_str());
		const std::string shortName = previewShortName();
		if (!shortName.empty())
			ImGui::Text("Preview short: %s", shortName.c_str());
		ImGui::Checkbox("Preview window", &m_PreviewOpen);

		if (ImGui::Button("Import"))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportRequested{m_Request.paths, m_Request.targetSeriesId, m_Request.recursive, buildOptions()});
			m_Open = false;
			m_PreviewOpen = false;
		}
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
		m_Parser = 0;
		m_DecimalSeparator = 0;
		m_TemperatureExtraction = 0;
		m_Delimiter.fill('\0');
		m_ShortNamePattern.fill('\0');
		m_CustomScriptPath.fill('\0');
		m_PreviewFileIndex = 0;
		m_PreviewScale = 0;
		m_PreviewFolderIndex = 0;
		m_SelectedPreviewFileIndices.clear();
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
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
		m_PreviewFolders.push_back("All folders");
		m_PreviewFolders.insert(m_PreviewFolders.end(), folders.begin(), folders.end());
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
		if (m_PreviewFolders.size() > 1)
		{
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##preview_folder", m_PreviewFolders[static_cast<std::size_t>(m_PreviewFolderIndex)].c_str()))
			{
				for (std::size_t index = 0; index < m_PreviewFolders.size(); ++index)
				{
					const bool selected = static_cast<int>(index) == m_PreviewFolderIndex;
					if (ImGui::Selectable(m_PreviewFolders[index].c_str(), selected))
					{
						m_PreviewFolderIndex = static_cast<int>(index);
						m_SelectedPreviewFileIndices.clear();
						applyPreviewFolderFilter();
					}
				}
				ImGui::EndCombo();
			}
		}
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
		ImGui::RadioButton("Linear", &m_PreviewScale, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Semi-log", &m_PreviewScale, 1);
		ImGui::SameLine();
		ImGui::RadioButton("Signed-log", &m_PreviewScale, 2);
		const std::vector<Path> previewPaths = selectedPreviewPaths();
		const float plotHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y * 0.58f);
		if (!previewPaths.empty() && ImPlot::BeginPlot("Preview Plot", ImVec2(-1.0f, plotHeight)))
		{
			ImPlot::SetupAxes("x", m_PreviewScale == 2 ? "signed log y" : "y");
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
		options.delimiter = m_Delimiter.data();
		options.decimalSeparator = DecimalOptions[static_cast<std::size_t>(m_DecimalSeparator)];
		options.temperatureExtraction = TemperatureOptions[static_cast<std::size_t>(m_TemperatureExtraction)];
		options.customScriptPath = m_CustomScriptPath.data();
		return options;
	}

	std::string JFMImportDialogPanel::previewName() const
	{
		const std::string pattern = m_NamePattern.data();
		if (m_Request.paths.empty())
			return pattern;

		return BuildNameFromPattern(pattern, selectedPreviewPath());
	}

	std::string JFMImportDialogPanel::previewShortName() const
	{
		const std::string pattern = m_ShortNamePattern.data();
		if (pattern.empty())
			return {};
		return BuildNameFromPattern(pattern, selectedPreviewPath());
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
} // namespace SST::Prototypes::JFM
