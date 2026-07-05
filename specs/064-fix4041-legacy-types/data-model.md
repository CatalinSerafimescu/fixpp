# Data Model — FIX 4.0/4.1 dictionary loader legacy-type support (064 / D-004)

**Feature**: `064-fix4041-legacy-types` · **Date**: 2026-07-05 · **Spec**: [spec.md](./spec.md)

No new entity or type is introduced. This feature appends two rows to an existing lookup table and adds
two vendored data files. The `field_data_type` enum is **frozen** (`[FIX50SP2 §3.3]`) and unchanged.

## E-1 — Field-type collapse table (`kFieldTypeTable`)

`src/dictionary/xml_loader.cpp:65-104` — a `constexpr FieldTypeEntry[]` of `{xml_name, enum_value}` rows,
scanned linearly by `resolve_field_type(name, out)` (`:107-117`). A miss returns `false`, which the loader
turns into `dict::xml_parse_error` / `dict_xml_parse_failed` (AC-L8).

**Existing carve-out** (`:97-104`) — post-canonical FIX 5.0 names collapsed to the nearest frozen enum:

| xml_name | enum_value |
|---|---|
| `TAGNUM` | `Int` |
| `LOCALMKTTIME` | `LocalMktDate` |
| `XID` | `String` |
| `XIDREF` | `String` |

**Two rows this feature ADDS** (pre-canonical legacy analogue, same block/style):

| xml_name | enum_value | Verified by | Divergence from QuickFIX |
|---|---|---|---|
| `TIME` | `field_data_type::UtcTimestamp` | QuickFIX `XMLTypeToType:675` + all 7 FIX40/41 `TIME` fields = `UTCTIMESTAMP` in FIX42/43/44 (research R2) | none — QuickFIX agrees |
| `DATE` | `field_data_type::LocalMktDate` | `TradeDate`/`FutSettDate` = `LOCALMKTDATE` in every FIX4.2+ dict (research R3); user decision | **yes** — QuickFIX returns `TYPE::Unknown` (no `DATE` branch); recorded per FR-009 |

**Invariant preserved**: `resolve_field_type` still returns `false` for any name in **neither** the
`[FIX50SP2 §3.3]` vocabulary **nor** the (now-six-row) collapse block → AC-L8 fail-closed disposition
holds for all genuinely unknown types.

**Relaxation scope**: global (not version-keyed), matching the existing collapse rows. See research R5.

## E-2 — `field_data_type` (UNCHANGED — reference only)

`include/fixpp/dict/field_ref.hpp:29` — the frozen `[FIX50SP2 §3.3]` enum. `UtcTimestamp` (`:50`) and
`LocalMktDate` (`:53`) already exist; **no variant is added**. For the Phase-1 validator, both collapse to
`field_type::String` via `field_type_from_data_type` (`field_type.hpp:98-114`) — so the E-1 additions are
metadata-only, with no inbound-validation effect (research R4).

## E-3 — Vendored dictionary files

`dictionaries/FIX40.xml`, `dictionaries/FIX41.xml` — verbatim from `quickfix/quickfix @ 19ef6a4c`
(`spec/FIX40.xml`, `spec/FIX41.xml`), checked in as data (not regenerated). Same contract as the other
eight vendored dictionaries. `dictionaries/README.md` and any refresh-recipe list updated to include them.

- **FIX 4.0**: session-bearing (pre-FIXT) — `Logon` (`A`), `Heartbeat` (`0`), `TestRequest` (`1`),
  `ResendRequest` (`2`), `Reject` (`3`), `SequenceReset` (`4`), `Logout` (`5`) live **in** this dict, plus
  app messages (`NewOrderSingle` `D`, `ExecutionReport` `8`, …).
- **FIX 4.1**: same session set + additional app messages.

## E-4 — Test parameters (`VersionParam`)

`tests/dictionary/lookup_test.cpp:33` — the existing per-version struct
(`{filename, expected_version, required_msg_types, forbidden_msg_types, required_group_no_tags,
has_clordid, parties_expected, has_instrument}`). Two rows added:

| field | FIX 4.0 row | FIX 4.1 row |
|---|---|---|
| `filename` | `FIX40.xml` | `FIX41.xml` |
| `expected_version` | `session_version::v40` | `session_version::v41` |
| `required_msg_types` | **session present**: `A`, `0` (+ an app msg e.g. `D`) | `A`, `0` (+ app msg) |
| `forbidden_msg_types` | `{}` (pre-FIXT — session lives in-dict; NOT the D-006 app-only shape) | `{}` |
| others | per what FIX40/41 actually declare (confirmed at implement against the vendored file) | same |

**Contrast with D-006**: FIX 5.0/5.0SP1 rows assert session msgtypes **forbidden** (moved to FIXT.1.1).
FIX 4.0/4.1 are the opposite — session msgtypes **required present**. Copying the D-006 shape would be a
correctness bug in the test (US2).

## State transitions

None. This is a pure lookup-table extension + data vendoring; no runtime state, no lifecycle.
