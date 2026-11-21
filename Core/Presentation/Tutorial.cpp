#include "Core/dspch.hpp"
#include "Tutorial.hpp"
#include <set>
#include <cmath>

namespace SST {
namespace {
void ValidateCondition(const Tutorial::Json &condition, const Tutorial::Predicates &predicates) {
    const auto name = condition.at("predicate").get<std::string>();
    if (!predicates.contains(name)) throw std::invalid_argument("Unknown tutorial predicate: " + name);
    const auto op = condition.at("op").get<std::string>();
    if (op != "eq" && op != "ge" && op != "gt" && op != "le" && op != "lt")
        throw std::invalid_argument("Unknown tutorial comparison: " + op);
    const auto &value = condition.at("value");
    if (op != "eq" && (!value.is_number() || !std::isfinite(value.get<double>())))
        throw std::invalid_argument("Tutorial comparison requires a finite number");
}
}
bool Tutorial::Check(const Json &condition, const Predicates &predicates) {
    ValidateCondition(condition, predicates);
    const auto value = predicates.at(condition.at("predicate").get<std::string>())();
    const auto &expected = condition.at("value");
    const auto op = condition.at("op").get<std::string>();
    if (op == "eq") return value == expected;
    if (!value.is_number() || !std::isfinite(value.get<double>())) return false;
    const double a = value.get<double>(), b = expected.get<double>();
    return op == "ge" ? a >= b : op == "gt" ? a > b : op == "le" ? a <= b : a < b;
}
void Tutorial::Start(Json data, const std::string &newMode, const Predicates &predicates, std::size_t resume) {
    if (Active()) throw std::invalid_argument("Stop the current tutorial first");
    if (newMode != "learn" && newMode != "demo" && newMode != "verify") throw std::invalid_argument("Unknown tutorial mode");
    if (data.at("schema_version") != 1 || data.at("id").get<std::string>().empty() || data.at("title").get<std::string>().empty())
        throw std::invalid_argument("Invalid tutorial definition");
    const auto &steps = data.at("steps");
    if (!steps.is_array() || steps.empty() || resume >= steps.size()) throw std::invalid_argument("Invalid tutorial steps or resume index");
    std::set<std::string> ids;
    for (const auto &step : steps) {
        const auto id = step.at("id").get<std::string>();
        if (id.empty() || !ids.insert(id).second || step.at("instruction").get<std::string>().empty()
            || step.at("target").at("window").get<std::string>().empty() || !step.at("command").is_object())
            throw std::invalid_argument("Invalid tutorial step");
        if (step.contains("timeout_s") && (!step.at("timeout_s").is_number() || !std::isfinite(step.at("timeout_s").get<double>()) || step.at("timeout_s").get<double>() <= 0))
            throw std::invalid_argument("Invalid tutorial timeout_s");
        ValidateCondition(step.at("condition"), predicates);
    }
    definition = std::move(data); mode = newMode; index = resume; state = "running";
    submitted = false; ResetShowMe(); results = Json::array(); started = std::chrono::steady_clock::now();
    preds = predicates; entryMet = Check(definition.at("steps").at(index).at("condition"), predicates);
}
double Tutorial::ShowMeAnim() const {
    if (phase != 1) return -1;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStart).count() / kAnimS;
    return t >= 1 ? -1 : t;
}
const Tutorial::Json *Tutorial::Step() const { return Active() ? &definition.at("steps").at(index) : nullptr; }
void Tutorial::Stop() { if (Active()) state = "stopped"; }
void Tutorial::Back() {
    if (!CanGoBack()) return;
    --index; if (results.size() > index) results.erase(results.begin() + index, results.end());
    submitted = false; ResetShowMe(); entryMet = Check(definition.at("steps").at(index).at("condition"), preds);
}
void Tutorial::Skip() {
    if (!Active() || mode != "learn") return;
    results.push_back({{"step_id", definition.at("steps")[index].at("id")}, {"status", "SKIP"}, {"error", nullptr}});
    if (++index == definition.at("steps").size()) state = "passed";
    submitted = false; ResetShowMe(); entryMet = state == "running" && Check(definition.at("steps").at(index).at("condition"), preds);
}
void Tutorial::FinishStep(bool pass, const std::string &code, const std::string &message) {
    results.push_back({{"step_id", definition.at("steps")[index].at("id")}, {"status", pass ? "PASS" : "FAIL"},
        {"error", pass ? Json(nullptr) : Json{{"code", code}, {"message", message}}}});
    if (!pass) {
        for (std::size_t i = index + 1; i < definition.at("steps").size(); ++i)
            results.push_back({{"step_id", definition.at("steps")[i].at("id")}, {"status", "SKIP"}, {"error", nullptr}});
        state = "failed"; return;
    }
    if (++index == definition.at("steps").size()) state = "passed";
    submitted = false; ResetShowMe(); started = std::chrono::steady_clock::now();
    entryMet = state == "running" && Check(definition.at("steps").at(index).at("condition"), preds);
}
void Tutorial::Tick(const Predicates &predicates, const std::function<Json(const Json &)> &execute, bool busy, const Json &response) {
    if (!Active()) return;
    try {
        const auto &step = *Step();
        if (mode == "learn") {
            if (busy) return;
            if (phase == 1) {
                if (ShowMeAnim() >= 0) return;
                showMe = false; (void)execute(step.at("command")); phase = 2; return;
            }
            if (phase == 2) { phase = 3; phaseStart = std::chrono::steady_clock::now(); return; }
            if (phase == 3) {
                if (std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStart).count() < kHoldS) return;
                phase = 0; shownMe = entryMet = true; return; // done: the user presses Next
            }
            // A condition that already held when the step began needs the user's Next; it never auto-advances.
            if (!Check(step.at("condition"), predicates)) entryMet = false;
            else if (!entryMet) FinishStep(true);
            return;
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > step.value("timeout_s", 120.)) {
            FinishStep(false, "tutorial.timeout", "Step exceeded timeout_s"); return;
        }
        if (busy) return;
        if (!submitted) {
            const auto result = execute(step.at("command"));
            if (!result.value("ok", false)) {
                FinishStep(false, "tutorial.command_failed", result.value("error", Json::object()).dump()); return;
            }
            submitted = true; return;
        }
        if (!response.value("ok", false)) FinishStep(false, "tutorial.command_failed", response.dump());
        else if (Check(step.at("condition"), predicates)) FinishStep(true);
        else FinishStep(false, "tutorial.condition_failed", step.at("condition").dump());
    } catch (const std::exception &e) { FinishStep(false, "tutorial.invalid_step", e.what()); }
}
Tutorial::Json Tutorial::Report() const {
    return {{"schema_version", 1}, {"tutorial_id", definition.value("id", std::string{})}, {"mode", mode},
        {"status", state}, {"step_index", index}, {"step_count", definition.contains("steps") ? definition.at("steps").size() : 0},
        {"steps", results}, {"ok", state == "passed"}};
}
}
