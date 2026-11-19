# slproj — ScapsLab multi-device workspace, 2026-11-19

Dostarczone: workspace wielu urządzeń, Devices panel, niezależne dokumenty/wyniki/undo/dirty, compare I-V i AC, sequential Core jobs z cancel, `.scapslab`, legacy YAML migration, CLI, File/Recent/Examples oraz Python/headless parity. Zmiany produktu ograniczono do `Prototypes/ScapsLab/**`; nie zmieniano scaps_core ani .git.

## Format i rozmiar

JSON z czytelnymi dokumentami i zwartymi kolumnami wyników w jednym pliku; Core TextFileIO wykonuje atomic replacement bez koordynacji sidecarów. Dokument urządzenia zachowuje scaps_core schema i unknown extensions. FNV-1a canonical-document hash, fingerprint SHA-256 źródeł silnika (16 cyfr), Unix time w ms są zapisane w provenance. Undo history pozostaje w runtime.

- 3 × 100 000 scalar I-V points: **76 418 790 B rows / 28 419 444 B columns**; fixture: `tests/storage-size.json`.
- Dla pełnych profiles: dokładne zliczenie zserializowanych wartości rzeczywistych próbek, powtarzanych do 100 000 punktów na każde z trzech urządzeń, daje **35 394 890 712 B**. To kalkulacja rozmiaru bez alokacji/zapisu pliku 35 GB; nie jest testem takiego obciążenia.
- Pierwsze sześć przykładów: 3.2–10.9 kB; przykład z wynikami: **15 800 000 B** (pełne profiles, 3 urządzenia).
- Ograniczenie: serialization/snapshots są w RAM; bardzo duże profiles wymagają w przyszłości streaming/immutable sidecars. Bieżące sweep operations zachowują limit 10 001 punktów.

## Przykłady — 7 projektów, 13 urządzeń

| Projekt | Źródło z `scaps_core-wt/ui/validation/` |
|---|---|
| `01-pn-temperature-series.scapslab` | `cases/a4-pn-270K`, `a4-pn-300K`, `a4-pn-T350`: `device_project.yaml`, `input.def` |
| `02-pn-recombination.scapslab` | `cases/a3-pn-diffusion`, `a3-pn-srh`: `device_project.yaml`, `input.def` |
| `03-pn-defects.scapslab` | `s11b/projects/baseline.yaml`, `deep.yaml`; oryginalne decks `phase-11-numerics/baseline/baseline.def`, `deep/deep.def`, conversion w `s11b/run.py` |
| `04-pn-multilayer.scapslab` | `cases/a2-pn-equilibrium/device_project.yaml`, `input.def` |
| `05-capacitance-ready.scapslab` | `sref1/device_project.yaml`, `b-default.def` |
| `06-contact-playground.scapslab` | `nl1/project.yaml`, deck `stages/01-contacts/n-low-s/n-low-s-effective.def` |
| `07-pn-compare-with-results.scapslab` | Te same trzy decks co `01`; wyniki obliczone przez Core commands/jobs, nie ręczne fixtures |

Każde urządzenie opisuje źródło. Materiały nie zostały wymyślone. Defaults i stable layer/defect IDs zostały materialized istniejącym readerem. Pierwsze sześć projektów nie przechowuje wyników. Build instaluje `examples/` obok exe; File > Examples pokazuje wszystkie siedem.

## Gates i wykonane sprawdzenia

**MSVC Release: ScapsLab, ScapsLabHeadless, ScapsLabTests zbudowane w `build/slproj/`. Pełna suite: 25/25 PASS, 31.442 s.** Wcześniejsze robocze buildy były w `%TEMP%/codex-builds/SST-Prototypes/slproj`; końcowe testy uruchomiono z repo build. Każdy build/test/GPU run miał osobne wywołanie build-lock, MSBuild `-nr:false -m:1`, `/MP6`. Lokalny wrapper użył prawdziwego odczytu RAM przez Windows API, bo CIM jest blokowany w sandboxie; limit 4 slotów i próg RAM zachowano.

- Każdy przykład ładuje się i każdy punkt dark I-V ma `Converged`.
- Load → save → load: document dumps identyczne; wynik po reload i ponownym solve identyczny bitwise.
- `05`: C-V i C-f również rozwiązywane i round-trip AC snapshots sprawdzony.
- Legacy YAML → 1-device project; invalid project load nie zmienia live state.
- Results/undo/dirty są per device; duplicate nie dziedziczy wyników; model edit B nie zmienia DC/AC urządzenia A.
- Zmiana active device podczas joba nie przenosi wyników; sequential batch i cancel sprawdzone.
- Osobny child process przerwany przy tymczasowym zapisie TextFileIO: poprzedni plik zachowany byte-for-byte.
- Python: project_load/save, device_list/add/duplicate/remove/select, run_all oraz istniejące single-device commands, kernel/CSV parity.
- UI ID checks: również Devices w narrow layout; wszystkie PASS. Labels left, physical sliders i Ctrl+click zachowane.

GUI executable z repo build tak?e uruchomiono i zapisano compare screenshot. Sandbox zablokowa? runtime zapis `logs/` w repo build, wi?c GUI QA u?ywa?o writable portable working directory `%TEMP%/codex-builds/SST-Prototypes/slproj/bin/Release/ScapsLab`; build i pe?na suite pozosta?y w `build/slproj`. Logi obu pr?b: `final-gui.log`, `runtime-write-denied.log` w katalogu runs/slproj.

Graphify: `graphify update .` zako?czone (AST-only).

Wyniki: `D:/SJCM/01_SemiconductorStudio/tools/agents/runs/slproj/tests.xml`, `tests.log`.

## Screenshots — zapisane przez `--screenshot`, obejrzane

`D:/SJCM/01_SemiconductorStudio/tools/agents/runs/slproj/shots/`:

- `devices.png`: trzy urządzenia, kolory, status i compare checkboxes.
- `compare.png`: trzy I-V curves z legendą i kolorami, domyślnie log |J|; signed J nadal dostępne.
- `examples-menu.png`: File > Examples z kompletem plików.
- `dirty-title.png`: rzeczywisty project_edit przez Core; `Project *`, dirty indicator i unsaved state.
- `workspace.png`: Devices docked po lewej nad Layers, aktywne edytory i compare.

Computer Use helper był niedostępny po retry/reset. Menu/dirty screenshot wykonano przez rozszerzenie istniejącego gallery mode (`SCAPSLAB_GALLERY_MENU`, `SCAPSLAB_GALLERY_DIRTY`), z rzeczywistym rendererem i commands/jobs. Nie wykonano ręcznej interakcji z native Open/Save dialogs ani close guard; guard code został przejrzany, gallery nie zastępuje takiego testu.

## Engine needs / ograniczenia

1. **Illumination:** obecny ui snapshot przechowuje flagę generation, lecz `make_device_from_project`/public solve nie przekazuje generation. Run/AC z illumination fail jawnie. Dlatego brak light/thin-film example i obliczonych Voc/Jsc/FF/efficiency. Compare table jest gotowa na `solar_metrics` z polami `Voc_V`, `Jsc_A_m2`, dimensionless `FF`, `efficiency`; nie fabrykuje danych.
2. **`.def`:** def1 importer nie jest dostępny przez public API tego ui snapshotu. YAML import działa; UI zawiera jawny TODO/komunikat. Potrzebne import API z typed diagnostics i zachowaniem unsupported fields.
3. Public solver version identifier; obecnie provenance używa source fingerprint.
4. Cancellation callback wewnątrz mesh/Newton; obecne cancel jest cooperative na granicach solve.

Nie zmieniano silnika i nie uruchamiano ponownie pełnej walidacji SCAPS/oracle.

## Handoff dla sl4 / slio

Rozszerzać **`DeviceState` w `Workflow.hpp`**: document, DC/band/AC results, undo, dirty i editor selection są w jednym miejscu. `Current()` jest active-device view; `project.devices` przechowuje trwałe obiekty przez `Ref`. Delete history zachowuje ten sam obiekt, więc undo przywraca również jego wyniki i undo stack.

Job przypina **`pendingDevice`**; kolejka `runQueue` zawiera stable device IDs. Każdy nowy batch/recorder/import ma używać tej samej Core command → job ścieżki z GUI i Python, a snapshot ma należeć do konkretnego ID. Nie dodawać session-wide wyników. Edits invalidują tylko wynik właściciela. Zmiany struktury projektu muszą przejść validation przed publikacją; podczas save/load/structural job device selection jest blokowany.

`Workspace()` / `SetWorkspace()` odpowiadają za storage, `PackResults()` / `UnpackResults()` za columns. `ui` i device documents pozostają verbatim; heterogeneous result rows nie są spłaszczane. `device_update`, `device_move`, `project_edit` są już dostępne headless i z GUI. Nie dodawać MCP/skills/agents do produktu.
