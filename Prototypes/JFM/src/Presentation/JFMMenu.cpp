#include "Core/dspch.hpp"

#include "Presentation/JFMMenu.hpp"

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"

namespace SST::Prototypes::JFM
{

	void execute(EditorServices &services, const char *commandId, const char *source)
	{
		auto registry = services.commandRegistry.lock();
		if (registry == nullptr)
		{
			DS_LOG_ERROR("JFM menu command runtime unavailable for '{}'", commandId);
			if (services.eventBus != nullptr)
			{
				Notification notification{
					NotificationSeverity::Error,
					NotificationCategory::UI,
					"Command unavailable",
					"JFM command runtime is not available.",
					"JFMMenu",
					Time::Now(),
					8000,
					false};
				services.eventBus->Queue(NotificationRequestedEvent{notification});
			}
			return;
		}

		CommandContext context(ContextID{"jfm.menu"});
		context.SetSource(source);
		(void)registry->Execute(CommandID{commandId}, std::move(context));
	}

	void commandMenuItem(EditorServices &services, const char *label, const char *shortcut, const char *commandId, bool enabled = true)
	{
		ImGui::BeginDisabled(!enabled);
		if (ImGui::MenuItem(label, shortcut))
			execute(services, commandId, label);
		ImGui::EndDisabled();
	}

	void panelMenuItem(EditorLayer &editorLayer, PanelId panelId, const char *label)
	{
		auto panel = editorLayer.FindPanel(panelId).lock();
		if (panel == nullptr)
			return;

		bool visible = panel->IsVisible();
		if (ImGui::MenuItem(label, nullptr, visible))
			panel->SetVisible(!visible);
	}
	

	void RenderJFMMenu(EditorLayer &editorLayer, const JFMPanelIds &panelIds, EditorServices &services)
	{
		if (ImGui::BeginMenu("File"))
		{
			commandMenuItem(services, "New Project", "Ctrl+N", "jfm.project.new");
			commandMenuItem(services, "Open Project...", "Ctrl+O", "jfm.project.open");
			ImGui::Separator();
			commandMenuItem(services, "Save Project", "Ctrl+S", "jfm.project.save");
			commandMenuItem(services, "Save Project As...", "Ctrl+Shift+S", "jfm.project.save_as");
			ImGui::Separator();
			commandMenuItem(services, "Quit", "Ctrl+Shift+W", "app.quit");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			commandMenuItem(services, "Undo", "Ctrl+Z", "edit.undo");
			commandMenuItem(services, "Redo", "Ctrl+Y", "edit.redo");
			commandMenuItem(services, "Redo", "Ctrl+Shift+Z", "edit.redo");
			ImGui::Separator();
			commandMenuItem(services, "Command Palette", "Ctrl+Shift+P", "app.command_palette");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Data"))
		{
			commandMenuItem(services, "Import Files...", "Ctrl+Shift+I", "jfm.import.files");
			commandMenuItem(services, "Import Directory...", nullptr, "jfm.import.directory");
			commandMenuItem(services, "Export Raw TSV", "Ctrl+Shift+E", "jfm.export.raw_tsv");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Analysis"))
		{
			commandMenuItem(services, "Fit", nullptr, "jfm.analysis.fit", false);
			commandMenuItem(services, "Monte Carlo", nullptr, "jfm.analysis.monte_carlo", false);
			commandMenuItem(services, "Derived Analysis", nullptr, "jfm.analysis.derived", false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			panelMenuItem(editorLayer, panelIds.projectTree, "Project Tree");
			panelMenuItem(editorLayer, panelIds.overview, "JFM Overview");
			panelMenuItem(editorLayer, panelIds.importStatus, "Import Status");
			panelMenuItem(editorLayer, panelIds.rawPlot, "Raw Plot");
			ImGui::EndMenu();
		}
	}
} // namespace SST::Prototypes::JFM
