#include "Core/dspch.hpp"
#include "Core/IO/ProjectRecovery.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Logging/Logger.hpp"
#include <Windows.h>
#include <fstream>
#include <charconv>
#include <atomic>

namespace SST {
namespace {
constexpr auto Owner = "SST-RECOVERY-v1";
bool owned(const std::filesystem::path& directory) {
    if (std::filesystem::is_symlink(directory)) return false;
    std::ifstream stream(directory/"owner"); std::string line; std::getline(stream,line);
    return line == Owner;
}
void saveText(const Path& path, std::string_view text) {
    std::string error;
    if (!TextFileIO::Save(path,text,error)) throw std::runtime_error(error);
}
std::vector<std::filesystem::directory_entry> generations(const std::filesystem::path& document) {
    std::vector<std::filesystem::directory_entry> result;
    for (const auto& g : std::filesystem::directory_iterator(document))
        if (g.is_directory() && !g.is_symlink() && g.path().filename().string().starts_with("generation-") && std::filesystem::is_regular_file(g.path()/"ready")) result.push_back(g);
    const auto order=[](const auto& entry) {
        const auto name=entry.path().filename().string();
        const auto first=name.find('-')+1,last=name.rfind('-')+1;
        std::uint64_t tick=0,sequence=0;
        std::from_chars(name.data()+first,name.data()+name.size(),tick);
        std::from_chars(name.data()+last,name.data()+name.size(),sequence);
        return std::pair{tick,sequence};
    };
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b) { return order(a)>order(b); });
    return result;
}
}
ProjectRecovery::ProjectRecovery(Path root) {
    session = root/("session-"+Logger::SessionId());
    std::filesystem::create_directories(session.Native());
    lease = CreateFileW((session/"lease").Native().c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (lease == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot reserve the recovery session. Check settings directory permissions and free space.");
    try { saveText(session/"owner",Owner); }
    catch (...) { CloseHandle(lease); lease=nullptr; throw; }
}
ProjectRecovery::~ProjectRecovery() { if (lease && lease != INVALID_HANDLE_VALUE) CloseHandle(lease); }
void ProjectRecovery::Begin() { visited.clear(); }
bool ProjectRecovery::NeedsSave(std::uint64_t key,std::uint64_t revision) const {
    const auto it=revisions.find(key);return it==revisions.end() || it->second!=revision;
}
void ProjectRecovery::Save(std::uint64_t key,std::uint64_t revision,const Path& original,const char* extension,const std::function<void(const Path&)>& write) {
    visited.insert(key);
    if (const auto it=revisions.find(key); it!=revisions.end() && it->second==revision) return;
    const auto document=session/("document-"+std::to_string(key));
    std::filesystem::create_directories(document.Native());
    for(const auto& entry:std::filesystem::directory_iterator(document.Native()))
        if(entry.is_directory() && !entry.is_symlink() && entry.path().filename().string().starts_with("generation-") && !std::filesystem::exists(entry.path()/"ready")) std::filesystem::remove_all(entry.path());
    static std::atomic_uint64_t sequence{0};
    const auto generation=document/("generation-"+std::to_string(GetTickCount64())+"-"+std::to_string(revision)+"-"+std::to_string(++sequence));
    if(!std::filesystem::create_directory(generation.Native()))throw std::runtime_error("Cannot reserve an autosave generation. Check the recovery directory and retry.");
    const auto snapshot=generation/(std::string("project")+extension);
    write(snapshot);
    saveText(generation/"original",original.Utf8());
    saveText(generation/"ready",snapshot.filename().Utf8());
    revisions[key]=revision;
    const auto completed=generations(document.Native());
    for (std::size_t i=5;i<completed.size();++i) std::filesystem::remove_all(completed[i].path());
}
void ProjectRecovery::End() {
    for (const auto& entry:std::filesystem::directory_iterator(session.Native())) {
        const auto name=entry.path().filename().string();
        if (!entry.is_directory() || entry.is_symlink() || !name.starts_with("document-")) continue;
        std::uint64_t key=0;
        const auto text=std::string_view(name).substr(9);
        const auto [last,error]=std::from_chars(text.data(),text.data()+text.size(),key);
        if (error==std::errc{} && last==text.data()+text.size() && !visited.contains(key)) {
            std::filesystem::remove_all(entry.path()); revisions.erase(key);
        }
    }
}
std::vector<RecoveryCandidate> ProjectRecovery::Candidates(const Path& root) {
    std::vector<RecoveryCandidate> result;
    if (!std::filesystem::exists(root.Native())) return result;
    for (const auto& entry:std::filesystem::directory_iterator(root.Native())) {
        if (!entry.is_directory() || !entry.path().filename().string().starts_with("session-") || !owned(entry.path())) continue;
        HANDLE probe=CreateFileW((entry.path()/"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (probe==INVALID_HANDLE_VALUE) continue;
        struct Close { HANDLE handle; ~Close(){CloseHandle(handle);} } close{probe};
        for (const auto& doc:std::filesystem::directory_iterator(entry.path())) {
            if (!doc.is_directory() || doc.is_symlink() || !doc.path().filename().string().starts_with("document-")) continue;
            const auto completed=generations(doc.path());
            if (completed.empty()) continue;
            std::string snapshot,original,error;
            const auto generation=Path::FromResolved(completed.front().path());
            if (!TextFileIO::Load(generation/"ready",snapshot,error) || !TextFileIO::Load(generation/"original",original,error)) continue;
            const auto filename=std::filesystem::u8path(snapshot);
            if (filename.empty() || filename.has_parent_path() || (!std::filesystem::exists(generation.Native()/filename) || std::filesystem::is_symlink(generation.Native()/filename))) continue;
            result.push_back({Path::FromResolved(doc.path()),Path::FromResolved(generation.Native()/filename),Path::FromResolved(std::filesystem::u8path(original))});
        }
    }
    return result;
}
void ProjectRecovery::Discard(const RecoveryCandidate& candidate) {
    const auto directory=candidate.directory.Native();
    if (!directory.filename().string().starts_with("document-") || !owned(directory.parent_path()) || std::filesystem::is_symlink(directory)) throw std::invalid_argument("Unrecognized recovery directory; nothing was deleted.");
    HANDLE probe=CreateFileW((directory.parent_path()/"lease").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (probe==INVALID_HANDLE_VALUE) throw std::runtime_error("This recovery session is active. Close its application before discarding the snapshot.");
    struct Close { HANDLE handle; ~Close(){CloseHandle(handle);} } close{probe};
    std::filesystem::remove_all(directory);
}
}
