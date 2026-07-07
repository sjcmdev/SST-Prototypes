Na podstawie załączonych plików przygotuj dwa pythonowe skrypty:

1) parser dla plików .dat z pomiarów IV
2) parser dla plików .iv ze SCAPS, szczególnie batch output

Możesz wydzielić wspólny moduł pomocniczy, jeśli to uprości kod, ale główne API ma być jasne i stabilne, bo będzie wołane z C++ przez nanobind.

Cel:
- parsowanie danych IV z różnych źródeł,
- automatyczne wyciąganie temperatury,
- ustandaryzowany wynik wyjściowy,
- możliwość późniejszego łatwego dopisania parsera pod nowy format pliku.

Wymagania wspólne dla obu parserów:

1. Wynik parsera ma być zawsze tym samym typem struktury, najlepiej plain dict + numpy.ndarray, bez pandas jako API wyjściowego.

Proponowany format:

{
    "ok": bool,
    "parser": str,
    "source_path": str,
    "temperature_K": float | None,
    "temperature_source": "filename" | "header" | "batch_parameter" | "missing",
    "curves": [
        {
            "name": str,
            "measurement": "IV",
            "columns": [
                {"name": str, "unit": str | None, "role": str}
            ],
            "data": np.ndarray,
            "sweep": {
                "direction": "increasing" | "decreasing" | "non_monotonic" | "loop",
                "v_start": float | None,
                "v_stop": float | None,
                "n_points": int
            },
            "metadata": dict,
            "warnings": list[str]
        }
    ],
    "warnings": list[str]
}

2. Temperatura:
- ma być automatycznie wykrywana z nazwy pliku, nagłówka albo parametrów batch,
- obsłuż formaty typu: T330, T300K, 290,0K, 290.0K,
- jeśli temperatury nie ma, zwróć None i warning,
- jeśli temperatura w nazwie i nagłówku/batchu się różni, zwróć warning i jasno oznacz źródło.

3. Zrób strukturę ParseOptions, np.:

ParseOptions(
    delimiter=None,
    decimal_separator="auto",
    skip_rows=0,
    has_header="auto",
    column_roles=None,
    column_units=None,
    encoding="auto",
    strict=False
)

4. Parser ma zwracać warningi zamiast cicho zgadywać:
- brak temperatury,
- niejednoznaczna temperatura,
- nieznane kolumny,
- niespójna liczba kolumn,
- sweep niemonotoniczny,
- wykryty decimal comma,
- puste/nienumeryczne linie w środku danych,
- kilka charakterystyk w jednym pliku.

5. Sweep napięciowy:
- wykryj kierunek z kolumny napięcia,
- rozróżnij increasing, decreasing, non_monotonic, loop,
- zwróć v_start, v_stop, n_points.

6. Kolumny:
- jeśli jest header, użyj go do nazw i jednostek,
- jeśli nie ma headera i są 2 kolumny, domyślnie traktuj jako V oraz I/J,
- jeśli nie ma headera i są 3 kolumny, domyślnie V, I, J,
- wszystko ma być nadpisywalne przez ParseOptions.

7. Nazwy krzywych:
Zrób osobną funkcję:

build_curve_name(metadata, pattern, user_overrides=None) -> str

Ma obsługiwać placeholdery, np.:

{sample}_{temperature_K:.0f}K_{sweep_direction}_{batch_step}

Ta funkcja będzie też wołana z C++ przed faktycznym parsowaniem zawartości pliku, więc powinna umieć budować nazwę częściowo na podstawie nazwy pliku i częściowo na podstawie danych od użytkownika.

Parser .dat:

- obsłuż przykładowe pliki załączone:
  - ivd_HZB25_T330_L0.dat
  - 003_R084_97C_Og003_1_REL_T300K_IVD_pm.dat
  - IV_C002_Z015_6_290,0K.dat

- wykrywaj liczbę kolumn,
- wykrywaj header i jednostki, jeśli są,
- obsłuż brak headera,
- obsłuż kropki i przecinki dziesiętne,
- obsłuż delimiter auto / spacja / tab / comma / semicolon,
- obsłuż liczbę linii do pominięcia,
- zaprojektuj to tak, żeby później użytkownik mógł dopisać swój parser w 1–3 funkcjach.

Parser SCAPS .iv:

- przeanalizuj załączony plik batch .iv oraz manual SCAPS,
- plik może zawierać jedną charakterystykę albo wiele charakterystyk z batcha,
- rozdziel batch file na osobne krzywe,
- każda krzywa ma mieć własne metadata, temperaturę, parametry batch i dane numeryczne,
- parser ma zwracać krzywe w pamięci w tym samym standardowym formacie co parser .dat,
- dodatkowo zrób funkcję eksportu:

export_curves(result, output_dir, name_pattern, format="dat")

- jeśli folder wyjściowy nie istnieje, utwórz go,
- nazwa każdej charakterystyki ma być budowana dynamicznie z patternu i metadata,
- użytkownik ma mieć dużą kontrolę nad nazwą pliku, bo to będzie istotne w GUI.

Ograniczenia:
- Python 3.11+,
- numpy OK,
- bez GUI,
- bez matplotlib,
- pandas najwyżej wewnętrznie, ale nie jako output API,
- kod ma być czysty i gotowy do użycia z nanobind,
- funkcje powinny mieć type hints i docstringi,
- ważniejsze są stabilne struktury danych i czytelna architektura niż „sprytne” zgadywanie wszystkiego.

Dodaj krótkie smoke testy na załączonych plikach:
- temperatura z nazwy,
- liczba kolumn,
- kierunek sweepu,
- rozdzielenie SCAPS batch na krzywe,
- zgodność wspólnego schematu wyjściowego.

Nie pisz długiego eseju. Najpierw pokaż proponowaną strukturę plików i API, potem napisz kod.