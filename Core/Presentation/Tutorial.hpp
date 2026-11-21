#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

namespace SST {
// Predicates read live application state; commands submit through the application's command registry.
class Tutorial {
public:
    using Json = nlohmann::json;
    using Predicates = std::map<std::string, std::function<Json()>>;
    void Start(Json definition, const std::string &mode, const Predicates &predicates, std::size_t resume = 0);
    void Tick(const Predicates &predicates, const std::function<Json(const Json &)> &execute,
              bool busy, const Json &response);
    void Stop();
    // Learn mode only: the user moves around or asks for one step to be performed.
    void Back();
    void Skip();
    // Show me is a visible demo: pointer travels to the target (kAnimS), the command runs, the result is held (kHoldS), then the coach says "Done"; it never advances by itself.
    void ShowMe() { if (Active() && mode == "learn" && !ShowMeBusy()) { showMe = true; phase = 1; phaseStart = std::chrono::steady_clock::now(); } }
    bool ShowMeBusy() const { return phase != 0; }
    double ShowMeAnim() const; // 0..1 while the pointer travels, otherwise -1
    bool ShowMeHold() const { return phase == 3; }
    bool ShownMe() const { return shownMe; }
    static constexpr double kAnimS = 0.8, kHoldS = 1.5;
    bool AlreadyDone() const { return Active() && mode == "learn" && entryMet; } // condition was true when the step began
    bool CanGoBack() const { return Active() && mode == "learn" && index > 0; }
    bool Active() const { return state == "running"; }
    bool Automatic() const { return Active() && mode != "learn"; }
    const Json *Step() const;
    Json Report() const;
    static bool Check(const Json &condition, const Predicates &predicates);
private:
    Json definition = Json::object(), results = Json::array();
    std::string mode = "learn", state = "idle";
    std::size_t index = 0;
    bool submitted = false, showMe = false, entryMet = false, shownMe = false;
    int phase = 0; // 0 idle, 1 pointer animation, 2 waiting for the command, 3 holding the result
    std::chrono::steady_clock::time_point phaseStart;
    void ResetShowMe() { showMe = shownMe = false; phase = 0; }
    Predicates preds; // copy: Back/Skip run outside Tick
    std::chrono::steady_clock::time_point started;
    void FinishStep(bool pass, const std::string &code = "", const std::string &message = "");
};
}
