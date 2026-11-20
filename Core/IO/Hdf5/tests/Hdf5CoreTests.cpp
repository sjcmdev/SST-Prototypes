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
    request->input = {{"file_path", "missing.h5"}, {"dataset_id", "/missing"}};
    auto result = request->response.get_future();
    CommandContext context; context.Set("hdf5.request", request);
    ASSERT_TRUE(service.Execute(CommandID{"storage.hdf5.read"}, context));
    ASSERT_NE(request->jobId, 0);
    ASSERT_EQ(result.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(result.get().at("error").at("code"), "hdf5.io");
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
    nlohmann::json request{{"operation", "save"}, {"file_path", Path(file).Utf8()}, {"datasets", nlohmann::json::array()}};
    ASSERT_TRUE(session.Start(request).at("ok").get<bool>());
    EXPECT_EQ(session.Start(request).at("error").at("code"), "hdf5.busy");
    EXPECT_TRUE(session.Cancel().at("ok").get<bool>());
    release.set_value();
    jobs->Shutdown();
    auto response = session.Poll();
    EXPECT_FALSE(response.at("running").get<bool>());
    EXPECT_EQ(response.at("response").at("error").at("code"), "hdf5.cancelled");
    EXPECT_FALSE(std::filesystem::exists(file));
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
