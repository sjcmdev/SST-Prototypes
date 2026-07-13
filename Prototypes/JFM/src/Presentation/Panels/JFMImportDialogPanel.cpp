#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMImportDialogPanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Events/JFMEvents.hpp"

#include <cstdio>
#include <imgui.h>
#include <sstream>

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
				ImGui::OpenPopup("Import Data");
			}
		}

		if (!m_Open)
			return;

		bool open = true;
		if (!ImGui::BeginPopupModal("Import Data", &open, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::Text("Files: %d", static_cast<int>(m_Request.paths.size()));
		if (!m_Request.paths.empty())
			ImGui::TextWrapped("%s", m_Request.paths.front().String().c_str());

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
		if (ImGui::CollapsingHeader("File preview"))
		{
			const std::string preview = previewFileText();
			ImGui::TextUnformatted(preview.empty() ? "No preview" : preview.c_str());
		}

		if (ImGui::Button("Import"))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportRequested{m_Request.paths, m_Request.targetSeriesId, m_Request.recursive, buildOptions()});
			m_Open = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_Open = false;
			ImGui::CloseCurrentPopup();
		}

		if (!open)
			m_Open = false;
		ImGui::EndPopup();
	}

	void JFMImportDialogPanel::loadRequest(JFMImportDialogRequest request)
	{
		m_Request = std::move(request);
		m_Parser = 0;
		m_DecimalSeparator = 0;
		m_TemperatureExtraction = 0;
		m_Delimiter.fill('\0');
		m_CustomScriptPath.fill('\0');
		std::snprintf(m_NamePattern.data(), m_NamePattern.size(), "%s", "{stem}");
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

		const Path &path = m_Request.paths.front();
		std::string text = ReplaceAll(pattern, "{stem}", path.Native().stem().string());
		text = ReplaceAll(text, "{source_path}", path.String());
		return text.empty() ? path.Native().stem().string() : text;
	}

	std::string JFMImportDialogPanel::previewFileText() const
	{
		if (m_Request.paths.empty())
			return {};

		std::string text;
		std::string error;
		if (!TextFileIO::Load(m_Request.paths.front(), text, error))
			return error;

		std::istringstream stream(text);
		std::ostringstream preview;
		std::string line;
		for (int index = 0; index < 12 && std::getline(stream, line); ++index)
			preview << line << '\n';
		return preview.str();
	}
} // namespace SST::Prototypes::JFM
