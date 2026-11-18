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
                "assert pytest.main([os.environ['SST_RUN_PYTEST'], '-q', '-p', 'no:cacheprovider']) == 0\n"
                "sys.stdout.flush(); sys.stderr.flush()\n"), 0);
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
        CapLabWorkflow bundleWorkflow(bundleJobs,bus,bundleUndo);
        RegisterCommands(bundleCommands,bus,bundleWorkflow);
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

#else
        EXPECT_FALSE(PythonInterpreter::IsEmbeddingAvailable());
        std::cout << "Embedded Python unavailable in this build; roundtrip not executed.\n";
        ASSERT_EQ(std::getenv("SST_REQUIRE_EMBEDDING"), nullptr)
            << "Embedding is required, but this target was built without it.";
#endif
    }
}
