#include "Core/dspch.hpp"
#include "IO/JFMMonteCarloStorage.hpp"

#include "IO/JFMProjectBundleSerializer.hpp"
#include "Analysis/JFMFitEngine.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace SST::Prototypes::JFM
{
	namespace
	{
		StructuredError BundleError(std::string message, std::string details)
		{
			return StructuredError{
				ErrorCategory::IO,
				Severity::Error,
				std::move(message),
				std::move(details),
				"Check the JFM bundle files and directory permissions.",
				"JFMProjectBundleSerializer",
				"jfm.bundle"};
		}

		template <typename T>
		T ReadOr(const YAML::Node &node, const char *key, T fallback)
		{
			if (!node || !node.IsMap())
				return fallback;
			const YAML::Node child = node[key];
			return child ? child.as<T>() : fallback;
		}

		std::vector<std::string> SplitTabs(const std::string &line)
		{
			std::vector<std::string> columns;
			std::size_t start = 0;
			while (start <= line.size())
			{
				const std::size_t end = line.find('\t', start);
				columns.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return columns;
		}

		std::string SanitizedId(CharacteristicId id)
		{
			return std::to_string(id);
		}

		std::string RawRelativePath(CharacteristicId id)
		{
			return "data/characteristics/" + SanitizedId(id) + ".tsv";
		}

		std::string McParameterRelativePath(CharacteristicId id)
		{
			return "mc/" + SanitizedId(id) + "/parameters.tsv";
		}

		std::string McNoiseRelativePath(CharacteristicId id)
		{
			return "mc/" + SanitizedId(id) + "/noise.tsv";
		}

		bool SaveText(const Path &path, const std::string &text, std::string &error)
		{
			std::error_code directoryError;
			if (!path.parent_path().Empty())
				(void)FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
			if (directoryError)
			{
				error = directoryError.message();
				return false;
			}
			return TextFileIO::Save(path, text, error);
		}

		YAML::Node ColorNode(const std::array<float, 4> &color)
		{
			YAML::Node node(YAML::NodeType::Sequence);
			for (float component : color)
				node.push_back(component);
			return node;
		}

		std::array<float, 4> ReadColor(const YAML::Node &node, std::array<float, 4> fallback)
		{
			if (!node || !node.IsSequence())
				return fallback;
			for (std::size_t index = 0; index < fallback.size() && index < node.size(); ++index)
				fallback[index] = node[index].as<float>();
			return fallback;
		}

		YAML::Node StyleNode(const JFMArrheniusLayoutStyle &style)
		{
			YAML::Node node;
			node["color"] = ColorNode(style.color);
			node["point_size"] = style.pointSize;
			node["line_weight"] = style.lineWeight;
			node["draw_mode"] = style.drawMode;
			node["marker"] = style.marker;
			node["visible"] = style.visible;
			return node;
		}

		JFMArrheniusLayoutStyle ReadStyle(const YAML::Node &node, JFMArrheniusLayoutStyle fallback)
		{
			if (!node || !node.IsMap())
				return fallback;
			fallback.color = ReadColor(node["color"], fallback.color);
			fallback.pointSize = ReadOr<float>(node, "point_size", fallback.pointSize);
			fallback.lineWeight = ReadOr<float>(node, "line_weight", fallback.lineWeight);
			fallback.drawMode = ReadOr<int>(node, "draw_mode", fallback.drawMode);
			fallback.marker = ReadOr<int>(node, "marker", fallback.marker);
			fallback.visible = ReadOr<bool>(node, "visible", fallback.visible);
			return fallback;
		}

		YAML::Node FitRangeNode(const JFMFitRange &range)
		{
			YAML::Node node;
			node["lower_index"] = range.lowerIndex;
			node["upper_index"] = range.upperIndex;
			node["lower_voltage"] = range.lowerVoltage;
			node["upper_voltage"] = range.upperVoltage;
			node["reason"] = range.reason;
			return node;
		}

		JFMFitRange ReadFitRange(const YAML::Node &node)
		{
			JFMFitRange range;
			range.lowerIndex = ReadOr<std::size_t>(node, "lower_index", 0);
			range.upperIndex = ReadOr<std::size_t>(node, "upper_index", 0);
			range.lowerVoltage = ReadOr<double>(node, "lower_voltage", 0.0);
			range.upperVoltage = ReadOr<double>(node, "upper_voltage", 0.0);
			range.reason = ReadOr<std::string>(node, "reason", "automatic");
			return range;
		}

		YAML::Node EstimateNode(const JFMParameterEstimate &estimate)
		{
			YAML::Node node;
			node["value"] = estimate.value;
			if (estimate.standardError)
				node["standard_error"] = *estimate.standardError;
			else
				node["standard_error"] = YAML::Null;
			node["source"] = estimate.source;
			node["warnings"] = estimate.warnings;
			return node;
		}

		JFMParameterEstimate ReadEstimate(const YAML::Node &node)
		{
			JFMParameterEstimate estimate;
			estimate.value = ReadOr<double>(node, "value", 0.0);
			const YAML::Node standardError = node["standard_error"];
			if (standardError && !standardError.IsNull())
				estimate.standardError = standardError.as<double>();
			estimate.source = ReadOr<std::string>(node, "source", {});
			if (node["warnings"])
				estimate.warnings = node["warnings"].as<std::vector<std::string>>();
			return estimate;
		}

		YAML::Node ParameterMapNode(const std::unordered_map<std::string, JFMParameterEstimate> &parameters)
		{
			YAML::Node node(YAML::NodeType::Map);
			for (const auto &[name, estimate] : parameters)
				node[name] = EstimateNode(estimate);
			return node;
		}

		std::unordered_map<std::string, JFMParameterEstimate> ReadParameterMap(const YAML::Node &node)
		{
			std::unordered_map<std::string, JFMParameterEstimate> parameters;
			if (!node || !node.IsMap())
				return parameters;
			for (const auto &entry : node)
				parameters[entry.first.as<std::string>()] = ReadEstimate(entry.second);
			return parameters;
		}

		YAML::Node PrefitNode(const JFMPrefitState &state)
		{
			YAML::Node node;
			node["model"] = state.modelId;
			node["status"] = state.status;
			if (state.manualLowerVoltage)
				node["manual_lower_voltage"] = *state.manualLowerVoltage;
			else
				node["manual_lower_voltage"] = YAML::Null;
			if (state.manualUpperVoltage)
				node["manual_upper_voltage"] = *state.manualUpperVoltage;
			else
				node["manual_upper_voltage"] = YAML::Null;
			node["fit_range"] = FitRangeNode(state.fitRange);
			node["parameters"] = ParameterMapNode(state.parameters);
			node["warnings"] = state.warnings;
			return node;
		}

		std::optional<JFMPrefitState> ReadPrefit(const YAML::Node &node)
		{
			if (!node || !node.IsMap())
				return std::nullopt;
			JFMPrefitState state;
			state.modelId = ReadOr<std::string>(node, "model", "SD-RsRsh");
			state.status = ReadOr<std::string>(node, "status", "not_started");
			if (const YAML::Node lower = node["manual_lower_voltage"]; lower && !lower.IsNull())
				state.manualLowerVoltage = lower.as<double>();
			if (const YAML::Node upper = node["manual_upper_voltage"]; upper && !upper.IsNull())
				state.manualUpperVoltage = upper.as<double>();
			state.fitRange = ReadFitRange(node["fit_range"]);
			state.parameters = ReadParameterMap(node["parameters"]);
			if (node["warnings"])
				state.warnings = node["warnings"].as<std::vector<std::string>>();
			return state;
		}

		YAML::Node FitNode(const JFMFitState &state)
		{
			YAML::Node node;
			node["model"] = state.modelId;
			node["status"] = state.status;
			node["fit_range"] = FitRangeNode(state.fitRange);
			node["parameters"] = ParameterMapNode(state.parameters);
			node["chi_squared"] = state.chiSquared;
			node["reduced_chi_squared"] = state.reducedChiSquared;
			node["iterations"] = state.iterations;
			node["converged"] = state.converged;
            node["lm_status"] = state.lmStatus;
            node["flat_directions"] = state.flatDirections;
            node["accepted"] = state.accepted;
            node["stop_reason"] = state.stopReason;
            node["fit_method"] = state.fitMethod;
			node["backend"] = state.backend;
            node["objective_mode"] = state.objective.mode;
            node["objective_current_unit"] = state.objective.currentUnit;
            node["objective_shift"] = state.objective.shift;
            node["objective_sigma_absolute"] = state.objective.sigmaAbsolute;
            node["objective_sigma_relative"] = state.objective.sigmaRelative;
            node["objective_switch_k"] = state.objective.switchK;
            node["objective_linear_points"] = state.objective.linearPoints;
            node["objective_log_points"] = state.objective.logPoints;
			node["warnings"] = state.warnings;
			return node;
		}

		std::optional<JFMFitState> ReadFit(const YAML::Node &node)
		{
			if (!node || !node.IsMap())
				return std::nullopt;
			JFMFitState state;
			state.modelId = ReadOr<std::string>(node, "model", "SD-RsRsh");
			state.status = ReadOr<std::string>(node, "status", "not_started");
			state.fitRange = ReadFitRange(node["fit_range"]);
			state.parameters = ReadParameterMap(node["parameters"]);
			state.chiSquared = ReadOr<double>(node, "chi_squared", 0.0);
			state.reducedChiSquared = ReadOr<double>(node, "reduced_chi_squared", 0.0);
			state.iterations = ReadOr<int>(node, "iterations", 0);
			state.converged = ReadOr<bool>(node, "converged", false);
            state.lmStatus = ReadOr<int>(node, "lm_status", -1);
            state.flatDirections = ReadOr<bool>(node, "flat_directions", false);
            state.accepted = ReadOr<bool>(node, "accepted", state.converged);
            state.stopReason = ReadOr<std::string>(node, "stop_reason", "unknown");
            state.fitMethod = ReadOr<std::string>(node, "fit_method", "NM");
			state.backend = ReadOr<std::string>(node, "backend", "cpu");
            state.objective.mode = ReadOr<std::string>(node, "objective_mode", "log_legacy");
            state.objective.currentUnit = ReadOr<std::string>(node, "objective_current_unit", "A");
            state.objective.shift = ReadOr<double>(node, "objective_shift", 0.0);
            state.objective.sigmaAbsolute = ReadOr<double>(node, "objective_sigma_absolute", 0.0);
            state.objective.sigmaRelative = ReadOr<double>(node, "objective_sigma_relative", 0.0);
            state.objective.switchK = ReadOr<double>(node, "objective_switch_k", 3.0);
            state.objective.linearPoints = ReadOr<int>(node, "objective_linear_points", 0);
            state.objective.logPoints = ReadOr<int>(node, "objective_log_points", 0);
			if (node["warnings"])
				state.warnings = node["warnings"].as<std::vector<std::string>>();
			return state;
		}

		YAML::Node MonteCarloStateNode(const JFMMonteCarloState &state)
		{
			YAML::Node node;
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
        node["sample_save_status"] = state.sampleSaveStatus;
        node["sample_save_error"] = state.sampleSaveError;
        node["sample_storage_id"] = state.sampleStorageId;
			node["total_milliseconds"] = state.totalMilliseconds;
			node["warnings"] = state.warnings;
			return node;
		}

		std::optional<JFMMonteCarloState> ReadMonteCarloState(const YAML::Node &node)
		{
			if (!node || !node.IsMap())
				return std::nullopt;
			JFMMonteCarloState state;
			state.modelId = ReadOr<std::string>(node, "model", "SD-RsRsh");
			state.status = ReadOr<std::string>(node, "status", "not_started");
			state.sampleCount = ReadOr<int>(node, "sample_count", 0);
			state.acceptedCount = ReadOr<int>(node, "accepted_count", 0);
            state.convergedCount = ReadOr<int>(node, "converged_count", 0);
			state.noisePercent = ReadOr<double>(node, "noise_percent", 0.0);
			state.startPerturbationPercent = ReadOr<double>(node, "start_perturbation_percent", 0.0);
			state.seed = ReadOr<std::uint32_t>(node, "seed", 42);
			state.backend = ReadOr<std::string>(node, "backend", {});
            state.objective.mode = ReadOr<std::string>(node, "objective_mode", "log_legacy");
            state.objective.currentUnit = ReadOr<std::string>(node, "objective_current_unit", "A");
            state.objective.shift = ReadOr<double>(node, "objective_shift", 0.0);
            state.objective.sigmaAbsolute = ReadOr<double>(node, "objective_sigma_absolute", 0.0);
            state.objective.sigmaRelative = ReadOr<double>(node, "objective_sigma_relative", 0.0);
            state.objective.switchK = ReadOr<double>(node, "objective_switch_k", 3.0);
            state.objective.linearPoints = ReadOr<int>(node, "objective_linear_points", 0);
            state.objective.logPoints = ReadOr<int>(node, "objective_log_points", 0);
			state.outputPath = Path::FromResolved(std::filesystem::u8path(ReadOr<std::string>(node, "output_path", {})));
        state.sampleSaveStatus = ReadOr<std::string>(node,"sample_save_status","not_requested");
        state.sampleSaveError = ReadOr<std::string>(node,"sample_save_error",{});
        state.sampleStorageId = ReadOr<std::string>(node,"sample_storage_id",{});
			state.totalMilliseconds = ReadOr<double>(node, "total_milliseconds", 0.0);
			if (node["warnings"])
				state.warnings = node["warnings"].as<std::vector<std::string>>();
			return state;
		}

		YAML::Node StringMapNode(const std::unordered_map<std::string, std::string> &values)
		{
			YAML::Node node(YAML::NodeType::Map);
			for (const auto &[key, value] : values)
				node[key] = value;
			return node;
		}

		std::unordered_map<std::string, std::string> ReadStringMap(const YAML::Node &node)
		{
			std::unordered_map<std::string, std::string> values;
			if (!node || !node.IsMap())
				return values;
			for (const auto &entry : node)
				values[entry.first.as<std::string>()] = entry.second.as<std::string>();
			return values;
		}

		std::string BuildRawTsv(const JFMCharacteristicRecord &record)
		{
			std::ostringstream out;
			out << std::setprecision(17);
			const bool hasCurrent = record.data != nullptr && !record.data->current.empty();
			const bool hasDensity = record.data != nullptr && !record.data->currentDensity.empty();
			out << (hasCurrent && hasDensity ? "voltage\tcurrent\tcurrent_density\n" : "voltage\tcurrent/current_density\n");
			if (record.data == nullptr)
				return out.str();
			const std::size_t points = record.data->voltage.size();
			for (std::size_t index = 0; index < points; ++index)
			{
				out << record.data->voltage[index];
				if (hasCurrent && hasDensity)
				{
					out << '\t' << (index < record.data->current.size() ? record.data->current[index] : 0.0);
					out << '\t' << (index < record.data->currentDensity.size() ? record.data->currentDensity[index] : 0.0);
				}
				else
				{
					const auto &values = hasDensity ? record.data->currentDensity : record.data->current;
					out << '\t' << (index < values.size() ? values[index] : 0.0);
				}
				out << '\n';
			}
			return out.str();
		}

		Result<Ref<const JFMCharacteristicData>> ReadRawTsv(const Path &path)
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(path, text, error))
				return BundleError("Raw characteristic TSV is missing.", error);

			std::istringstream input(text);
			std::string line;
			if (!std::getline(input, line))
				return BundleError("Raw characteristic TSV is empty.", path.Utf8());
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const std::vector<std::string> header = SplitTabs(line);
			const auto columnIndex = [&header](const char *name) -> int {
				const auto found = std::find(header.begin(), header.end(), name);
				return found == header.end() ? -1 : static_cast<int>(std::distance(header.begin(), found));
			};
			const int voltage = columnIndex("voltage");
			const int current = columnIndex("current");
			const int density = columnIndex("current_density");
			const int combined = columnIndex("current/current_density");
			if (voltage < 0 || (current < 0 && density < 0 && combined < 0))
				return BundleError("Raw characteristic TSV has unsupported columns.", path.Utf8());

			auto data = CreateRef<JFMCharacteristicData>();
			while (std::getline(input, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty())
					continue;
				const std::vector<std::string> columns = SplitTabs(line);
				const auto value = [&columns](int index) {
					return index >= 0 && static_cast<std::size_t>(index) < columns.size() ? std::stod(columns[static_cast<std::size_t>(index)]) : 0.0;
				};
				data->voltage.push_back(value(voltage));
				if (current >= 0)
					data->current.push_back(value(current));
				if (density >= 0)
					data->currentDensity.push_back(value(density));
				if (combined >= 0)
					data->current.push_back(value(combined));
			}
			return Ref<const JFMCharacteristicData>{data};
		}


		Result<JFMMonteCarloResult> ReadMonteCarloTsv(const Path &parameterPath, const Path &noisePath)
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(parameterPath, text, error))
				return BundleError("Monte Carlo parameters TSV is missing.", error);

			JFMMonteCarloResult result;
			std::istringstream parameterInput(text);
			std::string line;
			if (!std::getline(parameterInput, line))
				return BundleError("Monte Carlo parameters TSV is empty.", parameterPath.Utf8());
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const std::vector<std::string> header = SplitTabs(line);
			const auto chiIt = std::find(header.begin(), header.end(), "chi_squared");
			const std::size_t chiIndex = chiIt == header.end() ? header.size() : static_cast<std::size_t>(std::distance(header.begin(), chiIt));
			for (std::size_t index = 1; index < chiIndex; ++index)
				result.parameterNames.push_back(header[index]);
			result.freeParameterCount = static_cast<int>(result.parameterNames.size());

			while (std::getline(parameterInput, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty())
					continue;
				const std::vector<std::string> columns = SplitTabs(line);
				if (columns.size() < chiIndex + 4)
					continue;
				JFMMonteCarloSample sample;
				for (std::size_t index = 1; index < chiIndex; ++index)
					sample.parameters.push_back(std::stod(columns[index]));
				sample.chiSquared = std::stod(columns[chiIndex]);
				sample.reducedChiSquared = std::stod(columns[chiIndex + 1]);
				sample.iterations = std::stoi(columns[chiIndex + 2]);
				sample.converged = std::stoi(columns[chiIndex + 3]) != 0;
                sample.accepted = sample.converged;
                if (columns.size() >= chiIndex + 8) {
                    sample.accepted = std::stoi(columns[chiIndex + 4]) != 0;
                    sample.converged = sample.accepted;
                    sample.solverConverged = std::stoi(columns[chiIndex + 5]) != 0;
                    sample.stopReason = columns[chiIndex + 6];
                    result.confidenceAvailable = std::stoi(columns[chiIndex + 7]) != 0;
                }
                if (columns.size() >= chiIndex+12) {
                    sample.fitMethod = columns[chiIndex+8];
                    sample.flatDirections = std::stoi(columns[chiIndex+9]) != 0;
                    sample.lmStatus = std::stoi(columns[chiIndex+10]);
                    sample.objectiveChiSquared = std::stod(columns[chiIndex+11]);
                    if (columns.size() > chiIndex+13) { sample.linearPoints=std::stoi(columns[chiIndex+12]); sample.logPoints=std::stoi(columns[chiIndex+13]); }
                }
				result.samples.push_back(std::move(sample));
			}

			if (!TextFileIO::Load(noisePath, text, error))
				return result;
			std::istringstream noiseInput(text);
			if (!std::getline(noiseInput, line))
				return result;
			int firstSample = -1;
			while (std::getline(noiseInput, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty())
					continue;
				const std::vector<std::string> columns = SplitTabs(line);
				if (columns.size() < 4)
					continue;
				const int sample = std::stoi(columns[0]);
				if (firstSample < 0)
					firstSample = sample;
				if (sample == firstSample)
				{
					result.voltage.push_back(std::stod(columns[1]));
					result.idealCurrent.push_back(std::stod(columns[2]));
                    if (columns.size() > 5) result.currentSigma.push_back(std::stod(columns[5]));
				}
				result.noisyCurrent.push_back(std::stod(columns[3]));
			}
			return result;
		}

		YAML::Node ArrheniusLayoutNode(const JFMArrheniusLayoutDocument &layout)
		{
			YAML::Node root;
			root["schema_version"] = 1;
			YAML::Node plots(YAML::NodeType::Sequence);
			for (const JFMArrheniusPlotLayout &plot : layout.plots)
			{
				YAML::Node plotNode;
				plotNode["id"] = plot.id;
				plotNode["title"] = plot.title;
				plotNode["quantity"] = plot.quantity;
				plotNode["fit_model"] = plot.fitModel;
				plotNode["prefer_fit"] = plot.preferFit;
				plotNode["auto_refit"] = plot.autoRefit;
				plotNode["open"] = plot.open;
				plotNode["dock_slot"] = plot.dockSlot;
				plotNode["active_series"] = plot.activeSeries;
				YAML::Node series(YAML::NodeType::Sequence);
				for (const JFMArrheniusSeriesLayout &entry : plot.series)
				{
					YAML::Node seriesNode;
					seriesNode["source_mode"] = entry.sourceMode;
					seriesNode["source_id"] = entry.sourceId;
					seriesNode["title"] = entry.title;
					seriesNode["lower"] = entry.lower;
					seriesNode["upper"] = entry.upper;
					seriesNode["style"] = StyleNode(entry.style);
					seriesNode["tuned_style"] = StyleNode(entry.tunedStyle);
					YAML::Node fits(YAML::NodeType::Sequence);
					for (const JFMArrheniusFitLayout &fit : entry.fits)
					{
						YAML::Node fitNode;
						fitNode["name"] = fit.name;
						fitNode["model_name"] = fit.modelName;
						fitNode["model"] = fit.model;
						fitNode["lower"] = fit.lower;
						fitNode["upper"] = fit.upper;
						fitNode["color"] = ColorNode(fit.color);
						fitNode["line_weight"] = fit.lineWeight;
						fitNode["visible"] = fit.visible;
						fits.push_back(fitNode);
					}
					seriesNode["fits"] = fits;
					series.push_back(seriesNode);
				}
				plotNode["series"] = series;
				plots.push_back(plotNode);
			}
			root["plots"] = plots;
			return root;
		}

		JFMArrheniusLayoutDocument ReadArrheniusLayout(const YAML::Node &root)
		{
			JFMArrheniusLayoutDocument layout;
			for (const YAML::Node &plotNode : root["plots"])
			{
				JFMArrheniusPlotLayout plot;
				plot.id = ReadOr<std::uint64_t>(plotNode, "id", 0);
				plot.title = ReadOr<std::string>(plotNode, "title", {});
				plot.quantity = ReadOr<int>(plotNode, "quantity", 3);
				plot.fitModel = ReadOr<int>(plotNode, "fit_model", 0);
				plot.preferFit = ReadOr<bool>(plotNode, "prefer_fit", true);
				plot.autoRefit = ReadOr<bool>(plotNode, "auto_refit", true);
				plot.open = ReadOr<bool>(plotNode, "open", true);
				plot.dockSlot = ReadOr<int>(plotNode, "dock_slot", -1);
				plot.activeSeries = ReadOr<int>(plotNode, "active_series", 0);
				for (const YAML::Node &seriesNode : plotNode["series"])
				{
					JFMArrheniusSeriesLayout entry;
					entry.sourceMode = ReadOr<int>(seriesNode, "source_mode", 0);
					entry.sourceId = ReadOr<std::string>(seriesNode, "source_id", {});
					entry.title = ReadOr<std::string>(seriesNode, "title", {});
					entry.lower = ReadOr<int>(seriesNode, "lower", 0);
					entry.upper = ReadOr<int>(seriesNode, "upper", 0);
					entry.style = ReadStyle(seriesNode["style"], entry.style);
					entry.tunedStyle = ReadStyle(seriesNode["tuned_style"], entry.tunedStyle);
					for (const YAML::Node &fitNode : seriesNode["fits"])
					{
						JFMArrheniusFitLayout fit;
						fit.name = ReadOr<std::string>(fitNode, "name", {});
						fit.modelName = ReadOr<std::string>(fitNode, "model_name", {});
						fit.model = ReadOr<int>(fitNode, "model", 0);
						fit.lower = ReadOr<int>(fitNode, "lower", 0);
						fit.upper = ReadOr<int>(fitNode, "upper", 0);
						fit.color = ReadColor(fitNode["color"], fit.color);
						fit.lineWeight = ReadOr<float>(fitNode, "line_weight", fit.lineWeight);
						fit.visible = ReadOr<bool>(fitNode, "visible", true);
						entry.fits.push_back(std::move(fit));
					}
					plot.series.push_back(std::move(entry));
				}
				layout.plots.push_back(std::move(plot));
			}
			return layout;
		}

		YAML::Node FittingNode(const JFMProjectBundleSnapshot &snapshot)
		{
			std::unordered_map<CharacteristicId, const JFMMonteCarloBundleRecord *> mcById;
			for (const JFMMonteCarloBundleRecord &record : snapshot.monteCarloResults)
				if (record.result) mcById[record.characteristic] = &record;

			YAML::Node root;
			root["schema_version"] = 1;
			YAML::Node characteristics(YAML::NodeType::Sequence);
			for (const JFMCharacteristicInfo &info : snapshot.project.characteristics)
			{
				YAML::Node node;
				node["id"] = info.id;
				node["name"] = info.name;
				node["metadata"] = StringMapNode(info.metadata);
				if (info.prefit)
					node["prefit"] = PrefitNode(*info.prefit);
				if (info.fit)
					node["fit"] = FitNode(*info.fit);
				if (info.monteCarlo)
				{
					node["monte_carlo"] = MonteCarloStateNode(*info.monteCarlo);
					if (mcById.contains(info.id))
					{
						node["monte_carlo_files"]["hdf5"] = "mc/" + SanitizedId(info.id) + "/samples.h5";
                        node["monte_carlo_files"]["free_parameter_count"] = mcById.at(info.id)->result->freeParameterCount;
                        node["monte_carlo_files"]["confidence_available"] = mcById.at(info.id)->result->confidenceAvailable;
					}
				}
				characteristics.push_back(node);
			}
			root["characteristics"] = characteristics;
			return root;
		}

		void ApplyFittingNode(const YAML::Node &root, JFMProjectBundleSnapshot &snapshot, const Path &bundleRoot, std::vector<std::string> &warnings)
		{
			std::unordered_map<CharacteristicId, JFMCharacteristicInfo *> infos;
			for (JFMCharacteristicInfo &info : snapshot.project.characteristics)
				infos[info.id] = &info;

			for (const YAML::Node &node : root["characteristics"])
			{
				const CharacteristicId id = ReadOr<CharacteristicId>(node, "id", InvalidCharacteristicId);
				auto found = infos.find(id);
				if (found == infos.end())
					continue;
				JFMCharacteristicInfo &info = *found->second;
				info.metadata = ReadStringMap(node["metadata"]);
				info.prefit = ReadPrefit(node["prefit"]);
				info.fit = ReadFit(node["fit"]);
				info.monteCarlo = ReadMonteCarloState(node["monte_carlo"]);
				const YAML::Node files = node["monte_carlo_files"];
				if (!files || !files.IsMap())
					continue;
				const std::string parameterFile = ReadOr<std::string>(files, "parameters", {});
				const std::string noiseFile = ReadOr<std::string>(files, "noise", {});
                const auto binaryFile = ReadOr<std::string>(files, "hdf5", {});
                if (parameterFile.empty() && binaryFile.empty()) continue;
                auto result = !binaryFile.empty() ? ReadJFMMonteCarloHdf5(bundleRoot / Path(binaryFile))
                    : ReadMonteCarloTsv(bundleRoot / Path(parameterFile), noiseFile.empty() ? Path{} : bundleRoot / Path(noiseFile));
                if (result) {
                    result->freeParameterCount = ReadOr<int>(files, "free_parameter_count", result->freeParameterCount);
                    result->confidenceAvailable = ReadOr<bool>(files, "confidence_available", info.monteCarlo ? info.monteCarlo->noisePercent > 0.0 : result->confidenceAvailable);
                    snapshot.monteCarloResults.push_back(JFMMonteCarloBundleRecord{id, CreateRef<const JFMMonteCarloResult>(std::move(result).Value())});
                }
				else
					warnings.push_back("MC samples skipped for characteristic " + std::to_string(id) + ": " + result.Error().technicalDetails);
				if (info.monteCarlo)
					{ info.monteCarlo->outputPath = bundleRoot / Path("mc/" + SanitizedId(id)); info.monteCarlo->sampleSaveStatus = result ? "saved" : "failed"; }
			}
		}
	} // namespace

	namespace {
		void SetBundleCollectionStyle(YAML::Node node)
		{
			if (!node.IsMap() && !node.IsSequence()) return;
			node.SetStyle(node.size() ? YAML::EmitterStyle::Block : YAML::EmitterStyle::Flow);
			if (node.IsMap()) for (const auto &item : node) SetBundleCollectionStyle(item.second);
			else for (const auto &item : node) SetBundleCollectionStyle(item);
		}
	}


    Result<JFMMonteCarloResult> ReadJFMMonteCarloTsv(const Path& directory) {
        return ReadMonteCarloTsv(directory/Path("parameters.tsv"),directory/Path("noise.tsv"));
    }

	Result<JFMProjectBundleSaveResult> JFMProjectBundleSerializer::Save(const Path &root, const JFMProjectBundleSnapshot &snapshot)
	{
		try
		{
			std::error_code directoryError;
			(void)FileSystem::CreateDirectories(root.Native(), directoryError);
			if (directoryError)
				return BundleError("JFM bundle directory could not be created.", directoryError.message());

			JFMProject project = snapshot.project;
			for (JFMCharacteristicInfo &info : project.characteristics)
			{
				info.prefit.reset();
				info.fit.reset();
				info.monteCarlo.reset();
			}

			YAML::Node projectNode = JFMProjectYamlSerializer::ToNode(project);
			SetBundleCollectionStyle(projectNode);
			projectNode["bundle_schema_version"] = 1;
			projectNode["analysis"]["fitting"] = "analysis/fitting.yaml";
			projectNode["analysis"]["arrhenius"] = "analysis/arrhenius.yaml";
			YAML::Node external(YAML::NodeType::Sequence);
			for (const JFMCharacteristicRecord &record : snapshot.characteristics)
			{
				if (record.info.id == InvalidCharacteristicId)
					continue;
				YAML::Node ref;
				ref["id"] = record.info.id;
				ref["raw_file"] = RawRelativePath(record.info.id);
				external.push_back(ref);

				std::string error;
				if (!SaveText(root / Path(ref["raw_file"].as<std::string>()), BuildRawTsv(record), error))
					return BundleError("Raw characteristic TSV could not be saved.", error);
			}
			projectNode["external_data"] = external;
			std::string error;
			std::stringstream projectOut;
			projectOut << projectNode;
			if (!SaveText(root / Path("project.yaml"), projectOut.str(), error))
				return BundleError("project.yaml could not be saved.", error);

			std::stringstream fittingOut;
			fittingOut << FittingNode(snapshot);
			if (!SaveText(root / Path("analysis/fitting.yaml"), fittingOut.str(), error))
				return BundleError("analysis/fitting.yaml could not be saved.", error);

			if (snapshot.arrheniusLayout)
			{
				std::stringstream arrheniusOut;
				arrheniusOut << ArrheniusLayoutNode(*snapshot.arrheniusLayout);
				if (!SaveText(root / Path("analysis/arrhenius.yaml"), arrheniusOut.str(), error))
					return BundleError("analysis/arrhenius.yaml could not be saved.", error);
			}

			std::unordered_map<CharacteristicId, JFMMonteCarloState> mcStateById;
			for (const JFMCharacteristicInfo &info : snapshot.project.characteristics)
			{
				if (info.monteCarlo)
					mcStateById[info.id] = *info.monteCarlo;
			}
			for (const JFMMonteCarloBundleRecord &record : snapshot.monteCarloResults)
			{
				if (!record.result) return BundleError("Monte Carlo snapshot is missing.","Retain a valid immutable result before saving.");
				const double noisePercent = mcStateById.contains(record.characteristic) ? mcStateById[record.characteristic].noisePercent : 0.0;
				auto saved = SaveJFMMonteCarloHdf5(root / Path("mc/" + SanitizedId(record.characteristic) + "/samples.h5"), *record.result);
				if (!saved)
					return saved.Error();
			}
			return JFMProjectBundleSaveResult{};
		}
		catch (const std::exception &error)
		{
			return BundleError("JFM bundle serialization failed.", error.what());
		}
	}

	Result<JFMProjectBundleLoadResult> JFMProjectBundleSerializer::Load(const Path &root)
	{
		try
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(root / Path("project.yaml"), text, error))
				return BundleError("project.yaml could not be loaded.", error);
			const YAML::Node projectNode = YAML::Load(text);
			auto project = JFMProjectYamlSerializer::FromNode(projectNode);
			if (!project)
				return project.Error();

			JFMProjectBundleLoadResult loaded;
			loaded.snapshot.project = std::move(project).Value();
			std::unordered_map<CharacteristicId, JFMCharacteristicInfo *> infoById;
			for (JFMCharacteristicInfo &info : loaded.snapshot.project.characteristics)
				infoById[info.id] = &info;

			for (const YAML::Node &ref : projectNode["external_data"])
			{
				const CharacteristicId id = ReadOr<CharacteristicId>(ref, "id", InvalidCharacteristicId);
				const std::string rawFile = ReadOr<std::string>(ref, "raw_file", {});
				if (id == InvalidCharacteristicId || rawFile.empty())
					continue;
				auto info = infoById.find(id);
				if (info == infoById.end())
					continue;
				auto data = ReadRawTsv(root / Path(rawFile));
				if (!data)
					return data.Error();
				loaded.snapshot.characteristics.push_back(JFMCharacteristicRecord{*info->second, std::move(data).Value()});
			}

			const Path fittingPath = root / Path("analysis/fitting.yaml");
			if (FileSystem::Exists(fittingPath.Native()))
			{
				if (TextFileIO::Load(fittingPath, text, error))
					ApplyFittingNode(YAML::Load(text), loaded.snapshot, root, loaded.warnings);
				else
					loaded.warnings.push_back("analysis/fitting.yaml skipped: " + error);
			}
			for (JFMCharacteristicRecord &record : loaded.snapshot.characteristics)
			{
				if (auto found = infoById.find(record.info.id); found != infoById.end())
					record.info = *found->second;
			}

			const Path arrheniusPath = root / Path("analysis/arrhenius.yaml");
			if (FileSystem::Exists(arrheniusPath.Native()))
			{
				if (TextFileIO::Load(arrheniusPath, text, error))
					loaded.snapshot.arrheniusLayout = ReadArrheniusLayout(YAML::Load(text));
				else
					loaded.warnings.push_back("analysis/arrhenius.yaml skipped: " + error);
			}

			return loaded;
		}
		catch (const std::exception &error)
		{
			return BundleError("JFM bundle load failed.", error.what());
		}
	}
} // namespace SST::Prototypes::JFM
