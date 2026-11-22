#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewHost.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <future>
#include <set>

namespace SST
{
    using Json = nlohmann::json;
    namespace
    {
        StructuredError viewError(const std::string &code, const std::string &details)
        {
            return {ErrorCategory::Validation, Severity::Error, "View operation failed.", details,
                "Check the view request and registered item IDs.", "ViewHost", code};
        }
        Json failure(const StructuredError &error)
        {
            return {{"ok", false}, {"error", {{"code", error.code}, {"message", error.technicalDetails}}}};
        }
        void require(bool valid, const char *message)
        {
            if (!valid) throw std::invalid_argument(message);
        }
        void identifier(const std::string &id)
        {
            require(!id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':';
            }), "Invalid stable ID");
        }
        void ids(const Json &value)
        {
            require(value.is_array(), "item_ids must be an array");
            std::set<std::string> unique;
            for (const auto &item : value)
            {
                const auto id = item.get<std::string>(); identifier(id);
                require(unique.insert(id).second, "Duplicate item ID");
            }
        }
        WindowId windowId(const Json &value, bool allowZero = false)
        {
            require(value.is_number_integer(), "Window ID must be an integer");
            const auto id = value.get<std::int64_t>();
            require(id > 0 || (allowZero && id == 0), "Invalid window ID");
            return WindowId(id);
        }
        Json binding(const Json &value)
        {
            const auto mode = value.at("mode").get<std::string>();
            require(mode == "follow_active" || mode == "pinned" || mode == "explicit", "Unknown binding mode");
            auto items = value.value("item_ids", Json::array()); ids(items);
            require(mode != "pinned" || items.size() == 1, "Pinned binding requires one item");
            require(mode != "follow_active" || items.empty(), "Follow-active binding cannot contain items");
            return {{"mode", mode}, {"item_ids", items}, {"selection_source", windowId(value.value("selection_source", Json(0)), true)}};
        }
        Json table(const Json &value)
        {
            auto selected = value.value("selected_item_ids", Json());
            if (!selected.is_null()) ids(selected);
            return {{"filter_text", value.value("filter_text", std::string{})},
                {"sort_metric_id", value.value("sort_metric_id", std::string{})},
                {"descending", value.value("descending", false)}, {"selected_item_ids", selected}};
        }
        void validate(const Json &request)
        {
            const auto operation = request.at("operation").get<std::string>();
            if (operation == "open")
            {
                identifier(request.at("template_id").get<std::string>());
                (void)binding(request.value("binding", Json{{"mode", "follow_active"}}));
            }
            else if (operation == "bind") (void)binding(request.at("binding"));
            else if (operation == "table") (void)table(request.at("table"));
            else if (operation == "active")
            {
                const auto id = request.at("item_id").get<std::string>();
                if (!id.empty()) identifier(id);
            }
            else if (operation == "catalog")
            {
                const auto &metrics = request.at("metrics"), &items = request.at("items");
                require(metrics.is_array() && items.is_array(), "metrics and items must be arrays");
                require(metrics.size() <= 63, "Comparison tables support at most 63 metrics");
                std::set<std::string> metricIds, itemIds;
                for (const auto &metric : metrics)
                {
                    auto id = metric.at("id").get<std::string>(); identifier(id);
                    require(metricIds.insert(id).second, "Duplicate metric ID");
                    require(!metric.at("name").get<std::string>().empty(), "Metric name is empty");
                    (void)metric.at("unit").get<std::string>();
                }
                for (const auto &item : items)
                {
                    auto id = item.at("id").get<std::string>(); identifier(id);
                    require(itemIds.insert(id).second, "Duplicate item ID");
                    require(!item.at("name").get<std::string>().empty(), "Item name is empty");
                    const auto &values = item.at("metrics");
                    require(values.is_object(), "Item metrics must be an object");
                    for (auto it = values.begin(); it != values.end(); ++it)
                    {
                        require(metricIds.contains(it.key()), "Unknown metric ID");
                        require(it->is_null() || (it->is_number() && std::isfinite(it->get<double>())), "Metric must be finite or null");
                    }
                }
            }
            else require(operation == "list" || operation == "rows" || operation == "save" || operation == "load" ||
                operation == "close" || operation == "activate" || operation == "duplicate", "Unknown view operation");
            if (operation == "bind" || operation == "table" || operation == "rows" || operation == "close" || operation == "activate" || operation == "duplicate")
                (void)windowId(request.at("instance_id"));
            if (operation == "load") require(request.at("layout").is_string(), "layout must be a workspace JSON string");
        }
        // Validation runs on an immutable request; only the owner thread changes windows.
        class RequestJob final : public IJob
        {
        public:
            explicit RequestJob(Json request) : request(std::move(request)) {}
            Json request;
            std::promise<Result<void>> completion;
            std::string GetName() const override { return "Validate view request"; }
            std::string GetType() const override { return "view.request"; }
            void Execute(JobContext &) override
            {
                try { validate(request); completion.set_value(Result<void>{}); }
                catch (const std::exception &error) { completion.set_value(viewError("view.invalid", error.what())); }
            }
        };
        std::string folded(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }
    }

    class ViewHost::OperationCommand final : public ICommand
    {
    public:
        explicit OperationCommand(WeakRef<ViewHost> host) : host(std::move(host)) {}
        std::string Description() const override { return "View operation"; }
        Result<void> Execute(CommandContext &context) override
        {
            auto owner = host.lock();
            auto request = context.TryGet<Json>("view.request");
            auto output = context.TryGet<Ref<Json>>("view.response");
            if (!owner || !request || !output || owner->m_Thread != std::this_thread::get_id())
                return viewError("view.unavailable", "View host and its owner thread are required.");
            auto jobs = owner->m_Jobs.lock();
            if (!jobs) return viewError("view.unavailable", "Job system is unavailable.");
            auto job = CreateRef<RequestJob>(*request);
            auto completion = job->completion.get_future();
            const auto jobId = jobs->Submit(job);
            if (!jobId) return viewError("view.job_rejected", "View validation job was rejected.");
            // shortcut: validation waits at most 5 s; use async completion if large catalogs make this noticeable.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (completion.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                const auto status = jobs->GetJob(jobId);
                if (!status || status->status == JobStatus::Cancelled || status->status == JobStatus::Failed ||
                    std::chrono::steady_clock::now() >= deadline)
                {
                    (void)jobs->RequestCancel(jobId);
                    return viewError("view.job_unavailable", "View validation did not complete; no view state was changed.");
                }
            }
            auto checked = completion.get();
            if (!checked) return checked;
            try { **output = owner->Apply(*request); }
            catch (const std::exception &error) { return viewError("view.invalid", error.what()); }
            if (!(**output).at("ok").get<bool>())
                return viewError((**output).at("error").at("code").get<std::string>(), (**output).at("error").at("message").get<std::string>());
            return {};
        }
    private:
        WeakRef<ViewHost> host;
    };

    ViewHost::ViewHost(PanelRegistry &panels, Ref<WindowWorkspace> workspace)
        : m_Panels(panels), m_Workspace(std::move(workspace)) {}

    Result<void> ViewHost::Bind(Ref<CommandRegistry> commands, Ref<JobSystem> jobs)
    {
        if (!commands || !jobs) return viewError("view.unavailable", "Commands and jobs are required.");
        auto registered = commands->Register({CommandID{"view.execute"}, "View operation", "Views", {}, {}, CommandFlags::HiddenFromPalette},
            [weak = weak_from_this()](CommandContext &) { return CreateUnique<OperationCommand>(weak); });
        if (!registered) return registered;
        m_Commands = commands; m_Jobs = jobs;
        ViewDetail::BindPythonHost(weak_from_this());
        return RegisterTemplate({"core.comparison", "Comparison", {}});
    }
    Result<void> ViewHost::RegisterTemplate(ViewTemplate view)
    {
        try
        {
            identifier(view.id);
            require(!view.name.empty() && view.name.size() < 256 && view.name.find('#') == std::string::npos &&
                std::none_of(view.name.begin(), view.name.end(), [](unsigned char c) { return c < 32; }), "Invalid template name");
            require(!m_Templates.contains(view.id), "Template is already registered");
            const auto id = view.id, name = view.name;
            m_Workspace->RegisterFactory("view." + id, [weak = weak_from_this(), id, name] {
                return CreateRef<ViewInstance>(weak, id, name);
            });
            m_Templates.emplace(id, std::move(view));
            return {};
        }
        catch (const std::exception &error) { return viewError("view.invalid_template", error.what()); }
    }
    Json ViewHost::Execute(const Json &request)
    {
        if (m_Thread != std::this_thread::get_id()) return failure(viewError("view.wrong_thread", "Execute on the view host's owner thread."));
        auto commands = m_Commands.lock();
        if (!commands) return failure(viewError("view.unavailable", "View commands are unavailable."));
        auto output = CreateRef<Json>();
        CommandContext context; context.Set("view.request", request); context.Set("view.response", output);
        auto result = commands->Execute(CommandID{"view.execute"}, std::move(context));
        return result ? *output : failure(result.Error());
    }
    Ref<ViewInstance> ViewHost::Find(WindowId id) const
    {
        auto view = std::dynamic_pointer_cast<ViewInstance>(m_Workspace->Find(id));
        return view && view->m_Host.lock().get() == this ? view : nullptr;
    }
    Json ViewHost::Items(const ViewInstance &view, bool overlays) const
    {
        const auto &b = view.Binding();
        auto result = Json::array();
        const auto sourceId = b.at("selection_source").get<WindowId>();
        auto source = sourceId ? Find(sourceId) : nullptr;
        std::set<std::string> selectedIds;
        if (overlays && source)
        {
            const auto &selected = source->Table().at("selected_item_ids");
            for (const auto &row : Items(*source, false))
            {
                const auto id = row.at("id").get<std::string>();
                if (selected.is_null() || std::find(selected.begin(), selected.end(), id) != selected.end()) selectedIds.insert(id);
            }
        }
        for (const auto &item : m_Items)
        {
            const auto id = item.at("id").get<std::string>();
            const bool bound = b.at("mode") == "follow_active" ? id == m_ActiveItem :
                std::find(b.at("item_ids").begin(), b.at("item_ids").end(), id) != b.at("item_ids").end();
            if (!bound) continue;
            if (overlays && sourceId)
            {
                if (!selectedIds.contains(id)) continue;
            }
            if (!overlays)
            {
                const auto filter = folded(view.Table().at("filter_text").get<std::string>());
                if (folded(item.at("name").get<std::string>() + " " + id).find(filter) == std::string::npos) continue;
            }
            result.push_back(item);
        }
        if (!overlays)
        {
            const auto metric = view.Table().at("sort_metric_id").get<std::string>();
            const bool descending = view.Table().at("descending").get<bool>();
            std::stable_sort(result.begin(), result.end(), [&](const Json &a, const Json &b) {
                if (metric.empty()) return descending ? a.at("name") > b.at("name") : a.at("name") < b.at("name");
                const auto av = a.at("metrics").value(metric, Json()), bv = b.at("metrics").value(metric, Json());
                if (av.is_null() || bv.is_null()) return !av.is_null() && bv.is_null();
                return descending ? av.get<double>() > bv.get<double>() : av.get<double>() < bv.get<double>();
            });
        }
        return result;
    }
    Json ViewHost::Apply(const Json &request)
    {
        const auto operation = request.at("operation").get<std::string>();
        Json response{{"ok", true}};
        auto checkBinding = [&](const Json &value, WindowId target) {
            const auto source = value.at("selection_source").get<WindowId>();
            auto tableView = source ? Find(source) : nullptr;
            require(!source || (source != target && tableView && tableView->TemplateId() == "core.comparison"), "Invalid comparison selection source");
            require(!source || value.at("mode") == "explicit", "Overlay selection requires an explicit binding");
        };
        if (operation == "catalog")
        {
            m_Metrics = request.at("metrics"); m_Items = request.at("items");
        }
        else if (operation == "active")
        {
            const auto id = request.at("item_id").get<std::string>();
            require(id.empty() || std::any_of(m_Items.begin(), m_Items.end(), [&](const auto &item) { return item.at("id") == id; }), "Unknown active item");
            m_ActiveItem = id;
        }
        else if (operation == "open")
        {
            const auto id = request.at("template_id").get<std::string>();
            require(m_Templates.contains(id), "Unknown view template");
            auto b = binding(request.value("binding", Json{{"mode", "follow_active"}})); checkBinding(b, 0);
            require(id != "core.comparison" || b.at("selection_source") == 0, "Comparison tables cannot consume selection");
            auto instance = CreateRef<ViewInstance>(weak_from_this(), id, m_Templates.at(id).name);
            instance->m_Binding = std::move(b);
            response["instance_id"] = m_Panels.AddExisting(instance);
        }
        else if (operation == "list")
        {
            response["templates"] = Json::array(); response["instances"] = Json::array();
            for (const auto &[id, view] : m_Templates) response["templates"].push_back({{"id", id}, {"name", view.name}});
            for (const auto &window : m_Workspace->Windows()) if (auto view = Find(window->GetId()))
                response["instances"].push_back({{"instance_id", view->GetId()}, {"template_id", view->TemplateId()},
                    {"binding", view->Binding()}, {"table", view->Table()}, {"visible", view->IsVisible()}});
        }
        else if (operation == "save") response["layout"] = m_Workspace->Save();
        else if (operation == "load")
        {
            const auto result = m_Workspace->Load(request.at("layout").get<std::string>());
            if (!result) return failure(result.Error());
        }
        else
        {
            const auto id = windowId(request.at("instance_id")); auto view = Find(id);
            require(bool(view), "Unknown view instance");
            if (operation == "bind")
            {
                auto b = binding(request.at("binding")); checkBinding(b, id);
                require(view->TemplateId() != "core.comparison" || b.at("selection_source") == 0, "Comparison tables cannot consume selection");
                view->m_Binding = std::move(b);
            }
            else if (operation == "table")
            {
                require(view->TemplateId() == "core.comparison", "Expected a comparison table");
                auto settings = table(request.at("table"));
                const auto metric = settings.at("sort_metric_id").get<std::string>();
                require(metric.empty() || std::any_of(m_Metrics.begin(), m_Metrics.end(), [&](const auto &m) { return m.at("id") == metric; }), "Unknown sort metric");
                view->m_Table = std::move(settings);
            }
            else if (operation == "rows") response["items"] = Items(*view, view->TemplateId() != "core.comparison");
            else
            {
                const auto action = operation == "close" ? WindowAction::Close : operation == "activate" ? WindowAction::Activate : WindowAction::Duplicate;
                const auto result = m_Workspace->Apply({action, id});
                if (!result) return failure(result.Error());
                if (operation == "duplicate") response["instance_id"] = m_Workspace->Capture().active;
            }
        }
        if (ImGui::GetCurrentContext() && operation != "list" && operation != "rows" && operation != "save") ImGui::MarkIniSettingsDirty();
        return response;
    }
    void ViewHost::Render(ViewInstance &view)
    {
        namespace controls = sst::ui::widgets;
        auto b = view.Binding();
        int mode = b.at("mode") == "follow_active" ? 0 : b.at("mode") == "pinned" ? 1 : 2;
        auto rebind = [&] { (void)Execute({{"operation", "bind"}, {"instance_id", view.GetId()}, {"binding", b}}); };
        if (controls::Combo("Binding", &mode, "Follow active\0Pinned item\0Explicit items\0"))
        {
            b["mode"] = mode == 0 ? "follow_active" : mode == 1 ? "pinned" : "explicit";
            b["selection_source"] = 0; b["item_ids"] = Json::array();
            if (mode == 1 && !m_Items.empty()) b["item_ids"].push_back(m_ActiveItem.empty() ? m_Items.front().at("id").get<std::string>() : m_ActiveItem);
            if (mode == 2) for (const auto &item : m_Items) b["item_ids"].push_back(item.at("id"));
            rebind();
            b = view.Binding(); mode = b.at("mode") == "follow_active" ? 0 : b.at("mode") == "pinned" ? 1 : 2;
        }
        if (mode != 0 && controls::BeginCombo("Items", mode == 1 ? b.at("item_ids").front().get<std::string>().c_str() : "Select items"))
        {
            for (const auto &item : m_Items)
            {
                const auto id = item.at("id").get<std::string>();
                auto &bound = b["item_ids"]; auto it = std::find(bound.begin(), bound.end(), id);
                const auto label = item.at("name").get<std::string>() + "##" + id;
                if (ImGui::Selectable(label.c_str(), it != bound.end(), ImGuiSelectableFlags_NoAutoClosePopups))
                {
                    if (mode == 1) bound = Json::array({id});
                    else if (it == bound.end()) bound.push_back(id); else bound.erase(it);
                    rebind();
                }
            }
            controls::EndCombo();
        }
        if (mode == 2 && view.TemplateId() != "core.comparison" && controls::BeginCombo("Overlay selection", b.at("selection_source") == 0 ? "All bound items" : "Comparison table"))
        {
            if (ImGui::Selectable("All bound items", b.at("selection_source") == 0)) { b["selection_source"] = 0; rebind(); }
            for (const auto &window : m_Workspace->Windows()) if (auto candidate = Find(window->GetId()); candidate && candidate->TemplateId() == "core.comparison")
            {
                auto label = candidate->GetTitle() + "##" + std::to_string(candidate->GetId());
                if (ImGui::Selectable(label.c_str(), b.at("selection_source") == candidate->GetId())) { b["selection_source"] = candidate->GetId(); rebind(); }
            }
            controls::EndCombo();
        }
        ImGui::Separator();
        if (view.TemplateId() == "core.comparison") RenderComparisonTable(*this, view);
        else if (auto it = m_Templates.find(view.TemplateId()); it != m_Templates.end() && it->second.render)
            it->second.render(view, Items(view));
    }
    void ViewHost::RenderMenu()
    {
        if (m_Templates.size() == 1 && m_Items.empty()) return;
        if (ImGui::BeginMenu("New view"))
        {
            for (const auto &[id, view] : m_Templates)
                if (ImGui::MenuItem((view.name + "##" + id).c_str())) (void)Execute({{"operation", "open"}, {"template_id", id}});
            ImGui::EndMenu();
        }
    }

    ViewInstance::ViewInstance(WeakRef<ViewHost> host, std::string templateId, std::string name)
        : EditorWindow("view." + templateId, std::move(name), true, {.duplicable = true}),
          m_Host(std::move(host)), m_TemplateId(std::move(templateId)) {}
    WindowDuplicate ViewInstance::Duplicate() const { return {CreateRef<ViewInstance>(*this), {}}; }
    void ViewInstance::RenderContent() { if (auto host = m_Host.lock()) host->Render(*this); }
    ViewInstance::State ViewInstance::ReadState(const Json &b, const Json &t)
    {
        State state{b.at("mode"), b.at("item_ids").get<std::vector<std::string>>(), b.at("selection_source"),
            t.at("filter_text"), t.at("sort_metric_id"), t.at("descending")};
        if (!t.at("selected_item_ids").is_null()) state.selected = t.at("selected_item_ids").get<std::vector<std::string>>();
        return state;
    }
    std::any ViewInstance::CaptureViewState() const { return ReadState(m_Binding, m_Table); }
    bool ViewInstance::RestoreViewState(const std::any &value)
    {
        const auto *s = std::any_cast<State>(&value);
        if (!s || (m_TemplateId == "core.comparison" && s->selectionSource)) return false;
        m_Binding = {{"mode", s->mode}, {"item_ids", s->items}, {"selection_source", s->selectionSource}};
        m_Table = {{"filter_text", s->filter}, {"sort_metric_id", s->sortMetric}, {"descending", s->descending},
            {"selected_item_ids", s->selected ? Json(*s->selected) : Json()}};
        return true;
    }
    std::string ViewInstance::EncodeViewState(const std::any &value) const
    {
        const auto &s = std::any_cast<const State &>(value);
        return Json{{"version", 1}, {"binding", {{"mode", s.mode}, {"item_ids", s.items}, {"selection_source", s.selectionSource}}},
            {"table", {{"filter_text", s.filter}, {"sort_metric_id", s.sortMetric}, {"descending", s.descending},
                {"selected_item_ids", s.selected ? Json(*s.selected) : Json()}}}}.dump();
    }
    std::any ViewInstance::DecodeViewState(const std::string &text) const
    {
        const auto state = Json::parse(text); require(state.at("version") == 1, "Unsupported view state");
        auto b = binding(state.at("binding")), t = table(state.at("table"));
        require(m_TemplateId != "core.comparison" || b.at("selection_source") == 0, "Comparison table cannot consume selection");
        return ReadState(b, t);
    }
}
