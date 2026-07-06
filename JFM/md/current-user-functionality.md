# JFM — Audyt funkcjonalności widocznej dla użytkownika

**Gałąź:** `workin-sjcm`  
**Commit:** `f69295a978e8a8d4d49939e4b7d83838c3f6fd76`   
**Data audytu:** 2026-06-26

---

## 1. Krótki obraz aplikacji

Junction Fit Master (JFM) to desktopowa aplikacja napisana w C++ z GUI opartym na Dear ImGui. Służy do wczytywania plików pomiarowych I-V (prąd–napięcie) urządzeń półprzewodnikowych, automatycznego dopasowywania krzywych do modeli diodowych i analizy wyników.

Typowy cel użytkownika: mieć plik `.dat` z charakterystyką, wybrać model (4-parametrowy lub 6-parametrowy), uzyskać dopasowane wartości I₀, A, Rₛ, Rₛₕ (i opcjonalnie α, Rₛₕ₂), a następnie analizować ich zależność od temperatury (wykresy Arrheniusa) i przeprowadzić analizę Monte Carlo do oceny niepewności.

Aplikacja uruchamia się jako jedno okno z układem dokowanych paneli. Cały wymagany stos numeryczny (estymacja, dopasowanie Neldera–Meada, Monte Carlo) jest zawarty wewnętrznie. Brak zewnętrznych narzędzi GUI poza Python+matplotlib wywoływanym przez `std::system()` przy jednej operacji zapisu.

---

## 2. Główny workflow użytkownika

1. **Uruchomienie** — aplikacja startuje w bieżącym katalogu roboczym; wyświetlane jest 5 paneli: Plot Area, Characteristic List, File Explorer, Characteristic Inspector, Arrhenius Plots.
2. **Nawigacja** — w panelu File Explorer użytkownik nawiguje do katalogu z plikami `.dat`, używając przycisku `<-` (cofanie) lub podwójnego kliknięcia na folder. (będzie to zastąpione prze native-file-dialogn oraz drag-and-drop)
3. **Konfiguracja ładowania** — użytkownik wybiera typ charakterystyki (Dark/Light) oraz opcjonalnie wymusza konkretny model.
4. **Wczytanie danych** — zaznacza jeden lub więcej plików (Ctrl+klik dla wielokrotnego wyboru) i klika `Load`. Alternatywnie `Load All` ładuje wszystko z aktualnego katalogu. Podwójne kliknięcie na plik natychmiast go ładuje.
5. **Automatyczne przetwarzanie** — po wczytaniu każdej charakterystyki aplikacja automatycznie:
   - wyznacza zakres dopasowania (przycina ujemne I, wykrywa rolloff),
   - estymuje wartości startowe (I₀, A, Rₛ, Rₛₕ) ze wzorów analitycznych,
   - uruchamia dopasowanie Neldera–Meada,
   - jeśli błąd > 1×10⁻⁴ i model był 4P, automatycznie przełącza na 6P i ponawia dopasowanie.
6. **Przegląd wyników** — dopasowane parametry widoczne są w Characteristic List; wykres z krzywą mierzoną i dopasowaną — w Plot Area.
7. **Interaktywna edycja** — użytkownik wybiera aktywną charakterystykę, modyfikuje zakres, model, parametry fixowane, wartości startowe i uruchamia ponowne dopasowanie.
8. **Tuning ręczny** — suwakami w Characteristic Settings przesuwa wartości parametrów i na żywo widzi krzywą tunowaną.
9. **Monte Carlo** — w Characteristic Inspector ustawia N iteracji i sigma, uruchamia symulację, wyniki wizualizuje w MC Inspector.
10. **Wykresy Arrheniusa** — w panelu Arrhenius Plots dodaje wykresy zależności parametrów od temperatury.
11. **Eksport** — zapisuje parametry do pliku CSV, dane MC do CSV, opcjonalnie zapisuje wykres MC przez skrypt Python.

---

## 3. Okna i elementy interfejsu

### Domyślnie widoczne przy starcie

| Okno | Typ | Opis |
|------|-----|------|
| `JFM` | Główne okno z menu | Kontener dla dockspace; zawiera menu `Views` |
| `Plot Area` | Lewy panel (80% szerokości) | Wykres I-V + lista aktywnych charakterystyk (prawa kolumna) |
| `Characteristic Settings` | Dolna część lewego panelu | Ustawienia wykresu, zakres dopasowania, przyciski Estimate/Fit, wyświetlanie parametrów, suwaków tuningowych |
| `Characteristic List` | Prawy górny panel | Tabela wszystkich załadowanych charakterystyk z parametrami |
| `File Explorer` | Prawy panel (za zakładką) | Przeglądarka plików z przyciskami Load/Load All |
| `Characteristic Inspector` | Prawy dolny panel | Konfiguracja fitting (fixed, bounds, MC) per charakterystyka |
| `Arrhenius Plots` | Prawy dolny panel (ta sama pozycja) | Wykresy temperaturowe parametrów |

### Domyślnie ukryte (dostępne przez menu Views)

| Okno | Opis |
|------|------|
| `MC Inspector` | Wizualizacja wyników Monte Carlo |
| `Generate` | Generator syntetycznych danych I-V |

### Menu Views (w oknie JFM)

Pozwala przełączać widoczność każdego z powyższych paneli.

---

## 4. Inwentaryzacja funkcjonalności

### 4.1 Przeglądanie i ładowanie danych

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Nawigacja w katalogu | Klik `<-` lub dwuklik na folder | Zmiana `currentPath`, odświeżenie listy | **Działa** | `FileSelector.cpp`: obsługa `<-` i double-click |
| Wybór pojedynczego pliku | Klik na plik | Zaznaczenie (toggle) | **Działa** | `FileSelector.cpp`: `m_selection[sel_index-1]` |
| Wielokrotny wybór | Ctrl+klik na pliki | Zaznaczenie wielu bez czyszczenia | **Działa** | `FileSelector.cpp`: `if (!ImGui::GetIO().KeyCtrl ...)` |
| Load wybranych | Klik `Load` | Wczytanie zaznaczonych plików `.dat` | **Działa** | `App.cpp: m_loadCallback` |
| Load all | Klik `Load All` | Wczytanie wszystkich plików z katalogu | **Działa** | `App.cpp: m_loadAllCallback` |
| Ładowanie przez dwuklik | Dwuklik na plik | Ładowanie bezpośrednie | **Działa** | `FileSelector.cpp`: `IsMouseDoubleClicked` |
| Wybór typu charakterystyki | Dropdown `Loading Characteristic Type` → Dark/Light | Zmiana `m_characteristicType` | **Działa częściowo** | `FileSelector.cpp`; path Light istnieje w `App.cpp:700`, ale modele 5P/7P niewidoczne (patrz §6) |
| Wymuszenie modelu przed ładowaniem | Dropdown `##ForcedModel` → None/Four Parameter/Six Parameter | `forcedModelID` ustawiony, omija auto-selekcję | **Działa** | `FileSelector.cpp`; `App.cpp:730` |

**Format pliku:** tab-separated, 2 lub 3 kolumny (V, I, opcjonalnie J). Temperatura wyciągana z nazwy pliku: `T` + 3 cyfry (np. `ivd_HZB25_T181_L0.dat` → T=181 K). Separatory: tab; obsługuje przecinki zamiast kropek dziesiętnych.

### 4.2 Wyświetlanie charakterystyk

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Wykres oryginalnej krzywej | Opcja `Plot Original` (domyślnie ON) | Krzywa mierzona I(V) | **Działa** | `PlotArea.cpp: ImPlot::PlotLine "Active"` |
| Wykres krzywej ranged | `Plot Ranged` ON | Podkreślona część w zakresie dopasowania + pionowe linie graniczne | **Działa** | `PlotArea.cpp: PlotInfLines` |
| Wykres krzywej fitowanej | `Plot Fitted` ON | Krzywa dopasowana kolorem `colorFitted` | **Działa** | `PlotArea.cpp: ch.fittedI` |
| Wykres krzywej tunowanej | `Plot Tunned` ON | Krzywa tunowana kolorem `colorTunned` | **Działa** | `PlotArea.cpp: ch.tunedI` |
| Hide non-active | Checkbox `Hide non-active` | Tylko aktywna charakterystyka na wykresie | **Działa** | `PlotArea.cpp: data.hideNonActive` |
| Skala logarytmiczna Y | Checkbox `Log Y` | Oś Y logarytmiczna z etykietami 10^n | **Działa** | `PlotArea.cpp: TFL/TFNL, SetupAxisTicks` |
| Skala logarytmiczna X | Checkbox `Log X` | Oś X logarytmiczna | **Działa** | `PlotArea.cpp` |
| Fit plot area | Przycisk `Fit plot area` | Przybliżenie do danych (AutoFit) | **Działa** | `PlotArea.cpp: xFlags |= ImPlotAxisFlags_AutoFit` |
| Kolor charakterystyki | Przycisk koloru w Characteristic List | Zmiana koloru krzywej | **Działa** | `CharacteristicList.cpp: ColorEdit4` |
| Gradient kolorów | `Update colors` po wyborze start/end | Interpolacja liniowa RGB po wszystkich charakterystykach | **Działa** | `App.cpp: m_updateColorsCallback` |

### 4.3 Wybór aktywnej charakterystyki

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Wybór z listy Plot Area | Klik na nazwę w listbox | `data.active` = wskazana charakterystyka | **Działa** | `PlotArea.cpp: ImGui::Selectable → data.active = &ch` |
| Checkbox widoczności | Checkbox przy nazwie w Characteristic List | Włącza/wyłącza `ch.checked` | **Działa** | `CharacteristicList.cpp` |
| Podświetlenie przy hover | Hover nad nazwą w liście | Pogrubienie krzywej na wykresie (weight=2) | **Działa** | `CharacteristicList.cpp: ch.weight = 2.0f` |
| Select all | Przycisk `Select all` | Wszystkie charakterystyki `checked=true` | **Działa** | `App.cpp: m_selectAllCallback` |
| Unselect all | Przycisk `Unselect all` | — | **Uszkodzona** | `CharacteristicList.cpp:43`: callback zakomentowany |
| Invert selection | Przycisk `Invert Selection` | Odwrócenie stanu `checked` | **Działa** | `App.cpp: m_invertSelectionCallback` |
| Remove selected | Przycisk `Remove selected` | Usuwa zaznaczone z listy | **Działa** | `App.cpp: std::erase_if(...checked)` |
| Remove unselected | Przycisk `Remove unselected` | Usuwa niezaznaczone | **Działa** | `App.cpp: std::erase_if(!checked)` |

### 4.4 Zakres dopasowania

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Automatyczny zakres (przy ładowaniu) | Automatyczny | Dolna granica: pominięcie ujemnych I i V<0,1; górna: wykrycie rolloff | **Działa** | `PreFitter.cpp: getLowerRange, getUpperRange` |
| Suwak `Up range` | Przeciągnięcie | Zmiana `dataRange.second`, wyzwala Estimate + Fit | **Działa** | `PlotArea.cpp`: SliderInt → m_estimateCallback + m_fitCallback |
| Suwak `Down range` | Przeciągnięcie | Zmiana `dataRange.first`, wyzwala Estimate + Fit | **Działa** | j.w. |

### 4.5 Wybór modelu

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Dropdown modelu (per ch.) w Char. Settings | Wybór z listy | Zmiana `act.modelID`, wyzwala Estimate + Fit | **Działa** | `PlotArea.cpp: BeginCombo "##CurrentModel"` |
| Dropdown modelu w Char. Inspector | Wybór z listy | Zmiana `ch.modelID` (wymaga Update) | **Działa** | `CharacteristicInspector.cpp` |
| Auto-selekcja 4P→6P przy ładowaniu | Automatyczny | Jeśli error > 1e-4 po 4P, przełącza na 6P | **Działa** | `App.cpp:739` |

**Dostępne modele (widoczne w UI):**
- **Four Parameter** (Model4P, ID=3): I₀, A, Rₛ, Rₛₕ — dark I-V
- **Six Parameter** (Model6P, ID=5): I₀, A, Rₛ, Rₛₕ, α, Rₛₕ₂ — dark I-V

Modele 5P i 7P (light I-V) są zaimplementowane, ale **nie są widoczne w dropdownie**, bo nie zostały zarejestrowane w `NumericsConfig` (patrz §6).

### 4.6 Estymacja parametrów startowych

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Automatyczna estymacja | Automatyczny (przy ładowaniu i zmianie zakresu) | Obliczenie Rₛₕ, Rₛ, A (przez derivative log(I)), I₀; korekcja A przez tablicę lookup | **Działa** | `PreFitter.cpp: estimate4PModel` |
| Przycisk `Estimate` | Klik | Przeliczenie wartości startowych dla aktywnej ch. + aktualizacja bounds | **Działa** | `App.cpp: m_estimateCallback` |

### 4.7 Edycja parametrów przed dopasowaniem

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Ustawianie wartości startowej (initial guess) | InputDouble w Inspector, kolumna `Initial Values` | Zmiana `ch.initialGuess[key]` | **Działa** | `CharacteristicInspector.cpp` |
| Toggle `Use initial guess` | Checkbox | `ch.useInitial` | **Działa** | j.w. |
| Fixowanie parametru (Inspector) | Checkbox + InputDouble w kolumnie `Fixed parameters` | Ustawia `fixedParameterIDs`, `fixedParametersValues` | **Działa** | `CharacteristicInspector.cpp` |
| Bounds (Inspector) | InputDouble min/max | Zmiana `ch.bounds` | **Działa** | j.w. |
| Toggle `Use bounds` | Checkbox | `ch.useBounds` | **Działa** | j.w. |
| Global config (Select All) | Checkbox `Select All` w Inspector | Tryb globalny — ustawia wszystkie ch. jednocześnie | **Działa** | `CharacteristicInspector.cpp: drawGlobalConfig` |
| Update (zapisanie zmian) | Przycisk `Update` | Przepisuje `saved*` ← bieżące wartości; bez tego zmiany nie trafiają do fittingu | **Działa** | `CharacteristicInspector.cpp` |
| Restore | Przycisk `Restore` | Przywraca `saved*` → bieżące | **Działa** | j.w. |

### 4.8 Dopasowanie

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Przycisk `Fit` | Klik | Uruchomienie Neldera-Meada dla aktywnej charakterystyki z bieżącą konfiguracją | **Działa** | `App.cpp: m_fitCallback → m_numerics->Fit` |
| Automatyczne fit przy ładowaniu | Automatyczny | Fit z estymowanymi wartościami startowymi | **Działa** | `App.cpp: m_loadSingleCharacteristic` |
| Wielokrotne iteracje fittingu | Automatyczny | Jeśli wynik ujemny lub error > 1: ponowny fit z losowymi perturbacjami (max 1 powtórzenie) | **Działa częściowo** | `JFMFitter.cpp: checkRepetitionCondition` — iterationCondition ≤ 1 oznacza max 2 próby |

**Algorytm:** Nelder-Mead simplex, zaimplementowany w bibliotece NumericStorm (lokalny vendor). Simplex inicjalizowany losowo z przeskalowanych bounds; parametr A (ideality factor) inicjalizowany z dystrybucją log-uniform (`LogDist`).

### 4.9 Wyświetlanie wyników dopasowania

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Tabela parametrów (Fitted / Tuned) | Pasywna | Dwa panele: lewostronny `fittedParameters`, prawostronny `tunedParameters` | **Działa** | `PlotArea.cpp: BeginTable "Parameters"` |
| Błąd dopasowania (fit error) | Pasywna | Wyświetlany w Characteristic List i jako `tuneError` w Characteristic Settings | **Działa** | `CharacteristicList.cpp: ch.fitError`; `PlotArea.cpp: "Tune Error: %e"` |
| Checkbox `Is Fitted` | Toggle | Ręczne zaznaczenie ch. jako dopasowanej | **Działa** | `PlotArea.cpp` |
| Parametry w Characteristic List | Pasywna | Kolumny Name, Error, T, + każdy parametr z nConfig | **Działa** | `CharacteristicList.cpp` |

### 4.10 Tuning parametrów

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Suwak wartości (mantissa 1–9,999) | Przeciągnięcie | Zmiana `tunedParameters[id]`, na żywo przeliczenie I(V) | **Działa** | `PlotArea.cpp: SliderFloat "_slider"` |
| Suwak potęgi (-20 do 20) | Przeciągnięcie | Zmiana wykładnika | **Działa** | `PlotArea.cpp: SliderInt "_power"` |
| Checkbox `Enable` (kolumna 2) | Toggle | Wybór, które parametry tunować | **Działa** | `PlotArea.cpp: Checkbox tempParametersActive` |
| Checkbox `Fixed` | Klik | Ustawia `fixedParameterIDs[id] = true` | **Działa (tylko SET)** | `PlotArea.cpp:515`: sekcja else zakomentowana → **nie można odpiąć** |
| Przycisk `Update characteristic` | Klik | Zamierzone: zastąpienie fitted → tuned dla zaznaczonych | **Prawdopodobnie uszkodzona** | `PlotArea.cpp:355`: `tunedParameters.clear()` przed odczytem z tunedParameters (patrz §6) |
| Przycisk `Reset Tunning` | Klik | Przywraca tuned = fitted, re-estimate + fit | **Działa** | `PlotArea.cpp:364` |

### 4.11 Monte Carlo

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Konfiguracja N, SaveN, Sigma | Inspector → MC Config per ch. | `ch.mcConfig` | **Działa** | `CharacteristicInspector.cpp` |
| `Perform MC on active` | Klik | Symulacja MC dla aktywnej ch. | **Działa** | `App.cpp: m_performMCCallback → m_numerics->Simulate` |
| `Perform MC on all` | Klik (gdy `Select All`) | Symulacja MC dla wszystkich zaznaczonych ch. | **Działa** | `App.cpp: m_performMCOnAllCallback` |
| Przeglądanie wyników w MC Inspector | Klik na symulację w drzewie | Ustawia `activeMC`, widoczna lista iteracji | **Działa** | `MonteCarloInspector.cpp` |
| Wykres scatter MC | Wybór X/Y parametrów + `Add plot` | Scatter plot kolorowany wg poziomów chi² (3 kolory: 1σ/2σ/3σ) | **Działa** | `MonteCarloInspector.cpp: drawPlot` |
| Zakładki MC | Przycisk `+` | Nowa zakładka dla grupy wykresów | **Działa** | `MonteCarloInspector.cpp` |
| `Plot all MC` | Klik | Generuje wykresy dla par (I₀,A), (Rₛₕ,Rₛ), (Rₛₕ,Rₛₕ₂) automatycznie | **Działa z ograniczeniem** | `App.cpp:1263`: wymaga `mcTabs.size() >= 3` (domyślnie jest 1); patrz §6 |
| `Clear All Plots` | Klik | Czyści `mcPlots` | **Działa** | `MonteCarloInspector.cpp` |
| `Clear Simulations` | Klik | Czyści `mcData` ze wszystkich charakterystyk | **Działa** | `MonteCarloInspector.cpp` |
| Usunięcie pojedynczego wykresu | `Remove` w oknie wykresu | Usuwa z `mcPlots` | **Działa** | `MonteCarloInspector.cpp: mc.remove(mc.id)` |

**Algorytm MC:** Paralelizacja przez `std::async` (12 wątków). Każda iteracja: dodanie szumu normalnego do I, nowe estimate, fit simplex, rejestracja parametrów i błędu. Coloring wg tablicy chi² (chi2vec, 6 poziomów DOF).

### 4.12 Wykresy Arrheniusa

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Dodanie wykresu | Wybór typu + `Add Plot` | Nowy wykres jako floating window w dockspace | **Działa** | `ArrheniusViewer.cpp: data.m_arrheniusPlotData.emplace_back` |
| Typy wykresów | Dropdown `Next-Plot` | A·ln(I₀), A, I₀, Rₛ, Rₛₕ, Rₛₕ₂, α | **Działa** | `PlotData.cpp: ArrheniusPlotSetting()` |
| Inversja temperatury | `Invert Temperature` toggle | Oś X = 1000/T zamiast T | **Działa** | `PlotData.cpp: inversionT` |
| Auto-fit obszaru | `Fit Plots` toggle | Zamierzone: AutoFit; ale flaga przypisana niepoprawnie | **Uszkodzona** | `ArrheniusViewer.cpp:19`: `flags | ImPlotAxisFlags_AutoFit` bez `|=` — wynik odrzucony |
| Kolor fitted/tuned | ColorEdit | Zmiana kolorów serii | **Działa** | `ArrheniusViewer.cpp` |

Dane na wykresach aktualizują się automatycznie co klatkę na podstawie bieżącego stanu wszystkich `checked` charakterystyk (oddzielne serie: Fit i Tunned).

### 4.13 Generator syntetyczny danych

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Wybór modelu | Dropdown Model | `m_genModelID` | **Działa** | `DataGenerator.cpp` |
| Zakres napięcia | DragFloat2 Voltage Range | `m_voltageGenRange` | **Działa** | j.w. |
| Liczba kroków / krok | RadioButton + InputScalar | `m_byStepN`, `m_nSteps`, `m_voltageGenStep` | **Działa** | j.w. |
| Temperatura | InputFloat Temperature | `m_genT` | **Działa** | j.w. |
| Szum (% normalny) | InputFloat Noise | `m_noise`, dolna norma względna | **Działa** | j.w. |
| Sweep parametrów (range gen.) | Checkbox per parametr + start/end/N | Generacja kombinatoryjna wszystkich wersji | **Działa** | `App.cpp: m_generateCallback` |
| Sweep temperatury | DragFloat2 Temp Range + T number | Kombinacja T × parametry × szum | **Działa** | j.w. |
| Typy dystrybucji parametrów | Dropdown: Linear, Log, Exponential, Per Decade | `GenType` | **Działa** | `App.cpp: generateRange` |
| `Generate single` | Klik | Jedna charakterystyka, auto-fit, dodana do listy | **Działa** | `App.cpp: m_singleShot` |
| `Generate range` | Klik | Wszystkie kombinacje, auto-fit każdej, dodane do listy | **Działa** | `App.cpp: m_generateCallback` |

### 4.14 Eksport danych

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Zapis parametrów do CSV | `Save Parameters` w Char. Settings | `<currentPath>/Analysis/parameters.csv` — Name, T, I₀, A, Rₛ, Rₛₕ, [α, Rₛₕ₂], [Iₛc dla Light] | **Działa** | `App.cpp: m_saveParametersCallback` |
| Zapis danych MC do CSV | `Save all MC Data` w MC Inspector | `<currentPath>/Analysis/MC/<name>_{one,two,three}_Sigma.csv` | **Działa** | `App.cpp: m_saveMCData → saveOneSimulation` |
| Zapis wykresu MC jako PNG | `Save` per wykres lub `Save all MC` | CSV tymczasowy → `python generate_image.py` → PNG; CSV kasowany | **Działa warunkowo** | `JFMFitting.cpp:173`: wymaga Python + matplotlib w PATH |
| Zapis niepewności MC | `Save Uncertainty` | CSV z poziomami 66%/95%/99% per parametr | **Prawdopodobnie uszkodzona** | `App.cpp:1228`: `for (int i=0;i<4;i++)` ale `uncertainty.resize(3)` — iterator poza zakresem |
| Zapis konfiguracji MC (YAML) | `Save MC config` | YAML z konfiguracją startową MC (bez wyników) | **Działa** | `App.cpp: m_saveMCConfCallback` |

### 4.15 Wczytywanie zapisanych danych MC

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Ładowanie pliku YAML z wynikami MC | `Load` pliku `.yaml` | Próba wczytania MCOutput i skojarzenia z charakterystyką | **Nie można potwierdzić** | `Loader.cpp: MonteCarloResultLoader`; mapowanie nazw modeli odwrócone vs Dumper (patrz §6) |

### 4.16 Zarządzanie stanem aplikacji

| Funkcja | Działanie użytkownika | Wynik | Status | Dowody w kodzie |
|---------|----------------------|-------|--------|-----------------|
| Persystencja layoutu okien | — | Brak; `io.IniFilename = nullptr` | **Nieosiągalna** | `App.cpp:53` |
| Zapis/odczyt stanu sesji | — | Brak mechanizmu zapisu/odczytu całego stanu aplikacji | **Nie istnieje** | — |

---

## 5. Komunikaty o błędach i statusie

Aplikacja **nie ma systemu komunikatów błędów widocznych dla użytkownika**. Błędy trafiają do:
- `std::cerr` (konsola) — np. przy błędzie otwarcia pliku: `"Error: Unable to open file "` / `"Error: Could not open file: "`.
- `std::cout` — status zapisu i ścieżek (np. `"File saved successfully to: "`, `"File deleted successfully"`).

Brak pasków statusu, dialogów błędów, ani powiadomień w GUI. Użytkownik nie jest informowany o błędach ładowania pliku, niepowodzeniu fittingu, ani braku Pythona przy SaveMCPlot.

Jedyna forma pośrednia: wartość `fitError = -1.0` w Characteristic List oznacza, że dopasowanie nie zostało jeszcze wykonane; błąd wyświetlany jest w formacie `%e`.

---

## 6. Funkcje częściowe lub uszkodzone

### 6.1 Charakterystyki „light" (IV pod oświetleniem) — częściowe

**Co istnieje:** Dropdown `Loading Characteristic Type` (Dark/Light) w File Explorer. Przy wyborze Light: aplikacja wyciąga wartość `ShortCircuitCurrent` (minimum |I| przy V≈0), przesuwa prąd o tę wartość, uruchamia `RangeData`. Fitter 5P (`FiveParameterFitter`) i 7P (`SevenParameterFitter`) są zaimplementowane. PreFittery 5P i 7P są zarejestrowane.

**Co nie działa:** Modele `Model4PLight` (ID=4) i `Model6PLight` (ID=6) **nie zostały dodane** do `NumericsConfig` w `Fitting::instantiateNumericsConfig()`. Dropdown modelu ich nie pokazuje. Kod ładowania Light (`App.cpp:703`) przypisuje `temp.modelID = ModelID::Model4P` (ID=3), nie 4PLight. Efektywnie light charakterystyki dopasowywane są 4-parametrowym modelem dark.

**Ślady:** `JFMFitting.cpp: createModel()` — brak wpisów dla `Model4PLight`/`Model6PLight`; `Todos.md`: lista TODO dla light.

### 6.2 Przycisk „Unselect all" — uszkodzony

Przycisk widoczny w Characteristic List. Callback `m_unselectAllCallback` jest zakomentowany w implementacji: `//data.m_unselectAllCallback();`. Kliknięcie nie robi nic.

**Ślady:** `CharacteristicList.cpp:43`.

### 6.3 Odpiecie fixowanego parametru w suwakach tuningowych — uszkodzone

Po zaznaczeniu `Fixed` przy suwaku parametru, można go ustawić. Sekcja `else` (odznaczenie) jest w całości zakomentowana (`/* else { ... } */`). Raz ustawiony fix w tym widoku nie może być cofnięty bez przejścia do Characteristic Inspector.

**Ślady:** `PlotArea.cpp:519–522`.

### 6.4 Przycisk „Update characteristic" — prawdopodobnie uszkodzony

Logika: `act.tunedParameters.clear()` (linia 355), a następnie w pętli `val = act.tunedParameters[key]` (linia 359) — odczyt z wyczyszczonej mapy daje zawsze 0. Zamierzone działanie (skopiowanie tylko zaznaczonych tunedParameters do fitted) nie działa poprawnie.

**Ślady:** `PlotArea.cpp:354–363`.

### 6.5 Arrhenius „Fit Plots" — efektywnie bezczynnne

Przycisk toggle `Fit Plots` zmienia flagę `settings.fitPotArea = true`, ale w `ArrheniusPlotData::Plot()` kod jest: `if (settings.fitPotArea) flags | ImPlotAxisFlags_AutoFit;` — wynik wyrażenia bitowego jest odrzucony; brakuje `|=`. AutoFit nigdy nie jest aktywowany.

**Ślady:** `PlotData.cpp: ArrheniusPlotData::Plot` (linia z `flags | ...`).

### 6.6 „Plot all MC" — działa tylko z 3+ zakładkami

`m_plotAllMC` iteruje przez `ids[idx]` gdzie `ids` ma rozmiar 3, a pętla idzie po `mcTabs.size()`. Domyślnie jest 1 zakładka. Przy 1 zakładce generuje tylko parę (I₀,A). Przy 2 zakładkach — (I₀,A) i (Rₛₕ,Rₛ). Dopiero przy 3+ zakładkach — pełny zestaw. Brak sprawdzenia rozmiaru.

**Ślady:** `App.cpp:1260–1280`.

### 6.7 „Save Uncertainty" — błąd przekroczenia zakresu

`u.uncertainty.resize(3)` (3 poziomy), ale pętla `for (int i=0;i<4;i++)` iteruje 4 razy. Przy `i=3` — dostęp poza zakresem wektora → niezdefiniowane zachowanie (crash lub garbage).

**Ślady:** `App.cpp:1223–1232`.

### 6.8 Ładowanie YAML wyników MC — mapowanie nazw odwrócone

`MonteCarloResultDumper` zapisuje `Model4PLight` jako `"FiveParameterModel"` i `Model6PLight` jako `"SevenParameterModel"` (patrz `Dumper.cpp:15–18`). `MonteCarloResultLoader` odwrotnie: `"FiveParameterModel"` mapuje na `Model6PLight` i `"SevenParameterModel"` na `Model4PLight` (patrz `Loader.cpp:158–161`). Roundtrip jest niepoprawny dla modeli light.

**Ślady:** `Dumper.cpp:15–18`; `Loader.cpp:155–161`.

### 6.9 SevenParameterModelPreFit — błąd wyciągania Isc

```cpp
int index = *std::ranges::find(input.characteristic.voltageData, 0);
```
`std::ranges::find` zwraca iterator; dereferencja daje `double`, nie indeks. To przypisuje wartość napięcia (0.0) do `index`, nie pozycję. Isc jest potem brane z `currentData[0]`.

**Ślady:** `PreFitter.cpp: SevenParameterModelPreFit::Estimate`.

### 6.10 Okna RT MC i RT Error — nieosiągalne

W `App.cpp` istnieje blok kodu wyświetlający dwa dodatkowe okna (`RT MC`, `RT Error`) z globalными zmiennymi `globalNoisyI`, `globalErrors`. Cały blok jest otoczony `/* ... */`. Okna niedostępne z GUI.

**Ślady:** `App.cpp` (komentarz blokowy między `Characteristic Inspector` a `MC Inspector`).

---

## 7. Elementy wymagające późniejszego odtworzenia

Poniżej lista funkcjonalności (bez architektury i implementacji), które muszą być zachowane lub odtworzone w nowym JFM:

1. **Wczytywanie pliku `.dat`** — tab-separated, 2–3 kolumny (V, I, opcjonalnie J), odczyt temperatury z nazwy pliku.
2. **Automatyczna detekcja zakresu dopasowania** — odcinanie ujemnych wartości I i niskich napięć u dołu; wykrywanie rolloff u góry.
3. **Automatyczna estymacja parametrów startowych** — analityczne wyciągnięcie Rₛₕ, Rₛ, A, I₀ z kształtu krzywej.
4. **Dopasowanie krzywej I-V** — Nelder-Mead (lub odpowiednik) z inicjalizacją simpleksu z bounds.
5. **Cztery modele** — dark 4P i 6P; light 5P i 7P (obecne w kodzie, docelowo do aktywowania).
6. **Automatyczna selekcja modelu 4P→6P** — próba 4P, fallback na 6P przy dużym błędzie.
7. **Interaktywne suwaki tuningu** — natychmiastowe przeliczenie krzywej przy zmianie dowolnego parametru; mantissa + wykładnik.
8. **Fixowanie parametrów** — wykluczenie wybranych parametrów z fittingu (i możliwość cofnięcia fix).
9. **Granice parametrów (bounds)** — ogólne (globalne) i per-charakterystyka; kontrola poprawności (min < max).
10. **Initial guess** — możliwość nadpisania estymacji ręcznymi wartościami startowymi.
11. **Symulacja Monte Carlo** — iteracyjne perturbowanie krzywą szumem normalnym + fit; wielowątkowość; konfiguracja N i sigma.
12. **Wizualizacja MC** — scatter plot z kolorowaniem wg poziomów chi² (1σ / 2σ / 3σ); zmienny wybór osi X i Y.
13. **Wykresy Arrheniusa** — kilka typów (A·ln(I₀), A, I₀, Rₛ, Rₛₕ, Rₛₕ₂, α); oś X jako T lub 1000/T; scatter + linia.
14. **Wiele charakterystyk jednocześnie** — ładowanie, wyświetlanie, zarządzanie widocznością i kolorami wielu serii.
15. **Eksport parametrów do CSV** — per wiersz: nazwa, T, parametry; nagłówek z nazwami.
16. **Eksport danych MC do CSV** — podział na grupy sigma; kolumny parametrów + błąd.
17. **Eksport wykresu MC do PNG** — scatter z kolorowaniem (teraz przez Python, docelowo może inaczej).
18. **Eksport niepewności MC do CSV** — per parametr, per poziom ufności.
19. **Zapis/odczyt konfiguracji MC (YAML)** — persystencja ustawień MC (bez wyników).
20. **Ładowanie wcześniej wyliczonych wyników MC (YAML)** — z poprawnym mapowaniem nazw modeli.
21. **Generator syntetycznych danych** — z szumem normalnym, sweep parametrów (linear/log/PerDecade), sweep temperatury i szumu.
22. **Tryb „light" vs. „dark"** — przełącznik przy ładowaniu, odpowiedni model, ekstrakcja Isc.
23. **Gradient kolorów charakterystyk** — przypisanie kolorów przez interpolację palety.
24. **Widok globalny / per-charakterystyka** — globalna konfiguracja fixed/model/MC stosowana jednocześnie do wielu ch.

---

## 8. Niejasności i pytania

1. **Semantyka `tuneError` vs `fitError`:** `tuneError` jest wyświetlany jako główna wartość w Characteristic Settings, a `fitError` w Characteristic List. Ich role i wzajemna zależność są niejasne — czy `tuneError` to rzeczywiście błąd krzywej tunowanej względem mierzonej, czy fitowanej?

2. **Błąd chi²:** `CalculateError` w `JFMErrorModel` używa `sum((fit-meas)^2 / meas)` — niestandardowa formuła. Nie jest jasne, czy `mcColor` i chi2vec bazują na tym samym błędzie, czy to Δχ² względem najlepszego fitu (jak twierdzenie Wilksa).

3. **`GetUncertainty` ignoruje `level`:** Implementacja zwraca `max(|true-max|, |true-min|)` niezależnie od poziomu ufności. Trzy kolumny 66%/95%/99% w CSV będą identyczne. Czy to zamierzone, czy bug?

4. **Dwukrotne push_back w generatorze:** W `m_singleShot` i `m_generateCallback` charakterystyka jest dodawana do listy **dwukrotnie** — raz przed fitzem (niekompletna), raz w callbacku po ficie (gotowa). Czy to błąd, czy zamierzona podgląd-przed-dopasowaniem?

5. **`dataRange` inicjalizowany jako `{0, 1}`:** Characteristic::initState() ustawia minimalny zakres. Czy jest to ochrona przed pustą charakterystyką, czy przeoczenie?

6. **`Characteristic List` vs `PlotArea` sidebar:** Dwie listy charakterystyk — tabela w Characteristic List i listbox w PlotArea (sidebar). Pierwsza zarządza widocznością; druga wybiera aktywną. Czy docelowo mają być jednym widokiem?

7. **MC Inspector — `activeMC` a zaznaczenie charakterystyki:** Kliknięcie na symulację w drzewie ustawia jednocześnie `data.active = &ch`. Czy to celowe powiązanie z aktywną charakterystyką w Plot Area?

8. **Brak obsługi wielokrotnych osi:** Aktualny wykres I(V) ma jedną oś Y. Czy docelowo planowane są dodatkowe osie (np. J = I/A)?
