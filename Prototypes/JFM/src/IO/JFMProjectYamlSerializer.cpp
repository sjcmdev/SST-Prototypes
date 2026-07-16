#include "Core/dspch.hpp"

#include "IO/JFMProjectYamlSerializer.hpp"

#include <yaml-cpp/yaml.h>

namespace SST::Prototypes::JFM
{

	StructuredError makeYamlError(std::string message, std::string details)
	{
		return StructuredError{
			ErrorCategory::IO,
			Severity::Error,
			std::move(message),
			std::move(details),
			"Check that the selected file is a JFM YAML project.",
			"JFMProjectYamlSerializer",
			"jfm.yaml"};
	}

	template <typename TValue>
	TValue readOr(const YAML::Node &node, const char *key, TValue fallback)
	{
		if (!node || !node.IsMap())
			return fallback;
		const YAML::Node child = node[key];
		if (!child)
			return fallback;
		return child.as<TValue>();
	}

	void emitStringMap(YAML::Emitter &out, const std::unordered_map<std::string, std::string> &values)
	{
		out << YAML::BeginMap;
		for (const auto &[key, value] : values)
			out << YAML::Key << key << YAML::Value << value;
		out << YAML::EndMap;
	}

	std::unordered_map<std::string, std::string> readStringMap(const YAML::Node &node)
	{
		std::unordered_map<std::string, std::string> values;
		if (!node || !node.IsMap())
			return values;

		for (const auto &entry : node)
			values[entry.first.as<std::string>()] = entry.second.as<std::string>();
		return values;
	}

	void emitOptionalDouble(YAML::Emitter &out, const char *key, const std::optional<double> &value)
	{
		out << YAML::Key << key << YAML::Value;
		if (value.has_value())
			out << *value;
		else
			out << YAML::Null;
	}

	std::optional<double> readOptionalDouble(const YAML::Node &node, const char *key)
	{
		if (!node || !node.IsMap())
			return std::nullopt;
		const YAML::Node child = node[key];
		if (!child || child.IsNull())
			return std::nullopt;
		return child.as<double>();
	}

	CharacteristicId legacyIdFromText(const std::string &text)
	{
		try
		{
			const auto value = static_cast<CharacteristicId>(std::stoull(text));
			return value == InvalidCharacteristicId ? 1 : value;
		}
		catch (const std::exception &)
		{
			CharacteristicId hash = 1469598103934665603ull;
			for (const unsigned char character : text)
			{
				hash ^= character;
				hash *= 1099511628211ull;
			}
			return hash == InvalidCharacteristicId ? 1 : hash;
		}
	}

	CharacteristicId readCharacteristicId(const YAML::Node &node, const char *key, CharacteristicId fallback = InvalidCharacteristicId)
	{
		if (!node || !node.IsMap())
			return fallback;
		const YAML::Node child = node[key];
		if (!child || child.IsNull())
			return fallback;
		return legacyIdFromText(child.as<std::string>());
	}

	void emitImportOptions(YAML::Emitter &out, const JFMImportOptions &options)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "parser" << YAML::Value << options.parser;
		out << YAML::Key << "name_pattern" << YAML::Value << options.namePattern;
		out << YAML::Key << "short_name_pattern" << YAML::Value << options.shortNamePattern;
		out << YAML::Key << "variable_delimiter" << YAML::Value << options.variableDelimiter;
		out << YAML::Key << "delimiter" << YAML::Value << options.delimiter;
		out << YAML::Key << "decimal_separator" << YAML::Value << options.decimalSeparator;
		out << YAML::Key << "temperature_extraction" << YAML::Value << options.temperatureExtraction;
		out << YAML::Key << "custom_script_path" << YAML::Value << options.customScriptPath;
		out << YAML::Key << "variables" << YAML::Value << YAML::BeginSeq;
		for (const JFMNameVariableRule &rule : options.customVariables)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "name" << YAML::Value << rule.name;
			out << YAML::Key << "source" << YAML::Value << rule.source;
			out << YAML::Key << "token_index" << YAML::Value << rule.tokenIndex;
			out << YAML::Key << "substring_start" << YAML::Value << rule.substringStart;
			out << YAML::Key << "substring_length" << YAML::Value << rule.substringLength;
			out << YAML::EndMap;
		}
		out << YAML::EndSeq << YAML::EndMap;
	}

	JFMImportOptions readImportOptions(const YAML::Node &node, const std::string &fallbackParser)
	{
		JFMImportOptions options;
		if (!node || !node.IsMap())
		{
			options.parser = fallbackParser.empty() ? "auto" : fallbackParser;
			return options;
		}
		options.parser = readOr<std::string>(node, "parser", fallbackParser.empty() ? "auto" : fallbackParser);
		options.namePattern = readOr<std::string>(node, "name_pattern", "{stem}");
		options.shortNamePattern = readOr<std::string>(node, "short_name_pattern", {});
		options.variableDelimiter = readOr<std::string>(node, "variable_delimiter", "_");
		options.delimiter = readOr<std::string>(node, "delimiter", {});
		options.decimalSeparator = readOr<std::string>(node, "decimal_separator", "auto");
		options.temperatureExtraction = readOr<std::string>(node, "temperature_extraction", "auto");
		options.customScriptPath = readOr<std::string>(node, "custom_script_path", {});
		for (const YAML::Node &ruleNode : node["variables"])
		{
			options.customVariables.push_back(JFMNameVariableRule{
				readOr<std::string>(ruleNode, "name", {}),
				readOr<std::string>(ruleNode, "source", "stem"),
				readOr<int>(ruleNode, "token_index", -1),
				readOr<int>(ruleNode, "substring_start", 0),
				readOr<int>(ruleNode, "substring_length", -1)});
		}
		return options;
	}

	void emitPrefitState(YAML::Emitter &out, const JFMPrefitState &prefit)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "model" << YAML::Value << prefit.modelId;
		out << YAML::Key << "status" << YAML::Value << prefit.status;
		emitOptionalDouble(out, "manual_lower_voltage", prefit.manualLowerVoltage);
		emitOptionalDouble(out, "manual_upper_voltage", prefit.manualUpperVoltage);
		out << YAML::Key << "fit_range" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "lower_index" << YAML::Value << prefit.fitRange.lowerIndex;
		out << YAML::Key << "upper_index" << YAML::Value << prefit.fitRange.upperIndex;
		out << YAML::Key << "lower_voltage" << YAML::Value << prefit.fitRange.lowerVoltage;
		out << YAML::Key << "upper_voltage" << YAML::Value << prefit.fitRange.upperVoltage;
		out << YAML::Key << "reason" << YAML::Value << prefit.fitRange.reason;
		out << YAML::EndMap;
		out << YAML::Key << "parameters" << YAML::Value << YAML::BeginMap;
		for (const auto &[name, estimate] : prefit.parameters)
		{
			out << YAML::Key << name << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "value" << YAML::Value << estimate.value;
			emitOptionalDouble(out, "standard_error", estimate.standardError);
			out << YAML::Key << "source" << YAML::Value << estimate.source;
			out << YAML::Key << "warnings" << YAML::Value << estimate.warnings;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;
		out << YAML::Key << "warnings" << YAML::Value << prefit.warnings;
		out << YAML::EndMap;
	}

	std::optional<JFMPrefitState> readPrefitState(const YAML::Node &node)
	{
		if (!node || !node.IsMap())
			return std::nullopt;
		JFMPrefitState prefit;
		prefit.modelId = readOr<std::string>(node, "model", "SD-RsRsh");
		prefit.status = readOr<std::string>(node, "status", "not_started");
		prefit.manualLowerVoltage = readOptionalDouble(node, "manual_lower_voltage");
		prefit.manualUpperVoltage = readOptionalDouble(node, "manual_upper_voltage");
		const YAML::Node range = node["fit_range"];
		prefit.fitRange.lowerIndex = readOr<std::size_t>(range, "lower_index", 0);
		prefit.fitRange.upperIndex = readOr<std::size_t>(range, "upper_index", 0);
		prefit.fitRange.lowerVoltage = readOr<double>(range, "lower_voltage", 0.0);
		prefit.fitRange.upperVoltage = readOr<double>(range, "upper_voltage", 0.0);
		prefit.fitRange.reason = readOr<std::string>(range, "reason", "automatic");
		const YAML::Node parameters = node["parameters"];
		if (parameters && parameters.IsMap())
		{
			for (const auto &entry : parameters)
			{
				const YAML::Node estimateNode = entry.second;
				JFMParameterEstimate estimate;
				estimate.value = readOr<double>(estimateNode, "value", 0.0);
				estimate.standardError = readOptionalDouble(estimateNode, "standard_error");
				estimate.source = readOr<std::string>(estimateNode, "source", {});
				if (estimateNode["warnings"])
					estimate.warnings = estimateNode["warnings"].as<std::vector<std::string>>();
				prefit.parameters[entry.first.as<std::string>()] = std::move(estimate);
			}
		}
		if (node["warnings"])
			prefit.warnings = node["warnings"].as<std::vector<std::string>>();
		return prefit;
	}

	const char *groupingModeName(JFMGroupingMode mode)
	{
		return mode == JFMGroupingMode::Exact ? "exact" : "numeric_step";
	}

	JFMGroupingMode readGroupingMode(const YAML::Node &node)
	{
		return readOr<std::string>(node, "mode", "numeric_step") == "exact"
			? JFMGroupingMode::Exact
			: JFMGroupingMode::NumericStep;
	}

	void emitCharacteristicInfo(YAML::Emitter &out, const JFMCharacteristicInfo &characteristic)
	{
		out << YAML::BeginMap;
		out << YAML::Key << "id" << YAML::Value << characteristic.id;
		out << YAML::Key << "name" << YAML::Value << characteristic.name;
		out << YAML::Key << "measurement" << YAML::Value << characteristic.measurement;
		out << YAML::Key << "source_path" << YAML::Value << characteristic.sourcePath.String();
		out << YAML::Key << "source_record_key" << YAML::Value << characteristic.sourceRecordKey;
		out << YAML::Key << "parser" << YAML::Value << characteristic.parser;
		out << YAML::Key << "import" << YAML::Value;
		emitImportOptions(out, characteristic.importOptions);
		emitOptionalDouble(out, "temperature_K", characteristic.temperatureK);
		out << YAML::Key << "temperature_source" << YAML::Value << characteristic.temperatureSource;
		out << YAML::Key << "metadata" << YAML::Value;
		emitStringMap(out, characteristic.metadata);
		out << YAML::Key << "columns" << YAML::Value << YAML::BeginSeq;
		for (const JFMColumn &column : characteristic.columns)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "name" << YAML::Value << column.name;
			out << YAML::Key << "unit" << YAML::Value << column.unit;
			out << YAML::Key << "role" << YAML::Value << column.role;
			out << YAML::EndMap;
		}
		out << YAML::EndSeq;
		out << YAML::Key << "sweep" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "direction" << YAML::Value << characteristic.sweep.direction;
		emitOptionalDouble(out, "v_start", characteristic.sweep.vStart);
		emitOptionalDouble(out, "v_stop", characteristic.sweep.vStop);
		out << YAML::Key << "n_points" << YAML::Value << characteristic.sweep.pointCount;
		out << YAML::EndMap;
		out << YAML::Key << "qc" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "non_monotonic_sweep" << YAML::Value << characteristic.qc.nonMonotonicSweep;
		out << YAML::Key << "missing_temperature" << YAML::Value << characteristic.qc.hasMissingTemperature;
		out << YAML::Key << "warnings" << YAML::Value << characteristic.qc.warnings;
		out << YAML::EndMap;
		if (characteristic.prefit)
		{
			out << YAML::Key << "prefit" << YAML::Value;
			emitPrefitState(out, *characteristic.prefit);
		}
		out << YAML::Key << "raw_data" << YAML::Value << "external_only";
		out << YAML::Key << "raw_point_count" << YAML::Value << characteristic.sweep.pointCount;
		out << YAML::EndMap;
	}

	JFMCharacteristicInfo readCharacteristicInfo(const YAML::Node &characteristicNode)
	{
		JFMCharacteristicInfo characteristic;
		characteristic.id = readCharacteristicId(characteristicNode, "id");
		characteristic.name = readOr<std::string>(characteristicNode, "name", "Characteristic");
		characteristic.measurement = readOr<std::string>(characteristicNode, "measurement", "IV");
		characteristic.sourcePath = Path::FromResolved(readOr<std::string>(characteristicNode, "source_path", {}));
		characteristic.sourceRecordKey = readOr<std::string>(characteristicNode, "source_record_key", {});
		characteristic.parser = readOr<std::string>(characteristicNode, "parser", {});
		characteristic.importOptions = readImportOptions(characteristicNode["import"], characteristic.parser);
		characteristic.temperatureK = readOptionalDouble(characteristicNode, "temperature_K");
		characteristic.temperatureSource = readOr<std::string>(characteristicNode, "temperature_source", "missing");
		characteristic.metadata = readStringMap(characteristicNode["metadata"]);

		for (const YAML::Node &columnNode : characteristicNode["columns"])
		{
			characteristic.columns.push_back(JFMColumn{
				readOr<std::string>(columnNode, "name", {}),
				readOr<std::string>(columnNode, "unit", {}),
				readOr<std::string>(columnNode, "role", {})});
		}

		const YAML::Node sweepNode = characteristicNode["sweep"];
		characteristic.sweep.direction = readOr<std::string>(sweepNode, "direction", {});
		characteristic.sweep.vStart = readOptionalDouble(sweepNode, "v_start");
		characteristic.sweep.vStop = readOptionalDouble(sweepNode, "v_stop");
		characteristic.sweep.pointCount = readOr<std::size_t>(sweepNode, "n_points", 0);

		const YAML::Node qcNode = characteristicNode["qc"];
		characteristic.qc.nonMonotonicSweep = readOr<bool>(qcNode, "non_monotonic_sweep", false);
		characteristic.qc.hasMissingTemperature = readOr<bool>(qcNode, "missing_temperature", false);
		if (qcNode && qcNode.IsMap() && qcNode["warnings"])
			characteristic.qc.warnings = qcNode["warnings"].as<std::vector<std::string>>();
		characteristic.prefit = readPrefitState(characteristicNode["prefit"]);

		return characteristic;
	}

	Result<std::string> JFMProjectYamlSerializer::Serialize(const JFMProject &project)
	{
		try
		{
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "schema_version" << YAML::Value << JFMProjectSchemaVersion;
			out << YAML::Key << "project" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "id" << YAML::Value << project.id;
			out << YAML::Key << "name" << YAML::Value << project.name;
			out << YAML::Key << "metadata" << YAML::Value;
			emitStringMap(out, project.metadata);
			out << YAML::EndMap;

			out << YAML::Key << "characteristics" << YAML::Value << YAML::BeginSeq;
			for (const JFMCharacteristicInfo &characteristic : project.characteristics)
				emitCharacteristicInfo(out, characteristic);
			out << YAML::EndSeq;

			out << YAML::Key << "groups" << YAML::Value << YAML::BeginSeq;
			for (const JFMGroup &group : project.groups)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "id" << YAML::Value << group.id;
				out << YAML::Key << "name" << YAML::Value << group.name;
				out << YAML::Key << "parent_id" << YAML::Value << group.parentId;
				out << YAML::Key << "metadata" << YAML::Value;
				emitStringMap(out, group.metadata);
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;

			out << YAML::Key << "series" << YAML::Value << YAML::BeginSeq;
			for (const JFMSeries &series : project.series)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "id" << YAML::Value << series.id;
				out << YAML::Key << "name" << YAML::Value << series.name;
				out << YAML::Key << "group_id" << YAML::Value << series.groupId;
				out << YAML::Key << "metadata" << YAML::Value;
				emitStringMap(out, series.metadata);
				out << YAML::Key << "characteristics" << YAML::Value << YAML::BeginSeq;
				for (const CharacteristicId &characteristicId : series.characteristicIds)
					out << characteristicId;
				out << YAML::EndSeq;
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;

			out << YAML::Key << "fit_runs" << YAML::Value << YAML::BeginSeq;
			for (const JFMFitRun &run : project.fitRuns)
				out << YAML::BeginMap << YAML::Key << "id" << YAML::Value << run.id << YAML::Key << "name" << YAML::Value << run.name << YAML::Key << "status" << YAML::Value << run.status << YAML::EndMap;
			out << YAML::EndSeq;

			out << YAML::Key << "monte_carlo_runs" << YAML::Value << YAML::BeginSeq;
			for (const JFMMonteCarloRun &run : project.monteCarloRuns)
				out << YAML::BeginMap << YAML::Key << "id" << YAML::Value << run.id << YAML::Key << "name" << YAML::Value << run.name << YAML::Key << "status" << YAML::Value << run.status << YAML::EndMap;
			out << YAML::EndSeq;

			out << YAML::Key << "derived_analyses" << YAML::Value << YAML::BeginSeq;
			for (const JFMDerivedAnalysis &analysis : project.derivedAnalyses)
				out << YAML::BeginMap << YAML::Key << "id" << YAML::Value << analysis.id << YAML::Key << "name" << YAML::Value << analysis.name << YAML::Key << "status" << YAML::Value << analysis.status << YAML::EndMap;
			out << YAML::EndSeq;

			out << YAML::Key << "scripts" << YAML::Value << YAML::BeginSeq;
			for (const JFMScriptInfo &script : project.scripts)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "id" << YAML::Value << script.id;
				out << YAML::Key << "name" << YAML::Value << script.name;
				out << YAML::Key << "path" << YAML::Value << script.path.String();
				out << YAML::Key << "enabled" << YAML::Value << script.enabled;
				out << YAML::Key << "reload_count" << YAML::Value << script.reloadCount;
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;

			out << YAML::Key << "groupings" << YAML::Value << YAML::BeginSeq;
			for (const JFMGroupingDefinition &grouping : project.groupings)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "id" << YAML::Value << grouping.id;
				out << YAML::Key << "name" << YAML::Value << grouping.name;
				out << YAML::Key << "source_series" << YAML::Value << grouping.sourceSeriesIds;
				out << YAML::Key << "metadata_key" << YAML::Value << grouping.metadataKey;
				out << YAML::Key << "mode" << YAML::Value << groupingModeName(grouping.mode);
				out << YAML::Key << "numeric_step" << YAML::Value << grouping.numericStep;
				out << YAML::Key << "include_missing" << YAML::Value << grouping.includeMissing;
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;
			out << YAML::EndMap;

			if (!out.good())
				return makeYamlError("Project serialization failed.", out.GetLastError());
			return std::string(out.c_str());
		}
		catch (const std::exception &error)
		{
			return makeYamlError("Project serialization failed.", error.what());
		}
	}

	Result<JFMProject> JFMProjectYamlSerializer::Deserialize(const std::string &text)
	{
		try
		{
			const YAML::Node root = YAML::Load(text);
			if (!root || !root.IsMap())
				return makeYamlError("Project load failed.", "Root YAML node is not a map.");

			const int sourceSchemaVersion = readOr<int>(root, "schema_version", 1);
			if (sourceSchemaVersion < 1 || sourceSchemaVersion > JFMProjectSchemaVersion)
				return makeYamlError("Unsupported JFM project schema.", "Schema version " + std::to_string(sourceSchemaVersion) + " is not supported.");

			JFMProject project;
			project.schemaVersion = JFMProjectSchemaVersion;

			const YAML::Node projectNode = root["project"];
			project.id = readOr<std::string>(projectNode, "id", "jfm-project");
			project.name = readOr<std::string>(projectNode, "name", "Untitled JFM Project");
			project.metadata = readStringMap(projectNode["metadata"]);

			for (const YAML::Node &characteristicNode : root["characteristics"])
				project.characteristics.push_back(readCharacteristicInfo(characteristicNode));

			for (const YAML::Node &groupNode : root["groups"])
			{
				JFMGroup group{
					readOr<std::string>(groupNode, "id", {}),
					readOr<std::string>(groupNode, "name", "Group"),
					readOr<std::string>(groupNode, "parent_id", {})};
				group.metadata = readStringMap(groupNode["metadata"]);
				project.groups.push_back(std::move(group));
			}

			for (const YAML::Node &seriesNode : root["series"])
			{
				JFMSeries series;
				series.id = readOr<std::string>(seriesNode, "id", {});
				series.name = readOr<std::string>(seriesNode, "name", "Series");
				series.groupId = readOr<std::string>(seriesNode, "group_id", {});
				series.metadata = readStringMap(seriesNode["metadata"]);
				for (const YAML::Node &characteristicNode : seriesNode["characteristics"])
				{
					if (characteristicNode.IsScalar())
						series.characteristicIds.push_back(legacyIdFromText(characteristicNode.as<std::string>()));
					else
					{
						JFMCharacteristicInfo characteristic = readCharacteristicInfo(characteristicNode);
						series.characteristicIds.push_back(characteristic.id);
						project.characteristics.push_back(std::move(characteristic));
					}
				}
				project.series.push_back(std::move(series));
			}

			for (const YAML::Node &runNode : root["fit_runs"])
				project.fitRuns.push_back(JFMFitRun{readOr<std::string>(runNode, "id", {}), readOr<std::string>(runNode, "name", {}), readOr<std::string>(runNode, "status", "not_started")});

			for (const YAML::Node &runNode : root["monte_carlo_runs"])
				project.monteCarloRuns.push_back(JFMMonteCarloRun{readOr<std::string>(runNode, "id", {}), readOr<std::string>(runNode, "name", {}), readOr<std::string>(runNode, "status", "not_started")});

			for (const YAML::Node &analysisNode : root["derived_analyses"])
				project.derivedAnalyses.push_back(JFMDerivedAnalysis{readOr<std::string>(analysisNode, "id", {}), readOr<std::string>(analysisNode, "name", {}), readOr<std::string>(analysisNode, "status", "not_started")});

			for (const YAML::Node &scriptNode : root["scripts"])
			{
				project.scripts.push_back(JFMScriptInfo{
					readOr<std::string>(scriptNode, "id", {}),
					readOr<std::string>(scriptNode, "name", {}),
					Path::FromResolved(readOr<std::string>(scriptNode, "path", {})),
					readOr<bool>(scriptNode, "enabled", true),
					readOr<std::size_t>(scriptNode, "reload_count", 0)});
			}

			for (const YAML::Node &groupingNode : root["groupings"])
			{
				JFMGroupingDefinition grouping;
				grouping.id = readOr<std::string>(groupingNode, "id", {});
				grouping.name = readOr<std::string>(groupingNode, "name", "Grouping");
				if (groupingNode["source_series"])
					grouping.sourceSeriesIds = groupingNode["source_series"].as<std::vector<std::string>>();
				grouping.metadataKey = readOr<std::string>(groupingNode, "metadata_key", "temperature_K");
				grouping.mode = readGroupingMode(groupingNode);
				grouping.numericStep = readOr<double>(groupingNode, "numeric_step", 1.0);
				grouping.includeMissing = readOr<bool>(groupingNode, "include_missing", true);
				project.groupings.push_back(std::move(grouping));
			}

			if (project.series.empty())
				project.series.push_back(JFMSeries{"series-1", "Series 1", {}, {}});
			return project;
		}
		catch (const std::exception &error)
		{
			return makeYamlError("Project load failed.", error.what());
		}
	}
} // namespace SST::Prototypes::JFM
