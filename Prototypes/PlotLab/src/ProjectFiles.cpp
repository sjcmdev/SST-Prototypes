#include "ProjectFiles.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <array>
#include <fstream>
#include <stdexcept>
#include <cwctype>
#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace SST::Prototypes::PlotLab {
namespace {
constexpr std::string_view withoutImports="sst.plotlab.save-set.v1\nimports=0\n";
constexpr std::string_view withImports="sst.plotlab.save-set.v1\nimports=1\n";
auto Pending(const std::filesystem::path& file) {auto p=file;p+=".pending-save";return p;}
auto Sidecar(const std::filesystem::path& file,const char* suffix) {auto p=file;p+=suffix;return p;}
void Copy(const std::filesystem::path& from,const std::filesystem::path& to) {
    std::ifstream in(from,std::ios::binary);
    if(!in)throw std::runtime_error("Missing prepared project file: "+Path(from).Utf8());
    std::string error;
    if(!TextFileIO::Save(Path(to),[&](FILE* out) {
        std::array<char,65536> buffer;
        while(in) {in.read(buffer.data(),buffer.size());const auto size=static_cast<std::size_t>(in.gcount());if(std::fwrite(buffer.data(),1,size,out)!=size)return false;}
        return in.eof() && !in.bad();
    },error))throw std::runtime_error(error);
}
void Commit(const std::filesystem::path& destination,bool imports,const std::function<void(unsigned)>& afterCommit={}) {
    const auto directory=Pending(destination),staged=directory/destination.filename();
    // Check the entire generation before replacing any destination; recovery keeps these files intact.
    for(const auto* suffix:{"",".analysis.json"})if(!std::filesystem::is_regular_file(Sidecar(staged,suffix)))throw std::runtime_error("Incomplete prepared project generation");
    if(imports && !std::filesystem::is_regular_file(Sidecar(staged,".imports.json")))throw std::runtime_error("Missing prepared import state");
    const auto fonts=Sidecar(staged,".fonts");
    if(std::filesystem::exists(fonts))for(const auto& entry:std::filesystem::directory_iterator(fonts)) {
        if(!entry.is_regular_file())throw std::runtime_error("Invalid prepared font entry");
        Copy(entry.path(),Sidecar(destination,".fonts")/entry.path().filename());
    }
    Copy(Sidecar(staged,".analysis.json"),Sidecar(destination,".analysis.json"));
    if(afterCommit)afterCommit(1);
    if(imports)Copy(Sidecar(staged,".imports.json"),Sidecar(destination,".imports.json"));
    else std::filesystem::remove(Sidecar(destination,".imports.json"));
    if(afterCommit)afterCommit(2);
    Copy(staged,destination);
    if(afterCommit)afterCommit(3);
    // Removing the manifest is the commit point; an interrupted cleanup is harmless.
    std::filesystem::remove(directory/"ready");
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);
}
}
bool RecoverPrepared(const std::filesystem::path& destination) {
    if(destination.empty())return false;
    const auto directory=Pending(destination),manifest=directory/"ready";
    if(!std::filesystem::exists(manifest))return false;
    std::string text,error;
    if(!TextFileIO::Load(Path(manifest),text,error))throw std::runtime_error(error);
    if(text!=withoutImports && text!=withImports)throw std::runtime_error("Unsupported project save manifest; prepared files retained");
    Commit(destination,text==withImports);
    return true;
}
ProjectFilesLock::ProjectFilesLock(const std::filesystem::path& destination) {
    if(destination.empty())return;
#ifdef _WIN32
    auto key=std::filesystem::weakly_canonical(std::filesystem::absolute(destination)).wstring();
    std::uint64_t hash=14695981039346656037ull;
    for(auto c:key){hash^=static_cast<std::uint64_t>(std::towlower(c));hash*=1099511628211ull;}
    const auto name=L"Local\\SST.PlotLab.Save."+std::to_wstring(hash);
    const auto handle=CreateMutexW(nullptr,FALSE,name.c_str());
    if(!handle)throw std::runtime_error("Cannot lock project files");
    const auto result=WaitForSingleObject(handle,0);
    if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED){CloseHandle(handle);throw std::runtime_error("Project files are in use; retry when the other open/save finishes");}
    native=reinterpret_cast<std::intptr_t>(handle);
#else
    native=::open(Sidecar(destination,".save-lock").c_str(),O_CREAT|O_RDWR,0600);
    if(native<0 || flock(static_cast<int>(native),LOCK_EX|LOCK_NB)!=0){if(native>=0)::close(static_cast<int>(native));native=-1;throw std::runtime_error("Project files are in use");}
#endif
    try {recovered=RecoverPrepared(destination);}
    catch(...) {
#ifdef _WIN32
        ReleaseMutex(reinterpret_cast<HANDLE>(native));CloseHandle(reinterpret_cast<HANDLE>(native));
#else
        ::close(static_cast<int>(native));
#endif
        native=-1;throw;
    }
}
ProjectFilesLock::~ProjectFilesLock() {
    if(native==-1)return;
#ifdef _WIN32
    ReleaseMutex(reinterpret_cast<HANDLE>(native));CloseHandle(reinterpret_cast<HANDLE>(native));
#else
    ::close(static_cast<int>(native));
#endif
}
bool RecoverProjectFiles(const std::filesystem::path& destination) {return ProjectFilesLock(destination).Recovered();}
ProjectSidecars LoadProjectSidecars(const std::filesystem::path& source) {
    ProjectSidecars state;state.workflow=LoadWorkflowState(source);
    const auto presets=Sidecar(source,".imports.json");
    if(!source.empty() && std::filesystem::exists(presets))state.presets=LoadImportPresets(presets,&state.ui,&state.recipes);
    return state;
}
void SaveProjectFiles(const std::filesystem::path& destination,const std::function<bool(const std::filesystem::path&)>& writer,
                      const std::function<void(unsigned)>& afterCommit) {
    if(destination.empty() || destination.filename().empty())throw std::invalid_argument("Project path is empty");
    if(!destination.parent_path().empty())std::filesystem::create_directories(destination.parent_path());
    ProjectFilesLock lock(destination);
    const auto directory=Pending(destination);
    // An unprepared generation never touched the previous project and can be discarded.
    if(std::filesystem::exists(directory))std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    const bool imports=writer(directory/destination.filename());
    std::string error;
    if(!TextFileIO::Save(Path(directory/"ready"),imports?withImports:withoutImports,error))throw std::runtime_error(error);
    if(afterCommit)afterCommit(0);
    Commit(destination,imports,afterCommit);
}
}
