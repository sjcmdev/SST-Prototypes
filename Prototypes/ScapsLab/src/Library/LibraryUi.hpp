#pragma once
#include "Library/Session.hpp"
#include <imgui.h>
namespace SST::ScapsLab {class Workflow;}
namespace SST::ScapsLab::Library {
struct EditValue {Assets::Metadata metadata;std::shared_ptr<const Payload> payload;};
struct EditBuffer {
    RevisionRef source;
    EditValue value;
    scaps_core::DeviceProject carrier;
    std::map<std::string,double> numbers;
    Preview preview,reference;
    UndoStack undo;
    std::optional<EditValue> gesture;
    bool dirty=false;
    std::string diagnostic;
    unsigned previewBuilds=0;
    void Reset(RevisionRef);
    void Rebuild();
};
class Editor {
public:
    void Draw(Workflow&);
    unsigned PreviewBuilds() const;
private:
    std::map<std::string,std::shared_ptr<EditBuffer>> buffers;
    std::map<const Revision*,Preview> thumbnails;
    std::shared_ptr<const State> source;
    unsigned epoch=~0u;
    std::string importKind="material",profileTarget="Na_m3",destination="user",deriveId="derived-item",newLibraryId="team",newLibraryName="Team library";
    std::string exportPath,importPath;
    std::filesystem::path usageFolder;
    bool mountReadOnly=false;
    int compareVersion=0;
    const Revision* diffSource=nullptr;
    unsigned diffBuild=0;
    std::vector<Difference> differences;
    Assets::Filter lastFilter;
    std::string lastLibrary;
    std::vector<RevisionRef> filtered;
    RevisionRef selectedNotification;
    std::string newScope="project";
    std::string newRangePath,captureKind="material",captureName="New asset",captureId="new-asset";
    void Inspector(Workflow&,EditBuffer&,const std::string& library);
    void PreviewPlot(Workflow&,const EditBuffer&,const std::string& key,const RevisionRef& compare={});
};
struct AssetDrag {char library[129]{},item[129]{};std::uint32_t version=0;};
void DropTarget(Workflow&,const Target&);
void DrawDropChoice(Workflow&);
std::optional<bool> EditorCanStep(const Session&,bool undo);
std::optional<Result<void>> EditorStep(Session&,bool undo);
}
