#include <gtest/gtest.h>
#include "Core/Platform/NativeFileDialog.hpp"
#include <nfd.h>
#include <stdexcept>
#include <string>

// Supply the NFD backend at link time; tests never open operating-system dialogs.
namespace
{
    nfdresult_t initResult = NFD_OKAY, dialogResult = NFD_OKAY;
    bool countFails = false, pathFails = false;
    int quitCount = 0, freedSets = 0, freedPaths = 0;
    std::string defaultDirectory;
    char polishPath[] = "C:/Za\u017c\u00f3\u0142\u0107/g\u0119\u015bl\u0105.jfm.yaml";
    nfdresult_t Single(nfdu8char_t **out, const nfdu8char_t *directory)
    {
        defaultDirectory = directory ? directory : "";
        *out = polishPath;
        return dialogResult;
    }
    nfdresult_t Multiple(const nfdpathset_t **out, const nfdu8char_t *directory)
    {
        defaultDirectory = directory ? directory : "";
        *out = reinterpret_cast<const nfdpathset_t *>(1);
        return dialogResult;
    }
    class NativeFileDialogTests : public testing::Test
    {
        void SetUp() override
        {
            initResult = dialogResult = NFD_OKAY;
            countFails = pathFails = false;
            quitCount = freedSets = freedPaths = 0;
        }
    };
}
extern "C"
{
    nfdresult_t NFD_Init() { return initResult; }
    void NFD_Quit() { ++quitCount; }
    const char *NFD_GetError() { return "injected NFD failure"; }
    void NFD_FreePathU8(nfdu8char_t *) { ++freedPaths; }
    void NFD_PathSet_FreePathU8(const nfdu8char_t *) { ++freedPaths; }
    void NFD_PathSet_Free(const nfdpathset_t *) { ++freedSets; }
    nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t *, nfdpathsetsize_t *count)
    { *count = 2; return countFails ? NFD_ERROR : NFD_OKAY; }
    nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t *, nfdpathsetsize_t index, nfdu8char_t **out)
    { *out = polishPath; return pathFails && index == 1 ? NFD_ERROR : NFD_OKAY; }
    nfdresult_t NFD_OpenDialogU8(nfdu8char_t **out, const nfdu8filteritem_t *, nfdfiltersize_t, const nfdu8char_t *directory)
    { return Single(out, directory); }
    nfdresult_t NFD_SaveDialogU8(nfdu8char_t **out, const nfdu8filteritem_t *, nfdfiltersize_t, const nfdu8char_t *directory, const nfdu8char_t *)
    { return Single(out, directory); }
    nfdresult_t NFD_PickFolderU8(nfdu8char_t **out, const nfdu8char_t *directory)
    { return Single(out, directory); }
    nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t **out, const nfdu8filteritem_t *, nfdfiltersize_t, const nfdu8char_t *directory)
    { return Multiple(out, directory); }
    nfdresult_t NFD_PickFolderMultipleU8(const nfdpathset_t **out, const nfdu8char_t *directory)
    { return Multiple(out, directory); }
}
TEST_F(NativeFileDialogTests, CancellationIsEmptyAndErrorsThrowForEveryDialog)
{
    for (auto result : {NFD_CANCEL, NFD_ERROR})
    {
        dialogResult = result;
        if (result == NFD_CANCEL)
        {
            EXPECT_FALSE(SST::OpenFileDialog({}));
            EXPECT_FALSE(SST::SaveFileDialog({}, "project"));
            EXPECT_TRUE(SST::OpenFilesDialog({}).empty());
            EXPECT_TRUE(SST::PickFoldersDialog(false).empty());
            EXPECT_TRUE(SST::PickFoldersDialog(true).empty());
        }
        else
        {
            EXPECT_THROW(SST::OpenFileDialog({}), std::runtime_error);
            EXPECT_THROW(SST::SaveFileDialog({}, "project"), std::runtime_error);
            EXPECT_THROW(SST::OpenFilesDialog({}), std::runtime_error);
            EXPECT_THROW(SST::PickFoldersDialog(false), std::runtime_error);
            EXPECT_THROW(SST::PickFoldersDialog(true), std::runtime_error);
        }
    }
    EXPECT_EQ(quitCount, 10);
}
TEST_F(NativeFileDialogTests, InitializationFailureDoesNotQuitUninitializedBackend)
{
    initResult = NFD_ERROR;
    EXPECT_THROW(SST::OpenFileDialog({}), std::runtime_error);
    EXPECT_THROW(SST::SaveFileDialog({}, "project"), std::runtime_error);
    EXPECT_THROW(SST::OpenFilesDialog({}), std::runtime_error);
    EXPECT_THROW(SST::PickFoldersDialog(false), std::runtime_error);
    EXPECT_THROW(SST::PickFoldersDialog(true), std::runtime_error);
    EXPECT_EQ(quitCount, 0);
}
TEST_F(NativeFileDialogTests, PathSetFailureThrowsAndReleasesSetWithoutPartialResults)
{
    countFails = true;
    EXPECT_THROW(SST::OpenFilesDialog({}), std::runtime_error);
    EXPECT_THROW(SST::PickFoldersDialog(true), std::runtime_error);
    countFails = false;
    pathFails = true;
    EXPECT_THROW(SST::OpenFilesDialog({}), std::runtime_error);
    EXPECT_THROW(SST::PickFoldersDialog(true), std::runtime_error);
    EXPECT_EQ(freedSets, 4);
    EXPECT_EQ(freedPaths, 2);
}
TEST_F(NativeFileDialogTests, PolishPathsRoundtripThroughUtf8ForEveryDialog)
{
    const auto expected = SST::Path::FromResolved(std::filesystem::u8path(polishPath));
    EXPECT_EQ(SST::OpenFileDialog({}, expected), expected);
    EXPECT_EQ(defaultDirectory, polishPath);
    EXPECT_EQ(SST::SaveFileDialog({}, "project", expected), expected);
    EXPECT_EQ(defaultDirectory, polishPath);
    EXPECT_EQ(SST::OpenFilesDialog({}, expected), (std::vector<SST::Path>{expected, expected}));
    EXPECT_EQ(defaultDirectory, polishPath);
    EXPECT_EQ(SST::PickFoldersDialog(false, expected), (std::vector<SST::Path>{expected}));
    EXPECT_EQ(defaultDirectory, polishPath);
    EXPECT_EQ(SST::PickFoldersDialog(true, expected), (std::vector<SST::Path>{expected, expected}));
    EXPECT_EQ(defaultDirectory, polishPath);
    EXPECT_EQ(freedSets, 2);
    EXPECT_EQ(freedPaths, 7);
}
