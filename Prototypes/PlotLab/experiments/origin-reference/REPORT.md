# Raport org1b — 2026-11-19

Wyrenderowano **26/26 figur, 36 wariantów rozmiaru**: 108 plików PNG/SVG/EMF oraz 36 JSON z getterów. Zapisano `origin-reference.opju` z osobnymi graph pages dla obu rozmiarów oraz `.otpu` dla group/template replay i native colormap T. Indeks z linkami: [README.md](README.md); indeks maszynowy: [manifest.json](manifest.json). Generator jest powtarzalny i zamyka własną instancję przez `op.exit()` w `finally`. Origin został zamknięty.

Obejrzano wszystkie 36 końcowych PNG. Poprawiono clipping, legendy, shared X, reversed axis, tick spacing przy breaks oraz inset z czerwonym regionem. PNG mają 600 dpi i rozmiar zgodny z mm×dpi z tolerancją 2 px. SVG deklarują dokładnie 80×60 lub 160×120 mm. Origin kwantyzuje wewnętrzny page size do printer dots: największy błąd 0,02 mm. EMF wymaga native page-ratio calibration na podstawie nagłówka pierwszego eksportu; bez niej 80 mm dawało około 70 mm. Po ponownym eksporcie w Origin największy błąd wymiaru EMF wynosi 0,21 mm; nagłówków nie modyfikowano.

Nie udało się uzyskać getterów post-layout major/minor tick vectors, rendered tick labels, resolved RGB grup i font size colorbar. Pola mają `null` z powodem; nie zastępowano ich własnym locatorem ani etykietami obliczonymi z danych. Native mapping T uruchomiono początkowo w GUI i utrwalono w dołączonym `temperature-seed.otpu`; dalsza regeneracja jest headless. Overbar nad tekstem pozostaje artefaktem eksportu tej instalacji, również w minimalnym probe — nie należy kopiować go jako reguły PlotLab. Jednostka prądu A nadal jest założeniem źródłowych fixtures.

Reguły do odtworzenia w PlotLab:

- Tick direction jest bitmaską: major in/out = 1/2, minor in/out = 4/8. `showAxes` i `showLabels` są wspólne dla pary osi, ale tick masks i label visibility mogą różnić się stronami.
- Linked top 1000/T wymaga transformacji i odpowiedniej skali child axis; shared X musi synchronizować rzeczywiste granice, nie tylko zapisywać link ID.
- Colormap po user parameter T potrzebuje wspólnych color levels i grupy; samo załadowanie template potrafi przeskalować kolory tylko do pierwszej temperatury. Independent group Edit Mode pozwala zachować osobne line styles.
- Font size zależy od `layer.factor`. Przy zmianie page size trzeba też przeliczyć kotwice tekstu; w tej instalacji eksport respektuje `x1/y1`, podczas gdy same `x/y` mogły dawać poprawny getter i znikający tytuł. Legend substitutions: `@LL`, `@LU`, `@LD1` mają odrębne znaczenie.

Walidacja: Python build do `origin-reference/build/`, wszystkie trzy lokalne unittest przez wspólny build-lock. Wynik końcowego przebiegu: **3/3 PASS**, w tym kontrola kompletności 36 wariantów, PNG DPI/rozmiaru, SVG/EMF physical size, getterów funkcji, line styles i regionu insetu. Log: [build/validation.log](build/validation.log).

Nie budowano C++ produktu ani nie sprawdzano EMF w zewnętrznym viewerze. Bez git i bez zmian ustawień, rejestru lub uprawnień; pliki repo zmieniano wyłącznie w origin-reference. Nie uruchamiano `graphify update .`, ponieważ zapis poza tym katalogiem naruszałby STRICT SCOPE.

