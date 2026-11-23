#include "../../../tools/PerformanceGuard.hpp"
#include "Workflow.hpp"
#include "Core/JobSystem/ParallelFor.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/device_project.hpp>
#include <tlhelp32.h>

namespace SST::ScapsLab {
namespace {
std::size_t Threads() {
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 entry{};entry.dwSize=sizeof(entry);std::size_t n=0;
    if(Thread32First(snapshot,&entry))do{n+=entry.th32OwnerProcessID==GetCurrentProcessId();}while(Thread32Next(snapshot,&entry));CloseHandle(snapshot);return n;
}
double Cpu() {FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return (double((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+double((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime))/1e7;}
void Finish(Workflow& w) {
    const auto end=perfguard::Clock::now()+std::chrono::minutes(5);
    while(w.Pending() && perfguard::Clock::now()<end){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());ASSERT_TRUE(w.response.value("ok",false))<<w.response;
}
class SpectrumLoad final:public IJob {
public:
    std::size_t maximum=0;
    std::string GetName()const override{return "C-f batch load";}std::string GetType()const override{return "test.cf_pool";}
    void Execute(JobContext& ctx)override {
        auto p=scaps_core::make_default_device_project();p.mesh={31,2};p.left_contact.flat_band=p.right_contact.flat_band=true;for(auto& l:p.layers)l.defects.clear();
        const auto device=scaps_core::make_device_from_project(p);const auto mesh=scaps_core::build_mesh(device,p.mesh);const auto pc=scaps_core::physical_constants(p.constants_mode);
        const auto op=scaps_core::solve_iv(device,mesh,{0.},300.,p.solver,pc).front();std::atomic<std::size_t> peak=0;
        std::vector<double> frequencies;for(int i=0;i<64;++i)frequencies.push_back(100.+i*1000.);
        EXPECT_TRUE(ParallelFor(ctx,23,[&](std::size_t){
            const auto points=scaps_core::solve_ac_spectrum(device,mesh,op,frequencies,1e-3,pc,{JobExecutor{ctx},ctx.GetWorkerCount(),ctx.GetStopToken()});EXPECT_EQ(points.size(),64u);
            const auto count=Threads();auto old=peak.load();while(old<count && !peak.compare_exchange_weak(old,count)){}
        }));maximum=peak;
    }
};
}
TEST(CorePool, InjectedCfInsideBatchLoadCreatesNoExtraThreads) {
    JobSystem jobs({},23);const auto before=Threads();auto load=CreateRef<SpectrumLoad>();const auto id=jobs.Submit(load);
    const auto end=perfguard::Clock::now()+std::chrono::seconds(30);
    while(perfguard::Clock::now()<end){const auto status=jobs.TryGetStatus(id);if(status && (*status==JobStatus::Completed || *status==JobStatus::Failed))break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_EQ(jobs.TryGetStatus(id),JobStatus::Completed);EXPECT_LE(load->maximum,before);
    std::cout<<"POOL cf_batch23 threads_before="<<before<<" threads_peak="<<load->maximum<<'\n';
}
TEST(PoolPerformance, ScapsLabCfQeTemperatureAtTwentyThreeWorkers) {
    if(!std::getenv("PERFPOOL_BENCH"))GTEST_SKIP();
    Workflow w(CreateRef<JobSystem>(nullptr,23));w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
    const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures/perfguard-sweep5.scapslab";
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path.string()}}).value("ok",false));Finish(w);
    for(const auto& d:w.project.devices)if(d->name=="light")w.project.active_device=d->id;
    std::vector<Json> requests{
        {{"operation","cf"},{"temperature_K",300.},{"voltage_V",0.},{"start_Hz",1e2},{"stop_Hz",1e7},{"points",256}},
        {{"operation","qe"},{"start_nm",350.},{"stop_nm",1100.},{"points",101}}
    };
    auto spec=BatchDefaults();spec["axes"][0]["count"]=23;spec["voltages_V"]=Json::array({0.,.1,.2});spec["include_curves"]=false;
    requests.push_back({{"operation","run_batch"},{"spec",spec}});
    for(const auto& request:requests)for(int rep=0;rep<3;++rep) {
        const auto start=perfguard::Clock::now();const auto cpu=Cpu();const auto threads=Threads();
        ASSERT_TRUE(w.Dispatch(request).value("ok",false));Finish(w);const auto wall=std::chrono::duration<double>(perfguard::Clock::now()-start).count();
        std::cout<<"POOL scapslab op="<<request.at("operation")<<" workers=23 rep="<<rep<<" wall_s="<<wall<<" cores="<<(Cpu()-cpu)/wall<<" threads_before="<<threads<<" threads_after="<<Threads()<<'\n';
    }
}
} // namespace SST::ScapsLab
