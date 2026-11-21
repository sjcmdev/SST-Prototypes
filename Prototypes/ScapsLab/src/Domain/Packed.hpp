#pragma once
// Compact, lossless storage for result data (S4). Json is only the interchange form; results are kept as Column columns and PJson trees.
//  * Column: immutable double array, shared; identical arrays (mesh x_m repeated per bias point, all-zero recombination channels, ...) are
//    interned process-wide (weakly), so they cost once.
//  * PJson: a Json tree where float arrays became Column; everything else stays Json. Unpack(Pack(j)) dumps byte-identically to j
//    (non-finite floats and nulls inside float arrays are both written as null, exactly as Json::dump does).
#include "Core/Utils/Memory.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
namespace SST::ScapsLab {
using Json = nlohmann::json;
class Column {
    Ref<const std::vector<double>> p;
public:
    Column() = default;
    static Column Intern(std::vector<double> v); // empty -> null Column
    const std::vector<double> &get() const {static const std::vector<double> none; return p ? *p : none;}
    operator const std::vector<double> &() const {return get();}
    std::size_t size() const {return p ? p->size() : 0;}
    bool empty() const {return !p || p->empty();}
    double operator[](std::size_t i) const {return (*p)[i];}
    auto begin() const {return get().begin();}
    auto end() const {return get().end();}
    bool operator==(const Column &o) const {return get() == o.get();}
    const void *id() const {return p.get();}
};
struct PJson {
    enum class Kind : std::uint8_t {Plain, Floats, Obj, Arr};
    Kind kind = Kind::Plain;
    Json prim;                                       // Plain: the whole value; Obj: members that stay Json
    Column f;                                           // Floats
    std::vector<std::pair<std::string, PJson>> kids; // Obj: packed members; Arr: elements (key empty)
};
PJson Pack(const Json &j);
Json Unpack(const PJson &p);
void DropStructured(PJson &p); // erases every array/object member of an object (profile dropping); non-objects are left alone
inline Json FiniteOrNull(double v) {return std::isfinite(v) ? Json(v) : Json(nullptr);}
} // namespace SST::ScapsLab
