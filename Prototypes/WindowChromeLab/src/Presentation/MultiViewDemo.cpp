#include "Core/dspch.hpp"
#include "Presentation/MultiViewDemo.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/ViewHost.hpp"
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <map>
#define GLFW_INCLUDE_NONE
#include "Vendor/GLFW/include/GLFW/glfw3.h"

namespace SST::Prototypes::WindowChromeLab
{
    void RegisterMultiViewDemo(EditorLayer &editor)
    {
        using Json = nlohmann::json;
        namespace plot = sst::ui::plot;
        auto host = editor.GetViews();
        for (bool bands : {false, true})
        {
            const auto result = host->RegisterTemplate({bands ? "demo.bands" : "demo.iv", bands ? "Bands" : "I-V",
                [bands, states = std::map<WindowId, plot::PlotViewState>{}](ViewInstance &view, const Json &items) mutable {
                    std::vector<std::vector<double>> xs, ys;
                    for (const auto &item : items)
                    {
                        xs.push_back(item.at("data").at(bands ? "position_nm" : "voltage_V").get<std::vector<double>>());
                        ys.push_back(item.at("data").at(bands ? "energy_eV" : "current_density_mA_cm2").get<std::vector<double>>());
                    }
                    std::vector<plot::SeriesEntryView> entries;
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        auto key = items[i].at("name").get<std::string>() + "###" + items[i].at("id").get<std::string>();
                        entries.push_back({key, {xs[i].data(), ys[i].data(), xs[i].size()}});
                    }
                    plot::PlotSpec spec; spec.autoFit = plot::AutoFit::Once;
                    spec.x.label = bands ? "Position" : "Voltage"; spec.x.unit = bands ? "nm" : "V";
                    spec.y.label = bands ? "Energy" : "Current density"; spec.y.unit = bands ? "eV" : "mA/cm^2";
                    plot::RenderPlot("##bound_plot", spec, entries.data(), entries.size(), states[view.GetId()], {}, {-1, -1});
                }});
            if (!result) throw std::runtime_error(result.Error().technicalDetails);
        }
        Json items = Json::array();
        for (int run = 0; run < 3; ++run)
        {
            Json data{{"voltage_V", Json::array()}, {"current_density_mA_cm2", Json::array()},
                {"position_nm", Json::array()}, {"energy_eV", Json::array()}};
            for (int i = 0; i <= 50; ++i)
            {
                const double voltage = i / 50.;
                data["voltage_V"].push_back(voltage); data["current_density_mA_cm2"].push_back((20. + run * 3) * (voltage * voltage / (.7 + run * .05) - 1));
                data["position_nm"].push_back(i * 10.); data["energy_eV"].push_back(1.5 + run * .1 - voltage * .6);
            }
            items.push_back({{"id", "run-" + std::to_string(run + 1)}, {"name", "Run " + std::to_string(run + 1)},
                {"metrics", {{"voc_V", .7 + run * .05}, {"jsc_mA_cm2", 20. + run * 3}, {"ff_percent", 70. + run}}}, {"data", data}});
        }
        auto call = [host](const Json &request) {
            auto response = host->Execute(request);
            if (!response.at("ok").get<bool>()) throw std::runtime_error(response.dump());
            return response;
        };
        call({{"operation", "catalog"}, {"items", items}, {"metrics", Json::array({
            {{"id", "voc_V"}, {"name", "Voc"}, {"unit", "V"}},
            {{"id", "jsc_mA_cm2"}, {"name", "Jsc"}, {"unit", "mA/cm^2"}},
            {{"id", "ff_percent"}, {"name", "FF"}, {"unit", "%"}}})}});
        call({{"operation", "active"}, {"item_id", "run-1"}});
        const Json all = Json::array({"run-1", "run-2", "run-3"});
        int x = 0, y = 0, width = 0, height = 0;
        glfwGetWindowPos(glfwGetCurrentContext(), &x, &y);
        glfwGetWindowSize(glfwGetCurrentContext(), &width, &height);
        const float paneWidth = width / 2.f, paneHeight = (height - 60.f) / 2.f;
        editor.GetWindows()->SetAvailableArea(float(x), float(y + 30), float(width), float(height - 60));
        int pane = 0;
        auto open = [&](const std::string &templateId, const Json &binding) {
            auto view = CreateRef<ViewInstance>(host, templateId, host->Templates().at(templateId).name);
            auto state = view->GetState();
            state.x = float(x) + float(pane % 2) * paneWidth; state.y = float(y + 30) + float(pane / 2) * paneHeight;
            state.width = paneWidth; state.height = paneHeight;
            view->ApplyState(state);
            const auto id = editor.RegisterWindow(view); // Register geometry as the workspace's reset defaults.
            ++pane;
            call({{"operation", "bind"}, {"instance_id", id}, {"binding", binding}});
            return id;
        };
        const auto table = open("core.comparison", {{"mode", "explicit"}, {"item_ids", all}});
        open("demo.iv", {{"mode", "follow_active"}});
        open("demo.bands", {{"mode", "pinned"}, {"item_ids", Json::array({"run-2"})}});
        open("demo.iv", {{"mode", "explicit"}, {"item_ids", all}, {"selection_source", table}});
        editor.RegisterMenuRenderer("multi-view-demo", [host](EditorServices &) {
            if (ImGui::BeginMenu("Demo active run"))
            {
                for (int run = 1; run <= 3; ++run)
                {
                    const auto name = "Run " + std::to_string(run);
                    if (ImGui::MenuItem(name.c_str())) (void)host->Execute({{"operation", "active"}, {"item_id", "run-" + std::to_string(run)}});
                }
                ImGui::EndMenu();
            }
        });
    }
}
