#include <gtest/gtest.h>
#include "Domain/CapLabSourceOperations.hpp"
#include "CapLabCommands.hpp"
#include "TestProject.hpp"
namespace SST::Prototypes::CapLab {
namespace {
Json SourcePreview() {
    auto m=FixturePreview(); m["kind"]="FrequencySweep"; m["variant"]="Cp_Rp";
    m["names"]={"f","C","Rp"}; m["units"]={"Hz","F","Ohm"}; m["frequency_Hz"]=nullptr;
    m["temperature_K"]=200; m["columns"][1][0]=2e-9;
    return {{"samples",Json::array({{{"name","sample"},{"measurements",Json::array({m})}}})},{"historical",Json::array()}};
}
}
TEST(CapLabSources, ReplaceAndUndoKeepIdentityAndRejectStaleDiff) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>();
    auto commands=CreateRef<CommandRegistry>(); CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
    state.Document().project=FixtureProject();
    const auto before=Json::parse(Serialize(state.Document().project));
    const auto id=state.Document().project.sweeps[0].id;
    auto draft=state.EditSnapshot(); state.sourceDiff=ReplaceMeasurement(draft,id,SourcePreview());
    state.sourceDiff["import_preview"]=SourcePreview(); state.sourceDraft=draft; state.sourceRevision=state.Document().revision;
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.accept_source"}));
    ASSERT_EQ(state.Document().project.measurements.size(),1);
    EXPECT_EQ(state.Document().project.measurements[0].at("id"),id);
    const auto &engine=*state.Document().project.engine;
    const auto plot=engine.children(engine.children(state.Document().project.graph).at(0)).at(0);
    EXPECT_NE(engine.object(plot).name.find("T = 200"),std::string::npos);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Json::parse(Serialize(state.Document().project)),before);
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(state.Document().project.measurements[0].at("id"),id);
    state.sourceDraft=draft; state.sourceRevision=state.Document().revision; state.Changed();
    EXPECT_FALSE(commands->Execute(CommandID{"caplab.accept_source"}));
}
TEST(CapLabSources, ReimportPreservesUserCurveLabels) {
    auto project=FixtureProject();
    const auto plot=project.engine->children(project.engine->children(project.graph).at(0)).at(0);
    auto object=project.engine->object(plot); object.name="My measured C-f";
    project.engine->change("Custom label",{std::move(object)}).apply(*project.engine);
    ReplaceMeasurement(project,project.sweeps[0].id,SourcePreview());
    EXPECT_EQ(project.engine->object(plot).name,"My measured C-f");
}
TEST(CapLabSources, ImportAndSourceDiffRequireSeparateMetadataConfirmation) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>();
    auto commands=CreateRef<CommandRegistry>(); CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
    state.Document().project=FixtureProject();
    const auto before=Serialize(state.Document().project);
    auto preview=SourcePreview();
    preview["samples"][0]["measurements"][0]["metadata_conflicts"]=Json::array({{{"field","temperature_K"},{"header",200},{"candidate",300}}});
    auto draft=state.EditSnapshot(); state.sourceDiff=ReplaceMeasurement(draft,draft.sweeps[0].id,preview);
    state.sourceDiff["import_preview"]=preview; state.sourceDraft=draft; state.sourceRevision=state.Document().revision;
    state.metadataConfirmed=true;
    EXPECT_FALSE(commands->Execute(CommandID{"caplab.accept_source"}));
    EXPECT_EQ(Serialize(state.Document().project),before);
    state.sourceMetadataConfirmed=true;
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.accept_source"}));
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(state.Document().project),before);
    state.metadataConfirmed=false; state.preview=preview; state.previewRevision=state.Document().revision;
    EXPECT_FALSE(commands->Execute(CommandID{"caplab.import"}));
    EXPECT_EQ(Serialize(state.Document().project),before);
}
TEST(CapLabSources, LegacyPreviewConflictsRequireConfirmation) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>();
    auto commands=CreateRef<CommandRegistry>(); CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
    auto preview=FixturePreview();
    preview["metadata_conflicts"]=Json::array({{{"field","temperature_K"},{"header",200},{"candidate",100}}});
    state.preview=preview; state.temperature=200;
    EXPECT_FALSE(commands->Execute(CommandID{"caplab.import"}));
    EXPECT_TRUE(state.Document().project.samples.empty());
    state.metadataConfirmed=true;
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
    EXPECT_EQ(state.Document().project.sweeps[0].temperature_K,200);
    ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(state.Document().project.samples.empty());
}
TEST(CapLabSources, PlatformDropQueuesSamePreviewCommandAndOwnsUnicodePaths) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>();
    auto commands=CreateRef<CommandRegistry>(); CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
    std::vector<Path> received; auto subscription=bus->Subscribe<DocumentRequested>([&](const auto &event) {
        EXPECT_EQ(event.operation,Operation::Preview); received=event.sources;
    });
    FilesDroppedEvent event({Path{std::filesystem::path(u8"D:/測定/żółć.dat")}});
    EventVariant queued=event; auto &owned=std::get<FilesDroppedEvent>(queued);
    state.OnEvent(owned); EXPECT_TRUE(owned.handled); EXPECT_TRUE(received.empty());
    bus->ProcessQueue(); ASSERT_EQ(received.size(),1); EXPECT_EQ(received[0],event.paths[0]);
    EXPECT_FALSE(state.preview); EXPECT_TRUE(state.Document().project.samples.empty());
}
TEST(CapLabSources, NamedProfileUsesEngineOptionsAndUndo) {
    pl::ImportOptions options; options.decimal=','; options.extra={{"options",R"({"profile":"lab_is_z","imaginary_sign":"-ImZ"})"}};
    const auto text=pl::importProfilesJson({{"lab",options}});
    EXPECT_EQ(pl::importProfilesJson(pl::importProfilesFromJson(text)),text);
    EXPECT_THROW(pl::importProfilesFromJson(R"({"x":{"delimiter":"xx","decimal":".","header":0,"units":0,"extra":{}}})"),std::exception);
    EXPECT_THROW(pl::importProfilesJson({{"",options}}),std::exception);
    auto invalid=FixtureProject(); auto broken=options; broken.extra["options"]="[";
    invalid.importProfiles["broken"]=broken;
    EXPECT_THROW(Serialize(invalid),std::exception);
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>();
    auto commands=CreateRef<CommandRegistry>(); CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
    state.importOptions=options.extra.at("options"); state.decimal=',';
    CommandContext context; context.Set("request",Json{{"action","save"},{"name","lab"}});
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.profile"},std::move(context)));
    const auto restored=Parse(Serialize(state.Document().project)).importProfiles.at("lab");
    EXPECT_EQ(restored.decimal,',');
    EXPECT_EQ(Json::parse(restored.extra.at("options")),Json::parse(options.extra.at("options")));
    ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(state.Document().project.importProfiles.empty());
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(state.Document().project.importProfiles.size(),1);
}
}
