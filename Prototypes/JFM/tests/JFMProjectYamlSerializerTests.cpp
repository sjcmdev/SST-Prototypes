#include <gtest/gtest.h>

#include "IO/JFMProjectYamlSerializer.hpp"

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

		project.characteristics.push_back(characteristic);
		project.groups.push_back(JFMGroup{"group-1", "Folder", {}});
		project.series.push_back(JFMSeries{"series-1", "Series", "group-1", {1}});

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
		ASSERT_EQ(loaded->series.front().characteristicIds.size(), 1);
		EXPECT_EQ(loaded->series.front().characteristicIds.front(), 1);
		ASSERT_EQ(loaded->characteristics.size(), 1);
		ASSERT_TRUE(loaded->characteristics.front().temperatureK.has_value());
		EXPECT_DOUBLE_EQ(*loaded->characteristics.front().temperatureK, 300.0);
		EXPECT_EQ(loaded->characteristics.front().sourcePath.String(), Path::FromResolved("data/example.dat").String());
	}
} // namespace SST::Prototypes::JFM
