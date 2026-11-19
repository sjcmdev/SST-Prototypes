#include "Core/dspch.hpp"

#include "IO/JFMPythonBindings.hpp"

#include "Core/Logging/Logger.hpp"
#include "Domain/JFMTypes.hpp"
#include "IO/JFMPlotExport.hpp"
#include "IO/JFMFitJobs.hpp"
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
    module.def("plot", [](nb::dict request) {
        auto json = nb::module_::import_("json");
        nlohmann::json result;
        try {
            const auto input = nlohmann::json::parse(nb::cast<std::string>(json.attr("dumps")(request)));
            nb::gil_scoped_release release;
            result = RunJFMPlotRequest(input);
        } catch (const std::exception &e) {
            result = {{"ok", false}, {"error", {{"code", "jfm.plot.validation"}, {"message", e.what()}}}};
        }
        return json.attr("loads")(nb::str(result.dump().c_str()));
    });

    module.def("fit", [](nb::dict request, const std::string &method) {
        auto json = nb::module_::import_("json");
        nlohmann::json result;
        try {
            auto input = nlohmann::json::parse(nb::cast<std::string>(json.attr("dumps")(request)));
            const bool density = input.contains("current_density_A_per_cm2");
            if (density && input.contains("current_A")) throw std::invalid_argument("Choose current_A or current_density_A_per_cm2");
            if (density) { input["current_A"] = std::move(input["current_density_A_per_cm2"]); input.erase("current_density_A_per_cm2"); }
            // Preserve GUI density values: converting to A would change the current log-ratio weights.
            const std::pair<const char *, const char *> fields[] = {
                {"I0", density ? "I0_A_per_cm2" : "I0_A"}, {"A", "ideality_factor"},
                {"Rs", density ? "Rs_ohm_cm2" : "Rs_ohm"}, {"Rsh", density ? "Rsh_ohm_cm2" : "Rsh_ohm"},
                {"alpha", "alpha"}, {"Rsh2", density ? "Rsh2_V_pow_alpha_cm2_per_A" : "Rsh2_V_pow_alpha_per_A"},
                {"I_ph", density ? "Iph_A_per_cm2" : "Iph_A"}};
            auto field = [&](const std::string &name, bool incoming) -> std::string {
                for (const auto &[id, unitName] : fields)
                    if (name == (incoming ? unitName : id)) return incoming ? id : unitName;
                throw std::invalid_argument("Unknown parameter field: " + name);
            };
            input["method"] = method;
            for (const char *key : {"parameters", "bounds"}) if (input.contains(key)) {
                auto values = nlohmann::json::object();
                for (const auto &[name, value] : input.at(key).items()) values[field(name, true)] = value;
                input[key] = std::move(values);
            }
            if (input.contains("fixed_parameters"))
                for (auto &name : input.at("fixed_parameters")) name = field(name.get<std::string>(), true);
            { nb::gil_scoped_release release; result = RunJFMFitRequest(input); }
            if (result.contains("parameters")) {
                auto values = nlohmann::json::object();
                for (const auto &[name, value] : result.at("parameters").items()) values[field(name, false)] = value;
                result["parameters"] = std::move(values);
            }
            if (density) for (const auto &[from, to] : {std::pair{"ideal_current_A", "ideal_current_density_A_per_cm2"},
                                                         std::pair{"noisy_current_A", "noisy_current_density_A_per_cm2"}})
                if (result.contains(from)) { result[to] = std::move(result[from]); result.erase(from); }
            if (result.contains("parameter_names"))
                for (auto &name : result.at("parameter_names")) name = field(name.get<std::string>(), false);
        } catch (const std::exception &error) {
            result = {{"ok", false}, {"error", {{"code", "jfm.fit.validation"}, {"message", error.what()}}}};
        }
        return json.attr("loads")(nb::str(result.dump().c_str()));
    }, nb::arg("request"), nb::arg("method") = "lm");

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
			[](const JFMCharacteristicInfo &info) { return info.sourcePath.Utf8(); },
			[](JFMCharacteristicInfo &info, const std::string &path) { info.sourcePath = SST::Path::FromResolved(std::filesystem::u8path(path)); })
		.def_rw("source_record_key", &JFMCharacteristicInfo::sourceRecordKey)
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

	nb::class_<JFMGroup>(module, "JFMGroup")
		.def(nb::init<>())
		.def_rw("id", &JFMGroup::id)
		.def_rw("name", &JFMGroup::name)
		.def_rw("parent_id", &JFMGroup::parentId);

	nb::class_<JFMSeries>(module, "JFMSeries")
		.def(nb::init<>())
		.def_rw("id", &JFMSeries::id)
		.def_rw("name", &JFMSeries::name)
		.def_rw("group_id", &JFMSeries::groupId)
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
