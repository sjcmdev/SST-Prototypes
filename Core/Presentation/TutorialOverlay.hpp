#pragma once
#include "Tutorial.hpp"
#include <imgui.h>
#include <map>
#include "Core/Presentation/Panels/HostedEditorWindow.hpp"

namespace SST {
class HostedWindows;
class TutorialOverlay {
public:
    void Window(const std::string &id);
    void Item(const std::string &window, const std::string &id);
    bool Draw(const Tutorial &tutorial, HostedWindows *windows = nullptr); // Returns true when the user requests Stop.
    bool Highlighted() const { return highlighted; }
private:
    struct Target { ImVec2 min, max, rowMin, rowMax; int frame; };
    std::map<std::string, Target> targets;
    bool highlighted = false;
    bool reposition = false;
    std::string placedStep;
    HostedEditorWindow coach{"Tutorial coach###sst-tutorial", true, true};
};
}
