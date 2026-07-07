#include <gtest/gtest.h>

#include "Storage/JFMProjectYamlSerializer.hpp"

namespace SST::Prototypes::JFM
{
	TEST(JFMProjectYamlSerializerTests, RoundTripsMetadataWithoutInlineRawArrays)
	{
		JFMProject project;
		project.id = "project-1";
		project.name = "JFM test";
		project.metadata["owner"] = "test";

		JFMCharacteristic characteristic;
		characteristic.id = "ch-1";
		characteristic.name = "Curve";
		characteristic.sourcePath = "data/example.dat";
		characteristic.parser = "dat";
		characteristic.temperatureK = 300.0;
		characteristic.temperatureSource = "filename";
		characteristic.columns.push_back({"Voltage", "V", "voltage"});
		characteristic.columns.push_back({"Current", "A", "current"});
		characteristic.voltage = {0.0, 1.0, 2.0};
		characteristic.current = {1.0, 2.0, 3.0};
		characteristic.sweep.pointCount = characteristic.voltage.size();

		project.series.push_back(JFMSeries{"series-1", "Series", {characteristic}});

		auto text = JFMProjectYamlSerializer::Serialize(project);
		ASSERT_TRUE(text) << text.Error().technicalDetails;
		EXPECT_NE(text->find("raw_data: external_only"), std::string::npos);
		EXPECT_EQ(text->find("1.0, 2.0, 3.0"), std::string::npos);

		auto loaded = JFMProjectYamlSerializer::Deserialize(*text);
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;
		EXPECT_EQ(loaded->name, "JFM test");
		ASSERT_EQ(loaded->series.size(), 1);
		ASSERT_EQ(loaded->series.front().characteristics.size(), 1);
		ASSERT_TRUE(loaded->series.front().characteristics.front().temperatureK.has_value());
		EXPECT_DOUBLE_EQ(*loaded->series.front().characteristics.front().temperatureK, 300.0);
		EXPECT_TRUE(loaded->series.front().characteristics.front().voltage.empty());
	}
} // namespace SST::Prototypes::JFM
