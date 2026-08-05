# JFM — automatyczny wybór gałęzi modeli, modelu i zakresu dopasowania

## 1. Cel

Należy rozbudować mechanizm prefitu JFM tak, aby automatycznie:

1. wykrywał obecność składnika fotoprądowego (I_{ph}) bez dopasowywania wszystkich modeli z (I_{ph}) i bez (I_{ph}),
2. na podstawie tej detekcji wybierał jedną z dwóch rozłącznych gałęzi modeli:

   * `dark`,
   * `light`,
3. przechodził hierarchicznie od najprostszego modelu do bardziej złożonych wyłącznie wewnątrz wybranej gałęzi,
4. analizował rozkład residuów w funkcji napięcia,
5. wykrywał, czy niedopasowanie pochodzi z dolnej czy górnej części charakterystyki,
6. dobierał granice zakresu przez bisekcję po indeksach punktów z lokalnym jitteringiem,
7. wybierał możliwie najszerszy zakres opisany przez możliwie najprostszy model.

Kluczowa zasada:

> Modele z (I_{ph}) nie są dodatkowymi kandydatami porównywanymi z modelami bez (I_{ph}). Detekcja (I_{ph}) odbywa się przed wyborem modelu i wybiera całą gałąź modeli.

---

# 2. Aktualny przepływ

Obecnie:

```text
model wybrany ręcznie
        ↓
jeden wspólny detect_fit_range()
        ↓
prefit jednego modelu
        ↓
finalny Simplex fit
```

Problemy:

* brak automatycznej detekcji `dark/light`,
* brak hierarchii modeli,
* zakres jest wyznaczany przed wyborem modelu i wspólny dla wszystkich modeli,
* brak analizy przestrzennego rozkładu residuów,
* brak automatycznej decyzji o dodaniu (R_s) albo (R_{sh}),
* obecność (I_{ph}) wynika wyłącznie z tekstu `"I_ph"` w `modelId`,
* `_tail_plateau_bound()` korzysta z hardcoded progu `0.0001`,
* część parametrów `RangeOptions` jest martwą konfiguracją.

---

# 3. Docelowy przepływ

```text
dane I(V), temperatura, niepewności
        ↓
wstępna walidacja i sortowanie danych
        ↓
prosta detekcja I_ph
        ↓
wybór jednej gałęzi:
 dark albo light
        ↓
najprostszy model w wybranej gałęzi
        ↓
prefit / szybki fit
        ↓
analiza globalnej jakości i rozkładu residuów
        ↓
 ┌──────────────────────┬────────────────────────┐
 │ problem zakresu      │ brak składnika modelu  │
 │ na brzegu danych     │ Rs albo Rsh            │
 ↓                      ↓
bisekcja granicy        przejście do modelu-dziecka
+ jittering             w tej samej gałęzi
 └──────────────────────┴────────────────────────┘
        ↓
porównanie poprawnych kandydatów
        ↓
modelId + fitRange + diagnostyka decyzji
        ↓
finalny C++ Simplex fit
```

Detekcja (I_{ph}) jest wykonywana dokładnie raz na początku automatycznej selekcji. Po wybraniu gałęzi algorytm nie przechodzi między `dark` i `light`.

---

# 4. Gałęzie modeli

## 4.1. Gałąź `dark`

```text
SD-0
├── SD-Rs
├── SD-Rsh
└── SD-RsRsh
    └── SD-RsRsh-alpha-Rsh2
```

Przejścia:

```text
SD-0
  high-voltage residual structure → SD-Rs
  low-voltage residual structure  → SD-Rsh
  both                             → SD-RsRsh

SD-Rs
  persistent low-voltage problem  → SD-RsRsh

SD-Rsh
  persistent high-voltage problem → SD-RsRsh

SD-RsRsh
  persistent low-voltage curvature
  (systematyczna struktura residuów przy niskim napięciu,
  nieusuwalna liniowym Rsh)         → SD-RsRsh-alpha-Rsh2
```

`SD-RsRsh-alpha-Rsh2` to model 6-parametrowy (`I0`, `A`, `Rs`, `Rsh`, `alpha`, `Rsh2`) z nieliniowym członem bocznikowym. Jest liściem gałęzi `dark` — nie ma dalszych dzieci. Promocja do niego podlega tym samym regułom co każde inne przejście do modelu-dziecka (§9.3): musi albo istotnie rozszerzyć zakres, albo usunąć wyraźną systematyczną strukturę residuów przy tym samym zakresie z istotną poprawą AICc/BIC, i parametry `alpha`/`Rsh2` muszą być identyfikowalne.

## 4.2. Gałąź `light`

```text
SD-0-I_ph
├── SD-Rs-I_ph
├── SD-Rsh-I_ph
└── SD-RsRsh-I_ph
    └── SD-RsRsh-alpha-Rsh2-I_ph
```

Przejścia są identyczne jak w gałęzi `dark`, ale (I_{ph}) jest obowiązkowym parametrem każdego modelu, w tym liścia `SD-RsRsh-alpha-Rsh2-I_ph` (7 parametrów: `I0`, `A`, `Rs`, `Rsh`, `alpha`, `Rsh2`, `I_ph`).

## 4.3. Zakazy

Nie implementować:

```text
for model in all_10_models:
    fit(model)
    choose_best()
```

Nie należy porównywać:

```text
SD-RsRsh
```

z:

```text
SD-RsRsh-I_ph
```

za pomocą (\chi^2), AIC albo BIC.

Najpierw należy wykryć (I_{ph}), a następnie pracować wyłącznie w jednej gałęzi.

---

# 5. Prosta detekcja (I_{ph})

## 5.1. Założenie fizyczne

Dla przyjętej konwencji:

[
I(V)=I_{\mathrm{diode}}(V)-I_{ph}.
]

W pobliżu (V=0) prąd ciemny diody powinien być mały, natomiast obecność fotoprądu daje w przybliżeniu stałe przesunięcie prądowe.

Detektor powinien rozpoznawać:

* niezerowy poziom prądu w pobliżu (V=0),
* statystyczną istotność tego poziomu,
* przybliżoną stałość przesunięcia,
* zgodność znaku prądu z konwencją (I_{ph}).

Nie wolno klasyfikować krzywej jako `light` wyłącznie dlatego, że przy małych napięciach występuje duży prąd. Silnie liniowy prąd może pochodzić z (R_{sh}), a nie z fotoprądu.

---

## 5.2. Okno detekcyjne

Wybrać punkty najbliższe (V=0).

Proponowana szerokość:

[
\Delta V_0=
\max\left(6V_T,;0.05(V_{\max}-V_{\min})\right),
]

gdzie:

[
V_T=\frac{k_BT}{q}.
]

Początkowo:

```python
window = abs(voltage) <= delta_v0
```

Reguły dodatkowe:

* minimalnie `7` punktów,
* jeżeli w oknie jest mniej punktów, użyć `7` punktów najbliższych (V=0),
* maksymalnie około `20%` wszystkich punktów,
* jeżeli zakres nie obejmuje napięć bliskich zeru, obniżyć confidence detekcji.

---

## 5.3. Robust baseline

Dla punktów w oknie (W_0):

[
I_0=\operatorname{median}_{i\in W_0}(I_i),
]

[
s_0=1.4826\operatorname{MAD}_{i\in W_0}(I_i).
]

Błąd poziomu prądu:

[
s_{\bar I}=
\max\left(
\frac{s_0}{\sqrt{N_0}},
\sigma_{\mathrm{floor}}
\right).
]

Istotność przesunięcia:

[
Z_{ph}=\frac{|I_0|}{s_{\bar I}}.
]

---

## 5.4. Test stałości przesunięcia

W oknie (W_0) dopasować odporną prostą:

[
I(V)=a+bV.
]

Można użyć:

* Theil–Sen,
* weighted linear regression,
* zwykłej regresji po wcześniejszym odrzuceniu outlierów.

Metryka płaskości:

[
F_{ph}=
\frac{|b|\Delta V_W}
{\max(|a|,3s_0,\sigma_{\mathrm{floor}})},
]

gdzie (\Delta V_W) jest szerokością wykorzystanego okna.

Interpretacja:

* małe (F_{ph}): prawie stały offset, zgodny z (I_{ph}),
* duże (F_{ph}): zależność liniowa lub nieliniowa, możliwy wpływ (R_{sh}).

Spójność znaku:

[
C_{ph}=
\frac{
#{i:\operatorname{sign}(I_i)=\operatorname{sign}(I_0)}
}{
N_0
}.
]

---

## 5.5. Decyzja

Domyślne progi startowe:

```python
photo_z_threshold = 5.0
photo_flatness_max = 0.35
photo_sign_coherence_min = 0.80
expected_photo_current_sign = -1
```

Klasyfikacja:

```python
is_sign_valid = sign(I0) == expected_photo_current_sign

is_light = (
    is_sign_valid
    and Z_ph >= photo_z_threshold
    and F_ph <= photo_flatness_max
    and C_ph >= photo_sign_coherence_min
)
```

Wynik:

```text
is_light == true  → gałąź light
is_light == false → gałąź dark
```

Dodatkowe przecięcie charakterystyki z osią (I=0) przy dodatnim napięciu może zwiększać confidence, ale nie powinno być warunkiem koniecznym.

---

## 5.6. Wartość początkowa (I_{ph})

Dla konwencji:

[
I=I_{\mathrm{diode}}-I_{ph}
]

wartość początkowa:

[
I_{ph}^{(0)}=\max(0,-I_0).
]

Detektor nie wyznacza ostatecznej wartości (I_{ph}). Dostarcza:

* wybór gałęzi,
* wartość początkową parametru,
* confidence,
* diagnostykę.

---

## 5.7. Tryby działania

Dodać ustawienie:

```cpp
enum class PhotoBranchMode
{
    Auto,
    ForceDark,
    ForceLight
};
```

Tryby ręczne są potrzebne do:

* testów,
* nietypowych konwencji pomiarowych,
* danych nieobejmujących okolicy (V=0),
* wymuszenia gałęzi przez użytkownika.

Przy `Auto` i niskiej jakości detekcji domyślnie wybierać `dark`, ale zwracać:

```text
photoDetectionConfidence = low
photoDetectionWarning = true
```

Nie wykonywać automatycznego porównania obu gałęzi jako fallback.

---

# 6. Residua i lokalna analiza błędu

## 6.1. Residua znormalizowane

Dla każdego punktu:

[
r_i=
\frac{I_i-I_{\mathrm{model}}(V_i)}
{\sigma_i}.
]

Jeżeli znane są niepewności pomiarowe, użyć ich bezpośrednio.

Jeżeli nie są znane, zastosować istniejący model:

[
\sigma_i=
\max\left(
\alpha|I_i|,
\sigma_{\mathrm{floor}}
\right).
]

Nie należy analizować wyłącznie (|I_i-I_{\mathrm{model},i}|), ponieważ charakterystyka może obejmować wiele rzędów wielkości prądu.

---

## 6.2. Ruchome okno

Długość okna:

```python
window_size = odd(
    clamp(round(0.05 * number_of_points), 5, 21)
)
```

Dla każdego punktu policzyć ważony lokalny RMS:

[
E_i=
\sqrt{
\frac{
\sum_{j\in W_i}w_{ij}r_j^2
}{
\sum_{j\in W_i}w_{ij}
}
}.
]

Oraz signed bias:

[
B_i=
\frac{
\sum_{j\in W_i}w_{ij}r_j
}{
\sum_{j\in W_i}w_{ij}
}.
]

Wagi mogą być:

* trójkątne,
* gaussowskie,
* symetryczne względem punktu centralnego.

`E_i` wykrywa lokalny wzrost błędu.

`B_i` wykrywa systematyczne odchylenie o jednakowym znaku.

Pojedynczy outlier nie może samodzielnie powodować zmiany modelu lub zakresu.

---

## 6.3. Regiony charakterystyki

Dla aktualnego zakresu podzielić dane na:

```text
low edge      — pierwsze 15% punktów
interior      — środkowe 70% punktów
high edge     — ostatnie 15% punktów
```

Minimalna długość regionu brzegowego:

```python
edge_points = max(window_size, ceil(0.15 * fit_point_count))
```

Wyznaczyć:

```python
low_error_score
high_error_score
interior_error_score

low_bias_score
high_bias_score
interior_bias_score
```

---

## 6.4. Klasyfikacja problemu

```text
duży błąd przy niskich napięciach:
    possible Rsh
    zła dolna granica
    nieprawidłowe punkty blisko zera

duży błąd przy wysokich napięciach:
    possible Rs
    zła górna granica
    początek innego mechanizmu transportu

problem na obu końcach:
    possible Rs + Rsh
    albo konieczność przesunięcia obu granic

problem wewnątrz zakresu:
    model fizycznie niewystarczający
    błędne dane
    lokalna anomalia
```

Przykładowa reprezentacja:

```python
class ResidualProblem(Enum):
    NONE = "none"
    LOW_EDGE = "low_edge"
    HIGH_EDGE = "high_edge"
    BOTH_EDGES = "both_edges"
    INTERIOR = "interior"
```

---

# 7. Warunek poprawnego dopasowania

Dopasowanie jest akceptowalne tylko wtedy, gdy jednocześnie:

```text
optimizer zakończył się poprawnie
parametry są skończone
parametry leżą wewnątrz fizycznych ograniczeń
liczba punktów jest wystarczająca względem liczby parametrów
reduced chi² jest poniżej skonfigurowanego progu
low_error_score jest akceptowalny
high_error_score jest akceptowalny
nie występuje długi ciąg residuów o jednym znaku
parametry nie są przyklejone do bounds
```

Przykładowe progi startowe:

```python
local_rms_threshold = 1.5
local_bias_threshold = 0.75
max_same_sign_run_fraction = 0.20
minimum_degrees_of_freedom = 5
```

Wszystkie progi muszą znajdować się w konfiguracji, nie jako liczby zaszyte w algorytmie.

---

# 8. Bisekcja zakresu z jitteringiem

## 8.1. Zasada

Granice zakresu są indeksami punktów:

```python
fit_range = [lo_index, hi_index]
```

Jeżeli analiza residuów wskazuje problem:

```text
LOW_EDGE  → modyfikować lo_index
HIGH_EDGE → modyfikować hi_index
```

Jeżeli problem występuje na obu końcach, najpierw modyfikować stronę o większym przekroczeniu progu, a następnie drugą.

Nie usuwać pojedynczych punktów ze środka zakresu.

---

## 8.2. Dlaczego jittering jest potrzebny

Jakość prefitu nie musi być monotoniczna względem indeksu granicy.

Może się zdarzyć, że:

```text
hi = 150 → zły prefit
hi = 149 → zły prefit
hi = 147 → bardzo dobry prefit
hi = 145 → gorszy prefit
```

Czysta bisekcja może ominąć lokalnie dobry punkt.

Po znalezieniu przybliżonej granicy należy więc sprawdzić jej otoczenie.

---

## 8.3. Algorytm dla górnej granicy

1. Rozpocząć od możliwie szerokiego zakresu.
2. Sprawdzić dopasowanie.
3. Jeżeli problem występuje przy wysokich napięciach, znaleźć mniejszą granicę dającą akceptowalne dopasowanie.
4. Utworzyć bracket:

   * `hi_bad` — granica nieakceptowalna,
   * `hi_good` — granica akceptowalna.
5. Wykonywać bisekcję po indeksach do momentu, gdy bracket jest niewielki.
6. Wykonać jittering wokół znalezionej granicy.
7. Wybrać największe `hi`, dla którego dopasowanie pozostaje akceptowalne.

Jitter:

```python
candidate_offsets = [-J, ..., -1, 0, 1, ..., J]
```

Domyślnie:

```python
jitter_radius_points = 3
```

Analogiczny algorytm stosować dla dolnej granicy.

---

## 8.4. Końcowa próba rozszerzenia

Po zakończeniu bisekcji i jitteringu należy spróbować:

```text
lo - 1
hi + 1
```

oraz ewentualnie kilku kolejnych punktów, aby potwierdzić, że znaleziony zakres jest lokalnie maksymalny.

---

# 9. Hierarchiczny wybór modelu

## 9.1. Model początkowy

Po detekcji gałęzi:

```python
if branch == DARK:
    model = "SD-0"
else:
    model = "SD-0-I_ph"
```

Dla bieżącego modelu należy znaleźć jego maksymalny akceptowalny zakres.

Dopiero potem na podstawie residuów należy zdecydować, czy przejść do modelu bardziej złożonego.

---

## 9.2. Reguły przejścia

```python
if residual_problem == LOW_EDGE:
    child_model = add_rsh(current_model)

elif residual_problem == HIGH_EDGE:
    child_model = add_rs(current_model)

elif residual_problem == BOTH_EDGES:
    child_model = add_rs_and_rsh(current_model)

elif residual_problem == INTERIOR:
    report_model_inadequacy()
```

Jeżeli aktualny model zawiera już wymagany składnik:

```text
SD-Rs + problem low edge  → SD-RsRsh
SD-Rsh + problem high edge → SD-RsRsh
```

---

## 9.3. Warunek zaakceptowania modelu bardziej złożonego

Model-dziecko nie może być wybierany tylko dlatego, że ma mniejsze (\chi^2).

Należy go zaakceptować, jeżeli spełniony jest co najmniej jeden warunek:

1. pozwala istotnie rozszerzyć zakres dopasowania,
2. usuwa wyraźną systematyczną strukturę residuów,
3. dla tego samego zakresu daje istotną poprawę AICc/BIC,
4. parametry nowego składnika są identyfikowalne i fizyczne.

Minimalne rozszerzenie zakresu:

```python
min_range_gain_points = 3
min_range_gain_fraction = 0.03
```

Model bardziej złożony jest uzasadniony, gdy:

```python
range_gain_points >= min_range_gain_points
or range_gain_fraction >= min_range_gain_fraction
```

albo dla dokładnie tego samego zbioru punktów:

```python
delta_aicc <= -6
```

Nie porównywać bezpośrednio AICc modeli dopasowanych do różnych zbiorów punktów. Przy różnych zakresach najpierw porównywać coverage i poprawność dopasowania.

---

# 10. Ostateczny wybór

Dla każdej zaakceptowanej konfiguracji zapisać:

```text
branch
modelId
loIndex
hiIndex
pointCount
coverageFraction
parameterCount
reducedChi2
localResidualScores
AICc
parameterDiagnostics
```

Porządek wyboru:

1. odrzucić wszystkie niepoprawne dopasowania,
2. preferować większy zakres,
3. przy porównywalnym zakresie preferować mniej parametrów,
4. następnie preferować mniejszą strukturę residuów,
5. następnie reduced (\chi^2) albo AICc.

Zakresy należy uznać za porównywalne, jeżeli różnią się o mniej niż:

```python
equivalent_range_points = 2
```

lub:

```python
equivalent_range_fraction = 0.02
```

Dzięki temu dodatkowy parametr nie zostanie wybrany tylko po to, aby odzyskać jeden punkt pomiarowy.

---

# 11. Pseudokod całości

```python
def auto_select_prefit(data, options):
    validated = validate_and_sort(data)

    photo = detect_photo_branch(
        voltage=validated.voltage,
        current=validated.current,
        temperature=validated.temperature,
        sigma=validated.sigma,
        options=options.photo_detection,
    )

    if options.photo_branch_mode == "force_dark":
        branch = "dark"
    elif options.photo_branch_mode == "force_light":
        branch = "light"
    else:
        branch = "light" if photo.is_light else "dark"

    current_model = (
        "SD-0-I_ph"
        if branch == "light"
        else "SD-0"
    )

    initial_parameters = {}
    if branch == "light":
        initial_parameters["I_ph"] = photo.initial_i_ph

    tested_candidates = []
    best_candidate = None

    while current_model is not None:
        candidate = optimize_range_for_model(
            data=validated,
            model_id=current_model,
            initial_parameters=initial_parameters,
            options=options,
        )

        tested_candidates.append(candidate)

        if candidate.is_valid:
            best_candidate = choose_better_candidate(
                best_candidate,
                candidate,
                options.selection,
            )

        next_model = select_child_model(
            branch=branch,
            current_model=current_model,
            residual_diagnostics=candidate.residual_diagnostics,
        )

        if next_model is None:
            break

        child_candidate = optimize_range_for_model(
            data=validated,
            model_id=next_model,
            initial_parameters=candidate.parameters,
            options=options,
        )

        tested_candidates.append(child_candidate)

        if should_promote_to_child(
            parent=candidate,
            child=child_candidate,
            options=options.selection,
        ):
            current_model = next_model
            best_candidate = choose_better_candidate(
                best_candidate,
                child_candidate,
                options.selection,
            )
        else:
            break

    return AutoPrefitResult(
        branch=branch,
        photo_detection=photo,
        selected=best_candidate,
        tested_candidates=tested_candidates,
    )
```

Optymalizacja zakresu:

```python
def optimize_range_for_model(data, model_id, initial_parameters, options):
    fit_range = initial_safe_range(data, options.range)

    result = fit_and_diagnose(
        data,
        model_id,
        fit_range,
        initial_parameters,
    )

    for _ in range(options.range.max_iterations):
        if result.is_valid:
            break

        problem = result.residual_diagnostics.problem

        if problem == LOW_EDGE:
            fit_range.lo = search_lower_bound_with_bisection_and_jitter(...)

        elif problem == HIGH_EDGE:
            fit_range.hi = search_upper_bound_with_bisection_and_jitter(...)

        elif problem == BOTH_EDGES:
            side = select_more_severe_side(result.residual_diagnostics)
            fit_range = update_selected_side(...)

        else:
            break

        result = fit_and_diagnose(
            data,
            model_id,
            fit_range,
            result.parameters,
        )

    fit_range = locally_expand_range(
        data,
        model_id,
        fit_range,
        result.parameters,
        options.range,
    )

    return fit_and_diagnose(
        data,
        model_id,
        fit_range,
        result.parameters,
    )
```

---

# 12. Integracja z obecną aplikacją

## 12.1. Python

Zalecany podział:

```text
prefit.py
    run_prefit()
    run_auto_prefit()

photo_detection.py
    detect_photo_branch()

residual_diagnostics.py
    compute_normalized_residuals()
    compute_local_error_profile()
    classify_residual_problem()

adaptive_range.py
    optimize_range_for_model()
    search_lower_bound_with_bisection_and_jitter()
    search_upper_bound_with_bisection_and_jitter()
    locally_expand_range()

model_selection.py
    select_child_model()
    should_promote_to_child()
    choose_better_candidate()

model_registry.py
    modele i ich relacje hierarchiczne

types.py
    konfiguracja i typy wynikowe
```

`detect_fit_range()` nie powinno już wyznaczać jednego finalnego zakresu przed wyborem modelu.

Może pozostać jako:

```text
initial_safe_range()
```

czyli wyłącznie początkowy, szeroki i bezpieczny bracket.

Finalny zakres musi być wyznaczany osobno dla każdego analizowanego modelu.

---

## 12.2. Rejestr modeli

Rozszerzyć metadane modeli:

```python
ModelDefinition(
    id="SD-Rs",
    branch="dark",
    parameter_count=3,
    parent="SD-0",
    adds_rs=True,
    adds_rsh=False,
    has_i_ph=False,
)
```

oraz:

```python
ModelDefinition(
    id="SD-Rs-I_ph",
    branch="light",
    parameter_count=4,
    parent="SD-0-I_ph",
    adds_rs=True,
    adds_rsh=False,
    has_i_ph=True,
)
```

Nie używać obecności substringu `"I_ph"` jako podstawowego mechanizmu ustalania typu modelu. Informacja powinna znajdować się jawnie w metadanych.

---

## 12.3. C++

Dodać tryb wyboru modelu:

```cpp
enum class JFMModelSelectionMode
{
    Manual,
    Automatic
};
```

W trybie `Manual` zachować obecne zachowanie.

W trybie `Automatic`:

```text
C++ przekazuje dane i konfigurację do run_auto_prefit()
Python zwraca modelId i fitRange
C++ zapisuje wynik w JFMPrefitState
finalny Simplex fit korzysta z wybranego modelId i fitRange
```

`Auto` nie powinno być modelem w `FitEngineDetail::Models`. Jest to tryb selekcji, nie równanie modelu.

---

## 12.4. Wynik automatycznego prefitu

Rozszerzyć wynik o:

```cpp
struct JFMAutoSelectionResult
{
    std::string branch;
    bool photoCurrentDetected;
    double photoDetectionConfidence;
    double initialPhotoCurrent;

    std::string selectedModelId;
    std::size_t lowerIndex;
    std::size_t upperIndex;

    double reducedChi2;
    double coverageFraction;
    int parameterCount;

    JFMResidualDiagnostics residualDiagnostics;
    std::vector<JFMCandidateSummary> testedCandidates;

    std::string selectionReason;
    std::vector<std::string> warnings;
};
```

Przykładowy `selectionReason`:

```text
Selected light branch because a statistically significant and approximately
constant negative current offset was detected near V=0.

SD-0-I_ph was rejected because of systematic high-voltage residuals.

SD-Rs-I_ph extended the acceptable fit range by 11 points without introducing
non-identifiable parameters.
```

---

# 13. Konfiguracja

Wszystkie parametry algorytmu muszą być jawnie konfigurowalne.

```python
@dataclass
class PhotoDetectionOptions:
    z_threshold: float = 5.0
    flatness_max: float = 0.35
    sign_coherence_min: float = 0.80
    expected_current_sign: int = -1
    min_points: int = 7
    max_fraction: float = 0.20
    voltage_window_thermal_factor: float = 6.0
    voltage_window_range_fraction: float = 0.05
    sigma_floor: float = 1e-30


@dataclass
class ResidualOptions:
    local_window_fraction: float = 0.05
    local_window_min: int = 5
    local_window_max: int = 21
    edge_fraction: float = 0.15
    local_rms_threshold: float = 1.5
    local_bias_threshold: float = 0.75
    max_same_sign_run_fraction: float = 0.20


@dataclass
class AdaptiveRangeOptions:
    jitter_radius_points: int = 3
    max_iterations: int = 20
    min_fit_points: int = 8
    equivalent_range_points: int = 2
    equivalent_range_fraction: float = 0.02


@dataclass
class ModelSelectionOptions:
    min_range_gain_points: int = 3
    min_range_gain_fraction: float = 0.03
    minimum_aicc_improvement: float = 6.0
```

Należy usunąć lub faktycznie wykorzystać martwe pola istniejącego `RangeOptions`.

Hardcoded próg `0.0001` w `_tail_plateau_bound()` należy zastąpić progiem opartym na konfiguracji i poziomie szumu.

---

# 14. Testy

## 14.1. Detekcja (I_{ph})

Przygotować dane syntetyczne:

```text
dark, brak Rsh
dark, silny Rsh
light, stały I_ph
light, I_ph + Rsh
light, I_ph + Rs
krzywa z offsetem instrumentalnym
krzywa nieobejmująca V≈0
krzywa z outlierami przy V≈0
```

Sprawdzić:

* czy silny (R_{sh}) nie jest klasyfikowany jako (I_{ph}),
* czy stały offset o odpowiednim znaku wybiera `light`,
* czy pojedynczy outlier nie zmienia gałęzi,
* czy `ForceDark` i `ForceLight` działają deterministycznie.

## 14.2. Hierarchia modeli

Dla syntetycznych danych generowanych przez każdy model sprawdzić, czy algorytm wybiera:

```text
SD-0
SD-Rs
SD-Rsh
SD-RsRsh
SD-0-I_ph
SD-Rs-I_ph
SD-Rsh-I_ph
SD-RsRsh-I_ph
```

Nie wymagać identycznych parametrów do dokładności maszynowej, ale wymagać:

* poprawnej gałęzi,
* poprawnej klasy modelu,
* fizycznych parametrów,
* wysokiego coverage.

## 14.3. Zakres

Przygotować krzywe z celowo uszkodzonymi:

* pierwszymi punktami,
* ostatnimi punktami,
* kilkoma punktami przy granicy,
* lokalną anomalią wewnątrz zakresu.

Sprawdzić, czy:

* bisekcja wybiera prawidłową stronę,
* jittering znajduje lokalnie lepszą granicę,
* algorytm nie usuwa punktów ze środka,
* końcowy zakres jest lokalnie maksymalny.

---

# 15. Kryteria akceptacji implementacji

Implementację uznać za poprawną, jeżeli:

1. `I_ph` jest wykrywane przed wyborem modelu.
2. Detekcja wybiera jedną gałąź modeli.
3. Algorytm nigdy automatycznie nie porównuje modeli z różnych gałęzi.
4. Wybór `Rs` i `Rsh` wynika z lokalizacji struktury residuów.
5. Zakres jest wyznaczany osobno dla każdego modelu.
6. Granice są optymalizowane bisekcją z jitteringiem.
7. Model bardziej złożony musi uzasadnić dodatkowy parametr.
8. Wynik zawiera pełną diagnostykę decyzji.
9. Tryb ręczny zachowuje dotychczasowe działanie.
10. Wszystkie progi są konfigurowalne i pokryte testami.

Najważniejszy docelowy invariant:

```text
branch = detect_photo_branch(data)

selected_model, selected_range =
    find_simplest_model_with_maximal_valid_range(
        models=branch.models,
        data=data,
    )
```

Detekcja (I_{ph}), wybór modelu i wybór zakresu są trzema osobnymi etapami:

```text
detekcja I_ph
    → wybór gałęzi
        → hierarchiczny wybór modelu i zakresu
```
