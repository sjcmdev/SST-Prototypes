#include "Core/dspch.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <charconv>
#include <fstream>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

namespace SST {
namespace {
    Path s_CrashDirectory;
    constexpr std::string_view CrashOwner="SST-CRASH-v1\n";
    bool isCrashSession(const std::filesystem::path& path) {
        std::ifstream stream(path/"owner",std::ios::binary);char owner[32]{};
        stream.read(owner,static_cast<std::streamsize>(CrashOwner.size()));
        return stream.gcount()==static_cast<std::streamsize>(CrashOwner.size()) && std::string_view(owner,CrashOwner.size())==CrashOwner;
    }
#ifdef _WIN32
    HANDLE s_Pending = INVALID_HANDLE_VALUE;
    wchar_t s_Executable[32768]{};
    wchar_t s_DumpPath[32768]{};
    wchar_t s_DumpCommand[32768]{};
    LONG s_Capturing = 0;
#endif
}
Path DiagnosticsDirectory() {
#ifdef _WIN32
    if(const auto* root = _wgetenv(L"SST_DIAGNOSTICS_ROOT"); root && *root) return Path::FromResolved(std::filesystem::absolute(std::filesystem::path(root)));
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto* local = _wgetenv(L"LOCALAPPDATA");
    return Path::FromResolved((local ? std::filesystem::path(local) : FileSystem::TempDirectoryPath()) / L"SemiconductorStudio" / std::filesystem::path(executable).stem() / L"diagnostics");
#else
    if(const auto* root = std::getenv("SST_DIAGNOSTICS_ROOT"); root && *root) return Path::FromResolved(std::filesystem::absolute(std::filesystem::u8path(root)));
    return Path::FromResolved(FileSystem::TempDirectoryPath()/"SemiconductorStudio"/"diagnostics");
#endif
}
void InitializeLocalDiagnostics() {
    if(!s_CrashDirectory.Empty()) return;
    const auto root=DiagnosticsDirectory()/"crashes";
    std::filesystem::create_directories(root.Native());
    std::vector<std::filesystem::directory_entry> previous;
    for(const auto& entry:std::filesystem::directory_iterator(root.Native()))
        if(entry.is_directory() && !entry.is_symlink() && entry.path().filename().string().starts_with("session-") && isCrashSession(entry.path())) previous.push_back(entry);
    std::sort(previous.begin(),previous.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
    for(std::size_t i=19;i<previous.size();++i) {
#ifdef _WIN32
        const auto marker=previous[i].path()/"pending";
        if(std::filesystem::exists(marker)) {
            HANDLE probe=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(probe==INVALID_HANDLE_VALUE) continue;
            CloseHandle(probe);
        }
#endif
        std::error_code error;std::filesystem::remove_all(previous[i].path(),error);
    }
    s_CrashDirectory = root/("session-"+Logger::SessionId());
    std::filesystem::create_directories(s_CrashDirectory.Native());
    std::string error;
    if(!TextFileIO::Save(s_CrashDirectory/"owner",CrashOwner,error)) throw std::runtime_error("Cannot record crash ownership: "+error);
    if(!TextFileIO::Save(s_CrashDirectory/"build.txt", BuildDescription(), error)) throw std::runtime_error("Cannot record crash build identity: "+error);
#ifdef _WIN32
    GetModuleFileNameW(nullptr, s_Executable, 32768);
    wcscpy_s(s_DumpPath, (s_CrashDirectory/"crash.dmp").wstring().c_str());
    s_Pending = CreateFileW((s_CrashDirectory/"pending").wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(s_Pending == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create crash recovery marker. Check diagnostics directory permissions.");
#endif
}
void FinishLocalDiagnostics() {
#ifdef _WIN32
    if(s_Pending != INVALID_HANDLE_VALUE) { CloseHandle(s_Pending); s_Pending = INVALID_HANDLE_VALUE; }
#endif
    if(!s_CrashDirectory.Empty()) {
        std::error_code error;
        std::filesystem::remove((s_CrashDirectory/"pending").Native(), error);
        const bool dumpExists=std::filesystem::exists((s_CrashDirectory/"crash.dmp").Native(),error);
        if(!error && !dumpExists) std::filesystem::remove_all(s_CrashDirectory.Native(), error);
        s_CrashDirectory = {};
    }
}
void CaptureLocalCrash(void* pointers) noexcept {
#ifdef _WIN32
    if(!*s_DumpPath || InterlockedExchange(&s_Capturing, 1)) return;
    if(_snwprintf_s(s_DumpCommand, _TRUNCATE, L"\"%s\" --sst-minidump %lu %lu %llu \"%s\"", s_Executable, GetCurrentProcessId(), GetCurrentThreadId(), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pointers)), s_DumpPath) < 0) return;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if(CreateProcessW(s_Executable, s_DumpCommand, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) {
        if(WaitForSingleObject(child.hProcess, 15000) == WAIT_TIMEOUT) TerminateProcess(child.hProcess, 1);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
#else
    (void)pointers;
#endif
}
std::vector<Path> PreviousInterruptedSessions() {
    std::vector<Path> result;
    const auto root = DiagnosticsDirectory()/"crashes";
    std::error_code error;
    if(!std::filesystem::exists(root.Native(), error)) return result;
    for(const auto& entry : std::filesystem::directory_iterator(root.Native())) {
        if(!entry.is_directory() || entry.is_symlink() || !entry.path().filename().string().starts_with("session-") || !isCrashSession(entry.path())) continue;
#ifdef _WIN32
        HANDLE probe = CreateFileW((entry.path()/"pending").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(probe == INVALID_HANDLE_VALUE) continue;
        CloseHandle(probe);
        result.push_back(Path::FromResolved(entry.path()));
#endif
    }
    return result;
}
bool AcknowledgeInterruptedSession(const Path& session, std::string& error) {
    std::error_code ec;
    std::filesystem::remove((session/"pending").Native(), ec);
    if(ec) { error="Cannot dismiss recovery notice: "+ec.message()+". Check diagnostics directory permissions."; return false; }
    return true;
}
std::string DiagnosticPreview(const std::vector<LogEntry>& entries) {
    // Allowlist: free-form messages/settings/paths can contain partner data or credentials.
    const auto& logging=Logger::EffectiveOptions();
    std::string text = BuildDescription()+"\nPlatform: Windows / MSVC\nUpload: none\nSession: "+Logger::SessionId()
        +"\nInterrupted previous sessions: "+std::to_string(PreviousInterruptedSessions().size())
        +fmt::format("\nEffective logging: level={} file={} max_file_bytes={} files={} sessions={} runs_per_root=128 (active logs retained)\n",ToString(logging.level),logging.logToFile,logging.maxFileBytes,logging.rotatedFiles+1,logging.retainedSessions)
        +"Redaction: messages, settings values, source paths, scripts, raw data and dumps excluded.\n"
        +"Log tail (last 200 entries, metadata only):\n";
    const auto begin = entries.size()>200 ? entries.size()-200 : 0;
    for(auto i=begin;i<entries.size();++i) {
        const auto& e=entries[i];
        const auto code=e.context.code=="job.lifecycle" ? "job.lifecycle" : "redacted";
        text += fmt::format("level={} category={} code={} job={} attempt={} elapsed_ms={:.3f}\n", ToString(e.level), ToString(e.category),code,e.context.jobId, e.context.attempt, e.elapsedMs);
    }
    text += "Local crash dumps may contain sensitive memory. Share a dump separately only after reviewing consent and matching its build.txt with the private PDB.\n";
    return text;
}
bool ExportDiagnostics(const Path& destination, const std::string& preview, std::string& error) {
    return TextFileIO::Save(destination, preview, error);
}
int RunMinidumpHelper(int argc, char** argv) {
#ifdef _WIN32
    if(argc != 6) return 2;
    unsigned long long numbers[3]{};
    for(int i=0;i<3;++i) {
        const std::string_view value=argv[i+2];
        const auto [end, ec]=std::from_chars(value.data(), value.data()+value.size(), numbers[i]);
        if(ec!=std::errc{} || end!=value.data()+value.size()) return 2;
    }
    if(numbers[0]==0 || numbers[0]>MAXDWORD || numbers[1]==0 || numbers[1]>MAXDWORD) return 2;
    int wideCount=0;auto* wideArguments=CommandLineToArgvW(GetCommandLineW(),&wideCount);
    if(!wideArguments || wideCount!=6) {if(wideArguments) LocalFree(wideArguments);return 2;}
    const auto destination=std::filesystem::absolute(std::filesystem::path(wideArguments[5])).lexically_normal();
    LocalFree(wideArguments);
    const auto root=(DiagnosticsDirectory()/"crashes").Native().lexically_normal();
    const auto relative=destination.lexically_relative(root);
    if(relative.empty() || relative.begin()->string()==".." || destination.filename()!=L"crash.dmp") return 2;
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, static_cast<DWORD>(numbers[0]));
    if(!process) return 1;
    wchar_t target[32768]{}, current[32768]{}; DWORD size=32768;
    GetModuleFileNameW(nullptr, current, 32768);
    if(!QueryFullProcessImageNameW(process, 0, target, &size) || _wcsicmp(target,current)!=0) { CloseHandle(process); return 2; }
    HANDLE file=CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file==INVALID_HANDLE_VALUE) { CloseHandle(process); return 1; }
    MINIDUMP_EXCEPTION_INFORMATION info{static_cast<DWORD>(numbers[1]),reinterpret_cast<EXCEPTION_POINTERS*>(static_cast<std::uintptr_t>(numbers[2])),TRUE};
    const bool ok=MiniDumpWriteDump(process, static_cast<DWORD>(numbers[0]), file, MiniDumpNormal, numbers[2] ? &info : nullptr, nullptr, nullptr)!=FALSE;
    CloseHandle(file); CloseHandle(process);
    if(!ok) { std::error_code error; std::filesystem::remove(destination,error); }
    return ok ? 0 : 1;
#else
    (void)argc; (void)argv; return 1;
#endif
}
[[noreturn]] void RunCrashProbe(CrashProbeMode mode) {
    if(mode==CrashProbeMode::Terminate) std::terminate();
    if(mode==CrashProbeMode::Signal) std::raise(SIGABRT);
#ifdef _WIN32
    RaiseException(0xE0535354, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    std::abort();
}
}
