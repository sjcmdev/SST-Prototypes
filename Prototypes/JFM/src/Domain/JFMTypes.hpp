#pragma once

#include "Core/Utils/Path.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SST::Prototypes::JFM
{
	inline constexpr int JFMProjectSchemaVersion = 2;
	using ProjectHandle = std::uint64_t;
	inline constexpr ProjectHandle InvalidProjectHandle = 0;
	using JFMOperationId = std::uint64_t;
	inline constexpr JFMOperationId InvalidJFMOperationId = 0;
	using CharacteristicId = std::uint64_t;
	inline constexpr CharacteristicId InvalidCharacteristicId = 0;

	struct JFMCharacteristicRef
	{
		ProjectHandle project = InvalidProjectHandle;
		CharacteristicId characteristic = InvalidCharacteristicId;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return project != InvalidProjectHandle && characteristic != InvalidCharacteristicId;
		}

		bool operator==(const JFMCharacteristicRef &) const = default;
	};

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

	struct JFMNameVariableRule
	{
		std::string name;
		std::string source = "stem";
		int tokenIndex = -1;
		int substringStart = 0;
		int substringLength = -1;
	};

	struct JFMImportOptions
	{
		std::string parser = "auto";
		std::string namePattern = "{stem}";
		std::string shortNamePattern;
		std::string variableDelimiter = "_";
		std::vector<JFMNameVariableRule> customVariables;
		std::string delimiter;
		std::string decimalSeparator = "auto";
		std::string temperatureExtraction = "auto";
		std::string customScriptPath;
	};

	enum class JFMImportPlacement
	{
		NewSeries,
		ExistingSeries,
		PreserveFolderTree
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
		JFMImportOptions importOptions;
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

	struct JFMGroup
	{
		std::string id;
		std::string name;
		std::string parentId;
		std::unordered_map<std::string, std::string> metadata;
	};

	struct JFMSeries
	{
		std::string id;
		std::string name;
		std::string groupId;
		std::vector<CharacteristicId> characteristicIds;
		std::unordered_map<std::string, std::string> metadata;
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

	enum class JFMGroupingMode
	{
		Exact,
		NumericStep
	};

	struct JFMGroupingDefinition
	{
		std::string id;
		std::string name;
		std::vector<std::string> sourceSeriesIds;
		std::string metadataKey = "temperature_K";
		JFMGroupingMode mode = JFMGroupingMode::NumericStep;
		double numericStep = 1.0;
		bool includeMissing = true;
	};

	struct JFMGroupingBucket
	{
		std::string key;
		std::string label;
		std::vector<CharacteristicId> characteristicIds;
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
		std::vector<JFMGroupingDefinition> groupings;
	};
} // namespace SST::Prototypes::JFM
