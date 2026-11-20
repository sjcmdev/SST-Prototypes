#pragma once

#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <thread>

namespace SST
{
    class ViewInstance;
    struct ViewTemplate
    {
        std::string id, name;
        // Items retain stable application IDs; the application owns their data and units.
        std::function<void(ViewInstance &, const nlohmann::json &items)> render;
    };

    class ViewHost : public std::enable_shared_from_this<ViewHost>
    {
    public:
        ViewHost(PanelRegistry &panels, Ref<WindowWorkspace> workspace);
        Result<void> Bind(Ref<CommandRegistry> commands, Ref<JobSystem> jobs);
        Result<void> RegisterTemplate(ViewTemplate view);
        // Shared GUI/Python command -> validation job -> main-thread workspace path.
        [[nodiscard]] nlohmann::json Execute(const nlohmann::json &request);
        [[nodiscard]] Ref<ViewInstance> Find(WindowId id) const;
        [[nodiscard]] nlohmann::json Items(const ViewInstance &view, bool overlays = true) const;
        [[nodiscard]] const nlohmann::json &Metrics() const { return m_Metrics; }
        [[nodiscard]] const std::map<std::string, ViewTemplate> &Templates() const { return m_Templates; }
        void Render(ViewInstance &view);
        void RenderMenu();

    private:
        class OperationCommand;
        nlohmann::json Apply(const nlohmann::json &request);
        PanelRegistry &m_Panels;
        Ref<WindowWorkspace> m_Workspace;
        WeakRef<CommandRegistry> m_Commands;
        WeakRef<JobSystem> m_Jobs;
        std::thread::id m_Thread = std::this_thread::get_id();
        std::map<std::string, ViewTemplate> m_Templates;
        nlohmann::json m_Items = nlohmann::json::array(), m_Metrics = nlohmann::json::array();
        std::string m_ActiveItem;
    };

    class ViewInstance final : public EditorWindow
    {
    public:
        ViewInstance(WeakRef<ViewHost> host, std::string templateId, std::string name);
        WindowDuplicate Duplicate() const override;
        [[nodiscard]] const std::string &TemplateId() const { return m_TemplateId; }
        [[nodiscard]] const nlohmann::json &Binding() const { return m_Binding; }
        [[nodiscard]] const nlohmann::json &Table() const { return m_Table; }
    protected:
        void RenderContent() override;
        std::string SaveViewState() const override;
        bool LoadViewState(const std::string &text) override;
    private:
        friend class ViewHost;
        WeakRef<ViewHost> m_Host;
        std::string m_TemplateId;
        nlohmann::json m_Binding{{"mode", "follow_active"}, {"item_ids", nlohmann::json::array()}, {"selection_source", 0}};
        nlohmann::json m_Table{{"filter_text", ""}, {"sort_metric_id", ""}, {"descending", false}, {"selected_item_ids", nullptr}};
    };

    void RenderComparisonTable(ViewHost &host, ViewInstance &view);
    void RegisterViewPythonModule();
    namespace ViewDetail { void BindPythonHost(WeakRef<ViewHost> host); }
}
