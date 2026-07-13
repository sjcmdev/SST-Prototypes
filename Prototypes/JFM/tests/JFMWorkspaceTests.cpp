#include <gtest/gtest.h>

#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	TEST(JFMWorkspaceStoreTests, CreatesDefaultProject)
	{
		JFMWorkspaceStore store;
		const JFMProject project = store.GetProjectSnapshot();

		EXPECT_EQ(project.name, "Untitled JFM Project");
		ASSERT_EQ(project.series.size(), 1);
		EXPECT_EQ(store.GetSelectedSeriesId(), project.series.front().id);
	}

	TEST(JFMWorkspaceStoreTests, AddsImportedCharacteristicAndSelectsIt)
	{
		JFMWorkspaceStore store;
		JFMCharacteristic characteristic;
		characteristic.info.name = "T300";
		characteristic.data.voltage = {0.0, 1.0};
		characteristic.data.current = {1.0e-6, 2.0e-6};

		store.AddImportedCharacteristics({characteristic});
		const auto selected = store.GetSelectedCharacteristic();

		ASSERT_TRUE(selected.has_value());
		EXPECT_EQ(selected->info.name, "T300");
		EXPECT_EQ(selected->data.voltage.size(), 2);
		EXPECT_NE(selected->info.id, InvalidCharacteristicId);
		EXPECT_EQ(store.GetProjectSnapshot().series.front().characteristicIds.front(), selected->info.id);
	}

	TEST(JFMWorkspaceStoreTests, UpdatesCharacteristicDataWithoutChangingTree)
	{
		JFMWorkspaceStore store;
		JFMProject project;
		project.id = "project";
		project.name = "Loaded";
		project.characteristics.push_back(JFMCharacteristicInfo{7, "Loaded Curve"});
		project.series.push_back(JFMSeries{"series-1", "Series", {}, {7}});
		store.ReplaceProject(project, std::nullopt);

		JFMCharacteristic materialized;
		materialized.info.id = 7;
		materialized.data.voltage = {0.0, 1.0};
		materialized.data.current = {2.0, 3.0};

		EXPECT_EQ(store.UpdateCharacteristicData({materialized}), 1);
		const auto characteristic = store.FindCharacteristic(7);

		ASSERT_TRUE(characteristic.has_value());
		EXPECT_EQ(characteristic->info.name, "Loaded Curve");
		EXPECT_EQ(characteristic->data.voltage.size(), 2);
		EXPECT_EQ(store.GetProjectSnapshot().series.front().characteristicIds.front(), 7);
	}

	TEST(JFMWorkspaceStoreTests, CreatesNestedGroupsAndSeries)
	{
		JFMWorkspaceStore store;
		const std::string groupId = store.FindOrCreateGroupPath({"root", "child"});
		const JFMSeries series = store.AddSeries("Imported Data", groupId);
		const JFMProject project = store.GetProjectSnapshot();

		ASSERT_EQ(project.groups.size(), 2);
		EXPECT_EQ(project.groups.front().name, "root");
		EXPECT_EQ(project.groups.back().name, "child");
		EXPECT_EQ(project.groups.back().parentId, project.groups.front().id);
		EXPECT_EQ(series.groupId, groupId);
	}
} // namespace SST::Prototypes::JFM
