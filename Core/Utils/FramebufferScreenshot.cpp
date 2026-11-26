#include "Core/dspch.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/Diagnostics/DiagnosticsCommand.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <future>
#include <limits>
#include <string_view>
#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "Vendor/GLFW/deps/stb_image_write.h"

#if DS_PYTHON_CAPI_AVAILABLE
#ifndef NB_STATIC
#define NB_STATIC
#endif
#include "Vendor/nanobind/include/nanobind/nanobind.h"
#include "Vendor/nanobind/include/nanobind/stl/string.h"
#endif

namespace SST::ApplicationDetail
{
    namespace
    {
        WeakRef<CommandRegistry> pythonCommands;

        StructuredError ScreenshotError(const std::string &code, const std::string &details)
        {
            return {ErrorCategory::IO, Severity::Error, "Screenshot failed.", details,
                    "Capture a rendered main window to a writable .png path.", "Application", code};
        }

        class WritePngJob final : public IJob
        {
        public:
            Path path;
            int width = 0, height = 0;
            std::vector<unsigned char> pixels;
            std::promise<void> completion;
            std::string GetName() const override { return "Write screenshot"; }
            std::string GetType() const override { return "app.screenshot.write_png"; }
            void Execute(JobContext &) override
            {
                try
                {
                    std::ofstream output(path.Native(), std::ios::binary | std::ios::trunc);
                    if (!output) throw std::runtime_error("Cannot open screenshot: " + path.Utf8());
                    const auto write = [](void *context, void *data, int size) {
                        static_cast<std::ofstream *>(context)->write(static_cast<const char *>(data), size);
                    };
                    const int encoded = stbi_write_png_to_func(write, &output, width, height, 4, pixels.data(), width * 4);
                    output.flush();
                    if (!encoded || !output) throw std::runtime_error("Cannot write screenshot: " + path.Utf8());
                    completion.set_value();
                }
                catch (...)
                {
                    completion.set_exception(std::current_exception());
                    throw;
                }
            }
        };

        class ScreenshotCommand final : public ICommand
        {
        public:
            explicit ScreenshotCommand(Ref<JobSystem> jobs) : jobs(std::move(jobs)) {}
            std::string Description() const override { return "Capture the main window framebuffer"; }
            Result<void> Execute(CommandContext &context) override
            {
                const auto *path = context.TryGet<Path>("path");
                if (!path || path->Empty() || path->Native().extension() != ".png")
                    return ScreenshotError("app.screenshot.invalid_path", "Expected a .png path.");
                if (!Threading::IsMainThread() || !glfwGetCurrentContext() || !glReadPixels)
                    return ScreenshotError("app.screenshot.no_gl_context", "A current main-thread OpenGL context is required.");
                try
                {
                    int width = 0, height = 0;
                    glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
                    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4 ||
                        static_cast<size_t>(width) * height > std::numeric_limits<size_t>::max() / 4)
                        return ScreenshotError("app.screenshot.invalid_size", "The main framebuffer is empty or too large.");
                    auto job = CreateRef<WritePngJob>();
                    job->path = *path;
                    job->width = width;
                    job->height = height;
                    job->pixels.resize(static_cast<size_t>(width) * height * 4);
                    GLint framebuffer = 0, readBuffer = 0, packBuffer = 0, alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
                    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
                    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
                    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
                    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
                    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
                    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
                    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                    glReadBuffer(GL_BACK);
                    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, job->pixels.data());
                    const auto error = glGetError();
                    glReadBuffer(readBuffer);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
                    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
                    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
                    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
                    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
                    if (error != GL_NO_ERROR)
                        return ScreenshotError("app.screenshot.readback_failed", "glReadPixels failed: " + std::to_string(error));
                    const size_t stride = static_cast<size_t>(width) * 4;
                    for (int y = 0; y < height / 2; ++y)
                        std::swap_ranges(job->pixels.begin() + y * stride, job->pixels.begin() + (y + 1) * stride,
                                         job->pixels.begin() + (height - y - 1) * stride);
                    auto completion = job->completion.get_future();
                    if (jobs->Submit(job) == 0)
                        return ScreenshotError("app.screenshot.submit_failed", "PNG job was rejected.");
                    completion.get();
                    return {};
                }
                catch (const std::exception &error)
                {
                    return ScreenshotError("app.screenshot.write_failed", error.what());
                }
            }
        private:
            Ref<JobSystem> jobs;
        };
    }

    void RegisterScreenshotCommand(const Ref<CommandRegistry> &commands, const Ref<JobSystem> &jobs)
    {
        RegisterDiagnosticsCommand(commands,jobs);
        auto result = commands->Register({CommandID{"app.screenshot"}, "Screenshot", "Application", "Capture the main framebuffer to PNG.", {},
                                         CommandFlags::HiddenFromPalette},
                                        [jobs](CommandContext &) { return CreateUnique<ScreenshotCommand>(jobs); });
        if (!result) throw std::runtime_error(result.Error().technicalDetails);
        pythonCommands = commands;
    }

    Result<CommandOutcome> OpenStartupFile(CommandRegistry &commands, const Path &path)
    {
        if (path.Empty())
            return StructuredError{ErrorCategory::Validation, Severity::Error, "File was not opened.",
                "Expected a project or data path.", "Supply a nonempty path.", "Application", "app.open.invalid_path"};
        // Apps may register app.open; retain existing path-aware project commands.
        for (const char *id : {"app.open", "caplab.open", "jfm.project.open", "scapslab.load"})
        {
            if (!commands.HasCommand(CommandID{id})) continue;
            CommandContext context;
            context.SetSource("Application --open / Python");
            context.Set("path", path);
            if (std::string_view(id) == "scapslab.load")
                context.Set("request", nlohmann::json{{"operation", "load"}, {"path", path.Utf8()}});
            return commands.Execute(CommandID{id}, std::move(context));
        }
        return StructuredError{ErrorCategory::Capability, Severity::Warning,
            "Startup file was not opened.", "App has no path-aware open handler.",
            "Register app.open with a path context value.", "Application", "app.open.no_handler"};
    }
}

#if DS_PYTHON_CAPI_AVAILABLE
static std::string ExecuteApplicationRequest(const char *requestJson)
{
        using namespace SST;
        using nlohmann::json;
        try
        {
            const auto request = json::parse(requestJson);
            const auto id = request.at("command_id").get<std::string>();
            if (id != "app.screenshot" && id != "app.open" && id != "app.layout.reset")
                return json{{"ok", false}, {"error", {{"code", "app.command.unknown"}}}}.dump();
            const auto path = id == "app.layout.reset" ? std::string{} : request.at(id == "app.open" ? "path" : "path_png").get<std::string>();
            auto commands = ApplicationDetail::pythonCommands.lock();
            if (!commands || !Threading::IsMainThread())
                return json{{"ok", false}, {"error", {{"code", id == "app.screenshot" ? "app.screenshot.no_gl_context" : "app.runtime.unavailable"},
                    {"message", "A running application on its main thread is required."}}}}.dump();
            if (id == "app.open")
            {
                const auto result = ApplicationDetail::OpenStartupFile(*commands, Path::FromResolved(std::filesystem::u8path(path)));
                if (!result)
                    return json{{"ok", false}, {"error", {{"code", result.Error().code}, {"message", result.Error().technicalDetails}}}}.dump();
                return json{{"ok", true}, {"command_id", result.Value().id.value}, {"path", path}}.dump();
            }
            CommandContext context;
            if (id == "app.screenshot") context.Set("path", Path::FromResolved(std::filesystem::u8path(path)));
            context.SetSource("Python");
            auto result = commands->Execute(CommandID{id}, std::move(context));
            if (!result)
                return json{{"ok", false}, {"error", {{"code", result.Error().code}, {"message", result.Error().technicalDetails}}}}.dump();
            json response{{"ok", true}, {"command_id", id}};
            if (id == "app.screenshot") response["path_png"] = path;
            return response.dump();
        }
        catch (const std::exception &error)
        {
            return json{{"ok", false}, {"error", {{"code", "app.request.invalid"}, {"message", error.what()}}}}.dump();
        }
}
NB_MODULE(sst_application, module)
{
    module.def("version_info", [] {
        const auto& b=SST::GetBuildInfo(); nanobind::dict info;
        info["version"]=nanobind::str(b.version.data(),b.version.size());
        info["commit"]=nanobind::str(b.revision.data(),b.revision.size());
        info["build_utc"]=nanobind::str(b.builtUtc.data(),b.builtUtc.size());
        info["source_sha256"]=nanobind::str(b.sourceSha256.data(),b.sourceSha256.size());
        info["dependency_sha256"]=nanobind::str(b.dependencySha256.data(),b.dependencySha256.size());
        info["toolchain"]=nanobind::str(b.toolchain.data(),b.toolchain.size());
        info["configuration"]=nanobind::str(b.configuration.data(),b.configuration.size());
        info["python_available"]=b.pythonAvailable;
        return info;
    });
    module.def("export_diagnostics", [](const std::string& destination) {
        SST::DiagnosticExport request{SST::Path::FromResolved(std::filesystem::u8path(destination)),SST::DiagnosticPreview({})};
        auto commands=SST::ApplicationDetail::pythonCommands.lock();
        if(commands && !SST::Threading::IsMainThread()) {
            nanobind::dict error;error["code"]="diagnostics.main_thread_required";error["message"]="Export from the application's main thread or a headless host.";
            nanobind::dict response;response["ok"]=false;response["error"]=error;return response;
        }
        auto result=commands ? SST::ExecuteDiagnosticsExport(*commands,std::move(request)) : SST::ExportDiagnosticsHeadless(std::move(request));
        nanobind::dict response; response["ok"]=result.HasValue();
        if(!result) {
            nanobind::dict error;
            error["code"]=nanobind::str(result.Error().code.c_str());
            error["message"]=nanobind::str(result.Error().userMessage.c_str());
            response["error"]=error;
        }
        return response;
    },nanobind::arg("destination_path"));
    module.def("execute", [](const char *requestJson) {
        return nanobind::str(ExecuteApplicationRequest(requestJson).c_str());
    }, nanobind::arg("request_json"));
}
#endif

namespace SST::ApplicationDetail
{
    void RegisterApplicationPythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static const bool registered = PyImport_AppendInittab("sst_application", PyInit_sst_application) == 0;
        if (!registered) throw std::runtime_error("Cannot register sst_application.");
#endif
    }
}
