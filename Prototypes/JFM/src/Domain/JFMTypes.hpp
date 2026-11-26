#pragma once

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SST::Prototypes::JFM
{
	inline constexpr int JFMProjectSchemaVersion = 5;
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

	enum class JFMTreeItemKind
	{
		None,
		Project,
		Group,
		Series,
		Characteristic
	};

	struct JFMTreeItemRef
	{
		ProjectHandle project = InvalidProjectHandle;
		JFMTreeItemKind kind = JFMTreeItemKind::None;
		std::string id;
		std::vector<CharacteristicId> characteristicIds;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return project != InvalidProjectHandle && kind != JFMTreeItemKind::None;
		}
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

	struct JFMParameterEstimate
	{
		double value = 0.0;
		std::optional<double> standardError;
		std::string source;
		std::vector<std::string> warnings;
	};

	struct JFMFitRange
	{
		std::size_t lowerIndex = 0;
		std::size_t upperIndex = 0;
		double lowerVoltage = 0.0;
		double upperVoltage = 0.0;
		std::string reason = "automatic";
	};

	struct JFMPrefitState
	{
		std::string modelId = "SD-RsRsh";
		std::string status = "not_started";
		std::optional<double> manualLowerVoltage;
		std::optional<double> manualUpperVoltage;
		JFMFitRange fitRange;
		std::unordered_map<std::string, JFMParameterEstimate> parameters;
		std::vector<std::string> warnings;
		// When true, JFMPrefitJob runs the Python hierarchical branch/model/range
		// search (auto_prefit_characteristic) instead of prefitting the fixed
		// modelId above (docs/model-detection.md §12.3). Sticky across re-fits
		// until an explicit manual model/range edit turns it back off.
		bool autoModelSelection = false;
	};

	struct JFMPrefitCurve
	{
		std::vector<double> voltage;
		std::vector<double> current;
	};

    struct JFMObjectiveSummary
    {
        std::string mode = "log_legacy";
        std::string currentUnit = "A";
        double shift = 0;
        double sigmaAbsolute = 0;
        double sigmaRelative = 0;
        double switchK = 3;
        int linearPoints = 0;
        int logPoints = 0;
    };
	struct JFMFitState
	{
		std::string modelId = "SD-RsRsh";
		std::string status = "not_started";
		JFMFitRange fitRange;
		std::unordered_map<std::string, JFMParameterEstimate> parameters;
		double chiSquared = 0.0;
		double reducedChiSquared = 0.0;
		int iterations = 0;
		bool converged = false;
        JFMObjectiveSummary objective;
        bool accepted = false;
        bool flatDirections = false;
        std::string fitMethod = "NM";
        std::string stopReason = "unknown";
        int lmStatus = -1;
		std::string backend = "cpu";
		std::vector<std::string> warnings;
	};

	struct JFMFitCurve
	{
		std::vector<double> voltage;
		std::vector<double> current;
	};

	struct JFMMonteCarloState
	{
        JFMObjectiveSummary objective;
		std::string modelId = "SD-RsRsh";
		std::string status = "not_started";
		int sampleCount = 0;
		int acceptedCount = 0;
		int convergedCount = 0;
		double noisePercent = 0.0;
		double startPerturbationPercent = 0.0;
		std::uint32_t seed = 42;
		std::string backend;
		Path outputPath;
        std::string sampleSaveStatus = "not_requested";
        std::string sampleSaveError;
        std::string sampleStorageId;
		double totalMilliseconds = 0.0;
		std::vector<std::string> warnings;
	};

	struct JFMMonteCarloSample
	{
		std::vector<double> parameters;
		double chiSquared = 0.0;
		double reducedChiSquared = 0.0;
		int iterations = 0;
        bool converged = false; // Legacy accepted flag retained for saved data.
        bool accepted = converged;
        bool solverConverged = false;
        std::string stopReason = "unknown";
        std::string fitMethod = "NM";
        bool flatDirections = false;
        bool usedGpu = false;
        int lmStatus = -1;
        double objectiveChiSquared = 0.0;
        int linearPoints = 0;
        int logPoints = 0;
    };

    struct JFMMonteCarloResult
	{
		std::vector<std::string> parameterNames;
		int freeParameterCount = 0;
		bool confidenceAvailable = true;
		std::vector<JFMMonteCarloSample> samples;
		std::vector<double> voltage;
		std::vector<double> idealCurrent;
        std::vector<double> currentSigma;
		// Live noisy data: at most 16 MiB, and no more than the requested saved-sample prefix.
		std::vector<double> noisyCurrent;
		// Persisted recipe reproduces all sample IDs without storing repeated noisy curves.
		std::optional<std::uint64_t> noiseSeed;
		std::vector<double> noiseAmplitude;
	};

	struct JFMMonteCarloBundleRecord
	{
		CharacteristicId characteristic = InvalidCharacteristicId;
		Ref<const JFMMonteCarloResult> result;
	};

	struct JFMArrheniusLayoutStyle
	{
		std::array<float, 4> color{0.26f, 0.47f, 0.86f, 1.0f};
		float pointSize = 5.0f;
		float lineWeight = 1.5f;
		int drawMode = 2;
		int marker = 0;
		bool visible = true;
	};

	struct JFMArrheniusFitLayout
	{
		std::string name;
		std::string modelName;
		int model = 0;
		int lower = 0;
		int upper = 0;
		std::array<float, 4> color{0.90f, 0.42f, 0.18f, 1.0f};
		float lineWeight = 2.5f;
		bool visible = true;
	};

	struct JFMArrheniusSeriesLayout
	{
		int sourceMode = 0;
		std::string sourceId;
		std::string title;
		int lower = 0;
		int upper = 0;
		JFMArrheniusLayoutStyle style;
		JFMArrheniusLayoutStyle tunedStyle{{1.0f, 0.52f, 0.10f, 1.0f}, 5.0f, 1.5f, 2, 0, false};
		std::vector<JFMArrheniusFitLayout> fits;
	};

	struct JFMArrheniusPlotLayout
	{
		std::uint64_t id = 0;
		std::string title;
		int quantity = 3;
		int fitModel = 0;
		bool preferFit = true;
		bool autoRefit = true;
		bool open = true;
		int dockSlot = -1;
		int activeSeries = 0;
		std::vector<JFMArrheniusSeriesLayout> series;
	};

	struct JFMArrheniusLayoutDocument
	{
		std::vector<JFMArrheniusPlotLayout> plots;
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
		std::string sourceRecordKey;
		std::string parser;
		std::optional<double> temperatureK;
		std::string temperatureSource = "missing";
		std::vector<JFMColumn> columns;
		JFMSweepInfo sweep;
		JFMQualityControl qc;
		std::unordered_map<std::string, std::string> metadata;
		JFMImportOptions importOptions;
		std::optional<JFMPrefitState> prefit;
		std::optional<JFMFitState> fit;
		std::optional<JFMMonteCarloState> monteCarlo;
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

	struct JFMCharacteristicRecord
	{
		JFMCharacteristicInfo info;
		Ref<const JFMCharacteristicData> data;
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

	struct JFMProjectBundleSnapshot
	{
		JFMProject project;
		std::vector<JFMCharacteristicRecord> characteristics;
		std::vector<JFMMonteCarloBundleRecord> monteCarloResults;
		std::optional<JFMArrheniusLayoutDocument> arrheniusLayout;
		std::uint64_t revision = 0;
	};
} // namespace SST::Prototypes::JFM
