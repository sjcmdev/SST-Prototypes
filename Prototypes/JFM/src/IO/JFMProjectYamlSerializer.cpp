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
		const YAML::Node child = node[key];
		if (!child || child.IsNull())
			return std::nullopt;
		return child.as<double>();
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

			out << YAML::Key << "series" << YAML::Value << YAML::BeginSeq;
			for (const JFMSeries &series : project.series)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "id" << YAML::Value << series.id;
				out << YAML::Key << "name" << YAML::Value << series.name;
				out << YAML::Key << "characteristics" << YAML::Value << YAML::BeginSeq;
				for (const JFMCharacteristic &characteristic : series.characteristics)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "id" << YAML::Value << characteristic.id;
					out << YAML::Key << "name" << YAML::Value << characteristic.name;
					out << YAML::Key << "measurement" << YAML::Value << characteristic.measurement;
					out << YAML::Key << "source_path" << YAML::Value << characteristic.sourcePath;
					out << YAML::Key << "parser" << YAML::Value << characteristic.parser;
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
					out << YAML::Key << "raw_data" << YAML::Value << "external_only";
					out << YAML::Key << "raw_point_count" << YAML::Value << characteristic.voltage.size();
					out << YAML::EndMap;
				}
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

			JFMProject project;
			project.schemaVersion = readOr<int>(root, "schema_version", JFMProjectSchemaVersion);
			if (project.schemaVersion != JFMProjectSchemaVersion)
				return makeYamlError("Unsupported JFM project schema.", "Schema version " + std::to_string(project.schemaVersion) + " is not supported.");

			const YAML::Node projectNode = root["project"];
			project.id = readOr<std::string>(projectNode, "id", "jfm-project");
			project.name = readOr<std::string>(projectNode, "name", "Untitled JFM Project");
			project.metadata = readStringMap(projectNode["metadata"]);

			for (const YAML::Node &seriesNode : root["series"])
			{
				JFMSeries series;
				series.id = readOr<std::string>(seriesNode, "id", {});
				series.name = readOr<std::string>(seriesNode, "name", "Series");
				for (const YAML::Node &characteristicNode : seriesNode["characteristics"])
				{
					JFMCharacteristic characteristic;
					characteristic.id = readOr<std::string>(characteristicNode, "id", {});
					characteristic.name = readOr<std::string>(characteristicNode, "name", "Characteristic");
					characteristic.measurement = readOr<std::string>(characteristicNode, "measurement", "IV");
					characteristic.sourcePath = readOr<std::string>(characteristicNode, "source_path", {});
					characteristic.parser = readOr<std::string>(characteristicNode, "parser", {});
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
					if (qcNode["warnings"])
						characteristic.qc.warnings = qcNode["warnings"].as<std::vector<std::string>>();

					series.characteristics.push_back(std::move(characteristic));
				}
				project.series.push_back(std::move(series));
			}

			for (const YAML::Node &runNode : root["fit_runs"])
				project.fitRuns.push_back(JFMFitRun{readOr<std::string>(runNode, "id", {}), readOr<std::string>(runNode, "name", {}), readOr<std::string>(runNode, "status", "not_started")});

			for (const YAML::Node &runNode : root["monte_carlo_runs"])
				project.monteCarloRuns.push_back(JFMMonteCarloRun{readOr<std::string>(runNode, "id", {}), readOr<std::string>(runNode, "name", {}), readOr<std::string>(runNode, "status", "not_started")});

			for (const YAML::Node &analysisNode : root["derived_analyses"])
				project.derivedAnalyses.push_back(JFMDerivedAnalysis{readOr<std::string>(analysisNode, "id", {}), readOr<std::string>(analysisNode, "name", {}), readOr<std::string>(analysisNode, "status", "not_started")});

			if (project.series.empty())
				project.series.push_back(JFMSeries{"series-1", "Series 1", {}});
			return project;
		}
		catch (const std::exception &error)
		{
			return makeYamlError("Project load failed.", error.what());
		}
	}
} // namespace SST::Prototypes::JFM
