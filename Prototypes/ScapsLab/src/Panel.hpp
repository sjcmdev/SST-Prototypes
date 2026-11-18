#pragma once
#include "Workflow.hpp"
#include "Core/Presentation/Panels/EditorWindow.hpp"
namespace SST::ScapsLab {
class Panel final : public EditorWindow {
public:
    Panel(Workflow *workflow, int view, bool gallery=false);
    void RenderContent() override;
private:
    Workflow *w;
    int view, selected=0, bias=0;
    bool absoluteLog=false, placed=false, gallery=false;
    char file[1024]{};
    void Send(const Json &request);
    void Results();
};
}

