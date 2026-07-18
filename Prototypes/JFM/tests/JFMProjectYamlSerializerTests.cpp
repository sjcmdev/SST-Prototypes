#include <gtest/gtest.h>

#include "IO/JFMProjectYamlSerializer.hpp"

#include <utility>

namespace SST::Prototypes::JFM
{
	TEST(JFMProjectYamlSerializerTests, RoundTripsMetadataWithoutInlineRawArrays)
	{
		JFMProject project;
		project.id = "project-1";
		project.name = "JFM test";
		project.metadata["owner"] = "test";

		JFMCharacteristicInfo characteristic;
		characteristic.id = 1;
		characteristic.name = "Curve";
		characteristic.sourcePath = Path::FromResolved("data/example.dat");
		characteristic.sourceRecordKey = "curve-0";
		characteristic.parser = "dat";
		characteristic.temperatureK = 300.0;
		characteristic.temperatureSource = "filename";
		characteristic.columns.push_back({"Voltage", "V", "voltage"});
		characteristic.columns.push_back({"Current", "A", "current"});
		characteristic.sweep.pointCount = 3;
		characteristic.importOptions.parser = "dat";
		characteristic.importOptions.delimiter = "\t";
		characteristic.importOptions.decimalSeparator = ".";
		characteristic.importOptions.temperatureExtraction = "filename";
		characteristic.importOptions.customVariables.push_back({"sample", "stem", 1, 0, 4});
		characteristic.prefit = JFMPrefitState{};
		characteristic.prefit->modelId = "SD-RsRsh-alpha-Rsh2";
		characteristic.prefit->status = "completed";
		characteristic.prefit->manualLowerVoltage = 0.1;
		characteristic.prefit->manualUpperVoltage = 0.7;
		characteristic.prefit->fitRange = {4, 48, 0.1, 0.7, "manual"};
		characteristic.prefit->parameters["A"] = JFMParameterEstimate{1.6, 0.05, "semilog_block", {}};
		characteristic.fit = JFMFitState{};
		characteristic.fit->modelId = characteristic.prefit->modelId;
		characteristic.fit->status = "completed";
		characteristic.fit->fitRange = characteristic.prefit->fitRange;
		characteristic.fit->parameters["A"] = JFMParameterEstimate{1.58, 0.01, "simplex_cpu", {}};
		characteristic.fit->reducedChiSquared = 0.98;
		characteristic.fit->iterations = 75;
		characteristic.fit->converged = true;
		characteristic.monteCarlo = JFMMonteCarloState{};
		characteristic.monteCarlo->modelId = characteristic.prefit->modelId;
		characteristic.monteCarlo->status = "completed";
		characteristic.monteCarlo->sampleCount = 512;
		characteristic.monteCarlo->acceptedCount = 490;
		characteristic.monteCarlo->backend = "cuda";
		characteristic.monteCarlo->outputPath = Path::FromResolved("results/mc");

		project.characteristics.push_back(characteristic);
		JFMGroup group{"group-1", "Folder", {}};
		group.metadata["short_name"] = "G1";
		project.groups.push_back(std::move(group));
		JFMSeries series{"series-1", "Series", "group-1", {1}};
		series.metadata["short_name"] = "S1";
		project.series.push_back(std::move(series));
		project.scripts.push_back(JFMScriptInfo{"script-1", "custom.py", Path::FromResolved("scripts/custom.py"), true, 2});
		project.groupings.push_back(JFMGroupingDefinition{
			"grouping-1",
			"Temperature",
			{"series-1"},
			"temperature_K",
			JFMGroupingMode::NumericStep,
			1.0,
			true});

		auto text = JFMProjectYamlSerializer::Serialize(project);
		ASSERT_TRUE(text) << text.Error().technicalDetails;
		EXPECT_NE(text->find("raw_data: external_only"), std::string::npos);
		EXPECT_EQ(text->find("1.0, 2.0, 3.0"), std::string::npos);

		auto loaded = JFMProjectYamlSerializer::Deserialize(*text);
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;
		EXPECT_EQ(loaded->name, "JFM test");
		ASSERT_EQ(loaded->series.size(), 1);
		ASSERT_EQ(loaded->groups.size(), 1);
		EXPECT_EQ(loaded->series.front().groupId, "group-1");
		EXPECT_EQ(loaded->groups.front().metadata.at("short_name"), "G1");
		EXPECT_EQ(loaded->series.front().metadata.at("short_name"), "S1");
		ASSERT_EQ(loaded->series.front().characteristicIds.size(), 1);
		EXPECT_EQ(loaded->series.front().characteristicIds.front(), 1);
		ASSERT_EQ(loaded->characteristics.size(), 1);
		ASSERT_EQ(loaded->scripts.size(), 1);
		EXPECT_EQ(loaded->scripts.front().path.String(), Path::FromResolved("scripts/custom.py").String());
		EXPECT_EQ(loaded->scripts.front().reloadCount, 2);
		ASSERT_EQ(loaded->groupings.size(), 1);
		EXPECT_EQ(loaded->groupings.front().name, "Temperature");
		EXPECT_EQ(loaded->groupings.front().sourceSeriesIds, (std::vector<std::string>{"series-1"}));
		EXPECT_EQ(loaded->groupings.front().mode, JFMGroupingMode::NumericStep);
		ASSERT_TRUE(loaded->characteristics.front().temperatureK.has_value());
		EXPECT_DOUBLE_EQ(*loaded->characteristics.front().temperatureK, 300.0);
		EXPECT_EQ(loaded->characteristics.front().sourcePath.String(), Path::FromResolved("data/example.dat").String());
		EXPECT_EQ(loaded->characteristics.front().sourceRecordKey, "curve-0");
		EXPECT_EQ(loaded->characteristics.front().importOptions.delimiter, "\t");
		ASSERT_EQ(loaded->characteristics.front().importOptions.customVariables.size(), 1);
		EXPECT_EQ(loaded->characteristics.front().importOptions.customVariables.front().name, "sample");
		ASSERT_TRUE(loaded->characteristics.front().prefit.has_value());
		EXPECT_EQ(loaded->characteristics.front().prefit->modelId, "SD-RsRsh-alpha-Rsh2");
		EXPECT_EQ(loaded->characteristics.front().prefit->fitRange.reason, "manual");
		EXPECT_DOUBLE_EQ(loaded->characteristics.front().prefit->parameters.at("A").value, 1.6);
		ASSERT_TRUE(loaded->characteristics.front().fit.has_value());
		EXPECT_DOUBLE_EQ(loaded->characteristics.front().fit->parameters.at("A").value, 1.58);
		EXPECT_TRUE(loaded->characteristics.front().fit->converged);
		ASSERT_TRUE(loaded->characteristics.front().monteCarlo.has_value());
		EXPECT_EQ(loaded->characteristics.front().monteCarlo->acceptedCount, 490);
		EXPECT_EQ(loaded->characteristics.front().monteCarlo->outputPath.String(), Path::FromResolved("results/mc").String());
	}

	TEST(JFMProjectYamlSerializerTests, MigratesSchemaOneWithDefaultGroupingAndMaterializationOptions)
	{
		const std::string yaml = R"yaml(
schema_version: 1
project:
  id: legacy
  name: Legacy
characteristics:
  - id: 7
    name: Curve
    parser: dat
    source_path: data/curve.dat
series:
  - id: series-1
    name: Series
    characteristics: [7]
)yaml";

		auto loaded = JFMProjectYamlSerializer::Deserialize(yaml);
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;
		EXPECT_EQ(loaded->schemaVersion, JFMProjectSchemaVersion);
		EXPECT_TRUE(loaded->groupings.empty());
		ASSERT_EQ(loaded->characteristics.size(), 1);
		EXPECT_EQ(loaded->characteristics.front().importOptions.parser, "dat");
		EXPECT_TRUE(loaded->characteristics.front().sourceRecordKey.empty());
	}

	TEST(JFMProjectYamlSerializerTests, MigratesSchemaTwoWithoutSourceRecordKey)
	{
		const std::string yaml = R"yaml(
schema_version: 2
project:
  id: legacy-v2
  name: Legacy V2
characteristics:
  - id: 9
    name: Renamed Curve
    parser: dat
    source_path: data/curves.dat
series:
  - id: series-1
    name: Series
    characteristics: [9]
)yaml";

		auto loaded = JFMProjectYamlSerializer::Deserialize(yaml);
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;
		EXPECT_EQ(loaded->schemaVersion, JFMProjectSchemaVersion);
		ASSERT_EQ(loaded->characteristics.size(), 1);
		EXPECT_TRUE(loaded->characteristics.front().sourceRecordKey.empty());
	}
} // namespace SST::Prototypes::JFM
