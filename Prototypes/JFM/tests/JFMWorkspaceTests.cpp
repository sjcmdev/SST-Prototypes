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

	TEST(JFMWorkspaceStoreTests, MultiSelectAndDeleteSeriesRemovesCharacteristics)
	{
		JFMWorkspaceStore store;
		const JFMSeries series = store.AddSeries("To delete");

		JFMCharacteristic first;
		first.info.name = "A";
		JFMCharacteristic second;
		second.info.name = "B";
		const std::vector<CharacteristicId> ids = store.AddCharacteristics({first, second}, series.id);

		ASSERT_EQ(ids.size(), 2);
		store.SetSelectedCharacteristics(ids, ids.front(), series.id);
		EXPECT_EQ(store.GetSelectedCharacteristicIds().size(), 2);

		EXPECT_TRUE(store.DeleteSeries(series.id));
		EXPECT_FALSE(store.FindCharacteristic(ids.front()).has_value());
		EXPECT_FALSE(store.FindCharacteristic(ids.back()).has_value());
	}

	TEST(JFMWorkspaceStoreTests, RegistersReloadsAndRemovesScripts)
	{
		JFMWorkspaceStore store;
		const JFMScriptInfo script = store.AddScript(Path::FromResolved("custom_parser.py"));

		JFMProject project = store.GetProjectSnapshot();
		ASSERT_EQ(project.scripts.size(), 1);
		EXPECT_EQ(project.scripts.front().name, "custom_parser.py");

		EXPECT_TRUE(store.ReloadScript(script.id));
		project = store.GetProjectSnapshot();
		EXPECT_EQ(project.scripts.front().reloadCount, 1);

		EXPECT_TRUE(store.RemoveScript(script.id));
		EXPECT_TRUE(store.GetProjectSnapshot().scripts.empty());
	}

	TEST(JFMWorkspaceStoreTests, RegistersDefaultImportScriptsOnce)
	{
		JFMWorkspaceStore store;
		const Path scripts = Path::FromResolved("python/jfm_import");

		store.EnsureDefaultScripts(scripts);
		store.EnsureDefaultScripts(scripts);

		const JFMProject project = store.GetProjectSnapshot();
		ASSERT_EQ(project.scripts.size(), 2);
		EXPECT_EQ(project.scripts.front().name, "dat_parser.py");
		EXPECT_EQ(project.scripts.back().name, "scaps_iv_parser.py");
	}

	TEST(JFMWorkspaceStoreTests, RenamesProjectTreeItems)
	{
		JFMWorkspaceStore store;
		const JFMGroup group = store.AddGroup("Group");
		const JFMSeries series = store.AddSeries("Series", group.id);
		JFMCharacteristic characteristic;
		characteristic.info.name = "Curve";
		const CharacteristicId id = store.AddCharacteristics({characteristic}, series.id).front();

		EXPECT_TRUE(store.RenameProject("Renamed Project"));
		EXPECT_TRUE(store.RenameGroup(group.id, "Renamed Group"));
		EXPECT_TRUE(store.RenameSeries(series.id, "Renamed Series"));
		EXPECT_TRUE(store.RenameCharacteristic(id, "Renamed Curve"));

		const JFMProject project = store.GetProjectSnapshot();
		EXPECT_EQ(project.name, "Renamed Project");
		EXPECT_EQ(project.groups.back().name, "Renamed Group");
		EXPECT_EQ(project.series.back().name, "Renamed Series");
		EXPECT_EQ(store.FindCharacteristic(id)->info.name, "Renamed Curve");
	}

	TEST(JFMWorkspaceStoreTests, StoresShortNamesForTreeItems)
	{
		JFMWorkspaceStore store;
		const JFMGroup group = store.AddGroup("Long Group");
		const JFMSeries series = store.AddSeries("Long Series", group.id);
		JFMCharacteristic characteristic;
		characteristic.info.name = "Long Characteristic";
		const CharacteristicId id = store.AddCharacteristics({characteristic}, series.id).front();

		EXPECT_TRUE(store.SetGroupShortName(group.id, "G1"));
		EXPECT_TRUE(store.SetSeriesShortName(series.id, "S1"));
		EXPECT_TRUE(store.SetCharacteristicShortName(id, "C1"));

		const JFMProject project = store.GetProjectSnapshot();
		EXPECT_EQ(project.groups.back().metadata.at("short_name"), "G1");
		EXPECT_EQ(project.series.back().metadata.at("short_name"), "S1");
		EXPECT_EQ(store.FindCharacteristic(id)->info.metadata.at("short_name"), "C1");
	}
} // namespace SST::Prototypes::JFM
