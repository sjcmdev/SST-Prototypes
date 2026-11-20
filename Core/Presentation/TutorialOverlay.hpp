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
    bool Draw(Tutorial &tutorial, HostedWindows *windows = nullptr); // Returns true when the user presses Exit. Back / Skip / Show me act on the tutorial directly.
    bool Highlighted() const { return highlighted; }
    // Last drawn geometry (screen space): the target widget, the coach window and the arrow tip.
    struct Geometry { bool arrow = false; ImVec2 tip, targetMin, targetMax, coachMin, coachMax; };
    const Geometry &LastGeometry() const { return geometry; }
private:
    struct Target { ImVec2 min, max, rowMin, rowMax; int frame; };
    std::map<std::string, Target> targets;
    bool highlighted = false;
    Geometry geometry;
    bool reposition = false;
    std::string placedStep;
    HostedEditorWindow coach{"Tutorial coach###sst-tutorial", true, true};
};
}
