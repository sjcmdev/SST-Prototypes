#pragma once

#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#if defined(_MSC_VER) && defined(_DEBUG) && !defined(Py_DEBUG)
#define SST_RESTORE_DEBUG_AFTER_PYTHON_H
#undef _DEBUG
#endif

#include <Python.h>

#if defined(SST_RESTORE_DEBUG_AFTER_PYTHON_H)
#define _DEBUG 1
#undef SST_RESTORE_DEBUG_AFTER_PYTHON_H
#endif
#endif
