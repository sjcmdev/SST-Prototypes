#include "Core/dspch.hpp"

#include "IO/JFMPythonImportBridge.hpp"

#include "IO/JFMPythonRuntime.hpp"

#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include <nanobind/nanobind.h>
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
		const std::string extension = path.extension().Utf8();
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
			"File: " + path.Utf8()
				+ "\nPython root: " + pythonRoot.Utf8()
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

	nb::object dictItem(const nb::dict &dict, const char *key)
	{
		if (!dict.contains(key))
			return nb::none();
		return nb::borrow<nb::object>(dict[key].ptr());
	}

	std::optional<nb::list> asList(nb::handle value)
	{
		if (!value.is_valid() || value.is_none() || !nb::isinstance<nb::list>(value))
			return std::nullopt;
		return nb::borrow<nb::list>(value);
	}

	nb::callable importAdapter()
	{
		return nb::cast<nb::callable>(nb::module_::import_("jfm_import.cpp_adapter").attr("import_file"));
	}

	nb::dict makeImportOptionsDict(const JFMImportOptions &options)
	{
		nb::dict dict;
		dict["parser"] = nb::str(options.parser.c_str());
		dict["name_pattern"] = nb::str(options.namePattern.c_str());
		dict["short_name_pattern"] = nb::str(options.shortNamePattern.c_str());
		dict["variable_delimiter"] = nb::str(options.variableDelimiter.c_str());
		nb::list customVariables;
		for (const JFMNameVariableRule &rule : options.customVariables)
		{
			nb::dict item;
			item["name"] = nb::str(rule.name.c_str());
			item["source"] = nb::str(rule.source.c_str());
			item["token_index"] = nb::int_(rule.tokenIndex);
			item["substring_start"] = nb::int_(rule.substringStart);
			item["substring_length"] = nb::int_(rule.substringLength);
			customVariables.append(item);
		}
		dict["custom_variables"] = customVariables;
		dict["decimal_separator"] = nb::str(options.decimalSeparator.c_str());
		dict["temperature_extraction"] = nb::str(options.temperatureExtraction.c_str());
		dict["custom_script_path"] = nb::str(options.customScriptPath.c_str());
		if (options.delimiter.empty())
			dict["delimiter"] = nb::none();
		else
			dict["delimiter"] = nb::str(options.delimiter.c_str());
		return dict;
	}

	Result<JFMImportBatchResult> importWithPython(
		const Path &pythonRoot,
		const std::vector<Path> &paths,
		const JFMImportOptions &options,
		const JFMPythonImportBridge::ProgressCallback &progressCallback)
	{
		if (!ConfigureJFMPythonPaths(pythonRoot))
			return makePythonBridgeError("Python import failed.", "Could not append pythonRoot to sys.path: " + pythonRoot.Utf8());

		JFMImportBatchResult batch;
		const std::size_t total = paths.size();
		nb::dict importOptions = makeImportOptionsDict(options);
		for (std::size_t index = 0; index < paths.size(); ++index)
		{
			const Path &path = paths[index];
			const std::string parser = parserNameForPath(path);
			if (progressCallback)
				progressCallback(index, total, "Importing " + path.filename().Utf8());

			nb::dict result;
			try
			{
				nb::object adapterResult = importAdapter()(nb::str(path.Utf8().c_str()), importOptions);
				if (!nb::isinstance<nb::dict>(adapterResult))
					return makeImportFileError(path, pythonRoot, parser, "Python adapter returned a non-dict result.");
				result = nb::borrow<nb::dict>(adapterResult);
			}
			catch (const nb::python_error &error)
			{
				return makeImportFileError(path, pythonRoot, parser, error.what());
			}
			catch (const std::exception &error)
			{
				return makeImportFileError(path, pythonRoot, parser, error.what());
			}

			const std::string adapterParser = pyString(dictItem(result, "parser"));
			const std::string parserName = adapterParser.empty() ? parser : adapterParser;
			if (const std::optional<nb::list> warnings = asList(dictItem(result, "warnings")))
			{
				for (nb::handle warning : *warnings)
					batch.warnings.push_back(pyString(warning));
			}

			const std::optional<nb::list> characteristics = asList(dictItem(result, "characteristics"));
			if (!characteristics)
				return makeImportFileError(path, pythonRoot, parser, "Python adapter returned no characteristics list.");

			for (nb::handle characteristic : *characteristics)
			{
				try
				{
					JFMCharacteristic imported = nb::cast<JFMCharacteristic>(characteristic);
					imported.info.importOptions = options;
					if (imported.info.importOptions.parser == "auto")
						imported.info.importOptions.parser = parserName;
					batch.characteristics.emplace_back(std::move(imported));
				}
				catch (const nb::python_error &error)
				{
					return makeImportFileError(path, pythonRoot, parserName, error.what());
				}
				catch (const std::exception &error)
				{
					return makeImportFileError(path, pythonRoot, parserName, error.what());
				}
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

	Result<JFMImportBatchResult> JFMPythonImportBridge::ImportFiles(
		const std::vector<Path> &paths,
		const JFMImportOptions &options,
		const ProgressCallback &progressCallback)
	{
#if DS_PYTHON_CAPI_AVAILABLE
		static std::mutex pythonMutex;
		std::scoped_lock lock(pythonMutex);

		if (m_PythonRoot.Empty() || !FileSystem::Exists(m_PythonRoot.Native()))
		{
			return makePythonBridgeError(
				"Python import failed.",
				"Python root was not found: " + m_PythonRoot.Utf8());
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
			return importWithPython(m_PythonRoot, paths, options, progressCallback);
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
		(void)options;
		(void)progressCallback;
		return makePythonBridgeError(
			"Python import is unavailable in this build.",
			"DS_PYTHON_CAPI_AVAILABLE is 0, so JFM cannot call embedded Python.");
#endif
	}

	Result<JFMImportBatchResult> JFMPythonImportBridge::ImportFiles(const std::vector<Path> &paths, const ProgressCallback &progressCallback)
	{
		return ImportFiles(paths, JFMImportOptions{}, progressCallback);
	}
} // namespace SST::Prototypes::JFM
