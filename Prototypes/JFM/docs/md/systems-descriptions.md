# Techniczny opis systemów w `Defect-Studio-mgr` i eksperymentów `SST-Experiments`

**Kontekst:** kandydat na wspólny Core dla prototypów SemiconductorStudio (JFM i inne)
**Data analizy:** 2026-07-05
**Analizowane repozytoria:**
- `https://github.com/SirJamesClarkMaxwell/Defect-Studio-mgr.git`, branch `main`, HEAD `d78c39b`
- `https://github.com/sjcmdev/SST-Experiments.git`, branch `main`, HEAD `d053165`

---

## Spis treści

0. [Cel dokumentu i metodologia](#0-cel-dokumentu-i-metodologia)
1. [`Defect-Studio-mgr` — stan repozytorium](#1-defect-studio-mgr--stan-repozytorium)
2. [Core — dogłębny opis systemów](#2-core--dogłębny-opis-systemów)
3. [`App` — dzisiejszy composition root](#3-app--dzisiejszy-composition-root)
4. [Warstwy specyficzne dla Defect Studio](#4-warstwy-specyficzne-dla-defect-studio)
5. [Testy](#5-testy)
6. [Co już wiedzą ADR-y o docelowej architekturze](#6-co-już-wiedzą-adr-y-o-docelowej-architekturze)
7. [`SST-Experiments` — PoC GPU / Simplex / Monte Carlo](#7-sst-experiments--poc-gpu--simplex--monte-carlo)
8. [Obserwacje i ryzyka pod kątem podziału Core + moduły](#8-obserwacje-i-ryzyka-pod-kątem-podziału-core--moduły)
9. [Indeks plików](#9-indeks-plików)

---

## 0. Cel dokumentu i metodologia

### 0.1 Po co ten dokument

`Defect-Studio-mgr` to **nie jest kod JFM** — to aplikacja *Defect Studio* do analizy struktur
krystalicznych/defektów/VASP (VESTA-like). Jest to jednak repozytorium, które `PROJECT_CONTEXT.md`
wskazuje jako jedno z dwóch źródeł konsolidacji w **SST** (obok `sjcmdev/Defect-Studio`), a jego
warstwa `Core` jest kandydatem na wspólny fundament, na którym JFM (i kolejne prototypy) mają
zostać zbudowane.

Ten dokument opisuje więc **co realnie istnieje i działa dziś** w obu repozytoriach — czytane
przez pryzmat trzech plików projektowych (`PROJECT_CONTEXT.md`, `jfm-scientific-research-perplexity.md`,
`JFM_functional_specification.md`) — tak, aby:

- dać pełny, zweryfikowany w kodzie obraz systemów Core, zanim zapadną decyzje o ich wydzieleniu,
- jawnie oznaczyć, co jest reużywalne dla JFM wprost, co jako wzorzec, a co jest wyłącznie domeną
  krystalografii i nie przenosi się dalej,
- udokumentować PoC GPU/Simplex/Monte Carlo z `SST-Experiments`, które prototypują dokładnie
  tę matematykę, jakiej wymaga `jfm-scientific-research-perplexity.md`,
- zebrać obserwacje istotne dla przyszłego podziału Core + moduły (styl Hazel: Core z `main()`,
  moduły dołączane przez punkt rozszerzenia), **bez projektowania samego podziału** — to celowo
  osobne zadanie.

### 0.2 Legenda reużywalności dla JFM

W sekcjach 4 i 7 każdy system oznaczony jest etykietą:

| Etykieta                    | Znaczenie                                                                                       |
| --------------------------- | ----------------------------------------------------------------------------------------------- |
| 🟢 **Core-reusable**         | Kod (nie tylko koncepcja) nadaje się do użycia przez JFM bez zmian domenowych                   |
| 🟡 **Wzorzec**               | Sam kod jest specyficzny dla Defect Studio, ale architektura/wzorzec warto powielić w JFM       |
| 🔴 **Domena krystalografii** | Nie przenosi się do JFM w żadnej formie — dane i logika specyficzne dla struktur krystalicznych |

### 0.3 Metodologia

Każde twierdzenie w tym dokumencie pochodzi z bezpośredniej lektury kodu źródłowego w
klonach obu repozytoriów (nie z samych plików `.md` czy pamięci projektu) — pliki `.md`
istniejące w repozytoriach (ADR-y, `CLAUDE_REPO_CONTEXT.md`, dokumenty faz eksperymentów)
są cytowane jako punkt odniesienia, ale zestawione z aktualnym stanem nagłówków `.hpp`.

---

## 1. `Defect-Studio-mgr` — stan repozytorium

### 1.1 Tożsamość i miejsce w projekcie SST

`Defect-Studio-mgr` implementuje **Defect Studio** — aplikację desktopową do wizualizacji i
analizy struktur krystalicznych (import formatów krystalograficznych, właściwości pierwiastków,
renderowanie 3D atomów/komórek elementarnych, most do Pythona przez `pymatgen`/`ASE`). Domenowo
nie ma nic wspólnego z JFM (I-V, modele diodowe, Lambert W) — ale `PROJECT_CONTEXT.md` wskazuje
to repozytorium (obok `sjcmdev/Defect-Studio`) jako jedno z dwóch źródeł do konsolidacji w
docelowe **SST**, a jego `src/Core` jako fundament, na którym mają stanąć wszystkie prototypy,
JFM włącznie.

**Gałąź `main` jest wiążąca i aktualna**: 36 commitów przed `task/06-renderer` (branch wskazany
w `PROJECT_CONTEXT.md`), 0 commitów w drugą stronę. Branch `refactor/full-plan` jest przodkiem
`main`. `main` to zbieżny, najbardziej aktualny stan pracy.

### 1.2 Struktura budowania — dziś dosłownie jeden executable

Cały `premake5.lua` (korzeń repo) definiuje **jeden target**:

```lua
project "DefectStudio"
    kind (WindowedApp/ConsoleApp wg platformy)
    files {
        "src/**.hpp",
        "src/**.cpp",
    }
```

Innymi słowy: `Core`, `App`, `Domain`, `Renderer`, `Presentation`, `IO`, `ScientificRuntime`,
`Storage`, `Debug`, `Demo` — wszystko jest jednym globem plików kompilowanym w jeden `.exe`.
Nie ma pośrednich bibliotek statycznych/dynamicznych między warstwami — podział na warstwy
istnieje wyłącznie na poziomie struktury katalogów i przestrzeni nazw C++, nie na poziomie
jednostek budowania.

Osobny (nieużywany aktywnie) fragment `premake5.lua` definiuje `project "DefectStudioPythonBridge"`
przez sam `filter{}` bez odpowiadającego bloku `kind`/`files` — wygląda na martwą/niedokończoną
konfigurację z wcześniejszego etapu (prawdopodobnie z czasu, gdy most Pythona miał być osobnym
modułem `nanobind` budowanym niezależnie). Warto to zweryfikować przy porządkowaniu premake,
ale nie wpływa to na obecny build.

Vendor (`Vendor/` — GLFW, ImGui *docking*, GLAD, spdlog, Tracy, GoogleTest, yaml-cpp, nlohmann/json,
`bshoshany-thread-pool` (BS::thread_pool), ImGuiNotify, imgui-command-palette, nanobind, stb,
nativefiledialog-extended, glm, EnTT) jest spięty jako **git submodules** — w klonie użytym do
tej analizy nie były pobrane (puste katalogi), co nie przeszkadza w opisie Core, bo Core korzysta
z ich publicznego API w sposób typowy dla każdej z tych bibliotek.

### 1.3 Punkt wejścia

```cpp
// src/main.cpp — całość
#include "Core/dspch.hpp"
#include "App/Application.hpp"
using namespace DefectStudio;
int main(int argc, char **argv)
{
    Application app = Application::Create(argc, argv);
    app.Run();
}
```

`Application` (w `src/App`, **nie** w `src/Core`) jest konkretną klasą będącą jednocześnie
singletonem (`Application::Get()`) i composition rootem — tworzy i posiada `CoreLayer`,
renderer, warstwę domenową, ImGui, konfigurację. Nie ma tu punktu rozszerzenia w stylu Hazelowego
`CreateApplication()` — `Application` nie jest bazą do podklasowania ani fabryką wybieraną przez
moduł zewnętrzny. To jest dokładnie miejsce, w którym `Core` i `Domain/UI` są dziś zrośnięte
w jeden executable (rozwinięcie w sekcji 3).

### 1.4 Mapa warstw — potwierdzona przez sam kod

Kod już definiuje autorytatywną listę warstw najwyższego poziomu, we własnym enumie:

```cpp
// src/Core/LayerStack.hpp
enum class LayerId
{
    Core, IO, Storage, ScientificRuntime, Domain,
    Renderer, ImGui, Editor, Demo, Debug, Unknown
};
```

Ta lista jest tożsama ze strukturą katalogów `src/*` i jest przydatna jako punkt odniesienia —
każda z tych nazw odpowiada osobnej podklasie `Layer` (`CoreLayer`, `IOLayer`, `StorageLayer`,
`ScientificRuntimeLayer`, `DomainLayer`, `RendererLayer`, `ImGuiLayer`, `EditorLayer`, `DemoLayer`,
`DebugLayer`), spinanej w jeden `LayerStack` przez `Application`.

Rozmiar: `src/Core` to 688 KB (ok. 1/3 całości `src/`), 334 plików `.hpp`/`.cpp` w `src/`
łącznie, 48 plików testów w `tests/Core` (GoogleTest).

### 1.5 Istniejąca dokumentacja wewnętrzna

Zanim ten dokument powstał, repozytorium miało już własną, obszerną dokumentację:

- `CLAUDE_REPO_CONTEXT.md`, `AGENTS.md` — kontekst dla agentów pracujących w repo
- `docs/mdbook/*` — ok. 24 strony architektury (MkDocs/mdBook), w tym `architecture-core.md`,
  `architecture-layers.md`, `system-layer-stack.md`, `application-lifecycle.md`, `events.md`,
  `job-progress.md`
- `docs/work/architecture-code-review-2026-07-01.md` — świeży (4 dni przed tą analizą), szczery
  przegląd sprzężeń architektonicznych, zweryfikowany bezpośrednio w kodzie
- `docs/work/architecture/adr/ADR-001..010` — 10 Architecture Decision Records (zgodne z tym,
  co wskazuje pamięć projektu) — szczegóły w sekcji 6
- `docs/work/project/TODO.md`, `my-notes.md`, `old-ds-functionality.md` — plany robocze; `my-notes.md`
  wprost wspomina "SemiconductorStudio" jako kontekst przyszłego wielokrotnego użycia paneli

Ten dokument nie zastępuje tamtej dokumentacji — synchronizuje jej ustalenia z aktualnym stanem
nagłówków `.hpp`, ponieważ część z niej (`docs/adr/0001-*.md` w starszej lokalizacji) jest
rozbieżna z aktualną lokalizacją `docs/work/architecture/adr/`.

---

## 2. Core — dogłębny opis systemów

Konwencje nazewnicze obowiązujące w całym Core (warto je poznać raz, bo pojawiają się wszędzie):

```cpp
// src/Core/Utils/Memory.hpp — identyczne z konwencją silnika Hazel
template <typename T> using Ref     = std::shared_ptr<T>;
template <typename T> using WeakRef = std::weak_ptr<T>;
template <typename T> using Unique  = std::unique_ptr<T>;
template <typename T, typename... Args> Ref<T>    CreateRef(Args&&...);
template <typename T, typename... Args> Unique<T> CreateUnique(Args&&...);
template <typename T> WeakRef<T> CreateWeakRef(const Ref<T>&);
```

oraz `Core/Utils/Path.hpp` (`Path`/`FilePath` — cienka nakładka na `std::filesystem::path`
z operatorami `/`, `Join`, `parent_path`), `Core/Utils/Time.hpp` (`Time::Now()`,
`Time::TimePoint`, `Time::Milliseconds`) i `Core/Utils/Assert.hpp` (`DS_ASSERT(check, msg)`,
aktywny tylko w buildzie debug, loguje przez `Logger` i wywołuje `Platform::DebugBreak()`).

To, że nazewnictwo (`Ref`/`Unique`/`WeakRef`/`CreateRef`/`Layer`/`LayerStack`/`EventDispatcher`
z makrami `DS_EVENT_CLASS_TYPE`) jest w praktyce identyczne z konwencją silnika **Hazel**, nie
jest przypadkiem — to dokładnie ten sam idiom, o którym mówi materiał wideo przywołany jako
punkt odniesienia dla podziału Core+moduły. Innymi słowy: styl docelowej architektury nie byłby
obcym importem — Core już dziś "myśli" w tych kategoriach, brakuje tylko punktu wejścia
(`CreateApplication()`-podobnego) i separacji na jednostki budowania.

### 2.1 `Result<T>` i `StructuredError` — jednolita obsługa błędów (`Core/Diagnostics`, `Core/Types/Error.hpp`)

**Rola:** zamiennik wyjątków dla operacji, które mogą się nie powieść w sposób przewidywalny
(nieprawidłowa konfiguracja, brak capability, błąd IO). Używany niemal wszędzie w Core jako
typ zwracany (`[[nodiscard]] Result<T>`), zamiast `std::expected` (projekt celuje w C++23/26,
ale własny typ daje kontrolę nad kształtem błędu).

**Kluczowe typy:**

```cpp
struct StructuredError {
    ErrorCategory category;   // Config, Job, IO, Python, Validation, Internal,
                               // Capability, Configuration, Runtime, Cancelled
    Severity severity;        // Info, Warning, Error, Fatal
    DisplayPolicy displayPolicy; // Silent, Notification, BlockingPopup
    std::string code, technicalDetails, userMessage, suggestion, source;
    bool IsRecoverable() const noexcept; // severity != Fatal
};

template <typename T> class Result {   // wariant T | StructuredError
    bool HasValue() const; operator bool() const;
    T& Value(); StructuredError& Error();
    // + aliasy w stylu std::optional/std::expected: has_value(), value(), error()
    // + przezroczyste operator->, operator*, size()/begin()/end() gdy T jest kontenerem
};
template <> class Result<void> { /* jak wyżej, bez wartości */ };
using VoidResult = Result<void>;

Notification ToNotification(const StructuredError&); // most do systemu powiadomień
```

**Jak używać:**

```cpp
Result<CommandOutcome> outcome = commandService.Execute(CommandID{"project.save"});
if (!outcome) {
    notifier.Error(outcome.Error().userMessage, outcome.Error().technicalDetails);
    return;
}
// outcome.Value() / outcome->... dostępne
```

**Uwagi:** `ToNotification()` domyka pętlę błąd→UI — dowolny `StructuredError` można od razu
zamienić na `Notification` i opublikować przez `Notifier`. Dla JFM to bezpośrednio przekłada
się na wymóg z `JFM_functional_specification.md` §18 ("błędy widoczne w UI, nie tylko w konsoli") —
mechanizm już istnieje, wystarczy go używać konsekwentnie.

---

### 2.2 EventSystem — dwa równoległe systemy zdarzeń (`Core/EventSystem/*`)

Repozytorium celowo utrzymuje **dwa niezależne tory zdarzeniowe** (`EventSystemKind::Bus` i
`::Dispatching`), każdy z innym przeznaczeniem — potwierdzone w `Common/EventSystemCommon.hpp`.

#### 2.2.1 `EventBus` (`BusEventSystem/`) — typowany pub/sub dla zdarzeń domenowych/aplikacyjnych

**Rola:** luźno powiązana komunikacja między systemami (JobSystem → ProgressTracker,
CommandRegistry → obserwatorzy UI, dowolny moduł → `Notifier`, itd.). To jest system, którego
JFM będzie używać najczęściej.

**Kluczowe API:**

```cpp
class EventBus : public std::enable_shared_from_this<EventBus> {
public:
    template <BusEventType TEvent, typename THandler>
    SubscriptionHandle Subscribe(THandler&& handler, EventPriority priority = Normal);

    template <BusEventType TEvent> void Publish(TEvent& event);          // synchroniczne, natychmiastowe
    template <BusEventType TEvent, typename... Args> void PublishNew(Args&&...); // wygoda: skonstruuj+publikuj

    template <BusEventType TEvent> void Queue(const TEvent& event);      // bezpieczne z innego wątku
    template <BusEventType TEvent, typename... Args> void QueueNew(Args&&...);
    void ProcessQueue();  // odpal na wątku właściciela (zwykle main), drenuje kolejkę
};
```

Zdarzenie definiuje się przez dziedziczenie po `BusEvent`:

```cpp
struct MyThingHappened final : public DefectStudio::BusEvent {
    int value;
};

SubscriptionHandle handle = eventBus->Subscribe<MyThingHappened>(
    [](const MyThingHappened& e) { /* ... */ }, EventPriority::Normal);

eventBus->PublishNew<MyThingHappened>(42);   // dostarczone natychmiast, synchronicznie
```

`SubscriptionHandle` jest RAII — `Reset()`/destruktor automatycznie odsubskrybowuje; brak
ręcznego zarządzania cyklem życia subskrypcji. `EventPriority` (`Highest…Lowest`) determinuje
kolejność wywołań handlerów tego samego typu zdarzenia.

**Ważna pułapka wątkowa:** `m_Listeners` (mapa subskrypcji) **nie jest** chronione mutexem —
tylko `m_Queue` (użyte przez `Queue()`) ma `m_QueueMutex`. W praktyce oznacza to: **`Subscribe`
i `Publish` muszą się odbywać na jednym wątku** (zwykle main/UI), natomiast `Queue()` jest
bezpieczne do wołania z wątku roboczego (np. z `JobContext` wewnątrz zadania JobSystem), a
odebranie zdarzenia następuje dopiero przy kolejnym `ProcessQueue()` na wątku właściciela. Dla
JFM to jest wzorzec do powielenia wprost przy raportowaniu postępu MC/fitów z wątków GPU/CPU
worker do UI.

#### 2.2.2 `DispatchingEventSystem` — zdarzenia platformowe (`Event`, `EventDispatcher`, `EventQueue`)

**Rola:** klasyczny, jednoobiektowy dispatch (wzorzec identyczny z Hazelem/Cherno): zdarzenia
GLFW (`WindowClose`, `WindowResize`, `KeyPressed/Released/Repeated`, `MouseButtonPressed/Released`,
`MouseMoved`, `MouseScrolled`, `TouchpadGesture`) trafiają do `EventQueue` (bufor `std::variant`
o zamkniętym, 10-elementowym zestawie typów, ze `static_assert` pilnującym, by nikt nie zapomniał
zaktualizować `EventVariant` przy dodaniu nowego typu), drenowane raz na klatkę, a każdy
konkretny event obsługiwany jest przez:

```cpp
void Layer::OnEvent(Event& e) {
    EventDispatcher dispatcher(e);
    dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& ev) {
        return OnWindowResize(ev); // zwraca bool = "handled"
    });
}
```

**Różnica względem `EventBus`:** to jest system "jeden nadawca (platforma) → łańcuch warstw",
nie "wielu nadawców/wielu odbiorców". Dla JFM oznacza to: obsługa klawiatury/myszy/okna idzie
przez `DispatchingEventSystem`; komunikacja między systemami logicznymi (fit skończony, MC
gotowe, import zakończony) idzie przez `EventBus`. Oba systemy współdzielą `EventControl`
(`handled`, `stopPropagation`) i `EventThreadingPolicy` (`MainThreadOnly` / `ThreadSafeQueued`).

---

### 2.3 JobSystem (`Core/JobSystem/*`)

**Rola:** jedyny akceptowany sposób wykonywania pracy w tle (zgodnie z ADR-010, p. §6.2) —
budowany na `BS::thread_pool` (bshoshany-thread-pool), z priorytetami, opóźnionym uruchamianiem,
kooperacyjnym anulowaniem, historią i pełną integracją ze `EventBus`. To jest **prawdopodobnie
najważniejszy pojedynczy system Core dla JFM** — fit pojedynczej charakterystyki, fit wsadowy
serii, i Monte Carlo (setki-tysiące powtórzeń) to podręcznikowe przypadki użycia JobSystem.

**Interfejs zadania:**

```cpp
class IJob {
public:
    virtual std::string GetName() const = 0;
    virtual std::string GetType() const = 0;
    virtual void Execute(JobContext& context) = 0;
};
```

`JobContext` przekazany do `Execute()` daje zadaniu kontrolowany dostęp do świata zewnętrznego —
**nie ma bezpośredniego dostępu do JobSystem/EventBus**, tylko przez zestaw callbacków:

```cpp
class JobContext {
public:
    void SetProgress(float completed, float total) const;
    void SetStage(const std::string& stage) const;
    void SetMessage(const std::string& message) const;
    void LogInfo/LogWarning/LogError(const std::string&) const;

    bool IsCancellationRequested() const;
    void ThrowIfCancellationRequested() const;  // rzuca JobCancelledException

    JobId SubmitJob(const Ref<IJob>&, JobPriority = Normal) const;      // zadanie potomne, równoległe
    JobId SubmitJobSequential(const Ref<IJob>&, JobPriority = Normal) const; // zadanie potomne, sekwencyjne
    bool  WaitForJob(JobId) const;
};
```

**API `JobSystem`:**

```cpp
class JobSystem {
public:
    explicit JobSystem(Ref<EventBus> eventBus = {}, std::size_t threadCount = 0);

    JobId Submit(const Ref<IJob>& job, JobPriority priority = Normal);
    JobId SubmitAfter(const Ref<IJob>& job, Time::Milliseconds delay, JobPriority = Normal);
    bool  RequestCancel(JobId);
    bool  Retry(JobId, JobPriority = Normal);
    bool  RemoveFromHistory(JobId);

    bool  PauseAllJobs();  bool ResumeAllJobs();
    bool  SafeSetThreadCount(std::size_t newCount, std::string& outMessage); // bezpieczna zmiana w locie

    std::optional<JobSnapshot> GetJob(JobId) const;
    std::vector<JobSnapshot>   GetAllJobs() const;
    std::vector<JobSnapshot>   GetActiveJobs() const;
    std::vector<JobLogEntry>   GetLogs(JobId) const;

    void RegisterPreExecuteHook(PreExecuteHook hook); // np. wymuszenie affinity/kontekstu przed Execute()
};
```

**Jak używać (szkic fitu w tle):**

```cpp
class FitJob final : public IJob {
public:
    FitJob(FitConfig cfg) : m_Cfg(std::move(cfg)) {}
    std::string GetName() const override { return "Fit: " + m_Cfg.characteristicName; }
    std::string GetType() const override { return "jfm.fit"; }

    void Execute(JobContext& ctx) override {
        ctx.SetStage("Nelder-Mead");
        for (int i = 0; i < maxIter; ++i) {
            ctx.ThrowIfCancellationRequested();      // kooperacyjny cancel
            ctx.SetProgress(float(i), float(maxIter));
            // ... krok simpleksu ...
        }
        ctx.SetMessage("chi2_reduced = " + std::to_string(result.reducedChi2));
    }
private:
    FitConfig m_Cfg;
};

JobId id = jobSystem.Submit(CreateRef<FitJob>(cfg), JobPriority::Normal);
```

**Zdarzenia (`JobEvents.hpp`):** `JobQueuedEvent`, `JobStartedEvent`, `JobProgressEvent`,
`JobCompletedEvent`, `JobCancelledEvent`, `JobFailedEvent` — publikowane automatycznie przez
`JobSystem` na `EventBus`; do tego "requested"-warianty (`JobSubmitRequested`,
`JobCancelRequested`, `JobRetryRequested`...) pozwalające UI zlecać operacje na jobach
wyłącznie przez zdarzenia, bez trzymania referencji do `JobSystem`.

**Uwagi:** hierarchia zadań (`parentId`) i `SubmitJob`/`SubmitJobSequential` z poziomu
`JobContext` dają gotowy wzorzec na **Monte Carlo jako drzewo zadań** (jeden nadrzędny "MC job"
zarządzający N potomnych fitów) — dokładnie to, czego wymaga
`JFM_functional_specification.md` §12 (MC dla wielu charakterystyk, wiele niezależnych runów).
Domyślna liczba wątków roboczych to `RuntimeTuning::JobsDefaultWorkerThreads = 5`
(nadpisywalne w konfiguracji).

---

### 2.4 ProgressTrackingSystem (`Core/ProgressTrackingSystem/ProgressTracker.hpp`)

**Rola:** czysty **słuchacz** zdarzeń `Job*Event` z `EventBus`, utrzymujący żywą mapę
`JobId → ProgressEntrySnapshot` do odpytywania przez UI (paski postępu, panel zadań). Nie zna
`JobSystem` bezpośrednio — komunikacja wyłącznie przez zdarzenia, co czyni go w pełni odłączalnym.

```cpp
class ProgressTracker {
public:
    explicit ProgressTracker(Ref<EventBus> eventBus = {});
    Result<ProgressEntrySnapshot>              GetSnapshot(JobId) const;
    Result<std::vector<ProgressEntrySnapshot>> GetAllSnapshots() const;
    Result<std::vector<ProgressEntrySnapshot>> GetActiveSnapshots() const;
    Result<bool> RemoveEntry(JobId);
};
```

`ProgressEntrySnapshot` niesie m.in. `status`, `priority`, `totalWork`/`completedWork`,
`currentStage`, `currentMessage`, `errorSummary`, `finished` — kompletny zestaw do wyrenderowania
panelu postępu bez odpytywania samego `JobSystem`. W `Presentation/Panels/TaskMonitorWindow.hpp`
i `ProgressMonitorWindow.hpp` istnieją już gotowe, generyczne panele konsumujące dokładnie ten
typ (patrz sekcja 4.3) — bezpośrednio użyteczne dla JFM (fit/MC progress).

---

### 2.5 Commands (`Core/Commands/*`)

**Rola:** command pattern spinający wykonanie, undo/redo, uprawnienia (capabilities) i UI
(paleta komend, skróty klawiszowe) w jedną, spójną ścieżkę. To jest **jedyna** akceptowana
droga wykonania jakiejkolwiek zmiany stanu w aplikacji — bezpośrednie mutacje z pominięciem
komend nie są przewidziane wzorcem (i to jest przedmiotem ADR-001, patrz §6.1).

```cpp
class ICommand {
public:
    virtual Result<void> Execute(CommandContext&) = 0;
    virtual Result<void> Undo(CommandContext&);     // domyślna implementacja istnieje
    virtual Result<void> Redo(CommandContext&);
    virtual std::string  Description() const = 0;
    virtual bool IsUndoable() const noexcept;
    virtual bool CanMerge(const ICommand& next) const noexcept;   // np. scalanie wpisów tekstowych
    virtual Result<void> Merge(std::unique_ptr<ICommand> next);
};

enum class CommandFlags : uint32_t {
    None = 0, Undoable = 1<<0, AsyncCapable = 1<<1,
    HiddenFromPalette = 1<<2, NonUndoableBarrier = 1<<3
};

struct CommandMeta {
    CommandID id; std::string name, category, description;
    std::vector<CapabilityID> requiredCapabilities;   // sprawdzane przed Execute()
    CommandFlags flags = CommandFlags::None;
};
```

`CommandContext` to typowany, `std::any`-owy worek klucz→wartość (`Set<T>`/`TryGet<T>`/
`GetCopy<T>`) do przekazania parametrów wykonania (np. wybranej charakterystyki, nowego zakresu
dopasowania) bez rozrastania sygnatury `Execute()`.

`CommandRegistry` rejestruje `{CommandMeta, CommandFactory}`, waliduje wymagane capabilities
przez `CapabilityService` przed wykonaniem, i **jeśli komenda jest undoable, sam wypycha ją na
`UndoStack`** po udanym `Execute()`. `CommandService` jest cienką fasadą nad
`{Registry, UndoStack, CapabilityService}` opisaną w kodzie wprost jako *"jedyna ścieżka
wykonania komendy"*.

```cpp
registry.Register(
    CommandMeta{CommandID{"jfm.fit.run"}, "Run Fit", "Fitting", "Uruchamia dopasowanie",
                {}, CommandFlags::AsyncCapable},
    [](CommandContext& ctx) -> Unique<ICommand> {
        auto charId = ctx.GetCopy<std::string>("characteristicId");
        return CreateUnique<RunFitCommand>(charId.Value());
    });

Result<CommandOutcome> outcome = commandService.Execute(CommandID{"jfm.fit.run"}, ctx);
```

`CommandPaletteIndex` (`CommandPalette.hpp`) daje fuzzy-search po zarejestrowanych komendach +
listę ostatnio użytych + rozwiązywanie skrótu klawiszowego przez `KeymapResolver` — to jest
bezpośrednie zaplecze pod paletę komend widoczną w UI (biblioteka `imgui-command-palette` jest
zvendorowana pod to właśnie).

`SystemCommands.hpp` daje gotowe wbudowane komendy: `UndoCommand`, `RedoCommand`,
`OpenCommandPaletteCommand`, `QuitCommand`, `SaveProjectCommand` — ten ostatni od razu
odpowiada koncepcji "Projekt" z `JFM_functional_specification.md` §3.1 (chociaż dziś tylko
publikuje zdarzenie `ProjectSaveRequested`, bez realnej logiki zapisu — patrz sekcja 8).

---

### 2.6 Undo (`Core/Undo/UndoStack.hpp`)

**Rola:** klasyczny stos undo/redo operujący na `std::unique_ptr<ICommand>`, z grupowaniem
wielu komend w jedną pozycję historii (`BeginGroup`/`EndGroup`/`CancelGroup`, oraz RAII-owy
`UndoScope`/`ScopedGroup`) i śledzeniem "czystości" (`IsClean`/`MarkClean`) do wskaźnika
niezapisanych zmian w UI.

```cpp
UndoScope scope = undoStack.ScopedGroup("Zmiana zakresu dopasowania");
// ... wykonaj kilka komend, każda PushExecuted trafia do grupy ...
scope.Commit();   // albo scope.Cancel() by wycofać wszystko z powrotem
```

Zapytania stanu: `CanUndo()/CanRedo()`, `GetUndoDepth()/GetRedoDepth()`,
`GetUndoDescription()/GetRedoDescription()` (do etykiet w menu "Undo: Zmiana zakresu...").
Bezpośrednio realizuje potrzebę cofania tuningu z `JFM_functional_specification.md` §11.3
("cofnięcie zmian tuningu").

---

### 2.7 Notifications (`Core/Notifications/*`)

**Rola:** powiadomienia użytkownika (toasty) + historia, odsprzęgnięte od źródła przez `EventBus`.

```cpp
struct Notification {
    NotificationSeverity severity;  // Info, Warn, Error, Critical
    NotificationCategory category;  // General, Project, Import, Export, JobSystem,
                                     // Parsing, UI, Scripting, Rendering, Capability, Config
    std::string title, message, source;
    Time::TimePoint timestamp; uint32_t timeoutMs = 4000; bool pinned = false;
};

class Notifier {   // strona nadawcza — wygodne API
public:
    void Info(std::string title, std::string message, NotificationCategory = General);
    void Warning(...); void Error(...); void Critical(...);
    // + nasłuchuje NotificationRequestedEvent, więc dowolny kod może "zawiadomić"
    //   samą publikacją zdarzenia, bez trzymania referencji do Notifier
};

class NotificationCenter {  // strona odbiorcza — dla UI (np. biblioteki ImGuiNotify)
public:
    std::size_t RegisterListener(std::function<void(const Notification&)>);
    const std::vector<Notification>& GetNotifications() const;
};

class NotificationHistory { // filtrowanie: FilterBySeverity/ByCategory/BySource/ByTimeRange
};
```

Kategoria `JobSystem`/`Parsing`/`Import`/`Export` już istnieje — JFM potrzebuje tylko dodać
swoje własne wartości (`Fitting`, `MonteCarlo`) albo reużyć `General`/`JobSystem`.

---

### 2.8 Capabilities (`Core/Capabilities/*`)

**Rola:** rejestr flag możliwości (build-time / runtime-detected / policy), używany przez
`CommandRegistry` do blokowania komend niedostępnych w danej konfiguracji (np. brak GPU,
brak Pythona, tryb offline).

```cpp
enum class CapabilityCategory { BuildTime, RuntimeDetected, Policy };
struct CapabilityEntry { std::string name; CapabilityCategory category; bool available; std::string description; };

class CapabilityService {
public:
    void RegisterCapability(CapabilityEntry);
    void LockAfterStartup();              // po starcie nikt już nie zmieni zestawu capability
    bool IsAvailable(const std::string& name) const;
    Result<void> Require(const std::string& name) const;  // zwraca StructuredError gdy brak
};
```

Dla JFM: `RegisterCapability({"gpu.cuda", RuntimeDetected, cudaAvailable, "..."})` raz przy
starcie, potem `requiredCapabilities = {"gpu.cuda"}` w `CommandMeta` komendy "Run GPU Fit" —
mechanizm gotowy, wystarczy podłączyć rzeczywistą detekcję CUDA (dziś nieobecną w tym repo).

---

### 2.9 Assets (`Core/Assets/AssetManager.hpp`)

**Rola:** rozwiązywanie "ścieżki logicznej" (`"fonts/default.ttf"`) na rzeczywistą ścieżkę na
dysku, z dwoma korzeniami (`defaultRoot` — zasoby wbudowane, `userOverrideRoot` — nadpisania
użytkownika) i raportem walidacji wykrywającym brakujące zasoby krytyczne.

```cpp
AssetManager assets(Path{"assets"}, Path{"userdata/assets"});
assets.RegisterAsset({"models/jfm_defaults.yaml", AssetType::Config, AssetCriticality::Critical});
Result<ResolvedAsset> resolved = assets.ResolvePath("models/jfm_defaults.yaml");
AssetValidationReport report = assets.ValidateRegisteredAssets();
if (report.HasCriticalFailures()) { /* ... */ }
```

Dla JFM: naturalne miejsce na domyślne konfiguracje modeli, presety MC, szablony eksportu.

---

### 2.10 Logging (`Core/Logging/*`)

**Rola:** cienka nakładka na **spdlog**, plus w pamięci trzymany `LogRegistry` (do 2000 wpisów)
zasilający panel logów w UI (`Presentation/Panels/LoggingPanel.hpp`), oraz `SpdlogEventSink`,
który — jak nazwa wskazuje — publikuje wpisy logu jako `LogEvent` na `EventBus`, spinając
logging z resztą architektury zdarzeniowej.

```cpp
Logger::Initialize({.level = LogLevel::Info, .logToFile = true,
                     .logFilePath = Path{"logs/App.log"}, .logRegistry = registry});
DS_LOG_INFO("Fit zakończony: chi2={}", result.reducedChi2);
DS_LOG_ERROR("Import nieudany: {}", error.userMessage);
```

`LogCategory` ma niemal identyczny zestaw wartości co `NotificationCategory` — kolejny sygnał,
że te dwa systemy (log + notification) są projektowane razem i JFM powinien traktować je
spójnie (błąd fitu → jednocześnie log i notification).

---

### 2.11 Input (`Core/Input/*`) — skróty klawiszowe w stylu VS Code

**Rola:** kompletny system bindowania klawiszy z wyrażeniami kontekstowymi ("when clauses"),
niezależny od ImGui/GLFW.

```cpp
enum class KeymapLayer : uint8_t { Global, Project, WindowLocal };
struct KeyBinding {
    std::string id; KeyChord chord; CommandID commandId;
    ContextExpr when;      // np. "characteristicSelected && !fitRunning"
    KeymapLayer layer; bool enabled;
};

ContextManager ctx;
ctx.SetActive("characteristicSelected", true);

KeymapResolver resolver;
resolver.RegisterBinding({"jfm.fit.run", KeyChord{KeyCode::F5}, CommandID{"jfm.fit.run"},
                           ContextExpr{"characteristicSelected"}, KeymapLayer::Project, true});

KeyInputProcessor processor(resolver, ctx);
Result<KeyInputResult> result = processor.HandleKeyPressed(KeyChord{KeyCode::F5});
if (result && result->handled) commandService.Execute(*result->commandId);
```

`KeymapResolver` sam wykrywa konflikty (`GetConflicts()`) przy rejestracji — przydatne przy
JFM, gdzie zakres skrótów (lewa/prawa granica dopasowania na wykresie, patrz
`JFM_functional_specification.md` §6.2) będzie się rozrastał.

---

### 2.12 `Layer` / `LayerStack` (`Core/Layer.hpp`, `Core/LayerStack.hpp`)

**Rola:** szkielet aplikacji w stylu Hazel — `Layer` z `OnAttach/OnDetach/OnUpdate/OnEvent/
OnImGuiRender`, `LayerStack` z rozróżnieniem "layer" (zwykła warstwa, wstawiana przed granicą
overlayów) i "overlay" (zawsze na wierzchu, np. debug UI). Wyszukiwanie po nazwie, po `LayerId`
albo po typie (`FindLayer<TLayer>()` przez `dynamic_cast`).

To jest system, na którym stoi `CoreLayer` (sekcja 2.13) — każdy przyszły moduł domenowy
(JFM) najprawdopodobniej będzie się wpinał w aplikację jako własna `Layer`, dokładnie tak jak
dziś robi to `DomainLayer`/`RendererLayer`/`ScientificRuntimeLayer`.

---

### 2.13 `CoreLayer` (`Core/CoreLayer.hpp`) — agregator

**Rola:** jedyna warstwa (`Layer`), która spina większość systemów Core w jedno miejsce
tworzone i posiadane przez `Application`: `EventBus`, `JobSystem`, `ProgressTracker`,
`UndoStack`, `CommandRegistry`, `CommandService`, `KeymapResolver`, `ContextManager`, oraz
metodę pomocniczą `createSaveProjectCommand`. To jest, z całego repozytorium, **najbliższy
kandydat do stania się dosłownie zawartością przyszłej biblioteki `Core`** — już dziś ma
kształt "wszystko, czego potrzebuje dowolna domena, w jednym miejscu, bez wiedzy o Renderer/
Domain/ScientificRuntime".

---

### 2.14 Threading (`Core/Threading/ThreadAffinity.hpp`)

**Rola:** znacznik/sprawdzenie wątku głównego — mechanizm egzekwujący politykę z ADR-010
("UI/OpenGL tylko na main thread").

```cpp
namespace DefectStudio::Threading {
    void SetMainThread();          // wołane raz, na starcie, z wątku UI
    bool IsMainThread();
    std::thread::id GetMainThreadId();
}
#define ASSERT_MAIN_THREAD() DS_ASSERT(Threading::IsMainThread(), "Expected main thread")
```

---

### 2.15 Platform (`Core/Platform/*`)

**Rola:** cienka warstwa nad systemem operacyjnym, z podkatalogami `Windows/`, `Linux/`,
`MacOS/` dla implementacji specyficznych. Kluczowe elementy dla JFM:

```cpp
// ProcessRunner.hpp — uruchamianie procesów zewnętrznych z timeoutem i cancel
struct ProcessRunOptions { Path executable; std::vector<std::string> arguments;
                            Path workingDirectory; std::chrono::milliseconds timeout;
                            std::function<bool()> shouldCancel; };
struct ProcessRunResult  { int exitCode; std::string commandLine, standardOutput, standardError;
                            bool timedOut, cancelled, terminated; };
Result<ProcessRunResult> RunProcess(const ProcessRunOptions&);
```

To jest **bezpośrednio to, czego potrzebuje `JFM_functional_specification.md` §4.1/§5.2** dla
"niezależnych skryptów Pythonowych" importu/preprocessingu/prefit — gotowy, przetestowalny
mechanizm uruchamiania zewnętrznego procesu z limitem czasu i możliwością anulowania, bez
pisania niczego od nowa.

Pozostałe elementy: `PlatformWindow` (inicjalizacja okna GLFW, ikona), `PlatformPaths`
(`GetSystemFontDirectories`), `PlatformFontDiscovery` (skan czcionek systemowych/lokalnych),
`PlatformPythonRuntime` (`GetAppPythonRoot`, `ResolveEmbeddedPythonHome`,
`SetPythonHomeEnvironment` — rozwiązywanie środowiska Python embedded/systemowego),
`PlatformSystem` (`DebugBreak`, `InstallNativeCrashHandler` ze stack trace).

---

### 2.16 Configuration (`Core/Configuration/ConfigProfile.hpp`)

**Rola:** minimalna dziś (`ConfigProfileEntry{name, path}`) — realna logika configu (profile,
YAML) leży w `App/Managers/ConfigManager` i `App/Serialization/*` (patrz sekcja 3.3); w Core
zostaje tylko typ opisujący profil.

---

### 2.17 `Demo/` — żywa dokumentacja użycia Core

`src/Demo/*` (11 plików, osobna `Layer`) to zestaw działających przykładów: `EventBusDemo`,
`EventBusPublisherDemo`, `EventBusSubscriberDemo`, `EventDispatcherDemo`, `JobSystemDemo`,
`DemoCommands`, `DemoNotifications`, `DemoCapabilities`, `DemoBackendRuntime`. To jest
praktycznie gotowy, uruchamialny poradnik "jak używać EventBus/JobSystem/Commands" — zamiast
zgadywać wzorzec użycia z samych nagłówków, warto zacząć od przeczytania (i uruchomienia)
tej warstwy. Rekomendacja praktyczna: **pierwszy przystanek dla kogoś zaczynającego pracę nad
JFM-jako-modułem powinien być `src/Demo`, nie `src/Core`.**

---

## 3. `App` — dzisiejszy composition root

To jest sekcja o **największym znaczeniu praktycznym** dla planowanego podziału Core+moduły —
to tutaj, konkretnie, Core i logika domenowa są dziś zrośnięte.

### 3.1 `Application` — pojedynczy, konkretny singleton

`src/App/Application.hpp` + `ApplicationInternals.hpp` + `ApplicationLifecycle.hpp` +
`ApplicationState.hpp` (rozbite na kilka plików, ale to jedna logiczna całość) definiują klasę
`Application` z dostępem globalnym przez `Application::Get()`. Tworzona jest raz, w `main.cpp`,
przez `Application::Create(argc, argv)`, i to ona — nie `CoreLayer` — jest właścicielem
najwyższego poziomu:

- `LayerStack` (i przez nią: `CoreLayer`, `DomainLayer`, `RendererLayer`, `ScientificRuntimeLayer`,
  `ImGuiLayer`/`EditorLayer`, `IOLayer`, `StorageLayer`, `DebugLayer`, `DemoLayer`)
- `Window` (opakowanie GLFW)
- `ConfigManager` + profile konfiguracji
- Cyklu życia: inicjalizacja, pętla `Run()`, zamknięcie

**Kluczowy fakt architektoniczny:** `Application` nie jest bazą do podklasowania ani fabryką
wybieraną z zewnątrz. Nie ma odpowiednika Hazelowego:

```cpp
// To NIE istnieje w Defect-Studio-mgr:
extern Application* CreateApplication(int argc, char** argv);  // implementowane przez klienta
```

Zamiast tego `Application::Create()` **sam, wewnętrznie**, decyduje, co ma zbudować — łącznie
z konkretnymi typami domenowymi (`DomainLayer`, `RendererLayer`). Żeby dodać drugą aplikację
(JFM) korzystającą z tego samego Core, dziś jedyną ścieżką byłoby skopiowanie/rozgałęzienie
`Application`, a nie podanie jej nowego zestawu warstw z zewnątrz.

### 3.2 `ApplicationBootstrap.cpp` i `RendererStartupComposer` — dokładne miejsce zrośnięcia

`ApplicationBootstrap.cpp` (plik istnieje tylko jako `.cpp`, wywoływany wewnętrznie z
`Application`/`ApplicationLifecycle`) zawiera sekwencję inicjalizacji: Logger → CoreLayer →
AssetManager → ScientificRuntimeLayer → DomainLayer → RendererLayer → Presentation.

Najbardziej wymowny pojedynczy sygnatura w repozytorium to:

```cpp
// App/RendererStartupComposer.hpp
RendererStartupComposition ComposeRendererStartup(
    AssetManager& assetManager,
    ScientificRuntimeLayer* scientificRuntimeLayer,   // <- konkretny typ domenowy
    DomainLayer* domainLayer);                         // <- konkretny typ domenowy
```

Funkcja generycznego (App-owego) bootstrapu przyjmuje **wskaźniki na konkretne typy domenowe**
Defect Studio wprost w sygnaturze. To jest jedno zdanie kodu, które najlepiej tłumaczy, dlaczego
"Core i Domain/UI się dziś przekrywają jako jeden exe" — nie chodzi o pojedynczy duży plik, tylko
o to, że sam proces uruchamiania aplikacji ma domenę wpisaną w typy parametrów, a nie za
interfejsem/punktem rozszerzenia.

### 3.3 `ConfigManager` + `Serialization` (YAML)

`App/Managers/ConfigManager.hpp` + `ConfigProfileStore.hpp` zarządzają profilami konfiguracji
(pliki, prawdopodobnie wielo-profilowe ustawienia UI/renderer/joby — `RuntimeTuning.hpp` z
sekcji 2 pokazuje pełny zestaw strojonych wartości: rozmiar/pozycja okna, skala fontu UI, liczba
wątków JobSystem, pojemność `EventQueue`).

`App/Serialization/*` implementuje kodek YAML przez **interfejs**, nie konkretną klasę wprost:

```cpp
class IYamlCodec { /* ... */ };            // abstrakcja formatu
class YamlCodecFacade;                     // fasada nad yaml-cpp
class YamlConfigSerializer;                // (de)serializacja ApplicationConfig
class YamlRendererConfigSection;           // sekcja configu specyficzna dla renderera
```

To jest jeden z niewielu miejsc w `App`, gdzie separacja generyczne/domenowe już istnieje
(`IYamlCodec` jako generyczny interfejs, `YamlRendererConfigSection` jako jawnie wydzielona
sekcja domenowa) — dobry, już obecny wzorzec do powielenia przy wydzielaniu configu JFM.

### 3.4 `Window`

`App/Window.hpp`/`Window.cpp` opakowuje GLFW + `Core/Platform/PlatformWindow`
(inicjalizacja/ikona okna) w jeden obiekt trzymany przez `Application`. Generyczne, ale dziś
tworzone i posiadane bezpośrednio przez `Application`, nie przez `CoreLayer`.

---

## 4. Warstwy specyficzne dla Defect Studio

Ta sekcja jest celowo krótsza niż sekcja 2 — to jest kod, którego JFM **nie przejmie wprost**
(dane i logika krystalografii), ale zrozumienie jego kształtu jest potrzebne, żeby wiedzieć,
gdzie dziś przebiega (niepisana) granica Core/Domain i które fragmenty warto **wzorcowo**
powielić.

### 4.1 Domain (`src/Domain/*`) — 🔴 domena krystalografii, z wyjątkiem kształtu

Zaskakująco mała warstwa (12 plików): `Crystal/CrystalPrimitives.hpp`, `CrystalStructure.hpp`,
`ElementProperties.hpp` (+ zdarzenia), oraz `DomainLayer` + `ProjectWorkspace`:

```cpp
class DomainLayer final : public Layer {
public:
    ProjectWorkspace& Workspace() noexcept;
private:
    ProjectWorkspace m_Workspace;
};

using StructureId = std::string;
struct StructureRecord { StructureId id; CrystalStructure structure; Path sourcePath; std::string displayName; };

class StructureRegistry {
public:
    const StructureRecord& Add(CrystalStructure, Path sourcePath = {}, std::string displayName = {});
    const StructureRecord* Find(const StructureId&) const;
    const std::deque<StructureRecord>& Records() const noexcept;
};

class ProjectWorkspace {
public:
    StructureRegistry& Structures() noexcept;
};
```

**Ocena dla JFM:** `CrystalStructure`/`ElementProperties` to czysto krystalograficzne dane — 🔴,
nie przenosi się. Natomiast **kształt** `DomainLayer : Layer` posiadającej jeden rejestr danych
domenowych (`ProjectWorkspace` → `StructureRegistry`) jest dokładnie tym wzorcem, którego JFM
potrzebuje dla własnego `ProjectWorkspace` → rejestru `Characteristic`/`Series` z
`JFM_functional_specification.md` §3 — 🟡 wzorzec wart powielenia 1:1 (rejestr z ID, `Add`/`Find`,
kontener `std::deque` dla stabilnych referencji przy wzroście).

### 4.2 Renderer (`src/Renderer/*`) — 🔴 renderowanie 3D, z częściowym wzorcem

40 plików: backend OpenGL (`OpenGlFrameBuffer`, `OpenGlRendererBackend`, `OpenGlShaderLibrary`
+ katalog `Shaders/`), dane specyficzne dla sceny krystalicznej (`AtomStyleTable`,
`RendererMeshData`, `StructureRendererDataBuilder` — zamienia `CrystalStructure` na siatkę do
renderowania), kamera (`RendererViewCamera`), oraz integracja z Commands
(`RendererCommandRegistration`, `RendererViewportCommands`, `SetCameraViewCommand` — komendy
kamery przechodzące przez ten sam `CommandRegistry` co wszystko inne).

**Ocena dla JFM:** JFM nie renderuje scen 3D — potrzebuje wykresów 2D (I(V), Arrhenius, itp.),
prawdopodobnie przez ImPlot, nie przez własny backend OpenGL. Cały `Renderer/` to 🔴. Wzorcowo
🟡 wartościowe jest jednak to, *jak* komendy kamery są rejestrowane przez `RendererCommandRegistration`
— dobry przykład "moduł domenowy dokłada własne komendy do wspólnego `CommandRegistry` przy
starcie", zamiast trzymać osobny rejestr.

### 4.3 Presentation (`src/Presentation/*`) — mieszane: generyczna infrastruktura + panele domenowe

To najbardziej "mieszana" warstwa w repozytorium:

- `ImGuiLayer` — inicjalizacja kontekstu ImGui/docking/backend OpenGL3+GLFW — 🟡 wzorzec
  (implementacja specyficzna dla stosu graficznego Defect Studio, ale JFM prawdopodobnie użyje
  identycznego stosu ImGui+ImPlot+GLFW, więc realistycznie bliżej 🟢 niż czysto 🟡)
- `EditorLayer`, `EditorUiState` — układ głównego okna edytora — 🔴 (konkretny layout dla
  Defect Studio)
- `Panels/IPanel.hpp` + `PanelRegistry.hpp` — 🟢 **w pełni generyczna** infrastruktura paneli:

```cpp
class IPanel {
public:
    IPanel(std::string title, bool visibleByDefault = true);
    const std::string& GetTitle() const; void SetTitle(std::string);
    bool IsVisible() const; void SetVisible(bool);
    virtual Ref<IPanel> Clone() const = 0;
    virtual void Render() = 0;
};

class PanelRegistry {
public:
    template <typename TPanel, typename... Args> PanelId Add(Args&&...);
    WeakRef<IPanel> Get(PanelId); bool Remove(PanelId); PanelId Clone(PanelId source);
    std::vector<PanelId> GetIds() const;
};
```

  Ten interfejs jest domenowo pusty — JFM może rejestrować własne panele (`FitPanel`,
  `MonteCarloPanel`, `ArrheniusPanel`) w tym samym `PanelRegistry` bez żadnych zmian w jego
  kodzie.

- Konkretne panele — mieszane: `RendererPanel` (🔴, viewport 3D) obok w pełni generycznych
  `LoggingPanel` (🟢, konsumuje `LogRegistry`), `ProgressMonitorWindow`/`TaskMonitorWindow`
  (🟢, konsumują `ProgressTracker`/`JobSystem` — gotowe do pokazywania postępu fitów/MC bez
  zmian), `SettingsPanel`+`SettingsProfileManager` (🟡, logika generyczna, zawartość ustawień
  dziś specyficzna dla Defect Studio).

**Wniosek dla JFM:** warstwa Presentation nie jest monolitem "cały czerwony" — jej *szkielet*
(`IPanel`/`PanelRegistry`, `ImGuiLayer`) i kilka *konkretnych* paneli narzędziowych (logi,
postęp, zadania) są bezpośrednio reużywalne; tylko panele renderujące treść domenową (crystal
viewport ↔ przyszły panel wykresu I-V) trzeba napisać od nowa.

### 4.4 IO (`src/IO/*`) — głównie 🔴, jeden generyczny wyjątek

`AtomStyleIO`, `ElementPropertiesIO`, `PeriodicTableIO`, `RendererMeshIO`,
`RendererStartupLayoutIO` — wszystkie 🔴, formaty specyficzne dla Defect Studio.
`LogCsvIO` (eksport logów do CSV) i zwłaszcza `TextFileIO` — prosty, generyczny odczyt/zapis
plików tekstowych — 🟢, bezpośrednio użyteczne dla importu `.dat`/`.txt`/`.csv`/`.tsv`
wymaganego przez `JFM_functional_specification.md` §4.1 (choć bez parsowania kolumn — to trzeba
dopisać osobno).

### 4.5 ScientificRuntime (`src/ScientificRuntime/*`) — najbardziej wartościowa warstwa mieszana

To jest warstwa, w której proporcja reużywalności dla JFM jest **najwyższa** spośród
wszystkich warstw domenowych, bo JFM ma w specyfikacji wprost wymóg "zewnętrznych skryptów
Pythonowych" dla importu/preprocessingu/prefit (`JFM_functional_specification.md` §4.1, §5.2).

Generyczna infrastruktura embedded-Python (🟢): `PythonInterpreter` (cykl życia interpretera
CPython embedded), `PythonGilScope` (RAII zarządzanie GIL — bezpośrednio potrzebne, skoro
skrypty Pythona mają być wołane z zadań `JobSystem`, czyli z wątków roboczych),
`PythonErrors` (mapowanie wyjątków Pythona na `StructuredError`), `ScriptRunner` +
`PythonScriptJob` (**skrypt Pythona jako `IJob`** — czyli JobSystem + Python już są ze sobą
zintegrowane w tym repozytorium; to jest gotowy wzorzec na "prefit jako zewnętrzny skrypt
Python uruchamiany w tle" z §5.3 specyfikacji JFM), `ScriptBridgeUtils`,
`PythonBridgeBuildConfig`.

Domenowo specyficzne (🔴): `ASEBridge`, `PymatgenBridge`, `PymatgenConversion` — mosty do
bibliotek `ASE` (Atomic Simulation Environment) i `pymatgen`, obie z materiałoznawstwa/DFT,
zerowego znaczenia dla JFM.

### 4.6 Storage i Debug (`src/Storage/*`, `src/Debug/*`) — warstwy szkieletowe

Obie warstwy są dziś minimalne (po 3 pliki: `.hpp`/`.cpp` + prawdopodobnie plik testowy) —
`StorageLayer` i `DebugLayer` istnieją jako punkty zaczepienia w `LayerStack`
(`LayerId::Storage`, `LayerId::Debug`), ale nie mają jeszcze rozbudowanej treści. To są
warstwy, których kształt (a nie zawartość) można od razu przyjąć jako miejsce na przyszłą
funkcjonalność JFM (np. `StorageLayer` jako naturalne miejsce na persystencję Projektu z
`JFM_functional_specification.md` §3.1/§19, skoro nazwa i pozycja w `LayerId` już istnieją).

---

## 5. Testy

**Framework:** GoogleTest (vendorowany jako submoduł `Vendor/GoogleTest`).

**Struktura:** `tests/Core/{Assets,Commands,Input,JobSystem,ProgressTracking,Types,Undo}`,
`tests/Domain/Crystal`, `tests/IO`, `tests/Presentation`, `tests/Renderer`,
`tests/ScientificRuntime` (+ `fixtures/`) — łącznie 48 plików. Przykładowe pliki na poziomie
`tests/Core/` (nie w podkatalogu): `EventBusTests.cpp`, `AppLayerTests.cpp`,
`ApplicationConfigControllerTests.cpp`, `LoggerTests.cpp`, `LayerStackTests.cpp`.

**Build:** osobny target premake, `project "DefectStudioTests"` — **kompiluje ponownie cały
`src/**` (Core, App, Debug, Demo, Domain, IO, ScientificRuntime, Storage, Presentation, Renderer)
razem z `tests/**` i `gtest_main.cc`** w jeden, oddzielny executable (nie linkuje się do
głównego `DefectStudio` jako biblioteki — bo takiej biblioteki dziś po prostu nie ma). To jest
kolejny konkretny dowód praktycznej ceny braku podziału na jednostki budowania: **każda zmiana
w Core jest kompilowana dwa razy** (raz dla `DefectStudio`, raz dla `DefectStudioTests`), bo
nie ma wspólnej biblioteki `Core.lib`, do której oba targety mogłyby się linkować.

**Jak uruchomić:** wygenerować projekt przez premake5 (`tools/premake5.exe vs2022` lub
odpowiedni generator), zbudować target `DefectStudioTests`, uruchomić wynikowy executable
(standardowe GoogleTest CLI: `--gtest_filter=...`, `--gtest_list_tests` itd.).

**Wartość dla JFM:** testy Core (zwłaszcza `EventBusTests.cpp`, `tests/Core/JobSystem/*`,
`tests/Core/Undo`, `tests/Core/Commands`) są — obok `src/Demo` — drugim najlepszym źródłem
przykładów poprawnego użycia API opisanego w sekcji 2, bo pokazują też przypadki brzegowe
(np. `JobSystemResumeTests.cpp` dla wznawiania zapauzowanych zadań).

---

## 6. Co już wiedzą ADR-y o docelowej architekturze

Repozytorium ma 10 zaakceptowanych ADR (`docs/work/architecture/adr/ADR-001..010`, wszystkie
`Status: Accepted`, autor decyzji: "Project author"). Poniżej zestawienie z naciskiem na to, co
ma znaczenie dla pytania o podział Core+moduły; to jest wyłącznie **zestawienie faktów o
istniejących decyzjach**, nie propozycja ich zmiany.

| ADR     | Tytuł                                                | Istotne dla Core+moduły                                                                                                                                                                                                                                                                        |
| ------- | ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ADR-001 | Modular Domain Monolith                              | **Kluczowe.** Ustanawia jeden executable jako domyślną architekturę na obecnym etapie (patrz §6.1)                                                                                                                                                                                             |
| ADR-002 | Domain as Source of Truth and ECS Boundary           | Domain jest autorytatywnym źródłem prawdy; ECS (EnTT) tylko po stronie edytora/wizualizacji — zasada przenosi się wprost na JFM (dane fitów/MC w Domain, nie w strukturach renderera)                                                                                                          |
| ADR-003 | Python as Scientific Runtime                         | Python to "Scientific Runtime" za kontrolowaną granicą capability-based, nie pierwszoklasowy scripting; obiekty Pythona nie przekraczają granicy domain/presentation — bezpośrednio zgodne z modelem skryptów z `JFM_functional_specification.md`                                              |
| ADR-004 | Storage vs IO Split                                  | Rozdziela persystencję projektu (Storage) od surowej mechaniki plików/parserów/eksportu (IO) — wzorzec, który JFM powinien przejąć (import `.dat` = IO, zapis Projektu = Storage)                                                                                                              |
| ADR-005 | Single Executable with Selective Technical Libraries | **Kluczowe.** Elaboruje ADR-001: biblioteki wydziela się tylko selektywnie, gdy się to jednoznacznie opłaca (patrz §6.1)                                                                                                                                                                       |
| ADR-006 | TODO-First, Abstrakcje Gdy Potrzebne                 | Świadome unikanie przedwczesnej abstrakcji — kontekst: projekt rozwijany solo, z pomocą AI                                                                                                                                                                                                     |
| ADR-007 | Debug Layer and Capability Access                    | `DebugLayer` jako uprzywilejowana warstwa developerska + `CapabilityRegistry` jako centralny rejestr dostępności funkcji                                                                                                                                                                       |
| ADR-008 | Project Persistence and Recovery Contract            | Kontrakt zapisu Projektu (`format_version`, migracje, ścieżki względem `ProjectRoot`, autosave/recovery) — **opisany, ale nie w pełni zaimplementowany**: dzisiejszy `SaveProjectCommand` tylko publikuje zdarzenie `ProjectSaveRequested`, bez faktycznej logiki zapisu (patrz sekcja 8)      |
| ADR-009 | Domain Runtime Model and Scientific Entities         | `ProjectWorkspace` jako kontener runtime + rejestry per-encja + `UUID` jako tożsamość — **też szerszy niż dzisiejszy kod**: `StructureRegistry` używa dziś `std::string` jako ID, nie `UUID`, i nie ma jeszcze typów `Defect`/`DefectConfiguration`/`CalculationRecord`/`AnalysisRecord` z ADR |
| ADR-010 | Threading and GPU Compute Policy                     | Main-thread tylko dla UI/OpenGL, `JobSystem` dla reszty, model request/commit dla GPU, CUDA jako "future optional backend" — zero kodu CUDA w tym repo dziś (patrz sekcja 7)                                                                                                                   |

### 6.1 Napięcie ADR-001 / ADR-005 vs. cel "Core z `main()` + moduły"

To jest najważniejsza pojedyncza obserwacja tej sekcji, więc warto ją przytoczyć wprost.

**ADR-001** (2026-04-03) stwierdza, że DefectStudio pozostaje **jednym wykonywalnym plikiem** na
obecnym etapie, z modularnością wyrażoną przez wewnętrzne granice (namespace/katalogi/klasy
warstw), nie przez oddzielne jednostki budowania. Uzasadnienie w ADR wiąże się z etapem
projektu (jeden produkt, jeden zespół deweloperski, priorytet szybkości iteracji nad
elastycznością wdrożeniową).

**ADR-005** rozwija to wprost do tytułu "Single Executable **with Selective** Technical
Libraries" — wydzielanie osobnych bibliotek jest dopuszczalne, ale tylko punktowo, dla
komponentów o wyraźnie innym profilu technicznym (podany przykład: silnik GPU do korekcji FNV),
nie jako generalna zasada organizacji kodu.

**Konsekwencja:** cel, o którym mowa w obecnej rozmowie — jeden `Core` posiadający `main()` i
wiele modułów domenowych (JFM, Defect Studio, kolejne prototypy) importujących ten Core jako
bibliotekę, w stylu Hazel/`CreateApplication()` — jest **rewizją ADR-001 i ADR-005**, a nie ich
kontynuacją. Oba ADR były podjęte świadomie, w kontekście pojedynczej aplikacji Defect Studio;
przesłanka "wiele prototypów współdzielących jeden Core" (z `PROJECT_CONTEXT.md`) nie istniała
jeszcze w momencie ich akceptacji. To nie czyni ich błędnymi — czyni je **nieaktualnymi wobec
nowej przesłanki**, co w normalnym procesie ADR oznacza napisanie ADR, które je supersedes
(nadpisuje), a nie ciche obejście. Sama decyzja o superseded ADR-ów jest jednak poza zakresem
tego dokumentu (czysty opis + obserwacje, bez projektowania rozwiązania).