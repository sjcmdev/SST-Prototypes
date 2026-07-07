#include "Core/dspch.hpp"

#include "IO/IOLayer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Serialization/YamlCodecFacade.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeyBindingEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Events/EditorUiEvents.hpp"
#include "IO/TextFileIO.hpp"

namespace SST
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeIOLayer(
			EventBus &bus,
			IOLayer &layer,
			void (IOLayer::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer));
		}

		std::vector<Path> listFilesByExtension(const Path &directory, std::string_view extension)
		{
			std::vector<Path> result;
			std::error_code error;
			if (!std::filesystem::exists(directory.Native(), error) || error)
				return result;

			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
			{
				if (error)
					break;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				std::string fileExtension = entry.path().extension().string();
				std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), [](unsigned char character) {
					return static_cast<char>(std::tolower(character));
				});
				if (fileExtension == extension)
					result.push_back(Path::FromResolved(entry.path()));
			}

			std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
				return left.String() < right.String();
			});
			return result;
		}

		bool serializeBindings(const std::vector<KeyBinding> &bindings, std::string &text, std::string &error)
		{
			try
			{
				YAML::Emitter out;
				out << YAML::BeginMap;
				out << YAML::Key << "bindings" << YAML::Value << YAML::BeginSeq;
				for (const KeyBinding &binding : bindings)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "id" << YAML::Value << binding.id;
					out << YAML::Key << "chord" << YAML::Value << ToString(binding.chord);
					out << YAML::Key << "command" << YAML::Value << binding.commandId.value;
					out << YAML::Key << "context" << YAML::Value << binding.when.GetExpression();
					out << YAML::Key << "layer" << YAML::Value << static_cast<int>(binding.layer);
					out << YAML::Key << "enabled" << YAML::Value << binding.enabled;
					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
				out << YAML::EndMap;
				text = out.c_str();
				return true;
			}
			catch (const std::exception &exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool deserializeBindings(std::string_view text, std::vector<KeyBinding> &bindings, std::string &error)
		{
			try
			{
				YAML::Node root = YAML::Load(std::string(text));
				YAML::Node list = root["bindings"];
				if (!list || !list.IsSequence())
				{
					error = "Keybindings YAML has no 'bindings' sequence";
					return false;
				}

				bindings.clear();
				for (const YAML::Node &node : list)
				{
					const std::string id = node["id"].as<std::string>("");
					const std::string chordText = node["chord"].as<std::string>("");
					const std::string command = node["command"].as<std::string>("");
					if (id.empty() || chordText.empty() || command.empty())
						continue;

					auto chord = ParseKeyChord(chordText);
					if (!chord)
					{
						error = "Invalid chord: " + chordText;
						return false;
					}

					KeyBinding binding;
					binding.id = id;
					binding.chord = *chord;
					binding.commandId = CommandID{command};
					binding.when = ContextExpr{node["context"].as<std::string>("")};
					binding.layer = static_cast<KeymapLayer>(node["layer"].as<int>(static_cast<int>(KeymapLayer::Global)));
					binding.enabled = node["enabled"].as<bool>(true);
					bindings.push_back(std::move(binding));
				}
				return true;
			}
			catch (const std::exception &exception)
			{
				error = exception.what();
				return false;
			}
		}
	} // namespace

	IOLayer::IOLayer(Ref<EventBus> eventBus)
		: Layer("IOLayer"), m_EventBus(std::move(eventBus))
	{
		bindEvents();
	}

	void IOLayer::OnDetach()
	{
		ClearSubscriptions();
		m_EventBus.reset();
	}

	void IOLayer::bindEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(subscribeIOLayer<AppEvents::Config::PersistRequested>(*m_EventBus, *this, &IOLayer::onConfigPersistRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::PersistRequested>(*m_EventBus, *this, &IOLayer::onUiPersistRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::ThemeSaveRequested>(*m_EventBus, *this, &IOLayer::onThemeSaveRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::ThemeLoadRequested>(*m_EventBus, *this, &IOLayer::onThemeLoadRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::LayoutLoadRequested>(*m_EventBus, *this, &IOLayer::onLayoutLoadRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::LayoutListRequested>(*m_EventBus, *this, &IOLayer::onLayoutListRequested));
		AddSubscription(subscribeIOLayer<AppEvents::Keymap::BindingsSaveRequested>(*m_EventBus, *this, &IOLayer::onBindingsSaveRequested));
		AddSubscription(subscribeIOLayer<AppEvents::Keymap::BindingsLoadRequested>(*m_EventBus, *this, &IOLayer::onBindingsLoadRequested));
	}

	void IOLayer::onConfigPersistRequested(const AppEvents::Config::PersistRequested &event)
	{
		using namespace AppEvents::Config;

		std::string error;
		if (!TextFileIO::Save(event.path, event.contents, error))
		{
			if (event.kind == PersistKind::UserSettings)
				m_EventBus->Queue(UserSaveFailed{event.config, error});
			else
				m_EventBus->Queue(DefaultsSaveFailed{event.config, error});
			return;
		}

		if (event.kind == PersistKind::UserSettings)
			m_EventBus->Queue(UserSaved{event.config});
		else
			m_EventBus->Queue(DefaultsSaved{event.config});
	}

	void IOLayer::onUiPersistRequested(const EditorUiEvents::PersistRequested &event)
	{
		using namespace EditorUiEvents;

		std::string error;
		if (!TextFileIO::Save(event.path, event.contents, error))
		{
			if (event.kind == PersistKind::Theme)
				m_EventBus->Queue(ThemeSaveFailed{event.path, error});
			else
				m_EventBus->Queue(LayoutSaveFailed{event.path, error});
			return;
		}

		if (event.kind == PersistKind::Theme)
			m_EventBus->Queue(ThemeSaved{event.path});
		else
			m_EventBus->Queue(LayoutSaved{event.path, event.contents.size()});
	}

	void IOLayer::onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event)
	{
		std::string contents;
		std::string error;
		if (!YamlCodecFacade::Default().SerializeAppearanceTheme(event.appearance, contents, error))
		{
			m_EventBus->Queue(EditorUiEvents::ThemeSaveFailed{event.path, error});
			return;
		}
		if (!TextFileIO::Save(event.path, contents, error))
		{
			m_EventBus->Queue(EditorUiEvents::ThemeSaveFailed{event.path, error});
			return;
		}
		m_EventBus->Queue(EditorUiEvents::ThemeSaved{event.path});
	}

	void IOLayer::onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event)
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(event.path, text, error))
		{
			m_EventBus->Queue(EditorUiEvents::ThemeLoadFailed{event.path, error});
			return;
		}

		AppearanceConfig appearance;
		if (!YamlCodecFacade::Default().DeserializeAppearanceTheme(text, appearance, error))
		{
			m_EventBus->Queue(EditorUiEvents::ThemeLoadFailed{event.path, error});
			return;
		}

		m_EventBus->Queue(EditorUiEvents::ThemeLoaded{event.path, std::move(appearance)});
	}

	void IOLayer::onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event)
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(event.path, text, error))
		{
			m_EventBus->Queue(EditorUiEvents::LayoutLoadFailed{event.path, error});
			return;
		}
		m_EventBus->Queue(EditorUiEvents::LayoutLoaded{event.path, std::move(text)});
	}

	void IOLayer::onLayoutListRequested(const EditorUiEvents::LayoutListRequested &event)
	{
		std::error_code error;
		if (!std::filesystem::exists(event.directory.Native(), error) || error)
		{
			m_EventBus->Queue(EditorUiEvents::LayoutListLoaded{event.directory, {}});
			return;
		}
		if (!std::filesystem::is_directory(event.directory.Native(), error) || error)
		{
			m_EventBus->Queue(EditorUiEvents::LayoutListFailed{event.directory, "Layout path is not a directory"});
			return;
		}
		m_EventBus->Queue(EditorUiEvents::LayoutListLoaded{event.directory, listFilesByExtension(event.directory, ".ini")});
	}

	void IOLayer::onBindingsSaveRequested(const AppEvents::Keymap::BindingsSaveRequested &event)
	{
		std::string contents;
		std::string error;
		if (!serializeBindings(event.bindings, contents, error) || !TextFileIO::Save(event.targetPath, contents, error))
		{
			m_EventBus->Queue(AppEvents::Keymap::BindingsSaveFailed{error});
			return;
		}
		m_EventBus->Queue(AppEvents::Keymap::BindingsSaved{event.targetPath});
	}

	void IOLayer::onBindingsLoadRequested(const AppEvents::Keymap::BindingsLoadRequested &event)
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(event.sourcePath, text, error))
		{
			DS_LOG_WARN("Keybindings file not loaded [{}]: {}", event.sourcePath.String(), error);
			m_EventBus->Queue(AppEvents::Keymap::BindingsLoadFailed{error});
			return;
		}

		std::vector<KeyBinding> bindings;
		if (!deserializeBindings(text, bindings, error))
		{
			m_EventBus->Queue(AppEvents::Keymap::BindingsLoadFailed{error});
			return;
		}
		m_EventBus->Queue(AppEvents::Keymap::BindingsLoaded{std::move(bindings)});
	}
} // namespace SST
