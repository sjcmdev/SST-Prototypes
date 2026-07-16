# JFM — szczegółowy opis funkcjonalności

**Status:** zakres funkcjonalny wersji JFM.  
**Charakter dokumentu:** techniczny raport funkcjonalny.  
**Cel:** zamrożenie funkcji oczekiwanych od pierwszej wersji aplikacji JFM przed projektowaniem API, odpowiedzialności modułów i UI.

Dokument opisuje wyłącznie funkcjonalności wersji **JFM**. Nie opisuje docelowej architektury, implementacji, systemu pluginów ani technologicznego sposobu wykonania. Zakres JFM jest rozumiany jako pierwsza realnie używalna wersja narzędzia do analizy temperaturowych charakterystyk prądowo-napięciowych urządzeń półprzewodnikowych.

---

## 1. Miejsce JFM w projekcie

JFM jest modułem domenowym budowanym na istniejącym Core. Core stanowi wspólną bazę dla prototypów i aplikacji w SemiconductorStudio: dostarcza fundament aplikacji, organizację okien/paneli, integrację z wykresami, obsługę stanu aplikacji, podstawową infrastrukturę danych i mechanizmy wspólne dla kolejnych modułów. JFM nie powinien dublować funkcji Core, tylko korzystać z niego jako z bazy aplikacyjnej.

JFM odpowiada za domenę analizy charakterystyk `I(V,T)` / `J(V,T)`. Zakres obejmuje import danych, preprocessing, wybór zakresu dopasowania, prefit, dopasowanie modeli prądu, tuning parametrów, Monte Carlo, podstawową analizę temperaturową oraz eksport wyników do dalszej analizy w OriginPro lub równoważnym narzędziu.

---

## 2. Granica wersji JFM

JFM ma być uporządkowaną i rozszerzoną wersją funkcji, które w obecnej aplikacji zostały już przetestowane użytkowo. Nie jest to jeszcze platforma do budowania dowolnych modeli z Node Editora ani pełny system globalnego modelowania fizycznego.

Wersja JFM obejmuje:

- analizę wielu charakterystyk prądowo-napięciowych;
- obsługę danych ciemnych i jasnych;
- katalog zdefiniowanych modeli prądu;
- fit pojedynczych charakterystyk;
- analizę rodzin temperaturowych;
- Monte Carlo i ocenę niepewności;
- porównanie modeli w podstawowym zakresie;
- eksport danych, parametrów, niepewności i wyników MC.
- pełny globalny fit fizyczny po wielu temperaturach;
- prosty zapis i odtworzenia stanu projektu projektu jako warunku ukończenia wersji.


## 3. Podstawowe obiekty domenowe

JFM operuje na następujących pojęciach użytkowych.

### 3.1 Projekt

Projekt grupuje dane, serie, konfiguracje dopasowań, wyniki Monte Carlo, wykresy i eksporty związane z jedną analizą. W JFM pełna persystencja projektu może być ograniczona, ale struktura pracy użytkownika ma być projektowa, a nie oparta na luźnej liście plików.

### 3.2 Serie

Seria oznacza logiczną grupę charakterystyk. Typowe przykłady:

- jedna próbka mierzona w wielu temperaturach;
- jedna próbka mierzona w trybie dark i light;
- kilka powtórzeń tego samego pomiaru;
- zestaw charakterystyk porównywanych na jednym wykresie Arrheniusa.

Seria ma nazwę, opis, kolor/preset kolorów i zestaw charakterystyk.

### 3.3 Characteristic

Charakterystyka to pojedynczy zbiór punktów `V-I` albo `V-J` wraz z metadanymi. Minimalne metadane:

- nazwa;
- temperatura;
- typ: dark/light;
- informacja, czy dane reprezentują `I` czy `J`;
- jednostki;
- źródło importu;
- przypisanie do serii;
- aktualny zakres dopasowania;
- aktywny model lub zestaw modeli kandydackich.

### 3.4 FitRun

FitRun to pojedynczy wynik dopasowania konkretnej charakterystyki konkretnym modelem i konkretną konfiguracją. Jeden pomiar może mieć wiele FitRunów, np. dla różnych modeli, zakresów, wartości początkowych lub konfiguracji fixed/bounds.

FitRun przechowuje:

- model;
- zakres punktów;
- wartości początkowe;
- fixed/free;
- bounds;
- wynikowe parametry;
- błędy/statystyki dopasowania;
- status;
- informacje o niepowodzeniu;

### 3.5 MonteCarloRun

MonteCarloRun to symulacja niepewności związana z konkretnym FitRunem albo charakterystyką. Zawiera konfigurację szumu, liczbę iteracji, seed, rozkład otrzymanych parametrów, statystyki oraz dane do wizualizacji.

### 3.6 DerivedAnalysis

DerivedAnalysis obejmuje wykresy i wielkości wyznaczone na podstawie wielu FitRunów, np. zależności Arrheniusa, `A(T)`, `R_s(T)`, `A ln(I_0)` albo energia aktywacji z dopasowania liniowego. Oraz inne analizy opisane w dokumencie `jfm-scientific-research-perplexity.md`

---

## 4. Import danych

JFM umożliwia import danych pomiarowych z plików tekstowych oraz zewnętrznych skryptów importu.

### 4.1 Obsługiwane źródła

W JFM wchodzą:

- import `.dat`;
- import `.txt`;
- import `.csv`;
- import `.tsv`;
- import przez niezależne skrypty Pythonowe dla formatów specyficznych dla aparatury lub laboratorium.

Skrypty importu są częścią workflow JFM. Mają umożliwić obsługę dowolnego formatu który będzie dostarczał wystarczające dane. Powinny być hot-reload oraz możliwość ich dynamicznej rejestracji. 

### 4.2 Rozpoznawanie kolumn

Import rozpoznaje albo pozwala użytkownikowi wskazać:

- kolumnę napięcia `V`;
- kolumnę prądu `I`;
- opcjonalną kolumnę gęstości prądu `J`;
- temperaturę;
- dodatkowe kolumny metadanych, jeżeli występują.

Jeżeli plik zawiera zarówno `I`, jak i `J`, JFM zachowuje obie informacje i pozwala wybrać, która wielkość jest używana w danym FitRunie.

### 4.3 Temperatura i metadane

JFM odczytuje temperaturę:

- z nazwy pliku;
- z nagłówka;
- z kolumny;
- z ręcznej edycji po imporcie.

Temperatura nie może być traktowana jako dekoracyjna etykieta. Jest używana w modelach, analizie temperaturowej, eksporcie i grupowaniu serii.

### 4.4 Import wielu plików

JFM obsługuje:

- import pojedynczego pliku;
- import wielu zaznaczonych plików;
- import całego katalogu;
- przypisanie importowanych danych do istniejącej albo nowej serii;
- automatyczne sortowanie charakterystyk po temperaturze;
- zachowanie kolejności źródłowej jako opcjonalnej informacji.

### 4.5 Kontrola jakości importu

Po imporcie JFM oznacza problemy w danych:

- brak wymaganych kolumn;
- niepoprawne jednostki albo brak jednostek (wtedy przyjmowane są domyślne jednostki);
- brak temperatury;
- zduplikowane punkty;
- niemonotoniczne napięcie;
- puste wartości;
- punkty nienumeryczne;
- podejrzenie saturacji aparatury;
- podejrzenie compliance limit;
- obecność kilku sweepów w jednym pliku.
- rozdzielanie sweepów, jeżeli plik zawiera więcej niż jeden przebieg;

Problemy importu mają być widoczne w aplikacji. Brak poprawnego importu nie powinien kończyć się wyłącznie komunikatem w konsoli.

---

## 5. Preprocessing i prefit

JFM rozdziela preprocessing danych od prefit/estimate parametrów. Preprocessing dotyczy danych, prefit dotyczy modelu.

### 5.1 Preprocessing danych

JFM umożliwia:

- konwersję jednostek;
- rozpoznanie i konwersję `I ↔ J`, jeżeli znana jest powierzchnia;
- korektę znaku prądu lub napięcia, jeżeli użytkownik jawnie ją wybierze;
- usuwanie lub oznaczanie duplikatów;
- oznaczanie punktów wyłączonych z dopasowania;
- zapis informacji, które operacje preprocessingowe zostały wykonane.

Operacje zmieniające dane nie mogą być niewidoczne. Użytkownik musi widzieć, czy pracuje na danych surowych, przetworzonych czy ograniczonych do zakresu dopasowania.

### 5.2 Zewnętrzne skrypty preprocessingu

JFM dopuszcza osobne skrypty do:

- importu;
- preprocessingu;
- prefit;
- walidacji danych.

Skrypt ma jasny status: użyty / nieużyty / zakończony błędem. Wynik skryptu musi być widoczny jako część historii przygotowania charakterystyki.

### 5.3 PreFit / Estimate

Dla każdego modelu objętego JFM istnieje procedura oszacowania parametrów startowych albo jawny status, że prefit dla danego modelu nie jest jeszcze dostępny.

Prefit wyznacza:

- wartości początkowe;
- proponowane bounds;
- sugerowany zakres dopasowania;
- parametry, które można tymczasowo zafiksować;
- ostrzeżenia o słabej identyfikowalności.

Prefit nie jest wynikiem końcowym. Jest punktem startowym dla fitowania i diagnostyki.

---

## 6. Zakres dopasowania

JFM umożliwia automatyczny i ręczny wybór zakresu dopasowania.

### 6.1 Automatyczny zakres

Automatyczne wyznaczenie zakresu obejmuje:

- odcięcie obszaru, w którym prąd ma niepoprawny znak dla danego modelu;
- odcięcie obszaru bardzo niskich napięć, jeżeli dominuje $-I_0$ lub offset;
- wykrywanie wejścia w obszar dominacji `R_s`;
- wykrywanie rolloff/kink, jeżeli wpływa na dopasowanie;
- oznaczenie zakresu użytego w danym FitRunie.

### 6.2 Manualny zakres

Użytkownik może ustawić lewą i prawą granicę zakresu dopasowania:

- z poziomu panelu parametrów;
- bezpośrednio na wykresie;
- skrótami klawiaturowymi dla lewej/prawej granicy, gdy fokus jest na wykresie.

Zmiana zakresu może automatycznie uruchomić ponowny prefit i fit, jeżeli użytkownik włączy taki tryb.

### 6.3 Porównanie zakresów

JFM zapisuje, jaki zakres był użyty w danym FitRunie. Dzięki temu można porównywać wyniki dla kilku zakresów tej samej charakterystyki. Oczywiście potrzebny do tego jest specjalny guzik, który przypisuję daną analizę do zadanej charakterystyki.

---

## 7. Katalog modeli prądu w JFM

JFM zawiera katalog zdefiniowanych modeli prądu. Każdy model ma jawnie podaną listę parametrów, jednostki, typ danych wejściowych, ograniczenia fizyczne i status walidacji.

Nazwy typu `4P`, `5P`, `6P`, `7P` mogą być używane jako aliasy historyczne, ale nie jako jedyny opis modelu. W UI i eksporcie model powinien być identyfikowany przez składniki fizyczne, np. `SD-RsRsh`, `SD-Iph`, `SD-PLsh`.

### 7.1 Modele podstawowe

W JFM wchodzą modele:

| Model | Zastosowanie | Parametry główne |
|---|---|---|
| `SD-0` | idealna dioda, testy i przypadki referencyjne | `I_0`, `A` |
| `SD-Rs` | dioda z rezystancją szeregową | `I_0`, `A`, `R_s` |
| `SD-Rsh` | dioda z bocznikiem | `I_0`, `A`, `R_sh` |
| `SD-RsRsh` | podstawowy model dark | `I_0`, `A`, `R_s`, `R_sh` |
| `SD-Iph` | podstawowy model light | `I_ph`/`I_SC`, `I_0`, `A`, `R_s`, `R_sh` |
| `SD-PLsh` | model z nieliniowym bocznikiem | `I_0`, `A`, `R_s`, `R_sh`, `alpha`, `R_sh2` |

Modele legacy `4P` i `6P` są mapowane na jawne modele składnikowe. JFM zachowuje zdolność odtworzenia workflow starego JFM, ale nie utrwala niejednoznacznego nazewnictwa jako podstawowej konwencji.

### 7.2 Modele light

JFM obsługuje charakterystyki jasne jako pełnoprawny typ danych. Zakres obejmuje:

- model z prądem fotogenerowanym;
- wyznaczanie lub podawanie `I_SC` / `J_SC`;
- modele rozszerzone odpowiadające legacy `5P` i `7P`, jeżeli są wymagane do odtworzenia dotychczasowego workflow;
- osobne parametry i bounds dla trybu light;
- eksport parametrów light razem z metadanymi o oświetleniu, jeżeli są dostępne.

### 7.3 Modele rozszerzone w JFM

JFM zawiera również modele kandydackie potrzebne do rzeczywistej pracy naukowej:

| Model | Rola w JFM |
|---|---|
| `2D-RsRsh` | model dwudiodowy dla przypadków z dwoma kanałami rekombinacji |
| `2D-Iph` | wariant light modelu dwudiodowego |
| `TE-IR` | model tunelowo wzmacnianej rekombinacji interfejsowej |
| `TE-SCR` | model tunelowo wzmacnianej rekombinacji w obszarze ładunku przestrzennego |
| `BTD` | model roll-over / back-contact / S-kink |
| `SCLC` | diagnostyczny model prądów ograniczonych ładunkiem przestrzennym |

* pełny zestaw modeli barierowych i wielobarierowych bez walidacji; (do przedyskutowania)


Każdy model z tej grupy musi mieć w JFM wyraźny status: gotowy do rutynowego użycia, eksperymentalny, diagnostyczny albo wymagający ręcznej interpretacji. JFM nie powinien prezentować wyniku z modelu rozszerzonego jako automatycznego dowodu konkretnego mechanizmu fizycznego.


## 8. Konfiguracja parametrów modelu

Dla każdego modelu JFM udostępnia kontrolę parametrów na poziomie pojedynczej charakterystyki i grupy charakterystyk.

### 8.1 Wartości parametrów

Dla każdego parametru użytkownik widzi i może edytować:

- wartość oszacowaną;
- wartość początkową używaną do fitu;
- wartość dopasowaną;
- wartość tunowaną ręcznie;
- jednostkę;
- status parametru.

### 8.2 Fixed/free

Każdy parametr może być:

- swobodny;
- zafiksowany przed fitowaniem;
- zafiksowany na wartości wynikającej z tuningu;
- odfiksowany bez przechodzenia przez obejścia w innym panelu.

Fixed/free działa w trybie CPU i GPU.

### 8.3 Bounds

Dla każdego parametru można ustawić:

- dolną granicę;
- górną granicę;
- brak jednej z granic;
- bounds wynikające z prefit;
- bounds ręczne nadpisujące prefit.

JFM wykrywa błędne bounds, np. `min >= max`, i nie uruchamia fitu z niepoprawną konfiguracją.

### 8.4 Log-scale

Dla parametrów dodatnich o dużym zakresie wartości JFM pozwala pracować w skali logarytmicznej. Dotyczy to przede wszystkim parametrów typu `I_0`, `R_s`, `R_sh`, `R_sh2` i prefaktorów.

---

## 9. Dopasowanie charakterystyki

JFM umożliwia dopasowanie pojedynczej charakterystyki oraz wielu charakterystyk wybranych jednocześnie.

### 9.1 Tryby dopasowania

W JFM wchodzą:

- fit aktywnej charakterystyki;
- fit zaznaczonych charakterystyk;
- fit całej serii;
- ponowny fit po zmianie zakresu;
- ponowny fit po zmianie modelu;
- ponowny fit po zmianie bounds/fixed/initial guess;
- fit wielu modeli kandydackich dla tej samej charakterystyki.

### 9.2 Wynik dopasowania

Każdy FitRun zawiera:

- status: sukces, niepowodzenie, przerwany, wynik podejrzany, parametry na granicy;
- wartości parametrów;
- reduced `chi^2`;
- `Delta chi^2` względem najlepszego znanego wyniku dla danej konfiguracji;
- liczbę iteracji;
- liczbę ewaluacji funkcji celu;
- informację o parametrach na granicach;
- informację o parametrach niefizycznych;
- residuals;
- krzywą dopasowaną;
- konfigurację, z której wynik powstał.

### 9.3 Trace Simplexa

JFM zachowuje istniejącą funkcjonalność pełnego trace dla pojedynczego przebiegu debugowanego:

- parametry w kolejnych krokach;
- wartości `chi^2` w kolejnych krokach;
- najlepszy punkt;
- najgorszy punkt;
- typ wykonanej operacji Simplexa;
- możliwość wyświetlenia przebiegu `chi^2` i zmian parametrów.

Trace jest funkcją diagnostyczną. Nie jest wymagane przechowywanie pełnego trace dla tysięcy przebiegów Monte Carlo.

### 9.4 Ocena końcowa fitu

JFM traktuje reduced `chi^2` i `Delta chi^2` jako końcową ocenę jakości dopasowania. Wynik fitu nie jest oceniany wyłącznie wizualnie ani przez samą wartość błędu bez liczby stopni swobody.

Ocena fitu uwzględnia:

- reduced `chi^2`;
- `Delta chi^2`;
- residua;
- fizyczność parametrów;
- obecność parametrów na granicach;
- stabilność wyniku po zmianie zakresu lub modelu, jeżeli takie porównanie wykonano.

---

## 10. Porównywanie modeli w JFM

JFM ma podstawowy mechanizm porównywania modeli dla tej samej charakterystyki. Nie jest to jeszcze pełny system automatycznej interpretacji fizycznej.

Porównanie modeli obejmuje:

- uruchomienie kilku modeli kandydackich;
- zestawienie parametrów;
- zestawienie reduced `chi^2`;
- zestawienie `Delta chi^2`;
- informację o liczbie parametrów;
- informację o parametrach niefizycznych;
- porównanie residuów;
- wskazanie modelu rekomendowanego jako najlepszego użytkowo.

Rekomendacja modelu nie jest ostateczną interpretacją mechanizmu fizycznego. Użytkownik może nadpisać wybór modelu.

---

## 11. Tuning charakterystyki

JFM zachowuje i porządkuje funkcję ręcznego tuningu parametrów.

### 11.1 Widoki parametrów

Użytkownik widzi równolegle:

- parametry oszacowane (`estimated`);
- parametry dopasowane (`fitted`);
- parametry ręcznie tunowane (`tuned`).

### 11.2 Live preview

Zmiana parametru tunowanego natychmiast aktualizuje krzywą tunowaną na wykresie. Krzywa tunowana jest niezależna od krzywej dopasowanej.

### 11.3 Przeniesienie tuningu do fitu

JFM umożliwia:

- użycie wartości tuned jako nowych wartości początkowych;
- użycie wybranych wartości tuned jako fixed;
- przepisanie tuned do konfiguracji kolejnego fitu;
- cofnięcie zmian tuningu;
- reset tuned do fitted.

### 11.4 Auto-fit po tuningu

JFM może działać w trybie, w którym zmiana tuningu wybranych parametrów automatycznie uruchamia ponowny fit. Tryb ten jest jawnie włączany przez użytkownika.

---

## 12. Monte Carlo i niepewności

JFM obejmuje Monte Carlo jako funkcję podstawową, nie jako późniejsze rozszerzenie.

### 12.1 Uruchamianie MC

JFM umożliwia:

- MC dla aktywnej charakterystyki;
- MC dla zaznaczonych charakterystyk;
- MC dla całej serii;
- zapis wielu niezależnych MC runów dla tego samego FitRunu;
- powtórzenie MC z innym seedem;
- porównanie wyników kilku MC runów.

### 12.2 Konfiguracja MC

Użytkownik ustawia:

- liczbę iteracji;
- model szumu;
- poziom szumu;
- seed (jeśli musimy to eksportować, to powinno to być w ustawieniach numerycznyc aplikacji);
- liczbę zapisywanych próbek;
- model i FitRun, na którym MC bazuje;
- presety MC.

### 12.3 Wyniki MC

JFM pokazuje:

- rozkłady parametrów;
- scatter ploty dla par parametrów;
- elipsoidy kowariancji;
- poziomy `Delta chi^2`;
- korelacje parametrów;
- statystyki centralne;
- percentyle;
- nieudane przebiegi;
- odsetek przebiegów zakończonych niepowodzeniem;
- parametry na granicach.

### 12.4 Niepewności

JFM raportuje niepewności parametrów wynikające z MC oraz podstawowe wielkości pochodne. Dla JFM wystarczający jest nacisk na niepewności empiryczne z MC i rozkłady parametrów. Pełne profilowanie `chi^2`, bootstrapy specjalistyczne i zaawansowana propagacja przez złożone modele temperaturowe przechodzą do JFM+.

---

## 13. Analiza temperaturowa i wykresy pochodne

JFM zapewnia podstawową analizę rodzin temperaturowych.

### 13.1 Wykresy parametrów względem temperatury

JFM zawiera wykresy:

- `I_0(T)` / `J_0(T)`;
- `ln(I_0)(T)` / `ln(J_0)(T)`;
- `A(T)`;
- `R_s(T)`;
- `R_sh(T)`;
- `alpha(T)`;
- `R_sh2(T)`, jeżeli model je zawiera;
- parametry light, jeżeli model je zawiera.

### 13.2 Osie temperaturowe

Dla wykresów temperaturowych użytkownik może wybrać oś X:

- `T`;
- `1/T`;
- `1000/T`;
- `1/(kT)`.

Dla wykresów zależnych od ideality factor JFM dopuszcza również oś związaną z `1/(A kT)`, jeżeli potrzebna jest do podstawowej analizy aktywacyjnej.

### 13.3 Wykresy Arrheniusa

JFM zawiera:

- `ln(I_0)` / `ln(J_0)` vs `1000/T`;
- `ln(I_0)` / `ln(J_0)` vs `1/(kT)`;
- `A ln(I_0)` / `A ln(J_0)` vs `1/(kT)`;
- podstawowe dopasowanie liniowe wybranego zakresu;
- wyznaczenie nachylenia, przecięcia i energii aktywacji;
- eksport punktów, dopasowania i niepewności.

### 13.4 Wiele serii Arrheniusa

JFM obsługuje wiele serii na wykresach temperaturowych. Użytkownik może porównać parametry kilku próbek albo kilku serii pomiarowych na jednym wykresie. W tym dopasowania różnych funkcji opisujących zmiany tych parametrów (wiele fitów dla jednej serii) oraz serie pomiędzy sobą

### 13.5 Granica analizy temperaturowej w JFM

JFM pokazuje i dopasowuje podstawowe zależności temperaturowe. Pełna interpretacja mechanizmów Raua, globalne dopasowanie mechanizmu po wielu temperaturach i analiza odwrotna przechodzą do JFM+.

---

## 14. Wykresy charakterystyk i residuów

JFM zapewnia główne wykresy potrzebne do oceny dopasowania.

### 14.1 Charakterystyki

Na głównym wykresie można niezależnie pokazywać:

- dane surowe (`raw`);
- dane w zakresie dopasowania (`ranged`);
- krzywą dopasowaną (`fitted`);
- krzywą tunowaną (`tuned`).

Dla każdej warstwy użytkownik może ustawić widoczność, kolor i grubość.

### 14.2 Skale osi

JFM obsługuje:

- skalę liniową;
- skalę logarytmiczną;
- signed-log dla danych zmieniających znak (funkcja odłożona; nie należy do bieżącej iteracji prototypu);
- osobne ustawienia dla `I` i `J`, jeżeli obie wielkości są dostępne.

### 14.3 Residua

JFM pokazuje residua:

- bezwzględne;
- względne;
- logarytmiczne; signed-log pozostaje odłożony razem z obsługą tej skali dla wykresów;
- jako osobny wykres albo panel diagnostyczny.

### 14.4 Widoczność i aktywność

JFM obsługuje:

- aktywną charakterystykę;
- wiele widocznych charakterystyk;
- `hide non-active`;
- szybkie ukrywanie/pokazywanie serii;
- presety kolorów dla rodzin temperaturowych.

---

## 15. Zarządzanie charakterystykami w UI

JFM ma zapewnić wygodne zarządzanie dużą liczbą charakterystyk.

### 15.1 Drzewo projektu

Główny widok organizacyjny obejmuje:

- projekt;
- serie;
- charakterystyki;
- FitRuny;
- MonteCarloRuny;
- wykresy pochodne.

### 15.2 Multi-select

JFM obsługuje:

- zaznaczanie pojedyncze;
- Ctrl-select;
- Shift-select;
- drag select;
- zaznaczenie całej serii;
- odwrócenie zaznaczenia.

### 15.3 Context menu

Dla serii i charakterystyk dostępne są akcje:

- hide/unhide;
- select/unselect;
- invert selection;
- change model;
- set dark/light;
- fit selected;
- run MC selected;
- change color preset;
- remove;
- export selected.

### 15.4 Statusy w drzewie

Drzewo projektu pokazuje statusy:

- zaimportowane;
- wymaga prefit;
- fitted;
- fit failed;
- MC available;
- warning;
- excluded;
- model candidate;
- result outdated po zmianie danych lub konfiguracji.

---

## 16. Dane jasne i ciemne

JFM traktuje dark i light jako jawne typy charakterystyk.

### 16.1 Dark

Dla dark JFM obsługuje standardowe modele diodowe, modele z rezystancjami pasożytniczymi, nieliniowy bocznik i modele rozszerzone wskazane w katalogu.

### 16.2 Light

Dla light JFM obsługuje:

- prąd fotogenerowany;
- `I_SC` / `J_SC`;
- modele light odpowiadające rozszerzonym modelom dark;
- fitting charakterystyk jasnych bez przepinania ich przez modele dark;
- eksport parametrów light.

### 16.3 Analizy light/dark poza JFM

Pełna analiza superposition, cross-over dark/light, `V_OC(T)`, `J_SC(T)` i voltage-dependent collection należy do JFM+. W JFM wystarczy poprawna obsługa importu, fitu i eksportu danych light.

---

## 17. Eksport danych

JFM ma eksportować dane w sposób wygodny do dalszej pracy w OriginPro. Podstawowym formatem eksportu jest `.tsv`.

### 17.1 Eksport parametrów

Eksport parametrów zawiera:

- nazwę serii;
- nazwę charakterystyki;
- temperaturę;
- typ dark/light;
- model;
- zakres dopasowania;
- parametry fitted;
- niepewności;
- reduced `chi^2`;
- `Delta chi^2`;
- status fitu;
- flagi ostrzeżeń.

### 17.2 Eksport krzywych

JFM eksportuje:

- dane surowe;
- dane użyte w zakresie dopasowania;
- krzywą fitted;
- krzywą tuned;
- residua;
- wartości modelu dla każdego modelu kandydackiego, jeżeli były liczone.

### 17.3 Eksport MC

JFM eksportuje:

- surowe wyniki MC;
- parametry każdej iteracji;
- `Delta chi^2`;
- status iteracji;
- percentyle;
- macierz kowariancji albo dane potrzebne do jej odtworzenia;
- dane scatter plotów.

Jeden MC run powinien dawać jednoznaczny zestaw plików, bez mieszania wyników kilku konfiguracji w jednym nieopisanym pliku.

### 17.4 Eksport Arrheniusa i wykresów pochodnych

JFM eksportuje:

- punkty wykresów temperaturowych;
- wartości osi po przeliczeniu;
- dopasowania liniowe;
- nachylenia;
- przecięcia;
- energie aktywacji;
- niepewności;
- opis użytego zakresu temperatur.

### 17.5 Brak zależności od Pythona dla podstawowego eksportu

Podstawowy eksport danych wymaga Pythona. 

---

## 18. Komunikaty, ostrzeżenia i błędy

JFM ma mieć widoczne dla użytkownika statusy i komunikaty błędów.

### 18.1 Import

Użytkownik widzi:

- które pliki zostały zaimportowane;
- które pliki zostały pominięte;
- dlaczego import się nie powiódł;
- które dane wymagają ręcznego przypisania kolumn albo temperatury.

### 18.2 Fit

Użytkownik widzi:

- czy fit się powiódł;
- czy zakończył się na granicy parametrów;
- czy wynik jest niefizyczny;
- czy wynik jest podejrzany statystycznie;
- czy model jest nieidentyfikowalny w danym zakresie;
- czy dane wymagają zmiany zakresu lub modelu.

### 18.3 MC

Użytkownik widzi:

- liczbę udanych i nieudanych iteracji;
- powody niepowodzeń;
- czy rozkład parametrów jest asymetryczny;
- czy parametry często trafiają w granice;
- czy wynik MC jest zgodny z fitowaniem bazowym.

---

## 19. Minimalne warunki ukończenia JFM

JFM można uznać za funkcjonalnie domknięty, gdy spełnia następujące warunki:

1. Importuje rodziny charakterystyk i poprawnie przypisuje temperatury.
2. Pozwala zorganizować dane w serie.
3. Obsługuje dark i light jako osobne typy charakterystyk.
4. Ma katalog modeli obejmujący podstawowe modele dark/light oraz modele rozszerzone wskazane dla JFM.
5. Dla modeli podstawowych posiada prefit, bounds i sensowne wartości startowe.
6. Umożliwia ręczną korektę zakresu, parametrów, bounds i fixed/free.
7. Wykonuje fit CPU/GPU z porównywalną semantyką wyników.
8. Zachowuje trace Simplexa dla pojedynczych przebiegów debugowych.
9. Pokazuje raw/ranged/fitted/tuned oraz residua.
10. Umożliwia tuning charakterystyki.
11. Uruchamia MC dla jednej i wielu charakterystyk.
12. Pokazuje rozkłady MC, scatter ploty i elipsoidy kowariancji.
13. Raportuje reduced `chi^2`, `Delta chi^2` i status fitu.
14. Tworzy podstawowe wykresy temperaturowe i Arrheniusa.
15. Obsługuje wiele serii na wykresach temperaturowych.
16. Eksportuje parametry, krzywe, MC i dane Arrheniusa do `.tsv`.
17. Pokazuje błędy i ostrzeżenia w UI, nie tylko w konsoli.

---

## 21. Decyzje wymagające doprecyzowania przed API

Przed projektowaniem API należy doprecyzować:

1. Ostateczną listę modeli oznaczonych w JFM jako rutynowe, eksperymentalne i diagnostyczne.
2. Minimalny zestaw prefitów wymaganych dla ukończenia JFM.
3. Semantykę `FitRun`, `MonteCarloRun` i `DerivedAnalysis`.
4. Format wyników reduced `chi^2` i `Delta chi^2` w eksporcie.
5. Domyślne modele szumu dla MC.
6. Zakres informacji przechowywanych w trace Simplexa.
7. Zasady porównania CPU/GPU w testach statystycznych.
8. Konwencję znaków dla dark/light.
9. Konwencję nazw parametrów w UI i eksporcie.
10. Zakres odpowiedzialności skryptów Pythonowych dla importu, preprocessingu, prefit i eksport.

