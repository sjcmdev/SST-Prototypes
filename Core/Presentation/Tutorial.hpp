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
    bool Active() const { return state == "running"; }
    bool Automatic() const { return Active() && mode != "learn"; }
    const Json *Step() const;
    Json Report() const;
    static bool Check(const Json &condition, const Predicates &predicates);
private:
    Json definition = Json::object(), results = Json::array();
    std::string mode = "learn", state = "idle";
    std::size_t index = 0;
    bool submitted = false;
    std::chrono::steady_clock::time_point started;
    void FinishStep(bool pass, const std::string &code = "", const std::string &message = "");
};
}
