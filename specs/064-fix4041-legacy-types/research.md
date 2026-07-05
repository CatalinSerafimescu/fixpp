# Research — FIX 4.0/4.1 dictionary loader legacy-type support (064 / D-004)

**Feature**: `064-fix4041-legacy-types` · **Date**: 2026-07-05 · **Spec**: [spec.md](./spec.md)

All spec unknowns are resolved. The two mapping decisions were verified empirically and decided with the
user in-session (spec Clarifications §2026-07-05); this document consolidates the decisions plus the
evidence, so Gate A reviews from primary sources rather than the retired memory hypothesis.

---

## R1 — Which legacy type names FIX 4.0/4.1 use that the loader rejects

**Decision**: exactly two — `DATE` and `TIME`.

**Rationale / evidence**: fetched `spec/FIX40.xml` and `spec/FIX41.xml` from `quickfix/quickfix` at the
`dictionaries/UPSTREAM.txt`-pinned SHA `19ef6a4c` and enumerated every distinct `<field type="…">`
attribute (excluding the `type='FIX'` root attribute and `msgtype` noise):

| Version | Distinct field types used | Outside current vocabulary |
|---|---|---|
| FIX 4.0 | `CHAR`, `DATA`, `DATE`, `FLOAT`, `INT`, `LENGTH`, `TIME` | **`DATE`, `TIME`** |
| FIX 4.1 | + `DAYOFMONTH`, `MONTHYEAR`, `STRING` (superset of 4.0) | **`DATE`, `TIME`** |

Every other name is already in `kFieldTypeTable` (`src/dictionary/xml_loader.cpp:65-104`). So two
collapse rows make both dictionaries loadable — no other loader change is needed.

**Alternatives considered**: none — this is a factual enumeration, re-confirmed at implement time against
the vendored files (T-task) before the rows land.

---

## R2 — What `TIME` maps to

**Decision**: `TIME → field_data_type::UtcTimestamp`. Unambiguous; agrees with QuickFIX.

**Rationale / evidence** (two independent axes):
1. **Reference engine**: QuickFIX `DataDictionary::XMLTypeToType` (SHA `19ef6a4c`,
   `src/C++/DataDictionary.cpp:675-677`) maps `"TIME" → TYPE::UtcTimeStamp` explicitly.
2. **Successor typing**: all seven `TIME`-typed fields in FIX 4.0/4.1 — `SendingTime`, `TransactTime`,
   `OrigTime`, `OrigSendingTime`, `ExpireTime`, `ValidUntilTime`, `EffectiveTime` — are typed
   `UTCTIMESTAMP` uniformly (zero exceptions) in the already-vendored `FIX42.xml`/`FIX43.xml`/`FIX44.xml`.

**Alternatives considered**: `UtcTimeOnly` — rejected: FIX 4.0/4.1 `TIME` fields carry
`YYYYMMDD-HH:MM:SS` (a full timestamp), and no successor field collapses to time-only.

---

## R3 — What `DATE` maps to (the genuine fork)

**Decision**: `DATE → field_data_type::LocalMktDate`. A deliberate semantic upgrade over QuickFIX
(user decision, 2026-07-05).

**Rationale / evidence**:
- **QuickFIX has NO `DATE` branch** in `XMLTypeToType` — it falls through to `TYPE::Unknown`
  (`DataDictionary.cpp:678`), i.e. QuickFIX accepts `DATE` fields structurally and performs **no type
  validation** on them.
- The two `DATE`-typed fields (`TradeDate`, `FutSettDate`) are typed `LOCALMKTDATE` in **every** FIX 4.2+
  canonical dict (verified in vendored `FIX42.xml`/`FIX43.xml`; `FutSettDate` dropped by 4.4, `TradeDate`
  stays `LOCALMKTDATE`). Mapping to `LocalMktDate` gives our typed reads real date semantics rather than
  leaving the fields untyped.
- The retired memory hypothesis `DATE → UtcDateOnly` is wrong on both readings (neither QuickFIX's
  `Unknown` nor the successor `LocalMktDate`); it is explicitly **not** adopted.

**Divergence recorded**: because we ascribe a type QuickFIX does not, FR-009 requires a
behaviors-and-limitations row citing the QuickFIX `XMLTypeToType` anchor.

**Alternatives considered**: `DATE → DialectExtension` (QuickFIX-faithful "untyped" sentinel) — rejected
by the user: it leaves `TradeDate`/`FutSettDate` untyped in FIX 4.0/4.1 for no benefit, forgoing our
stronger-typing value proposition. See R4 for why the choice is behaviorally free.

---

## R4 — The `DATE` choice is metadata-only (no interop-rejection risk)

**Decision**: recorded as a resolved fact — the `LocalMktDate` vs `Unknown` divergence has **zero**
effect on inbound validation / wire acceptance.

**Rationale / evidence**: the fine-grained `field_data_type` is collapsed to a coarse `field_type`
category for the Phase-1 validator by `field_type_from_data_type` (`include/fixpp/dict/field_type.hpp:
98-114`). That switch maps **both** `UtcTimestamp` (line 105) and `LocalMktDate` (line 108) — and
`UtcDateOnly`, `DialectExtension`, and the `default` — to the **same** `field_type::String`. The header
comment records that a malformed tag-52 timestamp is "undetectable to the Phase-1 validator". Therefore:
- No date/timestamp value-format check is keyed on the fine-grained enum.
- Our `LocalMktDate` typing rejects nothing that QuickFIX's `TYPE::Unknown` path accepts.
- The divergence is confined to what `field_ref::type()` **reports** to a consumer (metadata), not to
  validation or wire behavior.

This removes the only latent interop concern and is why the R3 choice is behaviorally free.

**Confirmed against consumers**: `field_data_type` is consumed by `src/dictionary/{dictionary,xml_loader}
.cpp`, `src/capi/message_write.cpp`, and `include/fixpp/dict/{field_ref,dictionary,field_type}.hpp`. No
consumer branches on the date/timestamp distinction for inbound validation (the `message_write` path is
C-ABI *write* metadata, not inbound-parse validation).

---

## R5 — Mechanism: extend the existing collapse table (not the enum)

**Decision**: add two rows to `kFieldTypeTable` (`src/dictionary/xml_loader.cpp:97-104`, the collapse
sub-block), in the exact style of the existing post-canonical rows. The `field_data_type` enum is
untouched.

**Rationale**: the table already carries a documented carve-out accepting non-`[FIX50SP2 §3.3]` names
(`TAGNUM`, `LOCALMKTTIME`, `XID`, `XIDREF` → nearest enum). Those are *post-canonical* FIX 5.0 names; the
two new rows are the *pre-canonical* legacy analogue. Same mechanism, same `resolve_field_type` linear
scan, no new code path. `field_data_type::UtcTimestamp` and `::LocalMktDate` already exist
(`include/fixpp/dict/field_ref.hpp:50,53`).

**Scope of the AC-L8 relaxation** (recorded per FR-006/FR-007):
- **AC-L8** (`specs/002-dictionary-xml-loader/data-model.md:297`, witness
  `tests/dictionary/negative_paths_test.cpp:230` `AC_L8_UnknownFieldTypeThrowsXmlParseError`) says a type
  outside `[FIX50SP2 §3.3]` throws `dict_xml_parse_failed`. After this feature the accepted set grows by
  exactly two named aliases; the fail-closed disposition is preserved for every name still outside it —
  the AC-L8 witness (`UNKNOWN_TYPE`) stays RED-on-load (still throws), unchanged.
- The relaxation is **global, not version-scoped** — consistent with the existing global collapse rows,
  which are also not gated on version. A hypothetical `FIX44.xml` using `type="DATE"` would now load; no
  vendored FIX 4.2+ file actually does (they use canonical `LOCALMKTDATE`/`UTCTIMESTAMP`), so no vendored
  file's resolved typing changes (SC-006).

**Alternatives considered**:
- *Version-scope the aliases* (accept `DATE`/`TIME` only when major<4.2) — rejected: inconsistent with the
  existing global collapse rows, adds a version-conditional branch the table has never had, and buys
  nothing (SC-006 shows no newer file regresses).
- *Add a `field_data_type` enum variant for legacy date/time* — rejected: violates the `[FIX50SP2 §3.3]`
  enum freeze for no benefit (the coarse validator collapses it to `String` anyway, R4).

---

## Confirm-at-implement obligations

1. Re-enumerate `<field type=…>` in the **vendored** `FIX40.xml`/`FIX41.xml` (post-fetch) and assert the
   only out-of-vocabulary names are `DATE`, `TIME` (guards against a fetch mismatch vs R1).
2. Verify the two vendored files are byte-identical to upstream at SHA `19ef6a4c` (vendoring discipline,
   matches D-005/006).
3. Confirm `dictionaries/README.md` and (if present) any refresh-recipe list now includes FIX 4.0/4.1
   (the D-006 Gate B scoped them OUT as unsupported; they are now supported).
