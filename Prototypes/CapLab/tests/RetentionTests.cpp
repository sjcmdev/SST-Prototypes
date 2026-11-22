#include <gtest/gtest.h>
#include "CapLabCommands.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "IO/PrototypePythonBindings.hpp"
#include <cstdio>
#include <cstdlib>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
using namespace SST;
using namespace SST::Prototypes::CapLab;
TEST(PerfRetain, HundredUndoStepsLargestFixture) {
    if (!std::getenv("SST_MEMORY_PROBE")) GTEST_SKIP();
    RegisterPrototypePythonModule(); PythonInterpreter interpreter; ASSERT_TRUE(interpreter.Start());
    auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
    auto commands=CreateRef<CommandRegistry>(); auto undo=CreateRef<UndoStack>();
    CapLabWorkflow w(jobs,bus,undo); RegisterCommands(commands,bus,w);
    auto subscription=bus->Subscribe<DocumentRequested>([&](const auto& e){w.Start(e.operation,e.target);});
    w.importOptions={{"profile","propho_json"}};
    CommandContext request; request.Set("path",Path{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures/m2/samples_Cd_diffusion.json"});
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.preview"},std::move(request)));
    bus->ProcessQueue(); jobs->Shutdown(); w.OnUpdate(0);
    ASSERT_TRUE(w.preview)<<w.status; ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}))<<w.status;
    undo->Clear(); undo->MarkClean();
    const auto id=w.Document().project.samples.front().id;
    const auto originalName=w.Document().project.samples.front().name;
    for(int i=0;i<100;++i) {
        CommandContext edit; edit.Set("edit",Json{{"action","rename_sample"},{"sample_id",id},{"name","Memory "+std::to_string(i)}});
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.tree"},std::move(edit)))<<w.status;
        PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof(m);
        ASSERT_TRUE(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)));
        std::printf("RETAIN caplab step=%d private_bytes=%zu peak_commit_bytes=%zu undo=%zu\n",i+1,m.PrivateUsage,m.PeakPagefileUsage,undo->GetUndoDepth());
    }
    EXPECT_LE(undo->GetUndoDepth(),CapLabDocument::UndoSteps);
    EXPECT_LE(undo->GetByteSize(),CapLabDocument::UndoBytes);
    const auto depth=undo->GetUndoDepth();
    while(undo->CanUndo()) ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(w.Document().project.samples.front().name,depth==100?originalName:"Memory "+std::to_string(99-depth));
    EXPECT_EQ(undo->IsClean(),depth==100);
    while(undo->CanRedo()) ASSERT_TRUE(undo->Redo());
    EXPECT_EQ(w.Document().project.samples.front().name,"Memory 99");
}

TEST(CapLabRetention, UndoByteLimitAndCleanPoint) {
    struct Change final : ICommand {
        int &value; Change(int &v):value(v){}
        Result<void> Execute(CommandContext &) override {++value;return {};}
        Result<void> Undo(CommandContext &) override {--value;return {};}
        Result<void> Redo(CommandContext &) override {++value;return {};}
        bool IsUndoable() const noexcept override {return true;}
        std::string Description() const override {return "bounded";}
        std::size_t ByteSize() const noexcept override {return 1024;}
    };
    UndoStack stack; stack.SetLimits(50,3072); int value=0; CommandContext context;
    for(int i=0;i<10;++i) {auto c=CreateUnique<Change>(value);ASSERT_TRUE(c->Execute(context));ASSERT_TRUE(stack.PushExecuted(std::move(c)));}
    EXPECT_EQ(stack.GetUndoDepth(),3); EXPECT_EQ(stack.GetByteSize(),3072);
    while(stack.CanUndo())ASSERT_TRUE(stack.Undo());
    EXPECT_EQ(value,7); EXPECT_FALSE(stack.IsClean());
    stack.MarkClean(); ASSERT_TRUE(stack.Redo()); EXPECT_FALSE(stack.IsClean()); ASSERT_TRUE(stack.Undo()); EXPECT_TRUE(stack.IsClean());
    while(stack.CanRedo())ASSERT_TRUE(stack.Redo()); EXPECT_EQ(value,10);
}

TEST(CapLabRetention, FailedStreamingSavePreservesDestination) {
    const auto path=Path{std::filesystem::temp_directory_path()/"perfret1-atomic.txt"};std::string error,text;
    ASSERT_TRUE(TextFileIO::Save(path,"original",error));
    EXPECT_FALSE(TextFileIO::Save(path,[](FILE *f){std::fwrite("partial",1,7,f);return false;},error));
    ASSERT_TRUE(TextFileIO::Load(path,text,error));EXPECT_EQ(text,"original");
    EXPECT_THROW(TextFileIO::Save(path,[](FILE*)->bool{throw std::runtime_error("interrupted");},error),std::runtime_error);
    ASSERT_TRUE(TextFileIO::Load(path,text,error));EXPECT_EQ(text,"original");
    std::filesystem::remove(path.Native());
}
