#include "Core/dspch.hpp"

#include "IO/JFMPythonImportBridge.hpp"

#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonGilScope.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"
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
	class PyObjectRef
	{
	public:
		PyObjectRef() = default;
		explicit PyObjectRef(PyObject *object)
			: m_Object(object)
		{
		}

		PyObjectRef(const PyObjectRef &) = delete;
		PyObjectRef &operator=(const PyObjectRef &) = delete;

		PyObjectRef(PyObjectRef &&other) noexcept
			: m_Object(other.m_Object)
		{
			other.m_Object = nullptr;
		}

		PyObjectRef &operator=(PyObjectRef &&other) noexcept
		{
			if (this == &other)
				return *this;
			Py_XDECREF(m_Object);
			m_Object = other.m_Object;
			other.m_Object = nullptr;
			return *this;
		}

		~PyObjectRef()
		{
			Py_XDECREF(m_Object);
		}

		[[nodiscard]] PyObject *get() const
		{
			return m_Object;
		}

		[[nodiscard]] explicit operator bool() const
		{
			return m_Object != nullptr;
		}

	private:
		PyObject *m_Object = nullptr;
	};

	Result<void> ensurePythonRuntimeStarted()
	{
		static PythonInterpreter interpreter;
		static bool started = false;
		if (started)
			return {};

		Result<void> result = interpreter.Start();
		if (!result)
			return result.Error();

		started = true;
		return {};
	}

	std::string describePythonError()
	{
		if (!PyErr_Occurred())
			return "Unknown Python error.";

		PyObject *type = nullptr;
		PyObject *value = nullptr;
		PyObject *traceback = nullptr;
		PyErr_Fetch(&type, &value, &traceback);
		PyErr_NormalizeException(&type, &value, &traceback);
		PyObjectRef typeRef(type);
		PyObjectRef valueRef(value);
		PyObjectRef tracebackRef(traceback);

		std::string message;
		if (typeRef)
		{
			PyObjectRef typeName(PyObject_GetAttrString(typeRef.get(), "__name__"));
			if (typeName)
			{
				PyObjectRef utf8(PyUnicode_AsUTF8String(typeName.get()));
				if (utf8)
					message += PyBytes_AsString(utf8.get());
			}
		}

		if (valueRef)
		{
			PyObjectRef valueText(PyObject_Str(valueRef.get()));
			if (valueText)
			{
				PyObjectRef utf8(PyUnicode_AsUTF8String(valueText.get()));
				if (utf8)
				{
					if (!message.empty())
						message += ": ";
					message += PyBytes_AsString(utf8.get());
				}
			}
		}

		return message.empty() ? "Unknown Python error." : message;
	}

	std::string parserNameForPath(const Path &path)
	{
		const std::string extension = path.extension().String();
		if (extension == ".iv" || extension == ".IV")
			return "scaps_iv";
		return "dat";
	}

	std::string pythonStringLiteral(const std::string &value)
	{
		std::string result = "'";
		for (const char character : value)
		{
			if (character == '\\')
				result += "\\\\";
			else if (character == '\'')
				result += "\\'";
			else if (character == '\n')
				result += "\\n";
			else
				result += character;
		}
		result += "'";
		return result;
	}

	bool appendStringToSysPath(const std::string &path)
	{
		if (path.empty())
			return true;

		const std::string script =
			"import sys\n"
			"_sst_path = " + pythonStringLiteral(path) + "\n"
			"if _sst_path not in sys.path:\n"
			"    sys.path.insert(0, _sst_path)\n";
		return PyRun_SimpleString(script.c_str()) == 0;
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

	PyObject *dictGet(PyObject *dict, const char *key)
	{
		if (dict == nullptr || !PyDict_Check(dict))
			return nullptr;
		return PyDict_GetItemString(dict, key);
	}

	std::string pyString(PyObject *value)
	{
		if (value == nullptr || value == Py_None)
			return {};

		PyObjectRef text(PyObject_Str(value));
		if (!text)
			return {};

		PyObjectRef utf8(PyUnicode_AsUTF8String(text.get()));
		if (!utf8)
			return {};

		const char *raw = PyBytes_AsString(utf8.get());
		return raw == nullptr ? std::string{} : std::string(raw);
	}

	std::optional<double> pyOptionalDouble(PyObject *value)
	{
		if (value == nullptr || value == Py_None)
			return std::nullopt;

		const double number = PyFloat_AsDouble(value);
		if (PyErr_Occurred())
			return std::nullopt;
		return number;
	}

	std::vector<double> extractColumn(PyObject *rows, std::size_t columnIndex)
	{
		std::vector<double> values;
		if (rows == nullptr || !PySequence_Check(rows))
			return values;

		const Py_ssize_t rowCount = PySequence_Size(rows);
		if (rowCount <= 0)
			return values;
		values.reserve(static_cast<std::size_t>(rowCount));

		for (Py_ssize_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
		{
			PyObjectRef row(PySequence_GetItem(rows, rowIndex));
			if (!row || !PySequence_Check(row.get()))
				continue;

			const Py_ssize_t columnCount = PySequence_Size(row.get());
			if (columnIndex >= static_cast<std::size_t>(columnCount))
				continue;

			PyObjectRef value(PySequence_GetItem(row.get(), static_cast<Py_ssize_t>(columnIndex)));
			if (!value)
				continue;

			const double number = PyFloat_AsDouble(value.get());
			if (!PyErr_Occurred())
				values.push_back(number);
			else
				PyErr_Clear();
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

	JFMCharacteristic toCharacteristic(const Path &path, const std::string &parser, PyObject *curve)
	{
		JFMCharacteristic characteristic;
		characteristic.name = pyString(dictGet(curve, "name"));
		characteristic.measurement = pyString(dictGet(curve, "measurement"));
		characteristic.sourcePath = path.String();
		characteristic.parser = parser;

		PyObject *metadata = dictGet(curve, "metadata");
		if (metadata != nullptr && PyDict_Check(metadata))
		{
			PyObject *key = nullptr;
			PyObject *value = nullptr;
			Py_ssize_t position = 0;
			while (PyDict_Next(metadata, &position, &key, &value))
				characteristic.metadata[pyString(key)] = pyString(value);
		}
		characteristic.temperatureK = pyOptionalDouble(dictGet(metadata, "temperature_K"));
		characteristic.temperatureSource = pyString(dictGet(metadata, "temperature_source"));

		PyObject *columns = dictGet(curve, "columns");
		if (columns != nullptr && PySequence_Check(columns))
		{
			const Py_ssize_t count = PySequence_Size(columns);
			for (Py_ssize_t index = 0; index < count; ++index)
			{
				PyObjectRef column(PySequence_GetItem(columns, index));
				if (!column)
					continue;

				characteristic.columns.push_back(JFMColumn{
					pyString(dictGet(column.get(), "name")),
					pyString(dictGet(column.get(), "unit")),
					pyString(dictGet(column.get(), "role"))});
			}
		}

		PyObject *sweep = dictGet(curve, "sweep");
		characteristic.sweep.direction = pyString(dictGet(sweep, "direction"));
		characteristic.sweep.vStart = pyOptionalDouble(dictGet(sweep, "v_start"));
		characteristic.sweep.vStop = pyOptionalDouble(dictGet(sweep, "v_stop"));
		PyObject *pointCount = dictGet(sweep, "n_points");
		if (pointCount != nullptr)
			characteristic.sweep.pointCount = static_cast<std::size_t>(PyLong_AsUnsignedLongLong(pointCount));

		PyObject *warnings = dictGet(curve, "warnings");
		if (warnings != nullptr && PySequence_Check(warnings))
		{
			const Py_ssize_t count = PySequence_Size(warnings);
			for (Py_ssize_t index = 0; index < count; ++index)
			{
				PyObjectRef warning(PySequence_GetItem(warnings, index));
				if (warning)
					characteristic.qc.warnings.push_back(pyString(warning.get()));
			}
		}
		characteristic.qc.hasMissingTemperature = !characteristic.temperatureK.has_value();
		characteristic.qc.nonMonotonicSweep =
			std::find(characteristic.qc.warnings.begin(), characteristic.qc.warnings.end(), "non_monotonic_sweep")
				!= characteristic.qc.warnings.end()
			|| std::find(characteristic.qc.warnings.begin(), characteristic.qc.warnings.end(), "nonmonotonic_sweep")
				!= characteristic.qc.warnings.end();

		PyObjectRef rows(PyObject_CallMethod(dictGet(curve, "data"), "tolist", nullptr));
		const std::size_t voltageColumn = findColumnByRole(characteristic.columns, "voltage", 0);
		const std::size_t currentColumn = findColumnByRole(characteristic.columns, "current", 1);
		const std::size_t currentDensityColumn = findColumnByRole(characteristic.columns, "current_density", currentColumn);
		characteristic.voltage = extractColumn(rows.get(), voltageColumn);
		characteristic.current = extractColumn(rows.get(), currentColumn);
		characteristic.currentDensity = extractColumn(rows.get(), currentDensityColumn);
		return characteristic;
	}

	PyObjectRef importParserForPath(const Path &path)
	{
		const char *moduleName = parserNameForPath(path) == "scaps_iv"
			? "jfm_import.scaps_iv_parser"
			: "jfm_import.dat_parser";
		const char *functionName = parserNameForPath(path) == "scaps_iv"
			? "parse_scaps_iv"
			: "parse_dat";

		PyObjectRef module(PyImport_ImportModule(moduleName));
		if (!module)
			return {};

		return PyObjectRef(PyObject_GetAttrString(module.get(), functionName));
	}

	Result<JFMImportBatchResult> importWithPython(const Path &pythonRoot, const std::vector<Path> &paths, const JFMPythonImportBridge::ProgressCallback &progressCallback)
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

			PyObjectRef parserFunction = importParserForPath(path);
			if (!parserFunction || !PyCallable_Check(parserFunction.get()))
			{
				const std::string error = describePythonError();
				return makePythonBridgeError(
					"Python import failed.",
					"File: " + path.String()
						+ "\nPython root: " + pythonRoot.String()
						+ "\nParser: " + parser
						+ "\nError: " + error);
			}

			PyObjectRef pathArgument(PyUnicode_FromString(path.String().c_str()));
			PyObjectRef result(PyObject_CallFunctionObjArgs(parserFunction.get(), pathArgument.get(), nullptr));
			if (!result || !PyDict_Check(result.get()))
			{
				const std::string error = describePythonError();
				return makePythonBridgeError(
					"Python import failed.",
					"File: " + path.String()
						+ "\nPython root: " + pythonRoot.String()
						+ "\nParser: " + parser
						+ "\nError: " + error);
			}

			const std::string parserName = pyString(dictGet(result.get(), "parser"));
			PyObject *warnings = dictGet(result.get(), "warnings");
			if (warnings != nullptr && PySequence_Check(warnings))
			{
				const Py_ssize_t count = PySequence_Size(warnings);
				for (Py_ssize_t warningIndex = 0; warningIndex < count; ++warningIndex)
				{
					PyObjectRef warning(PySequence_GetItem(warnings, warningIndex));
					if (warning)
						batch.warnings.push_back(pyString(warning.get()));
				}
			}

			PyObject *curves = dictGet(result.get(), "curves");
			if (curves == nullptr || !PySequence_Check(curves))
			{
				return makePythonBridgeError(
					"Python import failed.",
					"File: " + path.String()
						+ "\nPython root: " + pythonRoot.String()
						+ "\nParser: " + parser
						+ "\nError: Parser returned no curves sequence.");
			}

			const Py_ssize_t curveCount = PySequence_Size(curves);
			for (Py_ssize_t curveIndex = 0; curveIndex < curveCount; ++curveIndex)
			{
				PyObjectRef curve(PySequence_GetItem(curves, curveIndex));
				if (curve && PyDict_Check(curve.get()))
					batch.characteristics.push_back(toCharacteristic(path, parserName, curve.get()));
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

		Result<void> runtime = ensurePythonRuntimeStarted();
		if (!runtime)
			return runtime.Error();

		PythonGilAcquireScope gil;
		if (!gil.IsActive())
			return makePythonBridgeError("Python import failed.", "Python GIL could not be acquired.");

		return importWithPython(m_PythonRoot, paths, progressCallback);
#else
		(void)paths;
		(void)progressCallback;
		return makePythonBridgeError(
			"Python import is unavailable in this build.",
			"DS_PYTHON_CAPI_AVAILABLE is 0, so JFM cannot call embedded Python.");
#endif
	}
} // namespace SST::Prototypes::JFM
