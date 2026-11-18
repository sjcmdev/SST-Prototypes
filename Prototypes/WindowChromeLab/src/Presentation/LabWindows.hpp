#pragma once
#include "Presentation/Panels/EditorWindow.hpp"
#include <vector>

namespace SST::Prototypes::WindowChromeLab
{
    struct LabSelection { WindowId selected = 0; };
    class LabWindow final : public EditorWindow
    {
    public:
        enum class Kind { Plot, Table, Tools };
        LabWindow(Kind kind, Ref<LabSelection> selection);
        WindowDuplicate Duplicate() const override { return {CreateRef<LabWindow>(*this), {}}; }
        bool IsDirty() const override { return m_Dirty; }
    protected:
        void RenderContent() override;
        void RenderContextMenu() override;
        void RevealInProject() override { m_Selection->selected = GetId(); }
        std::string SaveViewState() const override;
        bool LoadViewState(const std::string &text) override;
    private:
        Kind m_Kind;
        Ref<LabSelection> m_Selection;
        std::vector<float> m_Samples{0, 1, 4, 9, 16, 25, 36, 49};
        float m_Scale = 1;
        bool m_Dirty = false;
    };
}
