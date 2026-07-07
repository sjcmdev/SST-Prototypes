# JFM – Wymagania Naukowe dla Narzędzia do Analizy I(V,T) Urządzeń Półprzewodnikowych

> **Dokument samowystarczalny.** Przeznaczony dla osoby projektującej narzędzie JFM.
> Pisany po polsku; angielskie nazwy modeli i terminów technicznych zachowane tam, gdzie są standardem.
> Wersja rozszerzona – uwzględnia modele tunelowania wzmocnionej rekombinacji (TE-IR, TE-SCR),
> modele z nieliniowym bocznikiem, charakterystyki ciemne i jasne oraz pełen przegląd mechanizmów.

---

## Spis treści

1. Katalog modeli prądu
2. Postać analityczna i numeryczna modeli
3. Interpretacja fizyczna parametrów
4. Model i metodologia Uwego Raua (tunelowanie wzmocniona rekombinacja)
5. Wykresy podstawowe
6. Konstrukcja osi do bezpośredniego odczytu energii
7. Dobór zakresu dopasowania
8. Dopasowanie niezależne i analiza rodzin temperatur
9. Funkcja celu i model błędu
10. Niepewności parametrów dopasowania
11. Propagacja niepewności
12. Niepewność dopasowań Arrheniusa
13. Identyfikowalność parametrów
14. Automatyczny wybór modelu
15. Mechanizmy fizyczne i ich sygnatury
16. Preprocessing i jakość danych
17. Dane oświetlone i ciemne
18. Walidacja naukowa modeli
19. Minimalny i rozszerzony zakres naukowy
20. Otwarte pytania domenowe
21. Podsumowanie wymagań, listy rekomendacji i bibliografia

---

## 1. Katalog modeli prądu

### 1.1 Konwencja nazewnictwa

Nazwy „4P", „5P", „6P" są niejednoznaczne – różne publikacje stosują je dla różnych zestawów parametrów.
JFM stosuje nazewnictwo oparte na obecnych składnikach modelu:

| Skrót JFM | Znaczenie                                                             | Skrót JFM | Znaczenie |
| --------- | --------------------------------------------------------------------- | ------ | ------------------------------------------------------------ |
| SD-0      | Single-diode, brak oporów pasożytniczych (idealne równanie Shockleya) | TE-IR  | Tunneling-enhanced interface recombination                   |
| SD-Rs     | Single-diode + R_s                                                    | TE-SCR | Tunneling-enhanced space-charge recombination                |
| SD-Rsh    | Single-diode + R_sh                                                   | TE     | Thermionic emission (złącze Schottky lub bariera kontaktowa) |
| SD-RsRsh  | Single-diode + R_s + R_sh (klasyczny model 5-parametrowy)             | TFE    | Thermionic-field emission                                    |
| SD-Iph    | SD-RsRsh + prąd fotogenerowany I_ph (model oświetlonego ogniwa)       | FE     | Field emission (tunelowanie czyste)                          |
| 2D-RsRsh  | Two-diode + R_s + R_sh                                                | SCLC   | Space-charge-limited current                                 |
| 2D-Iph    | 2D-RsRsh + I_ph                                                       | BTD    | Back-to-back diode (anti-parallel, model roll-over/S-kink)   |
| SD-PLsh   | SD-RsRsh + potęgowy człon bocznika (power-law shunt)                  |
 

---

### 1.2 Tabela modeli

#### Model SD-0 – Idealne równanie Shockleya

$$
I = I_0 \left[\exp\!\left(\frac{qV}{AkT}\right) - 1\right]
$$

| Parametr | Jednostka           | Znaczenie fizyczne      | Typowy zakres |
| -------- | ------------------- | ----------------------- | ------------- |
| I_0      | A (lub J_0 w A/cm²) | Prąd nasycenia diody    | 10⁻¹²–10⁻⁶ A  |
| A        | —                   | Współczynnik idealności | 1–2           |
| T        | K                   | Temperatura             | 100–400 K     |

**Założenia:** idealne złącze p-n bez oporów pasożytniczych, rekombinacja dyfuzyjna lub SRH.
**Zakres stosowalności:** kryształy Si/Ge, analityczne testy referencyjne.
**Typowe problemy:** zawyżanie prądu w obszarze dużych napięć; nie opisuje nasycenia.

---

#### Model SD-Rs – Single-diode z rezystancją szeregową

$$
I = I_0 \left[\exp\!\left(\frac{q(V - IR_s)}{AkT}\right) - 1\right]
$$

Równanie **implicit** w I. Wymaga rozwiązania numerycznego lub funkcji Lamberta W.

| Parametr | Jednostka   | Znaczenie                                               |
| -------- | ----------- | ------------------------------------------------------- |
| I_0      | A           | Prąd nasycenia                                          |
| A        | —           | Współczynnik idealności                                 |
| R_s      | Ω lub Ω·cm² | Opór szeregowy (metalowe kontakty, transport neutralny) |

**Zakres stosowalności:** urządzenia z istotnym R_s, analiza obszaru dużych napięć.
**Typowe problemy:** silna korelacja A–I_0 oraz A–R_s; przy V ≫ AkT/q dominacja R_s maskuje mechanizm rekombinacji.

---

#### Model SD-Rsh – Single-diode z rezystancją bocznikującą

$$
I = I_0 \left[\exp\!\left(\frac{qV}{AkT}\right) - 1\right] + \frac{V}{R_{sh}}
$$

| Parametr | Znaczenie |
| -------- | --------- |
| R_sh     | Ω·cm²     | Rezystancja bocznikująca (upływy, pinholes) |

**Zakres stosowalności:** analiza reverse bias i małych napięć forward.
**Typowe problemy:** R_sh ukrywa inny mechanizm upływu przy małych prądach.

---

#### Model SD-RsRsh – Klasyczny model 5-parametrowy

$$
I = I_0 \left[\exp\!\left(\frac{q(V - IR_s)}{AkT}\right) - 1\right] + \frac{V - IR_s}{R_{sh}}
$$

Równanie implicit w I; 5 parametrów: I_0, A, R_s, R_sh, ewentualnie T.
Rozwiązanie przez Lambert W (Jain & Kapoor 2004):

$$
I = \frac{R_{sh}(I_0 + I_{ph}) - V}{R_s + R_{sh}} - \frac{AkT/q}{R_s} W\!\left(\frac{R_s I_0 R_{sh}}{AkT/q (R_s + R_{sh})} \exp\!\left(\frac{R_{sh}(R_s I_0 + R_s I_{ph} + V)}{AkT/q (R_s + R_{sh})}\right)\right)
$$

**Typowe problemy:** przy wysokich temperaturach lub małym R_sh korelacja A–I_0 uniemożliwia jednoznaczne wyznaczenie obu parametrów.

---

#### Model SD-Iph – Oświetlone ogniwo słoneczne (5P z I_ph)

$$
I = I_{ph} - I_0 \left[\exp\!\left(\frac{q(V - IR_s)}{AkT}\right) - 1\right] - \frac{V - IR_s}{R_{sh}}
$$

| Parametr | Znaczenie                                        |
| -------- | ------------------------------------------------ |
| I_ph     | Prąd fotogenerowany; ≈ I_SC dla idealnego złącza |

**Uwaga:** superposition principle zakłada liniową superpozycję prądu diody i fotoproądu. W cienkowarstwowych heterozłączach zawodzi (patrz §17).

---

#### Model 2D-RsRsh – Model dwudiodowy

$$
I = I_{01}\left[\exp\!\left(\frac{q(V-IR_s)}{A_1 kT}\right)-1\right] + I_{02}\left[\exp\!\left(\frac{q(V-IR_s)}{A_2 kT}\right)-1\right] + \frac{V-IR_s}{R_{sh}}
$$

| Dioda            | Typowy A               | Mechanizm |
| ---------------- | ---------------------- | --------- |
| Dioda 1 (A₁ ≈ 1) | dyfuzja w QNR          |
| Dioda 2 (A₂ ≈ 2) | rekombinacja SRH w SCR |

**Model 2D-Iph** dodaje człon I_ph.
**Typowe problemy:** przy danych z ograniczonym zakresem T lub V parametry I_01, I_02, A_1, A_2 są silnie skorelowane; model 7-parametrowy często nie jest identyfikowalny.

---

#### Model SD-PLsh – Single-diode z potęgowym bocznikiem

$$
I = I_0\!\left[\exp\!\left(\frac{q(V-IR_s)}{AkT}\right) - 1\right] + \frac{V - IR_s}{R_{sh}} + \frac{(V - IR_s)^\alpha}{R_{sh2}}
$$

Równanie (30) z załączonego materiału źródłowego. Parametr α > 1 opisuje nieliniowy kanał upływu.

| Parametr | Jednostka | Znaczenie                                      |
| -------- | --------- | ---------------------------------------------- |
| R_sh2    | Ω·cm^α    | Efektywna rezystancja kanału upływu            |
| α        | —         | Wykładnik nieliniowości; α = 1 → liniowy shunt |

**Fizyczne uzasadnienie:** mikrozwarcia o charakterze nieohm'owym, konduktywne ścieżki grain-boundary, SCLC w ograniczonym zakresie napięć.
**Pułapki:** α i R_sh2 są parametrami deskryptorów, nie mają prostej interpretacji mikrofizycznej. JFM powinien raportować ten model jako „użytkowy/diagnostyczny", nie jako „dowód konkretnego mechanizmu".

---

#### Model TE-IR – Tunneling-enhanced interface recombination

Źródło: Rau (1999) *Appl. Phys. Lett.* 74(1), 111; Scheer & Schock *Chalcogenide Photovoltaics* (2011).

$$
J_r = J_{00}\exp\!\left(-\frac{\Phi_f^b}{A' kT}\right)\exp\!\left(\frac{qV}{A' kT}\right)
$$

gdzie efektywny współczynnik idealności:

$$
A' = \frac{A}{\alpha} = \frac{1}{\alpha}\frac{E_{00}}{kT}\coth\!\left(\frac{E_{00}}{kT}\right)
$$

oraz efektywna bariera flat-band:

$$
\Phi_f^b = \frac{\Phi_f^b}{\alpha}
$$

| Parametr | Jednostka | Znaczenie fizyczne                                             |
| -------- | --------- | -------------------------------------------------------------- |
| J_00     | A/cm²     | Amplituda prądu rekombinacji interfejsowej                     |
| E_00     | eV        | Charakterystyczna energia tunelowania; E_00 = (ħ/2)√(N/m*·ε_s) |
| α        | —         | Współczynnik korekcji dla geometrii bariery; α ≤ 1             |
| Φ_f^b    | eV        | Bariera flat-band (niemodyfikowana przez tunelowanie)          |
| A'       | —         | Efektywny współczynnik idealności; A'(T) → A przy T → ∞        |

**Zachowanie temperaturowe A'(T):**
Przy T → ∞: A' → A/α (wartość stała)
Przy T → 0: A' → (A/α)(E_00/kT) rośnie jak 1/T → duże A' przy niskich T

**Zakres stosowalności:** heterozłącza (CIGSe, CdTe, organiczne), złącza z wysoką gęstością stanów pułapkowych na interfejsie.
**Typowe problemy:** model TE-IR i TE-SCR mogą dawać podobne A(T) – wymagana analiza E_a z wykresu Arrheniusa.

---

#### Model TE-SCR – Tunneling-enhanced space-charge recombination

Źródło: Rau (1999); Bayhan & Ercelebi (2005) *Turk. J. Phys.*

$$
J_r = J_{00}\exp\!\left(-\frac{E_g}{AkT}\right)\exp\!\left(\frac{qV}{AkT}\right)
$$

Zależność A(T) dana wzorem (Rau 1999, eq. 6):

$$
\frac{1}{A} = \frac{1}{2}\left(1 - \frac{E_{00}^2}{3(kT)^2} + \frac{kT}{E^*}\right)
$$

gdzie E* jest energią charakterystyczną zależną od geometrii SCR.

| Parametr | Jednostka | Znaczenie                                           |
| -------- | --------- | --------------------------------------------------- |
| E_g      | eV        | Przerwa wzbroniona (lub E_a = E_a interfejsu)       |
| E_00     | eV        | Energia charakterystyczna tunelowania (jak w TE-IR) |
| E*       | eV        | Energia opisująca zakrzywienie bariery SCR          |

**Diagnostyka:** przy E_00 → 0 model redukuje się do czystej rekombinacji SCR z A ≈ 2.
Przy rosnącym E_00: A > 2 przy niskich T, 1/A(T) wykazuje charakterystyczne odchylenie od linii prostej.

---

#### Model TE – Thermionic emission (złącza Schottky, bariery kontaktowe)

Źródło: Padovani & Stratton (1966) *Solid-State Electronics* 9, 695; Dimitrijev (2012).

$$
J = A^{**}T^2 \exp\!\left(-\frac{q\phi_B}{kT}\right)\left[\exp\!\left(\frac{qV}{kT}\right) - 1\right]
$$

| Parametr | Jednostka  | Znaczenie                                                  |
| -------- | ---------- | ---------------------------------------------------------- |
| A**      | A/(cm²·K²) | Efektywna stała Richardsona; A** ≈ 120 A·cm⁻²·K⁻² dla n-Si |
| φ_B      | eV         | Wysokość bariery Schottky'ego                              |

**Zależność T:** J_0 = A**T² exp(−qφ_B/kT) → Arrhenius modyfikowany przez T².
**Różnica od dyfuzji:** J_0 ∝ T² zamiast T^(3+γ/2); wykres ln(J_0/T²) vs 1/T jest liniowy.
**Typowe problemy:** image-force lowering zmniejsza φ_B z reverse-bias, generując pozorną zależność J_0(V_r).

---

#### Model TFE – Thermionic-field emission

Źródło: Padovani & Stratton (1966) – pełny model przejścia TE→TFE→FE.

$$
J \propto \exp\!\left(-\frac{q(\phi_B - V)}{E_0}\right), \quad E_0 = E_{00}\coth\!\left(\frac{E_{00}}{kT}\right)
$$

Warunek TFE: E_00 ≈ kT (tj. energia tunelowania porównywalna z energią termiczną).
Warunek FE: E_00 ≫ kT (tunelowanie dominujące niezależnie od T).
Warunek TE: E_00 ≪ kT (klasyczne przejście termiczne).

---

#### Model BTD – Back-to-back diode (anti-parallel)

Źródło: Scheer & Schock (2011); astro1.panet.utoledo.edu (S-kink PVSC 2016).

Dwie diody antyrównoległe opisują roll-over i S-kink w charakterystykach jasnych cienkowarstwowych ogniw:

$$
I_{total} = I_{main\_diode}(V_m) + I_{back\_diode}(-V_b)
$$

gdzie V = V_m + V_b (zasada podziału napięcia).

**Fizyczne uzasadnienie:** bariera back-contact (φ_bp > 0.3 eV prowadzi do roll-over; Scheer & Schock, Rule 7).
**Diagnostyka:** roll-over widoczny w ciemnej i jasnej charakterystyce; cross-over pojawia się gdy J_back0 < J_main0.

---

#### Model SCLC – Space-charge-limited current

Źródło: Mott & Gurney (1948); Mark & Helfrich (1962); Rose (1955).

Bez pułapek (Mott-Gurney):
$$
J = \frac{9}{8}\varepsilon\mu\frac{V^2}{d^3}
$$

Z wykładniczym rozkładem pułapek (trap-SCLC):
$$
J \propto V^{l+1}, \quad l = T_t/T \gg 1
$$

gdzie T_t jest temperaturą charakterystyczną rozkładu pułapek.

**Pułapka identyfikacyjna:** charakterystyka J ∝ V² (lub wyższy wykładnik) może imitować diodowe zachowanie w ograniczonym zakresie napięć; wykres log-log J vs V jest kluczową diagnostyką.

---

### 1.3 Mapowanie konwencji „nP"

| Konwencja  | Najczęstsza interpretacja             | Parametry                           |
| ---------- | ------------------------------------- | ----------------------------------- |
| 2P         | I_0, A                                | SD-0                                |
| 3P         | I_0, A, R_s lub I_0, A, R_sh          | SD-Rs lub SD-Rsh                    |
| 4P         | I_0, A, R_s, R_sh                     | SD-RsRsh bez I_ph                   |
| 5P (PV)    | I_ph, I_0, A, R_s, R_sh               | SD-Iph (najczęstszy w fotowoltaice) |
| 5P (dioda) | I_0, A_1, A_2, I_02, R_s              | wariant 2D bez R_sh                 |
| 6P         | I_ph, I_01, A_1, I_02, A_2, R_s       | 2D-Iph bez R_sh                     |
| 7P         | I_ph, I_01, A_1, I_02, A_2, R_s, R_sh | pełny 2D-Iph                        |

**Rekomendacja JFM:** zawsze wypisywać listę parametrów jawnie, nie używać skrótów „nP" jako jedynego identyfikatora modelu.

---

## 2. Postać analityczna i numeryczna modeli

### 2.1 Klasyfikacja równań

| Model     | I(V) jawne | V(I) jawne | Implicit | Lambert W | Newton/Halley |
| --------- | ---------- | ---------- | -------- | --------- | ------------- |
| SD-0      | ✓          | ✓ (log)    | —        | —         | —             |
| SD-Rs     | —          | ✓          | ✓        | ✓         | ✓             |
| SD-Rsh    | ✓          | —          | —        | —         | —             |
| SD-RsRsh  | —          | —          | ✓        | ✓         | ✓             |
| 2D-RsRsh  | —          | —          | ✓        | częściowo | ✓             |
| TE-IR/SCR | —          | —          | ✓        | ✓         | ✓             |
| BTD       | —          | —          | ✓        | —         | ✓ (2 etapy)   |

### 2.2 Stabilność numeryczna

**Overflow:** `exp(qV/AkT)` exploduje dla V > 10·AkT/q ≈ 0.25 V (przy A=1, T=300K).
Zabezpieczenie:
```
x = q(V - I*Rs) / (A*kT)
if x > x_max: return I_0*(exp(x_max) + x - x_max) + ...  # linear extrapolation
```

**Underflow:** przy bardzo niskich T lub dużym A argumenty funkcji exp mogą być < -700.
Zabezpieczenie: soft-clipping z minimalną wartością exp(-600).

**Lambert W:** zalecana implementacja przez `scipy.special.lambertw` lub algorytm Fritsch-Shafer-Crowley (błąd < 10⁻¹⁵). Dla dużych argumentów W₀(x) ≈ ln(x) − ln(ln(x)).

**Implicit Newton-Raphson** (zalecane dla masowych obliczeń):
- Start: I_prev = I_ph (dla oświetlonych), I_prev = I_D_approx (dla ciemnych)
- Kryterium zbieżności: |ΔI|/|I| < 10⁻⁸
- Zwykle 3–5 iteracji; dla ciągłości wykresu ważne śledzenie rozwiązania po napięciu

**Metoda Halleya** (zbieżność rzędu 3) – preferowana gdy konieczna wysoka dokładność przy małej liczbie iteracji.

### 2.3 Dane w skali logarytmicznej

Dopasowanie `ln|I|` vs V jest numerycznie stabilniejsze w obszarze wykładniczym, ale:
- nieodpowiednie przy zmianie znaku prądu (potrzebny signed-log)
- daje nadmierną wagę małym prądom (co może maskować błędy w obszarze dużych prądów)
- residua logarytmiczne odpowiadają względnym błędom

**Signed-log:** `slog(I) = sign(I)·log(1 + |I|/I_ref)` – ciągłe przez zero, zachowuje oba znaki.

---

## 3. Interpretacja fizyczna parametrów

### 3.1 Prąd nasycenia I_0 / J_0

Ogólna postać aktywacyjna (Scheer & Schock 2011, rozdz. 2):

$$
J_0 = J_{00}\exp\!\left(-\frac{E_a}{AkT}\right)
$$

| Mechanizm                         | E_a          | Prefactor J_00        |
| --------------------------------- | ------------ | --------------------- |
| Dyfuzja w QNR (homozłącze)        | E_g          | q·n_i²·D/L·N          |
| Rekombinacja SCR                  | E_g          | q·n_i·w/(2τ)          |
| Rekombinacja interfejsowa bez FLP | E_g,IF       | f(S_p0, N_v)          |
| Rekombinacja interfejsowa z FLP   | β_p < E_g,IF | f(S_p0, FLP position) |

**Korelacja J_00–E_a:** silna ujemna korelacja; J_00 nie ma bezpośredniej interpretacji bez E_a.

### 3.2 Współczynnik idealności A

**A ≈ 1:** dominuje rekombinacja dyfuzyjna (QNR) lub thermionic emission.
**A ≈ 2:** SRH w SCR (klasyczna interpretacja Schockley-Read-Hall dla jednego poziomu w środku przerwy).
**1 < A < 2:** mieszany udział dyfuzji i rekombinacji SCR, lub rekombinacja w warstwie akumulacyjnej.
**A > 2:** tunelowanie wzmocniona rekombinacja, rekombinacja przez exponential band-tail states, rekombinacja przy grain-boundaries z barierą, silna niejednorodność bariery, Fermi-level pinning.

**OSTRZEŻENIE:** żadna z powyższych reguł nie jest rozstrzygająca bez analizy temperaturowej.
Pozorny A może być zniekształcony przez:
- duże R_s (zawyża A)
- małe R_sh (zaniża nachylenie w zakresie logarytmicznym, fałszuje A)
- bariery kontaktowe (roll-over tworzy fałszywy A przy niskich V)
- FLP (zmienia E_a, a przez to interpretację A·ln J_0)

### 3.3 Rezystancja szeregowa R_s

Źródła: opory kontaktów metalicznych, opór warstwy okienkowej, opór bazy.
**Zależność T:** zazwyczaj R_s(T) maleje wraz z T (wzrost ruchliwości nośników).
**Metoda diagnostyczna:** Cheung's method (dV/d(ln I) vs I → nachylenie = R_s).

### 3.4 Rezystancja bocznikująca R_sh

Źródła: pinholes, grain-boundaries prostopadłe, mikrozwarcia.
**Zależność T:** R_sh często rośnie z T (termiczna aktywacja upływu).
**Diagnostyka:** nachylenie charakterystyki w zakresie V ≈ 0 (metoda 4-punktowa).

### 3.5 Energia charakterystyczna E_00

$$
E_{00} = \frac{\hbar}{2}\sqrt{\frac{N}{m^* \varepsilon_s}}
$$

gdzie N – stężenie donorów/akceptorów, m* – efektywna masa, ε_s – przenikalność.
**Typowe wartości:** E_00 = 10–100 meV dla N = 10¹⁵–10¹⁸ cm⁻³.
**Diagnostyka:** z nachylenia 1/A vs T (patrz §4).

### 3.6 Parametry tunelowania T*

Dla TE-SCR (Rau 1999):
$$
T^* = T\sqrt{1 + \left(\frac{E_{00}}{kT}\right)^2\frac{1}{3}}
$$
Efektywna temperatura zastępuje T w argumencie rekombinacji SCR przy silnym tunelowaniu.

---

## 4. Model i metodologia Uwego Raua

### 4.1 Źródło i kontekst

Rau U. (1999) „Tunneling-enhanced recombination in Cu(In,Ga)Se2 heterojunction solar cells."
*Applied Physics Letters* **74**(1), 111–113.

Model opisuje rekombinację przez stany głębokie w interfejsie lub SCR wzmocnioną tunelowaniem nośników. Był rozwinięty dla CIGS i CdTe, ale ma ogólniejsze zastosowanie w każdym heterostrukturalnym urządzeniu z dużą gęstością stanów głębokich.

### 4.2 Podstawowe równania modelu TE-IR

Prąd rekombinacji interfejsowej wzmocnionej tunelowaniem:

$$
J_{r}^{TE-IR} = J_{00}\exp\!\left(-\frac{\Phi_f^b}{A'kT}\right)\exp\!\left(\frac{qV}{A'kT}\right)
$$

Efektywny współczynnik idealności:

$$
A'(T) = \frac{A_0}{\alpha}\cdot\frac{E_{00}}{kT}\coth\!\left(\frac{E_{00}}{kT}\right)
$$

gdzie α jest współczynnikiem korekcyjnym geometrii bariery (α ≤ 1).

Limity graniczne:
- T → ∞: A'(T) → A_0/α (stały)
- T → 0: A'(T) → (A_0/α)·E_00/(kT) → ∞, tj. A'T → const = A_0·E_00/α

### 4.3 Podstawowe równania modelu TE-SCR

$$
J_{r}^{TE-SCR} = J_{00}\exp\!\left(-\frac{E_a}{AkT}\right)\exp\!\left(\frac{qV}{AkT}\right)
$$

$$
\frac{1}{A(T)} = \frac{1}{2}\left[1 - \frac{E_{00}^2}{3(kT)^2} + \frac{kT}{E^*}\right]
$$

Limity graniczne:
- T → ∞ i E_00 → 0: 1/A → 1/2, tj. A → 2 (czysta SCR-rekombinacja)
- T → 0: A może rosnąć powyżej 2 (tunelowanie dominuje)

### 4.4 Zależności temperaturowe – diagnostyczne wykresy

#### Wykres A(T)

Oś X: T [K], Oś Y: A(T) [—]
Nachylenie: wzrost A z malejącą T → sygnatura tunelowania.
Stały A(T) → czysta termiczna rekombinacja.

#### Wykres 1/A(T)

Oś X: T² [K²] lub T [K], Oś Y: 1/A(T) [—]
Dla TE-SCR: zależność 1/A(T) od T jest nielinearną funkcją; wyraźne odchylenie od linii prostej jest sygnaturą tunelowania.

#### Wykres A(T)·T

Oś X: T [K], Oś Y: A(T)·T [K]
Dla TE-IR przy niskich T: A'·T → const = A_0·E_00/(α·k)
Nachylenie i przecięcie tego wykresu dają E_00 i α.

#### Wykres ln J_0 vs 1/(AkT)

To jest poprawna wersja wykresu Arrheniusa dla modelu z A(T).

Postać liniowa: ln J_0 = ln J_00 − E_a/(AkT)

**Oś X:** 1/(A(T)·kT) [eV⁻¹], **Oś Y:** ln(J_0) [—]
**Nachylenie:** −E_a [eV]
**Przecięcie:** ln J_00

**PUŁAPKA:** Wykres ln J_0 vs 1/(kT) (bez A) jest poprawny TYLKO gdy A jest stałe. Jeśli A zależy od T (model TE-IR lub TE-SCR), tylko wykres ln J_0 vs 1/(A(T)kT) daje prostoliniową zależność z nachyleniem równym E_a.

### 4.5 Kombinacja A·ln J_0

Dla stałego A: A·ln J_0 = A·ln J_00 − E_a/(kT)

Wykres A·ln J_0 vs 1/(kT) ma nachylenie −E_a i daje energię aktywacji bez konieczności analizy J_00.
Uwaga: przy zmiennym A(T) ten wykres jest nieliniowy.

### 4.6 Fermi-level pinning (FLP)

Gdy na interfejsie pinning FLP ustala quasi-poziom Fermiego niezależnie od domieszek okna:
$$
E_a^{FLP} = \beta_p = E_g^{IF} - E_{pin}
$$

Energię pinningu β_p można wyznaczyć z wykresu V_OC(T) → ekstrapolacja do T = 0 daje qφ_b = E_a.
FLP prowadzi do E_a < E_g,IF, co jest kluczową diagnostyką mechanizmu rekombinacji.

### 4.7 Limity metody Raua

- Zakłada jednolitą barierę; niejednorodność bariery daje inne A(T).
- Zakłada jeden dominujący kanał rekombinacji; przy konkurencji TE-IR i TE-SCR interpretacja jest niejednoznaczna.
- Parametry E_00 i E* są silnie skorelowane; wymagana analiza profilu likelihood.
- Nie stosuje się bezpośrednio do urządzeń z istotnym SCLC lub wieloma barierami.

---

## 5. Wykresy podstawowe

### 5.1 Charakterystyki I(V)

| Wykres                 | Oś X  | Oś Y                    | Skala Y    | Co odczytać                        |
| ---------------------- | ----- | ----------------------- | ---------- | ---------------------------------- |
| Surowa charakterystyka | V [V] | I [A] lub J [A/cm²]     | liniowa    | I_SC, V_OC, FF                     |
| Semi-log               | V [V] | log\|I\| lub log\|J\|   | log₁₀      | A, I_0 z nachylenia                |
| Signed-log             | V [V] | slog(I)                 | signed-log | przejście przez zero, reverse bias |
| Z dopasowaniem         | V [V] | I [A]                   | liniowa    | jakość dopasowania wizualnie       |
| Residua bezwzględne    | V [V] | I_meas − I_fit          | liniowa    | systematyczne odchylenia           |
| Residua względne       | V [V] | (I_meas − I_fit)/I_meas | liniowa    | heteroscedastyczność               |
| Residua standaryzowane | V [V] | (I_meas − I_fit)/σ      | liniowa    | outliers (                         | r | >3) |

**Rodziny charakterystyk:** wszystkie temperatury na jednym wykresie z kodowaniem kolorystycznym T.

### 5.2 Parametry w funkcji temperatury

| Wykres    | Oś X  | Oś Y         | Co odczytać                                |
| --------- | ----- | ------------ | ------------------------------------------ |
| I_0(T)    | T [K] | I_0 [A]      | trend ogólny                               |
| ln I_0(T) | T [K] | ln I_0       | czy wykładnicza zależność                  |
| A(T)      | T [K] | A [—]        | stały vs rosnący → diagnostyka tunelowania |
| 1/A(T)    | T [K] | 1/A          | nachylenie dla TE-SCR                      |
| A·T(T)    | T [K] | A·T [K]      | asymptota dla TE-IR                        |
| R_s(T)    | T [K] | R_s [Ω·cm²]  | zależność transportu neutralnego           |
| R_sh(T)   | T [K] | R_sh [Ω·cm²] | aktywacja upływu                           |

### 5.3 Wykresy Arrheniusa

| Wykres                | Oś X            | Oś Y       | Nachylenie           | Przecięcie  |
| --------------------- | --------------- | ---------- | -------------------- | ----------- |
| Standardowy Arrhenius | 1/T [K⁻¹]       | ln I_0     | −E_a/k               | ln I_00     |
| Arrhenius ×1000       | 1000/T [K⁻¹]    | ln I_0     | −E_a [eV]×(k/1000)⁻¹ | ln I_00     |
| Zmodyfikowany (TFE)   | 1/T [K⁻¹]       | ln(I_0/T²) | −qφ_B/k              | ln(A**·A_J) |
| Poprawiony Arrhenius  | 1/(A·kT) [eV⁻¹] | ln J_0     | −E_a [eV]            | ln J_00     |
| A·ln J_0              | 1/(kT) [eV⁻¹]   | A·ln J_0   | −E_a [eV]            | A·ln J_00   |

**Relacje slope↔energia (patrz §6 dla szczegółów):**

Dla wykresu ln I_0 vs 1/T:
$$
E_a = -k \cdot \text{slope}
$$
gdzie slope wyrażone w K, E_a w eV: E_a [eV] = −(k_B [eV/K]) × slope [K].

Dla wykresu ln I_0 vs 1000/T:
$$
E_a [\text{eV}] = -k_B[\text{eV/K}] \times 1000 \times \text{slope}
= -8.617 \times 10^{-5} \times \text{slope}
$$
gdzie slope ma jednostkę [K] (tj. 1000/T jest bezwymiarowe).

### 5.4 Wykresy diagnostyki różniczkowej

#### Funkcja Cheunga (dV/d(ln I))

$$
\frac{dV}{d\ln I} = IR_s + \frac{AkT}{q}
$$

Wykres: dV/d(ln I) vs I.
Nachylenie = R_s [Ω]
Przecięcie (I=0) = AkT/q [V] → A z przecięcia.

#### Funkcja Norde'a

$$
F(V) = \frac{V}{\gamma} - \frac{kT}{q}\ln\!\left(\frac{I(V)}{A_J}\right)
$$

gdzie γ > A to całkowita liczba > A. Minimum F daje φ_B dla diod Schottky'ego.
Przydatna gdy standardowe metody zawodzą przy dużych prądach.

#### Differential resistance

$$
R_{diff}(V) = \frac{dV}{dI}
$$

Plateau R_diff = R_s przy dużych V (obszar dominacji R_s).
Rosn. R_diff przy V → 0 → R_sh diagnostyką.

#### Logarytmiczna pochodna prądu

$$
\frac{d\ln I}{dV} = \frac{q}{AkT} \quad \text{(zakres wykładniczy)}
$$

Stała pochodna = dobra diagnostyka zakresu wykładniczego i wyznaczenia A.

---

## 6. Konstrukcja osi do bezpośredniego odczytu energii

### 6.1 Konwencja 1/T

Wykres: ln J_0 (oś Y) vs 1/T [K⁻¹] (oś X).

Nachylenie = slope = d(ln J_0)/d(1/T) [K]

$$
E_a = -k_B \cdot \text{slope} = -8.617\times10^{-5}\ \text{eV/K} \times \text{slope [K]}
$$

Przy E_a = 1.0 eV: slope = −1.0/(8.617×10⁻⁵) = −11604 K.

**Problem praktyczny:** slope rzędu 10⁴ K jest trudna do odczytania na wykresie.

### 6.2 Konwencja 1000/T

Wykres: ln J_0 vs 1000/T [K⁻¹·10³].

slope = d(ln J_0)/d(1000/T) [bezwymiarowy? Nie – wymiarowy: K/1000]

$$
E_a [\text{eV}] = -k_B \cdot 10^3 \cdot \text{slope} = -0.08617 \cdot \text{slope}
$$

Przy E_a = 1.0 eV: slope = −1.0/0.08617 = −11.6.

**Zaleta:** slope ~10 jest wygodna do odczytu. **Standard w literaturze PV.**

Weryfikacja znaku: wzrost ln J_0 z rosnącą T → slope > 0 gdy oś X = 1000/T → uwaga na
konwencję. Przy typowej interpretacji:

$$
E_a = -\text{slope} \times 0.08617\ \text{eV}
$$

jeśli slope zdefiniowane jako dy/dx i J_0 maleje z malejącą T (rosnącym 1/T): slope < 0, E_a > 0. ✓

### 6.3 Konwencja q/(kT)

Oś X: q/(kT) = 1/V_T [V⁻¹] lub eV⁻¹.

slope = d(ln J_0)/d(q/kT) [V] lub [eV]

$$
E_a = -\text{slope}\ [\text{eV}]
$$

To jest konwencja, dla której E_a = −slope **bezpośrednio w eV bez przeliczania**.

Przeliczenie osi: q/(kT) = 11604/T [eV⁻¹] przy T w K.
1/(kT) = q/(kT)/q → taka sama skala po przeskalowaniu przez q = 1 (w jednostkach eV).

**Zalecenie JFM:** oś X = q/(kT) [eV⁻¹] jest preferowana gdy celem jest bezpośredni odczyt E_a w eV z nachylenia.

### 6.4 Konwencja 1/(kT)

Oś X: 1/(kT) [eV⁻¹] (numerycznie identyczna z q/(kT) gdy energia mierzona w eV).

slope = d(ln J_0)/d(1/kT) [eV]
$$
E_a = -\text{slope}\ [\text{eV}]
$$

Wartości osi 1/(kT): przy 300 K = 38.7 eV⁻¹; przy 100 K = 116 eV⁻¹.

### 6.5 Tabela przeliczeniowa

| Oś X   | Jednostka | slope [jednostka] | E_a [eV] z slope   | slope przy E_a=1 eV |
| ------ | --------- | ----------------- | ------------------ | ------------------- |
| 1/T    | K⁻¹       | K                 | −k_B × slope = E_a | −11604 K            |
| 1000/T | —         | —                 | −0.08617 × slope   | −11.60              |
| q/(kT) | eV⁻¹      | eV                | −slope             | −1.00               |
| 1/(kT) | eV⁻¹      | eV                | −slope             | −1.00               |

**Sprawdzenie warunku E_a = −slope:**
Oś X = 1/(kT) [eV⁻¹]: tak, E_a = −slope bezpośrednio w eV. ✓

### 6.6 Energetyczne skalowanie osi Y

Dla wykresu A·ln J_0 vs 1/(kT):
slope = d(A·ln J_0)/d(1/kT) [eV]
E_a = −slope [eV] ← bezpośredni odczyt.

Dla wykresu ln(J_0/T²) vs 1/(kT) (TE):
slope = −qφ_B [eV] → φ_B = −slope.

---

## 7. Dobór zakresu dopasowania

### 7.1 Regiony charakterystyki I(V)

| Region             | Napięcie           | Dominujący mechanizm           | Zalecenia         |
| ------------------ | ------------------ | ------------------------------ | ----------------- |
| Reverse bias       | V < −3kT/q         | Generacja, upływ               | R_sh z nachylenia |
| Okolice V=0        |                    | V                              | < 3kT/q           | Mixed R_sh, generacja |  |
| Obszar I–II        | 3kT/q < V < V_knee | Rekombinacja, dyfuzja          | A, I_0 z semi-log |
| Obszar przejściowy | V_knee             | Przejście rekombinacja→dyfuzja | Trudny; ostrożnie |
| Obszar Rs          | V > V_knee         | R_s dominuje                   | R_s z Cheunga     |
| Roll-over          | V bliskie V_OC     | Bariera back-contact           | Osobny model BTD  |

### 7.2 Kryteria wyboru zakresu

- **Automatyczny wybór:** identyfikacja zakresu liniowego w semi-log → użycie tego zakresu dla A i I_0.
- **Odrzucenie dużych V:** jeśli dV/d(ln I) przestaje być stałe → wejście w obszar R_s.
- **Odrzucenie małych V:** jeśli log|I| przestaje być liniowe → wpływ R_sh lub generacji.
- **Stabilność:** dopasowanie dla zakresu V ± 10% i porównanie wyników → jeśli A zmienia się > 5%, zakres jest niestabilny.
- **Kink/S-kink:** gdy charakterystyka wykazuje punkt przegięcia → model BTD zamiast SD.

### 7.3 Wpływ zakresu na parametry

Dopasowanie zbyt szerokiego zakresu:
- R_s „wchłania" część krzywizny → zawyżony R_s
- I_0 fałszywy (kompromis między regionami)

Dopasowanie zbyt wąskiego zakresu:
- Duża niepewność statystyczna
- Niestabilność numeryczna przy małej liczbie punktów

**Zalecenie JFM:** domyślnie używać zakresu wykładniczego (semi-log liniowy), raportować wyniki dla ±20% zmiany granicy i informować o stabilności.

---

## 8. Dopasowanie niezależne i analiza rodzin temperatur

### 8.1 Strategie dopasowania

**Strategia A – Niezależne dopasowanie każdej T:**
- Każda temperatura → osobny zestaw parametrów {I_0(T), A(T), R_s(T), R_sh(T)}
- Zalety: brak założeń o zależności temperaturowej, pełna swoboda
- Ryzyka: wyniki mogą nie być fizycznie spójne; korelacje parametrów w każdej T niezależne

**Strategia B – Ciepłe startowanie:**
- Wynik T_i jako punkt startowy dla T_{i+1}
- Zalety: szybsza zbieżność, utrzymanie ciągłości
- Ryzyka: propagacja błędów; lokalne minima

**Strategia C – Simultaneous fitting z narzuconymi zależnościami T:**
- Np. narzucenie I_0(T) = J_00·exp(−E_a/AkT), A = const
- Zalety: mniej parametrów efektywnych, fizycznie spójne
- Ryzyka: fałszywe dopasowanie gdy model T-zależności jest niepoprawny

**Strategia D – Global fitting:**
- Minimalizacja sumy chi^2 po wszystkich T jednocześnie
- Wymagana specyfikacja jakie parametry są wspólne (R_s, E_a) a jakie T-zależne
- Najwymagająca obliczeniowo i numerycznie; ryzyko overfitting

**Podstawowy workflow JFM:** Strategia A jako domyślna → post-processing wyników jako funkcji T → wizualizacja A(T), I_0(T), itp. → opcjonalnie Strategia C lub D jako zaawansowane.

---

## 9. Funkcja celu i model błędu

### 9.1 Podstawowe funkcje celu

**Zwykłe chi-kwadrat (unweighted least squares):**
$$
\chi^2 = \sum_i (I_i^{meas} - I_i^{fit})^2
$$
Zakłada: jednorodny szum, σ_i = const. Poprawne gdy szum prądu jest addytywny i jednorodny.

**Ważone chi-kwadrat:**
$$
\chi^2_w = \sum_i \frac{(I_i^{meas} - I_i^{fit})^2}{\sigma_i^2}
$$
Wymagane gdy szum zmienia się z prądem (heteroscedastyczność).

**Residua logarytmiczne:**
$$
\chi^2_{log} = \sum_i \left(\ln|I_i^{meas}| - \ln|I_i^{fit}|\right)^2
$$
Odpowiadają względnym błędom; nadają nadmierną wagę małym prądom.

**Residua względne:**
$$
\chi^2_{rel} = \sum_i \left(\frac{I_i^{meas} - I_i^{fit}}{I_i^{meas}}\right)^2
$$
Problem przy I_i → 0; wymaga regularyzacji.

### 9.2 Model szumu dla pomiarów I(V)

Realistyczny model: σ_i² = σ_abs² + (σ_rel · I_i)²
gdzie σ_abs – szum bezwzględny (floor), σ_rel – szum proporcjonalny.

**Zalecany minimalny model JFM:**
- σ_i = max(σ_floor, σ_rel · |I_i|)
- σ_floor = szum aparatury; σ_rel = relatywna niepewność skali prądu

### 9.3 Dopasowanie w domenie I(V) vs V(I)

Dopasowanie I(V): naturalne dla modeli implicit; minimalizuje błędy w kierunku I.
Dopasowanie V(I): naturalne gdy V jest kontrolowane z większą precyzją niż I.
**ODR (Orthogonal Distance Regression):** gdy niepewności zarówno I jak i V są istotne.

### 9.4 Zmiana znaku prądu

Problem: punkty przy I ≈ 0 mają duży względny błąd; punkty ujemne mogą dominować w log-fit.
Rozwiązanie: signed-log, lub ważenie z σ_i = σ_abs dla |I| < σ_abs.

---

## 10. Niepewności parametrów dopasowania

### 10.1 Macierz kowariancji

Dla nieliniowego least squares w punkcie minimum:

$$
\mathbf{C} \approx \sigma^2 (\mathbf{J}^T\mathbf{W}\mathbf{J})^{-1}
$$

gdzie J – Jacobian modelu względem parametrów, W – macierz wag.
σ_i wyznaczone z modelu szumu (patrz §9.2).

**Ograniczenia:** C jest lokalną aproksymacją; zawodzi przy asymetrycznych rozkładach i parametrach na granicach.

### 10.2 Profilowanie chi^2

Dla parametru θ_k: fijuj θ_k = θ_k^0 + δ, minimalizuj po pozostałych parametrach.
Profil Δchi^2(θ_k) = chi^2(θ_k) − chi^2_min.
Przedział ufności: Δchi^2 ≤ 1 (1σ), ≤ 3.84 (95% dla 1 par.).
Tw. Wilksa: stosowalne asymptotycznie dla dużych N.

### 10.3 Bootstrap

Parametric bootstrap: generuj syntetyczne zbiory danych z modelu szumu → dopasuj → rozkład parametrów.
Non-parametric bootstrap: losowanie z zastępowaniem punktów pomiarowych.
**Zaleta:** uwzględnia asymetrię, korelacje, parametry na granicach.
**Koszt:** wymaga N_boot × 1 pełnych dopasowań (typowo N_boot = 1000–5000).

### 10.4 Typy niepewności

| Typ                 | Definicja                                     | Kiedy stosować                   |
| ------------------- | --------------------------------------------- | -------------------------------- |
| Standard error      | σ/√N (lub z macierzy C)                       | Powtarzalne pomiary              |
| Confidence interval | przedział zawierający parametr z prawdop. 1−α | Standard w publikacjach          |
| Credible interval   | Bayesowski odpowiednik (posterior)            | Gdy dostępna informacja a priori |
| Prediction interval | dla przyszłego pojedynczego pomiaru           | Szerszy niż CI                   |

### 10.5 Pułapki

- **Silne korelacje A–I_0:** CI z macierzy C mylące; zawsze sprawdzić korelację > |0.95|.
- **Parametry na granicach:** asymptotyczne rozwiązanie macierzy C zawodzi; użyć profilowania.
- **Multimodalność:** kilka lokalnych minimów → porównać kilka punktów startowych.
- **Nieudane dopasowania:** raportować jako NaN z flagą błędu, nie jako zero.

---

## 11. Propagacja niepewności

### 11.1 Ogólna formuła

Dla funkcji f(x₁,…,x_n) z wektorem parametrów **x** i macierzą kowariancji C:

$$
\sigma_f^2 = \nabla f^T \mathbf{C} \nabla f = \sum_{i,j} \frac{\partial f}{\partial x_i}\frac{\partial f}{\partial x_j} C_{ij}
$$

**Nie zakładać niezależności A i I_0** – ich kowariancja C_{A,I_0} jest zazwyczaj silnie ujemna.

### 11.2 Propagacja dla kluczowych wielkości

#### ln I_0

$$
\sigma_{\ln I_0} = \frac{\sigma_{I_0}}{I_0}
$$
(prosta propagacja logarytmu; ignoruje korelację z innymi parametrami jeśli σ_{I_0} pochodzi z C).

#### A·ln I_0

$$
\sigma_{A\ln I_0}^2 = (\ln I_0)^2 \sigma_A^2 + A^2 \sigma_{\ln I_0}^2 + 2A\ln I_0 \cdot C_{A,\ln I_0}
$$

**Krytyczne:** człon z C_{A,ln I_0} jest często dominujący i może zarówno zwiększyć jak i zmniejszyć niepewność.

#### 1/A

$$
\sigma_{1/A} = \frac{\sigma_A}{A^2}
$$

#### A·T (przy A zależnym od T i T jako niepewnej wielkości)

$$
\sigma_{AT}^2 = T^2\sigma_A^2 + A^2\sigma_T^2 + 2AT\cdot C_{A,T}
$$

#### Energia aktywacji z nachylenia Arrheniusa

Dla prostej ln J_0 = a + b · (1/kT) dopasowanej do N punktów:

$$
\sigma_{E_a} = \sigma_b = \sigma_{slope}
$$

przy ważonej regresji liniowej:
$$
\sigma_b^2 = \frac{\sum_i w_i}{\Delta}, \quad \Delta = \left(\sum_i w_i\right)\left(\sum_i w_i x_i^2\right) - \left(\sum_i w_i x_i\right)^2
$$

gdzie x_i = 1/(k_B T_i), w_i = 1/σ_{y_i}^2.

### 11.3 Porównanie metod propagacji

| Metoda                 | Zalety                               | Wady                                                  |
| ---------------------- | ------------------------------------ | ----------------------------------------------------- |
| Analityczna (gradient) | Szybka, przejrzysta                  | Wymaga wyprowadzenia pochodnych; liniowa aproksymacja |
| Numeryczna (Jacobian)  | Universalna; łatwa implementacja     | Zaokrąglenia numeryczne przy małych h                 |
| Monte Carlo z próbek   | Uwzględnia nieliniowości i asymetrię | Kosztowna; wymaga modelu szumu                        |

**Zalecenie JFM:** analityczna dla prostych funkcji (ln J_0, 1/A), Monte Carlo dla złożonych (E_a z nachylenia przy niestandardowych modelach szumu).

---

## 12. Niepewność dopasowań Arrheniusa

### 12.1 Źródła niepewności

1. **Niepewności y_i = ln J_0(T_i):** wynikają z propagacji σ_{J_0} i σ_A (przy A·ln J_0)
2. **Niepewności x_i = 1/(kT_i):** z niepewności termometru σ_T → σ_{x_i} = σ_T/(kT_i²)
3. **Korelacje między punktami:** jeśli wszystkie T mają ten sam błąd kalibracji termometru (systematyczny)

### 12.2 Weighted linear regression

Dla y_i z niepewnościami σ_{y_i}:
$$
\min \sum_i \frac{(y_i - a - bx_i)^2}{\sigma_{y_i}^2}
$$
Rozwiązanie analityczne: patrz wzory w §11.2 z w_i = 1/σ_{y_i}^2.

Gdy niepewności x_i są istotne (σ_{x_i}/Δx_i > 0.1): stosować ODR (York 1966, *Canadian Journal of Physics*).

### 12.3 Confidence band vs prediction band

Confidence band dla prostej regresji (przy n punktach):
$$
y_{fit}(x) \pm t_{\alpha/2, n-2} \cdot s \sqrt{\frac{1}{n} + \frac{(x-\bar{x})^2}{\sum(x_i-\bar{x})^2}}
$$

Prediction band (dla nowego pomiaru) jest szerszy o √(1 + 1/n).

### 12.4 Kilka zakresów liniowych

Gdy Arrhenius wykazuje dwie linie proste (zmiana nachylenia):
- Punkt przejścia T* wyznaczany przez minimalizację łącznego chi^2 ze zmianą modelu
- Każda linia prosta ma swoje E_a i J_00 z osobnymi niepewnościami
- Test F lub likelihood ratio test do weryfikacji czy podział statystycznie uzasadniony

### 12.5 Propagacja niepewności punktów z Monte Carlo

Gdy σ_{J_0(T_i)} pochodzi z bootstrap lub profilowania (asymetryczne):
- Użyć mediany jako wartości centralnej
- Użyć percentyli [16%, 84%] jako ±1σ odpowiedniki
- Asymetryczne błędy w regresji liniowej: metoda BCES lub ODR z macierzą kowariancji

---

## 13. Identyfikowalność parametrów

### 13.1 Typowe silne korelacje

| Para parametrów        | Korelacja      | Przyczyna                                  |
| ---------------------- | -------------- | ------------------------------------------ |
| A i I_0                | −0.95 do −0.99 | Degeneracja ln I_0 = (E_a − A·kT·x)/const  |
| R_s i A                | pozytywna      | R_s zawyża pozorny A                       |
| R_sh i I_0 (leakage)   | pozytywna      | Upływ imituje I_0                          |
| I_SC i offset prądu    | silna          | Nierozróżnialne bez zewnętrznej informacji |
| I_01 i I_02 (model 2D) | silna ujemna   | Konkurencja obu diod                       |
| φ_B i prefactor (TE)   | silna ujemna   | Degeneracja jak A–I_0                      |

### 13.2 Diagnostyki identyfikowalności

- **Macierz korelacji:** wydrukować C/σ; korelacje > |0.9| sygnalizują problem.
- **Profile likelihood:** jeśli profil Δchi^2 jest płaski lub ma dwa minima → brak identyfikowalności.
- **Condition number:** κ(J^T J) > 10⁶ → numeryczna niestabilność wyznaczania parametrów.
- **Singular values Jacobianu:** małe wartości σ_i Jacobianu wskazują kierunki w przestrzeni parametrów niewidoczne w danych.
- **Stabilność vs punkt startowy:** wielokrotne uruchomienie z różnymi startami; rozrzut wyników > 2σ → problem z lokalnymi minimami.

### 13.3 Różnica między dobrym dopasowaniem a poprawnym modelem

Dobry R² / małe chi^2 NIE gwarantuje:
- jednoznaczności parametrów (degeneracja A–I_0)
- poprawności modelu fizycznego
- sensowności parametrów (np. A = 8 może dawać dobre dopasowanie ale jest niefizyczne)

Zawsze raportować:
- residua (szukać trendów, nie tylko rozrzutu)
- korelacje parametrów
- fizyczność parametrów (A < 0, R_s < 0, itp.)

---

## 14. Automatyczny wybór modelu

### 14.1 Kryteria informacyjne

| Kryterium     | Wzór                  | Penalizacja     | Zastosowanie                           |
| ------------- | --------------------- | --------------- | -------------------------------------- |
| chi^2         | Σ residua²            | brak            | Porównanie modeli tej samej złożoności |
| Reduced chi^2 | chi^2/dof             | pośrednia       | dof = N_points − N_params              |
| AIC           | 2k − 2ln L            | lekka           | Asymptotycznie; N duże                 |
| AICc          | AIC + 2k(k+1)/(N−k−1) | lekka, korekcja | N małe (N < 40k)                       |
| BIC           | k·ln(N) − 2ln L       | mocna           | Konsystentny wybór modelu              |
| LRT           | Δchi^2 ~ chi²_p       | —               | Modele zagnieżdżone                    |

k = liczba parametrów, N = liczba punktów, L = likelihood.

### 14.2 Kryteria fizyczności

Parametr odrzucamy gdy:
- A < 0.5 lub A > 6 (dla modeli bez tunelowania)
- R_s < 0
- R_sh < 0
- I_0 < 0
- I_ph < 0 (dla ciemnych pomiarów)

### 14.3 Workflow rankingowania modeli

1. Dopasuj wszystkie modele ze zbioru kandydatów (od prostszych do bardziej złożonych)
2. Oblicz AICc dla każdego
3. Sprawdź fizyczność parametrów → odrzuć niefizyczne
4. Sprawdź residua → szukaj trendów strukturalnych
5. Raportuj: (a) model o minimalnym AICc z fizycznymi parametrami, (b) różnicę ΔAICc względem modelu o najniższym AICc, (c) modele z ΔAIC < 2 jako "prawie równoważne"
6. Umożliwić użytkownikowi zmianę wyboru z uzasadnieniem

**Automatyczny wybór jest rekomendacją, nie decyzją ostateczną.**

---

## 15. Mechanizmy fizyczne i ich sygnatury

| Mechanizm                 | Forma I(V,T)         | A(T)               | I_0(T)            | E_a            | Inne sygnatury                   | Rozstrzygalność                               |
| ------------------------- | -------------------- | ------------------ | ----------------- | -------------- | -------------------------------- | --------------------------------------------- |
| Dyfuzja QNR               | ∝ exp(qV/kT)         | ≈ 1, stałe         | ∝ exp(−E_g/kT)    | E_g            | dV/d(ln I) stałe                 | Rozstrzygająca gdy A=1 i E_a=E_g              |
| Rekombinacja SCR (SRH)    | ∝ exp(qV/2kT)        | ≈ 2, stałe         | ∝ exp(−E_g/2kT)   | E_g/2          | —                                | Zgodna; niejednoznaczna bez E_a               |
| Rekombinacja interfejsowa | jak SCR              | ≈ 1–2              | ∝ exp(−E_a^IF/kT) | E_g^IF ≤ E_g   | V_OC(T) ekstrapoluje do E_g^IF/q | Niejednoznaczna bez V_OC(T)                   |
| Rekombinacja GB           | jak SCR              | ≈ 2                | —                 | ≈ E_g          | —                                | Niejednoznaczna bez kartografii przestrzennej |
| TE-IR                     | ∝ exp(qV/A'kT)       | A'(T) → ∞ przy T→0 | ∝ exp(−Φ/A'kT)    | zależy od Φ_f  | A'·T → const (niskotemp.)        | Rozstrzygająca gdy A'·T = const               |
| TE-SCR                    | ∝ exp(qV/AkT)        | A > 2, rośnie      | ∝ exp(−E_g/AkT)   | E_g            | 1/A(T) nielinearny               | Rozstrzygająca gdy 1/A(T) odchyla od prostej  |
| Thermionic emission       | A=1, J_0∝T²          | ≈ 1, stałe         | ∝ T²exp(−qφ_B/kT) | qφ_B           | ln(J_0/T²) liniowe vs 1/T        | Rozstrzygająca                                |
| TFE                       | pośredni             | ≈ 1–2 zależy od T  | złożona           | zależy od E_00 | Padovani-Stratton semilog        | Zgodna                                        |
| FE                        | niezależne od T      | ~ const; duże      | niezal. od T      | 0              | —                                | Rozstrzygająca gdy niezal. od T               |
| Bariera back-contact      | roll-over, nasycenie | —                  | —                 | φ_bp           | S-kink, cross-over               | Rozstrzygająca dla roll-over                  |
| FLP                       | A(T) zmodyfikowane   | —                  | E_a < E_g         | β_p < E_g      | V_OC(T)→β_p/q                    | Rozstrzygająca z V_OC(T)                      |
| Upływ (leakage)           | sublinearny          | pozorny A niski    | —                 | —              | R_sh bardzo małe                 | Zgodna                                        |
| R_s limitation            | nasycenie prądu      | pozorny A wysoki   | —                 | —              | dV/d(ln I) liniowy w I           | Rozstrzygająca z Cheungiem                    |
| SCLC                      | I ∝ V^m, m>1         | pozorny A          | —                 | —              | log-log liniowy                  | Rozstrzygająca na log-log                     |
| Band-tail (Urbach)        | A > 2; A zależy od T | rośnie z 1/T       | —                 | —              | EU ~ kT                          | Zgodna                                        |

---

## 16. Preprocessing i jakość danych

### 16.1 Operacje techniczne (nie zmieniają danych fizycznych)

- Konwersja jednostek (mA → A, mA/cm² → A/m²)
- Konwersja I → J przez podanie powierzchni (uwaga: σ_A → σ_J)
- Rejestracja metadanych: temperatura, powierzchnia, orientacja pomiaru (forward/reverse sweep)

### 16.2 Operacje na danych (wymagają rejestracji)

- Korekta offsetu napięcia (drift kontaktów)
- Korekta offsetu prądu (dark current baseline przy oświetleniu)
- Usuwanie duplikatów i redundantnych punktów
- Obsługa brakujących punktów (interpolacja vs usunięcie)
- Uśrednianie powtórzeń pomiaru: raportować zarówno μ jak i σ

### 16.3 Detekcja anomalii

- **Compliance limit:** plateau prądu przy stałej wartości → truncowanie danych
- **Saturacja aparatury:** plateau napięcia → błąd pomiaru
- **Outliers:** standaryzowane residua |r_i| > 3σ → flaga (nie automatyczne usunięcie)
- **Histereza:** porównanie forward i reverse sweep → δI_hyst = I_fwd − I_rev
- **Monotoniczność napięcia:** sprawdzić czy V jest ściśle monotonicze; jeśli nie → błąd pomiaru

### 16.4 Operacje mogące wpłynąć na interpretację

- Filtrowanie (smoothing) → wygładzenie prawdziwych cech fizycznych (np. kink)
- Odrzucenie punktów → zmiana zakresu dopasowania
- Normalizacja przez powierzchnię niejednorodną → błędy systematyczne

---

## 17. Dane oświetlone i ciemne

### 17.1 Superposition principle

Zasada superpozycji (shifting approximation):
$$
J_{light}(V) = J_{dark}(V) - J_{SC}
$$
Warunki poprawności: liniowa rekombinacja, brak voltage-dependent collection, brak barier światłozależnych.

### 17.2 Przyczyny naruszenia superpozycji

| Przyczyna                    | Mechanizm                                             | Sygnatura                               |
| ---------------------------- | ----------------------------------------------------- | --------------------------------------- |
| Voltage-dependent collection | Kolekcja nośników zależy od V przez szerokość SCR     | J_SC(V) niestałe; cross-over dark/light |
| Electronic doping CdS        | Światło zmienia typ CdS → modyfikacja J_02            | Cross-over; J_02(light) ≫ J_02(dark)    |
| Modulated CB spike           | Bariera na styku okno/absorber obniżana przez światło | Pozorny spadek R_s pod oświetleniem     |
| Back contact barrier         | Bariera back-contact obniżana przez światło           | Cross-over dla dużych φ_bp              |
| Traps/metastability          | Pułapki zmieniają stan pod oświetleniem               | Histereza; powolna relaksacja           |

Źródło: Agostinelli et al. (2003) *3rd WCPEC, Osaka*; Scheer & Schock (2011), rozdz. 7.

### 17.3 Modele voltage-dependent photocurrent

Rozszerzony model oświetlonego ogniwa:
$$
J(V) = J_{dark}(V) - J_{ph}(V)
$$

gdzie J_ph(V) = J_SC · f(V) z f(V) opisującym voltage-dependent collection.
Parametryczna postać (empiryczna): f(V) = 1 − (V/V_bi)^n lub bardziej złożone.

### 17.4 V_OC(T) – analiza aktywacyjna

$$
V_{OC}(T) \approx \frac{E_a}{q} - \frac{AkT}{q}\ln\!\left(\frac{J_{00}}{J_{SC}}\right)
$$

Ekstrapolacja do T = 0:
$$
V_{OC}(T=0) = \frac{E_a}{q}
$$

Wartość E_a z ekstrapolacji V_OC(T=0) jest niezależna od A i J_00 → ważna cross-check.
Przy FLP: E_a/q = β_p/q < E_g,IF/q.

### 17.5 J_SC(T)

Dla absorberów ze wzrastającym E_g(T): J_SC maleje z malejącą T.
Odwrotnie dla materiałów z dominującą dyfuzją niskotemperaturową.
J_SC(T) jest użyteczne do wyznaczenia dE_g/dT.

### 17.6 Suns-V_OC

Metoda mierząca V_OC jako funkcję intensywności oświetlenia (I_light):
$$
V_{OC}(I_{light}) = \frac{AkT}{q}\ln\!\left(\frac{I_{light}}{I_0}\right) + C
$$

Nachylenie ΔV_OC/Δ(ln I_light) = AkT/q → ekstrakcja A bez R_s.
Przydatna gdy R_s utrudnia ekstrakcję A z I(V).

---

## 18. Walidacja naukowa modeli

### 18.1 Testy z danymi syntetycznymi

Dla każdego modelu:
1. Generuj syntetyczne I(V) z parametrami p_true = {A=1.5, I_0=10⁻⁹A, R_s=1Ω, R_sh=10³Ω}
2. Dodaj szum Gaussian z σ = σ_floor + σ_rel·|I|
3. Dopasuj model → wyznacz p_fit
4. Sprawdź: |p_fit − p_true| < 3σ_p_fit (odzysk parametrów)
5. Powtórz dla 1000 realizacji szumu → rozkład bias i variance

### 18.2 Testy graniczne

- R_s → 0: wynik powinien zbiegać do modelu SD-Rsh
- R_sh → ∞: wynik powinien zbiegać do modelu SD-Rs
- A → 1: wynik powinien zbiegać do idealnego Shockleya
- T → ∞: A'(T) w modelu TE-IR powinien zbiegać do A_0/α

### 18.3 Porównanie solverów

Dla każdego modelu implicit:
- Lambert W vs Newton-Raphson vs Halley: max|I_LW − I_NR| < 10⁻¹⁰ A
- Test na N = 10⁶ losowych zestawów parametrów i napięć

### 18.4 Benchmarki z literatury

- Porównanie z wynikami ekstrakcji parametrów z opublikowanych danych (np. Osterwald 1989 dla Si, Sites 2010 dla CdTe)
- Weryfikacja E_00 z niezależnych pomiarów C-V lub DLTS

---

## 19. Minimalny i rozszerzony zakres naukowy

### 19.1 Zakres podstawowy (niezbędny)

- Modele: SD-0, SD-Rs, SD-Rsh, SD-RsRsh, SD-Iph
- Wykresy: I(V), log|I|(V), signed-log, residua
- Analiza temperaturowa: I_0(T), A(T), wykres Arrheniusa (1000/T)
- Niepewności: macierz kowariancji, korelacja A–I_0
- Preprocessing: konwersja jednostek, detekcja outlierów
- Eksport: parametry z niepewnościami do CSV/tabeli

### 19.2 Zakres rozszerzony (wysoce przydatny)

- Modele: 2D-RsRsh, TE-IR, TE-SCR, TE, SD-PLsh, BTD
- Wykresy: A(T), 1/A(T), A·T(T), dV/d(ln I), Norde, funkcja Cheunga
- Analiza: V_OC(T), J_SC(T), wyznaczanie E_00
- Niepewności: bootstrap, profilowanie chi^2
- Selekcja modelu: AICc, BIC, kryteria fizyczności
- Superposition check: cross-over dark/light

### 19.3 Zakres badawczy (eksperymentalny)

- Modele: TFE, FE, SCLC, wielobarierowe
- Globalne dopasowanie z narzuconymi zależnościami T
- Bayesian posterior sampling (MCMC)
- ODR (orthogonal distance regression)
- Modele z niejednorodną barierą (Tung 1992 dla Schottky)
- Automatyczna detekcja punktu zmiany mechanizmu w Arrhenius

---

## 20. Otwarte pytania domenowe

Następujące pytania wymagają decyzji fizyka prowadzącego projekt JFM:

1. **Klasy urządzeń:** Które typy złączy są priorytetowe? (CIGS, CdTe, Si, GaN, organiczne, Schottky, memrystory?)
2. **Zakresy pomiarowe:** Typowe zakresy T (100–400 K? 20–450 K?), prądów (pA–A?) i napięć (−5V–+2V?)?
3. **Typ pomiarów:** Tylko ciemne, tylko jasne, czy oba? Czy analiza J_SC(φ)?
4. **Powierzchnia urządzenia:** Czy zawsze dostępna dla konwersji I→J? Jak obsługiwać niejednorodne geometrie?
5. **Priorytety modeli:** Które modele są faktycznie używane w laboratorium? Czy TE-IR/TE-SCR są priorytetem?
6. **Konwencja znaku:** I vs J; znaki dla forward/reverse; definicja V przy złączu.
7. **Wielkości wynikowe:** Które parametry są głównymi wynikami (E_a, A, R_s, czy coś innego)?
8. **Automatyzacja vs. interakcja:** Które interpretacje mają być automatyczne, które pozostawione użytkownikowi?
9. **Dane dodatkowe:** Czy dostępne są pomiary C-V, DLTS, EL, QE do cross-walidacji?
10. **Format danych wejściowych:** CSV, format SPICE, format komercyjnych sourcemeterów?
11. **Wydajność:** Ile charakterystyk na sesję (dziesiątki vs. miliony)?
12. **Platforma:** Desktop, cloud, HPC?

---

## 21. Podsumowanie

### 21.1 Najważniejsze wymagania naukowe

1. JFM musi obsługiwać modele z A(T) zmiennym – nie zakładać A=const w analizie temperaturowej.
2. Wykres Arrheniusa musi mieć wersję z osią 1/(A(T)·kT) dla modeli TE-IR/TE-SCR.
3. Propagacja niepewności musi uwzględniać pełną macierz kowariancji (korelacja A–I_0).
4. Selekcja modelu = rekomendacja z uzasadnieniem, nie decyzja ostateczna.
5. Każdy parametr musi mieć raportowaną niepewność i korelacje z pozostałymi.
6. JFM musi odróżniać dobre dopasowanie krzywej od poprawnej interpretacji fizycznej.
7. Analiza cross-over dark/light jest wymaganą diagnostyką dla ogniw cienkowarstwowych.

### 21.2 Lista rekomendowanych modeli podstawowych

- SD-RsRsh (5P, ciemny)
- SD-Iph (5P, oświetlony)
- 2D-RsRsh (7P, ciemny)
- TE-IR z A'(T)
- TE-SCR z 1/A(T)
- BTD (back-to-back, roll-over)
- SD-PLsh (diagnostyczny)

### 21.3 Lista wymaganych wykresów

- I(V) i log|I|(V) z dopasowaniem i residuami dla każdej T
- Rodziny charakterystyk na jednym wykresie
- A(T), I_0(T), ln I_0(T), R_s(T), R_sh(T)
- Wykres Arrheniusa: ln I_0 vs 1000/T i vs 1/(A·kT)
- A·ln J_0 vs 1/(kT)
- A(T)·T vs T (diagnostyka TE-IR)
- 1/A(T) vs T (diagnostyka TE-SCR)
- dV/d(ln I) vs I (ekstrakcja R_s, Cheung)
- V_OC(T) z ekstrapolacją do T=0
- Cross-over dark/light (dla oświetlonych)

### 21.4 Lista obowiązkowych diagnostyk

- Korelacja A–I_0 (raportować C_{A,I0}/σ_A/σ_{I0})
- Residua standaryzowane (wykrywanie outlierów)
- Physical bounds check (A, R_s, R_sh, I_0 > 0)
- AICc dla porównania modeli
- Condition number Jacobianu
- Stabilność vs zakres dopasowania (sensitivity analysis)

### 21.5 Lista otwartych decyzji

→ Patrz §20.

---

## 22. Bibliografia pogrupowana tematycznie

### Fizyka złączy i modele prądu

- Shockley W. (1949). *Bell System Technical Journal* 28, 435.
- Padovani F.A., Stratton R. (1966). *Solid-State Electronics* 9, 695–707. [TE/TFE/FE]
- Crowell C.R. (1965). *Solid-State Electronics* 8, 395–399. [Stała Richardsona]
- Sze S.M., Ng K.K. (2006). *Physics of Semiconductor Devices*, 3rd ed. Wiley.
- Dimitrijev S. (2012). *Principles of Semiconductor Devices*, 2nd ed. Oxford.

### Heterozłącza i ogniwa cienkowarstwowe

- Scheer R., Schock H.W. (2011). *Chalcogenide Photovoltaics*. Wiley-VCH. [główne źródło]
- Green M.A. (1982). *Solar Cells*. Prentice-Hall.

### Model Raua i tunelowanie

- Rau U. (1999). Tunneling-enhanced recombination in Cu(In,Ga)Se₂ heterojunction solar cells. *Appl. Phys. Lett.* **74**(1), 111–113.
- Bayhan H., Ercelebi C. (2005). Tunnelling enhanced recombination in polycrystalline CdS/CdTe. *Turk. J. Phys.* [CdTe weryfikacja]
- Agostinelli G. et al. (2003). Light dependent current transport mechanisms in chalcogenide solar cells. *3rd WCPEC, Osaka*.

### Modele elektryczne i ekstrakcja parametrów

- Jain A., Kapoor A. (2004). Lambert W funkcja. *Solar Energy Materials and Solar Cells* 81, 269.
- Mott N.F., Gurney R.W. (1948). *Electronic Processes in Ionic Crystals*. [SCLC]
- Cheung S.K., Cheung N.W. (1986). *Appl. Phys. Lett.* 49, 85. [Metoda Cheunga]
- Norde H. (1979). *J. Appl. Phys.* 50, 5052. [Metoda Norde'a]

### Superposition i dark/light

- Klenk R. (2001). *Thin Solid Films* 387, 135. [Analiza charakterystyk heterozłącza]
- Moore J.E. et al. (2014). Correlation of built-in potential and I-V crossover. *IEEE J. Photovoltaics* 4, 1138–1148.

### Statystyka i niepewności

- Press W.H. et al. (2007). *Numerical Recipes*, 3rd ed. Cambridge.
- York D. (1966). *Canadian Journal of Physics* 44, 1079. [Weighted regression with errors in x and y]
- Bevington P.R., Robinson D.K. (2003). *Data Reduction and Error Analysis for the Physical Sciences*.

---

*Dokument przygotowany dla projektu JFM. Data: 2026-06-26. Wersja 2.0 (rozszerzona o TE-IR, TE-SCR, SD-PLsh, charakterystyki dark/light, superposition analysis).*
