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

namespace SST::Prototypes::CapLab
{
    TEST(PrototypePython, EmbeddedRoundtrip)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        RegisterPrototypePythonModule();
        PythonInterpreter interpreter;
        ASSERT_TRUE(interpreter.Start());
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
        EXPECT_TRUE(workflow.document.project.sweeps.empty());
        ASSERT_EQ(workflow.preview->at("columns").at(0).size(),40);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[0][0].get<double>(),100);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[1][0].get<double>(),8.8601e-10);
        EXPECT_DOUBLE_EQ(workflow.preview->at("columns")[2][0].get<double>(),134160);
        ASSERT_EQ(workflow.temperature,100);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
        EXPECT_EQ(workflow.document.project.sweeps.at(0).temperature_K,100);
        const auto &project=*workflow.document.project.engine;
        const auto layer=project.children(workflow.document.project.graph).at(0);
        const auto series=project.series(project.children(layer).at(0));
        const auto &spec=std::get<pl::Layer>(project.object(layer).data).spec;
        workflow.view.viewport=sst::ui::plot::ResolveBounds(spec,&series,1,workflow.view);
        DocumentJob exportJob(Operation::Export,"",Path{FileSystem::TempDirectoryPath()/"caplab-cf-test.png"});
        exportJob.snapshot=pl::snapshotGraph(project,workflow.document.project.graph,workflow.view);
        JobContext context({}, {}, {}, {}, [] {return false;});
        ASSERT_NO_THROW(exportJob.Execute(context));
        EXPECT_GT(std::filesystem::file_size(exportJob.path.Native()),1000);
        EXPECT_TRUE(FileSystem::Remove(exportJob.path.Native()));
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
        EXPECT_EQ(bundleWorkflow.document.project.measurements.size(),1);
        const auto imported = Serialize(bundleWorkflow.document.project);
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_TRUE(bundleWorkflow.document.project.samples.empty());
        ASSERT_TRUE(bundleUndo->Redo()); EXPECT_EQ(Serialize(bundleWorkflow.document.project),imported);
        CommandContext area;
        area.Set("sample_id",bundleWorkflow.document.project.samples.at(0).id); area.Set("area_m2",3.7e-6);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.area"},std::move(area)));
        bundleWorkflow.jobs=CreateRef<JobSystem>(nullptr,1);
        bundleWorkflow.analysisOptions={{"measurement_id",bundleWorkflow.document.project.measurements.at(0).at("id")},
            {"type","CV"},{"range_V",{0,.25}},{"material","ProPho-compat"},{"xd_source","DLCP"},{"xd_policy","block"}};
        const auto beforeAnalysis=Serialize(bundleWorkflow.document.project);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.analyze"})); bundleBus->ProcessQueue();
        bundleWorkflow.jobs->Shutdown(); bundleWorkflow.OnUpdate(0);
        ASSERT_EQ(bundleWorkflow.document.project.analyses.size(),1) << bundleWorkflow.status;
        const auto analyzed=Serialize(bundleWorkflow.document.project);
        EXPECT_EQ(Serialize(Parse(analyzed)),analyzed);
        EXPECT_EQ(bundleWorkflow.document.project.analyses.at(0).at("status"),"valid");
        EXPECT_EQ(bundleWorkflow.document.project.analyses.at(0).at("graphs").size(),4);
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_EQ(Serialize(bundleWorkflow.document.project),beforeAnalysis);
        ASSERT_TRUE(bundleUndo->Redo()); EXPECT_EQ(Serialize(bundleWorkflow.document.project),analyzed);
        CommandContext changedArea;
        changedArea.Set("sample_id",bundleWorkflow.document.project.samples.at(0).id); changedArea.Set("area_m2",7.4e-6);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.area"},std::move(changedArea)));
        EXPECT_EQ(bundleWorkflow.document.project.analyses.at(0).at("status"),"stale");
        ASSERT_TRUE(bundleUndo->Undo()); EXPECT_EQ(bundleWorkflow.document.project.analyses.at(0).at("status"),"valid");
        bundleWorkflow.jobs=CreateRef<JobSystem>(nullptr,1);
        ASSERT_TRUE(bundleCommands->Execute(CommandID{"caplab.analyze"})); bundleBus->ProcessQueue();
        bundleWorkflow.Changed(); // A later edit must win over this worker result.
        bundleWorkflow.jobs->Shutdown(); bundleWorkflow.OnUpdate(0);
        EXPECT_EQ(bundleWorkflow.document.project.analyses.size(),1);
        EXPECT_NE(bundleWorkflow.status.find("Stale revision"),std::string::npos);
        JobContext cancelled({}, {}, {}, {}, [] {return true;});
        DocumentJob cancelledAnalysis(Operation::Analyze,analyzed);
        cancelledAnalysis.analysisOptions=bundleWorkflow.analysisOptions;
        EXPECT_ANY_THROW(cancelledAnalysis.Execute(cancelled));

        // M4: existing M1 input -> AS job -> approved points -> both Arrhenius models.
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        workflow.analysisOptions={{"type","AS"},{"measurement_id",workflow.document.project.sweeps[0].id}};
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        ASSERT_EQ(workflow.document.project.analyses.size(),1) << workflow.status;
        EXPECT_EQ(workflow.document.project.analyses[0]["raw_D_F"].size(),40);
        const auto asText=Serialize(workflow.document.project);
        EXPECT_EQ(Serialize(Parse(asText)),asText);
        const auto sampleId=workflow.document.project.samples[0].id;
        auto edit=[&](Json value) {CommandContext c; c.Set("edit",std::move(value)); return commands->Execute(CommandID{"caplab.defect"},std::move(c));};
        ASSERT_TRUE(edit({{"action","create"},{"sample_id",sampleId},{"name","M4 synthetic"}}));
        const auto did=workflow.document.project.defects[0]["id"];
        for (auto point : {std::pair{200.,100.},std::pair{250.,1000.},std::pair{300.,10000.}})
            ASSERT_TRUE(edit({{"action","add"},{"defect_id",did},{"point",{{"measurement_id",nullptr},{"row_id",nullptr},
                {"temperature_K",point.first},{"frequency_Hz",point.second},{"provenance","independent manual fixture"}}}}));
        const auto mid=workflow.document.project.sweeps[0].id;
        const auto measuredRow=AnalysisMeasurement(workflow.document.project,mid).at("row_ids").at(0);
        ASSERT_TRUE(edit({{"action","add"},{"defect_id",did},{"point",{{"measurement_id",mid},{"row_id",measuredRow},
            {"provenance","approved measured point"}}}}));
        const auto preFit=Serialize(workflow.document.project);
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        workflow.analysisOptions={{"type","Arrhenius"},{"defect_id",did},{"models",{"omega-T2","ProPho-compat"}}};
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        ASSERT_EQ(workflow.document.project.analyses.size(),2) << workflow.status;
        auto fitted=Serialize(workflow.document.project);
        EXPECT_EQ(Serialize(Parse(fitted)),fitted);
        EXPECT_EQ(workflow.document.project.analyses[1]["fits"].size(),2);
        EXPECT_TRUE(workflow.document.project.analyses[1]["fits"][0]["sigma_cm2"].is_null());
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(workflow.document.project),preFit);
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(workflow.document.project),fitted);
        const auto pid=workflow.document.project.defects[0]["points"][0]["id"];
        ASSERT_TRUE(edit({{"action","enable"},{"defect_id",did},{"point_id",pid},{"enabled",false}}));
        EXPECT_EQ(workflow.document.project.analyses[1]["status"],"stale");
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(workflow.document.project.analyses[1]["status"],"valid");
        ASSERT_TRUE(edit({{"action","delete"},{"defect_id",did}}));
        EXPECT_EQ(workflow.document.project.analyses[1]["status"],"missing_input");
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(workflow.document.project.analyses[1]["status"],"valid");
        const auto savedInputs=Serialize(workflow.document.project);
        workflow.document.project.sweeps.clear(); RefreshAnalyses(workflow.document.project);
        EXPECT_EQ(workflow.document.project.analyses[0]["status"],"missing_input");
        EXPECT_EQ(workflow.document.project.analyses[1]["status"],"missing_input");
        workflow.document.project=Parse(savedInputs);
        workflow.document.project.sweeps[0].temperature_K=101; RefreshAnalyses(workflow.document.project);
        EXPECT_EQ(workflow.document.project.analyses[1]["status"],"stale");
        JobContext m4context({}, {}, {}, {}, [] {return false;});
        DocumentJob changedPoint(Operation::Analyze,Serialize(workflow.document.project));
        changedPoint.analysisOptions=workflow.analysisOptions;
        EXPECT_ANY_THROW(changedPoint.Execute(m4context));
        workflow.document.project=Parse(savedInputs);
        workflow.jobs=CreateRef<JobSystem>(nullptr,1);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
        workflow.Changed(); workflow.jobs->Shutdown(); workflow.OnUpdate(0);
        EXPECT_EQ(workflow.document.project.analyses.size(),2);
        EXPECT_NE(workflow.status.find("Stale revision"),std::string::npos);


#else
        EXPECT_FALSE(PythonInterpreter::IsEmbeddingAvailable());
        std::cout << "Embedded Python unavailable in this build; roundtrip not executed.\n";
        ASSERT_EQ(std::getenv("SST_REQUIRE_EMBEDDING"), nullptr)
            << "Embedding is required, but this target was built without it.";
#endif
    }
}
