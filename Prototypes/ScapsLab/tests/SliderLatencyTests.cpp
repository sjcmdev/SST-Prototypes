#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include <thread>
#include <fstream>
using namespace SST;using namespace SST::ScapsLab;
TEST(ResumePerf, SliderLiveLatency) {
    const auto out=std::getenv("TYPED3_LATENCY_OUT"),sweep=std::getenv("TYPED2_SWEEP5");if(!out || !sweep)GTEST_SKIP();
    using Clock=std::chrono::steady_clock;Json measurements=Json::array();
    for(int rep=0;rep<3;++rep){
        Workflow w(CreateRef<JobSystem>(nullptr,23));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;
        const auto pump=[&]{w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",sweep}}).value("ok",false));while(w.Pending())pump();ASSERT_TRUE(w.response.value("ok",false));
        for(const auto& d:w.project.devices)if(d->name=="light")w.project.active_device=d->id;
        ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));while(w.Pending())pump();ASSERT_TRUE(w.response.value("ok",false));
        w.autoRecalculate=true;w.dragging=true;w.lastFullMs=200.;Clock::time_point edited;
        for(double value:{301.,302.,303.}){
            edited=Clock::now();
#ifdef TYPED3_BASELINE
            auto next=w.EditDraft()?*w.EditDraft():w.Current().document;next["temperature_K"]=value;
            ASSERT_TRUE(w.Exec(Cmd::EditDocument::Replace(std::move(next),"latency drag")).value("ok",false));
#else
            auto next=CreateRef<ProjectDraft>(w.EditDraft()?*w.EditDraft():*w.Current().draft);next->project.temperature_K=value;
            ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(std::move(next),"latency drag")).value("ok",false));
#endif
            if(value!=303.)for(int tick=0;tick<5;++tick)pump();
        }
        const auto deadline=Clock::now()+std::chrono::seconds(60);
        while((w.Pending() || w.Current().previewV.empty() || w.Current().previewRev!=w.Current().revision) && Clock::now()<deadline)pump();
        ASSERT_FALSE(w.Pending());ASSERT_FALSE(w.Current().previewV.empty());ASSERT_EQ(w.Current().previewRev,w.Current().revision);
        const double live=std::chrono::duration<double,std::milli>(Clock::now()-edited).count();
        w.dragging=false;const auto released=Clock::now();
        while((w.Pending() || w.Current().needsRecalculate) && Clock::now()<deadline)pump();
        ASSERT_FALSE(w.Pending());ASSERT_FALSE(w.Current().needsRecalculate);ASSERT_TRUE(w.response.value("ok",false));
        const double exact=std::chrono::duration<double,std::milli>(Clock::now()-released).count();
        measurements.push_back({{"rep",rep},{"edit_to_live_ms",live},{"release_to_exact_ms",exact},{"previews",w.previewsStarted},{"undo_steps",w.Current().undo.GetUndoDepth()}});
        std::printf("SLIDER %s\n",measurements.back().dump().c_str());ASSERT_EQ(w.Current().undo.GetUndoDepth(),1u);
    }
    std::ofstream(out)<<measurements.dump(2);
}
