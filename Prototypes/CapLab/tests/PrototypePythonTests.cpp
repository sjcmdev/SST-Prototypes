#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include "CapLabCommands.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include <imgui_internal.h>
#include <implot.h>

namespace SST::Prototypes::CapLab
{
    TEST(PrototypePython, EmbeddedRoundtrip)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        RegisterPrototypePythonModule();
        PythonInterpreter interpreter;
        ASSERT_TRUE(interpreter.Start());
        // A GUI first import has not imported sst_caplab or initialized nanobind yet.
        {
            auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
            auto commands=CreateRef<CommandRegistry>(); auto undo=CreateRef<UndoStack>();
            CapLabWorkflow cold(jobs,bus,undo); RegisterCommands(commands,bus,cold);
            auto subscription=bus->Subscribe<DocumentRequested>([&](const auto& e) {cold.Start(e.operation,e.target);});
            CommandContext request; request.Set("path",Path{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures/m2/samples_Cd_diffusion.json"});
            cold.importOptions=R"({"profile":"propho_json"})";
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.preview"},std::move(request)));
            bus->ProcessQueue(); jobs->Shutdown(); cold.OnUpdate(0);
            ASSERT_TRUE(cold.preview) << cold.status;
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"})) << cold.status;
        }
        {
        PythonGilAcquireScope gil;
        ASSERT_TRUE(gil.IsActive());
        auto *paths = PySys_GetObject("path");
        auto *root = PyUnicode_FromString(SST_PROTOTYPE_PYTHON_ROOT);
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(PyList_Insert(paths, 0, root), 0);
        Py_DECREF(root);
        ASSERT_EQ(PyRun_SimpleString(
            "from caplab import parse_lab_as, project_from_preview\n"
            "p = project_from_preview(parse_lab_as('100 8.8601e-10 134160', 'cf_100K_7.dat'), 100)\n"
            "assert p['sweeps'][0]['temperature_K'] == 100\n"), 0);
        if (std::getenv("SST_RUN_PYTEST"))
            ASSERT_EQ(PyRun_SimpleString(
                "import json, os, sys\n"
                "sys.path.extend(json.loads(os.environ['SST_ROOT_PYTHON_PATHS']))\n"
                "import pytest\n"
                "pytest_result = pytest.main([os.environ['SST_RUN_PYTEST'], '-q', '-p', 'no:cacheprovider'])\n"
                "sys.stdout.flush(); sys.stderr.flush()\n"
                "assert pytest_result == 0\n"), 0);
        }
        // CL-02: render the actual import panel around the GUI command/job path.
        {
        auto* gui=ImGui::CreateContext(); auto* plot=ImPlot::CreateContext();
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1600,1200}; io.DeltaTime=1.f/60;
        io.ConfigDebugHighlightIdConflicts=true;
        unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        for (const auto* filename : {"m2/samples_Cd_diffusion.json", "cf_100K_7.dat"}) {
            auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
            auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
            CapLabWorkflow w(jobs,bus,undo); RegisterCommands(commands,bus,w);
            auto subscription=bus->Subscribe<DocumentRequested>([&](const auto& e) {w.Start(e.operation,e.target);});
            CapLabPrototypePanel panel(&w,commands,CapLabPrototypePanel::View::Import);
            auto render=[&] {
                ImGui::NewFrame(); ImGui::SetNextWindowSize({1500,1100}); ImGui::Begin("CL-02");
                panel.RenderContent(); ImGui::End(); ImGui::Render();
            };
            render();
            const Path path{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures"/filename};
            if (path.Native().extension()==".json") w.importOptions=R"({"profile":"propho_json"})";
            CommandContext request; request.Set("path",path);
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.preview"},std::move(request)));
            bus->ProcessQueue(); ASSERT_NE(w.activeJob,0);
            jobs->Shutdown(); w.OnUpdate(0); ASSERT_TRUE(w.preview) << w.status;
            render();
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"})) << w.status;
            EXPECT_FALSE(w.Document().project.samples.empty()); render();
            EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
        }
        ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
        }
        // Invalid preview metadata must fail in the job, never in the GUI update/render loop.
        for (const auto& [name,contents] : {
            std::pair{"caplab-clfix1-cf_0K.dat","100 1e-9 1000"},
            std::pair{"caplab-clfix1-bad-name.json",R"({"samples":[{"name":7,"as_data":[],"dl_data":[]}]})"}}) {
            const Path path{FileSystem::TempDirectoryPath()/name}; std::string error;
            ASSERT_TRUE(TextFileIO::Save(path,contents,error));
            auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
            auto commands=CreateRef<CommandRegistry>(); auto undo=CreateRef<UndoStack>();
            CapLabWorkflow invalid(jobs,bus,undo); RegisterCommands(commands,bus,invalid);
            auto subscription=bus->Subscribe<DocumentRequested>([&](const auto& e) {invalid.Start(e.operation,e.target);});
            if (path.Native().extension()==".json") invalid.importOptions=R"({"profile":"propho_json"})";
            CommandContext request; request.Set("path",path);
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.preview"},std::move(request)));
            bus->ProcessQueue(); jobs->Shutdown();
            ASSERT_NO_THROW(invalid.OnUpdate(0));
            EXPECT_FALSE(invalid.preview); ASSERT_TRUE(invalid.failure);
            EXPECT_EQ(invalid.failure->code,"caplab.preview");
            EXPECT_TRUE(invalid.Document().project.samples.empty());
            EXPECT_TRUE(FileSystem::Remove(path.Native()));
        }
        // Exercise the real command -> EventBus -> Python job -> main-thread commit path.
        auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
        auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
        CapLabWorkflow workflow(jobs,bus,undo); RegisterCommands(commands,bus,workflow);
        auto subscription=bus->Subscribe<DocumentRequested>([&](const auto &e) {workflow.Start(e.operation,e.target);});
        const Path fixture{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures/cf_100K_7.dat"};
        CommandContext request; request.Set("path",fixture);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.preview"},std::move(request)));
        bus->ProcessQueue(); ASSERT_NE(workflow.activeJob,0);
        jobs->Shutdown(); workflow.OnUpdate(0);
        ASSERT_TRUE(workflow.preview) << workflow.status;
        EXPECT_TRUE(workflow.Document().project.sweeps.empty());
        ASSERT_EQ(workflow.preview->at("columns").at(0).size(),40);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[0][0].get<double>(),100);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[1][0].get<double>(),8.8601e-10);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[2][0].get<double>(),134160);
        ASSERT_EQ(workflow.temperature,100);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
        EXPECT_EQ(workflow.Document().project.sweeps.at(0).temperature_K,100);
        const auto &project=*workflow.Document().project.engine;
        const auto layer=project.children(workflow.Document().project.graph).at(0);
        const auto series=project.series(project.children(layer).at(0));
        const auto &spec=std::get<pl::Layer>(project.object(layer).data).spec;
        workflow.view.viewport=sst::ui::plot::ResolveBounds(spec,&series,1,workflow.view);
        DocumentJob exportJob(Operation::Export,"",Path{FileSystem::TempDirectoryPath()/"caplab-cf-test.png"});
        exportJob.snapshot=pl::snapshotGraph(project,workflow.Document().project.graph,workflow.view);
        JobContext context({}, {}, {}, {}, [] {return false;});
        ASSERT_NO_THROW(exportJob.Execute(context));
        EXPECT_GT(std::filesystem::file_size(exportJob.path.Native()),1000);
        EXPECT_TRUE(FileSystem::Remove(exportJob.path.Native()));
        // Render the export controls and exercise GUI commands with the same shared job adapter.
        {
            auto* gui=ImGui::CreateContext(); auto* plot=ImPlot::CreateContext();
            auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1600,1200}; io.DeltaTime=1.f/60;
            io.ConfigDebugHighlightIdConflicts=true;
            unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
            CapLabPrototypePanel panel(&workflow,commands,CapLabPrototypePanel::View::Plot);
            for (int frame=0;frame<3;++frame) {
                ImGui::NewFrame(); ImGui::SetNextWindowSize({1200,1000}); ImGui::Begin("Export controls");
                panel.RenderContent(); ImGui::End(); ImGui::Render();
                EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
            }
            ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
        }
        for (const auto* format : {"png","svg","pdf"}) {
            workflow.jobs=CreateRef<JobSystem>(nullptr,1);
            workflow.exportGraphs={workflow.Document().project.graph};
            workflow.graphViews[workflow.Document().project.graph]=workflow.view;
            const auto destination=FileSystem::TempDirectoryPath()/(std::string("caplab-gui-batch.")+format);
            CommandContext request; request.Set("path",Path::FromResolved(destination));
            ASSERT_TRUE(commands->Execute(CommandID{"caplab.export"},std::move(request)));
            bus->ProcessQueue(); ASSERT_NE(workflow.activeJob,0) << workflow.status;
            workflow.jobs->Shutdown(); workflow.OnUpdate(0);
            ASSERT_TRUE(workflow.exportResult.contains("graphs")) << workflow.status;
            const Path output{workflow.exportResult.at("graphs").at(0).at("path").get<std::string>()};
            EXPECT_GT(std::filesystem::file_size(output.Native()),500);
            EXPECT_TRUE(FileSystem::Remove(output.Native()));
        }
        workflow.exportGraphs.clear();

        auto bundleJobs=CreateRef<JobSystem>(nullptr,1);
        auto bundleUndo=CreateRef<UndoStack>(); auto bundleCommands=CreateRef<CommandRegistry>();
        auto bundleBus=CreateRef<EventBus>();
        CapLabWorkflow bundleWorkflow(bundleJobs,bundleBus,bundleUndo);
        RegisterCommands(bundleCommands,bundleBus,bundleWorkflow);
        auto analysisRequest=bundleBus->Subscribe<DocumentRequested>([&](const auto &e) { bundleWorkflow.Start(e.operation,e.target); });
        bundleWorkflow.importOptions = R"({"profile":"propho_dl","temperature_K":360,"frequency_Hz":1000})";
        bundleWorkflow.Start(Operation::Preview,Path{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures/m2/#132_DL_1e3Hz_360K.dat"});
        bundleJobs->Shutdown(); bundleWorkflow.OnUpdate(0);
        ASSERT_TRUE(bundleWorkflow.preview) << bundleWorkflow.status;
        ASSERT_EQ(bundleWorkflow.preview->at("samples").at(0).at("measurements").size(),1);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.import"})) << bundleWorkflow.status;
        EXPECT_EQ(bundleWorkflow.Document().project.measurements.size(),1);
        const auto imported = Serialize(bundleWorkflow.Document().project);
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_TRUE(bundleWorkflow.Document().project.samples.empty());
        ASSERT_TRUE(bundleUndo->Redo()); EXPECT_EQ(Serialize(bundleWorkflow.Document().project),imported);
        CommandContext area;
        area.Set("sample_id",bundleWorkflow.Document().project.samples.at(0).id); area.Set("area_m2",3.7e-6);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.area"},std::move(area)));
        bundleWorkflow.jobs=CreateRef<JobSystem>(nullptr,1);
        bundleWorkflow.analysisOptions={{"measurement_id",bundleWorkflow.Document().project.measurements.at(0).at("id")},
            {"type","CV"},{"range_V",{0,.25}},{"material","ProPho-compat"},{"xd_source","DLCP"},{"xd_policy","block"}};
        const auto beforeAnalysis=Serialize(bundleWorkflow.Document().project);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.analyze"})); bundleBus->ProcessQueue();
        bundleWorkflow.jobs->Shutdown(); bundleWorkflow.OnUpdate(0);
        ASSERT_EQ(bundleWorkflow.Document().project.analyses.size(),1) << bundleWorkflow.status;
        const auto analyzed=Serialize(bundleWorkflow.Document().project);
        EXPECT_EQ(Serialize(Parse(analyzed)),analyzed);
        EXPECT_EQ(bundleWorkflow.Document().project.analyses.at(0).at("status"),"valid");
        EXPECT_EQ(bundleWorkflow.Document().project.analyses.at(0).at("graphs").size(),4);
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_EQ(Serialize(bundleWorkflow.Document().project),beforeAnalysis);
        ASSERT_TRUE(bundleUndo->Redo()); EXPECT_EQ(Serialize(bundleWorkflow.Document().project),analyzed);
        CommandContext changedArea;
        changedArea.Set("sample_id",bundleWorkflow.Document().project.samples.at(0).id); changedArea.Set("area_m2",7.4e-6);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.area"},std::move(changedArea)));
        EXPECT_EQ(bundleWorkflow.Document().project.analyses.at(0).at("status"),"stale");
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_EQ(bundleWorkflow.Document().project.analyses.at(0).at("status"),"valid");
        bundleWorkflow.jobs=CreateRef<JobSystem>(nullptr,1);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.analyze"})); bundleBus->ProcessQueue();
        bundleWorkflow.Changed(); // A later edit must win over this worker result.
        bundleWorkflow.jobs->Shutdown(); bundleWorkflow.OnUpdate(0);
        EXPECT_EQ(bundleWorkflow.Document().project.analyses.size(),1);
        EXPECT_NE(bundleWorkflow.status.find("Stale revision"),std::string::npos);
        JobContext cancelled({}, {}, {}, {}, [] {return true;});
        DocumentJob cancelledAnalysis(Operation::Analyze,analyzed);
        cancelledAnalysis.analysisOptions=bundleWorkflow.analysisOptions;
        EXPECT_ANY_THROW(cancelledAnalysis.Execute(cancelled));

        // M4: existing M1 input -> AS job -> approved points -> both Arrhenius models.
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        workflow.analysisOptions={{"type","AS"},{"measurement_id",workflow.Document().project.sweeps[0].id}};
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        ASSERT_EQ(workflow.Document().project.analyses.size(),1) << workflow.status;
        EXPECT_EQ(workflow.Document().project.analyses[0]["raw_D_F"].size(),40);
        const auto asText=Serialize(workflow.Document().project);
        EXPECT_EQ(Serialize(Parse(asText)),asText);
        CommandContext removeMeasurement;
        removeMeasurement.Set("edit",Json{{"measurement_id",workflow.Document().project.sweeps[0].id}});
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.measurement"},std::move(removeMeasurement)));
        EXPECT_TRUE(workflow.Document().project.sweeps.empty());
        EXPECT_EQ(workflow.Document().project.analyses[0]["status"],"missing_input");
        const auto deletedText=Serialize(workflow.Document().project);
        EXPECT_EQ(Serialize(Parse(deletedText)),deletedText);
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(workflow.Document().project),asText);
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(workflow.Document().project),deletedText);
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(workflow.Document().project),asText);

        const auto sampleId=workflow.Document().project.samples[0].id;
        auto edit=[&](Json value) {CommandContext c; c.Set("edit",std::move(value)); return commands->Execute(CommandID{"caplab.defect"},std::move(c));};
        ASSERT_TRUE(edit({{"action","create"},{"sample_id",sampleId},{"name","M4 synthetic"}}));
        const auto did=workflow.Document().project.defects[0]["id"];
        for (auto point : {std::pair{200.,100.},std::pair{250.,1000.},std::pair{300.,10000.}})
            ASSERT_TRUE(edit({{"action","add"},{"defect_id",did},{"point",{{"measurement_id",nullptr},{"row_id",nullptr},
                {"temperature_K",point.first},{"frequency_Hz",point.second},{"provenance","independent manual fixture"}}}}));
        const auto mid=workflow.Document().project.sweeps[0].id;
        const auto measuredRow=AnalysisMeasurement(workflow.Document().project,mid).at("row_ids").at(0);
        ASSERT_TRUE(edit({{"action","add"},{"defect_id",did},{"point",{{"measurement_id",mid},{"row_id",measuredRow},
            {"provenance","approved measured point"}}}}));
        const auto preFit=Serialize(workflow.Document().project);
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        workflow.analysisOptions={{"type","Arrhenius"},{"defect_id",did},{"models",{"omega-T2","ProPho-compat"}}};
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        ASSERT_EQ(workflow.Document().project.analyses.size(),2) << workflow.status;
        auto fitted=Serialize(workflow.Document().project);
        EXPECT_EQ(Serialize(Parse(fitted)),fitted);
        EXPECT_EQ(workflow.Document().project.analyses[1]["fits"].size(),2);
        EXPECT_TRUE(workflow.Document().project.analyses[1]["fits"][0]["sigma_cm2"].is_null());
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(workflow.Document().project),preFit);
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(workflow.Document().project),fitted);
        const auto pid=workflow.Document().project.defects[0]["points"][0]["id"];
        ASSERT_TRUE(edit({{"action","enable"},{"defect_id",did},{"point_id",pid},{"enabled",false}}));
        EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"stale");
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"valid");
        ASSERT_TRUE(edit({{"action","delete"},{"defect_id",did}}));
        EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"missing_input");
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"valid");
        const auto savedInputs=Serialize(workflow.Document().project);
        workflow.Document().project.sweeps.clear(); RefreshAnalyses(workflow.Document().project);
        EXPECT_EQ(workflow.Document().project.analyses[0]["status"],"missing_input");
        EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"missing_input");
        workflow.Document().project=Parse(savedInputs);
        workflow.Document().project.sweeps[0].temperature_K=101; RefreshAnalyses(workflow.Document().project);
        EXPECT_EQ(workflow.Document().project.analyses[1]["status"],"stale");
        JobContext m4context({}, {}, {}, {}, [] {return false;});
        DocumentJob changedPoint(Operation::Analyze,Serialize(workflow.Document().project));
        changedPoint.analysisOptions=workflow.analysisOptions;
        EXPECT_ANY_THROW(changedPoint.Execute(m4context));
        workflow.Document().project=Parse(savedInputs);
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.Changed(); workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        EXPECT_EQ(workflow.Document().project.analyses.size(),2);
        EXPECT_NE(workflow.status.find("Stale revision"),std::string::npos);


        // M7: same Core job/Undo path, dependency propagation and stale-result rejection.
        workflow.Document().project=Parse(savedInputs);
        CommandContext m7area; m7area.Set("sample_id",sampleId); m7area.Set("area_m2",5.5e-6);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.area"},std::move(m7area)));
        const auto beforeWalter=Serialize(workflow.Document().project);
        workflow.analysisOptions={{"type","Walter"},{"measurement_ids",{mid}},{"V_b_V",1.},{"E_F_eV",0.},
            {"zeta_s_1_K_2",1e8},{"parameter_source","independent M7 test"},{"xd_source","manual"},
            {"xd_manual_m",1e-6},{"xd_description","test fixture"},{"input_revision",workflow.Document().revision}};
        auto runWalter=[&] {
            workflow.jobs=CreateRef<JobSystem>(nullptr,1);
            EXPECT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
            workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        };
        runWalter();
        ASSERT_EQ(workflow.Document().project.analyses.size(),3) << workflow.status;
        EXPECT_EQ(workflow.Document().project.analyses.back().at("model"),"Walter1996-linear-field");
        const auto afterWalter=Serialize(workflow.Document().project);
        EXPECT_EQ(Serialize(Parse(afterWalter)),afterWalter);
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(workflow.Document().project),beforeWalter);
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(workflow.Document().project),afterWalter);
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue(); workflow.Changed();
        workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        EXPECT_EQ(workflow.Document().project.analyses.size(),3);
        EXPECT_NE(workflow.status.find("Stale revision"),std::string::npos);
        workflow.Document().project.sweeps.clear(); RefreshAnalyses(workflow.Document().project);
        EXPECT_EQ(workflow.Document().project.analyses.back().at("status"),"missing_input");

#else
        EXPECT_FALSE(PythonInterpreter::IsEmbeddingAvailable());
        std::cout << "Embedded Python unavailable in this build; roundtrip not executed.\n";
        ASSERT_EQ(std::getenv("SST_REQUIRE_EMBEDDING"), nullptr)
            << "Embedding is required, but this target was built without it.";
#endif
    }
}
