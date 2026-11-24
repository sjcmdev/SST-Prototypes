#include "Core/IO/Hdf5/Hdf5Session.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "sst/hdf5/storage.hpp"
#include <gtest/gtest.h>
#include <Windows.h>

TEST(Hdf5Core, EmbeddedPythonInterop) {
    SST::RegisterHdf5PythonModule();
    Py_InitializeEx(0);
    wchar_t executable[32768];
    auto length = GetModuleFileNameW(nullptr, executable, 32768);
    auto host = PyUnicode_FromWideChar(executable, length);
    ASSERT_EQ(PySys_SetObject("_sst_hdf5_test_host", host), 0);
    Py_DECREF(host);
    auto python = PyUnicode_FromString(HDF5_TEST_PYTHON);
    ASSERT_EQ(PySys_SetObject("_sst_hdf5_test_python", python), 0);
    Py_DECREF(python);
    const auto script = std::string("import sys\nsys.path.insert(0, r'") + HDF5_TEST_ENV + "')\n"
        "exec(compile(open(r'" HDF5_PYTHON_TEST "', encoding='utf-8').read(), r'" HDF5_PYTHON_TEST "', 'exec'))\n";
    EXPECT_EQ(PyRun_SimpleString(script.c_str()), 0);
    // Keep CPython alive: nanobind and extension teardown follows the existing embedded test pattern.
}
TEST(Hdf5Core, RegisteredCommandsUseHostJobSystem) {
    using namespace SST;
    auto jobs = CreateRef<JobSystem>(nullptr, 1);
    auto registry = CreateRef<CommandRegistry>();
    ASSERT_TRUE(RegisterHdf5Commands(*registry, jobs));
    CommandService service(registry, {});
    auto request = CreateRef<Hdf5Request>();
    request->input.file="missing.h5"; request->input.datasetId="/missing";
    auto result = request->response.get_future();
    CommandContext context; context.Set("hdf5.request", request);
    ASSERT_TRUE(service.Execute(CommandID{"storage.hdf5.read"}, context));
    ASSERT_NE(request->jobId, 0);
    ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(result.get()->error, Hdf5Error::IO);
    jobs->Shutdown();
    EXPECT_EQ(jobs->GetJob(request->jobId)->status, JobStatus::Failed);
}
TEST(Hdf5Core, QueuedCancellationAndBusySession) {
    using namespace SST;
    struct GateJob final : IJob {
        std::promise<void> entered;
        std::shared_future<void> release;
        std::string GetName() const override { return "Test gate"; }
        std::string GetType() const override { return "test.gate"; }
        void Execute(JobContext&) override { entered.set_value(); release.wait(); }
    };
    auto jobs = CreateRef<JobSystem>(nullptr, 1);
    std::promise<void> release;
    auto gate = CreateRef<GateJob>();
    gate->release = release.get_future().share();
    auto entered = gate->entered.get_future();
    ASSERT_NE(jobs->Submit(gate), 0);
    ASSERT_EQ(entered.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    Hdf5Session session(jobs);
    auto file = std::filesystem::temp_directory_path() / ("sst-hdf5-queued-" + std::to_string(GetCurrentProcessId()) + ".h5");
    Hdf5Input request;request.operation=Hdf5Operation::Save;request.file=file;
    ASSERT_FALSE(session.Start(request).error);
    EXPECT_EQ(session.Start(request).error, Hdf5Error::Busy);
    EXPECT_TRUE(session.Cancel().ok);
    release.set_value();
    jobs->Shutdown();
    auto response = session.Poll();
    EXPECT_FALSE(response.running);
    EXPECT_EQ(response.response->error, Hdf5Error::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(file));
    std::filesystem::remove(file);
}
TEST(Hdf5Core, TypedSessionMatchesStorageAndKeepsPollBuffersShared) {
    using namespace SST;
    const auto file=std::filesystem::temp_directory_path()/("sst-hdf5-typed-"+std::to_string(GetCurrentProcessId())+".h5");
    auto jobs=CreateRef<JobSystem>(nullptr,1);
    Hdf5Session session(jobs);
    Hdf5Input save;save.operation=Hdf5Operation::Save;save.file=file;save.attributes={{"source","typed golden"}};
    save.datasets.push_back({"/golden",{2,3},{1.,2.,3.,4.,5.,6.},"V",{{"label","golden"}},{1,3},4});
    ASSERT_FALSE(session.Start(std::move(save)).error);jobs->Shutdown();
    auto saved=session.Poll();ASSERT_TRUE(saved.response);ASSERT_FALSE(saved.response->error);EXPECT_EQ(saved.response->datasetCount,1u);
    const auto reference=sst::hdf5::read(file,"/golden",{1,1},{1,2});
    auto readers=CreateRef<JobSystem>(nullptr,1);Hdf5Session read(readers);
    Hdf5Input input;input.file=file;input.datasetId="/golden";input.offset={1,1};input.count={1,2};
    ASSERT_FALSE(read.Start(std::move(input)).error);readers->Shutdown();
    const auto result=read.Poll();ASSERT_TRUE(result.response);ASSERT_FALSE(result.response->error);ASSERT_TRUE(result.response->data);
    EXPECT_EQ(result.response->data->values,(std::vector<double>{5.,6.}));
    EXPECT_EQ(result.response->data->values,reference.values);EXPECT_EQ(result.response->data->shape,reference.shape);
    EXPECT_EQ(result.response->data->count,reference.count);EXPECT_EQ(result.response->data->units,reference.units);
    EXPECT_EQ(result.response->data->attributes,reference.attributes);EXPECT_EQ(result.response->data->fileAttributes,reference.fileAttributes);
    EXPECT_EQ(result.response,read.Poll().response);EXPECT_EQ(result.response->data->values.data(),read.Poll().response->data->values.data());
    std::filesystem::remove(file);
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--interrupt-save") {
        std::vector<double> values(100000, 42);
        sst::hdf5::Dataset d{"/new", {100, 1000}, values, "V"};
        int checkpoints = 0;
        sst::hdf5::save(std::filesystem::path(argv[2]), {&d, 1}, {}, [&] {
            if (++checkpoints == 3) TerminateProcess(GetCurrentProcess(), 73);
        });
        return 1;
    }
    SST::Logger::Initialize({});
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    SST::Logger::Shutdown();
    return result;
}
