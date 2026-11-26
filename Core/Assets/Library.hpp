#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace SST::Assets {
enum class Scope { Builtin=0, User=1, Project=2, Shared=3 };
enum class UsePolicy { Copy, Linked };
enum class ErrorCode { Invalid, Missing, ReadOnly, Conflict, Unsupported, Io };
inline const char* CodeName(ErrorCode code) {switch(code){case ErrorCode::Missing:return "missing";case ErrorCode::ReadOnly:return "read_only";case ErrorCode::Conflict:return "conflict";case ErrorCode::Unsupported:return "unsupported";case ErrorCode::Io:return "io_error";default:return "invalid_asset";}}
struct Error : std::runtime_error {
    ErrorCode code;
    Error(ErrorCode c,const std::string& message):std::runtime_error(message),code(c) {}
};
struct Reference {
    std::string libraryId,itemId;
    std::uint32_t version=0;
    bool operator==(const Reference&) const = default;
};
struct Provenance {
    std::string source,citation,doi,locator,author,date,runId,sourcePath;
    bool operator==(const Provenance&) const = default;
};
struct ParameterRange {
    std::string path,unit,citation;
    std::optional<double> lower,upper,uncertainty;
    bool operator==(const ParameterRange&) const = default;
};
struct Validity {
    std::optional<double> temperatureMin_K,temperatureMax_K,compositionMin,compositionMax;
    bool operator==(const Validity&) const = default;
};
struct Metadata {
    std::string id,name,description,collection,changeNote;
    std::vector<std::string> tags;
    Provenance provenance;
    Validity validity;
    std::vector<ParameterRange> parameters;
    std::optional<Reference> basedOn;
    bool locked=false;
    bool operator==(const Metadata&) const = default;
};
struct Use {
    Reference source;
    UsePolicy policy=UsePolicy::Copy;
    std::string target; // stable application identity, never a vector index
    bool operator==(const Use&) const = default;
};
struct Usage {
    std::string project,device,name;
    Use use;
};
inline bool ValidId(const std::string& text) {
    return !text.empty() && text.size()<=128 && std::all_of(text.begin(),text.end(),[](unsigned char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.';}) && text!="." && text!="..";
}
inline void Require(bool condition,const std::string& message) {if(!condition)throw Error(ErrorCode::Invalid,message);}
inline void Validate(const Metadata& m) {
    Require(ValidId(m.id),"Invalid asset ID: "+m.id);Require(!m.name.empty(),"Asset name is required");
    Require(!m.collection.starts_with('/') && !m.collection.starts_with('\\') && m.collection.find("..") == std::string::npos,"Collection must be a relative folder");
    const auto range=[](std::optional<double> lo,std::optional<double> hi,const std::string& label) {
        Require(!lo || std::isfinite(*lo),label+" minimum must be finite");Require(!hi || std::isfinite(*hi),label+" maximum must be finite");Require(!lo || !hi || *lo<=*hi,label+" minimum exceeds maximum");
    };
    range(m.validity.temperatureMin_K,m.validity.temperatureMax_K,"Temperature");
    Require((!m.validity.temperatureMin_K || *m.validity.temperatureMin_K>0) && (!m.validity.temperatureMax_K || *m.validity.temperatureMax_K>0),"Validity temperature must be positive [K]");
    range(m.validity.compositionMin,m.validity.compositionMax,"Composition");
    Require((!m.validity.compositionMin || (*m.validity.compositionMin>=0 && *m.validity.compositionMin<=1)) && (!m.validity.compositionMax || (*m.validity.compositionMax>=0 && *m.validity.compositionMax<=1)),"Composition validity must be in [0,1]");
    std::vector<std::string> paths;
    for(const auto& p:m.parameters) {
        Require(!p.path.empty() && !p.unit.empty(),"Parameter range needs a path and unit");range(p.lower,p.upper,p.path);
        Require(!p.uncertainty || (std::isfinite(*p.uncertainty) && *p.uncertainty>=0),p.path+": uncertainty must be finite and nonnegative");
        Require(std::find(paths.begin(),paths.end(),p.path)==paths.end(),"Duplicate parameter range: "+p.path);paths.push_back(p.path);
    }
    if(m.basedOn)Require(ValidId(m.basedOn->libraryId) && ValidId(m.basedOn->itemId) && m.basedOn->version>0,"Invalid based-on reference");
}
inline std::string Fold(std::string text) {for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
struct Filter {std::string text,collection,tag;std::vector<std::string> favorites;bool favoritesOnly=false;bool operator==(const Filter&) const = default;};
template<class Payload> struct Revision {
    Metadata meta;
    std::uint32_t version=1;
    std::shared_ptr<const Payload> payload;
};
// A collection of immutable revisions; semiconductor schemas and file codecs stay outside Core.
template<class Payload> class Library {
public:
    using Rev=Revision<Payload>;
    using History=std::vector<std::shared_ptr<const Rev>>;
    std::string id,name;
    Scope scope=Scope::User;
    bool readOnly=false;
    std::uint64_t generation=0;
    explicit Library(std::function<void(const Payload&)> validator={}):validate(std::move(validator)) {}
    bool ReadOnly() const {return readOnly || scope==Scope::Builtin;}
    const std::map<std::string,History>& Items() const {return items;}
    std::shared_ptr<const Rev> Find(const std::string& key,std::uint32_t version=0) const {
        const auto found=items.find(key);if(found==items.end())throw Error(ErrorCode::Missing,"Unknown asset: "+key);
        if(version==0)return found->second.back();
        for(const auto& r:found->second)if(r->version==version)return r;
        throw Error(ErrorCode::Missing,"Unknown asset version: "+key+" v"+std::to_string(version));
    }
    std::vector<std::shared_ptr<const Rev>> Search(const Filter& f={}) const {
        std::vector<std::shared_ptr<const Rev>> out;const auto needle=Fold(f.text);
        for(const auto& [key,history]:items) {
            const auto& r=history.back();const auto& m=r->meta;
            if(!f.collection.empty() && m.collection!=f.collection && !m.collection.starts_with(f.collection+"/"))continue;
            if(!f.tag.empty() && std::find(m.tags.begin(),m.tags.end(),f.tag)==m.tags.end())continue;
            if(f.favoritesOnly && std::find(f.favorites.begin(),f.favorites.end(),key)==f.favorites.end())continue;
            std::string hay=m.name+" "+m.id+" "+m.description+" "+m.provenance.citation;for(const auto& tag:m.tags)hay+=" "+tag;
            if(!needle.empty() && Fold(hay).find(needle)==std::string::npos)continue;
            out.push_back(r);
        }
        return out;
    }
    std::shared_ptr<const Rev> Create(Metadata m,std::shared_ptr<const Payload> payload) {
        Writable();if(items.contains(m.id))throw Error(ErrorCode::Conflict,"Asset already exists: "+m.id);
        Check(m,payload);const auto key=m.id;auto r=std::make_shared<const Rev>(Rev{std::move(m),1,std::move(payload)});items[key].push_back(r);++generation;return r;
    }
    std::shared_ptr<const Rev> Revise(Metadata m,std::shared_ptr<const Payload> payload,std::uint32_t expectedVersion) {
        Writable();auto previous=Find(m.id);if(previous->meta.locked)throw Error(ErrorCode::ReadOnly,"Asset is locked; derive an editable copy");
        if(previous->version!=expectedVersion)throw Error(ErrorCode::Conflict,"Asset changed; reload before saving");
        Require(previous->version<UINT32_MAX,"Asset version exhausted");Require(!m.changeNote.empty(),"A change note is required");Check(m,payload);
        const auto key=m.id;auto r=std::make_shared<const Rev>(Rev{std::move(m),previous->version+1,std::move(payload)});items.at(key).push_back(r);++generation;return r;
    }
    std::shared_ptr<const Rev> Derive(const std::string& sourceLibrary,const Rev& source,Metadata m) {
        m.basedOn=Reference{sourceLibrary,source.meta.id,source.version};m.locked=false;return Create(std::move(m),source.payload);
    }
    void SetLocked(const std::string& key,bool locked) {
        Writable();auto old=Find(key);auto m=old->meta;m.locked=locked;Check(m,old->payload);
        // Lock is catalog access metadata; it does not manufacture a scientific revision.
        items.at(key).back()=std::make_shared<const Rev>(Rev{std::move(m),old->version,old->payload});++generation;
    }
    void Restore(History history) { // file importer validates the entire candidate before publishing it
        Require(!history.empty() && bool(history.front()),"Empty asset history");const auto key=history.front()->meta.id;
        Require(!items.contains(key),"Duplicate asset ID: "+key);std::uint32_t version=0;
        for(const auto& r:history){Require(bool(r),"Null asset revision");Check(r->meta,r->payload);Require(r->meta.id==key && r->version==++version,"Asset history must have consecutive versions starting at 1");}
        items.emplace(key,std::move(history));
    }
private:
    std::map<std::string,History> items;
    std::function<void(const Payload&)> validate;
    void Writable() const {if(ReadOnly())throw Error(ErrorCode::ReadOnly,"Library is read-only; derive into a user or project library");}
    void Check(const Metadata& m,const std::shared_ptr<const Payload>& p) const {Validate(m);Require(bool(p),"Missing typed asset payload");if(validate)validate(*p);}
};
inline std::vector<Usage> WhereUsed(const std::vector<Usage>& known,const Reference& ref) {
    std::vector<Usage> out;for(const auto& u:known)if(u.use.source.libraryId==ref.libraryId && u.use.source.itemId==ref.itemId && (!ref.version || u.use.source.version==ref.version))out.push_back(u);return out;
}
}
