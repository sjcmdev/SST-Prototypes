#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMImportDialogPanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <implot.h>
#include <sstream>
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
		m_CustomScriptPath.fill('\0');
		m_PreviewFileIndex = 0;
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
		refreshPreviewFiles();
	}

	void JFMImportDialogPanel::refreshPreviewFiles()
	{
		m_PreviewFiles.clear();
		for (const Path &path : m_Request.paths)
		{
			if (IsDirectory(path))
				AddFilesFromDirectory(path, m_Request.recursive, m_PreviewFiles);
			else if ((IsRegularFile(path) || !FileSystem::Exists(path.Native())) && IsDataFile(path))
				m_PreviewFiles.push_back(path);
		}
		std::sort(m_PreviewFiles.begin(), m_PreviewFiles.end(), [](const Path &left, const Path &right) {
			return left.String() < right.String();
		});
		if (m_PreviewFileIndex >= static_cast<int>(m_PreviewFiles.size()))
			m_PreviewFileIndex = 0;
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

		ImGui::SetNextItemWidth(360.0f);
		if (ImGui::BeginCombo("File", m_PreviewFiles[static_cast<std::size_t>(m_PreviewFileIndex)].filename().String().c_str()))
		{
			for (std::size_t index = 0; index < m_PreviewFiles.size(); ++index)
			{
				const bool selected = static_cast<int>(index) == m_PreviewFileIndex;
				if (ImGui::Selectable(m_PreviewFiles[index].String().c_str(), selected))
					m_PreviewFileIndex = static_cast<int>(index);
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		std::vector<double> x;
		std::vector<double> y;
		if (buildPreviewCurve(x, y) && ImPlot::BeginPlot("Mini Plot", ImVec2(460.0f, 220.0f)))
		{
			ImPlot::SetupAxes("x", "y");
			ImPlot::PlotLine("preview", x.data(), y.data(), static_cast<int>(std::min(x.size(), y.size())));
			ImPlot::EndPlot();
		}
		else
		{
			ImGui::TextDisabled("Mini plot unavailable for this file.");
		}

		const std::string preview = previewFileText();
		ImGui::SeparatorText("Text");
		ImGui::BeginChild("##jfm_import_text_preview", ImVec2(640.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(preview.empty() ? "No preview" : preview.c_str());
		ImGui::EndChild();
		ImGui::End();
	}

	JFMImportOptions JFMImportDialogPanel::buildOptions() const
	{
		JFMImportOptions options;
		options.parser = ParserOptions[static_cast<std::size_t>(m_Parser)];
		options.namePattern = m_NamePattern.data();
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

		const Path path = selectedPreviewPath();
		std::string text = ReplaceAll(pattern, "{stem}", path.Native().stem().string());
		text = ReplaceAll(text, "{source_path}", path.String());
		return text.empty() ? path.Native().stem().string() : text;
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

	bool JFMImportDialogPanel::buildPreviewCurve(std::vector<double> &x, std::vector<double> &y) const
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(selectedPreviewPath(), text, error))
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
