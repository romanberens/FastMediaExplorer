# Version Decision

## Iteration Assessed

- Previous version: `0.2.0-alpha`
- Assigned version: `0.3.0-alpha`
- Decision date: `2026-03-04`

## Decision Summary

Wersja tej iteracji zostala podniesiona do `0.3.0-alpha`, poniewaz glowny zakres zmian to nowe funkcje uzytkowe (workflow root folder), bez deklarowanego breaking change.

## Evaluation Against Policy Criteria

| Kryterium | Ocena | Argument |
|---|---|---|
| kompatybilnosc | zachowana | brak usuniecia istniejacych funkcji i skrotow; analiza duplikatow oraz istniejace menu dzialaja dalej |
| funkcjonalnosc | rozszerzona | dodano drag and drop folderu oraz `File -> Open folder...` |
| stabilnosc | poprawiona | naprawiono COM lifecycle oraz reset stanu thumbnail/duplicate przy zmianie root |
| workflow | rozszerzony | uzytkownik ma szybszy i bardziej naturalny sposob zmiany zrodla danych |
| ryzyko | umiarkowane-niskie | zmiany dotycza glownie wejscia uzytkownika i zarzadzania stanem; build `Debug/Release` przechodzi |

## Change Classification

- `Added`: nowe sciezki wejscia (`drag and drop`, `Open folder`, `Ctrl+O`).
- `Changed`: ujednolicona logika przelaczania root folder.
- `Fixed`: COM init/uninit, reset kolejki/cache miniatur, reset stanu duplikatow.
- `Docs`: aktualizacja polityki i changelog.

## SemVer Mapping

Zgodnie z regula:

- `BREAK -> MAJOR`
- `ADD -> MINOR`
- `FIX -> PATCH`

Dominujacy typ zmiany w iteracji: `ADD`, wiec bump: `0.2.0-alpha -> 0.3.0-alpha`.

## Why Still Alpha

Projekt pozostaje w fazie `alpha`, poniewaz nadal brakuje kluczowych elementow planowanych w roadmap:

- dedykowany Duplicate Groups UI,
- bezpieczne batch delete do kosza,
- trwaly cache indeksu.
