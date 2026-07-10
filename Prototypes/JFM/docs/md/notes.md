1. Dlaczego `sourcePath` w JFMCharacteristic jest raw-string? Czy nie powinno być Path z Core::Utils?
2. Dlaczego voltage, current, currentDensity jest std::vector i jest to zapisane bezpośrednio w JFMCharacteristic? Wydaje mi się, że odrazu powinniśmy wprowadź koncent 
Characteristics. Tam powinny się znajdować tego typu rzeczy i trzymać to w jakimś rodzaju `CharacteristicsRegistry` czy czymś podobnym. Wtedy identyfikujemy to jedynie po UUID. Nie mamy wtedy problemu z wektorem wektrów wektorów
3. Myślałem aby wprowadzić klasę `CharacteristicInfo` gdzie też by się znajdowały informacje o typie sweepu, vStart, vEnd, ilości punktów itd. 
4. Dlaczego jakiekolwiek `id` jest std::string? 
5. funkcje w `JFMIORegistration` zdecydowanie powinny być osobnymi funkcjami.
6. Bardzo mi się nie podoba częstość i fakt potrzeby używania `Application::Get()...` w `JFMIORegistration`
7. Bardzo mi się podoba to jak są rejestrowane `Handler` w IOLayer. Czym innym jest rejestracja Eventu i jego obsłużenie, że chcemy zaimportować charakterystykę a czym innym rejestracja mechanizmów importu i eksportu. To są dwie różne rzeczy, których nie wolno mieszać
8. Przy jako reakcja na event o niepowodzeniu podczas importu/eksportu dobrze by było zrobić Notification
9. Można pogrupować Joby związane z IO w JFM 
10. Dlaczego Domain i Storage jest rozdzielone? Czy my podjeliśmy taką świadomą decyzję? 
11. Czy w `ensureSelectionLocked` nie lepiej by zrobić flatten listy charakterystyk przy takim ułozeniu? W przyszłym ułożeniu (punkty powyżej) może nie będzie potrzeba podwójnej pętli for
12. Projekt powinien dostarczać swoje własne API do operacji na charakterystykach i później na analizach aby nie polegać na tym, że to jest vector czy array itd. 
13. Projekt powinien dostarczyć możliwość poproszenia o charakterystykę na podstawie UUID albo jej nazwy aby nie musieć w wielu miejscach pisać tych samych funkcji szukających konkretnego elementu
14. Bardzo bardzo mi się nie podoba, że nie korzystamy z całego systemu `ScientificRuntime`. Obecny import charakterystyk po przez pythona jest całkowicie do przepisania z wykorzystaniem już istniejącego systemu `ScientificRuntime`

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
- `ScientificRuntime` jest niespójny: aktywnie budowany jest mały Python runtime, a stary `ScientificPythonRuntime` referencjonuje usunięte ASE/Pymatgen. To trzeba uporządkować przed dalszym importem.
- Problem z otwieraniem projektu jest źródłowy: YAML zapisuje `raw_data: external_only`, ale load nie materializuje danych z `source_path`, więc drzewo wraca bez raw arrays.

### Plan naprawy - architektura

1. Wprowadzić lekki `RuntimeServices`/`ApplicationRuntimeServices` jako agregat uchwytów: `EventBus`, `JobSystem`, `ProgressTracker`, `CommandRegistry`, `KeymapResolver`, `Notifier`, config paths.
2. Ograniczyć użycie `Application::Get()` w JFM do zera. Globalny accessor zostawić tylko dla Core/bootstrap/legacy.
3. Uporządkować Python runtime: jeden oficjalny Core service dla embedded Python/GIL/sys.path/job hooks.
4. Usunąć albo przepisać martwy `ScientificPythonRuntime`, który nadal zależy od ASE/Pymatgen.
5. Zmienić `RegisterJFMIOHandlers` na jawny kontekst, np. `JFMIORegistrationContext { store, eventBus, notifier, pythonRoot }`.
6. Zachować `IOLayer::RegisterHandler<T>()` jako bezpośredni mechanizm rejestracji handlerów; eventy zostają tylko do żądań i wyników pracy.
7. Rozdzielić `JFMIORegistration` na funkcje: import requested/progress/completed/failed, export, save, load.

### Plan naprawy - model danych

1. Wydzielić `JFMCharacteristicInfo` oraz `JFMCharacteristicData`.
2. Zostawić workspace jako jedynego właściciela projektu i registry charakterystyk.
3. Zmienić serie tak, aby przechowywały `CharacteristicId`, nie pełne obiekty z raw arrays.
4. Dodać API workspace: `AddSeries`, `AddCharacteristics`, `FindCharacteristic`, `FindCharacteristicByName`, `GetSelectedCharacteristic`, `DeleteCharacteristic`, `MoveCharacteristic`.
5. Przenieść/zmienić nazwę `Storage/JFMWorkspaceStore` na `Workspace` albo `State`.
6. Zmienić `sourcePath` w runtime na `Path`; serializer mapuje `Path <-> string`.

### Plan naprawy - save/load i import

1. Zachować zasadę: raw arrays nie są serializowane inline w YAML.
2. Po `ProjectLoadCompleted` uruchamiać background job do materializacji danych z `source_path`.
3. Jeśli plik źródłowy nie istnieje, zostawić charakterystykę w drzewie z warningiem i bez raw arrays.
4. Import folderu: każdy folder jako osobna seria.
5. Import/export/save/load failure publikuje `NotificationRequestedEvent` oraz aktualizuje status workspace.
6. Grupować joby JFM IO przez spójny `source/type`, bez nowej hierarchii klas jeśli nie jest potrzebna.

### Plan UI po fundamentach

1. Context menu: projekt `import series/add series`, seria `delete/add characteristics`, charakterystyka `delete`.
2. Multi-select charakterystyk: Ctrl/Shift, później drag selection.
3. Import dialog: parser/script, nazwa charakterystyki, delimiter, decimal separator, temperature extraction.
4. Script registry w projekcie: add/remove/reload script, widoczne w Project Tree.
5. Raw plot: multi-plot serii, drag-drop z drzewa na plot area, wiele plot areas.
6. Później: kolorowanie, sortowanie po temperaturze/metadanych, panel szczegółów charakterystyki.

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
