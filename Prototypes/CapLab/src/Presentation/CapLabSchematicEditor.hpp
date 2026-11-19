// Adapted from CircuitCore-UI SchematicEditor for CapLab presentation.
#pragma once
#include "CircuitDocument.h"
#include "imgui.h"
#include <map>
#include <string>
namespace ccui {
class CapLabSchematicEditor {
public:
    std::map<std::string,double> uncertainties;
    bool draw(CircuitDocument& document);
    bool properties(CircuitDocument& document);
    void reset() { selected=-1; terminal=-1; pending=-1; openValue=false; fitPending=true; }
private:
    enum class Tool { Select, Wire, Ground, Positive, Negative };
    Tool tool=Tool::Select;
    int selected=-1, terminal=-1, pending=-1, palette=-1;
    bool fitPending=true, openValue=false;
    float zoom=1;
    ImVec2 pan{20,20}, moveMouse{};
    Point moveStart{};
};
}
