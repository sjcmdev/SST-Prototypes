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
		EXPECT_EQ(loaded->characteristics.front().importOptions.delimiter, "\t");
		ASSERT_EQ(loaded->characteristics.front().importOptions.customVariables.size(), 1);
		EXPECT_EQ(loaded->characteristics.front().importOptions.customVariables.front().name, "sample");
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
	}
} // namespace SST::Prototypes::JFM
