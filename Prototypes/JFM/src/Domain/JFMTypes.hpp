#pragma once

#include "Core/Utils/Path.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SST::Prototypes::JFM
{
	inline constexpr int JFMProjectSchemaVersion = 1;
	using CharacteristicId = std::uint64_t;
	inline constexpr CharacteristicId InvalidCharacteristicId = 0;

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

	struct JFMCharacteristicInfo
	{
		CharacteristicId id = InvalidCharacteristicId;
		std::string name;
		std::string measurement = "IV";
		Path sourcePath;
		std::string parser;
		std::optional<double> temperatureK;
		std::string temperatureSource = "missing";
		std::vector<JFMColumn> columns;
		JFMSweepInfo sweep;
		JFMQualityControl qc;
		std::unordered_map<std::string, std::string> metadata;
	};

	struct JFMCharacteristicData
	{
		std::vector<double> voltage;
		std::vector<double> current;
		std::vector<double> currentDensity;
	};

	struct JFMCharacteristic
	{
		JFMCharacteristicInfo info;
		JFMCharacteristicData data;
	};

	struct JFMImportOptions
	{
		std::string parser = "auto";
		std::string namePattern = "{stem}";
		std::string delimiter;
		std::string decimalSeparator = "auto";
		std::string temperatureExtraction = "auto";
		std::string customScriptPath;
	};

	struct JFMGroup
	{
		std::string id;
		std::string name;
		std::string parentId;
	};

	struct JFMSeries
	{
		std::string id;
		std::string name;
		std::string groupId;
		std::vector<CharacteristicId> characteristicIds;
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

	struct JFMScriptInfo
	{
		std::string id;
		std::string name;
		Path path;
		bool enabled = true;
		std::size_t reloadCount = 0;
	};

	struct JFMProject
	{
		int schemaVersion = JFMProjectSchemaVersion;
		std::string id;
		std::string name = "Untitled JFM Project";
		std::unordered_map<std::string, std::string> metadata;
		std::vector<JFMGroup> groups;
		std::vector<JFMSeries> series;
		std::vector<JFMCharacteristicInfo> characteristics;
		std::vector<JFMFitRun> fitRuns;
		std::vector<JFMMonteCarloRun> monteCarloRuns;
		std::vector<JFMDerivedAnalysis> derivedAnalyses;
		std::vector<JFMScriptInfo> scripts;
	};
} // namespace SST::Prototypes::JFM
