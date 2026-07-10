#include "Core/dspch.hpp"

#include "IO/JFMPythonBindings.hpp"

#include "Core/Logging/Logger.hpp"
#include "Domain/JFMTypes.hpp"
#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>
#endif

#if DS_PYTHON_CAPI_AVAILABLE
namespace nb = nanobind;

NB_MODULE(sst_jfm, module)
{
	using namespace SST::Prototypes::JFM;
	module.attr("schema_version") = JFMProjectSchemaVersion;

	nb::class_<JFMColumn>(module, "JFMColumn")
		.def(nb::init<>())
		.def_rw("name", &JFMColumn::name)
		.def_rw("unit", &JFMColumn::unit)
		.def_rw("role", &JFMColumn::role);

	nb::class_<JFMSweepInfo>(module, "JFMSweepInfo")
		.def(nb::init<>())
		.def_rw("direction", &JFMSweepInfo::direction)
		.def_rw("v_start", &JFMSweepInfo::vStart)
		.def_rw("v_stop", &JFMSweepInfo::vStop)
		.def_rw("n_points", &JFMSweepInfo::pointCount);

	nb::class_<JFMQualityControl>(module, "JFMQualityControl")
		.def(nb::init<>())
		.def_rw("non_monotonic_sweep", &JFMQualityControl::nonMonotonicSweep)
		.def_rw("has_missing_temperature", &JFMQualityControl::hasMissingTemperature)
		.def_rw("warnings", &JFMQualityControl::warnings);

	nb::class_<JFMCharacteristicInfo>(module, "JFMCharacteristicInfo")
		.def(nb::init<>())
		.def_rw("id", &JFMCharacteristicInfo::id)
		.def_rw("name", &JFMCharacteristicInfo::name)
		.def_rw("measurement", &JFMCharacteristicInfo::measurement)
		.def_prop_rw(
			"source_path",
			[](const JFMCharacteristicInfo &info) { return info.sourcePath.String(); },
			[](JFMCharacteristicInfo &info, const std::string &path) { info.sourcePath = SST::Path::FromResolved(path); })
		.def_rw("parser", &JFMCharacteristicInfo::parser)
		.def_rw("temperature_K", &JFMCharacteristicInfo::temperatureK)
		.def_rw("temperature_source", &JFMCharacteristicInfo::temperatureSource)
		.def_rw("columns", &JFMCharacteristicInfo::columns)
		.def_rw("sweep", &JFMCharacteristicInfo::sweep)
		.def_rw("qc", &JFMCharacteristicInfo::qc)
		.def_rw("metadata", &JFMCharacteristicInfo::metadata);

	nb::class_<JFMCharacteristicData>(module, "JFMCharacteristicData")
		.def(nb::init<>())
		.def_rw("voltage", &JFMCharacteristicData::voltage)
		.def_rw("current", &JFMCharacteristicData::current)
		.def_rw("current_density", &JFMCharacteristicData::currentDensity);

	nb::class_<JFMCharacteristic>(module, "JFMCharacteristic")
		.def(nb::init<>())
		.def_rw("info", &JFMCharacteristic::info)
		.def_rw("data", &JFMCharacteristic::data);

	nb::class_<JFMSeries>(module, "JFMSeries")
		.def(nb::init<>())
		.def_rw("id", &JFMSeries::id)
		.def_rw("name", &JFMSeries::name)
		.def_rw("characteristic_ids", &JFMSeries::characteristicIds);
}
#endif

namespace SST::Prototypes::JFM
{
	bool RegisterJFMPythonBindings()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		static bool registered = false;
		if (registered)
			return true;

		if (PythonInterpreter::IsRuntimeInitialized() || Py_IsInitialized())
		{
			DS_LOG_WARN("JFM Python bindings registration skipped: Python runtime is already initialized");
			return false;
		}

		if (PyImport_AppendInittab("sst_jfm", PyInit_sst_jfm) != 0)
		{
			DS_LOG_ERROR("JFM Python bindings registration failed");
			return false;
		}

		registered = true;
		return true;
#else
		return false;
#endif
	}
} // namespace SST::Prototypes::JFM
