#include "Core/dspch.hpp"

#include "Presentation/JFMMenu.hpp"

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"

namespace SST::Prototypes::JFM
{
	void execute(EditorServices &services, const char *commandId, const char *source, CommandContext context);

	void execute(EditorServices &services, const char *commandId, const char *source)
	{
		CommandContext context(ContextID{"jfm.menu"});
		context.SetSource(source);
		execute(services, commandId, source, std::move(context));
	}

	void execute(EditorServices &services, const char *commandId, const char *source, CommandContext context)
	{
		(void)source;
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

	void recentProjectMenuItem(EditorServices &services, const Path &path)
	{
		CommandContext context(ContextID{"jfm.menu.recent_projects"});
		context.SetSource("Last Projects");
		context.Set("path", path);
		if (ImGui::MenuItem(path.filename().String().c_str()))
			execute(services, "jfm.project.open", "Last Projects", std::move(context));
	}

	void RenderJFMMenu(
		EditorLayer &editorLayer,
		const JFMPanelIds &panelIds,
		Ref<JFMWorkspaceStore> workspace,
		Ref<JFMUserSettingsStore> settings,
		EditorServices &services)
	{
		if (ImGui::BeginMenu("File"))
		{
			commandMenuItem(services, "New Project", "Ctrl+N", "jfm.project.new");
			commandMenuItem(services, "Open New Project Window", nullptr, "jfm.project.open_new_window");
			commandMenuItem(services, "Open Project...", "Ctrl+O", "jfm.project.open");
			if (ImGui::BeginMenu("Last Projects"))
			{
				const std::vector<Path> recent = settings != nullptr ? settings->GetRecentProjects() : std::vector<Path>{};
				for (const Path &path : recent)
					recentProjectMenuItem(services, path);
				if (recent.empty())
					ImGui::MenuItem("No recent projects", nullptr, false, false);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			commandMenuItem(services, "Save Project", "Ctrl+S", "jfm.project.save");
			commandMenuItem(services, "Save Project As...", "Ctrl+Shift+S", "jfm.project.save_as");
			ImGui::Separator();
			if (ImGui::BeginMenu("Import"))
			{
				commandMenuItem(services, "Files...", "Ctrl+Shift+I", "jfm.import.files");
				commandMenuItem(services, "Folder as Series...", "Ctrl+Shift+D", "jfm.import.directory");
				commandMenuItem(services, "Folders as Series...", "Ctrl+Alt+Shift+D", "jfm.import.directories");
				commandMenuItem(services, "Folder Trees...", "Ctrl+Alt+Shift+R", "jfm.import.directories_recursive");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Export"))
			{
				commandMenuItem(services, "Selected Raw TSV", "Ctrl+Shift+E", "jfm.export.raw_tsv");
				commandMenuItem(services, "Selected Series Raw TSV", nullptr, "jfm.export.series_raw_tsv");
				ImGui::BeginDisabled(workspace == nullptr || workspace->GetProjectSnapshot().groups.empty());
				commandMenuItem(services, "Selected Group Raw TSV", nullptr, "jfm.export.group_raw_tsv", !workspace->GetSelectedGroupId().empty());
				ImGui::EndDisabled();
				ImGui::EndMenu();
			}
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
			panelMenuItem(editorLayer, panelIds.details, "Characteristic Details");
			panelMenuItem(editorLayer, panelIds.control, "Control Panel");
			panelMenuItem(editorLayer, panelIds.monteCarloSettings, "MC Settings");
			panelMenuItem(editorLayer, panelIds.monteCarloScatter, "MC Scatter");
			panelMenuItem(editorLayer, panelIds.monteCarloCurves, "MC Curves");
			ImGui::Separator();
			editorLayer.RenderCorePanelMenuItems();
			ImGui::EndMenu();
		}
	}
} // namespace SST::Prototypes::JFM
