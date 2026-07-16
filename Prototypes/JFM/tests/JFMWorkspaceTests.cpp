#include <gtest/gtest.h>

#include "App/JFMCommands.hpp"
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

	TEST(JFMWorkspaceStoreTests, IsolatesEqualCharacteristicIdsAcrossProjects)
	{
		JFMWorkspaceStore store;
		const ProjectHandle firstProject = store.GetActiveProjectHandle();
		JFMCharacteristic first;
		first.info.id = 1;
		first.info.name = "First";
		first.data.voltage = {1.0};
		(void)store.AddCharacteristics(firstProject, {first}, store.GetProjectSnapshot(firstProject).series.front().id);

		JFMProject secondSnapshot;
		secondSnapshot.id = "second";
		secondSnapshot.name = "Second project";
		secondSnapshot.characteristics.push_back(JFMCharacteristicInfo{1, "Second"});
		secondSnapshot.series.push_back(JFMSeries{"series-2", "Series", {}, {1}});
		const ProjectHandle secondProject = store.AttachProject(std::move(secondSnapshot), std::nullopt);
		JFMCharacteristic second;
		second.info.id = 1;
		second.data.voltage = {2.0};
		EXPECT_EQ(store.UpdateCharacteristicData(secondProject, {second}), 1);

		const auto firstResult = store.FindCharacteristic({firstProject, 1});
		const auto secondResult = store.FindCharacteristic({secondProject, 1});
		ASSERT_TRUE(firstResult);
		ASSERT_TRUE(secondResult);
		EXPECT_EQ(firstResult->info.name, "First");
		EXPECT_EQ(secondResult->info.name, "Second");
		EXPECT_DOUBLE_EQ(firstResult->data.voltage.front(), 1.0);
		EXPECT_DOUBLE_EQ(secondResult->data.voltage.front(), 2.0);
	}

	TEST(JFMWorkspaceStoreTests, ResolvesDynamicNumericExactAndMissingGroupingBuckets)
	{
		JFMWorkspaceStore store;
		const ProjectHandle project = store.GetActiveProjectHandle();
		const JFMSeries source = store.AddSeries(project, "Source");
		const JFMSeries ignored = store.AddSeries(project, "Ignored");

		JFMCharacteristic low;
		low.info.name = "Low";
		low.info.temperatureK = 300.2;
		low.info.parser = "dat";
		JFMCharacteristic high;
		high.info.name = "High";
		high.info.temperatureK = 300.7;
		high.info.parser = "dat";
		JFMCharacteristic missing;
		missing.info.name = "Missing";
		missing.info.parser = "scaps";
		(void)store.AddCharacteristics(project, {low, high, missing}, source.id);
		JFMCharacteristic outside;
		outside.info.name = "Outside";
		outside.info.temperatureK = 300.2;
		(void)store.AddCharacteristics(project, {outside}, ignored.id);

		JFMGroupingDefinition numeric;
		numeric.name = "Temperature";
		numeric.sourceSeriesIds = {source.id};
		numeric.metadataKey = "temperature_K";
		numeric.mode = JFMGroupingMode::NumericStep;
		numeric.numericStep = 1.0;
		numeric.includeMissing = true;
		const JFMGroupingDefinition storedNumeric = store.AddGrouping(project, numeric);
		auto buckets = store.ResolveGrouping(project, storedNumeric.id);
		ASSERT_EQ(buckets.size(), 3);
		EXPECT_EQ(buckets[0].label, "300 K");
		EXPECT_EQ(buckets[1].label, "301 K");
		EXPECT_EQ(buckets[2].key, "missing");

		JFMCharacteristic added;
		added.info.name = "Added";
		added.info.temperatureK = 300.1;
		(void)store.AddCharacteristics(project, {added}, source.id);
		buckets = store.ResolveGrouping(project, storedNumeric.id);
		ASSERT_EQ(buckets.front().characteristicIds.size(), 2);

		JFMGroupingDefinition exact;
		exact.name = "Parser";
		exact.sourceSeriesIds = {source.id};
		exact.metadataKey = "parser";
		exact.mode = JFMGroupingMode::Exact;
		exact.includeMissing = false;
		const auto exactBuckets = store.ResolveGrouping(project, exact);
		ASSERT_EQ(exactBuckets.size(), 2);
		EXPECT_EQ(exactBuckets.front().label, "dat");
		EXPECT_EQ(exactBuckets.back().label, "scaps");
	}

	TEST(JFMWorkspaceStoreTests, ProjectStateSnapshotsRestoreStructureAndSharedRawData)
	{
		JFMWorkspaceStore store;
		const ProjectHandle project = store.GetActiveProjectHandle();
		const std::string seriesId = store.GetProjectSnapshot(project).series.front().id;
		JFMCharacteristic characteristic;
		characteristic.info.name = "Original";
		characteristic.data.voltage = {0.0, 1.0};
		const CharacteristicId id = store.AddCharacteristics(project, {characteristic}, seriesId).front();
		const auto before = store.CaptureProjectState(project);
		ASSERT_TRUE(before);

		EXPECT_TRUE(store.RenameCharacteristic(project, id, "Changed"));
		EXPECT_TRUE(store.DeleteCharacteristic(project, id));
		ASSERT_TRUE(store.RestoreProjectState(project, *before));

		const auto restored = store.FindCharacteristic({project, id});
		ASSERT_TRUE(restored);
		EXPECT_EQ(restored->info.name, "Original");
		EXPECT_EQ(restored->data.voltage, (std::vector<double>{0.0, 1.0}));
	}

	TEST(JFMWorkspaceStoreTests, SaveCompletionKeepsLaterEditsDirty)
	{
		JFMWorkspaceStore store;
		const ProjectHandle project = store.GetActiveProjectHandle();
		(void)store.AddSeries(project, "Before Save");
		const std::uint64_t savedRevision = store.GetProjectRevision(project);
		(void)store.AddGroup(project, "Edited During Save");

		store.MarkProjectSaved(project, Path::FromResolved("project.jfm.yaml"), savedRevision);

		EXPECT_TRUE(store.IsDirty(project));
	}

	TEST(JFMWorkspaceStoreTests, ProjectEditCommandRestoresAndActivatesItsProject)
	{
		auto store = CreateRef<JFMWorkspaceStore>();
		const ProjectHandle editedProject = store->GetActiveProjectHandle();
		const ProjectHandle otherProject = store->CreateProject("Other");
		JFMProjectEditRequest request;
		request.project = editedProject;
		request.description = "Rename project";
		request.apply = [](JFMWorkspaceStore &workspace, ProjectHandle project) {
			(void)workspace.RenameProject(project, "Renamed");
			return Result<void>{};
		};
		auto command = CreateJFMProjectEditCommand(store, std::move(request));
		CommandContext context;

		ASSERT_TRUE(command->Execute(context));
		EXPECT_EQ(store->GetProjectSnapshot(editedProject).name, "Renamed");
		EXPECT_EQ(store->GetActiveProjectHandle(), otherProject);
		ASSERT_TRUE(command->Undo(context));
		EXPECT_EQ(store->GetProjectSnapshot(editedProject).name, "Untitled JFM Project");
		EXPECT_EQ(store->GetActiveProjectHandle(), editedProject);
		ASSERT_TRUE(store->SetActiveProject(otherProject));
		ASSERT_TRUE(command->Redo(context));
		EXPECT_EQ(store->GetProjectSnapshot(editedProject).name, "Renamed");
		EXPECT_EQ(store->GetActiveProjectHandle(), editedProject);
	}
} // namespace SST::Prototypes::JFM
