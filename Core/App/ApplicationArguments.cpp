#include "Core/dspch.hpp"
#include "Core/App/Application.hpp"
#include "Core/App/ApplicationInternals.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/Diagnostics/DiagnosticsCommand.hpp"
#include <charconv>
#include <iostream>

namespace SST {
namespace {
constexpr auto Usage = "Usage: SST [--help] [--version] [--reset-layout] [--trace-events]\n"
    "  [--log-level=trace|debug|info|warn|error|critical] [--log-file[=path]]\n"
    "  [--open <project-or-data-path>] [--screenshot <path.png>]\n"
    "  [--screenshot-after-frames <positive-integer>] [--exit-after-screenshot]\n"
    "  [--export-diagnostics <path.txt>] [--crash-probe[=terminate|signal|wait]]\n"
    "Screenshots capture the main framebuffer. Crash probe deliberately terminates this process.\n";
}
namespace ApplicationDetail {
ApplicationSpecification ParseApplicationArguments(int argc, char** argv) {
    ApplicationSpecification spec;
    bool frameCountSpecified=false;
    if(argc<0 || (argc>0 && !argv)) throw std::invalid_argument("Invalid argument vector");
    for(int i=1;i<argc;++i) {
        if(!argv[i]) throw std::invalid_argument("Null command-line argument");
        const std::string_view arg=argv[i];
        auto value=[&]() -> std::string_view {
            if(++i>=argc || !argv[i] || !*argv[i] || argv[i][0]=='-') throw std::invalid_argument(std::string(arg)+" requires a value");
            return argv[i];
        };
        if(arg=="--help" || arg=="-h") spec.showHelp=true;
        else if(arg=="--version") spec.showVersion=true;
        else if(arg=="--crash-probe") spec.crashProbe=CrashProbeMode::Native;
        else if(arg=="--crash-probe=wait") spec.crashProbe=CrashProbeMode::Wait;
        else if(arg=="--crash-probe=terminate") spec.crashProbe=CrashProbeMode::Terminate;
        else if(arg=="--crash-probe=signal") spec.crashProbe=CrashProbeMode::Signal;
        else if(arg=="--reset-layout") spec.resetLayout=true;
        else if(arg=="--exit-after-screenshot") spec.exitAfterScreenshot=true;
        else if(arg=="--trace-events") { spec.traceEvents=true; spec.traceEventsOverride=true; }
        else if(arg=="--log-file") { spec.logToFile=true; spec.logToFileOverride=true; }
        else if(arg.starts_with("--log-file=")) {
            const auto path=arg.substr(11);
            if(path.empty()) throw std::invalid_argument("--log-file= requires a non-empty path");
            spec.logFilePath=Path::FromResolved(std::filesystem::u8path(path));
            spec.logFilePathOverride=spec.logFilePath; spec.logToFile=true; spec.logToFileOverride=true;
        }
        else if(arg.starts_with("--log-level=")) {
            const auto level=arg.substr(12);
            if(level=="trace") spec.logLevel=LogLevel::Trace;
            else if(level=="debug") spec.logLevel=LogLevel::Debug;
            else if(level=="info") spec.logLevel=LogLevel::Info;
            else if(level=="warn" || level=="warning") spec.logLevel=LogLevel::Warn;
            else if(level=="error") spec.logLevel=LogLevel::Error;
            else if(level=="critical") spec.logLevel=LogLevel::Critical;
            else throw std::invalid_argument("Unknown log level. Choose trace, debug, info, warn, error or critical");
            spec.logLevelOverride=spec.logLevel;
        }
        else if(arg=="--screenshot-after-frames") {
            frameCountSpecified=true;
            const auto text=value(); unsigned int frames=0;
            const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),frames);
            if(ec!=std::errc{} || end!=text.data()+text.size() || frames==0) throw std::invalid_argument("--screenshot-after-frames requires a positive integer");
            spec.screenshotAfterFrames=frames;
        }
        else if(arg=="--open" || arg=="--screenshot" || arg=="--export-diagnostics") {
            auto path=Path::FromResolved(std::filesystem::u8path(value()));
            if(arg=="--open") spec.openPath=std::move(path);
            else if(arg=="--export-diagnostics") spec.diagnosticExportPath=std::move(path);
            else {
                if(path.Native().extension()!=L".png") throw std::invalid_argument("--screenshot requires a .png path");
                spec.screenshotPath=std::move(path);
            }
        }
        else throw std::invalid_argument("Unknown argument: "+std::string(arg));
    }
    if((spec.exitAfterScreenshot || frameCountSpecified) && spec.screenshotPath.Empty()) throw std::invalid_argument("Screenshot controls require --screenshot <path.png>");
    const int actions=static_cast<int>(spec.showHelp)+static_cast<int>(spec.showVersion)+static_cast<int>(spec.crashProbe.has_value())+static_cast<int>(!spec.diagnosticExportPath.Empty());
    if(actions>1) throw std::invalid_argument("Choose only one of --help, --version, --export-diagnostics or --crash-probe");
    if(actions && (!spec.openPath.Empty() || !spec.screenshotPath.Empty() || spec.resetLayout)) throw std::invalid_argument("Headless actions cannot be combined with UI actions");
    return spec;
}
}
std::optional<int> HandleEarlyApplicationArguments(int argc, char** argv) {
    try {
        if(argc>1 && argv && argv[1] && std::string_view(argv[1])=="--sst-minidump") return RunMinidumpHelper(argc,argv);
        const auto spec=ApplicationDetail::ParseApplicationArguments(argc,argv);
        if(spec.showHelp) { std::cout<<Usage; return 0; }
        if(spec.showVersion) { std::cout<<BuildDescription()<<'\n'; return 0; }
        if(!spec.diagnosticExportPath.Empty()) {
            const auto result=ExportDiagnosticsHeadless({spec.diagnosticExportPath,DiagnosticPreview({})});
            if(!result) { std::cerr<<result.Error().userMessage<<' '<<result.Error().technicalDetails<<". Check the destination and free space.\n"; return 1; }
            return 0;
        }
        if(spec.crashProbe) {
            InitializeLocalDiagnostics();
            ApplicationDetail::InstallCrashFallbackHandlers();
            if(*spec.crashProbe==CrashProbeMode::Wait) {
                std::cout<<"Crash probe ready; terminate this process to test interrupted-session recovery.\n"<<std::flush;
                for(;;) std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            RunCrashProbe(*spec.crashProbe);
        }
        return std::nullopt;
    } catch(const std::invalid_argument& error) { std::cerr<<"Invalid arguments: "<<error.what()<<". Use --help for supported options.\n"<<Usage; return 2; }
      catch(const std::exception& error) { std::cerr<<"Startup action failed: "<<error.what()<<". Check diagnostics directory permissions and retry.\n"; return 1; }
}
int RunApplication(int argc, char** argv, Application::ApplicationSetup setup) {
    if(const auto early=HandleEarlyApplicationArguments(argc,argv)) return *early;
    try {
        auto app=Application::Create(argc,argv,std::move(setup));
        return app.Run();
    } catch(const std::exception& error) { std::cerr<<"Application startup failed: "<<error.what()<<". Review local diagnostics and retry.\n"; return 1; }
}
}
