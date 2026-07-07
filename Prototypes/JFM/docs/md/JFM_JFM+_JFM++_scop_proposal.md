# Zakres JFM / JFM+ / JFM++ — ujednolicona wersja

**Status dokumentu:** wersja robocza do zamrożenia zakresu przed projektowaniem API, odpowiedzialności modułów i UI.  
**Cel:** ustalić, co należy do JFM, JFM+ i JFM++, aby nie mieszać pierwszego używalnego prototypu z długoterminową platformą badawczą.

---

## 0. Najważniejsze korekty względem poprzedniej wersji

Po doprecyzowaniu aktualnego stanu projektu przyjmujemy następujące korekty:

1. **Simplex CPU i Simplex GPU już istnieją jako proof-of-concept.**  
   Nie planujemy już eksperymentu „czy to zadziała”, tylko rewizję, uporządkowanie API, testy regresyjne, integrację z Core i przygotowanie systemu pod dalsze rozszerzanie.

2. **Trace Simplexa już działa.**  
   Obecna implementacja ma pełny trace parametrów i $\chi^2$ w zależności od kroku. W JFM trzeba zachować tę funkcjonalność, ale przepisać ją w czystszy, stabilniejszy sposób.

3. **GPU proof-of-concept jest już wystarczająco daleko, aby traktować GPU MC jako element JFM.**  
   Elipsoidy kowariancji generowane przez algorytm są poprawne w zakresie dotychczasowych testów. Teraz potrzebne są testy regresyjne i walidacja na znanych wynikach.

4. **Import/preprocessing/prefit przez skrypty Python ma wejść już do JFM.**  
   Nie odkładamy tego do JFM+. W JFM ma istnieć ograniczony, ale realny mechanizm zewnętrznych skryptów importu i prefit.

5. **JFM ma zawierać katalog modeli potrzebny do rzeczywistej pracy, nie tylko stare 4P/6P.**  
   Modele z researchu Perplexity, w tym TE-IR, TE-SCR, BTD i SCLC, mogą być obecne już w JFM jako modele bezpośredniego dopasowania lub modele kandydackie, o ile mają jasno zdefiniowane równania, prefit/bounds i status walidacji.

6. **Node Editor dla modeli prądu nie jest wymagany w JFM.**  
   Zestaw modeli zdefiniowany ręcznie prawdopodobnie wystarczy na długi czas. Generowanie modeli prądu z Node Editora przechodzi do JFM++ lub ewentualnie późnego JFM+ jako prototyp badawczy.

7. **Globalne fittingi fizyczne i analiza odwrotna modeli Raua przechodzą do JFM+.**  
   Nie odkładamy całej analizy temperaturowej mechanizmów do JFM++. JFM++ zostaje dla custom models, Node Editora, pluginów i najbardziej badawczych rozszerzeń.

8. **Reduced $\chi^2$ i $\Delta\chi^2$ są końcową ewaluacją jakości dopasowania.**  
   Nie traktujemy ich jako jedynej funkcji celu. Funkcja celu może być prosta na początku, ale system ma raportować reduced $\chi^2$, $\Delta\chi^2$, statystyki MC i zgodność z modelami referencyjnymi.

9. **Testem poprawności nie jest tylko zgodność CPU–GPU krok po kroku.**  
   Kluczowe są testy statystyczne: dopasowanie znanych danych, odzyskiwanie znanych parametrów i sprawdzanie, czy aplikacja odtwarza wyniki referencyjne.

10. **Zapis/odczyt pełnego stanu sesji można odłożyć.**  
    Na etapie JFM ważniejszy jest stabilny import, fit, MC, analiza i eksport do OriginPro niż pełna persystencja projektu.

---

## 1. Aktualny stan wejściowy

Mamy kilka mocnych filarów, które powinny być traktowane jako wejście do dalszego projektowania.

### 1.1 Audyt starego JFM

Wiemy, które funkcje starego JFM działają i powinny zostać odtworzone:

- import `.dat`;
- automatyczny zakres dopasowania;
- prefit / estimate;
- fitting;
- tuning;
- fixed parameters;
- bounds;
- Monte Carlo;
- wykresy Arrheniusa;
- eksport;
- obsługa wielu charakterystyk;
- częściowa obsługa light/dark.

Wiemy też, które elementy są częściowe albo uszkodzone:

- light models istnieją w kodzie, ale nie są poprawnie podłączone;
- `Unselect all` jest uszkodzone;
- cofanie fixed z poziomu tuningu jest częściowe;
- `Update characteristic` jest prawdopodobnie błędne;
- część eksportu MC/uncertainty ma błędy;
- brak systemu komunikatów błędów w GUI;
- brak pełnego zapisu/odczytu sesji.

Wniosek: **nie przepisujemy starego JFM 1:1**. Odtwarzamy funkcje użytkowe, ale naprawiamy semantykę, UI i architekturę.

### 1.2 Research naukowy Perplexity

Mamy materiał naukowy obejmujący:

- katalog modeli prądu;
- interpretację parametrów;
- modele dark/light;
- modele TE-IR, TE-SCR, BTD, SCLC i inne;
- metodologię Uwego Raua;
- wykresy Arrheniusa i diagnostyczne;
- konstrukcję osi do odczytu energii aktywacji;
- funkcje celu i modele błędu;
- niepewności i propagację błędów;
- identyfikowalność parametrów;
- selekcję modeli;
- interpretację mechanizmów fizycznych.

Wniosek: JFM powinien mieć od początku sensowny katalog modeli i analiz, ale **zaawansowana interpretacja mechanizmów** powinna zostać oddzielona od samego dopasowania pojedynczej charakterystyki.

### 1.3 Proof-of-concept Simplex CPU/GPU

Istnieje:

- Simplex CPU jako proof-of-concept;
- Simplex GPU jako proof-of-concept;
- trace parametrów i $\chi^2$;
- podstawowa walidacja elipsoid/kowariancji;
- doświadczenie z GPU MC.

Wniosek: następny etap to **rewizja i stabilizacja**, nie implementacja od zera.

### 1.4 Core jako baza prototypów

Mamy solidny Core, który ma być bazą dla prototypów i modułów.

Ważna decyzja architektoniczna do doprecyzowania:

> Jeden Core dostarcza fundament aplikacji, a klient/prototyp implementuje własną domenę i konfigurację, podobnie do podejścia typu `CreateApplication` znanego z architektury Hazel.

To oznacza, że JFM powinien być traktowany jako domenowy klient Core, a nie jako osobna aplikacja pisana od zera.

### 1.5 Node Editor / NVRTC / GPU pipeline

Istnieje proof-of-concept Node Editora i generacji kodu CUDA, ale nie musi on wejść do JFM. W modelach dopasowania wartości parametrów nie mogą być osadzane w kodzie jako literały wymagające rekompilacji NVRTC przy każdej zmianie.

Wniosek: Node Editor dla modeli prądu odkładamy do JFM++ albo późnego JFM+.

---

## 2. Główna zasada podziału JFM / JFM+ / JFM++

```text
JFM     = pierwsze realnie używalne narzędzie badawcze:
          import, prefit, fit, tuning, MC GPU, podstawowa analiza temperaturowa,
          katalog modeli prądu i eksport do OriginPro.

JFM+    = rozszerzona analiza naukowa:
          porównywanie modeli, interpretacja mechanizmów, analiza Raua,
          globalne zależności temperaturowe, zaawansowane niepewności,
          batch i analiza wielu serii.

JFM++   = platforma badawcza i rozszerzalna:
          Node Editor dla modeli prądu, custom models, pluginy,
          zaawansowane solvery, integracje z innymi modułami SemiconductorStudio,
          eksperymentalne metody interpretacji.
```

JFM nie ma być „mały” w sensie zabawkowy. Ma być ograniczony do dobrze zdefiniowanego, używalnego workflow, ale powinien być wystarczająco mocny, żeby realnie analizować dane pomiarowe.

---

# 3. Zakres JFM

## 3.1 Definicja JFM

JFM to pierwszy stabilny prototyp narzędzia do analizy charakterystyk $I(V,T)$ / $J(V,T)$.

Jego celem jest:

1. wczytać dane pomiarowe;
2. rozpoznać strukturę danych i metadane;
3. wykonać preprocessing i prefit;
4. dopasować jeden lub kilka znanych modeli prądu;
5. umożliwić ręczne poprawianie/tuning;
6. policzyć MC i niepewności;
7. wykonać podstawową analizę temperaturową;
8. wyeksportować dane i wyniki do plików wygodnych dla OriginPro.

JFM powinien być nową, uporządkowaną wersją sprawdzonego workflow starego JFM, ale opartą o Core i poprawioną bibliotekę numeryczną.

---

## 3.2 JFM — dane i workflow

W JFM wchodzi:

- import `.dat`, `.txt`, `.csv`, `.tsv`;
- obsługa innych formatów przez niezależne skrypty Python;
- rozpoznawanie kolumn:
  - $V$,
  - $I$,
  - $T$,
  - metadanych;
  - opcjonalnie $J$,
- odczyt temperatury z nazwy pliku i/lub metadanych;
- rozróżnienie $I$ vs $J$;
- konwersja $I \leftrightarrow J$, jeśli znana jest powierzchnia;
- jawny typ charakterystyki:
  - dark,
  - light;
- wiele charakterystyk w jednej serii;
- wiele serii w projekcie;
- workflow:

```text
Import
→ Preprocess
→ PreFit / Estimate
→ Fit
→ Tune
→ Monte Carlo
→ Arrhenius / Derived plots
→ Export
```

### Python w JFM

Do JFM wchodzi ograniczony, ale realny mechanizm skryptów Python:

- osobny skrypt importu;
- osobny skrypt preprocessing;
- osobny skrypt prefit;
- możliwość podmiany/dodania skryptu bez rekompilacji aplikacji;
- możliwość przeładowania skryptu podczas pracy aplikacji, jeśli technicznie jest to stabilne;
- jasny kontrakt wejścia/wyjścia między C++ a Pythonem.

Nie jest to jeszcze pełny system pluginów. Pluginy i pełna automatyzacja należą do JFM++.

---

## 3.3 JFM — pojęcia domenowe

JFM powinien wprowadzać stabilne pojęcia:

```text
Project
→ Series
→ Characteristic
→ FitRun
→ MonteCarloRun
→ DerivedAnalysis
→ ExportJob
```

### Characteristic

Charakterystyka powinna zawierać co najmniej:
na zasadzie trochę jak ECS, charakterystyka ma UUID, różne rzeczy po za tymi oczywistymi, są umieszczone w mapach które przechowują UUID charakterystyki i listę tych odpowiednich rzeczy 

- nazwę; (w charakterystyce)
- typ: dark/light; (w charakterystyce)
- temperaturę; (w charakterystyce)
- dane $V$; (w charakterystyce)
- dane $I$ lub $J$; (w charakterystyce)
- informację o powierzchni urządzenia, jeśli dostępna; (w charakterystyce)
- status preprocessingu;(w charakterystyce)
- aktywny zakres dopasowania;
- status prefit;
- listę wykonanych fitów;
- listę wykonanych MC;
- flagi widoczności;
- kolor/legendę;
- metadane importu.

### Series

Seria powinna grupować charakterystyki, np.:

- pomiary tej samej próbki w różnych temperaturach;
- pomiary dark;
- pomiary light;
- różne przebiegi/sweepy;
- różne urządzenia lub pola kontaktu.

W JFM wiele serii ma działać użytkowo, ale zaawansowana porównywarka serii należy do JFM+.

---

## 3.4 JFM — modele prądu

JFM powinien zawierać katalog znanych modeli prądu, zdefiniowanych ręcznie w kodzie, bez potrzeby Node Editora.

### Modele legacy do odtworzenia

- legacy dark 4P:

$$
I_0,\ A,\ R_s,\ R_{sh}
$$

- legacy dark 6P:

$$
I_0,\ A,\ R_s,\ R_{sh},\ \alpha,\ R_{sh2}
$$

- light 5P:

$$
I_{ph}/I_{SC},\ I_0,\ A,\ R_s,\ R_{sh}
$$

- light 7P albo odpowiednik rozszerzonego modelu light.

Modele light muszą być poprawnie podłączone, ponieważ w starej aplikacji ich obsługa była częściowa.

### Modele naukowe w JFM

JFM powinien objąć katalog modeli z researchu jako modele kandydackie do bezpośredniego dopasowania, z wyraźnym statusem walidacji.

Minimalny katalog JFM:

- `SD-0`;
- `SD-Rs`;
- `SD-Rsh`;
- `SD-RsRsh`;
- `SD-Iph`;
- `SD-PLsh`;
- `2D-RsRsh`;
- `2D-Iph`, jeśli jest potrzebny do light;
- `TE-IR`;
- `TE-SCR`;
- `BTD`;
- `SCLC`.

Dla każdego modelu muszą istnieć:

- jawna nazwa;
- lista parametrów;
- jednostki;
- równanie;
- sposób ewaluacji $I(V)$ lub $J(V)$;
- informacja, czy równanie jest jawne czy implicit;
- prefit/estimate albo informacja, że prefit nie istnieje;
- domyślne bounds;
- minimalne testy syntetyczne;
- informacja, czy model jest:
  - stabilny,
  - eksperymentalny,
  - diagnostyczny,
  - wymagający ostrożnej interpretacji.

Nazwy „4P/5P/6P/7P” mogą być użyte jako aliasy legacy, ale UI powinno pokazywać rzeczywisty skład modelu, np. `SD-RsRsh`, `SD-Iph`, `SD-PLsh`.

---

## 3.5 JFM — biblioteka numeryczna i solver

W JFM wchodzi przebudowa istniejących PoC Simplex CPU/GPU do stabilnej biblioteki numerycznej.

Wymagania:

- Simplex CPU;
- Simplex GPU;
- wspólna semantyka parametrów;
- parametry:
  - free/fixed,
  - bounds,
  - log-scale,
  - initial value,
  - current value,
  - unit,
  - label;
- dynamiczne strategie inicjalizacji simpleksu;
- dynamiczne strategie ustalania maksymalnej liczby iteracji;
- możliwość późniejszego dodania:
  - Levenberg–Marquardt,
  - Simulated Annealing,
  - innych solverów;
- testy regresyjne;
- testy statystyczne odzyskiwania parametrów;
- testy zgodności CPU/GPU.

### Reduced $\chi^2$ i $\Delta\chi^2$

W JFM reduced $\chi^2$ i $\Delta\chi^2$ są częścią końcowej ewaluacji dopasowania.

System powinien raportować:

- wartość funkcji celu;
- reduced $\chi^2$;
- $\Delta\chi^2$ względem najlepszego rozwiązania/modelu;
- liczbę stopni swobody;
- liczbę punktów;
- liczbę parametrów swobodnych;
- status zbieżności;
- informację o parametrach na granicach;
- informację o podejrzanych parametrach.

---

## 3.6 JFM — fitting, prefit i zakres dopasowania

W JFM wchodzi:

- automatyczny prefit dla obsługiwanych modeli;
- manualne wartości startowe;
- manualne bounds;
- automatic bounds z prefit;
- dynamiczny dobór bounds dla parametrów o dużym rozstrzale, szczególnie $R_s$ i $R_{sh}$;
- fixed parameters w CPU i GPU;
- manualny zakres dopasowania;
- auto-fit po zmianie zakresu;
- detekcja rolloff;
- odrzucanie zakresów oczywiście niepoprawnych;
- możliwość porównania kilku zakresów;
- podstawowa rekomendacja modelu.

### Automatyczny wybór modelu w JFM

JFM powinien mieć podstawowy workflow:

```text
fit modeli kandydackich
→ reduced χ²
→ Δχ²
→ fizyczność parametrów
→ struktura residuów
→ stabilność zakresu
→ rekomendacja modelu
```

To ma być rekomendacja, nie nieodwołalna decyzja programu.

AICc/BIC można przygotować jako element API, ale pełna metodologia model selection należy do JFM+.

---

## 3.7 JFM — tuning parametrów

Tuning jest funkcją obowiązkową.

JFM powinien obsłużyć:

- widok parametrów:
  - estimated,
  - fitted,
  - tuned;
- suwaki lub kontrolki do ręcznego tuningu;
- osobne sterowanie wartością i skalą albo lepszy odpowiednik mantysa/wykładnik;
- live preview krzywej tuned;
- możliwość przepisania tuned → fitted;
- możliwość natychmiastowego fitu po zmianie wybranego parametru;
- możliwość fixed/free z poziomu tuningu;
- cofanie fixed/free;
- reset tuningu;
- widoczne porównanie:
  - raw,
  - ranged,
  - fitted,
  - tuned.

Jeżeli MC będzie wystarczająco szybkie, można rozważyć szybkie przeliczenie niepewności po tuningu, ale nie jest to warunek JFM.

---

## 3.8 JFM — UI

JFM powinien odtworzyć dobre elementy starego UI i naprawić słabe.

Wchodzi:

- główne drzewo/lista projektu:
  - serie,
  - charakterystyki,
  - fit runs,
  - MC runs;
- multi-select:
  - Ctrl,
  - Shift,
  - drag select;
- context menu:
  - hide/unhide,
  - select/unselect,
  - invert selection,
  - change model,
  - change color preset,
  - remove,
  - fit selected,
  - run MC selected;
- główny wykres:
  - raw,
  - ranged,
  - fitted,
  - tuned;
- osobne kontrolki dla:
  - widoczności,
  - koloru,
  - grubości linii,
  - stylu;
- `Hide non-active`;
- auto-scale;
- auto-scale z marginesem/zoom-out;
- manualny zakres fitowania:
  - suwaki,
  - pola liczbowe,
  - ewentualnie skróty klawiaturowe `L/R` dla lewej/prawej granicy, gdy fokus jest na wykresie;
- panel parametrów;
- panel MC;
- panel Arrheniusa;
- jasne komunikaty błędów/statusu w GUI.

JFM powinien mieć lepszy system informacji o błędach niż stary JFM, w którym wiele błędów trafiało tylko do konsoli.

---

## 3.9 JFM — Monte Carlo

W JFM wchodzi:

- MC na aktywnej charakterystyce;
- MC na zaznaczonych charakterystykach;
- GPU MC;
- presety MC;
- konfiguracja:
  - liczba iteracji,
  - model szumu,
  - sigma,
  - seed,
  - liczba zapisywanych próbek;
- scatter plots dla par parametrów;
- elipsoidy kowariancji;
- reduced $\chi^2$;
- $\Delta\chi^2$;
- eksport surowych wyników MC;
- zapis konfiguracji MC;
- porównanie wyników z oczekiwaniami statystycznymi.

Logikę ustawień MC można w dużym stopniu przejąć koncepcyjnie ze starego JFM, ale implementację należy uporządkować.

---

## 3.10 JFM — podstawowa analiza temperaturowa

W JFM wchodzi podstawowa analiza temperatury:

- $I_0(T)$;
- $1/A(T)$;
- modele $A(T)$;
- $\ln I_0$ vs $1/(A kT)$;
- $A\ln I_0$ vs $1/(kT)$;
- fitowanie rodzin wykresów:
  - $T$,
  - $1/T$,
  - $1000/T$,
  - $1/(kT)$;
- modele $I_0(T)$;
- modele $R_s(T)$;
- $R_{sh}(T)$;
- wybór osi:
  - $T$,
  - $1/T$,
  - $1000/T$,
  - $1/(kT)$;
- eksport danych do dalszej analizy.
- wyciąganie energii aktywacji;
- propagacja niepewności na osiach i w energiach;
- wiele serii Arrheniusa;
JFM nie musi jeszcze wykonywać pełnej interpretacji mechanizmu. Ma przygotować poprawne dane, wykresy i eksport.

---

## 3.11 JFM — eksport

Eksport w JFM ma być ukierunkowany na OriginPro i dalszą analizę naukową.

Wchodzi:

- parametry fitu + niepewności do `.tsv`;
- derived parameters do `.tsv`;
- raw MC data do `.tsv`;
- jeden MC run = jeden logiczny zestaw plików;
- dane charakterystyki i fit curves do `.tsv`;
- dane wykresów Arrheniusa do `.tsv`;
- dane niepewności do `.tsv`;
- nazwy kolumn zgodne z nazwami serii i charakterystyk;
- brak zależności od Pythona jako podstawowego mechanizmu generowania wykresów.

Eksport powinien być bardziej uporządkowany niż w starym JFM.

---



# 4. Zakres JFM+

## 4.1 Definicja JFM+

JFM+ rozszerza JFM z narzędzia do dopasowania i podstawowej analizy w narzędzie do pełniejszej interpretacji naukowej.

JFM+ skupia się na:

- porównywaniu modeli;
- analizie mechanizmów;
- globalnych zależnościach temperaturowych;
- interpretacji Raua;
- zaawansowanej analizie niepewności;
- batch workflow;
- analizie wielu serii;
- pełniejszej obsłudze light/dark.

---

## 4.2 JFM+ — zaawansowana analiza modeli

Wchodzi:

- porównywanie wielu modeli dla jednej charakterystyki;
- porównywanie modeli między seriami;
- ranking modeli:
  - reduced $\chi^2$,
  - $\Delta\chi^2$,
  - AICc,
  - BIC,
  - residua,
  - fizyczność parametrów,
  - stabilność względem zakresu;
- analiza sytuacji, w której inny model opisuje inny zakres napięcia;
- detekcja zmian mechanizmu wraz z napięciem;
- wiele zakresów dopasowania;
- stability maps;
- dynamiczne strategie bounds;
- automatyczne fixowanie $R_s$, $R_{sh}$, gdy są wiarygodnie wyznaczone z innej diagnostyki;
- decyzja, kiedy warto uruchomić Simulated Annealing.

---

## 4.3 JFM+ — analiza Raua i globalne modele temperaturowe

Do JFM+ wchodzi pełniejsza analiza temperaturowa i mechanistyczna:

- $A(T)$;
- $1/A(T)$;
- $A(T)T$;
- $\ln I_0$ vs $1/(A kT)$;
- $A\ln I_0$ vs $1/(kT)$;
- fitowanie rodzin wykresów:
  - $T$,
  - $1/T$,
  - $1000/T$,
  - $1/(kT)$;
- modele $A(T)$;
- modele $I_0(T)$;
- modele $R_s(T)$;
- globalne dopasowanie zależności temperaturowych;
- fitowanie mechanizmów, nie tylko pojedynczych charakterystyk;
- odwrócona analiza: z założonego modelu Raua wracamy do parametrów charakterystyk;
- wyciąganie energii aktywacji;
- propagacja niepewności na osiach i w energiach;
- wiele serii Arrheniusa;
- porównanie serii między próbkami.

W JFM+ analiza Raua jest traktowana jako moduł interpretacji wyników, a nie jako część podstawowego fittingu pojedynczej charakterystyki.

---

## 4.4 JFM+ — niepewności i diagnostyka

Wchodzi:

- propagacja niepewności z pełną macierzą kowariancji;
- elipsy kowariancji;
- profile $\chi^2$;
- bootstrap;
- parametric bootstrap;
- raport korelacji parametrów;
- condition number;
- analiza identyfikowalności;
- analiza stabilności względem zakresu;
- confidence bands dla wykresów Arrheniusa;
- obsługa asymetrycznych rozkładów MC;
- testy statystyczne modeli na rodzinach charakterystyk.

---

## 4.5 JFM+ — automatyzacja i batch

W JFM+ wchodzi automatyzacja na poziomie kampanii analitycznych, a nie sam podstawowy import Python.

Wchodzi:

- batch fitting wielu serii;
- batch MC;
- batch Arrhenius;
- presety analiz;
- presety kolorów;
- powtarzalne pipeline’y dla katalogów i grup próbek;
- porównywanie wyników między seriami;
- raporty zbiorcze.

Import/preprocess/prefit Python istnieje już w JFM. W JFM+ zostaje rozszerzony o batch i kampanie analityczne.

---

## 4.6 JFM+ — light/dark analysis

Pełna interpretacja light/dark należy do JFM+.

Wchodzi:

- superposition check;
- cross-over dark/light;
- $J_{SC}(T)$;
- $V_{OC}(T)$;
- $V_{OC}(T=0)$;
- analiza voltage-dependent photocurrent;
- porównanie parametrów dark vs light;
- interpretacja naruszenia superpozycji.

---

## 4.7 JFM+ — możliwy prototyp Node Editora

Node Editor dla modeli prądu **nie jest wymaganiem JFM+**, ale może pojawić się jako prototyp badawczy, jeśli ręcznie zdefiniowany katalog modeli okaże się niewystarczający.

Jeżeli trafi do JFM+, powinien być traktowany jako eksperyment, nie jako podstawowy mechanizm definiowania modeli.

---

# 5. Zakres JFM++

## 5.1 Definicja JFM++

JFM++ to etap platformowy i badawczy. Jego celem jest umożliwienie definiowania nowych modeli, nowych solverów i głębokich integracji z resztą SemiconductorStudio.

---

## 5.2 JFM++ — Node Editor dla modeli prądu

Do JFM++ wchodzi pełny Node Editor dla modeli prądu.

Wymaga to:

- IR/AST;
- `ParameterRegistry`;
- walidacji grafu;
- CPU evaluator;
- CUDA codegen;
- zgodności CPU/GPU;
- wersjonowania modeli;
- zapisu/odczytu grafów;
- bezpieczeństwa generowanego kodu;
- obsługi parametrów bez rekompilacji;
- stabilnych identyfikatorów parametrów;
- możliwości eksportu modelu.

To jest osobny projekt w projekcie i nie powinno blokować JFM.

---

## 5.3 JFM++ — custom models i pluginy

Wchodzi:

- custom user models;
- plugin system;
- zewnętrzne modele użytkownika;
- dodatkowe backendy obliczeniowe;
- integracje z Pythonem jako pełnym środowiskiem rozszerzeń;
- wersjonowanie modeli;
- udostępnianie modeli między projektami.

---

## 5.4 JFM++ — zaawansowane metody badawcze

Wchodzi:

- Bayesian/MCMC;
- ODR;
- modele z niejednorodną barierą;
- modele wielobarierowe;
- zaawansowane warianty TFE/FE;
- badawcze modele SCLC, jeśli przekraczają zakres JFM;
- integracja z danymi C-V, DLTS, EL, QE;
- sprzężenie z innymi modułami SemiconductorStudio;
- praca serwerowa/HPC.

---

# 6. Tabela zakresu

| Obszar | JFM | JFM+ | JFM++ |
|---|---|---|---|
| Core jako baza aplikacji | tak | tak | tak |
| Import `.dat/.txt/.csv/.tsv` | tak | tak | tak |
| Skrypty Python import/preprocess/prefit | tak, ograniczony kontrakt | batch/kampanie | pluginy i pełna automatyzacja |
| Hot-reload skryptów Python | tak, jeśli stabilne | tak | tak |
| Modele legacy 4P/6P | tak | tak | tak |
| Light 5P/7P | tak, użytkowo | pełna analiza light/dark | rozszerzenia |
| SD-RsRsh / SD-Iph / SD-PLsh | tak | tak | tak |
| TE-IR / TE-SCR | tak, jako modele kandydackie | pełna analiza Raua i globalne T-fit | custom/global mechanistic models |
| BTD / SCLC | tak, jako modele kandydackie/diagnostyczne | analiza mechanizmów i zakresów | rozszerzone badawcze warianty |
| TFE / FE | opcjonalnie diagnostycznie, jeśli gotowe | analiza mechanizmów | pełne modele badawcze |
| Simplex CPU/GPU | tak | tak | tak |
| MC GPU | tak | batch/automatyzacja | HPC/server |
| Trace Simplexa | tak, pełny dla debug | narzędzia diagnostyczne | profiler/zaawansowany debug |
| Reduced $\chi^2$, $\Delta\chi^2$ | tak | tak | tak |
| Model selection | podstawowy | AICc/BIC/residua/stability | meta-model selection |
| Arrhenius | podstawowy | pełna analiza wielu osi | globalne/niestandardowe modele |
| Rau / TE-IR / TE-SCR interpretation | podstawowe dane i wykresy | pełna interpretacja | custom mechanistic workflows |
| Global fitting zależności temperaturowych | nie | tak | rozszerzalne/global custom |
| Niepewności | MC + podstawowe | propagacja, profile, bootstrap | Bayesian/MCMC |
| Node Editor dla modeli prądu | nie | opcjonalny prototyp | tak |
| Eksport Origin TSV | tak | rozszerzony | pipeline/reporting |
| Generator syntetycznych danych | nie jako funkcja użytkowa | testy wewnętrzne | narzędzie walidacyjne |
| Zapis/odczyt pełnej sesji | odłożone | możliwe | pełny system projektu |
| Plugin system | nie | nie / przygotowanie | tak |

---

# 7. Co teraz powinniśmy zrobić

Następny dokument powinien mieć charakter zamrażający zakres:

```text
JFM_VERSION_SCOPE.md
```

Ten dokument może powstać bez projektowania szczegółowego API.

Powinien zawierać:

1. definicję JFM;
2. definicję JFM+;
3. definicję JFM++;
4. listę funkcji JFM;
5. listę funkcji JFM+;
6. listę funkcji JFM++;
7. co świadomie wyrzucamy z JFM;
8. co jest warunkiem ukończenia JFM;
9. które funkcje starego JFM odtwarzamy;
10. które funkcje starego JFM porzucamy;
11. które bugi starego JFM naprawiamy przez redesign;
12. które wyniki z Perplexity trafiają do JFM;
13. które wyniki z Perplexity trafiają do JFM+;
14. listę decyzji otwartych.

Dopiero po zamrożeniu zakresu warto pisać:

```text
CHARACTERISTIC_API.md
NUMERICS_API.md
MODEL_CATALOG.md
PREFIT_AND_BOUNDS_SPEC.md
JFM_UI_WORKFLOW.md
JFM_EXPORT_SPEC.md
JFM_VALIDATION_PLAN.md
```

---

# 8. Decyzje otwarte

Przed projektowaniem API trzeba doprecyzować:

1. Jaka jest dokładna lista modeli aktywnych w pierwszym JFM, a które są oznaczone jako eksperymentalne?
2. Czy TE-IR, TE-SCR, BTD i SCLC mają być domyślnie dostępne w UI, czy ukryte jako modele zaawansowane?
3. Jak dokładnie reprezentować charakterystykę: jako $I(V)$, $J(V)$, czy oba naraz z zależnością przez powierzchnię?
4. Jaki jest kontrakt skryptów Python dla importu, preprocessingu i prefit?
5. Czy Python skrypt prefit może zmieniać zakres dopasowania, bounds i initial guess?
6. Jak rozdzielić bibliotekę numeryczną CPU/GPU: jedna biblioteka z backendami czy dwie biblioteki z jednym kontraktem?
7. Jaki jest format trace Simplexa?
8. Które elementy trace muszą być dostępne w UI?
9. Jak definiujemy „znane wyniki” do testów statystycznych?
10. Jakie zakresy reduced $\chi^2$ uznajemy za akceptowalne?
11. Kiedy uruchamiać Simulated Annealing?
12. Czy Levenberg–Marquardt ma być własny, czy używamy biblioteki open-source?
13. Jak zaprojektować eksport Origin TSV, aby nie zmieniać go co tydzień?
14. Jak zapisywać relacje między serią, charakterystyką, fit runem i MC runem?
15. Kiedy wracamy do Node Editora dla modeli prądu?

---

# 9. Rekomendacja końcowa

Najrozsądniejszy podział jest następujący:

```text
JFM:
  czysta, stabilna, używalna wersja starego JFM
  + poprawiona biblioteka numeryczna CPU/GPU
  + GPU MC
  + katalog znanych modeli
  + Python import/preprocess/prefit
  + podstawowa analiza temperaturowa
  + eksport Origin TSV.

JFM+:
  naukowa interpretacja i automatyzacja:
  model selection, analiza Raua, globalne zależności T,
  zaawansowane niepewności, batch wielu serii, light/dark analysis.

JFM++:
  platforma rozszerzalna:
  Node Editor dla modeli prądu, custom models, pluginy,
  Bayesian/MCMC, ODR, integracje z innymi modułami i HPC.
```

Najbliższy praktyczny krok: zamrozić ten dokument jako `JFM_VERSION_SCOPE.md`, a potem przejść do dwóch dokumentów projektowych:

```text
CHARACTERISTIC_API.md
NUMERICS_API.md
```
