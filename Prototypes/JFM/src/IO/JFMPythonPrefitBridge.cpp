#include "Core/dspch.hpp"

#include "IO/JFMPythonPrefitBridge.hpp"

#include "IO/JFMPythonRuntime.hpp"
#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include <nanobind/nanobind.h>

#include <mutex>
#endif

namespace SST::Prototypes::JFM
{
	namespace
	{
		StructuredError makePrefitError(std::string details)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"JFM prefit failed.",
				std::move(details),
				"Check the characteristic data, temperature, and embedded Python environment.",
				"JFMPythonPrefitBridge",
				"jfm.python_prefit"};
		}

#if DS_PYTHON_CAPI_AVAILABLE
		namespace nb = nanobind;

		nb::object dictItem(const nb::dict &dict, const char *key)
		{
			return dict.contains(key) ? nb::borrow<nb::object>(dict[key].ptr()) : nb::none();
		}

		std::string pyString(nb::handle value)
		{
			if (!value.is_valid() || value.is_none())
				return {};
			const nb::str text(value);
			const char *raw = text.c_str();
			return raw == nullptr ? std::string{} : std::string(raw);
		}

		std::vector<std::string> stringList(nb::handle value)
		{
			std::vector<std::string> values;
			if (!value.is_valid() || value.is_none() || !nb::isinstance<nb::list>(value))
				return values;
			for (nb::handle item : nb::borrow<nb::list>(value))
				values.push_back(pyString(item));
			return values;
		}

		std::vector<double> doubleList(nb::handle value)
		{
			std::vector<double> values;
			if (!value.is_valid() || value.is_none() || !nb::isinstance<nb::list>(value))
				return values;
			for (nb::handle item : nb::borrow<nb::list>(value))
				values.push_back(nb::cast<double>(item));
			return values;
		}

		nb::list makeDoubleList(const std::vector<double> &values)
		{
			nb::list result;
			for (double value : values)
				result.append(nb::float_(value));
			return result;
		}

		Result<JFMPrefitBridgeResult> runWithPython(const Path &pythonRoot, const JFMCharacteristicRecord &characteristic)
		{
			if (!characteristic.data || characteristic.data->voltage.empty())
				return makePrefitError("Raw characteristic arrays are unavailable.");
			if (!characteristic.info.temperatureK)
				return makePrefitError("Characteristic temperature is missing.");
			if (!ConfigureJFMPythonPaths(pythonRoot))
				return makePrefitError("Could not configure Python paths for " + pythonRoot.String());

			const JFMPrefitState requested = characteristic.info.prefit.value_or(JFMPrefitState{});
			const bool useDensity = !characteristic.data->currentDensity.empty();
			const std::vector<double> &current = useDensity ? characteristic.data->currentDensity : characteristic.data->current;
			if (current.size() != characteristic.data->voltage.size())
				return makePrefitError("Voltage and current arrays have different lengths.");

			nb::dict rangeOptions;
			if (requested.manualLowerVoltage && requested.manualUpperVoltage)
			{
				rangeOptions["manual_lower_voltage"] = nb::float_(*requested.manualLowerVoltage);
				rangeOptions["manual_upper_voltage"] = nb::float_(*requested.manualUpperVoltage);
			}
			nb::dict options;
			options["range_options"] = rangeOptions;
			const bool light = requested.modelId.find("I_ph") != std::string::npos;

			nb::object adapterResult = nb::module_::import_("jfm_prefit.cpp_adapter")
				.attr("prefit_characteristic")(
					makeDoubleList(characteristic.data->voltage),
					makeDoubleList(current),
					nb::float_(*characteristic.info.temperatureK),
					nb::str(requested.modelId.c_str()),
					nb::str(light ? "light" : "dark"),
					nb::str(useDensity ? "A/cm2" : "A"),
					options);
			if (!nb::isinstance<nb::dict>(adapterResult))
				return makePrefitError("Python adapter returned a non-dict result.");
			const nb::dict result = nb::borrow<nb::dict>(adapterResult);

			JFMPrefitBridgeResult output;
			output.state.modelId = pyString(dictItem(result, "model_id"));
			output.state.manualLowerVoltage = requested.manualLowerVoltage;
			output.state.manualUpperVoltage = requested.manualUpperVoltage;
			output.state.warnings = stringList(dictItem(result, "warnings"));
			output.curve.voltage = doubleList(dictItem(result, "voltage"));
			output.curve.current = doubleList(dictItem(result, "prefit_current"));

			const nb::object rangeObject = dictItem(result, "fit_range");
			if (!nb::isinstance<nb::dict>(rangeObject))
				return makePrefitError("Python adapter returned no fit_range map.");
			const nb::dict range = nb::borrow<nb::dict>(rangeObject);
			output.state.fitRange.lowerIndex = nb::cast<std::size_t>(dictItem(range, "lo"));
			output.state.fitRange.upperIndex = nb::cast<std::size_t>(dictItem(range, "hi"));
			output.state.fitRange.lowerVoltage = nb::cast<double>(dictItem(range, "voltage_min"));
			output.state.fitRange.upperVoltage = nb::cast<double>(dictItem(range, "voltage_max"));
			output.state.fitRange.reason = pyString(dictItem(range, "reason"));

			const nb::object parametersObject = dictItem(result, "parameters");
			if (!nb::isinstance<nb::dict>(parametersObject))
				return makePrefitError("Python adapter returned no parameters map.");
			for (const auto &[nameHandle, estimateHandle] : nb::borrow<nb::dict>(parametersObject))
			{
				if (!nb::isinstance<nb::dict>(estimateHandle))
					continue;
				const nb::dict estimateDict = nb::borrow<nb::dict>(estimateHandle);
				JFMParameterEstimate estimate;
				estimate.value = nb::cast<double>(dictItem(estimateDict, "value"));
				const nb::object standardError = dictItem(estimateDict, "stderr");
				if (!standardError.is_none())
					estimate.standardError = nb::cast<double>(standardError);
				estimate.source = pyString(dictItem(estimateDict, "source"));
				estimate.warnings = stringList(dictItem(estimateDict, "warnings"));
				output.state.parameters[pyString(nameHandle)] = std::move(estimate);
			}
			if (output.curve.voltage.size() != output.curve.current.size())
				return makePrefitError("Python adapter returned mismatched prefit curve arrays.");
			return output;
		}
#endif
	}

	JFMPythonPrefitBridge::JFMPythonPrefitBridge(Path pythonRoot)
		: m_PythonRoot(std::move(pythonRoot))
	{
	}

	Result<JFMPrefitBridgeResult> JFMPythonPrefitBridge::Run(const JFMCharacteristicRecord &characteristic) const
	{
#if DS_PYTHON_CAPI_AVAILABLE
		static std::mutex pythonMutex;
		std::scoped_lock lock(pythonMutex);
		if (m_PythonRoot.Empty() || !FileSystem::Exists(m_PythonRoot.Native()))
			return makePrefitError("Python root was not found: " + m_PythonRoot.String());
		if (!PythonInterpreter::IsRuntimeInitialized())
			return makePrefitError("Embedded Python runtime is not initialized.");
		if (!PyGILState_Check())
			return makePrefitError("Python GIL is not held by the current prefit job.");
		try
		{
			return runWithPython(m_PythonRoot, characteristic);
		}
		catch (const nb::python_error &error)
		{
			return makePrefitError(error.what());
		}
		catch (const std::exception &error)
		{
			return makePrefitError(error.what());
		}
#else
		(void)characteristic;
		return makePrefitError("Embedded Python is unavailable in this build.");
#endif
	}
}
