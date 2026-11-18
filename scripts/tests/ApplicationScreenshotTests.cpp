#include <gtest/gtest.h>
#include "Core/App/ApplicationInternals.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#include "Vendor/Tracy/test/stb_image.h"

namespace SST
{
    static ApplicationSpecification Parse(std::vector<std::string> arguments)
    {
        std::vector<char *> argv;
        for (auto &argument : arguments) argv.push_back(argument.data());
        return ApplicationDetail::ParseApplicationArguments(static_cast<int>(argv.size()), argv.data());
    }

    TEST(ApplicationArguments, ScreenshotDefaultsAndPaths)
    {
        const auto defaults = Parse({"app"});
        EXPECT_EQ(defaults.screenshotAfterFrames, 30u);
        EXPECT_TRUE(defaults.screenshotPath.Empty());
        EXPECT_FALSE(defaults.exitAfterScreenshot);
        const auto spec = Parse({"app", "--screenshot", "folder with spaces/test.png", "--screenshot-after-frames", "7",
                                 "--exit-after-screenshot", "--open", "project with spaces.yaml", "--reset-layout", "--trace-events"});
        EXPECT_EQ(spec.screenshotPath.Native(), std::filesystem::path("folder with spaces/test.png"));
        EXPECT_EQ(spec.openPath.Native(), std::filesystem::path("project with spaces.yaml"));
        EXPECT_EQ(spec.screenshotAfterFrames, 7u);
        EXPECT_TRUE(spec.exitAfterScreenshot);
        EXPECT_TRUE(spec.resetLayout);
        EXPECT_TRUE(spec.traceEvents);
        EXPECT_EQ(Parse({"app", "--screenshot-after-frames", "1"}).screenshotAfterFrames, 1u);
    }

    TEST(ApplicationArguments, RejectsInvalidScreenshotArguments)
    {
        for (const auto &option : {"--screenshot", "--open", "--screenshot-after-frames"})
        {
            EXPECT_THROW(Parse({"app", option}), std::invalid_argument);
            EXPECT_THROW(Parse({"app", option, "--exit-after-screenshot"}), std::invalid_argument);
            EXPECT_THROW(Parse({"app", option, ""}), std::invalid_argument);
        }
        for (const auto &value : {"0", "-1", "2x", "1.5", "4294967296"})
            EXPECT_THROW(Parse({"app", "--screenshot-after-frames", value}), std::invalid_argument);
        EXPECT_THROW(Parse({"app", "--screenshot", "out.jpg"}), std::invalid_argument);
        EXPECT_EQ(ApplicationDetail::ParseApplicationArguments(0, nullptr).screenshotAfterFrames, 30u);
    }

    TEST(ApplicationArguments, OpenUsesRegisteredPathHandlerAndReportsMissingHandler)
    {
        class OpenCommand final : public ICommand
        {
        public:
            explicit OpenCommand(Path &received) : received(received) {}
            Result<void> Execute(CommandContext &context) override
            {
                received = *context.TryGet<Path>("path");
                return {};
            }
            std::string Description() const override { return "Open fixture"; }
            Path &received;
        };
        CommandRegistry commands;
        const Path input = Path::FromResolved("folder with spaces/project.yaml");
        auto missing = ApplicationDetail::OpenStartupFile(commands, input);
        ASSERT_FALSE(missing);
        EXPECT_EQ(missing.Error().code, "app.open.no_handler");
        Path received;
        ASSERT_TRUE(commands.Register({CommandID{"jfm.project.open"}, "Open"},
            [&](CommandContext &) { return CreateUnique<OpenCommand>(received); }));
        auto opened = ApplicationDetail::OpenStartupFile(commands, input);
        ASSERT_TRUE(opened);
        EXPECT_EQ(opened.Value().id.value, "jfm.project.open");
        EXPECT_EQ(received, input);
        ASSERT_TRUE(commands.Register({CommandID{"app.open"}, "Open"},
            [&](CommandContext &) { return CreateUnique<OpenCommand>(received); }));
        EXPECT_EQ(ApplicationDetail::OpenStartupFile(commands, input).Value().id.value, "app.open");
        EXPECT_EQ(ApplicationDetail::OpenStartupFile(commands, {}).Error().code, "app.open.invalid_path");
    }

    TEST(ApplicationScreenshot, HeadlessPythonReturnsTypedError)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        PythonInterpreter interpreter;
        ASSERT_TRUE(interpreter.Start());
        PythonGilAcquireScope gil;
        ASSERT_TRUE(gil.IsActive());
        ASSERT_EQ(PyRun_SimpleString(
            "import json, sst_application\n"
            "r = json.loads(sst_application.execute(json.dumps({'command_id':'app.screenshot','path_png':'test.png'})))\n"
            "assert not r['ok'] and r['error']['code'] == 'app.screenshot.no_gl_context'\n"
            "r = json.loads(sst_application.execute(json.dumps({'command_id':'app.open','path':'project.caplab'})))\n"
            "assert not r['ok'] and r['error']['code'] == 'app.runtime.unavailable'\n"
            "assert json.loads(sst_application.execute('{}'))['error']['code'] == 'app.request.invalid'\n"), 0);
#else
        GTEST_SKIP() << "Embedded Python unavailable; JSON binding cannot be exercised.";
#endif
    }

    TEST(ApplicationScreenshot, HiddenContextWritesFlippedPngAndRestoresGlState)
    {
        Threading::SetMainThread();
        if (!glfwInit()) GTEST_SKIP() << "No GLFW display/GL context available in this environment.";
        struct GlfwGuard { ~GlfwGuard() { glfwTerminate(); } } glfwGuard;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        auto *window = glfwCreateWindow(16, 16, "Screenshot test", nullptr, nullptr);
        if (!window) GTEST_SKIP() << "Cannot create a hidden OpenGL context in this environment.";
        struct WindowGuard { GLFWwindow *window; ~WindowGuard() { glfwDestroyWindow(window); } } windowGuard{window};
        glfwMakeContextCurrent(window);
        if (!gladLoadGL(glfwGetProcAddress)) GTEST_SKIP() << "Cannot load OpenGL entry points.";
        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, framebufferHeight / 2, framebufferWidth, framebufferHeight - framebufferHeight / 2);
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glPixelStorei(GL_PACK_ROW_LENGTH, 21);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        const auto path = std::filesystem::temp_directory_path() / "sst-framebuffer-test.png";
        struct FileGuard { std::filesystem::path path; ~FileGuard() { std::error_code ec; std::filesystem::remove(path, ec); } } fileGuard{path};
        auto registry = CreateRef<CommandRegistry>();
        auto jobs = CreateRef<JobSystem>(nullptr, 1);
        ApplicationDetail::RegisterScreenshotCommand(registry, jobs);
        CommandContext context;
        context.Set("path", Path::FromResolved(path));
        const auto result = registry->Execute(CommandID{"app.screenshot"}, context);
        ASSERT_TRUE(result) << result.Error().technicalDetails;
        ASSERT_GT(std::filesystem::file_size(path), 8u);
        GLint rowLength, skipRows, skipPixels;
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
        EXPECT_EQ(rowLength, 21); EXPECT_EQ(skipRows, 2); EXPECT_EQ(skipPixels, 3);
        int width, height, channels;
        auto *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(width, framebufferWidth); EXPECT_EQ(height, framebufferHeight);
        EXPECT_EQ(pixels[0], 0); EXPECT_EQ(pixels[2], 255);
        EXPECT_EQ(pixels[(height - 1) * width * 4], 255);
        EXPECT_EQ(pixels[(height - 1) * width * 4 + 2], 0);
        stbi_image_free(pixels);
        context.Set("path", Path::FromResolved(path.parent_path() / "missing-sst-dir" / "out.png"));
        const auto failed = registry->Execute(CommandID{"app.screenshot"}, context);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.Error().code, "app.screenshot.write_failed");
    }
}
