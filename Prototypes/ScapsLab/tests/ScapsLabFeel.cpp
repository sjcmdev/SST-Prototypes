// "Feels instant?" probe: user action -> final plot, with real Workflow + real Panels rendering ImGui frames (no GPU).
// Run: SCAPSLAB_PERF_TEST=1 [SCAPSLAB_FEEL_PROJECTS="a.scapslab;b.scapslab"] [SCAPSLAB_FEEL_ONLY=open,run,drag,...] ScapsLabTests --gtest_filter=Feel.*
// Frame pacing copies App.cpp (SetIdleWaitProvider + Application::onUpdate): 16 ms while input is recent (0.4 s), 50 ms while a job
// runs, 500 ms idle. Output lines start with "FEEL|" (machine-readable, '|' separated) for REPORT.md.
#include <gtest/gtest.h>
#include "Panel.hpp"
#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <thread>
#include <atomic>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef GetJob
#include <scaps_core/api.hpp>
#include <scaps_core/device_project_yaml.hpp>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
using Clock=std::chrono::steady_clock;
double Since(Clock::time_point a,Clock::time_point b=Clock::now()){return std::chrono::duration<double,std::milli>(b-a).count();}
double CpuMs(){FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return (double(((ULONGLONG)k.dwHighDateTime<<32)|k.dwLowDateTime)+double(((ULONGLONG)u.dwHighDateTime<<32)|u.dwLowDateTime))/1e4;}
struct Stats {double p50=0,p95=0,mx=0;int n=0,over16=0,over50=0;};
Stats Summ(std::vector<double> v){Stats s;s.n=int(v.size());if(v.empty())return s;for(double x:v){s.over16+=x>16.7;s.over50+=x>50;}std::sort(v.begin(),v.end());s.p50=v[v.size()/2];s.p95=v[std::min(v.size()-1,size_t(double(v.size())*.95))];s.mx=v.back();return s;}

struct Rig {
    Workflow w;std::vector<Ref<Panel>> panels;std::vector<double> *sink=nullptr,*sinkApply=nullptr,*sinkDispatch=nullptr;std::map<int,std::vector<double>> *sinkPanel=nullptr;std::vector<int> views;
    bool held=false;Clock::time_point activeUntil=Clock::now();double lastApply=0,lastWork=0;Clock::time_point frameEnd=Clock::now();
    const void *lastPending=nullptr;double snapQueue=-1,snapRun=-1;std::map<std::string,int> started;
    std::vector<SubscriptionHandle> subs;std::vector<std::pair<Clock::time_point,std::string>> completed;int cancelled=0,failedOther=0;
    explicit Rig(const std::vector<int> &views={0,1,4,5,10,22,18,6,8}):w(CreateRef<JobSystem>(nullptr,std::max(2u,std::thread::hardware_concurrency()))) {
        ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1920,1080);io.Fonts->AddFontDefault();io.Fonts->Build();
        w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.WatchPlots();
        this->views=views;for(int v:views)panels.push_back(CreateRef<Panel>(&w,v));
        subs.push_back(w.bus->Subscribe<RunCompleted>([this](const RunCompleted &e){completed.push_back({Clock::now(),e.kind});}));
        subs.push_back(w.bus->Subscribe<RunFailed>([this](const RunFailed &e){e.cancelled?++cancelled:++failedOther;}));
    }
    ~Rig(){subs.clear();while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(2));}ImPlot::DestroyContext();ImGui::DestroyContext();}
    void Input(){activeUntil=Clock::now()+std::chrono::milliseconds(400);}
    void Track(){if(w.pending && w.pending.get()!=lastPending){lastPending=w.pending.get();++started[w.pending->request.value("operation",std::string{})];}if(!w.pending)lastPending=nullptr;}
    // One app frame: Workflow::OnUpdate then every panel (draw lists built, no GPU). Returns UI-thread work in ms.
    double Frame() {
        const auto a=Clock::now();if(w.active)if(const auto snap=w.jobs->GetJob(w.active);snap&&snap->status==JobStatus::Completed){snapQueue=std::chrono::duration<double,std::milli>(snap->startedAt-snap->createdAt).count();snapRun=std::chrono::duration<double,std::milli>(snap->finishedAt-snap->startedAt).count();}
        w.interacting=held;w.OnUpdate(0);Track();const auto b=Clock::now();
        auto &io=ImGui::GetIO();io.DeltaTime=1.f/60;ImGui::NewFrame();int i=0;
        for(auto &p:panels){ImGui::SetNextWindowPos(ImVec2(float(i%3)*640,float(i/3)*360));ImGui::SetNextWindowSize(ImVec2(630,350));
            ImGui::Begin(("rig"+std::to_string(i)).c_str(),nullptr,ImGuiWindowFlags_NoSavedSettings);const auto tp=Clock::now();p->RenderContent();if(sinkPanel)(*sinkPanel)[views[size_t(i)]].push_back(Since(tp));ImGui::End();++i;}
        ImGui::Render();const double ms=Since(a),apply=Since(a,b);frameEnd=Clock::now();lastWork=ms;lastApply=apply;if(sink)sink->push_back(ms);if(sinkApply)sinkApply->push_back(apply);
        const double wait=held||Clock::now()<activeUntil?16.:w.Pending()?50.:500.;if(std::getenv("SCAPSLAB_FEEL_TRACE"))std::printf("TRACE work=%.2f wait=%.0f pending=%d active=%llu\n",ms,wait,int(w.Pending()),(unsigned long long)w.active);const double rest=wait-ms;if(rest>0)std::this_thread::sleep_for(std::chrono::duration<double,std::milli>(rest));
        return ms;
    }
    Json Send(const Json &r){const auto t=Clock::now();const auto out=w.Dispatch(r);if(sinkDispatch)sinkDispatch->push_back(Since(t));Track();Input();return out;}
};
struct Res {double latency=0,applyMs=0,renderMs=0,execMs=0,firstCurve=-1,sinceRelease=-1;int starts=0,cancels=0,completions=0;Stats frames;double cpuPct=0;};
void Print(const std::string &proj,const char *action,const Res &r,const std::string &note="") {
    std::printf("FEEL|%s|%s|latency_ms=%.1f|first_curve_ms=%.1f|after_release_ms=%.1f|apply_ms=%.2f|render_after_ms=%.2f|job_exec_ms=%.1f|starts=%d|cancels=%d|completions=%d|frames=%d|p50=%.2f|p95=%.2f|max=%.2f|over16=%d|over50=%d|cpu_pct=%.0f|%s\n",
        proj.c_str(),action,r.latency,r.firstCurve,r.sinceRelease,r.applyMs,r.renderMs,r.execMs,r.starts,r.cancels,r.completions,r.frames.n,r.frames.p50,r.frames.p95,r.frames.mx,r.frames.over16,r.frames.over50,r.cpuPct,note.c_str());std::fflush(stdout);
}
bool Wanted(const char *name){const char *only=std::getenv("SCAPSLAB_FEEL_ONLY");if(!only)return true;return (std::string(",")+only+",").find(std::string(",")+name+",")!=std::string::npos;}
// Dispatch `start`, then render frames (app cadence) until the job finished and the frame that applied the result was drawn.
// latency = action -> end of the frame that drew the new data (plotGen changed); apply = OnUpdate share of that frame; render = rest (typed conversion + plot cache rebuild).
Res Measure(Rig &r,const std::function<void()> &start,const char *execKey=nullptr,double timeoutMs=180000) {
    Res res;std::vector<double> fr;r.sink=&fr;const auto gen0=r.w.plotGen;const size_t c0=r.completed.size();const int cancel0=r.cancelled;const auto s0=r.started;
    const double cpu0=CpuMs();const auto t0=Clock::now();start();const double dispatchMs=Since(t0);r.Input();
    // watcher: exact moment the worker finished (independent of the UI cadence)
    std::atomic<bool> stop{false};std::atomic<double> doneMs{-1};const auto id=r.w.active;
    std::thread watcher([&]{if(!id)return;while(!stop){if(const auto snap=r.w.jobs->GetJob(id);snap&&snap->status!=JobStatus::Queued&&snap->status!=JobStatus::Running){doneMs=Since(t0);return;}std::this_thread::sleep_for(std::chrono::microseconds(300));}});
    bool seen=false;double apply=0,render=0,first=-1;int quiet=0;
    for(;;) {
        r.Frame();if(first<0)first=Since(t0,r.frameEnd);
        if(!seen && r.w.plotGen!=gen0 && !r.w.Pending()){seen=true;apply=r.lastApply;render=r.lastWork-r.lastApply;res.latency=Since(t0,r.frameEnd);}
        if(seen){if(++quiet>=2)break;}
        if(Since(t0)>timeoutMs){res.latency=-1;break;}
        if(!seen && !r.w.Pending() && Since(t0)>1500){res.latency=first;break;} // plotGen unchanged (in-place op, e.g. device switch): first frame drawn after the action
    }
    stop=true;watcher.join();
    if(r.snapRun>=0)std::printf("FEEL|jobtimes|dispatch_ms=%.1f|worker_done_at_ms=%.1f|done_to_visible_ms=%.1f|",dispatchMs,doneMs.load(),res.latency-doneMs.load());
    if(r.snapRun>=0)std::printf("queue_ms=%.1f|run_ms=%.1f|latency_ms=%.1f|slack_ms=%.1f\n",r.snapQueue,r.snapRun,res.latency,res.latency-r.snapQueue-r.snapRun-apply-render);
    r.snapQueue=r.snapRun=-1;r.sink=nullptr;res.applyMs=apply;res.renderMs=render;res.frames=Summ(fr);res.completions=int(r.completed.size()-c0);res.cancels=r.cancelled-cancel0;
    for(const auto &[k,v]:r.started){const auto it=s0.find(k);res.starts+=v-(it==s0.end()?0:it->second);}
    res.cpuPct=100.*(CpuMs()-cpu0)/std::max(1.,Since(t0));
    if(execKey){const auto &d=r.w.Current();const Json *j=nullptr;const std::string k=execKey;
        if(k=="iv"&&d.result.contains("compute_time_ms"))j=&d.result.at("compute_time_ms");else if(k=="band"&&d.bandResult.contains("compute_time_ms"))j=&d.bandResult.at("compute_time_ms");
        else if(d.acResults.contains(k)&&d.acResults.at(k).contains("compute_time_ms"))j=&d.acResults.at(k).at("compute_time_ms");
        if(j&&j->is_number())res.execMs=j->get<double>();
        const Json &src=k=="iv"?*d.result.share():k=="band"?*d.bandResult.share():*d.acResults.share();const Json *ph=k=="iv"||k=="band"?(src.contains("perf_phase_ms")?&src.at("perf_phase_ms"):nullptr):(src.contains(k)&&src.at(k).contains("perf_phase_ms")?&src.at(k).at("perf_phase_ms"):nullptr);
        if(ph)std::printf("FEEL|phases|%s|%s\n",k.c_str(),ph->dump().c_str());}
    return res;
}
// Median-latency run of `reps` repetitions (the box is shared with other agents; single samples are noisy).
Res Med(const std::function<Res()> &f,int reps=3){std::vector<Res> v;for(int i=0;i<reps;++i)v.push_back(f());std::sort(v.begin(),v.end(),[](const Res &a,const Res &b){return a.latency<b.latency;});return v[v.size()/2];}
std::string Stem(const std::string &p){return std::filesystem::path(p).stem().string();}
void Settle(Rig &r,double ms=800){const auto t=Clock::now();while(Since(t)<ms||r.w.Pending())r.Frame();}
std::vector<std::string> Projects(){
    std::vector<std::string> projects;
    if(const char *env=std::getenv("SCAPSLAB_FEEL_PROJECTS")){std::string s=env;size_t p;while((p=s.find(';'))!=std::string::npos){projects.push_back(s.substr(0,p));s=s.substr(p+1);}if(!s.empty())projects.push_back(s);}
    else for(const char *n:{"11-si-pn-am15","01-pn-temperature-series","09-cigs-cds-zno-am15"})projects.push_back((ExamplesDirectory()/(std::string(n)+".scapslab")).string());
    return projects;
}
}

TEST(Feel,Actions) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    for(const auto &path:Projects()) {
        const std::string name=Stem(path);Rig r;
        const bool big=std::filesystem::file_size(path)>5'000'000;
        // 1. open
        if(Wanted("open")) Print(name,"open",Measure(r,[&]{(void)r.Send({{"operation","project_load"},{"path",path}});}),big?"big":"");
        else {(void)r.Send({{"operation","project_load"},{"path",path}});Settle(r);}
        Settle(r,300);
        std::printf("FEEL|%s|info|devices=%zu|history=%zu|layers=%zu|file_mb=%.1f\n",name.c_str(),r.w.project.devices.size(),r.w.Current().history.size(),r.w.Current().document.at("layers").size(),double(std::filesystem::file_size(path))/1e6);
        // 2. idle CPU (UI thread only: frames at the 500 ms idle cadence)
        if(Wanted("idle")) {Settle(r,500);const double c0=CpuMs();const auto t0=Clock::now();std::vector<double> fr;r.sink=&fr;while(Since(t0)<3000)r.Frame();r.sink=nullptr;Res m;m.frames=Summ(fr);m.cpuPct=100.*(CpuMs()-c0)/Since(t0);m.latency=Since(t0);Print(name,"idle3s",m,"cadence 500ms");}
        // 3. I-V, band, AC
        if(Wanted("run")) Print(name,"run_iv",Med([&]{return Measure(r,[&]{(void)r.Send({{"operation","run"}});},"iv");}),"median of 3");
        if(Wanted("band")) Print(name,"band_selected_bias",Med([&]{return Measure(r,[&]{(void)r.Send({{"operation","band"},{"voltage_V",r.w.Current().biasVoltage_V}});},"band");}),"median of 3");
        if(Wanted("ac")) {
            Print(name,"cv",Med([&]{return Measure(r,[&]{(void)r.Send({{"operation","cv"},{"temperature_K",300},{"start_V",0},{"stop_V",0.49},{"step_V",0.01},{"frequency_Hz",1e6}});},"cv");}),"50 points, median of 3");
            Print(name,"cf",Med([&]{return Measure(r,[&]{(void)r.Send({{"operation","cf"},{"temperature_K",300},{"voltage_V",0.},{"start_Hz",1e2},{"stop_Hz",1e7},{"points",31}});},"cf");}),"31 points, median of 3");
            Print(name,"qe",Med([&]{return Measure(r,[&]{(void)r.Send({{"operation","qe"},{"start_nm",350.},{"stop_nm",1100.},{"points",101}});},"qe");}),"101 wavelengths, median of 3");
        }
        // 4. Single shot (pin live curve) and Results clicks
        if(Wanted("shot")&&!r.w.Current().history.empty()) {const auto d=r.w.Current().id;const auto id=r.w.Current().history.back().at("run_id");
            Print(name,"single_shot",Measure(r,[&]{(void)r.Send({{"operation","history_update"},{"device_id",d},{"run_id",id},{"pinned",true}});}));}
        if(Wanted("click")) {std::vector<double> lat;Res worst;const auto hist=r.w.Current().history;const size_t n=std::min<size_t>(hist.size(),8);
            for(size_t i=0;i<n;++i){const auto id=hist[(i*5+1)%hist.size()].at("run_id");auto m=Measure(r,[&]{(void)r.Send({{"operation","history_select"},{"device_id",r.w.Current().id},{"run_id",id}});});lat.push_back(m.latency);if(m.latency>=worst.latency)worst=m;}
            if(n){const auto s=Summ(lat);Print(name,"results_click_worst",worst,"p50="+std::to_string(s.p50)+" ms over "+std::to_string(n)+" clicks");}}
        // 5. device switch
        if(Wanted("device")&&r.w.project.devices.size()>1) {std::vector<double> lat;Res worst;for(size_t i=0;i<r.w.project.devices.size();++i){const auto id=r.w.project.devices[i]->id;if(id==r.w.project.active_device)continue;auto m=Measure(r,[&]{(void)r.Send({{"operation","device_select"},{"device_id",id}});});lat.push_back(m.latency);if(m.latency>=worst.latency)worst=m;}
            if(!lat.empty())Print(name,"device_switch_worst",worst,"p50="+std::to_string(Summ(lat).p50)+" ms over "+std::to_string(lat.size()));}
        // 6. save (no plot change: wall = until !Pending)
        if(Wanted("save")) {const auto file=(std::filesystem::temp_directory_path()/"feel-save.scapslab").string();
            Res s;std::vector<double> fr;r.sink=&fr;const auto t0=Clock::now();(void)r.Send({{"operation","project_save"},{"path",file}});while(r.w.Pending())r.Frame();s.latency=Since(t0,r.frameEnd);r.sink=nullptr;s.frames=Summ(fr);
            Print(name,"save",s,"bytes="+std::to_string(std::filesystem::exists(file)?std::filesystem::file_size(file):0));std::filesystem::remove(file);}
        // 7. batch of 10 (run_batch)
        if(Wanted("batch")&&!big) {auto spec=BatchDefaults();spec["axes"][0]["count"]=10;Print(name,"batch10",Measure(r,[&]{(void)r.Send({{"operation","run_batch"},{"spec",spec}});}));}
        // 8. edit with auto-recalc: slider drag of layer-0 thickness (45 frames held), then a single discrete edit
        if(Wanted("drag")) {
            (void)r.Send({{"operation","auto_recalculate"},{"enabled",true}});Settle(r,600);
            for(int pass=0;pass<2;++pass) {
                const bool drag=pass==0;Res res;std::vector<double> fr;r.sink=&fr;const size_t c0=r.completed.size();const int cancel0=r.cancelled;const auto s0=r.started;
                std::vector<double> ap,dsp;std::map<int,std::vector<double>> pan;r.sinkApply=&ap;r.sinkDispatch=&dsp;r.sinkPanel=&pan;const double cpu0=CpuMs();const auto tStart=Clock::now();const Json base=r.w.Current().document;const double th=base["layers"][0]["thickness_m"].get<double>();
                if(drag) {for(int i=0;i<45;++i){r.held=true;auto d=base;d["layers"][0]["thickness_m"]=th*(1+0.004*(i+1));(void)r.Send({{"operation","edit"},{"document",d},{"gesture_id","feel"}});r.Frame();}}
                else {auto d=base;d["layers"][0]["thickness_m"]=th*1.05;(void)r.Send({{"operation","edit"},{"document",d}});}
                const auto tRelease=Clock::now();r.held=false;r.Input();const auto hold=Summ(fr);const auto holdAp=Summ(ap),holdDsp=Summ(dsp);
                while((r.w.Pending()||r.w.Current().needsRecalculate||r.completed.size()==c0)&&Since(tStart)<120000)r.Frame();
                Settle(r,700);
                r.sink=nullptr;r.sinkApply=nullptr;r.sinkDispatch=nullptr;r.sinkPanel=nullptr;res.frames=Summ(fr);
                if(drag){std::string t;for(auto &[v,x]:pan){const auto q=Summ(x);t+="view"+std::to_string(v)+" p95="+std::to_string(q.p95)+" max="+std::to_string(q.mx)+"; ";}std::printf("FEEL|%s|drag_panel_cost_ms|%s\n",name.c_str(),t.c_str());}
                {std::string ops;for(const auto &[k,v]:r.started){const auto it=s0.find(k);const int n=v-(it==s0.end()?0:it->second);if(n)ops+=k+"="+std::to_string(n)+" ";}std::printf("FEEL|%s|%s_ops_started|%s\n",name.c_str(),drag?"drag":"edit",ops.c_str());}
                for(size_t i=c0;i<r.completed.size();++i)if(r.completed[i].second=="iv"){res.firstCurve=Since(tStart,r.completed[i].first);res.sinceRelease=Since(tRelease,r.completed[i].first);break;}
                size_t lastIv=0;bool any=false;for(size_t i=c0;i<r.completed.size();++i)if(r.completed[i].second=="iv"){lastIv=i;any=true;}
                if(any)res.latency=Since(tRelease,r.completed[lastIv].first);
                res.completions=int(r.completed.size()-c0);res.cancels=r.cancelled-cancel0;for(const auto &[k,v]:r.started){const auto it=s0.find(k);res.starts+=v-(it==s0.end()?0:it->second);}
                res.cpuPct=100.*(CpuMs()-cpu0)/Since(tStart);
                if(drag)std::printf("FEEL|%s|drag_hold_only|frames=%d|p50=%.2f|p95=%.2f|max=%.2f|over16=%d|onupdate_p95=%.2f|onupdate_max=%.2f|dispatch_p95=%.2f|dispatch_max=%.2f\n",name.c_str(),hold.n,hold.p50,hold.p95,hold.mx,hold.over16,holdAp.p95,holdAp.mx,holdDsp.p95,holdDsp.mx);
                Print(name,drag?"drag_thickness_45f":"edit_then_auto",res,"latency=release->final iv curve; after_release=release->first iv curve; first_curve=drag start->first iv curve");
            }
            (void)r.Send({{"operation","auto_recalculate"},{"enabled",false}});
        }
    }
}

// Engine-only I-V, same sequence as Operation::Execute "run" (prepare_mesh, equilibrium, solve_iv/solve_bias) but no JSON and no UI.
TEST(Feel,EngineOnly) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    for(const auto &path:Projects()) {
        Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;(void)w.Dispatch({{"operation","project_load"},{"path",path}});while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const auto doc=w.Current().document;using namespace scaps_core;
        for(int rep=0;rep<3;++rep) {
            const auto t0=Clock::now();const auto p=Model(doc);const auto device=make_device_from_project(p);const auto pc=physical_constants(p.constants_mode);auto volts=make_voltage_list(p.voltage);for(auto &v:volts)v=-v;
            const auto t1=Clock::now();const auto mesh=prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);const auto t2=Clock::now();
            const auto eq=solve_equilibrium(device,mesh,p.temperature_K,p.solver,pc);auto previous=eq.state;size_t conv=0;
            const bool ext=p.illumination.enabled||p.external.series_ohm_m2!=0||!std::isinf(p.external.shunt_ohm_m2);
            if(ext){const auto s=solve_iv(device,mesh,volts,p.temperature_K,p.solver,pc);conv=s.size();}
            else for(double v:volts){if(v==0)continue;const auto r=solve_bias(device,mesh,previous,v,p.temperature_K,p.solver,pc);if(r.status==SolverStatus::Converged){previous=r.state;++conv;}}
            const auto t3=Clock::now();
            std::printf("FEEL|%s|engine_only_iv|rep=%d|model_ms=%.1f|mesh_ms=%.1f|solve_ms=%.1f|total_ms=%.1f|points=%zu|converged=%zu|nodes=%zu|external_network=%d\n",Stem(path).c_str(),rep,Since(t0,t1),Since(t1,t2),Since(t2,t3),Since(t0,t3),volts.size(),conv,mesh.x_m.size(),int(ext));std::fflush(stdout);
        }
    }
}
