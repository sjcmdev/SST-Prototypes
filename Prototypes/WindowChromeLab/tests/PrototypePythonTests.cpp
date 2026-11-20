#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include "IO/PrototypePythonBindings.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Presentation/Panels/ViewHost.hpp"
#include "Presentation/Panels/HostedEditorWindow.hpp"

namespace {
    const bool applicationModuleRegistered = [] {
        SST::ApplicationDetail::RegisterApplicationPythonModule(); return true;
    }();
}

namespace SST::Prototypes::WindowChromeLab
{
    TEST(PrototypePython, EmbeddedRoundtrip)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        RegisterPrototypePythonModule();
        Threading::SetMainThread();
        PanelRegistry panels;
        auto workspace = CreateRef<WindowWorkspace>(panels);
        auto commands = CreateRef<CommandRegistry>();
        ASSERT_TRUE(workspace->Bind(commands, {}));
        auto jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 1);
        auto views = CreateRef<ViewHost>(panels, workspace);
        ASSERT_TRUE(views->Bind(commands, jobs));
        ASSERT_TRUE(views->RegisterTemplate({"iv", "I-V", {}}));
        auto window = CreateRef<HostedEditorWindow>("Headless tool");
        const auto id = panels.AddExisting(window);
        auto state = window->GetState(); state.pinned = true; state.visible = false; window->ApplyState(state);
        ApplicationDetail::RegisterScreenshotCommand(commands, {});
        PythonInterpreter interpreter;
        ASSERT_TRUE(interpreter.Start());
        PythonGilAcquireScope gil;
        ASSERT_TRUE(gil.IsActive());
        auto *paths = PySys_GetObject("path");
        auto *root = PyUnicode_FromString(SST_PROTOTYPE_PYTHON_ROOT);
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(PyList_Insert(paths, 0, root), 0);
        Py_DECREF(root);
        ASSERT_EQ(PyRun_SimpleString(
            "from prototype_example import conductivity\n"
            "assert abs(conductivity(2.0) - 4.4860945752) < 1e-12\n"
            "import json, sst_application\n"
            "response = json.loads(sst_application.execute(json.dumps({'command_id': 'app.layout.reset'})))\n"
            "assert response == {'ok': True, 'command_id': 'app.layout.reset'}, response\n"
            "import sst_views\n"
            "def view_call(**request):\n"
            "    response = json.loads(sst_views.execute(json.dumps(request)))\n"
            "    assert response['ok'], response\n"
            "    return response\n"
            "view_call(operation='catalog', metrics=[dict(id='voc_V', name='Voc', unit='V')], items=[dict(id='a',name='Alpha',metrics=dict(voc_V=.8)),dict(id='b',name='Beta',metrics=dict(voc_V=.7))])\n"
            "view_call(operation='active', item_id='a')\n"
            "instance = view_call(operation='open', template_id='iv')['instance_id']\n"
            "assert view_call(operation='rows', instance_id=instance)['items'][0]['id'] == 'a'\n"
            "view_call(operation='bind', instance_id=instance, binding={'mode':'pinned','item_ids':['b']})\n"
            "assert view_call(operation='rows', instance_id=instance)['items'][0]['id'] == 'b'\n"
            "table = view_call(operation='open', template_id='core.comparison', binding=dict(mode='explicit',item_ids=['a','b']))['instance_id']\n"
            "view_call(operation='table', instance_id=table, table=dict(sort_metric_id='voc_V',selected_item_ids=['b']))\n"
            "view_call(operation='bind', instance_id=instance, binding=dict(mode='explicit',item_ids=['a','b'],selection_source=table))\n"
            "assert view_call(operation='rows', instance_id=instance)['items'][0]['id'] == 'b'\n"
            "assert len(view_call(operation='list')['instances']) == 2\n"
            "saved = view_call(operation='save')['layout']\n"
            "view_call(operation='duplicate', instance_id=instance)\n"
            "view_call(operation='close', instance_id=instance)\n"
            "view_call(operation='activate', instance_id=instance)\n"
            "view_call(operation='load', layout=saved)\n"
            "assert not json.loads(sst_views.execute('{bad'))['ok']\n"), 0);
        EXPECT_FALSE(window->GetState().pinned); EXPECT_TRUE(window->IsVisible()); EXPECT_EQ(window->GetId(), id);
        if (std::getenv("SST_RUN_PYTEST"))
            ASSERT_EQ(PyRun_SimpleString(
                "import json, os, sys\n"
                "sys.path.extend(json.loads(os.environ['SST_ROOT_PYTHON_PATHS']))\n"
                "import pytest\n"
                "assert pytest.main([os.environ['SST_RUN_PYTEST'], '-q', '-p', 'no:cacheprovider']) == 0\n"), 0);
#else
        EXPECT_FALSE(PythonInterpreter::IsEmbeddingAvailable());
        std::cout << "Embedded Python unavailable in this build; roundtrip not executed.\n";
        ASSERT_EQ(std::getenv("SST_REQUIRE_EMBEDDING"), nullptr)
            << "Embedding is required, but this target was built without it.";
#endif
    }
}
