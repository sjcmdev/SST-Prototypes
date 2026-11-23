#include "../../../tools/PerformanceGuard.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"
#include "IO/JFMFitJobs.hpp"
#include "Events/JFMEvents.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <bit>

namespace SST::Prototypes::JFM {
namespace {
JFMCharacteristicRecord PoolRecord() {
    JFMCharacteristicRecord r;r.info.name="pool benchmark";r.info.temperatureK=300.;
    r.info.prefit=JFMPrefitState{};auto& p=*r.info.prefit;p.modelId="SD-RsRsh";p.status="completed";
    const std::unordered_map<std::string,double> values{{"I0",1e-10},{"A",1.5},{"Rs",2.},{"Rsh",1e6}};
    for(const auto& [key,v]:values)p.parameters[key]=JFMParameterEstimate{v,std::nullopt,"test",{}};
    auto data=CreateRef<JFMCharacteristicData>();
    for(int i=0;i<96;++i){const auto v=.02+.7*i/95.;data->voltage.push_back(v);data->current.push_back(EvaluateJFMCurrent(p.modelId,v,300.,values));}
    r.data=data;p.fitRange={0,96,.02,.72,"test"};return r;
}
std::uint64_t Hash(const JFMMonteCarloComputation& c) {
    std::uint64_t h=14695981039346656037ull;
    const auto add=[&](double x){const auto b=std::bit_cast<std::uint64_t>(x);for(int k=0;k<8;++k){h^=(b>>(8*k))&255;h*=1099511628211ull;}};
    for(const auto& s:c.result.samples){for(auto v:s.parameters)add(v);add(s.chiSquared);add(s.reducedChiSquared);add(s.objectiveChiSquared);add(s.iterations);add(s.accepted);add(s.solverConverged);}
    for(auto v:c.result.noisyCurrent)add(v);add(c.state.acceptedCount);add(c.state.convergedCount);return h;
}
std::uint64_t StatisticsHash(const JFMMonteCarloComputation& c) {
    const auto s=CalculateJFMMonteCarloScatterStatistics(c.result,0,1);if(!s)return 0;
    std::uint64_t h=14695981039346656037ull;
    for(const auto v:{s->meanX,s->meanY,s->covarianceXX,s->covarianceXY,s->covarianceYY,s->eigenvalueMajor,s->eigenvalueMinor,s->eigenvectorAngle}){h^=std::bit_cast<std::uint64_t>(v);h*=1099511628211ull;}return h;
}
double Cpu() {FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return (double((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+double((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime))/1e7;}
bool Wait(JobSystem& jobs,JobId id) {
    const auto end=perfguard::Clock::now()+std::chrono::minutes(8);
    while(perfguard::Clock::now()<end){const auto s=jobs.GetJob(id);if(s && (s->status==JobStatus::Completed || s->status==JobStatus::Failed || s->status==JobStatus::Cancelled))return s->status==JobStatus::Completed;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;
}
class McJob final:public IJob {
public:
    JFMCharacteristicRecord record=PoolRecord();JFMFitMonteCarloSettings settings;std::optional<JFMMonteCarloComputation> result;std::atomic<bool> started{false};
    McJob(){settings.preferGpu=false;settings.monteCarloSamples=4096;settings.monteCarloMaxIterations=200;settings.monteCarloSeed=12345;settings.monteCarloNoisePercent=1.;settings.saveSamples=true;settings.savedSampleLimit=8;}
    std::string GetName()const override{return "MC pool probe";}std::string GetType()const override{return "test.mc_pool";}
    void Execute(JobContext& ctx)override{started=true;JFMFitState fit;fit.modelId=record.info.prefit->modelId;fit.status="completed";fit.parameters=record.info.prefit->parameters;auto r=RunJFMMonteCarlo(record,*record.info.prefit,fit,settings,&ctx);if(!r)throw std::runtime_error(r.Error().technicalDetails);result=std::move(*r);}
};
}
TEST(PoolPerformance, FitEightAndMonteCarlo4096) {
    if(!std::getenv("PERFPOOL_BENCH"))GTEST_SKIP();
    const auto workers=std::getenv("PERFPOOL_WORKERS")?std::atoi(std::getenv("PERFPOOL_WORKERS")):23;
    JobSystem jobs({},workers);auto bus=CreateRef<EventBus>();auto store=CreateRef<JFMWorkspaceStore>();auto queue=CreateRef<JFMFitWorkQueue>();
    const auto project=store->GetActiveProjectHandle();const auto record=PoolRecord();const auto ids=store->AddCharacteristicRecords(project,std::vector<JFMCharacteristicRecord>(8,record));
    JFMFitMonteCarloSettings settings;settings.preferGpu=false;settings.fitMaxIterations=2000;
    std::vector<JFMFitWorkItem> items;std::vector<JFMFitState> fits(8);int completed=0;
    for(auto id:ids){JFMCharacteristicRef ref{project,id};items.push_back({ref,store->BeginFit(ref),record,settings,false});}
    auto sub=bus->Subscribe<FitCompleted>([&](const FitCompleted& e){const auto i=std::find(ids.begin(),ids.end(),e.ref.characteristic)-ids.begin();if(i<fits.size())fits[i]=e.state;++completed;});
    ASSERT_TRUE(queue->Enqueue(std::move(items)));auto fitJob=CreateRef<JFMFitJob>(bus,store,queue);
    auto start=perfguard::Clock::now();auto cpu=Cpu();ASSERT_TRUE(Wait(jobs,jobs.Submit(fitJob)));const auto fitWall=std::chrono::duration<double>(perfguard::Clock::now()-start).count();bus->ProcessQueue();ASSERT_EQ(completed,8);
    std::uint64_t fitHash=14695981039346656037ull;for(const auto& f:fits)for(const auto* key:{"I0","A","Rs","Rsh"}){fitHash^=std::bit_cast<std::uint64_t>(f.parameters.at(key).value);fitHash*=1099511628211ull;}
    std::cout<<"POOL fit8 workers="<<workers<<" wall_s="<<fitWall<<" cores="<<(Cpu()-cpu)/fitWall<<" hash="<<std::hex<<fitHash<<std::dec<<'\n';
    EXPECT_EQ(fitHash,0xf20a0a1197458ee5ull);
    auto mc=CreateRef<McJob>();start=perfguard::Clock::now();cpu=Cpu();ASSERT_TRUE(Wait(jobs,jobs.Submit(mc)));const auto mcWall=std::chrono::duration<double>(perfguard::Clock::now()-start).count();ASSERT_TRUE(mc->result);
    std::cout<<"POOL mc4096 workers="<<workers<<" wall_s="<<mcWall<<" cores="<<(Cpu()-cpu)/mcWall<<" hash="<<std::hex<<Hash(*mc->result)<<std::dec<<" samples="<<mc->result->result.samples.size()<<'\n';
    EXPECT_EQ(Hash(*mc->result),0x827c0607d71acedaull);
    EXPECT_EQ(StatisticsHash(*mc->result),0x50cf8600d1a35e3full);
    std::cout<<"POOL statistics_hash="<<std::hex<<StatisticsHash(*mc->result)<<std::dec<<'\n';
}
TEST(CorePool, MonteCarloIsBitIdenticalAtOneTwoAndTwentyFourWorkers) {
    auto serial=CreateRef<McJob>();serial->settings.monteCarloSamples=128;
    auto ctx=JobContext({},{},{},{},[]{return false;});serial->Execute(ctx);ASSERT_TRUE(serial->result);
    for(const auto workers:{1u,2u,24u}) {
        JobSystem jobs({},workers);auto parallel=CreateRef<McJob>();parallel->settings=serial->settings;
        ASSERT_TRUE(Wait(jobs,jobs.Submit(parallel)));ASSERT_TRUE(parallel->result);
        EXPECT_EQ(Hash(*parallel->result),Hash(*serial->result));
        EXPECT_EQ(StatisticsHash(*parallel->result),StatisticsHash(*serial->result));
        EXPECT_EQ(parallel->result->result.voltage,serial->result->result.voltage);
        EXPECT_EQ(parallel->result->result.currentSigma,serial->result->result.currentSigma);
    }
}
TEST(CorePool, MonteCarloCancellationStopsBeforePublishingPartialSamples) {
    JobSystem jobs({},23);auto mc=CreateRef<McJob>();mc->settings.monteCarloSamples=65536;
    const auto id=jobs.Submit(mc);const auto deadline=perfguard::Clock::now()+std::chrono::seconds(2);
    while(!mc->started && perfguard::Clock::now()<deadline)std::this_thread::yield();ASSERT_TRUE(mc->started);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));const auto start=perfguard::Clock::now();ASSERT_TRUE(jobs.RequestCancel(id));
    EXPECT_FALSE(Wait(jobs,id));const auto latency=std::chrono::duration<double,std::milli>(perfguard::Clock::now()-start).count();
    EXPECT_EQ(jobs.GetJob(id)->status,JobStatus::Cancelled);EXPECT_FALSE(mc->result);EXPECT_LT(latency,1000.);
    std::cout<<"POOL mc_cancel_ms="<<latency<<'\n';
}
TEST(CorePool, EightNontrivialFitsMatchSerial) {
    auto record=PoolRecord();auto data=CreateRef<JFMCharacteristicData>();
    const std::unordered_map<std::string,double> truth{{"I0",1e-10},{"A",1.5},{"Rs",2.},{"Rsh",1e6}};
    for(int i=0;i<2048;++i){const double v=.02+.7*i/2047.;data->voltage.push_back(v);data->current.push_back(EvaluateJFMCurrent("SD-RsRsh",v,300.,truth));}
    record.data=data;record.info.prefit->fitRange={0,2048,.02,.72,"test"};record.info.prefit->parameters["I0"].value*=3.;record.info.prefit->parameters["A"].value*=.85;record.info.prefit->parameters["Rs"].value*=4.;
    std::vector<JFMFitState> reference;
    for(const auto workers:{1u,23u}) {
        JobSystem jobs({},workers);auto bus=CreateRef<EventBus>();auto store=CreateRef<JFMWorkspaceStore>();auto queue=CreateRef<JFMFitWorkQueue>();
        const auto project=store->GetActiveProjectHandle();const auto ids=store->AddCharacteristicRecords(project,std::vector<JFMCharacteristicRecord>(8,record));
        JFMFitMonteCarloSettings settings;settings.preferGpu=false;settings.fitMaxIterations=2000;
        std::vector<JFMFitWorkItem> items;std::vector<JFMFitState> fits(8);int completed=0;
        for(auto id:ids){JFMCharacteristicRef ref{project,id};items.push_back({ref,store->BeginFit(ref),record,settings,false});}
        auto sub=bus->Subscribe<FitCompleted>([&](const FitCompleted& e){const auto i=std::find(ids.begin(),ids.end(),e.ref.characteristic)-ids.begin();fits[i]=e.state;++completed;});
        ASSERT_TRUE(queue->Enqueue(std::move(items)));auto job=CreateRef<JFMFitJob>(bus,store,queue);
        const auto start=perfguard::Clock::now();const auto cpu=Cpu();ASSERT_TRUE(Wait(jobs,jobs.Submit(job)));const auto wall=std::chrono::duration<double>(perfguard::Clock::now()-start).count();bus->ProcessQueue();ASSERT_EQ(completed,8);
        if(reference.empty())reference=fits;else for(std::size_t i=0;i<fits.size();++i){EXPECT_EQ(fits[i].iterations,reference[i].iterations);EXPECT_EQ(std::bit_cast<std::uint64_t>(fits[i].chiSquared),std::bit_cast<std::uint64_t>(reference[i].chiSquared));for(const auto& [name,p]:reference[i].parameters)EXPECT_EQ(std::bit_cast<std::uint64_t>(fits[i].parameters.at(name).value),std::bit_cast<std::uint64_t>(p.value));}
        std::cout<<"POOL fit8_2048 workers="<<workers<<" wall_s="<<wall<<" cores="<<(Cpu()-cpu)/wall<<" iterations="<<fits.front().iterations<<'\n';
    }
}
} // namespace SST::Prototypes::JFM
