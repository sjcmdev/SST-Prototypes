#pragma once
#include "Workflow.hpp"
#include "Core/Presentation/Panels/EditorWindow.hpp"
namespace SST::ScapsLab {
class Panel final : public EditorWindow {
public:
    Panel(Workflow *workflow, int view, bool gallery=false);
    void RenderContent() override;
    Workflow *GetWorkflow() const {return w;}
private:
    Workflow *w;
    int view;
    bool absoluteLog=false, placed=false, gallery=false;
    char file[1024]{}, csvFile[1024]="scapslab-ac.csv";
    Json acRequest;
    void AC();
    bool Defects(Json &draft,bool interface);
    bool Structure(Json &draft);
    void Preview(bool interface);
    int selectedDefect=0;
    void Send(const Json &request);
    void Results();
};
}

