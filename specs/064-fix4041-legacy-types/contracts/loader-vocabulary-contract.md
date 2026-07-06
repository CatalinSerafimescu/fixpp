# Contract — Field-type vocabulary acceptance (064 / D-004)

**Feature**: `064-fix4041-legacy-types` · **Date**: 2026-07-05

This feature has **no external interface surface** (no public header, C-ABI symbol, wire format, or error
variant changes — FR-008). The only observable contract is the loader's **field-type acceptance set** and
its fail-closed disposition. This document states the before/after contract and the amendment checklist
Gate B verifies.

## Observable contract: `XmlLoader` field-type acceptance

For a `<field number=N name=X type=T>` element:

| `T` category | Before | After (this feature) |
|---|---|---|
| In `[FIX50SP2 §3.3]` vocabulary (`INT`, `STRING`, `PRICE`, `UTCTIMESTAMP`, `LOCALMKTDATE`, …) | accept → mapped enum | **unchanged** |
| Post-canonical collapse alias (`TAGNUM`, `LOCALMKTTIME`, `XID`, `XIDREF`) | accept → nearest enum | **unchanged** |
| **`TIME`** | **reject** → `xml_parse_error` / `dict_xml_parse_failed` | **accept** → `field_data_type::UtcTimestamp` |
| **`DATE`** | **reject** → `xml_parse_error` / `dict_xml_parse_failed` | **accept** → `field_data_type::LocalMktDate` |
| Any other name (e.g. `UNKNOWN_TYPE`) | reject → `dict_xml_parse_failed` | **unchanged (still rejects)** |

The change is **exactly two names moving from reject → accept**. No other cell changes.

## Behavioral guarantees

- **BG-1 (loadability)**: `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` load to completion with no
  `xml_parse_error`; every field resolves to a `field_data_type`. (SC-001)
- **BG-2 (fail-closed preserved)**: a `<field type='UNKNOWN_TYPE'>` still throws `xml_parse_error` with
  `.code() == dict_xml_parse_failed` — the AC-L8 witness
  (`tests/dictionary/negative_paths_test.cpp:230`) stays green, unmodified. (SC-003)
- **BG-3 (metadata-only)**: the `DATE → LocalMktDate` divergence from QuickFIX affects only
  `field_ref::type()`; no inbound value-format validation is keyed on it (both `LocalMktDate` and
  `UtcTimestamp` collapse to `field_type::String`, `field_type.hpp:98-114`), so no message QuickFIX
  accepts is rejected. (SC-005, research R4)
- **BG-4 (no surface change)**: no public/C-ABI/wire/error/layout change; the `field_data_type` enum gains
  no variant. (SC-004, FR-008)
- **BG-5 (no newer-file regression)**: no vendored FIX 4.2+ dictionary uses `DATE`/`TIME`, so the global
  relaxation changes none of their resolved types. (SC-006)

## Amendment / recording checklist (Gate B verifies each present)

| # | Site | What must land |
|---|---|---|
| 1 | `src/dictionary/xml_loader.cpp` collapse block (`:97-104`) | two rows `{"TIME", UtcTimestamp}`, `{"DATE", LocalMktDate}` with a comment citing FIX 4.0/4.1 legacy typing + the QuickFIX-divergence note for `DATE` |
| 2 | `specs/064-*/research.md` (R3/R4/R5), `data-model.md` (E-1) | the AC-L8 relaxation scope + global-not-versioned + metadata-only rationale (this bundle — already written) |
| 3 | `spec/behaviors-and-limitations.md` | a B-/L- row: "`DATE` typed `LocalMktDate` (QuickFIX = `Unknown`, no validation); metadata-only, no interop rejection" — citing `DataDictionary.cpp` `XMLTypeToType` |
| 4 | `dictionaries/README.md` (+ any refresh-recipe list) | list `FIX40.xml`/`FIX41.xml`; extend the recipe that D-006 Gate B scoped to exclude FIX40/41 (now supported) |
| 5 | `dictionaries/UPSTREAM.txt` | unchanged SHA (`19ef6a4c`) — the two files are fetched at the same pin; assert no pin drift |
| 6 | `spec/feature-catalogue.md` (D-004 rows) | flip to `done` with this PR as evidence (close-out, step 19) |

Note: unlike 060, **every** amendment site is inside this library submodule — there is **no** parent-repo
post-merge amendment split (the AC-L8 anchor, B&L, README, and catalogue all live in the submodule). The
parent-repo work at close-out is limited to the standard submodule-pointer bump + tracker/dashboard
updates (pipeline step 19), not a contract amendment.
