# SemiconductorStudio (SST) — Kontekst projektu

## Czym jest projekt

**SemiconductorStudio (SST)** to duże środowisko desktopowe (C++) do charakteryzacji, analizy, modelowania i symulacji urządzeń półprzewodnikowych. Celem jest replikacja i rozszerzenie funkcjonalności narzędzi takich jak OriginPro i SCAPS-1D.

Projekt ma zakres wieloletni. Podstawowym użytkownikiem końcowym jest ojciec autora (fizyk, ten sam workspace). Decyzje strategiczne są konsultowane z liderem projektu.

---

## Stan obecny

Projekt **nie jest greenfield** — to aktywna, rozbudowana baza kodu (~70–75% fundamentów gotowe, ~24 000–33 000 linii produkcyjnego C++).

### Dwa repozytoria (do konsolidacji)

| Repo | Branch | Uwagi |
|------|--------|-------|
| `sjcmdev/Defect-Studio` | `task/04-core` | core |
| `SirJamesClarkMaxwell/Defect-Studio-mgr` | `task/06-renderer` | renderer |

Oba dzielą wspólny core, który uległ dryftowi — wymagana konsolidacja w trzy repozytoria: **SST**, **SST-experiments**, **SST-prototypes**.

### Gotowe komponenty (produkcyjne)

- Logging, EventBus, JobSystem (BS::thread_pool, priority queue, cancel/retry/progress)
- CapabilityService, EventDispatcher, NotificationCenter
- ImGui layer system (docking, layout persistence), keymap/hotkey
- Python via nanobind (GIL management), YAML config, UI serialization
- Dual event system (EventBus + EventQueue), structured error handling
- OpenGL renderer (instanced rendering, compute shaders)
- 10 Architecture Decision Records (ADR)

---

## Stos technologiczny

| Warstwa | Technologie |
|---------|-------------|
| Język / build | C++23 (target C++26), MSVC (debug), Clang (Linux), Premake5 |
| UI | Dear ImGui (docking), ImPlot, OpenGL 4.3+, GLFW |
| Python | embedded CPython via nanobind, `uv`, JAX + XLA (GPU Monte Carlo) |
| Numerics | levmar (LM), LambertW (Halley + NR), LTSpice, SCAPS-1D wrapper |
| Data | pliki tekstowe (.dat), YAML,  SQLite, mp-units |
| Misc | spdlog, Tracy, EnTT, yaml-cpp |
| Docs | MkDocs Material (PlantUML, LaTeX math, rectangle-card nav) |
| VCS | GitHub (3 repo), bez GitHub Actions / Docker |

---

## Moduły docelowe

1. **JFM** — J-V / IV curve analysis & fitting (P09 = MVP, pełna reimplementacja)
2. **CV/DLCP** — capacitance-voltage, deep level transient
3. **Admittance/Impedance Spectroscopy** — z integracją LTSpice
4. **SunsVoc**
5. **SCAPS wrapper**
6. **AI/RAG integration** — wewnętrzne narzędzie deweloperskie (nie produkt fizyków)
7. **Python automation layer**
8. **Computation server**
9. **Node editor** — definicja modeli I(V) dla fitterów
10. **Plugin system** <- potencjalnie. Architektura jeszcze nie jest zamrożona

---

## Aktualny priorytet
1. Stabilizacja Core jako template dla prototypów
2. eksperyment z podłączeniem GPU C++ ImGui przez Premake5
3. Określenie zakres JFM/JFM+/JFM++ jako pierwszego prototypu 
4. Projekt architektury JFM/JFM+/JFM++ (po to jest ten projekt)
5. Ustalenie zakresu i kolejności kolejnych prototypów
6. Ustalenie MVP całego projektu
7. Ustalenie architektury w jakiej cały projekt będzie działał -> ADR
8. 

---

## Kluczowe zasady architektoniczne

- **Core layer** = selekcja, unifikacja nazw, izolacja — NIE zastępowanie std/zewnętrznych bibliotek
- Komponenty dodawane przyrostowo, napędzane realną potrzebą
- Git submodules → **Vendor/ directory** (header-only) + **Python bootstrap script**
- CI = lokalne skrypty batch/shell z pinowanymi wersjami kompilatora

- Istniejący kod JFM = **referencja tylko**, nie do migracji
- GPU solver 1D = **odłożony** (ryzyko pochłonięcia zasobów)

---

## Decyzje otwarte (do zatwierdzenia przez lidera)

- `SCOPE.md` — freeze zakresu MVP
- Zakres AI (dev tool vs. fizyk-facing feature)

---

## Konwencje pracy

- Język roboczy: **polski** (terminologia techniczna po angielsku)
- Hierarchia źródeł prawdy: dokumenty domenowe → README/CLAUDE.md → historia rozmowy → wiedza ogólna
- Kompresja kontekstu: `„skompresuj dotychczasową wiedzę i kontynuuj"`
- Dokumentacja: MkDocs Material, per-algorytm `VALIDATION.md`, staged extraction model
- Pre-fit routines definiowane przez Python scripts (`jnp`, `jit`/`vmap` compatible)
- Monte Carlo Δχ² liczone względem krzywej idealnej (nie próbki), zgodnie z tw. Wilksa
