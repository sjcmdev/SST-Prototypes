#pragma once
#include "Core/IO/TextFileIO.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
// ScapsLab file IO seam: every disk access of the app goes through these helpers so tests can assert that none runs on the UI thread
// (Io::Watch(true) on the thread that plays "UI"; Io::UiCalls() lists what touched the disk from it). Production cost: one relaxed atomic load.
namespace SST::ScapsLab::Io {
struct State {std::atomic<bool> on{false};std::thread::id ui;std::mutex m;std::vector<std::string> calls;};
inline State &S() {static State s;return s;}
inline void Watch(bool on) {auto &s=S();if(on)s.ui=std::this_thread::get_id();{std::lock_guard l(s.m);s.calls.clear();}s.on.store(on,std::memory_order_release);}
inline std::vector<std::string> UiCalls() {auto &s=S();std::lock_guard l(s.m);return s.calls;}
inline void Note(const char *what,const std::string &path) {
    auto &s=S();if(!s.on.load(std::memory_order_relaxed) || std::this_thread::get_id()!=s.ui) return;
    std::lock_guard l(s.m);s.calls.push_back(std::string(what)+" "+path);
}
inline bool Load(const std::filesystem::path &p,std::string &text,std::string &error) {Note("load",p.string());return TextFileIO::Load(Path{p},text,error);}
inline bool Save(const std::filesystem::path &p,const std::string &text,std::string &error) {Note("save",p.string());return TextFileIO::Save(Path{p},text,error);}
inline bool Exists(const std::filesystem::path &p) {Note("exists",p.string());std::error_code ec;return std::filesystem::exists(p,ec);}
inline std::filesystem::path BackupOriginal(const std::filesystem::path& p,int version) {
    Note("backup",p.string());auto target=p;target+=".v"+std::to_string(version)+"-original";
    for(unsigned i=1;std::filesystem::exists(target);++i){target=p;target+=".v"+std::to_string(version)+"-original-"+std::to_string(i);}
    std::filesystem::copy_file(p,target);return target;
}
inline void Remove(const std::filesystem::path &p) {Note("remove",p.string());std::error_code ec;std::filesystem::remove(p,ec);}
}
