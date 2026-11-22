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

	YAML::Node stringMapNode(const std::unordered_map<std::string, std::string> &values)
	{
		YAML::Node node(YAML::NodeType::Map);
		for (const auto &[key, value] : values)
			node[key] = value;
		return node;
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

	void emitOptionalDouble(YAML::Node &out, const char *key, const std::optional<double> &value)
	{
		if (value) out[key] = *value;
		else out[key] = YAML::Node(YAML::NodeType::Null);
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

	YAML::Node importOptionsNode(const JFMImportOptions &options)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["parser"] = options.parser;
		node["name_pattern"] = options.namePattern;
		node["short_name_pattern"] = options.shortNamePattern;
		node["variable_delimiter"] = options.variableDelimiter;
		node["delimiter"] = options.delimiter;
		node["decimal_separator"] = options.decimalSeparator;
		node["temperature_extraction"] = options.temperatureExtraction;
		node["custom_script_path"] = options.customScriptPath;
		YAML::Node variablesNode(YAML::NodeType::Sequence);
		for (const JFMNameVariableRule &rule : options.customVariables)
		{
			YAML::Node variableNode(YAML::NodeType::Map);
			variableNode["name"] = rule.name;
			variableNode["source"] = rule.source;
			variableNode["token_index"] = rule.tokenIndex;
			variableNode["substring_start"] = rule.substringStart;
			variableNode["substring_length"] = rule.substringLength;
			variablesNode.push_back(variableNode);
		}
		node["variables"] = variablesNode;
		return node;
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

	YAML::Node prefitStateNode(const JFMPrefitState &prefit)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["model"] = prefit.modelId;
		node["status"] = prefit.status;
		emitOptionalDouble(node, "manual_lower_voltage", prefit.manualLowerVoltage);
		emitOptionalDouble(node, "manual_upper_voltage", prefit.manualUpperVoltage);
		YAML::Node rangeNode(YAML::NodeType::Map);
		rangeNode["lower_index"] = prefit.fitRange.lowerIndex;
		rangeNode["upper_index"] = prefit.fitRange.upperIndex;
		rangeNode["lower_voltage"] = prefit.fitRange.lowerVoltage;
		rangeNode["upper_voltage"] = prefit.fitRange.upperVoltage;
		rangeNode["reason"] = prefit.fitRange.reason;
		node["fit_range"] = rangeNode;
		YAML::Node parametersNode(YAML::NodeType::Map);
		for (const auto &[name, estimate] : prefit.parameters)
		{
			YAML::Node estimateNode(YAML::NodeType::Map);
			estimateNode["value"] = estimate.value;
			emitOptionalDouble(estimateNode, "standard_error", estimate.standardError);
			estimateNode["source"] = estimate.source;
			estimateNode["warnings"] = estimate.warnings;
			parametersNode[name] = estimateNode;
		}
		node["parameters"] = parametersNode;
		node["warnings"] = prefit.warnings;
		return node;
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

	YAML::Node fitStateNode(const JFMFitState &fit)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["model"] = fit.modelId;
		node["status"] = fit.status;
		YAML::Node rangeNode(YAML::NodeType::Map);
		rangeNode["lower_index"] = fit.fitRange.lowerIndex;
		rangeNode["upper_index"] = fit.fitRange.upperIndex;
		rangeNode["lower_voltage"] = fit.fitRange.lowerVoltage;
		rangeNode["upper_voltage"] = fit.fitRange.upperVoltage;
		rangeNode["reason"] = fit.fitRange.reason;
		node["fit_range"] = rangeNode;
		YAML::Node parametersNode(YAML::NodeType::Map);
		for (const auto &[name, estimate] : fit.parameters)
		{
			YAML::Node estimateNode(YAML::NodeType::Map);
			estimateNode["value"] = estimate.value;
			emitOptionalDouble(estimateNode, "standard_error", estimate.standardError);
			estimateNode["source"] = estimate.source;
			estimateNode["warnings"] = estimate.warnings;
			parametersNode[name] = estimateNode;
		}
		node["parameters"] = parametersNode;
		node["chi_squared"] = fit.chiSquared;
		node["reduced_chi_squared"] = fit.reducedChiSquared;
		node["iterations"] = fit.iterations;
		node["converged"] = fit.converged;
		node["fit_method"] = fit.fitMethod;
		node["stop_reason"] = fit.stopReason;
		node["accepted"] = fit.accepted;
		node["flat_directions"] = fit.flatDirections;
		node["lm_status"] = fit.lmStatus;
		node["backend"] = fit.backend;
		node["objective_mode"] = fit.objective.mode;
		node["objective_current_unit"] = fit.objective.currentUnit;
		node["objective_shift"] = fit.objective.shift;
		node["objective_sigma_absolute"] = fit.objective.sigmaAbsolute;
		node["objective_sigma_relative"] = fit.objective.sigmaRelative;
		node["objective_switch_k"] = fit.objective.switchK;
		node["objective_linear_points"] = fit.objective.linearPoints;
		node["objective_log_points"] = fit.objective.logPoints;
		node["warnings"] = fit.warnings;
		return node;
	}

	std::optional<JFMFitState> readFitState(const YAML::Node &node)
	{
		if (!node || !node.IsMap())
			return std::nullopt;
		JFMFitState fit;
		fit.modelId = readOr<std::string>(node, "model", "SD-RsRsh");
		fit.status = readOr<std::string>(node, "status", "not_started");
		const YAML::Node range = node["fit_range"];
		fit.fitRange.lowerIndex = readOr<std::size_t>(range, "lower_index", 0);
		fit.fitRange.upperIndex = readOr<std::size_t>(range, "upper_index", 0);
		fit.fitRange.lowerVoltage = readOr<double>(range, "lower_voltage", 0.0);
		fit.fitRange.upperVoltage = readOr<double>(range, "upper_voltage", 0.0);
		fit.fitRange.reason = readOr<std::string>(range, "reason", "automatic");
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
				fit.parameters[entry.first.as<std::string>()] = std::move(estimate);
			}
		}
		fit.chiSquared = readOr<double>(node, "chi_squared", 0.0);
		fit.reducedChiSquared = readOr<double>(node, "reduced_chi_squared", 0.0);
		fit.iterations = readOr<int>(node, "iterations", 0);
		fit.converged = readOr<bool>(node, "converged", false);
		fit.fitMethod = readOr<std::string>(node, "fit_method", "NM");
		fit.stopReason = readOr<std::string>(node, "stop_reason", "unknown");
		fit.accepted = readOr<bool>(node, "accepted", fit.converged);
		fit.flatDirections = readOr<bool>(node, "flat_directions", false);
		fit.lmStatus = readOr<int>(node, "lm_status", -1);
		fit.backend = readOr<std::string>(node, "backend", "cpu");
        fit.objective.mode = readOr<std::string>(node, "objective_mode", "log_legacy");
        fit.objective.currentUnit = readOr<std::string>(node, "objective_current_unit", "A");
        fit.objective.shift = readOr<double>(node, "objective_shift", 0.0);
        fit.objective.sigmaAbsolute = readOr<double>(node, "objective_sigma_absolute", 0.0);
        fit.objective.sigmaRelative = readOr<double>(node, "objective_sigma_relative", 0.0);
        fit.objective.switchK = readOr<double>(node, "objective_switch_k", 3.0);
        fit.objective.linearPoints = readOr<int>(node, "objective_linear_points", 0);
        fit.objective.logPoints = readOr<int>(node, "objective_log_points", 0);
		if (node["warnings"])
			fit.warnings = node["warnings"].as<std::vector<std::string>>();
		return fit;
	}

	YAML::Node monteCarloStateNode(const JFMMonteCarloState &state)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["model"] = state.modelId;
		node["status"] = state.status;
		node["sample_count"] = state.sampleCount;
		node["accepted_count"] = state.acceptedCount;
		node["converged_count"] = state.convergedCount;
		node["noise_percent"] = state.noisePercent;
		node["start_perturbation_percent"] = state.startPerturbationPercent;
		node["seed"] = state.seed;
		node["backend"] = state.backend;
		node["objective_mode"] = state.objective.mode;
		node["objective_current_unit"] = state.objective.currentUnit;
		node["objective_shift"] = state.objective.shift;
		node["objective_sigma_absolute"] = state.objective.sigmaAbsolute;
		node["objective_sigma_relative"] = state.objective.sigmaRelative;
		node["objective_switch_k"] = state.objective.switchK;
		node["objective_linear_points"] = state.objective.linearPoints;
		node["objective_log_points"] = state.objective.logPoints;
		node["output_path"] = state.outputPath.Utf8();
		node["total_milliseconds"] = state.totalMilliseconds;
		node["warnings"] = state.warnings;
		return node;
	}

	std::optional<JFMMonteCarloState> readMonteCarloState(const YAML::Node &node)
	{
		if (!node || !node.IsMap())
			return std::nullopt;
		JFMMonteCarloState state;
		state.modelId = readOr<std::string>(node, "model", "SD-RsRsh");
		state.status = readOr<std::string>(node, "status", "not_started");
		state.sampleCount = readOr<int>(node, "sample_count", 0);
		state.acceptedCount = readOr<int>(node, "accepted_count", 0);
        state.convergedCount = readOr<int>(node, "converged_count", 0);
		state.noisePercent = readOr<double>(node, "noise_percent", 0.0);
		state.startPerturbationPercent = readOr<double>(node, "start_perturbation_percent", 0.0);
		state.seed = readOr<std::uint32_t>(node, "seed", 42);
		state.backend = readOr<std::string>(node, "backend", {});
        state.objective.mode = readOr<std::string>(node, "objective_mode", "log_legacy");
        state.objective.currentUnit = readOr<std::string>(node, "objective_current_unit", "A");
        state.objective.shift = readOr<double>(node, "objective_shift", 0.0);
        state.objective.sigmaAbsolute = readOr<double>(node, "objective_sigma_absolute", 0.0);
        state.objective.sigmaRelative = readOr<double>(node, "objective_sigma_relative", 0.0);
        state.objective.switchK = readOr<double>(node, "objective_switch_k", 3.0);
        state.objective.linearPoints = readOr<int>(node, "objective_linear_points", 0);
        state.objective.logPoints = readOr<int>(node, "objective_log_points", 0);
		state.outputPath = Path::FromResolved(std::filesystem::u8path(readOr<std::string>(node, "output_path", {})));
		state.totalMilliseconds = readOr<double>(node, "total_milliseconds", 0.0);
		if (node["warnings"])
			state.warnings = node["warnings"].as<std::vector<std::string>>();
		return state;
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

	YAML::Node characteristicInfoNode(const JFMCharacteristicInfo &characteristic)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["id"] = characteristic.id;
		node["name"] = characteristic.name;
		node["measurement"] = characteristic.measurement;
		node["source_path"] = characteristic.sourcePath.Utf8();
		node["source_record_key"] = characteristic.sourceRecordKey;
		node["parser"] = characteristic.parser;
		node["import"] = importOptionsNode(characteristic.importOptions);
		emitOptionalDouble(node, "temperature_K", characteristic.temperatureK);
		node["temperature_source"] = characteristic.temperatureSource;
		node["metadata"] = stringMapNode(characteristic.metadata);
		YAML::Node columnsNode(YAML::NodeType::Sequence);
		for (const JFMColumn &column : characteristic.columns)
		{
			YAML::Node columnNode(YAML::NodeType::Map);
			columnNode["name"] = column.name;
			columnNode["unit"] = column.unit;
			columnNode["role"] = column.role;
			columnsNode.push_back(columnNode);
		}
		node["columns"] = columnsNode;
		YAML::Node sweepNode(YAML::NodeType::Map);
		sweepNode["direction"] = characteristic.sweep.direction;
		emitOptionalDouble(sweepNode, "v_start", characteristic.sweep.vStart);
		emitOptionalDouble(sweepNode, "v_stop", characteristic.sweep.vStop);
		sweepNode["n_points"] = characteristic.sweep.pointCount;
		node["sweep"] = sweepNode;
		YAML::Node qcNode(YAML::NodeType::Map);
		qcNode["non_monotonic_sweep"] = characteristic.qc.nonMonotonicSweep;
		qcNode["missing_temperature"] = characteristic.qc.hasMissingTemperature;
		qcNode["warnings"] = characteristic.qc.warnings;
		node["qc"] = qcNode;
		if (characteristic.prefit)
		{
			node["prefit"] = prefitStateNode(*characteristic.prefit);
		}
		if (characteristic.fit)
		{
			node["fit"] = fitStateNode(*characteristic.fit);
		}
		if (characteristic.monteCarlo)
		{
			node["monte_carlo"] = monteCarloStateNode(*characteristic.monteCarlo);
		}
		node["raw_data"] = "external_only";
		node["raw_point_count"] = characteristic.sweep.pointCount;
		return node;
	}

	JFMCharacteristicInfo readCharacteristicInfo(const YAML::Node &characteristicNode)
	{
		JFMCharacteristicInfo characteristic;
		characteristic.id = readCharacteristicId(characteristicNode, "id");
		characteristic.name = readOr<std::string>(characteristicNode, "name", "Characteristic");
		characteristic.measurement = readOr<std::string>(characteristicNode, "measurement", "IV");
		characteristic.sourcePath = Path::FromResolved(std::filesystem::u8path(readOr<std::string>(characteristicNode, "source_path", {})));
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
		characteristic.fit = readFitState(characteristicNode["fit"]);
		characteristic.monteCarlo = readMonteCarloState(characteristicNode["monte_carlo"]);

		return characteristic;
	}

	YAML::Node JFMProjectYamlSerializer::ToNode(const JFMProject &project)
	{
		YAML::Node node(YAML::NodeType::Map);
		node["schema_version"] = JFMProjectSchemaVersion;
		YAML::Node projectNode(YAML::NodeType::Map);
		projectNode["id"] = project.id;
		projectNode["name"] = project.name;
		projectNode["metadata"] = stringMapNode(project.metadata);
		node["project"] = projectNode;

		YAML::Node characteristicsNode(YAML::NodeType::Sequence);
		for (const JFMCharacteristicInfo &characteristic : project.characteristics)
			characteristicsNode.push_back(characteristicInfoNode(characteristic));
		node["characteristics"] = characteristicsNode;

		YAML::Node groupsNode(YAML::NodeType::Sequence);
		for (const JFMGroup &group : project.groups)
		{
			YAML::Node groupNode(YAML::NodeType::Map);
			groupNode["id"] = group.id;
			groupNode["name"] = group.name;
			groupNode["parent_id"] = group.parentId;
			groupNode["metadata"] = stringMapNode(group.metadata);
			groupsNode.push_back(groupNode);
		}
		node["groups"] = groupsNode;

		YAML::Node seriesNode(YAML::NodeType::Sequence);
		for (const JFMSeries &series : project.series)
		{
			YAML::Node seriesItemNode(YAML::NodeType::Map);
			seriesItemNode["id"] = series.id;
			seriesItemNode["name"] = series.name;
			seriesItemNode["group_id"] = series.groupId;
			seriesItemNode["metadata"] = stringMapNode(series.metadata);
			YAML::Node membersNode(YAML::NodeType::Sequence);
			for (const CharacteristicId &characteristicId : series.characteristicIds)
				membersNode.push_back(characteristicId);
			seriesItemNode["characteristics"] = membersNode;
			seriesNode.push_back(seriesItemNode);
		}
		node["series"] = seriesNode;

		YAML::Node fitRunsNode(YAML::NodeType::Sequence);
		for (const JFMFitRun &run : project.fitRuns)
		{
			YAML::Node fitRunNode(YAML::NodeType::Map);
			fitRunNode["id"] = run.id;
			fitRunNode["name"] = run.name;
			fitRunNode["status"] = run.status;
			fitRunsNode.push_back(fitRunNode);
		}
		node["fit_runs"] = fitRunsNode;

		YAML::Node monteCarloRunsNode(YAML::NodeType::Sequence);
		for (const JFMMonteCarloRun &run : project.monteCarloRuns)
		{
			YAML::Node monteCarloRunNode(YAML::NodeType::Map);
			monteCarloRunNode["id"] = run.id;
			monteCarloRunNode["name"] = run.name;
			monteCarloRunNode["status"] = run.status;
			monteCarloRunsNode.push_back(monteCarloRunNode);
		}
		node["monte_carlo_runs"] = monteCarloRunsNode;

		YAML::Node derivedAnalysesNode(YAML::NodeType::Sequence);
		for (const JFMDerivedAnalysis &analysis : project.derivedAnalyses)
		{
			YAML::Node analysisNode(YAML::NodeType::Map);
			analysisNode["id"] = analysis.id;
			analysisNode["name"] = analysis.name;
			analysisNode["status"] = analysis.status;
			derivedAnalysesNode.push_back(analysisNode);
		}
		node["derived_analyses"] = derivedAnalysesNode;

		YAML::Node scriptsNode(YAML::NodeType::Sequence);
		for (const JFMScriptInfo &script : project.scripts)
		{
			YAML::Node scriptNode(YAML::NodeType::Map);
			scriptNode["id"] = script.id;
			scriptNode["name"] = script.name;
			scriptNode["path"] = script.path.Utf8();
			scriptNode["enabled"] = script.enabled;
			scriptNode["reload_count"] = script.reloadCount;
			scriptsNode.push_back(scriptNode);
		}
		node["scripts"] = scriptsNode;

		YAML::Node groupingsNode(YAML::NodeType::Sequence);
		for (const JFMGroupingDefinition &grouping : project.groupings)
		{
			YAML::Node groupingNode(YAML::NodeType::Map);
			groupingNode["id"] = grouping.id;
			groupingNode["name"] = grouping.name;
			groupingNode["source_series"] = grouping.sourceSeriesIds;
			groupingNode["metadata_key"] = grouping.metadataKey;
			groupingNode["mode"] = groupingModeName(grouping.mode);
			groupingNode["numeric_step"] = grouping.numericStep;
			groupingNode["include_missing"] = grouping.includeMissing;
			groupingsNode.push_back(groupingNode);
		}
		node["groupings"] = groupingsNode;
		return node;
	}

	Result<std::string> JFMProjectYamlSerializer::Serialize(const JFMProject &project)
	{
		try
		{
			YAML::Emitter out;
			out << ToNode(project);

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
		try { return FromNode(YAML::Load(text)); }
		catch (const std::exception &error) { return makeYamlError("Project load failed.", error.what()); }
	}

	Result<JFMProject> JFMProjectYamlSerializer::FromNode(const YAML::Node &root)
	{
		try
		{
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
					Path::FromResolved(std::filesystem::u8path(readOr<std::string>(scriptNode, "path", {}))),
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
