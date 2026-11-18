# W1 ? Window Chrome

Zaimplementowano wsp?ln? baz? `SST::EditorWindow` w Core/Presentation. `SST::Window` pozostaje natywnym oknem aplikacji. `IPanel` zachowuje zgodno?? dla paneli oczekuj?cych na migracj?. Nie wykonywano operacji git ani zmian w ?r?d?owych plikach Prototypes/JFM/install/app/config/*.yaml.

## Funkcje

| Funkcja | Wynik |
| --- | --- |
| Close / minimize / maximize / restore | Bazowy chrome; minimize do dolnego paska, maximize w g??wnym viewport aplikacji, restore zachowuje geometri? i dock slot. |
| Pin | Float + blokada pozycji/rozmiaru, przed innymi zwyk?ymi widokami; klasa viewport dostaje TopMost. Unpin przywraca dock slot. |
| Context menu | Na pasku tytu?u i zak?adce docka, tak?e nieaktywnej: short/long rename, duplicate, float, dock do aplikacji/istniej?cej grupy, close others/type, reveal. Aplikacja mo?e dopisa? w?asne pozycje. |
| Duplicate | Nowa instancja i monotoniczny ID; wsp?lny Core UndoStack. Fabryka mo?e do??czy? undoable engine command, odtwarzany razem z widokiem. |
| Gestures | Double-click tytu?u maksymalizuje/przywraca; middle-click zak?adki zamyka. |
| Dirty | Gwiazdka w tytule i potwierdzenie close/bulk close. Close ukrywa widok i zachowuje dane. |
| Window menu | Lista widok?w nowej bazy, cascade, tile horizontal/vertical, show minimized. |
| Input | Ctrl+Tab / Ctrl+Shift+Tab przez Core keymap; wy??czony konkurencyjny handler ImGui. |
| Nazwy i to?samo?? | ID niezale?ny od short/long name; stabilny ImGui ### suffix. |
| Lock / workspaces | Lock zachowuje istniej?ce dockowanie; nazwane workspaces przechowuj? layout/view options w istniej?cym INI. |
| Persistence | JSON stanu bez GUI + handler SSTWindows w layout INI. Template zapisuje JSON stanu z dokumentem. Stare po?o?enie INI migrowanych paneli jest importowane. |

## API aplikacji

Instrukcja: [Core/Presentation/WINDOWS.md](../../Core/Presentation/WINDOWS.md). Rejestracja przez `EditorLayer::RegisterPanel<T>()`; odtwarzanie jawnego ID przez `RegisterWindow(window, savedId)`; aplikacja rysuje `RenderContent()`, deklaruje capabilities i opcjonalnie implementuje factory/menu/reveal/dirty/view-state hooks. Akcje u?ytkownika przez `editor.GetWindows()->Execute(request)`. Dla engine-owned windows odtwarzanie projektu musi najpierw zarejestrowa? widoki z zapisanymi ID; layout nie duplikuje danych silnika.

Proof migration: PrototypeTemplate oraz JFMOverviewPanel i ImportStatusPanel. Tre?? paneli i wywo?ania JFMWorkspaceStore pozosta?y takie same. Demo WindowChromeLab wygenerowano skryptem scripts/generate_prototype.py; zawiera document, plot, table i Project Explorer. Lab sample vectors s? demonstracyjnymi danymi w pami?ci; layout ich nie nadpisuje, duplicate ma niezale?n? kopi?. Document menu u?ywa rzeczywistego IO template.

## Oddzielne punkty wycofania propozycji

Nie dodano flag konfiguracyjnych ani nowych zale?no?ci. Propozycje s? odseparowane przez wywo?ania/metody/sekcje; wymagane pola kontraktu architektonicznego pozostaj? wsp?lne.

| Propozycja | Punkt wycofania UI |
| --- | --- |
| Title/tab gestures | Blok `Proposal: title/tab gestures` w EditorWindow::Render. |
| Dirty marker / confirm | IsDirty oraz flagi/etykieta tytu?u, preflight Close w Apply i modal Unsaved window data w RenderOverlays. Domy?lne IsDirty zwraca false. |
| Window menu / arrangements | Jedno wywo?anie RenderMenu w EditorLayer; logika Cascade/Tile/ShowMinimized pozostaje niezale?na w Apply. |
| Ctrl+Tab | Dwa wpisy keymap w WindowWorkspace::Bind; komendy next/previous s? osobne od window.action. |
| Short / long name UI | Dwa pola w modalu Rename; stabilny ID nie zale?y od ?adnego z nich. Pola pozostaj? cz??ci? wymaganego kontraktu. |
| Lock / named workspaces | Sekcja ko?cowa RenderMenu, WorkspaceOperation i sekcje LockLayout/locked w state/chrome; named payload w SettingsPayload. |

## ImGui patches

Brak. Public API i imgui_internal wystarczy?y: DockNodeBeginAmendTabBar/End, title/tab rectangles, ImGuiWindowClass, dock-node flags, BeginPopupContextItem i settings handler. Vendor/ImGui nie by? modyfikowany; nie utworzono plik?w .patch.

## Walidacja wykonana w tym zadaniu

Build Debug w **D:/SJCM/01_SemiconductorStudio/SST-Prototypes/build**: WindowChromeLab, WindowChromeLabTests, JFM, JFMTests. Ka?dy premake/MSBuild/test uruchomiony przez tools/agents/build-lock.sh; MSBuild `-nr:false -m:1`. Buildy zako?czy?y si? powodzeniem; Core librarian zg?osi? istniej?ce ostrze?enia LNK4006 import libraries, bez b??d?w kompilacji/linkowania.

- WindowChromeLab GoogleTest: **43/43** (22 state logic, 8 ImGui integration, 13 inherited/template tests).
- JFM GoogleTest: **59/59**.
- JFM pytest: **96 passed, 3 xfailed**; 946 RuntimeWarnings w obliczeniach numerycznych. Nie zmieniano kodu oblicze?.
- WindowChromeLab embedded Python host: **1/1**, w nim **2/2 pytest**.
- `graphify update .`: zako?czone; graph.json i GRAPH_REPORT.md zaktualizowane. Narz?dzie pomin??o HTML z powodu limitu rozmiaru grafu; zg?osi?o brak extractor?w .m/.sql oraz puste w?z?y cz??ci plik?w danych.
- Raporty: build/w1-WindowChromeLabTests.xml, build/w1-JFMTests.xml, build/w1-jfm-pytest.xml, build/w1-lab-python.xml. Logi build?w: build/w1-<target>-build.log.

## Ograniczenia UX i integracji

Pozosta?e legacy IPanel nie maj? nowego chrome/listy Window do czasu migracji. Pasek zminimalizowanych ma jeden wiersz. Dock to wskazuje aplikacj? lub istniej?c? grup?; podzia? strony wykonuje natywny drag docking ImGui. Focus/reveal oraz save/load named workspace nie dodaj? wpisu Undo; mutacje stanu i duplicate tak. Zamkni?cie nie usuwa danych dokumentu; zapis danych jest operacj? aplikacji. Dane silnika i dock tree nie s? cz??ci? headless JSON: projekt serializuje dane, INI serializuje drzewo dock?w. Numeryczne ID built-in windows wymagaj? stabilnej kolejno?ci rejestracji mi?dzy wersjami. Szersza migracja JFM oraz pod??czenie PlotLab engine factory pozostaj? kolejnymi zadaniami.

Computer Use odm?wi? dost?pu do WindowChromeLab (`not approved`), dlatego nie wykonano wizualnego przegl?du uruchomionej aplikacji. Backend-free testy ImGui wykona?y rzeczywiste Begin/End, docking, tab chrome, klik ?rodkowy, context menu nieaktywnej zak?adki, focus cycling, lock i INI/workspace roundtrip. Nie zaimplementowano Kopiuj jako obraz zgodnie z zakresem.

## Nag??wek bazy ? verbatim

```cpp
#pragma once

#include <cstdint>
#include <string>
#include "Presentation/Panels/IPanel.hpp"
#include "Core/Commands/Command.hpp"

namespace SST
{
    using WindowId = std::uint64_t;
    class WindowWorkspace;
    class PanelRegistry;
    class EditorWindow;
    struct WindowDuplicate
    {
        Ref<EditorWindow> window;
        Unique<ICommand> contentCommand;
    };

    enum class WindowMode { Normal, Minimized, Maximized };
    struct WindowCapabilities
    {
        bool closable = true, renamable = true, duplicable = false;
        bool minimizable = true, maximizable = true, pinnable = true, dockable = true;
        bool projectItem = false;
    };
    struct WindowState
    {
        std::string shortName, longName;
        WindowMode mode = WindowMode::Normal;
        WindowMode restoreMode = WindowMode::Normal;
        bool visible = true, pinned = false;
        float x = 80, y = 80, width = 640, height = 420;
        std::uint32_t dockId = 0;
        bool operator==(const WindowState &) const = default;
    };
    enum class WindowAction
    {
        Close, Minimize, Maximize, Restore, Pin, Rename, Duplicate, Float, Dock,
        CloseOthers, CloseType, Reveal, Activate, Next, Previous,
        Cascade, TileHorizontal, TileVertical, ShowMinimized, LockLayout,
        SaveWorkspace, LoadWorkspace
    };
    struct WindowRequest
    {
        WindowAction action = WindowAction::Activate;
        WindowId id = 0;
        std::string shortName, longName;
        std::uint32_t dockId = 0;
        bool confirmed = false;
    };

    // Content-only base for new SST windows; IPanel remains the migration adapter.
    class EditorWindow : public IPanel
    {
    public:
        EditorWindow(std::string type, std::string shortName, bool visible = true,
               WindowCapabilities capabilities = {});
        EditorWindow(const EditorWindow &other);
        [[nodiscard]] WindowId GetId() const { return m_Id; }
        [[nodiscard]] const std::string &GetType() const { return m_Type; }
        [[nodiscard]] const WindowCapabilities &Capabilities() const { return m_Capabilities; }
        [[nodiscard]] WindowState GetState() const;
        void ApplyState(const WindowState &state);
        [[nodiscard]] virtual bool IsDirty() const { return false; }
        [[nodiscard]] virtual WindowDuplicate Duplicate() const { return {}; }
        [[nodiscard]] Ref<IPanel> Clone() const final { return {}; } // Use the undoable workspace factory.
        void Render() final;
        [[nodiscard]] std::string ImGuiName() const;

    protected:
        virtual void RenderContent() = 0;
        virtual void RenderContextMenu() {}
        virtual void RevealInProject() {}
        // Project serializers own content. This hook is for window-specific view state.
        [[nodiscard]] virtual std::string SaveViewState() const { return {}; }
        virtual bool LoadViewState(const std::string &state) { return state.empty(); }
        void Request(WindowRequest request);

    private:
        friend class PanelRegistry;
        friend class WindowWorkspace;
        WindowId m_Id = 0;
        std::string m_Type;
        WindowCapabilities m_Capabilities;
        WindowState m_State;
        WeakRef<WindowWorkspace> m_Workspace;
        bool m_ApplyGeometry = false;
    };
}
```
