#include "Core/dspch.hpp"

#include "App/JFMCommands.hpp"

namespace SST::Prototypes::JFM
{
	namespace ProjectEditDetail
	{
		StructuredError makeEditError(std::string message, std::string details)
		{
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				std::move(message),
				std::move(details),
				"Retry the edit while the target project is open.",
				"JFMProjectEditCommand",
				"jfm.project.edit"};
		}

		class ProjectEditCommand final : public ICommand
		{
		public:
			ProjectEditCommand(Ref<JFMWorkspaceStore> workspace, JFMProjectEditRequest request)
				: m_Workspace(std::move(workspace)), m_Request(std::move(request))
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				if (m_Workspace == nullptr || m_Request.project == InvalidProjectHandle || !m_Request.apply)
					return makeEditError("Project edit failed.", "The edit command has no valid project or mutation.");
				m_Before = m_Workspace->CaptureProjectState(m_Request.project);
				if (!m_Before)
					return makeEditError("Project edit failed.", "The target project is no longer open.");
				auto result = m_Request.apply(*m_Workspace, m_Request.project);
				if (!result)
				{
					(void)m_Workspace->RestoreProjectState(m_Request.project, *m_Before);
					return result;
				}
				m_After = m_Workspace->CaptureProjectState(m_Request.project);
				m_Request.apply = {};
				if (!m_After)
					return makeEditError("Project edit failed.", "The edited project disappeared before its state could be captured.");
				if (m_Request.afterChange)
					m_Request.afterChange();
				return {};
			}

			Result<void> Undo(CommandContext &) override
			{
				if (!m_Before || !m_Workspace->RestoreProjectState(m_Request.project, *m_Before))
					return makeEditError("Undo failed.", "The target JFM project is no longer open.");
				if (m_Request.afterChange)
					m_Request.afterChange();
				return {};
			}

			Result<void> Redo(CommandContext &) override
			{
				if (!m_After || !m_Workspace->RestoreProjectState(m_Request.project, *m_After))
					return makeEditError("Redo failed.", "The target JFM project is no longer open.");
				if (m_Request.afterChange)
					m_Request.afterChange();
				return {};
			}

			std::string Description() const override
			{
				return m_Request.description.empty() ? "Edit JFM project" : m_Request.description;
			}

			bool IsUndoable() const noexcept override
			{
				return true;
			}

		private:
			Ref<JFMWorkspaceStore> m_Workspace;
			JFMProjectEditRequest m_Request;
			std::optional<JFMProjectStateSnapshot> m_Before;
			std::optional<JFMProjectStateSnapshot> m_After;
		};
	} // namespace ProjectEditDetail
	using namespace ProjectEditDetail;

	Unique<ICommand> CreateJFMProjectEditCommand(Ref<JFMWorkspaceStore> workspace, JFMProjectEditRequest request)
	{
		return CreateUnique<ProjectEditCommand>(std::move(workspace), std::move(request));
	}
} // namespace SST::Prototypes::JFM
