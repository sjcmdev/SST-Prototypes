#include "Core/dspch.hpp"

#include "IO/JFMPythonImportBridge.hpp"

#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
//! Error: Te importy nie powinny się tutaj znajdować. Komunikacja z pythonem powinna się odbywać przez moduł z Core
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <Python.h>
#endif


namespace SST::Prototypes::JFM
{

	StructuredError makePythonBridgeError(std::string message, std::string details)
	{
		return StructuredError{
			ErrorCategory::Python,
			Severity::Error,
			std::move(message),
			std::move(details),
			"Enable embedded Python/nanobind for the JFM project and make sure Prototypes/JFM/python is on sys.path.",
			"JFMPythonImportBridge",
			"jfm.python_import"};
	}

#if DS_PYTHON_CAPI_AVAILABLE
	namespace nb = nanobind; 

	std::string asStringOrEmpty(nb::handle value)
	{
		if (value.is_none())
			return {};
		return nb::cast<std::string>(nb::str(value));
	}

	std::optional<double> asOptionalDouble(nb::handle value)
	{
		if (value.is_none())
			return std::nullopt;
		return nb::cast<double>(value);
	}

	void appendPythonRootToPath(const Path &pythonRoot)
	{
		nb::module_ sys = nb::module_::import_("sys");
		nb::list path = nb::cast<nb::list>(sys.attr("path"));
		const std::string root = pythonRoot.String();
		path.attr("insert")(0, root);
	}

	nb::object importParserForPath(const Path &path)
	{
		const std::string extension = path.extension().String();
		if (extension == ".iv" || extension == ".IV")
			return nb::module_::import_("jfm_import.scaps_iv_parser").attr("parse_scaps_iv");
		return nb::module_::import_("jfm_import.dat_parser").attr("parse_dat");
	}

	std::vector<double> extractColumn(nb::list rows, std::size_t columnIndex)
	{
		std::vector<double> values;
		values.reserve(nb::len(rows));
		for (nb::handle rowHandle : rows)
		{
			nb::list row = nb::cast<nb::list>(rowHandle);
			if (columnIndex < static_cast<std::size_t>(nb::len(row)))
				values.push_back(nb::cast<double>(row[columnIndex]));
		}
		return values;
	}

	std::size_t findColumnByRole(const std::vector<JFMColumn> &columns, const std::string &role, std::size_t fallback)
	{
		for (std::size_t index = 0; index < columns.size(); ++index)
		{
			if (columns[index].role == role)
				return index;
		}
		return fallback;
	}

	JFMCharacteristic toCharacteristic(const Path &path, const std::string &parser, nb::dict curve)
	{
		JFMCharacteristic characteristic;
		characteristic.name = nb::cast<std::string>(curve["name"]);
		characteristic.measurement = nb::cast<std::string>(curve["measurement"]);
		characteristic.sourcePath = path.String();
		characteristic.parser = parser;

		nb::dict metadata = nb::cast<nb::dict>(curve["metadata"]);
		for (auto item : metadata)
			characteristic.metadata[nb::cast<std::string>(item.first)] = asStringOrEmpty(item.second);
		characteristic.temperatureK = asOptionalDouble(metadata["temperature_K"]);
		characteristic.temperatureSource = asStringOrEmpty(metadata["temperature_source"]);

		nb::list columns = nb::cast<nb::list>(curve["columns"]);
		for (nb::handle columnHandle : columns)
		{
			nb::dict column = nb::cast<nb::dict>(columnHandle);
			characteristic.columns.push_back(JFMColumn{
				asStringOrEmpty(column["name"]),
				asStringOrEmpty(column["unit"]),
				asStringOrEmpty(column["role"])});
		}

		nb::dict sweep = nb::cast<nb::dict>(curve["sweep"]);
		characteristic.sweep.direction = asStringOrEmpty(sweep["direction"]);
		characteristic.sweep.vStart = asOptionalDouble(sweep["v_start"]);
		characteristic.sweep.vStop = asOptionalDouble(sweep["v_stop"]);
		characteristic.sweep.pointCount = static_cast<std::size_t>(nb::cast<int>(sweep["n_points"]));

		nb::list warnings = nb::cast<nb::list>(curve["warnings"]);
		for (nb::handle warning : warnings)
			characteristic.qc.warnings.push_back(nb::cast<std::string>(warning));
		characteristic.qc.hasMissingTemperature = !characteristic.temperatureK.has_value();
		characteristic.qc.nonMonotonicSweep =
			std::find(characteristic.qc.warnings.begin(), characteristic.qc.warnings.end(), "nonmonotonic_sweep")
			!= characteristic.qc.warnings.end();

		nb::list rows = nb::cast<nb::list>(curve["data"].attr("tolist")());
		const std::size_t voltageColumn = findColumnByRole(characteristic.columns, "voltage", 0);
		const std::size_t currentColumn = findColumnByRole(characteristic.columns, "current", 1);
		const std::size_t currentDensityColumn = findColumnByRole(characteristic.columns, "current_density", currentColumn);
		characteristic.voltage = extractColumn(rows, voltageColumn);
		characteristic.current = extractColumn(rows, currentColumn);
		characteristic.currentDensity = extractColumn(rows, currentDensityColumn);
		return characteristic;
	}
#endif


	JFMPythonImportBridge::JFMPythonImportBridge(Path pythonRoot)
		: m_PythonRoot(std::move(pythonRoot))
	{
	}

	bool JFMPythonImportBridge::IsAvailable()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		return true;
#else
		return false;
#endif
	}

	Result<JFMImportBatchResult> JFMPythonImportBridge::ImportFiles(const std::vector<Path> &paths, const ProgressCallback &progressCallback)
	{
#if DS_PYTHON_CAPI_AVAILABLE
		static std::mutex pythonMutex;
		std::scoped_lock lock(pythonMutex);

		try
		{
			if (!Py_IsInitialized())
				Py_InitializeEx(0);
			if (!Py_IsInitialized())
				return makePythonBridgeError("Python import failed.", "Py_InitializeEx did not initialize Python.");

			nb::gil_scoped_acquire gil;
			appendPythonRootToPath(m_PythonRoot);

			JFMImportBatchResult batch;
			const std::size_t total = paths.size();
			for (std::size_t index = 0; index < paths.size(); ++index)
			{
				const Path &path = paths[index];
				if (progressCallback)
					progressCallback(index, total, "Importing " + path.filename().String());

				nb::object parser = importParserForPath(path);
				nb::dict result = nb::cast<nb::dict>(parser(path.String()));
				const std::string parserName = nb::cast<std::string>(result["parser"]);
				nb::list warnings = nb::cast<nb::list>(result["warnings"]);
				for (nb::handle warning : warnings)
					batch.warnings.push_back(nb::cast<std::string>(warning));

				nb::list curves = nb::cast<nb::list>(result["curves"]);
				for (nb::handle curve : curves)
					batch.characteristics.push_back(toCharacteristic(path, parserName, nb::cast<nb::dict>(curve)));
			}

			if (progressCallback)
				progressCallback(total, total, "Import complete");
			return batch;
		}
		catch (const std::exception &error)
		{
			return makePythonBridgeError("Python import failed.", error.what());
		}
#else
		(void)paths;
		(void)progressCallback;
		return makePythonBridgeError(
			"Python import is unavailable in this build.",
			"DS_PYTHON_CAPI_AVAILABLE is 0, so JFM cannot call nanobind.");
#endif
	}
} // namespace SST::Prototypes::JFM
