#include "Core/dspch.hpp"

#include "IO/JFMPythonImportBridge.hpp"

#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include <mutex>
#include <optional>
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
			"Enable embedded Python for the JFM project and make sure Prototypes/JFM/python is on sys.path.",
			"JFMPythonImportBridge",
			"jfm.python_import"};
	}

#if DS_PYTHON_CAPI_AVAILABLE
	namespace nb = nanobind;

	std::string parserNameForPath(const Path &path)
	{
		const std::string extension = path.extension().String();
		if (extension == ".iv" || extension == ".IV")
			return "scaps_iv";
		return "dat";
	}

	StructuredError makeImportFileError(
		const Path &path,
		const Path &pythonRoot,
		const std::string &parser,
		const std::string &error)
	{
		return makePythonBridgeError(
			"Python import failed.",
			"File: " + path.String()
				+ "\nPython root: " + pythonRoot.String()
				+ "\nParser: " + parser
				+ "\nError: " + error);
	}

	std::string pyString(nb::handle value)
	{
		if (!value.is_valid() || value.is_none())
			return {};

		const nb::str text(value);
		const char *raw = text.c_str();
		return raw == nullptr ? std::string{} : std::string(raw);
	}

	std::optional<double> pyOptionalDouble(nb::handle value)
	{
		if (!value.is_valid() || value.is_none())
			return std::nullopt;

		try
		{
			return nb::cast<double>(value);
		}
		catch (const std::exception &)
		{
			return std::nullopt;
		}
	}

	std::size_t pySize(nb::handle value)
	{
		if (!value.is_valid() || value.is_none())
			return 0;

		try
		{
			return nb::cast<std::size_t>(value);
		}
		catch (const std::exception &)
		{
			return 0;
		}
	}

	nb::object dictItem(const nb::dict &dict, const char *key)
	{
		if (!dict.contains(key))
			return nb::none();
		return nb::borrow<nb::object>(dict[key].ptr());
	}

	std::optional<nb::dict> asDict(nb::handle value)
	{
		if (!value.is_valid() || value.is_none() || !nb::isinstance<nb::dict>(value))
			return std::nullopt;
		return nb::borrow<nb::dict>(value);
	}

	std::optional<nb::list> asList(nb::handle value)
	{
		if (!value.is_valid() || value.is_none() || !nb::isinstance<nb::list>(value))
			return std::nullopt;
		return nb::borrow<nb::list>(value);
	}

	bool appendStringToSysPath(const std::string &path)
	{
		if (path.empty())
			return true;

		nb::module_ sys = nb::module_::import_("sys");
		nb::list sysPath = nb::cast<nb::list>(sys.attr("path"));
		for (nb::handle item : sysPath)
		{
			if (pyString(item) == path)
				return true;
		}

		sysPath.insert(0, nb::str(path.c_str()));
		return true;
	}

	void appendVenvSitePackages(const Path &venvRoot)
	{
		if (venvRoot.Empty() || !FileSystem::Exists((venvRoot / Path("pyvenv.cfg")).Native()))
			return;

#if defined(DS_PLATFORM_WINDOWS)
		const Path sitePackages = venvRoot / Path("Lib") / Path("site-packages");
		if (FileSystem::Exists(sitePackages.Native()))
			(void)appendStringToSysPath(sitePackages.String());
#else
		const Path libRoot = venvRoot / Path("lib");
		if (!FileSystem::Exists(libRoot.Native()))
			return;

		std::error_code error;
		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(libRoot.Native(), error))
		{
			if (error || !entry.is_directory())
				continue;
			const Path sitePackages = Path::FromResolved(entry.path()) / Path("site-packages");
			if (FileSystem::Exists(sitePackages.Native()))
				(void)appendStringToSysPath(sitePackages.String());
		}
#endif
	}

	void appendNearbyVenvs(Path root)
	{
		for (int depth = 0; depth < 8 && !root.Empty(); ++depth)
		{
			appendVenvSitePackages(root / Path(".venv"));
			appendVenvSitePackages(root / Path("venv"));
			const Path parent = root.parent_path();
			if (parent == root)
				break;
			root = parent;
		}
	}

	bool configurePythonPaths(const Path &pythonRoot)
	{
		appendNearbyVenvs(Path::FromResolved(FileSystem::CurrentPath()));
		appendNearbyVenvs(pythonRoot);
		return appendStringToSysPath(pythonRoot.String());
	}

	std::vector<double> extractColumn(nb::ndarray<const double, nb::ndim<2>, nb::device::cpu> data, std::size_t columnIndex)
	{
		std::vector<double> values;
		if (!data.is_valid() || data.ndim() != 2 || columnIndex >= data.shape(1))
			return values;

		auto view = data.view();
		values.reserve(data.shape(0));
		for (std::size_t row = 0; row < data.shape(0); ++row)
			values.push_back(view(row, columnIndex));
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

	void readMetadata(JFMCharacteristic &characteristic, const nb::dict &metadata)
	{
		for (auto [key, value] : metadata)
			characteristic.metadata[pyString(key)] = pyString(value);

		characteristic.temperatureK = pyOptionalDouble(dictItem(metadata, "temperature_K"));
		characteristic.temperatureSource = pyString(dictItem(metadata, "temperature_source"));
	}

	void readColumns(JFMCharacteristic &characteristic, const nb::list &columns)
	{
		for (nb::handle columnValue : columns)
		{
			const std::optional<nb::dict> column = asDict(columnValue);
			if (!column)
				continue;

			characteristic.columns.push_back(JFMColumn{
				pyString(dictItem(*column, "name")),
				pyString(dictItem(*column, "unit")),
				pyString(dictItem(*column, "role"))});
		}
	}

	void readSweep(JFMCharacteristic &characteristic, const nb::dict &sweep)
	{
		characteristic.sweep.direction = pyString(dictItem(sweep, "direction"));
		characteristic.sweep.vStart = pyOptionalDouble(dictItem(sweep, "v_start"));
		characteristic.sweep.vStop = pyOptionalDouble(dictItem(sweep, "v_stop"));
		characteristic.sweep.pointCount = pySize(dictItem(sweep, "n_points"));
	}

	void readWarnings(JFMCharacteristic &characteristic, const nb::list &warnings)
	{
		for (nb::handle warning : warnings)
			characteristic.qc.warnings.push_back(pyString(warning));

		characteristic.qc.hasMissingTemperature = !characteristic.temperatureK.has_value();
		characteristic.qc.nonMonotonicSweep =
			std::find(characteristic.qc.warnings.begin(), characteristic.qc.warnings.end(), "non_monotonic_sweep")
				!= characteristic.qc.warnings.end()
			|| std::find(characteristic.qc.warnings.begin(), characteristic.qc.warnings.end(), "nonmonotonic_sweep")
				!= characteristic.qc.warnings.end();
	}

	JFMCharacteristic toCharacteristic(const Path &path, const std::string &parser, const nb::dict &curve)
	{
		JFMCharacteristic characteristic;
		characteristic.name = pyString(dictItem(curve, "name"));
		characteristic.measurement = pyString(dictItem(curve, "measurement"));
		characteristic.sourcePath = path.String();
		characteristic.parser = parser;

		if (const std::optional<nb::dict> metadata = asDict(dictItem(curve, "metadata")))
			readMetadata(characteristic, *metadata);
		if (const std::optional<nb::list> columns = asList(dictItem(curve, "columns")))
			readColumns(characteristic, *columns);
		if (const std::optional<nb::dict> sweep = asDict(dictItem(curve, "sweep")))
			readSweep(characteristic, *sweep);
		if (const std::optional<nb::list> warnings = asList(dictItem(curve, "warnings")))
			readWarnings(characteristic, *warnings);

		nb::ndarray<const double, nb::ndim<2>, nb::device::cpu> data =
			nb::cast<nb::ndarray<const double, nb::ndim<2>, nb::device::cpu>>(dictItem(curve, "data"));
		const std::size_t voltageColumn = findColumnByRole(characteristic.columns, "voltage", 0);
		const std::size_t currentColumn = findColumnByRole(characteristic.columns, "current", 1);
		const std::size_t currentDensityColumn = findColumnByRole(characteristic.columns, "current_density", currentColumn);
		characteristic.voltage = extractColumn(data, voltageColumn);
		characteristic.current = extractColumn(data, currentColumn);
		characteristic.currentDensity = extractColumn(data, currentDensityColumn);
		return characteristic;
	}

	nb::callable importParserForPath(const Path &path)
	{
		const bool scaps = parserNameForPath(path) == "scaps_iv";
		const char *moduleName = scaps ? "jfm_import.scaps_iv_parser" : "jfm_import.dat_parser";
		const char *functionName = scaps ? "parse_scaps_iv" : "parse_dat";
		return nb::cast<nb::callable>(nb::module_::import_(moduleName).attr(functionName));
	}

	Result<JFMImportBatchResult> importWithPython(
		const Path &pythonRoot,
		const std::vector<Path> &paths,
		const JFMPythonImportBridge::ProgressCallback &progressCallback)
	{
		if (!configurePythonPaths(pythonRoot))
			return makePythonBridgeError("Python import failed.", "Could not append pythonRoot to sys.path: " + pythonRoot.String());

		JFMImportBatchResult batch;
		const std::size_t total = paths.size();
		for (std::size_t index = 0; index < paths.size(); ++index)
		{
			const Path &path = paths[index];
			const std::string parser = parserNameForPath(path);
			if (progressCallback)
				progressCallback(index, total, "Importing " + path.filename().String());

			nb::dict result;
			try
			{
				nb::object parserResult = importParserForPath(path)(nb::str(path.String().c_str()));
				if (!nb::isinstance<nb::dict>(parserResult))
					return makeImportFileError(path, pythonRoot, parser, "Parser returned a non-dict result.");
				result = nb::borrow<nb::dict>(parserResult);
			}
			catch (const nb::python_error &error)
			{
				return makeImportFileError(path, pythonRoot, parser, error.what());
			}
			catch (const std::exception &error)
			{
				return makeImportFileError(path, pythonRoot, parser, error.what());
			}

			const std::string parserName = pyString(dictItem(result, "parser"));
			if (const std::optional<nb::list> warnings = asList(dictItem(result, "warnings")))
			{
				for (nb::handle warning : *warnings)
					batch.warnings.push_back(pyString(warning));
			}

			const std::optional<nb::list> curves = asList(dictItem(result, "curves"));
			if (!curves)
				return makeImportFileError(path, pythonRoot, parser, "Parser returned no curves list.");

			for (nb::handle curveValue : *curves)
			{
				const std::optional<nb::dict> curve = asDict(curveValue);
				if (curve)
					batch.characteristics.push_back(toCharacteristic(path, parserName, *curve));
			}
		}

		if (progressCallback)
			progressCallback(total, total, "Import complete");
		return batch;
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

		if (m_PythonRoot.Empty() || !FileSystem::Exists(m_PythonRoot.Native()))
		{
			return makePythonBridgeError(
				"Python import failed.",
				"Python root was not found: " + m_PythonRoot.String());
		}

		if (!PythonInterpreter::IsRuntimeInitialized())
			return makePythonBridgeError(
				"Python import failed.",
				"Core ScientificRuntimeLayer has not initialized the embedded Python runtime.");

		if (!PyGILState_Check())
			return makePythonBridgeError(
				"Python import failed.",
				"Python GIL is not held by the current job. Check JFMImportJob::UsesPythonRuntime() and ScientificRuntimeLayer job hooks.");

		try
		{
			return importWithPython(m_PythonRoot, paths, progressCallback);
		}
		catch (const nb::python_error &error)
		{
			return makePythonBridgeError("Python import failed.", error.what());
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
			"DS_PYTHON_CAPI_AVAILABLE is 0, so JFM cannot call embedded Python.");
#endif
	}
} // namespace SST::Prototypes::JFM
