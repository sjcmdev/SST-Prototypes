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
        std::any CaptureViewState() const override { return m_Scale; }
        bool RestoreViewState(const std::any &state) override;
        std::string EncodeViewState(const std::any &state) const override;
        std::any DecodeViewState(const std::string &text) const override;
    private:
        Kind m_Kind;
        Ref<LabSelection> m_Selection;
        std::vector<float> m_Samples{0, 1, 4, 9, 16, 25, 36, 49};
        float m_Scale = 1;
        bool m_Dirty = false;
    };
}
