# Versioning Policy for FastMediaExplorer

## 1. Cel wersjonowania

Numer wersji ma komunikować:

- zakres zmian w aplikacji,
- ryzyko aktualizacji dla użytkownika,
- kompatybilność z poprzednimi wersjami.

Standard oparty jest na Semantic Versioning (SemVer), w którym numer wersji ma format:

`MAJOR.MINOR.PATCH`

Każdy element niesie określone znaczenie dotyczące kompatybilności i zmian w oprogramowaniu.

## 2. Struktura wersji

Format wersji:

`MAJOR.MINOR.PATCH[-PRERELEASE]`

Przykład:

`0.3.0-alpha`

| Element | Znaczenie |
|---|---|
| `MAJOR` | zmiany niekompatybilne |
| `MINOR` | nowe funkcje |
| `PATCH` | poprawki błędów |
| `PRERELEASE` | wersja niestabilna (`alpha`/`beta`/`rc`) |

## 3. Zasady zmiany wersji

### 3.1 PATCH

Zmiana:

`X.Y.Z -> X.Y.(Z+1)`

Stosujemy gdy:

- poprawka błędu,
- poprawka stabilności,
- poprawka UI,
- refactor bez zmiany funkcjonalności.

Przykład:

`0.3.0 -> 0.3.1`

`PATCH` oznacza zmiany kompatybilne wstecz i minimalne ryzyko aktualizacji.

### 3.2 MINOR

Zmiana:

`X.Y.Z -> X.(Y+1).0`

Stosujemy gdy:

- pojawia się nowa funkcja,
- rozszerza się workflow użytkownika,
- zmienia się UI lub możliwości aplikacji,
- zmiany są kompatybilne wstecz.

Przykład:

`0.3.0 -> 0.4.0`

`MINOR` oznacza nowe funkcjonalności przy zachowaniu kompatybilności.

### 3.3 MAJOR

Zmiana:

`X.Y.Z -> (X+1).0.0`

Stosujemy gdy:

- zmiana architektury,
- zmiana API,
- zmiana formatu danych,
- breaking change dla użytkownika.

Przykład:

`1.2.3 -> 2.0.0`

`MAJOR` sygnalizuje brak kompatybilności wstecz.

## 4. Pre-release versions

Przed stabilną wersją stosujemy oznaczenia:

| Oznaczenie | Znaczenie |
|---|---|
| `alpha` | wczesna wersja rozwojowa |
| `beta` | stabilizowana funkcjonalność |
| `rc` | release candidate |

Przykład:

- `0.4.0-alpha`
- `0.4.0-beta`
- `0.4.0-rc1`

Pre-release oznacza podwyższone ryzyko i niestabilność.

## 5. Wersje 0.x (faza rozwoju)

Projekt w wersji `0.y.z` oznacza fazę rozwoju.

W tej fazie:

- breaking changes mogą pojawiać się w `MINOR`,
- API nie jest jeszcze stabilne,
- funkcjonalność może się zmieniać.

## 6. Kryteria oceny iteracji

Przed nadaniem wersji każda iteracja oceniana jest w pięciu obszarach:

| Kryterium | Pytanie |
|---|---|
| kompatybilność | czy coś przestaje działać |
| funkcjonalność | czy pojawiła się nowa funkcja |
| stabilność | czy poprawiono błędy |
| workflow | czy zmienił się sposób używania |
| ryzyko | czy aktualizacja może powodować problemy |

## 7. Klasyfikacja zmian

Zmiany klasyfikujemy według typów:

- `Added`
- `Changed`
- `Fixed`
- `Refactored`
- `Performance`
- `Docs`

## 8. Reguła nadawania wersji

Decyzja:

- `BREAK -> MAJOR`
- `ADD -> MINOR`
- `FIX -> PATCH`

| Typ zmiany | Wersja |
|---|---|
| breaking change | `MAJOR` |
| nowa funkcja | `MINOR` |
| poprawka błędu | `PATCH` |

## 9. Skoki wersji

Możliwe jest pominięcie numerów wersji, np.:

`0.3 -> 0.5`

gdy:

- pojawia się duży milestone,
- wdrażany jest duży zestaw funkcji,
- następuje reorganizacja projektu.

## 10. Changelog

Każda wersja musi mieć wpis w pliku:

`CHANGELOG.md`

Format:

```md
## 0.3.0-alpha

### Added
- Drag & drop folder support
- File -> Open folder dialog

### Changed
- Unified root switching logic

### Fixed
- Long path handling in WM_DROPFILES
```

## 11. Format release

Release powinien mieć nazwę:

`FastMediaExplorer-0.3.0-alpha-win64.zip`

## 12. Proces nadawania wersji w projekcie

Przy każdej iteracji:

1. analizujemy zmiany,
2. klasyfikujemy je,
3. oceniamy wpływ,
4. nadajemy wersję,
5. aktualizujemy changelog.

## 13. Obecny etap projektu

Aktualny model wersjonowania:

`0.MINOR.PATCH`

Przykłady:

- `0.2.0`
- `0.3.0`
- `0.3.1`
- `0.4.0`

## Podsumowanie

Wersjonowanie w projekcie FastMediaExplorer ma komunikować:

- zakres zmian,
- kompatybilność,
- ryzyko aktualizacji.

Model:

`MAJOR.MINOR.PATCH`

z pre-release:

- `-alpha`
- `-beta`
- `-rc`

zgodnie z zasadami Semantic Versioning.
