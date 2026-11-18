#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include "IO/PrototypePythonBindings.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
    TEST(PrototypePython, EmbeddedRoundtrip)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        RegisterPrototypePythonModule();
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
            "assert abs(conductivity(2.0) - 4.4860945752) < 1e-12\n"), 0);
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
