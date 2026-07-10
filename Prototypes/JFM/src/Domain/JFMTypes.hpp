#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SST::Prototypes::JFM
{
	inline constexpr int JFMProjectSchemaVersion = 1;

	struct JFMColumn
	{
		std::string name;
		std::string unit;
		std::string role;
	};

	struct JFMSweepInfo
	{
		std::string direction;
		std::optional<double> vStart;
		std::optional<double> vStop;
		std::size_t pointCount = 0;
	};

	struct JFMQualityControl
	{
		bool nonMonotonicSweep = false;
		bool hasMissingTemperature = false;
		std::vector<std::string> warnings;
	};

	struct JFMCharacteristic
	{
		std::string id;
		std::string name;
		std::string measurement = "IV";
		std::string sourcePath; 
		std::string parser;
		std::optional<double> temperatureK;
		std::string temperatureSource = "missing";
		std::vector<JFMColumn> columns;
		JFMSweepInfo sweep;
		JFMQualityControl qc;
		std::unordered_map<std::string, std::string> metadata;
		std::vector<double> voltage;
		std::vector<double> current;
		std::vector<double> currentDensity;
	};

	struct JFMSeries
	{
		std::string id;
		std::string name;
		std::vector<JFMCharacteristic> characteristics;
	};

	struct JFMFitRun
	{
		std::string id;
		std::string name;
		std::string status = "not_started";
	};

	struct JFMMonteCarloRun
	{
		std::string id;
		std::string name;
		std::string status = "not_started";
	};

	struct JFMDerivedAnalysis
	{
		std::string id;
		std::string name;
		std::string status = "not_started";
	};

	struct JFMProject
	{
		int schemaVersion = JFMProjectSchemaVersion;
		std::string id;
		std::string name = "Untitled JFM Project";
		std::unordered_map<std::string, std::string> metadata;
		std::vector<JFMSeries> series;
		std::vector<JFMFitRun> fitRuns;
		std::vector<JFMMonteCarloRun> monteCarloRuns;
		std::vector<JFMDerivedAnalysis> derivedAnalyses;
	};
} // namespace SST::Prototypes::JFM
