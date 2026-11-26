#pragma once
#include "Core/Utils/Path.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace SST {
struct RecoveryCandidate { Path directory, snapshot, original; };
// File-edge policy: immutable generations, ready marker last, five completed generations per document.
// The OS lease excludes live sessions; recovery always opens a copy, never overwrites the source.
class ProjectRecovery {
public:
    explicit ProjectRecovery(Path root);
    ~ProjectRecovery();
    ProjectRecovery(const ProjectRecovery&) = delete;
    void Begin();
    void Save(std::uint64_t key, std::uint64_t revision, const Path& original, const char* extension,
              const std::function<void(const Path&)>& write);
    void End();
    bool NeedsSave(std::uint64_t key,std::uint64_t revision) const;
    static std::vector<RecoveryCandidate> Candidates(const Path& root);
    static void Discard(const RecoveryCandidate& candidate);
private:
    Path session;
    void* lease = nullptr;
    std::map<std::uint64_t,std::uint64_t> revisions;
    std::set<std::uint64_t> visited;
};
struct RecoveryWrite {
    std::uint64_t key,revision;
    Path original;
    std::string extension;
    std::function<void(const Path&)> write;
};
// Capture typed immutable snapshots on the UI thread, then write them in one Core job.
class ProjectRecoveryPlan {
public:
    explicit ProjectRecoveryPlan(ProjectRecovery& recovery):recovery(recovery){}
    template<class Prepare> void Save(std::uint64_t key,std::uint64_t revision,Path original,const char* extension,Prepare prepare) {
        std::function<void(const Path&)> write;
        if(recovery.NeedsSave(key,revision))write=prepare();
        writes.push_back({key,revision,std::move(original),extension,std::move(write)});
    }
    std::vector<RecoveryWrite> writes;
private:
    ProjectRecovery& recovery;
};
}
