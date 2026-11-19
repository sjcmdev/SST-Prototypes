#include <gtest/gtest.h>
#include "IO/JFMImportSources.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <chrono>

namespace SST::Prototypes::JFM
{
    TEST(JFMImportSources, SharedCollectorPreservesSeriesFolderTreeAndExtensions)
    {
        const Path root{FileSystem::TempDirectoryPath()/("jfm-clshare2-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))};
        struct Cleanup { Path root; ~Cleanup() {FileSystem::RemoveAll(root.Native());} } cleanup{root};
        ASSERT_TRUE(FileSystem::CreateDirectories(root.Join("group/series").Native()));
        std::string error;
        const auto top=root.Join("a.DAT"), nested=root.Join("group/series/b.iv");
        ASSERT_TRUE(TextFileIO::Save(top,"test",error)); ASSERT_TRUE(TextFileIO::Save(nested,"test",error));
        ASSERT_TRUE(TextFileIO::Save(root.Join("ignored.bin"),"test",error));
        auto flat=BuildImportGroups({root},false,JFMImportPlacement::NewSeries,{});
        ASSERT_EQ(flat.size(),1); EXPECT_EQ(flat[0].paths,std::vector<Path>{top});
        auto tree=BuildImportGroups({root},false,JFMImportPlacement::PreserveFolderTree,{});
        ASSERT_EQ(tree.size(),2);
        EXPECT_EQ(tree[1].seriesName,"series"); EXPECT_EQ(tree[1].paths,std::vector<Path>{nested});
        EXPECT_EQ(tree[1].groupPath,(std::vector<std::string>{root.filename().Utf8(),"group"}));
        auto existing=BuildImportGroups({root},true,JFMImportPlacement::ExistingSeries,"existing");
        ASSERT_EQ(existing.size(),2);
        for (const auto &group : existing) { EXPECT_EQ(group.targetSeriesId,"existing"); EXPECT_TRUE(group.groupPath.empty()); }
        EXPECT_EQ(CollectImportSourcePaths({root,nested},true).size(),2);
        std::vector<std::string> warnings;
        EXPECT_TRUE(BuildImportGroups({root.Join("missing-folder")},true,JFMImportPlacement::NewSeries,{},&warnings).empty());
        EXPECT_FALSE(warnings.empty());
        // Explicit missing data files still reach the parser, preserving JFM's per-file error policy.
        auto missing=BuildImportGroups({root.Join("missing.dat")},false,JFMImportPlacement::NewSeries,{});
        ASSERT_EQ(missing.size(),1); EXPECT_EQ(missing[0].paths.size(),1);
    }
}
