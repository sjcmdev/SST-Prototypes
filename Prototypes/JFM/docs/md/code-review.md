# Code review 1 JFM od `96b750c5`

## Zakres

Przeglad obejmuje diff `96b750c5..HEAD` tylko dla kodu JFM:

- `Prototypes/JFM/src/**`: App, Domain, Events, IO, Presentation, Workspace.
- `Prototypes/JFM/python/jfm_import/**`: opcje importu, adapter C++/Python.
- `Prototypes/JFM/tests/**` i `Prototypes/JFM/python/tests/**`: tylko jako ocena pokrycia zmian.

Poza zakresem:

- zmiany w `Core/Presentation/**`, mimo ze sa potrzebne do integracji zakladki ustawien,
- pliki danych/projektow JFM i lokalne pliki robocze poza kodem,
- ogolny audyt calego repo.

Graphify bylo uzyte jako pierwsze zrodlo nawigacji. Najbardziej trafne wezly dla zakresu: `JFMImportDialogPanel`, `ProjectTreePanel`, `RawPlotPanel`, `JFMUserSettingsStore`, `JFMWorkspaceStore`, `JFMPlotRequestState` oraz zasady warstw z `CLAUDE_REPO_CONTEXT.md`.

## Wnioski

### P1 - `ProjectTreePanel` omija system komend, keymap i undo

Nowe akcje uzytkownika sa wykonywane bezposrednio z UI przez `JFMWorkspaceStore`: dodawanie/usuwanie, rename, short-name, drag/drop move, kopiowanie/duplikowanie/wklejanie oraz skroty `Ctrl+C/V/D`, `Delete`, `F2`.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:383`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:446`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:612`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:681`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:726`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:764`
- istniejacy system: `Prototypes/JFM/src/App/JFMCommands.cpp:607`

To narusza zasade repo: akcje uzytkownika powinny isc przez `CommandRegistry`/`CommandService`/keymap, a zmiany mutowalne powinny miec jeden kontrolowany punkt wejscia. Praktyczny skutek: brak spojnego undo/redo, observerow komend, wspolnego mapowania skrotow i jednego miejsca walidacji. UI zaczyna byc drugim orkiestratorem aplikacji.

Najmniejsza poprawka: zostawic `JFMWorkspaceStore` jako wlasciciela stanu, ale przeniesc nowe akcje drzewa do komend JFM. Panel powinien wysylac komende albo waski request, nie wykonywac logiki operacji.

### P1 - Preview importu robi IO i parsing w warstwie Presentation

`JFMImportDialogPanel` enumeruje katalogi, laduje pliki i parsuje krzywe w kodzie UI. Dodatkowo `previewFileText()` i `buildPreviewCurve()` wolaja `TextFileIO::Load()` w trakcie renderowania preview, czyli potencjalnie co klatke i dla wielu zaznaczonych plikow.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:191`
- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:399`
- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:699`
- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:739`

To lamie granice `Presentation`/`IO` i zasade "dlugie operacje przez JobSystem". Przy duzym folderze albo pliku UI moze zamarznac; przy parserze custom/scaps preview moze tez pokazywac inne dane niz realny import.

Najmniejsza poprawka: cache preview po zmianie selekcji/opcji, czytac tylko limitowany fragment pliku, a parsing/rekurencyjne skanowanie przeniesc do IO albo malego joba preview.

### P1 - Dwa mechanizmy komunikacji dla tego samego "plot request"

Zmiana dodaje `JFMPlotRequestState` jako wspoldzielona kolejke miedzy `ProjectTreePanel` i `RawPlotPanel`, ale rownolegle istnieje juz `PlotCharacteristicsRequested` na `EventBus`.

Referencje:

- `Prototypes/JFM/src/Presentation/JFMPlotRequestState.hpp:18`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:331`
- `Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.cpp:141`
- `Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.cpp:266`
- `Prototypes/JFM/src/IO/JFMIORegistration.cpp:233`

Import po `plotAfterImport` idzie eventem, a drzewko idzie prywatna kolejka. To tworzy dwa zrodla prawdy i inna semantyke: ukryty `RawPlotPanel` nie konsumuje kolejki, a przy klonach/panelach pierwszy konsument moze zdjac wszystkie requesty.

Najmniejsza poprawka: usunac `JFMPlotRequestState` i uzyc wszedzie `PlotCharacteristicsRequested` przez `EventBus`.

### P2 - Preview nazw nie gwarantuje zgodnosci z realnym importem

C++ preview robi proste `ReplaceAll("{var}")`, a import Python robi `pattern.format(**values)` i przy brakujacym kluczu wraca do fallbacku. Dodatkowo C++ `{T}` czyta tylko filename, a Python potrafi wziac temperature z headera.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:155`
- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:682`
- `Prototypes/JFM/python/jfm_import/common.py:67`
- `Prototypes/JFM/python/jfm_import/common.py:214`

Przy wzorcu z nieznana zmienna UI moze pokazac nazwe z literalnym `{missing}`, a import zapisze inna nazwe. Przy temperaturze z headera preview short-name bedzie puste/inne niz finalne metadata.

Najmniejsza poprawka: uzyc jednej implementacji ekspansji nazw. Jesli ma zostac podwojenie C++/Python, dodac test kontraktowy dla unknown variable, `{T}` z headera i custom variables.

### P2 - Za duze lambdy i logika domenowa w panelu drzewa

`PasteClipboard()` ma zagniezdzone lambdy do klonowania charakterystyk, serii i grup rekurencyjnie. To nie jest juz render UI, tylko operacja na modelu projektu.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:900`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:916`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:948`

To utrudnia testowanie i narusza granice. Najmniejsza poprawka: przeniesc kopiowanie/duplikowanie do `JFMWorkspaceStore` albo komendy aplikacyjnej, a panelowi zostawic tylko zebranie intencji.

### P3 - Nowy kod powieksza anonymous namespace

Standard uzytkownika mowi "brak anonymous namespace". Zmienione pliki dodaja lub rozbudowuja helpery w `namespace {}`.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp:26`
- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp:20`
- `Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.cpp:16`
- `Prototypes/JFM/src/App/JFMUserSettings.cpp:13`

To w czesci bylo juz w bazie, wiec nie traktuje tego jako samodzielny blocker, ale nowe helpery utrwalaja naruszenie standardu. Najmniejsza poprawka: named `detail` namespace albo prywatne/statyczne funkcje klasowe tam, gdzie helper dotyczy panelu.

### P3 - Ustawienia moga zapisywac YAML zbyt czesto

`JFMSettingsPanel::renderPlotDefaults()` zapisuje ustawienia przez `SetPlotDefaults()` przy kazdej zmianie ImGui, a slider zwraca `true` w trakcie przeciagania. To moze generowac serie zapisow pliku konfiguracyjnego na jedna interakcje.

Referencje:

- `Prototypes/JFM/src/Presentation/Panels/JFMSettingsPanel.cpp:71`
- `Prototypes/JFM/src/App/JFMUserSettings.cpp:195`

Najmniejsza poprawka: zapis dopiero na `ImGui::IsItemDeactivatedAfterEdit()` albo przyciskiem Apply dla grupy ustawien.

## Co jest w dobrym kierunku

- `JFMWorkspaceStore` pozostaje wlascicielem mutacji stanu projektu.
- Metadata `short_name` dla grup/serii dostalo round-trip w YAML.
- `plotAfterImport` uzywa `EventBus` z IO do Presentation; to jest dobry kierunek, tylko nie powinien miec rownoleglej kolejki panel-panel.

## Testy

Nie uruchamialem testow; review jest statyczne dla diffu `96b750c5..HEAD`.

# Code review 2 JFM od `96b750c5`

## 1. Zakres przeglądu (ustalony niezależnie)

- **Repozytorium:** `sjcmdev/SST-Prototypes`, gałąź `JFM-prototype`.
- **Zakres commitów:** od `96b750c5` (włącznie) do `HEAD` gałęzi w momencie klonowania.
  - Punkt odniesienia (rodzic `96b750c5`): `52c077a`
  - Punkt końcowy: `2c2bf7b`
  - 7 commitów, wszystkie z 2026-07-13:
    1. `96b750c` Implement JFM import export workflow
    2. `d7fc055` Improve JFM plot and import preview workflow
    3. `86bce3b` Improve JFM raw plot controls
    4. `b2b5247` Improve JFM tree selection and plotting
    5. `94638e0` Improve JFM import names and plot workflow
    6. `5e6b7d5` Improve JFM import naming and plot settings
    7. `2c2bf7b` Improve JFM import settings and naming UI
- **Definicja „kodu JFM"**: wszystko pod `Prototypes/JFM/**` zmienione w tym zakresie. To **41 plików**, +4664/‑244 linii: `src/App/*`, `src/Domain/JFMTypes.hpp`, `src/Events/JFMEvents.hpp`, `src/IO/*`, `src/Presentation/*`, `src/Workspace/*`, `python/jfm_import/*`, `tests/*`, `premake5.lua`. Pełna lista w Załączniku B.
- **Świadomie wykluczone z przeglądu (kod Core):** te same commity dotykają też 4 plików `Core/Presentation/**` (`EditorLayer.cpp/.hpp`, `Panels/Settings.cpp`, `Panels/SettingsPanel.hpp`, +59/‑2). To generyczny mechanizm `SettingsTabExtension`/`RegisterSettingsTab`, z którego korzysta nowy `JFMSettingsPanel`. Zgodnie z poleceniem nie jest to tu recenzowane liniowo, ale jest istotne dla oceny granic systemów — patrz 4.3 i Załącznik C.

### Metodologia

`graphify-out/` jest w tym repo generowane lokalnie i ignorowane przez git (patrz `.gitignore`, `AGENTS.md`) — w tym środowisku katalog nie istnieje i samo narzędzie `graphify` nie jest zainstalowane, więc nie było dostępne. Nawigację po zależnościach i granicach modułów wykonano więc metodą zalecaną w `AGENTS.md` jako zapasową: bezpośrednia lektura źródeł, `git diff`/`git log` w precyzyjnych zakresach oraz ukierunkowane `grep` po całym `Prototypes/JFM/src`, żeby odróżnić „nowe w tym zakresie" od „odziedziczone". Jako punkt odniesienia dla standardów posłużyły `AGENTS.md` (specyficzny dla tego repo) oraz `CLAUDE_REPO_CONTEXT.md` (który opisuje nazewnictwo katalogów z pokrewnego repo `Defect-Studio-mgr`, więc jego konkretne ścieżki nie pasują 1:1 do `SST-Prototypes` — potraktowano go jako źródło ogólnych zasad architektonicznych, nie jako mapę katalogów).

---

## 2. Podsumowanie

Import/eksport/joby (`JFMIORegistration`, `JFMImportJob`, `JFMExportRawTsvJob`, `JFMPythonImportBridge`) oraz nowe panele (`JFMImportDialogPanel`, `CharacteristicDetailsPanel`) są napisane solidnie i poprawnie korzystają z Core (EventBus, JobSystem, IOLayer, Notifier, TextFileIO, Logger) — patrz sekcja 5. Największy problem tego zakresu nie leży w pojedynczym pliku, tylko w **rozjeździe między nowo dodaną warstwą komend a realnym UI**: `ProjectTreePanel` wykonuje mutacje bezpośrednio na `JFMWorkspaceStore`, a równolegle w tym samym zakresie commitów dopisano do `CommandRegistry` osiem komend robiących to samo — nieużywanych przez ten panel. Do tego dochodzi drugi, analogiczny rozjazd (dwa kanały „poproś o wykres"), jeden konkretny martwy element UI, powtarzalne naruszenie zasady braku anonymous namespace i jedna regresja funkcjonalna względem dokumentu wymagań naukowych.

| #   | Ustalenie                                                                               | Waga          |
| --- | --------------------------------------------------------------------------------------- | ------------- |
| 4.1 | Nowe komendy `CommandRegistry` odłączone od realnego UI (Store wywoływany bezpośrednio) | **Krytyczne** |
| 4.2 | Dwa równoległe mechanizmy „poproś o wykres" (EventBus vs. ręczna kolejka)               | Ważne         |
| 4.3 | Kod WinAPI w `Prototypes/JFM` zamiast rozszerzenia `Core/Platform`                      | Ważne         |
| 4.4 | Anonymous namespace w 5 plikach — niezgodność ze standardem                             | Średnie       |
| 4.5 | Usunięcie `PlotScale::SignedLog` — regresja względem specyfikacji                       | Średnie       |
| 4.6 | Zduplikowane lambdy wyszukujące w `JFMWorkspaceStore`                                   | Drobne        |
| 4.7 | Brak testów na poziomie „wiring" komend — luka wyjaśniająca 4.1                         | Drobne        |

---

## 3. Ustalenia szczegółowe

### 4.1 [KRYTYCZNE] Nowe komendy w `CommandRegistry` są martwe — `ProjectTreePanel` woła `JFMWorkspaceStore` bezpośrednio

W tym samym zakresie commitów dopisano do `Prototypes/JFM/src/App/JFMCommands.cpp` osiem nowych komend:

| CommandID                                | Handler                             | Rejestracja |
| ---------------------------------------- | ----------------------------------- | ----------- |
| `jfm.project_tree.add_series`            | `handleAddSeries` (L461)            | L607-610    |
| `jfm.project_tree.delete_series`         | `handleDeleteSeries` (L470)         | L612-615    |
| `jfm.project_tree.delete_characteristic` | `handleDeleteCharacteristic` (L479) | L617-620    |
| `jfm.scripts.add`                        | `handleAddScript` (L492)            | L622-625    |
| `jfm.scripts.remove`                     | `handleRemoveScript` (L499)         | L627-630    |
| `jfm.scripts.reload`                     | `handleReloadScript` (L506)         | L632-635    |
| `jfm.export.series_raw_tsv`              | `handleExportSeriesRawTsv` (L398)   | —           |
| `jfm.export.group_raw_tsv`               | `handleExportGroupRawTsv` (L424)    | L602-605    |

Tymczasem `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp` — czyli **jedyne realne miejsce w UI**, gdzie użytkownik dodaje/usuwa serie, characteristics i skrypty — w ogóle nie zna `CommandRegistry` dla tych akcji i woła store wprost:

- `AddSeries`: L384, L426, L917, L929
- `DeleteSeries`: L522
- `DeleteCharacteristic`: L617, L621, L783
- `AddScript`/`ReloadScript`/`RemoveScript`: L648, L659, L661 (`RenderScriptsNode`)
- `ExportGroup` (metoda własna panelu, L808-829) — powiela logikę `handleExportGroupRawTsv` (zbiera id grup przez `collectGroupIds`, buduje `JFMExportItem`, publikuje `ExportRawTsvBatchRequested`) zamiast wołać komendę z kontekstem `group_id`; wywoływana z menu kontekstowego grupy (L432).
- Analogicznie „Export Raw TSV" w menu kontekstowym serii (`RenderSeriesNode`, ok. L502) samodzielnie buduje `ExportRawTsvBatchRequested`, zamiast wołać `jfm.export.series_raw_tsv`.

Konsekwencje:
- **Undo/Redo nie działa dla żadnej z tych operacji.** `Core/Commands` jest jedynym miejscem integracji z `Core/Undo` w tym repo (potwierdzone: `grep -i undo` w całym `Prototypes/JFM` nie zwraca nic poza pozycją menu `edit.undo`/`edit.redo` w `JFMMenu.cpp`). Dodanie/usunięcie serii, characteristics i skryptów w drzewie projektu nie trafia na stos cofania mimo że w tym samym zakresie zbudowano dla nich pełnowymiarowe komendy.
- **Command Palette i keybindingi dla tych akcji są fasadą** — komenda istnieje w rejestrze i ma opis, ale naciśnięcie/odpalenie jej nic sensownego nie robi dla dwóch z nich (patrz niżej), a dla pozostałych po prostu duplikuje kod, który i tak wykona się inaczej z UI.
- Dwie komendy są dodatkowo **całkowicie niefunkcjonalne nawet wywołane wprost**, bo nie mają fallbacku i nic nigdy nie ustawia wymaganego kontekstu (potwierdzone `grep` po całym `Prototypes/JFM/src` — `"group_id"`, `"script_id"` jako klucze `CommandContext` są tylko odczytywane, nigdy ustawiane):
  - `handleExportGroupRawTsv` (L424): `if (groupId.empty()) return Result<void>{};` — cichy no-op. Do tego pozycja menu wywołująca ją jest zablokowana na stałe (patrz 4.1.1).
  - `handleRemoveScript`/`handleReloadScript` (L499, L506): bez `context.TryGet<std::string>("script_id")` nie robią nic, cicho.
  - Dla porównania `handleDeleteSeries`, `handleDeleteCharacteristic`, `handleAddSeries` mają sensowny fallback (`GetSelectedSeriesId()`, `GetSelectedCharacteristicIds()`, pusty `group_id` = seria top-level), więc technicznie by zadziałały z Command Palette — ale i tak nie są z niczym połączone.

#### 4.1.1 Konkretny błąd: pozycja menu eksportu grupy jest trwale wyłączona

`Prototypes/JFM/src/Presentation/JFMMenu.cpp`:
```cpp
ImGui::BeginDisabled(workspace == nullptr || workspace->GetProjectSnapshot().groups.empty());
commandMenuItem(services, "Selected Group Raw TSV", nullptr, "jfm.export.group_raw_tsv", false);
ImGui::EndDisabled();
```
Piąty argument `commandMenuItem` to `enabled`; przekazanie na sztywno `false` nadpisuje warunkowy `BeginDisabled(...)` powyżej — pozycja menu jest wyłączona **zawsze**, niezależnie od tego, czy w projekcie są grupy. To odróżnia ją od analogicznych, poprawnych wywołań w tym samym pliku (np. `"jfm.export.raw_tsv"` i `"jfm.export.series_raw_tsv"` nie dostają twardego `false`). W połączeniu z pustym `group_id` z 4.1 realny eksport grupy działa wyłącznie przez `ProjectTreePanel::ExportGroup` (prawy klawisz na grupie) — ścieżka przez menu główne i przez samą komendę jest martwa.

**Rekomendacja:** to wymaga decyzji architektonicznej, nie tylko łatki. Albo (a) `ProjectTreePanel` zaczyna wołać `services.commandRegistry`/`Execute(...)` z odpowiednim `CommandContext` zamiast metod store'a bezpośrednio — co przywraca Undo i sens istnienia tych ośmiu komend — albo (b) te komendy są świadomie usuwane/oznaczone jako placeholder, jeśli bezpośrednie wywołania z drzewa są celowym uproszczeniem prototypu. Obecny stan — dwa równoległe, częściowo zepsute mechanizmy dla tych samych akcji — jest gorszy niż każda z tych opcji osobno.

---

### 4.2 [WAŻNE] Dwa niezależne kanały komunikacji dla „poproś o wykres"

`Prototypes/JFM/src/Events/JFMEvents.hpp` dodaje właściwy `BusEvent`:
```cpp
struct PlotCharacteristicsRequested final : public BusEvent { std::vector<CharacteristicId> ids; std::string title; };
```
Producent: `JFMIORegistration.cpp` (`HandleImportBatch`) — po imporcie, jeśli `plotAfterImport`, publikuje to zdarzenie przez `state->eventBus->Queue(...)`. Konsument: `RawPlotPanel.cpp` (konstruktor) subskrybuje je przez `m_EventBus->Subscribe<PlotCharacteristicsRequested>(...)` i woła `PlotIds(...)`.

Równolegle `Prototypes/JFM/src/Presentation/JFMPlotRequestState.hpp` (nowy plik) definiuje ręczną, chronioną mutexem kolejkę `{ids, title}` z `QueuePlot()`/`ConsumeRequests()` — mechanicznie robiącą dokładnie to samo. Producent: `ProjectTreePanel.cpp` (`QueuePlot`, wywoływane z menu „Plot Series"/„Plot Group"/itp.). Konsument: ten sam `RawPlotPanel::Render()`, który co klatkę odpytuje `m_PlotRequests->ConsumeRequests()` i woła to samo `PlotIds(...)`. `App.cpp` łączy oba panele tym samym `Ref<JFMPlotRequestState>`, więc mechanicznie działa — problem jest architektoniczny, nie funkcjonalny.

Efekt: jeden konsument (`RawPlotPanel`), dwa nieskoordynowane wejścia dla identycznej operacji, zależnie od tego, kto jest nadawcą. To dokładnie ten sam wzorzec co w 4.1 (nowy, generyczny system komunikacji obok już istniejącego, zamiast w nim), tylko po stronie prezentacji zamiast komend.

**Rekomendacja:** ujednolicić do jednego kanału — `EventBus`/`PlotCharacteristicsRequested` już działa i jest generyczny, `ProjectTreePanel` powinien publikować to samo zdarzenie zamiast wołać `QueuePlot` na dedykowanym obiekcie. Usunięcie `JFMPlotRequestState` zdejmuje z `RawPlotPanel` jedną zależność i pętlę odpytywania w `Render()`.

---

### 4.3 [WAŻNE] Kod specyficzny dla Windows w `Prototypes/JFM` zamiast rozszerzenia `Core/Platform`

`handleOpenNewProjectWindow` (`JFMCommands.cpp`, L298, komenda `jfm.project.open_new_window`) używa wprost `#if defined(DS_PLATFORM_WINDOWS)` z `CreateProcessW`/`GetModuleFileNameW` z `<windows.h>`. Repo ma już dedykowaną, przetestowaną abstrakcję procesów: `Core/Platform/ProcessRunner.hpp/.cpp` (`RunProcess`, `Result<ProcessRunResult>`) — nie nadaje się wprost do „odpal i zapomnij" (jest synchroniczny, czeka na zakończenie i przechwytuje stdout/stderr), ale to tylko wzmacnia wniosek: brakującą operację „uruchom nową, niezależną instancję aplikacji" powinien dostarczyć `Core/Platform` (obok istniejących `PlatformWindow`, osobnych implementacji Windows/Linux/macOS), a nie pojedynczy handler komendy w module JFM.

Poza naruszeniem granicy warstw (kod platformowy w warstwie App/JFM zamiast w Core), to również luka funkcjonalna: repo ma pliki `Core/Platform/Linux/LinuxPlatformWindow.cpp` i `Core/Platform/MacOS/MacOSPlatformWindow.cpp`, więc Linux/macOS są realnymi platformami docelowymi projektu (zgodnie też z `PROJECT_CONTEXT.md`: Clang/Linux jako drugi kompilator), a ta komenda na tych platformach zwraca jedynie błąd „only implemented on Windows" — czyli nowa pozycja menu (`JFMMenu.cpp`) jest funkcjonalna tylko na jednym z trzech wspieranych systemów.

**Rekomendacja:** przenieść uruchamianie nowej instancji do `Core/Platform` (np. `PlatformSystem`/nowy `ProcessLauncher` z metodą `LaunchDetached(Path executable, args)`), z implementacjami per-platforma tam, gdzie już mieszka `PlatformWindow`. `JFMCommands.cpp` powinien wołać tylko tę abstrakcję.

---

### 4.4 [ŚREDNIE] Anonymous namespace w 5 plikach — niespójne ze standardem „brak anonymous namespace"

Sprawdzone po całym `Prototypes/JFM/src` (uwzględniając styl `namespace` / `{` w osobnych liniach, używany w tym repo). Blok anonimowy pojawia się w:

- `Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp` — **nowo dodany** w tym zakresie (potwierdzone w diffie jako linia `+`).
- `Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.cpp` — nowo dodany.
- `Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp` — nowo dodany (plik w całości nowy).
- `Prototypes/JFM/src/App/JFMUserSettings.cpp` — nowo dodany (plik w całości nowy).
- `Prototypes/JFM/src/IO/JFMExportRawTsvJob.cpp` — anonymous namespace istniał już przed `96b750c5`; w tym zakresie dopisano do jego wnętrza nową funkcję `buildTsv` zamiast wynieść ją poza namespace lub zlikwidować blok przy okazji.

Dla kontrastu, w tym samym zakresie **poprawnie** (bez anonymous namespace, wolne funkcje wprost w `namespace SST::Prototypes::JFM`) napisano: `JFMCommands.cpp`, `JFMWorkspaceStore.cpp`, `JFMIORegistration.cpp`, `JFMPythonImportBridge.cpp`, `JFMProjectYamlSerializer.cpp`, `CharacteristicDetailsPanel.cpp`, `JFMMenu.cpp`. Czyli większość plików w tym zakresie trzyma standard — problem jest skoncentrowany w nowych/dużych plikach prezentacji i w ustawieniach.

**Rekomendacja:** zamienić `namespace { ... }` na zwykłe umieszczenie funkcji pomocniczych bezpośrednio w `namespace SST::Prototypes::JFM { ... }`, zgodnie z konwencją pozostałych 7 plików z tego samego zakresu.

---

### 4.5 [ŚREDNIE] Regresja: usunięcie `PlotScale::SignedLog`

Przed tym zakresem `RawPlotPanel.hpp` definiował `enum class PlotScale { Linear, SemiLog, SignedLog }` z realną obsługą w renderze (`signedLog(rawY)` + osobny wykres). Po refaktorze na wielo-obszarowy plot (`PlotArea`, wiele okien wykresów) enum ma już tylko `{ Linear, SemiLog }` — `SignedLog` usunięty z typu, bez śladu w nowym `Render()`: zostały tylko dwa `RadioButton` („Linear", „Semi-log"), a gałąź rysowania dla „nie-Linear" filtruje niedodatnie wartości Y (`absolutePositive`, czyli de facto semi-log) zamiast liczyć signed-log.

To bezpośrednia rozbieżność z `jfm-scientific-research-perplexity.md` (§2.3 „Signed-log: ciągłe przez zero, zachowuje oba znaki") i z `JFM_functional_specification.md` (§14.2, który explicite wymienia signed-log obok liniowej i logarytmicznej jako wymaganą skalę osi). Dla charakterystyk ciemnych ze zmianą znaku prądu blisko V=0 (typowy przypadek w tej domenie) nowy `RawPlotPanel` po prostu nie pokaże punktów ujemnych w trybie innym niż liniowy — cicho, bez ostrzeżenia.

**Rekomendacja:** przywrócić signed-log w nowym, wielo-obszarowym `Render()` (dodać trzeci `RadioButton` i gałąź liczącą `slog(I)` zamiast `absolutePositive`), albo — jeśli to świadoma decyzja na czas prototypu — odnotować to jako celowe ograniczenie w dokumencie wymagań, żeby nie wyglądało na przeoczenie przy kolejnym przeglądzie.

---

### 4.6 [DROBNE] Zduplikowane lambdy wyszukujące w `JFMWorkspaceStore.cpp`

Klasa ma już ustaloną konwencję prywatnych helperów `find*Locked` dla Group/Series/Characteristic (istniejące przed `96b750c5`). Nowy kod dla skryptów tej konwencji nie powtarza — identyczny predykat pojawia się po dwa razy:
```cpp
[&path](const JFMScriptInfo &script) { return script.path == path; }      // L446, L477
[&scriptId](const JFMScriptInfo &script) { return script.id == scriptId; } // L498, L512
```
To nie jest przykład „lambda zamiast funkcji" w sensie problematycznym (są to trywialne, jednorazowe predykaty STL — idiomatyczne C++), ale konkretna duplikacja logiki, której nie ma przy analogicznych wyszukiwaniach Group/Series/Characteristic w tym samym pliku.

**Rekomendacja:** dodać `findScriptByPathLocked`/`findScriptByIdLocked`, analogicznie do istniejących helperów, i użyć ich w czterech miejscach.

Poza tym pojedynczym przypadkiem, ocena użycia lambd w tym zakresie jest dobra — patrz sekcja 5.

---

### 4.7 [DROBNE] Luka w testach na poziomie „wiring" komend

Nowe testy (`JFMWorkspaceTests.cpp`, `JFMUserSettingsTests.cpp`, `JFMProjectJobsTests.cpp`, `JFMProjectYamlSerializerTests.cpp`) dobrze pokrywają warstwę store/serializer/job, ale żaden test nie wykonuje komend z `JFMCommands.cpp` przez `CommandRegistry` z realnym `CommandContext`. Test taki (np. „`Execute("jfm.export.group_raw_tsv", ctx z group_id)` faktycznie tworzy pliki") wychwyciłby 4.1/4.1.1 automatycznie. To nie jest krytyka istniejących testów (są solidne w swoim zakresie) — to konkretna, tania rekomendacja na przyszłość.

---

## 4. Rzeczy zrobione dobrze

- **Import/eksport/joby** (`JFMIORegistration.cpp`, `JFMImportJob`, `JFMExportRawTsvJob`, `JFMPythonImportBridge`) poprawnie i konsekwentnie korzystają z `EventBus`, `JobSystem`/`IOLayer`, `Notifier`, `TextFileIO`, `Logger` — dokładnie zgodnie z listą „Implemented Systems to Reuse" z `AGENTS.md`. `JFMExportRawTsvBatchJob` sensownie wydziela wspólne `buildTsv` zamiast duplikować logikę z pojedynczego eksportu.
- **`JFMImportDialogPanel`** — mimo rozmiaru (797 linii) dobrze zdekomponowany na małe, opisowe metody prywatne (`refreshPreviewFiles`, `buildVariableRules`, `previewShortName`, `relativeLabel`...); przycisk „Import" poprawnie publikuje zdarzenie `ImportRequested` przez `EventBus` zamiast wołać logikę importu bezpośrednio — to właściwy wzorzec, kontrastujący z 4.1/4.2.
- **`ProjectTreePanel`**, pomimo problemu opisanego w 4.1, jest dobrze rozbity na 20+ jednoznacznie nazwanych metod (`RenderGroupNode`, `RenderSeriesNode`, `RenderCharacteristicNode`, `HandleKeyboard`, `PasteClipboard`...) zamiast jednego monolitycznego `Render()`.
- **`JFMUserSettingsStore`** poprawnie korzysta z istniejących `Core/IO/TextFileIO` i biblioteki `yaml-cpp` zamiast tworzyć własny format/parser; `ConfigManager` z `Core/App/Managers` nie pasuje tu bezpośrednio (jest zamknięty na `ApplicationConfig`/`UIConfig`, nie ma punktu rozszerzenia per-moduł), więc własny store jest tu uzasadniony, nie jest to przeoczenie reużycia.
- **Rozszerzenie Core o `SettingsTabExtension`/`EditorLayer::RegisterSettingsTab`** (poza zakresem tego przeglądu, ale istotne dla oceny granic) to dobry przykład właściwego rozszerzania Core: generyczny punkt rozszerzenia w `Core/Presentation`, konkretna instancja (`JFMSettingsPanel`) zarejestrowana z poziomu `App.cpp` modułu — dokładnie odwrotnie niż w przypadku 4.3.
- **Brak anonymous namespace** dotrzymany w większości plików tego zakresu (`JFMCommands.cpp`, `JFMWorkspaceStore.cpp`, `JFMIORegistration.cpp`, `JFMPythonImportBridge.cpp`, `JFMProjectYamlSerializer.cpp`, `CharacteristicDetailsPanel.cpp`, `JFMMenu.cpp`) — patrz 4.4 dla wyjątków.
- **Nanobind adapter** (`JFMPythonImportBridge::makeImportOptionsDict` ↔ `cpp_adapter.py::_parse_options`) spójnie mapuje nowe opcje importu (custom variables, custom parser, decimal separator) po obu stronach FFI, z testami po stronie Pythona (`test_cpp_adapter_applies_parse_options`, `test_cpp_adapter_custom_script`).

---

## 5. Rekomendacje (w kolejności priorytetu)

1. Podjąć jawną decyzję architektoniczną dla wzorca z 4.1: albo `ProjectTreePanel` zaczyna wołać komendy przez `CommandRegistry`/`CommandContext` (przywraca Undo, Command Palette, keybindingi), albo osiem nowych komend zostaje usuniętych/oznaczonych jako TODO. Obecny stan „oba na raz, częściowo zepsute" jest najgorszą z trzech opcji.
2. Naprawić `"Selected Group Raw TSV"` w `JFMMenu.cpp` (usunąć twardy `false`) i połączyć drogę przez komendę z realnym `group_id` — albo świadomie usunąć tę pozycję menu, skoro `ProjectTreePanel::ExportGroup` już to robi poprawnie z prawego klawisza.
3. Ujednolicić „poproś o wykres" do jednego kanału (rekomendacja: `EventBus`/`PlotCharacteristicsRequested`, bo już działa dla ścieżki importu) i usunąć `JFMPlotRequestState`.
4. Przenieść „otwórz nowe okno projektu" do `Core/Platform` jako operację cross-platform zamiast WinAPI w `JFMCommands.cpp`; dodać realną obsługę Linux/macOS albo świadomie ukryć/wyłączyć pozycję menu tam, gdzie nieobsługiwana.
5. Usunąć anonymous namespace z 5 wskazanych plików (4.4), zgodnie z konwencją reszty zakresu.
6. Przywrócić `PlotScale::SignedLog` w nowym, wielo-obszarowym `RawPlotPanel::Render()` albo świadomie zaktualizować dokument wymagań, jeśli rezygnacja jest zamierzona.
7. Dodać `findScriptByPathLocked`/`findScriptByIdLocked` w `JFMWorkspaceStore`, usuwając duplikację z 4.6.
8. Rozważyć test „wiring" sprawdzający, że każda user-facing komenda ma realny efekt end-to-end — tańszy sposób wychwytywania klasy błędów z 4.1 niż przegląd kodu.

---

## Załącznik A — commity w zakresie

```
2c2bf7b 2026-07-13 21:18:46 +0200 Improve JFM import settings and naming UI
5e6b7d5 2026-07-13 20:22:23 +0200 Improve JFM import naming and plot settings
94638e0 2026-07-13 18:57:33 +0200 Improve JFM import names and plot workflow
b2b5247 2026-07-13 18:24:38 +0200 Improve JFM tree selection and plotting
86bce3b 2026-07-13 17:59:23 +0200 Improve JFM raw plot controls
d7fc055 2026-07-13 17:28:54 +0200 Improve JFM plot and import preview workflow
96b750c 2026-07-13 16:58:41 +0200 Implement JFM import export workflow
```

## Załącznik B — pliki JFM zmienione w zakresie (41)

```
M  Prototypes/JFM/premake5.lua
M  Prototypes/JFM/python/jfm_import/common.py
M  Prototypes/JFM/python/jfm_import/cpp_adapter.py
M  Prototypes/JFM/python/tests/test_jfm_import_smoke.py
M  Prototypes/JFM/src/App.cpp
M  Prototypes/JFM/src/App/JFMCommands.cpp
M  Prototypes/JFM/src/App/JFMCommands.hpp
A  Prototypes/JFM/src/App/JFMUserSettings.cpp
A  Prototypes/JFM/src/App/JFMUserSettings.hpp
M  Prototypes/JFM/src/App/NativeFileDialog.cpp
M  Prototypes/JFM/src/App/NativeFileDialog.hpp
M  Prototypes/JFM/src/Domain/JFMTypes.hpp
M  Prototypes/JFM/src/Events/JFMEvents.hpp
M  Prototypes/JFM/src/IO/JFMExportRawTsvJob.cpp
M  Prototypes/JFM/src/IO/JFMExportRawTsvJob.hpp
M  Prototypes/JFM/src/IO/JFMIORegistration.cpp
M  Prototypes/JFM/src/IO/JFMIORegistration.hpp
M  Prototypes/JFM/src/IO/JFMImportJob.cpp
M  Prototypes/JFM/src/IO/JFMImportJob.hpp
M  Prototypes/JFM/src/IO/JFMProjectYamlSerializer.cpp
M  Prototypes/JFM/src/IO/JFMPythonImportBridge.cpp
M  Prototypes/JFM/src/IO/JFMPythonImportBridge.hpp
M  Prototypes/JFM/src/Presentation/JFMMenu.cpp
M  Prototypes/JFM/src/Presentation/JFMMenu.hpp
A  Prototypes/JFM/src/Presentation/JFMPlotRequestState.hpp
A  Prototypes/JFM/src/Presentation/Panels/CharacteristicDetailsPanel.cpp
A  Prototypes/JFM/src/Presentation/Panels/CharacteristicDetailsPanel.hpp
A  Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.cpp
A  Prototypes/JFM/src/Presentation/Panels/JFMImportDialogPanel.hpp
A  Prototypes/JFM/src/Presentation/Panels/JFMSettingsPanel.cpp
A  Prototypes/JFM/src/Presentation/Panels/JFMSettingsPanel.hpp
M  Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.cpp
M  Prototypes/JFM/src/Presentation/Panels/ProjectTreePanel.hpp
M  Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.cpp
M  Prototypes/JFM/src/Presentation/Panels/RawPlotPanel.hpp
M  Prototypes/JFM/src/Workspace/JFMWorkspaceStore.cpp
M  Prototypes/JFM/src/Workspace/JFMWorkspaceStore.hpp
M  Prototypes/JFM/tests/JFMProjectJobsTests.cpp
M  Prototypes/JFM/tests/JFMProjectYamlSerializerTests.cpp
A  Prototypes/JFM/tests/JFMUserSettingsTests.cpp
M  Prototypes/JFM/tests/JFMWorkspaceTests.cpp
```

## Załącznik C — kod Core dotknięty w tym samym zakresie (poza zakresem przeglądu)

```
M  Core/Presentation/EditorLayer.cpp
M  Core/Presentation/EditorLayer.hpp
M  Core/Presentation/Panels/Settings.cpp
M  Core/Presentation/Panels/SettingsPanel.hpp
```
+59/‑2: dodanie `SettingsTabExtension` i `EditorLayer::RegisterSettingsTab(...)`, generycznego punktu rozszerzenia, z którego korzysta nowy `JFMSettingsPanel` (patrz sekcja 5, pozytywny przykład).