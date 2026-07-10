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
		EXPECT_FALSE(selected->info.id.empty());
		EXPECT_EQ(store.GetProjectSnapshot().series.front().characteristicIds.front(), selected->info.id);
	}
} // namespace SST::Prototypes::JFM
