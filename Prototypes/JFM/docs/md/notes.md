[x] 1. Dlaczego `sourcePath` w JFMCharacteristic jest raw-string? Czy nie powinno być Path z Core::Utils?
[x] 2. Dlaczego voltage, current, currentDensity jest std::vector i jest to zapisane bezpośrednio w JFMCharacteristic? Wydaje mi się, że odrazu powinniśmy wprowadź koncent 
Characteristics. Tam powinny się znajdować tego typu rzeczy i trzymać to w jakimś rodzaju `CharacteristicsRegistry` czy czymś podobnym. Wtedy identyfikujemy to jedynie po UUID. Nie mamy wtedy problemu z wektorem wektrów wektorów
[x] 3. Myślałem aby wprowadzić klasę `CharacteristicInfo` gdzie też by się znajdowały informacje o typie sweepu, vStart, vEnd, ilości punktów itd. 
[x] 4. Dlaczego jakiekolwiek `id` jest std::string? 
[x] 5. funkcje w `JFMIORegistration` zdecydowanie powinny być osobnymi funkcjami.
[x] 6. Bardzo mi się nie podoba częstość i fakt potrzeby używania `Application::Get()...` w `JFMIORegistration`
6. Przy jako reakcja na event o niepowodzeniu podczas importu/eksportu dobrze by było zrobić Notification
7. Można pogrupować Joby związane z IO w JFM 
[x] 8. Dlaczego Domain i Storage jest rozdzielone? Czy my podjeliśmy taką świadomą decyzję? 
[x] 9. Czy w `ensureSelectionLocked` nie lepiej by zrobić flatten listy charakterystyk przy takim ułozeniu? W przyszłym ułożeniu (punkty powyżej) może nie będzie potrzeba podwójnej pętli for
[x] 10. Projekt powinien dostarczać swoje własne API do operacji na charakterystykach i później na analizach aby nie polegać na tym, że to jest vector czy array itd. 
[x] 11. Projekt powinien dostarczyć możliwość poproszenia o charakterystykę na podstawie UUID albo jej nazwy aby nie musieć w wielu miejscach pisać tych samych funkcji szukających konkretnego elementu
[x] 12. Bardzo bardzo mi się nie podoba, że nie korzystamy z całego systemu `ScientificRuntime`. Obecny import charakterystyk po przez pythona jest całkowicie do przepisania z wykorzystaniem już istniejącego systemu `ScientificRuntime`

Opcje UI do dodania:
1. wyświetlanie zarejestrowanych skryptów pythonowych do importu (jako element projektu) (context menu (add script/remove script/reload script))
2. dodawanie nowych skryptów do projektu (context menu oraz ogólne menu w aplikacji) 
3. zaznaczanie charakterystyk ctrl/shift/drag, shift +strzałki up/down do zaznaczania
4. ekstrakt charakterystyk do nowych serii
5. okienko importu (wybranie skryptu, jak ma być zbudowana nazwa charakterystyki (np. import scaps), typ delimatera, coś jest separatorem dla części dziesiętnej, co gdzie jest temperatura w nazwie pliku jeśli aplikowalne co jeszcze można tutaj dodać?)
6. multi plot dla całych serii
---
1. kolorowanie charakterystyk (jakiś panel gdzie można ustawić wygląd jednej charakterystyki albo całej grupy)
2. toggle button dla auto-fit oraz slider dla zakresu jaki ma być automatyczny odjazd
3. sortowanie charakterystyk ze względu na temperaturę albo jakieś metadane w postaci numerycznej
4. okienko z podglądem informacji o konkretnech charakterystyce
5. Mam folder `JFM-projects` tam zapisałem dwa projekty. Jeden z charakterystykami scapsa drugi bez nich. Jak próbuję otworzyć projekt to drzewko projektu się poprawnie otwiera ale dane faktyczne nie są importowane.
6. jako element context-menu dla projektu dodajmy: (import series, add series)
7. jako element context-menu dla serii dodajmy: (delete series, add characteristic(s))
8. jako element context-menu dla charakterystyki dodajmy: (delete characteristic(s))
9. jak jest import folderów to dobrze by było aby każdy folder był osobną serią
10. powinno się dać zmieniać miejsce charakterystyki drag-drop w drzewku projektu. Tak samo przenosić je pomiędzy seriami.
11. drag-drop charakterystyki(lub zaznaczonych charakterystyk) z drzewka projektu na plot-area pownien być możliwy
12. otworzenie drugiego plot-area bez żadnych charakterystyk powinno być możliwe (można wyświetlić więcej niż )
-------------

## Codex - odniesienie techniczne i plan naprawy

Data: 2026-07-10

### Status

- Odniesienie dotyczy zmian po rebase z `main` oraz planu `JFM-implementation-plan.md`.
- Priorytet: najpierw runtime/lifecycle/IO/model danych, dopiero później UI i ergonomia.
- Nie nadpisywać istniejących uwag; ta sekcja jest dopisana jako komentarz techniczny.

### Odniesienie do zarzutów

- `sourcePath` jako `std::string` jest słabe. W runtime powinien być `Path`; YAML nadal może serializować go jako string.
- `voltage/current/currentDensity` w `JFMCharacteristic` były akceptowalne jako MVP Fazy 1, ale docelowo powinny być poza metadanymi charakterystyki.
- `CharacteristicInfo` ma sens. Powinno zawierać source, parser, measurement, sweep, temperature, columns, QC, metadata i point count.
- `id` jako goły `std::string` jest zbyt łatwy do pomylenia. Minimalnie potrzebne są typy domenowe/aliasy: `ProjectId`, `SeriesId`, `CharacteristicId`.
- `JFMIORegistration` robi za dużo naraz. Rejestracja może zostać bezpośrednia, ale obsługa import/export/save/load powinna być rozbita na osobne funkcje.
- Częste `Application::Get()` w JFM IO to service locator i realny problem testowalności. IO powinno dostać jawnie `EventBus`, `Notifier`, `Config/pythonRoot` i `Workspace`.
- Rozdzielenie eventów od mechanizmów IO jest właściwe: event mówi co użytkownik chce zrobić, handler/importer mówi jak to wykonać.
- Failure import/export/save/load powinien publikować notification, nie tylko status string/log.
- `Storage/JFMWorkspaceStore` jest mylące nazewniczo. To jest in-memory workspace/state, a nie storage/persistence.
- `ensureSelectionLocked` z podwójną pętlą jest do przeżycia teraz, ale po wprowadzeniu registry/id powinno przejść na `FindCharacteristic(id)`.
- UI nie powinno wiedzieć, że dane są w `vector`. Projekt powinien wystawiać API operacji na seriach i charakterystykach.
- `ScientificRuntime` powinien być jedną warstwą runtime dla Pythona. Linki do ASE/Pymatgen zostały usunięte z tej ścieżki; nie przywracamy domenowych mostów Crystal do Core.
- Problem z otwieraniem projektu jest źródłowy: YAML zapisuje `raw_data: external_only`, ale load nie materializuje danych z `source_path`, więc drzewo wraca bez raw arrays.

### Plan naprawy - architektura

[x] 1. Wprowadzić lekki `RuntimeServices`/`ApplicationRuntimeServices` jako agregat uchwytów: `EventBus`, `JobSystem`, `ProgressTracker`, `CommandRegistry`, `KeymapResolver`, `Notifier`, config paths.
[x] 2. Ograniczyć użycie `Application::Get()` w JFM do zera. Globalny accessor zostawić tylko dla Core/bootstrap/legacy.
[x] 3. Uporządkować Python runtime: jeden oficjalny Core service dla embedded Python/GIL/sys.path/job hooks.
[x] 4. Utrzymać `ScientificPythonRuntime` jako mały wrapper nad `PythonInterpreter`.
[x] 5. Zmienić `RegisterJFMIOHandlers` na jawny kontekst, np. `JFMIORegistrationContext { store, eventBus, notifier, pythonRoot }`.
[x] 6. Rozdzielić `JFMIORegistration` na funkcje: import requested/progress/completed/failed, export, save, load.

### Plan naprawy - model danych

[x] 1. Wydzielić `JFMCharacteristicInfo` oraz `JFMCharacteristicData`.
[x] 2. Zostawić workspace jako jedynego właściciela projektu i registry charakterystyk.
[x] 3. Zmienić serie tak, aby przechowywały `CharacteristicId`, nie pełne obiekty z raw arrays.
[x] 4. Dodać API workspace: `AddSeries`, `AddCharacteristics`, `FindCharacteristic`, `FindCharacteristicByName`, `GetSelectedCharacteristic`, `DeleteCharacteristic`, [x] `MoveCharacteristic`.
[x] 5. Przenieść/zmienić nazwę `Storage/JFMWorkspaceStore` na `Workspace` albo `State`.
[x] 6. Zmienić `sourcePath` w runtime na `Path`; serializer mapuje `Path <-> string`.

### Plan naprawy - save/load i import

1. Zachować zasadę: raw arrays nie są serializowane inline w YAML.
2. Po `ProjectLoadCompleted` uruchamiać background job do materializacji danych z `source_path`.
3. Jeśli plik źródłowy nie istnieje, zostawić charakterystykę w drzewie z warningiem i bez raw arrays.
4. Import folderu: każdy folder jako osobna seria.
5. Import/export/save/load failure publikuje `NotificationRequestedEvent` oraz aktualizuje status workspace.
6. Grupować joby JFM IO przez spójny `source/type`, bez nowej hierarchii klas jeśli nie jest potrzebna.

1. import całych zagnieżdżonych folderów, należy odtworzyć strukturę folderów. 
2. Przy importcie folderów należy sprawdzić czy są tam jakieś pliki które można zaimportować a jeśli się nie uda to pominąć
3. Podczas nieudanego importu danych należy iść dalej i importować to co się da z kolejnych folderów
4.  Dodajmy skrót klawiaturowy dla importu pojedynczego folderu
5.  Dodajmy skrót klawiaturowy dla importu wielu folderów
6.  Dodajmy skrót klawiaturowy dla importu folderu/folderów rekursywnie 
---
1. Należało by zrobić jakiś wrapper na NFD
2. jak jest import rekursywny i folderów to jest dodatkowy poziom chierarchi za dużo w postaci (imported) należy to usunąć
3. W JFMProjectJobs w funkcji `Execute` znajduje się duży blok if-else: zredukuj dwa wcięcia po przez zamianę warunków 
4. W JFMProjectJobs w funkcji `Execute` znajduje się wektor charakterystyk i wyjątków: oszacuj/policz odpowiedni rozmiar tych wektorów i zarezerwuj ich odpowiedni rozmiar. Tam gdzie się da to używaj `emplace_back` zamiast `push_back`
### Plan UI po fundamentach

1. Context menu: projekt `import series/add series`, seria `delete/add characteristics`, charakterystyka `delete`.
2. Rzeczy z importu danych powinny być w File -> Import
3. Dodajmy funkcję ekstrakcji wielu danych do osobnych plików: całe serie, grupy do wybranego folderu
4. Przy imporcie niech będzie domyślny folder (example-data), otwieranie/zapis projektów (JFM-projects) (obydwie ścierzki powinny być konfigurowalne w UI (ikonka po której otwiera się okienko z wybraniem folderu)), powinno to być serializowane w ustawieniach
5. W zakładce File dodajmy zakładkę Last Projects
6. W zakładce File dodamy (otwórz nowy projek) -> otwarcie nowego okna aplikacji. 
7. Multi-select charakterystyk: Ctrl/Shift, później drag selection.
8. Import dialog: parser/script, nazwa charakterystyki, delimiter, decimal separator, temperature extraction. (Tak samo podgląd jak wyglądają pliki też by się przydał i jak by wyglądała obecnie zbudowana nazwa)
9. Script registry w projekcie: add/remove/reload script, widoczne w Project Tree.
10. Raw plot: multi-plot serii, drag-drop z drzewa na plot area, wiele plot areas.
11. Kolorowanie, sortowanie po temperaturze/metadanych, panel szczegółów charakterystyki.

### Test plan

- Unit: workspace API bez zależności UI od layoutu `vector`.
- Unit: YAML round-trip metadanych bez raw arrays.
- Unit: load projektu uruchamia materialization job dla brakujących raw arrays.
- Unit: JFM IO handlers działają bez `Application::Get()`.
- Unit: Python runtime/GIL przez jeden Core service.
- Smoke: import `.dat` i `.iv`, save, reopen, raw arrays odtworzone z `source_path`.
- Manual: menu, skróty, notifications, Project Tree, Raw Plot.

### Założenia

- Nie dodajemy pełnego plugin systemu parserów teraz.
- Nie serializujemy raw arrays inline w YAML.
- Najpierw stabilizujemy runtime/lifecycle/IO, potem UI.
- Ponytail: minimalne typy i funkcje, bez fabryk i interfejsów z jedną implementacją.
