#include <scaps_core/boundary_probe.hpp>
#include "Domain/Packed.hpp"
#include <cstring>
#include <mutex>
#include <unordered_map>
namespace SST::ScapsLab {
namespace {
struct Pool {
    std::mutex lock;
    std::unordered_map<std::uint64_t, std::vector<WeakRef<const std::vector<double>>>> buckets;
    std::size_t inserts = 0;
};
Pool &ThePool() {static Pool pool; return pool;}
std::uint64_t Hash(const std::vector<double> &v) {
    std::uint64_t h = 14695981039346656037ull ^ v.size();
    for (double d : v) {std::uint64_t bits; std::memcpy(&bits, &d, 8); h = (h ^ bits) * 1099511628211ull; h ^= h >> 29;}
    return h;
}
bool Same(const std::vector<double> &a, const std::vector<double> &b) {return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * 8) == 0);} // bitwise: -0.0 and NaN payloads survive
constexpr std::size_t kMinFloats = 8; // shorter arrays stay plain Json (a Column costs more than the values)
bool IsFloats(const Json &a) {
    if (!a.is_array() || a.size() < kMinFloats) return false;
    bool anyFloat = false;
    for (const auto &e : a) {if (e.is_number_float()) anyFloat = true; else if (!e.is_null()) return false;}
    return anyFloat;
}
bool Structured(const Json &v) {return v.is_object() || v.is_array();}
} // namespace
Column Column::Intern(std::vector<double> v) {
    Column out;
    if (v.empty()) return out;
    auto &pool = ThePool();
    const auto h = Hash(v);
    std::lock_guard guard(pool.lock);
    auto &bucket = pool.buckets[h];
    for (auto it = bucket.begin(); it != bucket.end();) {
        if (auto live = it->lock()) {if (Same(*live, v)) {out.p = std::move(live); return out;} ++it;}
        else it = bucket.erase(it);
    }
    out.p = CreateRef<const std::vector<double>>(std::move(v));
    bucket.push_back(out.p);
    if (++pool.inserts % 4096 == 0) for (auto it = pool.buckets.begin(); it != pool.buckets.end();) { // sweep dead buckets now and then
        auto &b = it->second; std::erase_if(b, [](const auto &w) {return w.expired();});
        it = b.empty() ? pool.buckets.erase(it) : std::next(it);
    }
    return out;
}
PJson Pack(const Json &j) {SCAPS_BOUNDARY_RESULT();
    PJson out;
    if (IsFloats(j)) {
        std::vector<double> v; v.reserve(j.size());
        for (const auto &e : j) v.push_back(e.is_null() ? double(NAN) : e.get<double>());
        out.kind = PJson::Kind::Floats; out.f = Column::Intern(std::move(v)); return out;
    }
    if (j.is_array() && std::any_of(j.begin(), j.end(), Structured)) {
        std::vector<std::pair<std::string, PJson>> kids; kids.reserve(j.size()); bool packed = false;
        for (const auto &e : j) {kids.emplace_back(std::string{}, Pack(e)); packed |= kids.back().second.kind != PJson::Kind::Plain;}
        if (packed) {out.kind = PJson::Kind::Arr; out.kids = std::move(kids); return out;}
    } else if (j.is_object()) {
        Json prim = Json::object(); std::vector<std::pair<std::string, PJson>> kids;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (Structured(it.value())) {auto k = Pack(it.value()); if (k.kind != PJson::Kind::Plain) {kids.emplace_back(it.key(), std::move(k)); continue;}}
            prim[it.key()] = it.value();
        }
        if (!kids.empty()) {out.kind = PJson::Kind::Obj; out.prim = std::move(prim); out.kids = std::move(kids); return out;}
    }
    out.prim = j; return out;
}
Json Unpack(const PJson &p) {SCAPS_BOUNDARY_RESULT();
    switch (p.kind) {
    case PJson::Kind::Plain: return p.prim;
    case PJson::Kind::Floats: {auto a = Json::array(); for (double v : p.f) a.push_back(FiniteOrNull(v)); return a;}
    case PJson::Kind::Arr: {auto a = Json::array(); for (const auto &k : p.kids) a.push_back(Unpack(k.second)); return a;}
    default: {Json o = p.prim; for (const auto &k : p.kids) o[k.first] = Unpack(k.second); return o;}
    }
}
void DropStructured(PJson &p) {
    if (p.kind == PJson::Kind::Obj) {p.kids.clear(); p.kind = PJson::Kind::Plain;}
    if (p.kind == PJson::Kind::Plain && p.prim.is_object())
        for (auto it = p.prim.begin(); it != p.prim.end();) it = Structured(it.value()) ? p.prim.erase(it) : std::next(it);
}
} // namespace SST::ScapsLab
